/*
 * encoding.c - UTF-8 and UTF-16LE codecs and utility functions
 *
 * Copyright 2012-2023 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>

#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"

/*
 * Allow unpaired surrogates, such as might exist in Windows-style filenames ---
 * which are normally valid UTF-16LE, but are actually treated as opaque
 * sequences of 16-bit WCHARs by Windows.  When decoding "UTF-16LE", unpaired
 * surrogates will be decoded as their surrogate codepoints; and when encoding
 * to and from "UTF-8", the encoding will actually be WTF-8 ("Wobbly
 * Transformation Format - 8-bit"), a superset of UTF-8 which permits the
 * surrogate codepoints.
 *
 * In combination with also allowing the "non-character" codepoints U+FFFE and
 * U+FFFF, the result is that every Windows-style filename can be translated to
 * a UNIX-style filename.
 *
 * Unfortunately, the converse is not true: not every UNIX filename can be
 * translated to a Windows filename.  Only UNIX filenames that are valid "WTF-8"
 * can be translated.  I considered ways to define a bijective mapping, but
 * there did not seem to be a straightforward way.  The "UTF-8b" scheme, for
 * example, would map each invalid byte 'b' to a surrogate "escape code" 'U+DC00
 * + b'.  The problem with this was that surrogate escape codes can be combined
 * to create a valid UTF-8 sequence, thus breaking the bijection by mapping
 * multiple Windows filenames to a single UNIX filename.
 */
#define ALLOW_UNPAIRED_SURROGATES	1

#define INVALID_CODEPOINT	0xFFFFFFFF
#define VALIDATE(expr)		if (validate && unlikely(!(expr))) goto invalid
#ifndef IS_SURROGATE
#define IS_SURROGATE(c)		((c) >= 0xD800 && (c) < 0xE000)
#endif
#ifndef IS_HIGH_SURROGATE
#define IS_HIGH_SURROGATE(c)	((c) >= 0xD800 && (c) < 0xDC00)
#endif
#ifndef IS_LOW_SURROGATE
#define IS_LOW_SURROGATE(c)	((c) >= 0xDC00 && (c) < 0xE000)
#endif
#define IS_UTF8_TAIL(c)		(((c) & 0xC0) == 0x80)

/*
 * Decode the next Unicode codepoint from the string at @in, which has
 * @remaining >= 1 bytes remaining.  Return the number of bytes consumed and
 * write the decoded codepoint to *c_ret.
 *
 * If the input might not be a valid string in the source encoding, then
 * @validate must be specified as %true, and then on invalid input the function
 * consumes at least one byte and sets *c_ret to INVALID_CODEPOINT.  If the
 * input is guaranteed to be valid, then @validate may be specified as %false.
 */
typedef unsigned (*decode_codepoint_fn)(const u8 *in, size_t remaining,
					bool validate, u32 *c_ret);

/* Encode the Unicode codepoint @c and return the number of bytes used. */
typedef unsigned (*encode_codepoint_fn)(u32 c, u8 *out);

static forceinline unsigned
utf8_decode_codepoint(const u8 *in, size_t remaining, bool validate, u32 *c_ret)
{
	if (likely(in[0] < 0x80)) { /* U+0...U+7F */
		*c_ret = in[0];
		return 1;
	}

	if (in[0] < 0xE0) { /* U+80...U+7FF */
		VALIDATE(in[0] >= 0xC2 && remaining >= 2 &&
			 IS_UTF8_TAIL(in[1]));
		*c_ret = ((u32)(in[0] & 0x1F) << 6) |
			 ((u32)(in[1] & 0x3F) << 0);
		return 2;
	}

	if (in[0] < 0xF0) { /* U+800...U+FFFF, possibly excluding surrogates */
		VALIDATE(remaining >= 3 &&
			 IS_UTF8_TAIL(in[1]) &&
			 IS_UTF8_TAIL(in[2]));
		*c_ret = ((u32)(in[0] & 0x0F) << 12) |
			 ((u32)(in[1] & 0x3F) << 6) |
			 ((u32)(in[2] & 0x3F) << 0);
		VALIDATE(*c_ret >= 0x800);
	#if !ALLOW_UNPAIRED_SURROGATES
		VALIDATE(!IS_SURROGATE(*c_ret));
	#endif
		return 3;
	}

	/* U+10000...U+10FFFF */
	VALIDATE(in[0] < 0xF8 && remaining >= 4 &&
		 IS_UTF8_TAIL(in[1]) &&
		 IS_UTF8_TAIL(in[2]) &&
		 IS_UTF8_TAIL(in[3]));
	*c_ret = ((u32)(in[0] & 0x07) << 18) |
		 ((u32)(in[1] & 0x3F) << 12) |
		 ((u32)(in[2] & 0x3F) << 6) |
		 ((u32)(in[3] & 0x3F) << 0);
	VALIDATE(*c_ret >= 0x10000 && *c_ret <= 0x10FFFF);
	return 4;

invalid:
	*c_ret = INVALID_CODEPOINT;
	return 1;
}

