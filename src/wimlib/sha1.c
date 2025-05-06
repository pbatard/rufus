/*
 * sha1.c - implementation of the Secure Hash Algorithm version 1 (FIPS 180-1)
 *
 * Copyright 2022-2023 Eric Biggers
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/cpu_features.h"
#include "wimlib/endianness.h"
#include "wimlib/sha1.h"
#include "wimlib/unaligned.h"

/*----------------------------------------------------------------------------*
 *                              Shared helpers                                *
 *----------------------------------------------------------------------------*/

static inline u32
rol32(u32 v, int bits)
{
	return (v << bits) | (v >> (32 - bits));
}

/* Expands to the round constant for the given round */
#define SHA1_K(i)			\
	(((i) < 20) ? 0x5A827999 :	\
	 ((i) < 40) ? 0x6ED9EBA1 :	\
	 ((i) < 60) ? 0x8F1BBCDC :	\
		      0xCA62C1D6)

/* Expands to the computation on b, c, and d for the given round */
#define SHA1_F(i, b, c, d)					\
	(((i) < 20) ? /* Choice */ (b & (c ^ d)) ^ d :		\
	 ((i) < 40) ? /* Parity */ b ^ c ^ d :			\
	 ((i) < 60) ? /* Majority */ (c & d) ^ (b & (c ^ d)) :	\
		      /* Parity */ b ^ c ^ d)

/*
 * Expands to a memory barrier for the given array, preventing values of the
 * array from being cached in registers past the barrier.  Use this to prevent
 * the compiler from making counter-productive optimizations when there aren't
 * enough registers available to hold the full array.
 */
#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_ReadWriteBarrier)
#define FORCE_NOT_CACHED(array)	_ReadWriteBarrier()
#else
#define FORCE_NOT_CACHED(array)	asm volatile("" : "+m" (array))
#endif

/*
 * Expands to FORCE_NOT_CACHED() if the architecture has 16 or fewer general
 * purpose registers, otherwise does nothing.
 */
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || defined(__arm__)
#  define FORCE_NOT_CACHED_IF_FEW_REGS(array)	FORCE_NOT_CACHED(array)
#else
#  define FORCE_NOT_CACHED_IF_FEW_REGS(array)	(void)(array)
#endif

/*----------------------------------------------------------------------------*
 *                         Generic implementation                             *
 *----------------------------------------------------------------------------*/

/*
 * This is SHA-1 in portable C code.  It computes the message schedule
 * just-in-time, in a rolling window of length 16.
 */

#define SHA1_GENERIC_ROUND(i, a, b, c, d, e)				\
	FORCE_NOT_CACHED_IF_FEW_REGS(w);				\
	if ((i) < 16)							\
		w[i] = get_unaligned_be32(data + ((i) * 4));		\
	else								\
		w[(i) % 16] = rol32(w[((i) - 16) % 16] ^		\
				    w[((i) - 14) % 16] ^		\
				    w[((i) -  8) % 16] ^		\
				    w[((i) -  3) % 16], 1);		\
	e += w[(i) % 16] + rol32(a, 5) + SHA1_F((i), b, c, d) + SHA1_K(i); \
	b = rol32(b, 30);
	/* implicit: the new (a, b, c, d, e) is the old (e, a, b, c, d) */

#define SHA1_GENERIC_5ROUNDS(i)				\
	SHA1_GENERIC_ROUND((i) + 0, a, b, c, d, e);	\
	SHA1_GENERIC_ROUND((i) + 1, e, a, b, c, d);	\
	SHA1_GENERIC_ROUND((i) + 2, d, e, a, b, c);	\
	SHA1_GENERIC_ROUND((i) + 3, c, d, e, a, b);	\
	SHA1_GENERIC_ROUND((i) + 4, b, c, d, e, a);

