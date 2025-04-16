#ifndef _WIMLIB_ENCODING_H
#define _WIMLIB_ENCODING_H

#include <string.h>

#include "wimlib/error.h"
#include "wimlib/util.h"
#include "wimlib/types.h"

/* String conversion functions */

int
utf8_to_utf16le(const char *in, size_t in_nbytes,
		utf16lechar **out_ret, size_t *out_nbytes_ret);

int
utf16le_to_utf8(const utf16lechar *in, size_t in_nbytes,
		char **out_ret, size_t *out_nbytes_ret);

/* Identity conversion: duplicate a 'tchar' string. */
static inline int
tstr_to_tstr(const tchar *in, size_t in_nbytes,
	     tchar **out_ret, size_t *out_nbytes_ret)
{
	tchar *out = MALLOC(in_nbytes + sizeof(tchar));
	if (unlikely(!out))
		return WIMLIB_ERR_NOMEM;
	memcpy(out, in, in_nbytes);
	out[in_nbytes / sizeof(tchar)] = 0;
	*out_ret = out;
	if (out_nbytes_ret)
		*out_nbytes_ret = in_nbytes;
	return 0;
}

#if TCHAR_IS_UTF16LE

/* tstr(UTF-16LE) <=> UTF-16LE  */
#  define tstr_to_utf16le	tstr_to_tstr
#  define utf16le_to_tstr	tstr_to_tstr

/* tstr(UTF-16LE) <=> UTF-8  */
#  define tstr_to_utf8		utf16le_to_utf8
#  define utf8_to_tstr		utf8_to_utf16le

#else

/* tstr(UTF-8) <=> UTF-16LE  */
#  define tstr_to_utf16le	utf8_to_utf16le
#  define utf16le_to_tstr	utf16le_to_utf8

/* tstr(UTF-8) <=> UTF-8  */
#  define tstr_to_utf8		tstr_to_tstr
#  define utf8_to_tstr		tstr_to_tstr

#endif

/* Convert a tchar string to UTF-16LE, but if both encodings are UTF-16LE, then
 * simply re-use the string.  Release with tstr_put_utf16le() when done.  */
static inline int
tstr_get_utf16le_and_len(const tchar *in,
			 const utf16lechar **out_ret, size_t *out_nbytes_ret)
{
	size_t in_nbytes = tstrlen(in) * sizeof(tchar);
#if TCHAR_IS_UTF16LE
	*out_ret = in;
	if (out_nbytes_ret)
		*out_nbytes_ret = in_nbytes;
	return 0;
#else
	return tstr_to_utf16le(in, in_nbytes,
			       (utf16lechar **)out_ret, out_nbytes_ret);
#endif
}

static inline int
tstr_get_utf16le(const tchar *in, const utf16lechar **out_ret)
{
	return tstr_get_utf16le_and_len(in, out_ret, NULL);
}

/* Release a string acquired with tstr_get_utf16le() or
 * tstr_get_utf16le_and_len().  */
static inline void
tstr_put_utf16le(const utf16lechar *s)
{
#if !TCHAR_IS_UTF16LE
	FREE((void *)s);
#endif
}

/* Convert a UTF-16LE string to a tchar string, but if both encodings are
 * UTF-16LE, then simply re-use the string.  Release with utf16le_put_tstr()
 * when done.  */
static inline int
utf16le_get_tstr(const utf16lechar *in, size_t in_nbytes,
		 const tchar **out_ret, size_t *out_nbytes_ret)
{
#if TCHAR_IS_UTF16LE
	*out_ret = in;
	if (out_nbytes_ret)
		*out_nbytes_ret = in_nbytes;
	return 0;
#else
	return utf16le_to_tstr(in, in_nbytes,
			       (tchar **)out_ret, out_nbytes_ret);
#endif
}

/* Release a string acquired with utf16le_get_tstr().  */
static inline void
utf16le_put_tstr(const tchar *s)
{
#if !TCHAR_IS_UTF16LE
	FREE((void *)s);
#endif
}


/* UTF-16LE utilities */

extern u16 upcase[65536];

void
init_upcase(void);

int
cmp_utf16le_strings(const utf16lechar *s1, size_t n1,
		    const utf16lechar *s2, size_t n2,
		    bool ignore_case);

int
cmp_utf16le_strings_z(const utf16lechar *s1, const utf16lechar *s2,
		      bool ignore_case);

utf16lechar *
utf16le_dupz(const void *s, size_t size);

utf16lechar *
utf16le_dup(const utf16lechar *s);

size_t
utf16le_len_bytes(const utf16lechar *s);

size_t
utf16le_len_chars(const utf16lechar *s);

#endif /* _WIMLIB_ENCODING_H */