static forceinline unsigned
utf8_encode_codepoint(u32 c, u8 *out)
{
	if (likely(c < 0x80)) {
		out[0] = c;
		return 1;
	}

	if (c < 0x800) {
		out[0] = 0xC0 | (c >> 6);
		out[1] = 0x80 | (c & 0x3F);
		return 2;
	}

	if (c < 0x10000) {
		out[0] = 0xE0 | (c >> 12);
		out[1] = 0x80 | ((c >> 6) & 0x3F);
		out[2] = 0x80 | (c & 0x3F);
		return 3;
	}

	out[0] = 0xF0 | (c >> 18);
	out[1] = 0x80 | ((c >> 12) & 0x3F);
	out[2] = 0x80 | ((c >> 6) & 0x3F);
	out[3] = 0x80 | (c & 0x3F);
	return 4;
}

static forceinline unsigned
utf16le_decode_codepoint(const u8 *in, size_t remaining, bool validate,
			 u32 *c_ret)
{
	u32 h, l;

	VALIDATE(remaining >= 2);
	h = get_unaligned_le16(in);
	if (unlikely(IS_SURROGATE(h))) {
		/* Surrogate pairs are U+10000...U+10FFFF.
		 * Unpaired surrogates are U+D800...U+DFFF. */
	#if ALLOW_UNPAIRED_SURROGATES
		if (unlikely(!IS_HIGH_SURROGATE(h) || remaining < 4))
			goto unpaired;
		l = get_unaligned_le16(in + 2);
		if (unlikely(!IS_LOW_SURROGATE(l)))
			goto unpaired;
	#else
		VALIDATE(IS_HIGH_SURROGATE(h) && remaining >= 4);
		l = get_unaligned_le16(in + 2);
		VALIDATE(IS_LOW_SURROGATE(l));
	#endif
		*c_ret = 0x10000 + ((h - 0xD800) << 10) + (l - 0xDC00);
		return 4;
	}
#if ALLOW_UNPAIRED_SURROGATES
unpaired:
#endif
	*c_ret = h;
	return 2;

invalid:
	*c_ret = INVALID_CODEPOINT;
	return min(remaining, 2);
}

static forceinline unsigned
utf16le_encode_codepoint(u32 c, u8 *out)
{
	if (likely(c < 0x10000)) {
		put_unaligned_le16(c, out);
		return 2;
	}
	c -= 0x10000;
	put_unaligned_le16(0xD800 + (c >> 10), out);
	put_unaligned_le16(0xDC00 + (c & 0x3FF), out + 2);
	return 4;
}

/*
 * Convert the string @in of size @in_nbytes from the encoding given by the
 * @decode_codepoint function to the encoding given by the @encode_codepoint
 * function.  @in does not need to be null-terminated, but a null terminator
 * will be added to the output string.
 *
 * On success, write the allocated output string to @out_ret (must not be NULL)
 * and its size excluding the null terminator to @out_nbytes_ret (may be NULL).
 *
 * If the input string is malformed, return @ilseq_err with errno set to EILSEQ.
 * If out of memory, return WIMLIB_ERR_NOMEM with errno set to ENOMEM.
 */