#define SHA1_GENERIC_20ROUNDS(i)	\
	SHA1_GENERIC_5ROUNDS((i) +  0);	\
	SHA1_GENERIC_5ROUNDS((i) +  5);	\
	SHA1_GENERIC_5ROUNDS((i) + 10);	\
	SHA1_GENERIC_5ROUNDS((i) + 15);

static void
sha1_blocks_generic(u32 h[5], const u8 *data, size_t num_blocks)
{
	do {
		u32 a = h[0];
		u32 b = h[1];
		u32 c = h[2];
		u32 d = h[3];
		u32 e = h[4];
		u32 w[16];

		SHA1_GENERIC_20ROUNDS(0);
		SHA1_GENERIC_20ROUNDS(20);
		SHA1_GENERIC_20ROUNDS(40);
		SHA1_GENERIC_20ROUNDS(60);

		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
		h[4] += e;
		data += SHA1_BLOCK_SIZE;
	} while (--num_blocks);
}

/*----------------------------------------------------------------------------*
 *                    x86 SSSE3 (and AVX+BMI2) implementation                 *
 *----------------------------------------------------------------------------*/

/*
 * This is SHA-1 using the x86 SSSE3 instructions.  A copy of it is also
 * compiled with AVX and BMI2 code generation enabled for improved performance.
 *
 * Unfortunately this isn't actually much faster than the generic
 * implementation, since only the message schedule can be vectorized, not the
 * SHA itself.  The vectorized computation of the message schedule is
 * interleaved with the scalar computation of the SHA itself.
 *
 * Specifically, 16 rounds ahead of time, the words of the message schedule are
 * calculated, the round constants are added to them, and they are stored in a
 * temporary array that the scalar code reads from later.  This is done 4 words
 * at a time, but split into 4 steps, so that one step is executed during each
 * round.  Rounds 16-31 use the usual formula 'w[i] = rol32(w[i-16] ^ w[i-14] ^
 * w[i-8] ^ w[i-3], 1)', while rounds 32-79 use the equivalent formula 'w[i] =
 * rol32(w[i-32] ^ w[i-28] ^ w[i-16] ^ w[i-6], 2)' for improved vectorization.
 *
 * During rounds 80-95, the first 16 message schedule words for the next block
 * are prepared.
 */
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>

#define SHA1_SSSE3_PRECALC(i, w0, w1, w2, w3, w4, w5, w6, w7)		\
	if ((i) % 20 == 0)						\
		k = _mm_set1_epi32(SHA1_K((i) % 80));			\
	if ((i) < 32) {							\
		/*
		 * Vectorized computation of w[i] = rol32(w[i-16] ^ w[i-14] ^
		 * w[i-8] ^ w[i-3], 1) for i...i+3, split into 4 steps.
		 * w[i-16..i+3] are in (w0, w1, w2, w3, w4).
		 */							\
		if ((i) % 4 == 0) {					\
			w4 = _mm_xor_si128(_mm_alignr_epi8(w1, w0, 8), w2);		\
			t0 = _mm_srli_si128(w3, 4);			\
		} else if ((i) % 4 == 1) {				\
			t0 = _mm_xor_si128(t0, _mm_xor_si128(w4, w0));	\
			t1 = _mm_slli_si128(t0, 12);			\
		} else if ((i) % 4 == 2) {				\
			t2 = _mm_slli_epi32(t1, 2);			\
			w4 = _mm_slli_epi32(t0, 1);			\
			t0 = _mm_srli_epi32(t0, 31);			\
			t2 = _mm_xor_si128(t2, _mm_srli_epi32(t1, 30));	\
		} else {						\
			w4 = _mm_xor_si128(w4, _mm_xor_si128(t0, t2));	\
			t0 = _mm_add_epi32(w4, k);			\
			_mm_store_si128((__m128i *)&tmp[((i) - 3) % 16], t0);	\
		}							\
	} else if ((i) < 80) {						\
		/*
		 * Vectorized computation of w[i] = rol32(w[i-32] ^ w[i-28] ^
		 * w[i-16] ^ w[i-6], 2) for i...i+3, split into 4 steps.
		 * w[i-32..i+3] are in (w4, w5, w6, w7, w0, w1, w2, w3, w4);
		 * note the reuse of w4.
		 */							\
		if ((i) % 4 == 0)					\
			w4 = _mm_xor_si128(w4, _mm_alignr_epi8(w3, w2, 8));		\
		else if ((i) % 4 == 1)					\
			w4 = _mm_xor_si128(w4, _mm_xor_si128(w5, w0));			\
		else if ((i) % 4 == 2)					\
			w4 = _mm_xor_si128(_mm_slli_epi32(w4, 2),		\
			     _mm_srli_epi32(w4, 30));			\
		else							\
			_mm_store_si128((__m128i *)&tmp[((i) - 3) % 16],\
					_mm_add_epi32(w4, k));		\
	} else if ((i) < 96) {						\
		/* Precomputation of w[0..15] for next block */		\
		if ((i) == 80 && --num_blocks != 0)			\
			data = _PTR(data + SHA1_BLOCK_SIZE);	\
		if ((i) % 4 == 0)					\
			w0 = _mm_loadu_si128(_PTR(data + (((i) - 80) * 4)));	\
		else if ((i) % 4 == 1)					\
			w0 = _mm_shuffle_epi8(w0, bswap32_mask);	\
		else if ((i) % 4 == 2)					\
			t0 = _mm_add_epi32(w0, k);			\
		else							\
			_mm_store_si128((__m128i *)&tmp[(i) - 83], t0);	\
	}

