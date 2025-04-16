/*
 * matchfinder_common.h - common code for Lempel-Ziv matchfinding
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _WIMLIB_MATCHFINDER_COMMON_H
#define _WIMLIB_MATCHFINDER_COMMON_H

#include "wimlib/bitops.h"
#include "wimlib/unaligned.h"

/*
 * Given a 32-bit value that was loaded with the platform's native endianness,
 * return a 32-bit value whose high-order 8 bits are 0 and whose low-order 24
 * bits contain the first 3 bytes, arranged in octets in a platform-dependent
 * order, at the memory location from which the input 32-bit value was loaded.
 */
static forceinline u32
loaded_u32_to_u24(u32 v)
{
	if (CPU_IS_LITTLE_ENDIAN())
		return v & 0xFFFFFF;
	else
		return v >> 8;
}

/*
 * Load the next 3 bytes from @p into the 24 low-order bits of a 32-bit value.
 * The order in which the 3 bytes will be arranged as octets in the 24 bits is
 * platform-dependent.  At least 4 bytes (not 3) must be available at @p.
 */
static forceinline u32
load_u24_unaligned(const u8 *p)
{
#if UNALIGNED_ACCESS_IS_FAST
	return loaded_u32_to_u24(load_u32_unaligned(p));
#else
	if (CPU_IS_LITTLE_ENDIAN())
		return ((u32)p[0] << 0) | ((u32)p[1] << 8) | ((u32)p[2] << 16);
	else
		return ((u32)p[2] << 0) | ((u32)p[1] << 8) | ((u32)p[0] << 16);
#endif
}

/*
 * The hash function: given a sequence prefix held in the low-order bits of a
 * 32-bit value, multiply by a carefully-chosen large constant.  Discard any
 * bits of the product that don't fit in a 32-bit value, but take the
 * next-highest @num_bits bits of the product as the hash value, as those have
 * the most randomness.
 */
static forceinline u32
lz_hash(u32 seq, unsigned num_bits)
{
	return (u32)(seq * 0x1E35A7BD) >> (32 - num_bits);
}

/*
 * Return the number of bytes at @matchptr that match the bytes at @strptr, up
 * to a maximum of @max_len.  Initially, @start_len bytes are matched.
 */
static forceinline unsigned
lz_extend(const u8 * const strptr, const u8 * const matchptr,
	  const unsigned start_len, const unsigned max_len)
{
	unsigned len = start_len;
	machine_word_t v_word;

	if (UNALIGNED_ACCESS_IS_FAST) {

		if (likely(max_len - len >= 4 * WORDBYTES)) {

		#define COMPARE_WORD_STEP				\
			v_word = load_word_unaligned(&matchptr[len]) ^	\
				 load_word_unaligned(&strptr[len]);	\
			if (v_word != 0)				\
				goto word_differs;			\
			len += WORDBYTES;				\

			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
		#undef COMPARE_WORD_STEP
		}

		while (len + WORDBYTES <= max_len) {
			v_word = load_word_unaligned(&matchptr[len]) ^
				 load_word_unaligned(&strptr[len]);
			if (v_word != 0)
				goto word_differs;
			len += WORDBYTES;
		}
	}

	while (len < max_len && matchptr[len] == strptr[len])
		len++;
	return len;

word_differs:
	if (CPU_IS_LITTLE_ENDIAN())
		len += (bsfw(v_word) >> 3);
	else
		len += (WORDBITS - 1 - bsrw(v_word)) >> 3;
	return len;
}

#endif /* _WIMLIB_MATCHFINDER_COMMON_H */