static forceinline int
convert_string(const u8 * const in, const size_t in_nbytes,
	       u8 **out_ret, size_t *out_nbytes_ret,
	       int ilseq_err,
	       decode_codepoint_fn decode_codepoint,
	       encode_codepoint_fn encode_codepoint)
{
	size_t i;
	u8 *p_out;
	size_t out_nbytes = 0;
	u8 *out;
	u8 tmp[8]; /* assuming no codepoint requires > 8 bytes to encode */
	u32 c;

	/* Validate the input string and compute the output size. */
	for (i = 0; i < in_nbytes; ) {
		i += (*decode_codepoint)(&in[i], in_nbytes - i, true, &c);
		if (unlikely(c == INVALID_CODEPOINT)) {
			errno = EILSEQ;
			return ilseq_err;
		}
		out_nbytes += (*encode_codepoint)(c, tmp);
	}

	/* Allocate the output string, including space for a null terminator. */
	out = MALLOC(out_nbytes + (*encode_codepoint)(0, tmp));
	if (unlikely(!out))
		return WIMLIB_ERR_NOMEM;

	/* Do the conversion. */
	p_out = out;
	for (i = 0; i < in_nbytes; ) {
		i += (*decode_codepoint)(&in[i], in_nbytes - i, false, &c);
		p_out += (*encode_codepoint)(c, p_out);
	}

	/* Add a null terminator. */
	(*encode_codepoint)(0, p_out);

	/* Return the output string and its size (by reference). */
	*out_ret = out;
	if (out_nbytes_ret)
		*out_nbytes_ret = out_nbytes;
	return 0;
}

int
utf8_to_utf16le(const char *in, size_t in_nbytes,
		utf16lechar **out_ret, size_t *out_nbytes_ret)
{
	return convert_string((const u8 *)in, in_nbytes,
			      (u8 **)out_ret, out_nbytes_ret,
			      WIMLIB_ERR_INVALID_UTF8_STRING,
			      utf8_decode_codepoint, utf16le_encode_codepoint);
}

int
utf16le_to_utf8(const utf16lechar *in, size_t in_nbytes,
		char **out_ret, size_t *out_nbytes_ret)
{
	return convert_string((const u8 *)in, in_nbytes,
			      (u8 **)out_ret, out_nbytes_ret,
			      WIMLIB_ERR_INVALID_UTF16_STRING,
			      utf16le_decode_codepoint, utf8_encode_codepoint);
}

/*
 * A table that maps from UCS-2 characters to their upper case equivalents.
 * Index and array values are both CPU endian.
 * Note: this is only an *approximation* of real UTF-16 case folding.
 */
u16 upcase[65536];