#define SHA1_SSSE3_2ROUNDS(i, a, b, c, d, e, w0, w1, w2, w3, w4, w5, w6, w7) \
	FORCE_NOT_CACHED(tmp);						\
	e += tmp[(i) % 16] + rol32(a, 5) + SHA1_F((i), b, c, d);	\
	b = rol32(b, 30);						\
	SHA1_SSSE3_PRECALC((i) + 16, w0, w1, w2, w3, w4, w5, w6, w7);	\
	FORCE_NOT_CACHED(tmp);						\
	d += tmp[((i) + 1) % 16] + rol32(e, 5) + SHA1_F((i) + 1, a, b, c); \
	SHA1_SSSE3_PRECALC((i) + 17, w0, w1, w2, w3, w4, w5, w6, w7);	\
	a = rol32(a, 30);
	/* implicit: the new (a, b, c, d, e) is the old (d, e, a, b, c) */

#define SHA1_SSSE3_4ROUNDS(i, a, b, c, d, e, w0, w1, w2, w3, w4, w5, w6, w7)	\
	SHA1_SSSE3_2ROUNDS((i) + 0, a, b, c, d, e, w0, w1, w2, w3, w4, w5, w6, w7); \
	SHA1_SSSE3_2ROUNDS((i) + 2, d, e, a, b, c, w0, w1, w2, w3, w4, w5, w6, w7); \
	/*
	 * implicit: the new (w0-w7) is the old (w1-w7,w0),
	 * and the new (a, b, c, d, e) is the old (b, c, d, e, a)
	 */

#define SHA1_SSSE3_20ROUNDS(i, w0, w1, w2, w3, w4, w5, w6, w7)		\
	SHA1_SSSE3_4ROUNDS((i) +  0, a, b, c, d, e, w0, w1, w2, w3, w4, w5, w6, w7); \
	SHA1_SSSE3_4ROUNDS((i) +  4, b, c, d, e, a, w1, w2, w3, w4, w5, w6, w7, w0); \
	SHA1_SSSE3_4ROUNDS((i) +  8, c, d, e, a, b, w2, w3, w4, w5, w6, w7, w0, w1); \
	SHA1_SSSE3_4ROUNDS((i) + 12, d, e, a, b, c, w3, w4, w5, w6, w7, w0, w1, w2); \
	SHA1_SSSE3_4ROUNDS((i) + 16, e, a, b, c, d, w4, w5, w6, w7, w0, w1, w2, w3);
	/* implicit: the new (w0-w7) is the old (w5-w7,w0-w4) */

