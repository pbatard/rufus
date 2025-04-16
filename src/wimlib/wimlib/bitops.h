/*
 * bitops.h - inline functions for bit manipulation
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

#ifndef _WIMLIB_BITOPS_H
#define _WIMLIB_BITOPS_H

#include "wimlib/compiler.h"
#include "wimlib/types.h"

/*
 * Bit Scan Reverse (BSR) - find the 0-based index (relative to the least
 * significant bit) of the *most* significant 1 bit in the input value.  The
 * input value must be nonzero!
 */

static forceinline unsigned
bsr32(u32 v)
{
	return 31 - __builtin_clz(v);
}

static forceinline unsigned
bsr64(u64 v)
{
	return 63 - __builtin_clzll(v);
}

static forceinline unsigned
bsrw(machine_word_t v)
{
	STATIC_ASSERT(WORDBITS == 32 || WORDBITS == 64);
	if (WORDBITS == 32)
		return bsr32(v);
	else
		return bsr64(v);
}

/*
 * Bit Scan Forward (BSF) - find the 0-based index (relative to the least
 * significant bit) of the *least* significant 1 bit in the input value.  The
 * input value must be nonzero!
 */

static forceinline unsigned
bsf32(u32 v)
{
	return __builtin_ctz(v);
}

static forceinline unsigned
bsf64(u64 v)
{
	return __builtin_ctzll(v);
}

static forceinline unsigned
bsfw(machine_word_t v)
{
	STATIC_ASSERT(WORDBITS == 32 || WORDBITS == 64);
	if (WORDBITS == 32)
		return bsf32(v);
	else
		return bsf64(v);
}

/* Return the log base 2 of 'n', rounded up to the nearest integer. */
static forceinline unsigned
ilog2_ceil(size_t n)
{
        if (n <= 1)
                return 0;
        return 1 + bsrw(n - 1);
}

/* Round 'n' up to the nearest power of 2 */
static forceinline size_t
roundup_pow_of_2(size_t n)
{
	return (size_t)1 << ilog2_ceil(n);
}

#endif /* _WIMLIB_BITOPS_H */