void
init_upcase(void)
{
	/* This is the table used in NTFS volumes formatted by Windows 10.
	 * It was compressed by tools/compress_upcase_table.c.  */
	static const u16 upcase_compressed[] = {
		0x0000, 0x0000, 0x0060, 0x0000, 0x0000, 0xffe0, 0x0019, 0x0061,
		0x0061, 0x0000, 0x001b, 0x005d, 0x0008, 0x0060, 0x0000, 0x0079,
		0x0000, 0x0000, 0x0000, 0xffff, 0x002f, 0x0100, 0x0002, 0x0000,
		0x0007, 0x012b, 0x0011, 0x0121, 0x002f, 0x0103, 0x0006, 0x0101,
		0x0000, 0x00c3, 0x0006, 0x0131, 0x0007, 0x012e, 0x0004, 0x0000,
		0x0003, 0x012f, 0x0000, 0x0061, 0x0004, 0x0130, 0x0000, 0x00a3,
		0x0003, 0x0000, 0x0000, 0x0082, 0x000b, 0x0131, 0x0006, 0x0189,
		0x0008, 0x012f, 0x0007, 0x012e, 0x0000, 0x0038, 0x0006, 0x0000,
		0x0000, 0xfffe, 0x0007, 0x01c4, 0x000f, 0x0101, 0x0000, 0xffb1,
		0x0015, 0x011e, 0x0004, 0x01cc, 0x002a, 0x0149, 0x0014, 0x0149,
		0x0007, 0x0000, 0x0009, 0x018c, 0x000b, 0x0138, 0x0000, 0x2a1f,
		0x0000, 0x2a1c, 0x0000, 0x0000, 0x0000, 0xff2e, 0x0000, 0xff32,
		0x0000, 0x0000, 0x0000, 0xff33, 0x0000, 0xff33, 0x0000, 0x0000,
		0x0000, 0xff36, 0x0000, 0x0000, 0x0000, 0xff35, 0x0004, 0x0000,
		0x0002, 0x0257, 0x0000, 0x0000, 0x0000, 0xff31, 0x0004, 0x0000,
		0x0000, 0xff2f, 0x0000, 0xff2d, 0x0000, 0x0000, 0x0000, 0x29f7,
		0x0003, 0x0000, 0x0002, 0x0269, 0x0000, 0x29fd, 0x0000, 0xff2b,
		0x0002, 0x0000, 0x0000, 0xff2a, 0x0007, 0x0000, 0x0000, 0x29e7,
		0x0002, 0x0000, 0x0000, 0xff26, 0x0005, 0x027e, 0x0003, 0x027e,
		0x0000, 0xffbb, 0x0000, 0xff27, 0x0000, 0xff27, 0x0000, 0xffb9,
		0x0005, 0x0000, 0x0000, 0xff25, 0x0065, 0x007b, 0x0079, 0x0293,
		0x0008, 0x012d, 0x0003, 0x019c, 0x0002, 0x037b, 0x002e, 0x0000,
		0x0000, 0xffda, 0x0000, 0xffdb, 0x0002, 0x03ad, 0x0012, 0x0060,
		0x000a, 0x0060, 0x0000, 0xffc0, 0x0000, 0xffc1, 0x0000, 0xffc1,
		0x0008, 0x0000, 0x0000, 0xfff8, 0x001a, 0x0118, 0x0000, 0x0007,
		0x0008, 0x018d, 0x0009, 0x0233, 0x0046, 0x0035, 0x0006, 0x0061,
		0x0000, 0xffb0, 0x000f, 0x0450, 0x0025, 0x010e, 0x000a, 0x036b,
		0x0032, 0x048b, 0x000e, 0x0100, 0x0000, 0xfff1, 0x0037, 0x048a,
		0x0026, 0x0465, 0x0034, 0x0000, 0x0000, 0xffd0, 0x0025, 0x0561,
		0x00de, 0x0293, 0x1714, 0x0587, 0x0000, 0x8a04, 0x0003, 0x0000,
		0x0000, 0x0ee6, 0x0087, 0x02ee, 0x0092, 0x1e01, 0x0069, 0x1df7,
		0x0000, 0x0008, 0x0007, 0x1f00, 0x0008, 0x0000, 0x000e, 0x1f02,
		0x0008, 0x1f0e, 0x0010, 0x1f06, 0x001a, 0x1f06, 0x0002, 0x1f0f,
		0x0007, 0x1f50, 0x0017, 0x1f19, 0x0000, 0x004a, 0x0000, 0x004a,
		0x0000, 0x0056, 0x0003, 0x1f72, 0x0000, 0x0064, 0x0000, 0x0064,
		0x0000, 0x0080, 0x0000, 0x0080, 0x0000, 0x0070, 0x0000, 0x0070,
		0x0000, 0x007e, 0x0000, 0x007e, 0x0028, 0x1f1e, 0x000c, 0x1f06,
		0x0000, 0x0000, 0x0000, 0x0009, 0x000f, 0x0000, 0x000d, 0x1fb3,
		0x000d, 0x1f44, 0x0008, 0x1fcd, 0x0006, 0x03f2, 0x0015, 0x1fbb,
		0x014e, 0x0587, 0x0000, 0xffe4, 0x0021, 0x0000, 0x0000, 0xfff0,
		0x000f, 0x2170, 0x000a, 0x0238, 0x0346, 0x0587, 0x0000, 0xffe6,
		0x0019, 0x24d0, 0x0746, 0x0587, 0x0026, 0x0561, 0x000b, 0x057e,
		0x0004, 0x012f, 0x0000, 0xd5d5, 0x0000, 0xd5d8, 0x000c, 0x022e,
		0x000e, 0x03f8, 0x006e, 0x1e33, 0x0011, 0x0000, 0x0000, 0xe3a0,
		0x0025, 0x2d00, 0x17f2, 0x0587, 0x6129, 0x2d26, 0x002e, 0x0201,
		0x002a, 0x1def, 0x0098, 0xa5b7, 0x0040, 0x1dff, 0x000e, 0x0368,
		0x000d, 0x022b, 0x034c, 0x2184, 0x5469, 0x2d26, 0x007f, 0x0061,
		0x0040, 0x0000,
	};

	/* Simple LZ decoder  */
	const u16 *in_next = upcase_compressed;
	for (u32 i = 0; i < ARRAY_LEN(upcase); ) {
		u16 length = *in_next++;
		u16 src_pos = *in_next++;
		if (length == 0) {
			/* Literal */
			upcase[i++] = src_pos;
		} else {
			/* Match */
			do {
				upcase[i++] = upcase[src_pos++];
			} while (--length);
		}
	}

	/* Delta filter  */
	for (u32 i = 0; i < ARRAY_LEN(upcase); i++)
		upcase[i] += i;
}