#define SHA1_SSSE3_BODY							\
	const __m128i bswap32_mask =					\
		_mm_setr_epi8( 3,  2,  1,  0,  7,  6,  5,  4,		\
			      11, 10,  9,  8, 15, 14, 13, 12);		\
	__m128i w0, w1, w2, w3, w4, w5, w6, w7;				\
	__m128i k = _mm_set1_epi32(SHA1_K(0));				\
	PRAGMA_ALIGN(u32 tmp[16], 16);			\
									\
	w0 = _mm_shuffle_epi8(_mm_loadu_si128(_PTR(data +  0)), bswap32_mask); \
	w1 = _mm_shuffle_epi8(_mm_loadu_si128(_PTR(data + 16)), bswap32_mask); \
	w2 = _mm_shuffle_epi8(_mm_loadu_si128(_PTR(data + 32)), bswap32_mask); \
	w3 = _mm_shuffle_epi8(_mm_loadu_si128(_PTR(data + 48)), bswap32_mask); \
	_mm_store_si128((__m128i *)&tmp[0], _mm_add_epi32(w0, k));	\
	_mm_store_si128((__m128i *)&tmp[4], _mm_add_epi32(w1, k));	\
	_mm_store_si128((__m128i *)&tmp[8], _mm_add_epi32(w2, k));	\
	_mm_store_si128((__m128i *)&tmp[12], _mm_add_epi32(w3, k));	\
									\
	do {								\
		u32 a = h[0];						\
		u32 b = h[1];						\
		u32 c = h[2];						\
		u32 d = h[3];						\
		u32 e = h[4];						\
		__m128i t0, t1, t2;					\
									\
		SHA1_SSSE3_20ROUNDS(0, w0, w1, w2, w3, w4, w5, w6, w7); \
		SHA1_SSSE3_20ROUNDS(20, w5, w6, w7, w0, w1, w2, w3, w4); \
		SHA1_SSSE3_20ROUNDS(40, w2, w3, w4, w5, w6, w7, w0, w1); \
		SHA1_SSSE3_20ROUNDS(60, w7, w0, w1, w2, w3, w4, w5, w6); \
									\
		h[0] += a;						\
		h[1] += b;						\
		h[2] += c;						\
		h[3] += d;						\
		h[4] += e;						\
									\
		/* 'data' and 'num_blocks' were updated at start of round 64. */ \
	} while (num_blocks);

#define HAVE_SHA1_BLOCKS_X86_SSSE3
static void __attribute__((target("ssse3")))
sha1_blocks_x86_ssse3(u32 h[5], const void *data, size_t num_blocks)
{
	SHA1_SSSE3_BODY;
}

#define HAVE_SHA1_BLOCKS_X86_AVX_BMI2
static void __attribute__((target("avx,bmi2")))
sha1_blocks_x86_avx_bmi2(u32 h[5], const void *data, size_t num_blocks)
{
	SHA1_SSSE3_BODY;
}
#endif /* x86 SSSE3 (and AVX+BMI2) implementation */

/*----------------------------------------------------------------------------*
 *                        x86 SHA Extensions implementation                   *
 *----------------------------------------------------------------------------*/

/*
 * This is SHA-1 using the x86 SHA extensions.
 *
 * The SHA1RNDS4 instruction does most of the work.  It takes in a 128-bit
 * vector containing 'a', 'b', 'c', and 'd' (high-order to low-order), a 128-bit
 * vector containing the next 4 words of the message schedule with 'e' added to
 * the high-order word, and an immediate that identifies the current 20-round
 * section.  It does 4 rounds and updates 'a', 'b', 'c', and 'd' accordingly.
 *
 * Each SHA1RNDS4 is paired with SHA1NEXTE.  It takes in the abcd vector,
 * calculates the value of 'e' after 4 rounds, and adds it to the high-order
 * word of a vector that contains the next 4 words of the message schedule.
 *
 * Each 4 words of the message schedule for rounds 16-79 is calculated as
 * rol32(w[i-16] ^ w[i-14] ^ w[i-8] ^ w[i-3], 1) in three steps using the
 * SHA1MSG1, PXOR, and SHA1MSG2 instructions.  This happens in a rolling window,
 * so during the j'th set of 4 rounds we do the SHA1MSG2 step for j+1'th set of
 * message schedule words, PXOR for j+2'th set, and SHA1MSG1 for the j+3'th set.
 */
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>

#define SHA1_NI_4ROUNDS(i, w0, w1, w2, w3, we0, we1)			\
	if ((i) < 16)							\
		w0 = _mm_shuffle_epi8(					\
			_mm_loadu_si128(_PTR(data + ((i) * 4))), bswap_mask);	\
	if ((i) == 0)							\
		we0 = _mm_add_epi32(h_e, w0);				\
	else								\
		we0 = _mm_sha1nexte_epu32(/* old abcd */ we0, w0);	\
	we1 = abcd;							\
	if ((i) >= 12 && (i) < 76)					\
		w1 = _mm_sha1msg2_epu32(w1, w0);			\
	abcd = _mm_sha1rnds4_epu32(abcd, we0, (i) / 20);		\
	if ((i) >= 8 && (i) < 72)					\
		w2 = _mm_xor_si128(w2, w0);				\
	if ((i) >= 4 && (i) < 68)					\
		w3 = _mm_sha1msg1_epu32(w3, w0);			\
	/*
	 * implicit: the new (w0, w1, w2, w3) is the old (w1, w2, w3, w0),
	 * and the new (we0, we1) is the old (we1, we0)
	 */

#define SHA1_NI_16ROUNDS(i)					\
	SHA1_NI_4ROUNDS((i) +  0, w0, w1, w2, w3, we0, we1);	\
	SHA1_NI_4ROUNDS((i) +  4, w1, w2, w3, w0, we1, we0);	\
	SHA1_NI_4ROUNDS((i) +  8, w2, w3, w0, w1, we0, we1);	\
	SHA1_NI_4ROUNDS((i) + 12, w3, w0, w1, w2, we1, we0);

#define HAVE_SHA1_BLOCKS_X86_SHA
static void __attribute__((target("sha,sse4.1")))
sha1_blocks_x86_sha(u32 h[5], const u8 *data, size_t num_blocks)
{
	const __m128i bswap_mask =
		_mm_setr_epi8(15, 14, 13, 12, 11, 10,  9,  8,
			      7,  6,   5,  4,  3,  2,  1,  0);
	__m128i h_abcd = _mm_shuffle_epi32(
				_mm_loadu_si128((__m128i *)h), 0x1B);
	__m128i h_e = _mm_setr_epi32(0, 0, 0, h[4]);

	do {
		__m128i abcd = h_abcd;
		__m128i w0, w1, w2, w3, we0, we1;

		SHA1_NI_16ROUNDS(0);
		SHA1_NI_16ROUNDS(16);
		SHA1_NI_16ROUNDS(32);
		SHA1_NI_16ROUNDS(48);
		SHA1_NI_16ROUNDS(64);

		h_abcd = _mm_add_epi32(h_abcd, abcd);
		h_e = _mm_sha1nexte_epu32(we0, h_e);
		data += SHA1_BLOCK_SIZE;
	} while (--num_blocks);

	_mm_storeu_si128((__m128i *)h, _mm_shuffle_epi32(h_abcd, 0x1B));
	h[4] = _mm_extract_epi32(h_e, 3);
}
#endif /* x86 SHA Extensions implementation */