/*
 * Compare UTF-16LE strings case-sensitively (%ignore_case == false) or
 * case-insensitively (%ignore_case == true).
 *
 * This is implemented using the default upper-case table used by NTFS.  It does
 * not handle all possible cases allowed by UTF-16LE.  For example, different
 * normalizations of the same sequence of "characters" are not considered equal.
 * It hopefully does the right thing most of the time though.
 */
int
cmp_utf16le_strings(const utf16lechar *s1, size_t n1,
		    const utf16lechar *s2, size_t n2,
		    bool ignore_case)
{
	size_t n = min(n1, n2);

	if (ignore_case) {
		for (size_t i = 0; i < n; i++) {
			u16 c1 = upcase[le16_to_cpu(s1[i])];
			u16 c2 = upcase[le16_to_cpu(s2[i])];
			if (c1 != c2)
				return (c1 < c2) ? -1 : 1;
		}
	} else {
		for (size_t i = 0; i < n; i++) {
			u16 c1 = le16_to_cpu(s1[i]);
			u16 c2 = le16_to_cpu(s2[i]);
			if (c1 != c2)
				return (c1 < c2) ? -1 : 1;
		}
	}
	if (n1 == n2)
		return 0;
	return (n1 < n2) ? -1 : 1;
}

/* Like cmp_utf16le_strings(), but assumes the strings are null terminated.  */
int
cmp_utf16le_strings_z(const utf16lechar *s1, const utf16lechar *s2,
		      bool ignore_case)
{
	if (ignore_case) {
		for (;;) {
			u16 c1 = upcase[le16_to_cpu(*s1)];
			u16 c2 = upcase[le16_to_cpu(*s2)];
			if (c1 != c2)
				return (c1 < c2) ? -1 : 1;
			if (c1 == 0)
				return 0;
			s1++, s2++;
		}
	} else {
		while (*s1 && *s1 == *s2)
			s1++, s2++;
		if (*s1 == *s2)
			return 0;
		return (le16_to_cpu(*s1) < le16_to_cpu(*s2)) ? -1 : 1;
	}
}

/* Duplicate a UTF-16 string.  The input string might not be null terminated and
 * might be misaligned, but the returned string is guaranteed to be null
 * terminated and properly aligned.  */
utf16lechar *
utf16le_dupz(const void *s, size_t size)
{
	utf16lechar *dup = MALLOC(size + sizeof(utf16lechar));
	if (dup) {
		memcpy(dup, s, size);
		dup[size / sizeof(utf16lechar)] = 0;
	}
	return dup;
}

/* Duplicate a null-terminated UTF-16 string.  */
utf16lechar *
utf16le_dup(const utf16lechar *s)
{
	return memdup(s, utf16le_len_bytes(s) + sizeof(utf16lechar));
}

/* Return the length, in bytes, of a null terminated UTF-16 string, excluding
 * the null terminator.  */
size_t
utf16le_len_bytes(const utf16lechar *s)
{
	const utf16lechar *p = s;
	while (*p)
		p++;
	return (p - s) * sizeof(utf16lechar);
}

/* Return the length, in UTF-16 coding units, of a null terminated UTF-16
 * string, excluding the null terminator.  */
size_t
utf16le_len_chars(const utf16lechar *s)
{
	return utf16le_len_bytes(s) / sizeof(utf16lechar);
}

#ifdef ENABLE_TEST_SUPPORT

#include "wimlib/test_support.h"

WIMLIBAPI int
wimlib_utf8_to_utf16le(const char *in, size_t in_nbytes,
		       utf16lechar **out_ret, size_t *out_nbytes_ret)
{
	return utf8_to_utf16le(in, in_nbytes, out_ret, out_nbytes_ret);
}

WIMLIBAPI int
wimlib_utf16le_to_utf8(const utf16lechar *in, size_t in_nbytes,
		       char **out_ret, size_t *out_nbytes_ret)
{
	return utf16le_to_utf8(in, in_nbytes, out_ret, out_nbytes_ret);
}
#endif /* ENABLE_TEST_SUPPORT */