/*----------------------------------------------------------------------------*
 *                     ARMv8 Crypto Extensions implementation                 *
 *----------------------------------------------------------------------------*/

/*
 * This is SHA-1 using the ARMv8 Crypto Extensions.
 *
 * This does 4 rounds at a time, and it works very similarily to the x86 SHA
 * Extensions implementation.  The differences are fairly minor:
 *
 * - x86 has SHA1RNDS4 that takes an immediate that identifies the set of 20
 *   rounds, and it handles adding the round constants.  ARM has SHA1C for
 *   rounds 0-19, SHA1P for rounds 20-39 and 60-79, and SHA1M for rounds 40-59.
 *   These don't add the round constants, so that must be done separately.
 *
 * - ARM needs only two instructions, instead of x86's three, to prepare each
 *   set of 4 message schedule words: SHA1SU0 which does w[i-16] ^ w[i-14] ^
 *   w[i-8], and SHA1SU1 which XOR's in w[i-3] and rotates left by 1.
 */
#if (defined(__aarch64__) || defined(_M_ARM64)) && \
	(defined(__clang__) || defined(_MSC_VER) || (defined(__GNUC__) && __GNUC__ >= 5))

/*
 * clang's arm_neon.h used to have a bug where it only defined the SHA-1
 * intrinsics when CRYPTO (clang 12 and earlier) or SHA2 (clang 13 and 14) is
 * enabled in the main target.  This prevents them from being used in target
 * attribute functions.  Work around this by defining the macros ourselves.
 */
#if defined(__clang__) && __clang_major__ <= 15
#  ifndef __ARM_FEATURE_CRYPTO
#    define __ARM_FEATURE_CRYPTO 1
#    define DEFINED_ARM_FEATURE_CRYPTO
#  endif
#  ifndef __ARM_FEATURE_SHA2
#    define __ARM_FEATURE_SHA2 1
#    define DEFINED_ARM_FEATURE_SHA2
#  endif
#endif
#include <arm_neon.h>
#ifdef DEFINED_ARM_FEATURE_CRYPTO
#  undef __ARM_FEATURE_CRYPTO
#endif
#ifdef DEFINED_ARM_FEATURE_SHA2
#  undef __ARM_FEATURE_SHA2
#endif

/* Expands to a vector containing 4 copies of the given round's constant */
#define SHA1_CE_K(i)		\
	((i) < 20 ? k0 :	\
	 (i) < 40 ? k1 :	\
	 (i) < 60 ? k2 :	\
		    k3)

/* Expands to the appropriate instruction for the given round */
#define SHA1_CE_OP(i, abcd, e, w)			\
	((i) < 20 ? vsha1cq_u32((abcd), (e), (w)) :	\
	 (i) < 40 ? vsha1pq_u32((abcd), (e), (w)) :	\
	 (i) < 60 ? vsha1mq_u32((abcd), (e), (w)) :	\
		    vsha1pq_u32((abcd), (e), (w)))

#define SHA1_CE_4ROUNDS(i, w0, w1, w2, w3, e0, e1)	\
	tmp = vaddq_u32(w0, SHA1_CE_K(i));			\
	e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));	\
	abcd = SHA1_CE_OP((i), abcd, e0, tmp);		\
	if ((i) >= 12 && (i) < 76)			\
		w1 = vsha1su1q_u32(w1, w0);		\
	if ((i) >= 8 && (i) < 72)			\
		w2 = vsha1su0q_u32(w2, w3, w0);
	/*
	 * implicit: the new (w0, w1, w2, w3) is the old (w1, w2, w3, w0),
	 * and the new (e0, e1) is the old (e1, e0)
	 */

#define SHA1_CE_16ROUNDS(i)					\
	SHA1_CE_4ROUNDS((i) +  0, w0, w1, w2, w3, e0, e1);	\
	SHA1_CE_4ROUNDS((i) +  4, w1, w2, w3, w0, e1, e0);	\
	SHA1_CE_4ROUNDS((i) +  8, w2, w3, w0, w1, e0, e1);	\
	SHA1_CE_4ROUNDS((i) + 12, w3, w0, w1, w2, e1, e0);

#define HAVE_SHA1_BLOCKS_ARM_CE
static void
#ifdef __clang__
	/*
	 * clang has the SHA-1 instructions under "sha2".  "crypto" used to work
	 * too, but only in clang 15 and earlier.  So, use "sha2" here.
	 */
	__attribute__((target("sha2")))
#elif defined (__GNUC__)
	/* gcc wants "+crypto".  "+sha2" doesn't work. */
	__attribute__((target("+crypto")))
#endif
sha1_blocks_arm_ce(u32 h[5], const void *data, size_t num_blocks)
{
	uint32x4_t h_abcd = vld1q_u32(h);
	uint32x4_t k0 = vdupq_n_u32(SHA1_K(0));
	uint32x4_t k1 = vdupq_n_u32(SHA1_K(20));
	uint32x4_t k2 = vdupq_n_u32(SHA1_K(40));
	uint32x4_t k3 = vdupq_n_u32(SHA1_K(60));

	do {
		uint32x4_t abcd = h_abcd;
		u32 e0 = h[4], e1;
		uint32x4_t tmp, w0, w1, w2, w3;

		w0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(_PTR(data + 0))));
		w1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(_PTR(data + 16))));
		w2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(_PTR(data + 32))));
		w3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(_PTR(data + 48))));

		SHA1_CE_16ROUNDS(0);
		SHA1_CE_16ROUNDS(16);
		SHA1_CE_16ROUNDS(32);
		SHA1_CE_16ROUNDS(48);
		SHA1_CE_16ROUNDS(64);

		h_abcd = vaddq_u32(h_abcd, abcd);
		h[4] += e0;
		data = _PTR(data + SHA1_BLOCK_SIZE);
	} while (--num_blocks);

	vst1q_u32(h, h_abcd);
}
#endif /* ARMv8 Crypto Extensions implementation */

/*----------------------------------------------------------------------------*
 *                              Everything else                               *
 *----------------------------------------------------------------------------*/

static void
sha1_blocks(u32 h[5], const void *data, size_t num_blocks)
{
#ifdef HAVE_SHA1_BLOCKS_X86_SHA
	if ((cpu_features & (X86_CPU_FEATURE_SHA | X86_CPU_FEATURE_SSE4_1)) ==
	    (X86_CPU_FEATURE_SHA | X86_CPU_FEATURE_SSE4_1)) {
		sha1_blocks_x86_sha(h, data, num_blocks);
		return;
	}
#endif
#ifdef HAVE_SHA1_BLOCKS_X86_AVX_BMI2
	if ((cpu_features & (X86_CPU_FEATURE_AVX | X86_CPU_FEATURE_BMI2)) ==
	    (X86_CPU_FEATURE_AVX | X86_CPU_FEATURE_BMI2)) {
		sha1_blocks_x86_avx_bmi2(h, data, num_blocks);
		return;
	}
#endif
#ifdef HAVE_SHA1_BLOCKS_X86_SSSE3
	if (cpu_features & X86_CPU_FEATURE_SSSE3) {
		sha1_blocks_x86_ssse3(h, data, num_blocks);
		return;
	}
#endif
#ifdef HAVE_SHA1_BLOCKS_ARM_CE
	if (cpu_features & ARM_CPU_FEATURE_SHA1) {
		sha1_blocks_arm_ce(h, data, num_blocks);
		return;
	}
#endif
	sha1_blocks_generic(h, data, num_blocks);
}

/*
 * Initialize the given SHA-1 context.
 *
 * After sha1_init(), call sha1_update() zero or more times to provide the data
 * to be hashed.  Then call sha1_final() to get the resulting message digest.
 */
void
sha1_init(struct sha1_ctx *ctx)
{
	ctx->bytecount = 0;

	ctx->h[0] = 0x67452301;
	ctx->h[1] = 0xEFCDAB89;
	ctx->h[2] = 0x98BADCFE;
	ctx->h[3] = 0x10325476;
	ctx->h[4] = 0xC3D2E1F0;
}

/* Update the SHA-1 context with @len bytes of data. */
void
sha1_update(struct sha1_ctx *ctx, const void *data, size_t len)
{
	unsigned buffered = ctx->bytecount % SHA1_BLOCK_SIZE;
	size_t blocks;

	ctx->bytecount += len;

	if (buffered) {
		unsigned remaining = SHA1_BLOCK_SIZE - buffered;

		if (len < remaining) {
			memcpy(&ctx->buffer[buffered], data, len);
			return;
		}
		memcpy(&ctx->buffer[buffered], data, remaining);
		sha1_blocks(ctx->h, ctx->buffer, 1);
		data = _PTR(data + remaining);
		len -= remaining;
	}

	blocks = len / SHA1_BLOCK_SIZE;
	if (blocks) {
		sha1_blocks(ctx->h, data, blocks);
		data = _PTR(data + blocks * SHA1_BLOCK_SIZE);
		len -= blocks * SHA1_BLOCK_SIZE;
	}

	if (len)
		memcpy(ctx->buffer, data, len);
}

/* Finalize the SHA-1 operation and return the resulting message digest. */
void
sha1_final(struct sha1_ctx *ctx, u8 hash[SHA1_HASH_SIZE])
{
	unsigned buffered = ctx->bytecount % SHA1_BLOCK_SIZE;
	const be64 bitcount = cpu_to_be64(ctx->bytecount * 8);

	ctx->buffer[buffered++] = 0x80;
	if (buffered > SHA1_BLOCK_SIZE - 8) {
		// Keep Coverity happy
		if (buffered != SHA1_BLOCK_SIZE)
			memset(&ctx->buffer[buffered], 0, SHA1_BLOCK_SIZE - buffered);
		sha1_blocks(ctx->h, ctx->buffer, 1);
		buffered = 0;
	}
	memset(&ctx->buffer[buffered], 0, SHA1_BLOCK_SIZE - 8 - buffered);
	memcpy(&ctx->buffer[SHA1_BLOCK_SIZE - 8], &bitcount, 8);
	sha1_blocks(ctx->h, ctx->buffer, 1);

	put_unaligned_be32(ctx->h[0], &hash[0]);
	put_unaligned_be32(ctx->h[1], &hash[4]);
	put_unaligned_be32(ctx->h[2], &hash[8]);
	put_unaligned_be32(ctx->h[3], &hash[12]);
	put_unaligned_be32(ctx->h[4], &hash[16]);
}

/* Calculate the SHA-1 message digest of the given data. */
void
sha1(const void *data, size_t len, u8 hash[SHA1_HASH_SIZE])
{
	struct sha1_ctx ctx;

	sha1_init(&ctx);
	sha1_update(&ctx, data, len);
	sha1_final(&ctx, hash);
}

/* "Null" SHA-1 message digest containing all 0's */
const u8 zero_hash[SHA1_HASH_SIZE];

/* Build a hexadecimal string representation of a SHA-1 message digest. */
void
sprint_hash(const u8 hash[SHA1_HASH_SIZE], tchar strbuf[SHA1_HASH_STRING_LEN])
{
	int i;
	u8 high, low;

	for (i = 0; i < SHA1_HASH_SIZE; i++) {
		high = hash[i] >> 4;
		low = hash[i] & 0xF;
		strbuf[i * 2 + 0] = (high < 10 ? high + '0' : high - 10 + 'a');
		strbuf[i * 2 + 1] = (low  < 10 ? low  + '0' : low  - 10 + 'a');
	}
	strbuf[i * 2] = 0;
}
