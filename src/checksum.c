/*
 * Rufus: The Reliable USB Formatting Utility
 * Message-Digest algorithms (md5sum, sha1sum, sha256sum, sha512sum)
 * Copyright © 1998-2001 Free Software Foundation, Inc.
 * Copyright © 2004-2019 Tom St Denis
 * Copyright © 2004 g10 Code GmbH
 * Copyright © 2002-2015 Wei Dai & Igor Pavlov
 * Copyright © 2015-2021 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * SHA-1 code taken from GnuPG, as per copyrights above.
 *
 * SHA-256 taken from 7-zip's Sha256.c, itself based on Crypto++ - Public Domain
 *
 * SHA-512 modified from LibTomCrypt - Public Domain
 *
 * MD5 code from various public domain sources sharing the following
 * copyright declaration:
 *
 * This code implements the MD5 message-digest algorithm.
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 */

 /* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <windowsx.h>

#include "db.h"
#include "rufus.h"
#include "winio.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#undef BIG_ENDIAN_HOST

#define BUFFER_SIZE         (64*KB)
#define WAIT_TIME           5000

/* Blocksize for each algorithm - Must be a power of 2 */
#define MD5_BLOCKSIZE       64
#define SHA1_BLOCKSIZE      64
#define SHA256_BLOCKSIZE    64
#define SHA512_BLOCKSIZE    128
#define MAX_BLOCKSIZE       SHA512_BLOCKSIZE

/* Hashsize for each algorithm */
#define MD5_HASHSIZE        16
#define SHA1_HASHSIZE       20
#define SHA256_HASHSIZE     32
#define SHA512_HASHSIZE     64
#define MAX_HASHSIZE        SHA512_HASHSIZE

/* Number of buffers we work with */
#define NUM_BUFFERS         3   // 2 + 1 as a mere double buffered async I/O
                                // would modify the buffer being processed.

/* Globals */
char sum_str[CHECKSUM_MAX][150];
uint32_t proc_bufnum, sum_count[CHECKSUM_MAX] = { MD5_HASHSIZE, SHA1_HASHSIZE, SHA256_HASHSIZE, SHA512_HASHSIZE };
HANDLE data_ready[CHECKSUM_MAX] = { 0 }, thread_ready[CHECKSUM_MAX] = { 0 };
DWORD read_size[NUM_BUFFERS];
BOOL enable_extra_hashes = FALSE;
uint8_t ALIGNED(64) buffer[NUM_BUFFERS][BUFFER_SIZE];
extern int default_thread_priority;

/*
 * Rotate 32 or 64 bit integers by n bytes.
 * Don't bother trying to hand-optimize those, as the
 * compiler usually does a pretty good job at that.
 */
#define ROL32(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROR32(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define ROL64(a,b) (((a) << (b)) | ((a) >> (64-(b))))
#define ROR64(a,b) (((a) >> (b)) | ((a) << (64-(b))))

/*
 * SHA-256, SHA-512 common macros (use Wikipedia SHA-2 names for clarity)
 */
#define Ch(x,y,z) ((z) ^ ((x) & ((y) ^ (z))))
#define Ma(x,y,z) (((x) & (y)) | ((z) & ((x) | (y))))

/* SHA-256 constants */
static const uint32_t K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/* SHA-512 constants */
static const uint64_t K512[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/*
 * For convenience, we use a common context for all the checksum algorithms,
 * which means some elements may be unused...
 */
typedef struct ALIGNED(64) {
	uint8_t buf[MAX_BLOCKSIZE];
	uint64_t state[8];
	uint64_t bytecount;
} SUM_CONTEXT;

static void md5_init(SUM_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
}

static void sha1_init(SUM_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
	ctx->state[4] = 0xc3d2e1f0;
}

static void sha256_init(SUM_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
}

static void sha512_init(SUM_CONTEXT* ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state[0] = 0x6a09e667f3bcc908ULL;
	ctx->state[1] = 0xbb67ae8584caa73bULL;
	ctx->state[2] = 0x3c6ef372fe94f82bULL;
	ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
	ctx->state[4] = 0x510e527fade682d1ULL;
	ctx->state[5] = 0x9b05688c2b3e6c1fULL;
	ctx->state[6] = 0x1f83d9abfb41bd6bULL;
	ctx->state[7] = 0x5be0cd19137e2179ULL;
}

/* Transform the message X which consists of 16 32-bit-words (SHA-1) */
static void sha1_transform(SUM_CONTEXT *ctx, const uint8_t *data)
{
	uint32_t a, b, c, d, e, tm, x[16];

	/* get values from the chaining vars */
	a = (uint32_t)ctx->state[0];
	b = (uint32_t)ctx->state[1];
	c = (uint32_t)ctx->state[2];
	d = (uint32_t)ctx->state[3];
	e = (uint32_t)ctx->state[4];

#ifdef BIG_ENDIAN_HOST
	memcpy(x, data, sizeof(x));
#else
	{
		unsigned k;
		for (k = 0; k < 16; k += 4) {
			const uint8_t *p2 = data + k * 4;
			x[k] = read_swap32(p2);
			x[k + 1] = read_swap32(p2 + 4);
			x[k + 2] = read_swap32(p2 + 8);
			x[k + 3] = read_swap32(p2 + 12);
		}
	}
#endif

#define K1  0x5a827999L
#define K2  0x6ed9eba1L
#define K3  0x8f1bbcdcL
#define K4  0xca62c1d6L
#define F1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )
#define F2(x,y,z)   ( x ^ y ^ z )
#define F3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )
#define F4(x,y,z)   ( x ^ y ^ z )

#define M(i) ( tm = x[i&0x0f] ^ x[(i-14)&0x0f] ^ x[(i-8)&0x0f] ^ x[(i-3)&0x0f], (x[i&0x0f] = ROL32(tm,1)) )

#define SHA1STEP(a, b, c, d, e, f, k, m) do { e += ROL32(a, 5) + f(b, c, d) + k + m; \
                                              b = ROL32(b, 30); } while(0)
	SHA1STEP(a, b, c, d, e, F1, K1, x[0]);
	SHA1STEP(e, a, b, c, d, F1, K1, x[1]);
	SHA1STEP(d, e, a, b, c, F1, K1, x[2]);
	SHA1STEP(c, d, e, a, b, F1, K1, x[3]);
	SHA1STEP(b, c, d, e, a, F1, K1, x[4]);
	SHA1STEP(a, b, c, d, e, F1, K1, x[5]);
	SHA1STEP(e, a, b, c, d, F1, K1, x[6]);
	SHA1STEP(d, e, a, b, c, F1, K1, x[7]);
	SHA1STEP(c, d, e, a, b, F1, K1, x[8]);
	SHA1STEP(b, c, d, e, a, F1, K1, x[9]);
	SHA1STEP(a, b, c, d, e, F1, K1, x[10]);
	SHA1STEP(e, a, b, c, d, F1, K1, x[11]);
	SHA1STEP(d, e, a, b, c, F1, K1, x[12]);
	SHA1STEP(c, d, e, a, b, F1, K1, x[13]);
	SHA1STEP(b, c, d, e, a, F1, K1, x[14]);
	SHA1STEP(a, b, c, d, e, F1, K1, x[15]);
	SHA1STEP(e, a, b, c, d, F1, K1, M(16));
	SHA1STEP(d, e, a, b, c, F1, K1, M(17));
	SHA1STEP(c, d, e, a, b, F1, K1, M(18));
	SHA1STEP(b, c, d, e, a, F1, K1, M(19));
	SHA1STEP(a, b, c, d, e, F2, K2, M(20));
	SHA1STEP(e, a, b, c, d, F2, K2, M(21));
	SHA1STEP(d, e, a, b, c, F2, K2, M(22));
	SHA1STEP(c, d, e, a, b, F2, K2, M(23));
	SHA1STEP(b, c, d, e, a, F2, K2, M(24));
	SHA1STEP(a, b, c, d, e, F2, K2, M(25));
	SHA1STEP(e, a, b, c, d, F2, K2, M(26));
	SHA1STEP(d, e, a, b, c, F2, K2, M(27));
	SHA1STEP(c, d, e, a, b, F2, K2, M(28));
	SHA1STEP(b, c, d, e, a, F2, K2, M(29));
	SHA1STEP(a, b, c, d, e, F2, K2, M(30));
	SHA1STEP(e, a, b, c, d, F2, K2, M(31));
	SHA1STEP(d, e, a, b, c, F2, K2, M(32));
	SHA1STEP(c, d, e, a, b, F2, K2, M(33));
	SHA1STEP(b, c, d, e, a, F2, K2, M(34));
	SHA1STEP(a, b, c, d, e, F2, K2, M(35));
	SHA1STEP(e, a, b, c, d, F2, K2, M(36));
	SHA1STEP(d, e, a, b, c, F2, K2, M(37));
	SHA1STEP(c, d, e, a, b, F2, K2, M(38));
	SHA1STEP(b, c, d, e, a, F2, K2, M(39));
	SHA1STEP(a, b, c, d, e, F3, K3, M(40));
	SHA1STEP(e, a, b, c, d, F3, K3, M(41));
	SHA1STEP(d, e, a, b, c, F3, K3, M(42));
	SHA1STEP(c, d, e, a, b, F3, K3, M(43));
	SHA1STEP(b, c, d, e, a, F3, K3, M(44));
	SHA1STEP(a, b, c, d, e, F3, K3, M(45));
	SHA1STEP(e, a, b, c, d, F3, K3, M(46));
	SHA1STEP(d, e, a, b, c, F3, K3, M(47));
	SHA1STEP(c, d, e, a, b, F3, K3, M(48));
	SHA1STEP(b, c, d, e, a, F3, K3, M(49));
	SHA1STEP(a, b, c, d, e, F3, K3, M(50));
	SHA1STEP(e, a, b, c, d, F3, K3, M(51));
	SHA1STEP(d, e, a, b, c, F3, K3, M(52));
	SHA1STEP(c, d, e, a, b, F3, K3, M(53));
	SHA1STEP(b, c, d, e, a, F3, K3, M(54));
	SHA1STEP(a, b, c, d, e, F3, K3, M(55));
	SHA1STEP(e, a, b, c, d, F3, K3, M(56));
	SHA1STEP(d, e, a, b, c, F3, K3, M(57));
	SHA1STEP(c, d, e, a, b, F3, K3, M(58));
	SHA1STEP(b, c, d, e, a, F3, K3, M(59));
	SHA1STEP(a, b, c, d, e, F4, K4, M(60));
	SHA1STEP(e, a, b, c, d, F4, K4, M(61));
	SHA1STEP(d, e, a, b, c, F4, K4, M(62));
	SHA1STEP(c, d, e, a, b, F4, K4, M(63));
	SHA1STEP(b, c, d, e, a, F4, K4, M(64));
	SHA1STEP(a, b, c, d, e, F4, K4, M(65));
	SHA1STEP(e, a, b, c, d, F4, K4, M(66));
	SHA1STEP(d, e, a, b, c, F4, K4, M(67));
	SHA1STEP(c, d, e, a, b, F4, K4, M(68));
	SHA1STEP(b, c, d, e, a, F4, K4, M(69));
	SHA1STEP(a, b, c, d, e, F4, K4, M(70));
	SHA1STEP(e, a, b, c, d, F4, K4, M(71));
	SHA1STEP(d, e, a, b, c, F4, K4, M(72));
	SHA1STEP(c, d, e, a, b, F4, K4, M(73));
	SHA1STEP(b, c, d, e, a, F4, K4, M(74));
	SHA1STEP(a, b, c, d, e, F4, K4, M(75));
	SHA1STEP(e, a, b, c, d, F4, K4, M(76));
	SHA1STEP(d, e, a, b, c, F4, K4, M(77));
	SHA1STEP(c, d, e, a, b, F4, K4, M(78));
	SHA1STEP(b, c, d, e, a, F4, K4, M(79));

#undef F1
#undef F2
#undef F3
#undef F4

	/* Update chaining vars */
	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
}

/* Transform the message X which consists of 16 32-bit-words (SHA-256) */
static __inline void sha256_transform(SUM_CONTEXT *ctx, const uint8_t *data)
{
	uint32_t a, b, c, d, e, f, g, h, j, x[16];

	a = (uint32_t)ctx->state[0];
	b = (uint32_t)ctx->state[1];
	c = (uint32_t)ctx->state[2];
	d = (uint32_t)ctx->state[3];
	e = (uint32_t)ctx->state[4];
	f = (uint32_t)ctx->state[5];
	g = (uint32_t)ctx->state[6];
	h = (uint32_t)ctx->state[7];

// Nesting the ROR allows for single register compiler optimizations
#define S0(x) (ROR32(ROR32(ROR32(x,9)^(x),11)^(x),2))	// Σ0 (Sigma 0)
#define S1(x) (ROR32(ROR32(ROR32(x,14)^(x),5)^(x),6))	// Σ1 (Sigma 1)
#define s0(x) (ROR32(ROR32(x,11)^(x),7)^((x)>>3))		// σ0 (sigma 0)
#define s1(x) (ROR32(ROR32(x,2)^(x),17)^((x)>>10))		// σ1 (sigma 1)
#define BLK0(i) (x[i])
#define BLK2(i) (x[i] += s1(x[((i)-2)&15]) + x[((i)-7)&15] + s0(x[((i)-15)&15]))
#define R(a, b, c, d, e, f, g, h, i) \
	h += S1(e) + Ch(e,f,g) + K256[(i)+(j)] + (j ? BLK2(i) : BLK0(i)); \
	d += h; \
	h += S0(a) + Ma(a, b, c)
#define RX_8(i) \
	R(a, b, c, d, e, f, g, h, i);   \
	R(h, a, b, c, d, e, f, g, i+1); \
	R(g, h, a, b, c, d, e, f, i+2); \
	R(f, g, h, a, b, c, d, e, i+3); \
	R(e, f, g, h, a, b, c, d, i+4); \
	R(d, e, f, g, h, a, b, c, i+5); \
	R(c, d, e, f, g, h, a, b, i+6); \
	R(b, c, d, e, f, g, h, a, i+7)

#ifdef BIG_ENDIAN_HOST
	memcpy(x, data, sizeof(x));
#else
	{
		uint32_t k;
		for (k = 0; k < 16; k += 4) {
			const uint8_t* p2 = data + k * 4;
			x[k] = read_swap32(p2);
			x[k + 1] = read_swap32(p2 + 4);
			x[k + 2] = read_swap32(p2 + 8);
			x[k + 3] = read_swap32(p2 + 12);
		}
	}
#endif

	for (j = 0; j < 64; j += 16) {
		RX_8(0);
		RX_8(8);
	}

#undef S0
#undef S1
#undef s0
#undef s1
#undef BLK0
#undef BLK2
#undef R
#undef RX_8

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

/*
 * Transform the message X which consists of 16 64-bit-words (SHA-512)
 * This is an algorithm that *REALLY* benefits from being executed as 64-bit
 * code rather than 32-bit, as it's more than twice as fast then...
 */
static __inline void sha512_transform(SUM_CONTEXT* ctx, const uint8_t* data)
{
	uint64_t a, b, c, d, e, f, g, h, W[80];
	uint32_t i;

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];

// Nesting the ROR allows for single register compiler optimizations
#define S0(x) (ROR64(ROR64(ROR64(x,5)^(x),6)^(x),28))	// Σ0 (Sigma 0)
#define S1(x) (ROR64(ROR64(ROR64(x,23)^(x),4)^(x),14))	// Σ1 (Sigma 1)
#define s0(x) (ROR64(ROR64(x,7)^(x),1)^((x)>>7))		// σ0 (sigma 0)
#define s1(x) (ROR64(ROR64(x,42)^(x),19)^((x)>>6))		// σ1 (sigma 1)
#define R(a, b, c, d, e, f, g, h, i) \
	h += S1(e) + Ch(e, f, g) + K512[i] + W[i]; \
	d += h; \
	h += S0(a) + Ma(a, b, c)

	for (i = 0; i < 80; i++) {
		if (i < 16)
#ifdef BIG_ENDIAN_HOST
			W[i] = *((uint64_t*)&data[8 * i]));
#else
			W[i] = read_swap64(&data[8 * i]);
#endif
		else
			W[i] = s1(W[i - 2]) + W[i - 7] + s0(W[i - 15]) + W[i - 16];
	}

	for (i = 0; i < 80; i += 8) {
		R(a, b, c, d, e, f, g, h, i);
		R(h, a, b, c, d, e, f, g, i+1);
		R(g, h, a, b, c, d, e, f, i+2);
		R(f, g, h, a, b, c, d, e, i+3);
		R(e, f, g, h, a, b, c, d, i+4);
		R(d, e, f, g, h, a, b, c, i+5);
		R(c, d, e, f, g, h, a, b, i+6);
		R(b, c, d, e, f, g, h, a, i+7);
	}

#undef S0
#undef S1
#undef s0
#undef s1
#undef R

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

/* Transform the message X which consists of 16 32-bit-words (MD5) */
static void md5_transform(SUM_CONTEXT *ctx, const uint8_t *data)
{
	uint32_t a, b, c, d, x[16];

	a = (uint32_t)ctx->state[0];
	b = (uint32_t)ctx->state[1];
	c = (uint32_t)ctx->state[2];
	d = (uint32_t)ctx->state[3];

#ifdef BIG_ENDIAN_HOST
	{
		uint32_t k;
		for (k = 0; k < 16; k += 4) {
			const uint8_t *p2 = data + k * 4;
			x[k] = read_swap32(p2);
			x[k + 1] = read_swap32(p2 + 4);
			x[k + 2] = read_swap32(p2 + 8);
			x[k + 3] = read_swap32(p2 + 12);
		}
	}
#else
	memcpy(x, data, sizeof(x));
#endif

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, data, s) do { \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s),  w += x ); } while(0)

	MD5STEP(F1, a, b, c, d, x[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, x[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, x[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, x[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, x[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, x[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, x[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, x[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, x[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, x[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, x[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, x[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, x[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, x[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, x[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, x[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, x[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, x[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, x[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, x[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, x[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, x[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, x[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, x[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, x[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, x[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, x[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, x[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, x[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, x[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, x[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, x[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, x[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, x[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, x[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, x[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, x[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, x[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, x[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, x[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, x[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, x[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, x[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, x[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, x[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, x[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, x[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, x[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, x[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, x[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, x[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, x[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, x[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, x[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, x[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, x[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, x[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, x[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, x[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, x[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, x[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, x[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, x[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, x[9] + 0xeb86d391, 21);

#undef F1
#undef F2
#undef F3
#undef F4

	/* Update chaining vars */
	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
}

/* Update the message digest with the contents of the buffer (SHA-1) */
static void sha1_write(SUM_CONTEXT *ctx, const uint8_t *buf, size_t len)
{
	size_t num = ctx->bytecount & (SHA1_BLOCKSIZE - 1);

	/* Update bytecount */
	ctx->bytecount += len;

	/* Handle any leading odd-sized chunks */
	if (num) {
		uint8_t *p = ctx->buf + num;

		num = SHA1_BLOCKSIZE - num;
		if (len < num) {
			memcpy(p, buf, len);
			return;
		}
		memcpy(p, buf, num);
		sha1_transform(ctx, ctx->buf);
		buf += num;
		len -= num;
	}

	/* Process data in blocksize chunks */
	while (len >= SHA1_BLOCKSIZE) {
		PREFETCH64(buf + SHA1_BLOCKSIZE);
		sha1_transform(ctx, buf);
		buf += SHA1_BLOCKSIZE;
		len -= SHA1_BLOCKSIZE;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->buf, buf, len);
}

/* Update the message digest with the contents of the buffer (SHA-256) */
static void sha256_write(SUM_CONTEXT *ctx, const uint8_t *buf, size_t len)
{
	size_t num = ctx->bytecount & (SHA256_BLOCKSIZE - 1);

	/* Update bytecount */
	ctx->bytecount += len;

	/* Handle any leading odd-sized chunks */
	if (num) {
		uint8_t *p = ctx->buf + num;

		num = SHA256_BLOCKSIZE - num;
		if (len < num) {
			memcpy(p, buf, len);
			return;
		}
		memcpy(p, buf, num);
		sha256_transform(ctx, ctx->buf);
		buf += num;
		len -= num;
	}

	/* Process data in blocksize chunks */
	while (len >= SHA256_BLOCKSIZE) {
		PREFETCH64(buf + SHA256_BLOCKSIZE);
		sha256_transform(ctx, buf);
		buf += SHA256_BLOCKSIZE;
		len -= SHA256_BLOCKSIZE;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->buf, buf, len);
}

/* Update the message digest with the contents of the buffer (SHA-512) */
static void sha512_write(SUM_CONTEXT* ctx, const uint8_t* buf, size_t len)
{
	size_t num = ctx->bytecount & (SHA512_BLOCKSIZE - 1);

	/* Update bytecount */
	ctx->bytecount += len;

	/* Handle any leading odd-sized chunks */
	if (num) {
		uint8_t* p = ctx->buf + num;

		num = SHA512_BLOCKSIZE - num;
		if (len < num) {
			memcpy(p, buf, len);
			return;
		}
		memcpy(p, buf, num);
		sha512_transform(ctx, ctx->buf);
		buf += num;
		len -= num;
	}

	/* Process data in blocksize chunks */
	while (len >= SHA512_BLOCKSIZE) {
		PREFETCH64(buf + SHA512_BLOCKSIZE);
		sha512_transform(ctx, buf);
		buf += SHA512_BLOCKSIZE;
		len -= SHA512_BLOCKSIZE;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->buf, buf, len);
}

/* Update the message digest with the contents of the buffer (MD5) */
static void md5_write(SUM_CONTEXT *ctx, const uint8_t *buf, size_t len)
{
	size_t num = ctx->bytecount & (MD5_BLOCKSIZE - 1);

	/* Update bytecount */
	ctx->bytecount += len;

	/* Handle any leading odd-sized chunks */
	if (num) {
		uint8_t *p = ctx->buf + num;

		num = MD5_BLOCKSIZE - num;
		if (len < num) {
			memcpy(p, buf, num);
			return;
		}
		memcpy(p, buf, num);
		md5_transform(ctx, ctx->buf);
		buf += num;
		len -= num;
	}

	/* Process data in blocksize chunks */
	while (len >= MD5_BLOCKSIZE) {
		PREFETCH64(buf + MD5_BLOCKSIZE);
		md5_transform(ctx, buf);
		buf += MD5_BLOCKSIZE;
		len -= MD5_BLOCKSIZE;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->buf, buf, len);
}

/* Finalize the computation and write the digest in ctx->state[] (SHA-1) */
static void sha1_final(SUM_CONTEXT *ctx)
{
	size_t pos = ((size_t)ctx->bytecount) & (SHA1_BLOCKSIZE - 1);
	uint64_t bitcount = ctx->bytecount << 3;
	uint8_t *p;

	ctx->buf[pos++] = 0x80;

	/* Pad whatever data is left in the buffer */
	while (pos != (SHA1_BLOCKSIZE - sizeof(uint64_t))) {
		pos &= (SHA1_BLOCKSIZE - 1);
		if (pos == 0)
			sha1_transform(ctx, ctx->buf);
		ctx->buf[pos++] = 0;
	}

	/* Append to the padding the total message's length in bits and transform */
	ctx->buf[SHA1_BLOCKSIZE - 1] = (uint8_t) bitcount;
	ctx->buf[SHA1_BLOCKSIZE - 2] = (uint8_t) (bitcount >> 8);
	ctx->buf[SHA1_BLOCKSIZE - 3] = (uint8_t) (bitcount >> 16);
	ctx->buf[SHA1_BLOCKSIZE - 4] = (uint8_t) (bitcount >> 24);
	ctx->buf[SHA1_BLOCKSIZE - 5] = (uint8_t) (bitcount >> 32);
	ctx->buf[SHA1_BLOCKSIZE - 6] = (uint8_t) (bitcount >> 40);
	ctx->buf[SHA1_BLOCKSIZE - 7] = (uint8_t) (bitcount >> 48);
	ctx->buf[SHA1_BLOCKSIZE - 8] = (uint8_t) (bitcount >> 56);

	sha1_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(uint32_t*)p = (uint32_t)ctx->state[a]; p += 4; } while(0)
#else
#define X(a) do { write_swap32(p, (uint32_t)ctx->state[a]); p += 4; } while(0);
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
#undef X
}

/* Finalize the computation and write the digest in ctx->state[] (SHA-256) */
static void sha256_final(SUM_CONTEXT *ctx)
{
	size_t pos = ((size_t)ctx->bytecount) & (SHA256_BLOCKSIZE - 1);
	uint64_t bitcount = ctx->bytecount << 3;
	uint8_t *p;

	ctx->buf[pos++] = 0x80;

	/* Pad whatever data is left in the buffer */
	while (pos != (SHA256_BLOCKSIZE - sizeof(uint64_t))) {
		pos &= (SHA256_BLOCKSIZE - 1);
		if (pos == 0)
			sha256_transform(ctx, ctx->buf);
		ctx->buf[pos++] = 0;
	}

	/* Append to the padding the total message's length in bits and transform */
	ctx->buf[SHA256_BLOCKSIZE - 1] = (uint8_t) bitcount;
	ctx->buf[SHA256_BLOCKSIZE - 2] = (uint8_t) (bitcount >> 8);
	ctx->buf[SHA256_BLOCKSIZE - 3] = (uint8_t) (bitcount >> 16);
	ctx->buf[SHA256_BLOCKSIZE - 4] = (uint8_t) (bitcount >> 24);
	ctx->buf[SHA256_BLOCKSIZE - 5] = (uint8_t) (bitcount >> 32);
	ctx->buf[SHA256_BLOCKSIZE - 6] = (uint8_t) (bitcount >> 40);
	ctx->buf[SHA256_BLOCKSIZE - 7] = (uint8_t) (bitcount >> 48);
	ctx->buf[SHA256_BLOCKSIZE - 8] = (uint8_t) (bitcount >> 56);

	sha256_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(uint32_t*)p = (uint32_t)ctx->state[a]; p += 4; } while(0)
#else
#define X(a) do { write_swap32(p, (uint32_t)ctx->state[a]); p += 4; } while(0);
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
	X(5);
	X(6);
	X(7);
#undef X
}

/* Finalize the computation and write the digest in ctx->state[] (SHA-256) */
static void sha512_final(SUM_CONTEXT* ctx)
{
	size_t pos = ((size_t)ctx->bytecount) & (SHA512_BLOCKSIZE - 1);
	/* 16 EB ought to be enough for everybody... */
	uint64_t bitcount_lo = ctx->bytecount << 3;
	uint64_t bitcount_hi = ctx->bytecount >> (64 - 3);
	uint8_t* p;

	ctx->buf[pos++] = 0x80;

	/* Pad whatever data is left in the buffer */
	while (pos != (SHA512_BLOCKSIZE - (2 * sizeof(uint64_t)))) {
		pos &= (SHA512_BLOCKSIZE - 1);
		if (pos == 0)
			sha512_transform(ctx, ctx->buf);
		ctx->buf[pos++] = 0;
	}

	/* Append to the padding the total message's length in bits and transform */
	ctx->buf[SHA512_BLOCKSIZE - 1] = (uint8_t)bitcount_lo;
	ctx->buf[SHA512_BLOCKSIZE - 2] = (uint8_t)(bitcount_lo >> 8);
	ctx->buf[SHA512_BLOCKSIZE - 3] = (uint8_t)(bitcount_lo >> 16);
	ctx->buf[SHA512_BLOCKSIZE - 4] = (uint8_t)(bitcount_lo >> 24);
	ctx->buf[SHA512_BLOCKSIZE - 5] = (uint8_t)(bitcount_lo >> 32);
	ctx->buf[SHA512_BLOCKSIZE - 6] = (uint8_t)(bitcount_lo >> 40);
	ctx->buf[SHA512_BLOCKSIZE - 7] = (uint8_t)(bitcount_lo >> 48);
	ctx->buf[SHA512_BLOCKSIZE - 8] = (uint8_t)(bitcount_lo >> 56);
	ctx->buf[SHA512_BLOCKSIZE - 9] = (uint8_t)bitcount_hi;
	/* For clarity since, with a 64-bit bytecount, the following are always 0 */
	ctx->buf[SHA512_BLOCKSIZE - 10] = (uint8_t)(bitcount_hi >> 8);
	ctx->buf[SHA512_BLOCKSIZE - 11] = (uint8_t)(bitcount_hi >> 16);
	ctx->buf[SHA512_BLOCKSIZE - 12] = (uint8_t)(bitcount_hi >> 24);
	ctx->buf[SHA512_BLOCKSIZE - 13] = (uint8_t)(bitcount_hi >> 32);
	ctx->buf[SHA512_BLOCKSIZE - 14] = (uint8_t)(bitcount_hi >> 40);
	ctx->buf[SHA512_BLOCKSIZE - 15] = (uint8_t)(bitcount_hi >> 48);
	ctx->buf[SHA512_BLOCKSIZE - 16] = (uint8_t)(bitcount_hi >> 56);

	sha512_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *p = ctx->state[a]; p += 8; } while(0)
#else
#define X(a) do { write_swap64(p, ctx->state[a]); p += 8; } while(0);
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
	X(5);
	X(6);
	X(7);
#undef X
}

/* Finalize the computation and write the digest in ctx->state[] (MD5) */
static void md5_final(SUM_CONTEXT *ctx)
{
	size_t count = ((size_t)ctx->bytecount) & (MD5_BLOCKSIZE - 1);
	uint64_t bitcount = ctx->bytecount << 3;
	uint8_t *p;

	/* Set the first char of padding to 0x80.
	 * This is safe since there is always at least one byte free
	 */
	p = ctx->buf + count;
	*p++ = 0x80;

	/* Bytes of padding needed to make blocksize */
	count = (MD5_BLOCKSIZE - 1) - count;

	/* Pad out to 56 mod 64 */
	if (count < 8) {
		/* Two lots of padding: Pad the first block to blocksize */
		memset(p, 0, count);
		md5_transform(ctx, ctx->buf);

		/* Now fill the next block */
		memset(ctx->buf, 0, MD5_BLOCKSIZE - 8);
	} else {
		/* Pad block to blocksize */
		memset(p, 0, count - 8);
	}

	/* append the 64 bit count (little endian) */
	ctx->buf[MD5_BLOCKSIZE - 8] = (uint8_t) bitcount;
	ctx->buf[MD5_BLOCKSIZE - 7] = (uint8_t) (bitcount >> 8);
	ctx->buf[MD5_BLOCKSIZE - 6] = (uint8_t) (bitcount >> 16);
	ctx->buf[MD5_BLOCKSIZE - 5] = (uint8_t) (bitcount >> 24);
	ctx->buf[MD5_BLOCKSIZE - 4] = (uint8_t) (bitcount >> 32);
	ctx->buf[MD5_BLOCKSIZE - 3] = (uint8_t) (bitcount >> 40);
	ctx->buf[MD5_BLOCKSIZE - 2] = (uint8_t) (bitcount >> 48);
	ctx->buf[MD5_BLOCKSIZE - 1] = (uint8_t) (bitcount >> 56);

	md5_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { write_swap32(p, (uint32_t)ctx->state[a]); p += 4; } while(0);
#else
#define X(a) do { *(uint32_t*)p = (uint32_t)ctx->state[a]; p += 4; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
#undef X
}

//#define NULL_TEST
#ifdef NULL_TEST
// These 'null' calls are useful for testing load balancing and individual algorithm speed
static void null_init(SUM_CONTEXT *ctx) { memset(ctx, 0, sizeof(*ctx)); }
static void null_write(SUM_CONTEXT *ctx, const uint8_t *buf, size_t len) { }
static void null_final(SUM_CONTEXT *ctx) { }
#endif

typedef void sum_init_t(SUM_CONTEXT *ctx);
typedef void sum_write_t(SUM_CONTEXT *ctx, const uint8_t *buf, size_t len);
typedef void sum_final_t(SUM_CONTEXT *ctx);
sum_init_t *sum_init[CHECKSUM_MAX] = { md5_init, sha1_init , sha256_init, sha512_init };
sum_write_t *sum_write[CHECKSUM_MAX] = { md5_write, sha1_write , sha256_write, sha512_write };
sum_final_t *sum_final[CHECKSUM_MAX] = { md5_final, sha1_final , sha256_final, sha512_final };

// Compute an individual checksum without threading or buffering, for a single file
BOOL HashFile(const unsigned type, const char* path, uint8_t* sum)
{
	BOOL r = FALSE;
	SUM_CONTEXT sum_ctx = { {0} };
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD rs = 0;
	uint64_t rb;
	uint8_t buf[4096];

	if ((type >= CHECKSUM_MAX) || (path == NULL) || (sum == NULL))
		goto out;

	h = CreateFileU(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		uprintf("Could not open file: %s", WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
		goto out;
	}

	sum_init[type](&sum_ctx);
	for (rb = 0; ; rb += rs) {
		CHECK_FOR_USER_CANCEL;
		if (!ReadFile(h, buf, sizeof(buf), &rs, NULL)) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_READ_FAULT;
			uprintf("  Read error: %s", WindowsErrorString());
			goto out;
		}
		if (rs == 0)
			break;
		sum_write[type](&sum_ctx, buf, (size_t)rs);
	}
	sum_final[type](&sum_ctx);

	memcpy(sum, sum_ctx.buf, sum_count[type]);
	r = TRUE;

out:
	safe_closehandle(h);
	return r;
}

BOOL HashBuffer(const unsigned type, const uint8_t* buf, const size_t len, uint8_t* sum)
{
	BOOL r = FALSE;
	SUM_CONTEXT sum_ctx = { {0} };

	if ((type >= CHECKSUM_MAX) || (sum == NULL))
		goto out;

	sum_init[type](&sum_ctx);
	sum_write[type](&sum_ctx, buf, len);
	sum_final[type](&sum_ctx);

	memcpy(sum, sum_ctx.buf, sum_count[type]);
	r = TRUE;

out:
	return r;
}

/*
 * Checksum dialog callback
 */
INT_PTR CALLBACK ChecksumCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i, dw, dh;
	RECT rc;
	HFONT hFont;
	HDC hDC;

	switch (message) {
	case WM_INITDIALOG:
		apply_localization(IDD_CHECKSUM, hDlg);
		hDC = GetDC(hDlg);
		hFont = CreateFontA(-MulDiv(9, GetDeviceCaps(hDC, LOGPIXELSY), 72),
			0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			0, 0, PROOF_QUALITY, 0, "Courier New");
		safe_release_dc(hDlg, hDC);
		SendDlgItemMessageA(hDlg, IDC_MD5, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendDlgItemMessageA(hDlg, IDC_SHA1, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendDlgItemMessageA(hDlg, IDC_SHA256, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendDlgItemMessageA(hDlg, IDC_SHA512, WM_SETFONT, (WPARAM)hFont, TRUE);
		SetWindowTextA(GetDlgItem(hDlg, IDC_MD5), sum_str[0]);
		SetWindowTextA(GetDlgItem(hDlg, IDC_SHA1), sum_str[1]);
		SetWindowTextA(GetDlgItem(hDlg, IDC_SHA256), sum_str[2]);
		if (enable_extra_hashes)
			SetWindowTextA(GetDlgItem(hDlg, IDC_SHA512), sum_str[3]);
		else
			SetWindowTextU(GetDlgItem(hDlg, IDC_SHA512), lmprintf(MSG_311, "<Alt>-<H>"));

		// Move/Resize the controls as needed to fit our text
		hDC = GetDC(GetDlgItem(hDlg, IDC_MD5));
		SelectFont(hDC, hFont);	// Yes, you *MUST* reapply the font to the DC, even after SetWindowText!

		GetWindowRect(GetDlgItem(hDlg, IDC_MD5), &rc);
		dw = rc.right - rc.left;
		dh = rc.bottom - rc.top;
		DrawTextU(hDC, sum_str[0], -1, &rc, DT_CALCRECT);
		dw = rc.right - rc.left - dw + 12;	// Ideally we'd compute the field borders from the system, but hey...
		dh = rc.bottom - rc.top - dh + 6;
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA256), 0, 0, dw, dh, 1.0f);
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA512), 0, 0, dw, dh, 1.0f);

		GetWindowRect(GetDlgItem(hDlg, IDC_SHA1), &rc);
		dw = rc.right - rc.left;
		DrawTextU(hDC, sum_str[1], -1, &rc, DT_CALCRECT);
		dw = rc.right - rc.left - dw + 12;
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_MD5), 0, 0, dw, 0, 1.0f);
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA1), 0, 0, dw, 0, 1.0f);
		ResizeButtonHeight(hDlg, IDOK);

		safe_release_dc(GetDlgItem(hDlg, IDC_MD5), hDC);

		if (image_path != NULL) {
			for (i = (int)strlen(image_path); (i > 0) && (image_path[i] != '\\'); i--);
			SetWindowTextU(hDlg, &image_path[i + 1]);
		}
		// Set focus on the OK button
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDOK), TRUE);
		CenterDialog(hDlg, NULL);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			reset_localization(IDD_CHECKSUM);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

// Individual thread that computes one of MD5, SHA1, SHA256 or SHA512 in parallel
DWORD WINAPI IndividualSumThread(void* param)
{
	SUM_CONTEXT sum_ctx = { {0} }; // There's a memset in sum_init, but static analyzers still bug us
	uint32_t i = (uint32_t)(uintptr_t)param, j;

	sum_init[i](&sum_ctx);
	// Signal that we're ready to service requests
	if (!SetEvent(thread_ready[i]))
		goto error;

	// Wait for requests
	while (1) {
		if (WaitForSingleObject(data_ready[i], WAIT_TIME) != WAIT_OBJECT_0) {
			uprintf("Failed to wait for event for checksum thread #%d: %s", i, WindowsErrorString());
			return 1;
		}
		if (read_size[proc_bufnum] != 0) {
			sum_write[i](&sum_ctx, buffer[proc_bufnum], (size_t)read_size[proc_bufnum]);
			if (!SetEvent(thread_ready[i]))
				goto error;
		} else {
			sum_final[i](&sum_ctx);
			memset(&sum_str[i], 0, ARRAYSIZE(sum_str[i]));
			for (j = 0; j < sum_count[i]; j++) {
				sum_str[i][2 * j] = ((sum_ctx.buf[j] >> 4) < 10) ?
					((sum_ctx.buf[j] >> 4) + '0') : ((sum_ctx.buf[j] >> 4) - 0xa + 'a');
				sum_str[i][2 * j + 1] = ((sum_ctx.buf[j] & 15) < 10) ?
					((sum_ctx.buf[j] & 15) + '0') : ((sum_ctx.buf[j] & 15) - 0xa + 'a');
			}
			sum_str[i][2 * j] = 0;
			return 0;
		}
	}
error:
	uprintf("Failed to set event for checksum thread #%d: %s", i, WindowsErrorString());
	return 1;
}

DWORD WINAPI SumThread(void* param)
{
	DWORD_PTR* thread_affinity = (DWORD_PTR*)param;
	HANDLE sum_thread[CHECKSUM_MAX] = { NULL, NULL, NULL, NULL };
	DWORD wr;
	VOID* fd = NULL;
	uint64_t processed_bytes;
	int i, read_bufnum, r = -1;
	int num_checksums = CHECKSUM_MAX - (enable_extra_hashes ? 0 : 1);

	if ((image_path == NULL) || (thread_affinity == NULL))
		ExitThread(r);

	uprintf("\r\nComputing checksum for '%s'...", image_path);

	if (thread_affinity[0] != 0)
		// Use the first affinity mask, as our read thread is the least
		// CPU intensive (mostly waits on disk I/O or on the other threads)
		// whereas the OS is likely to requisition the first Core, which
		// is usually in this first mask, for other tasks.
		SetThreadAffinityMask(GetCurrentThread(), thread_affinity[0]);

	for (i = 0; i < num_checksums; i++) {
		// NB: Can't use a single manual-reset event for data_ready as we
		// wouldn't be able to ensure the event is reset before the thread
		// gets into its next wait loop
		data_ready[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
		thread_ready[i] = CreateEvent(NULL, FALSE, FALSE, NULL);
		if ((data_ready[i] == NULL) || (thread_ready[i] == NULL)) {
			uprintf("Unable to create checksum thread event: %s", WindowsErrorString());
			goto out;
		}
		sum_thread[i] = CreateThread(NULL, 0, IndividualSumThread, (LPVOID)(uintptr_t)i, 0, NULL);
		if (sum_thread[i] == NULL) {
			uprintf("Unable to start checksum thread #%d", i);
			goto out;
		}
		SetThreadPriority(sum_thread[i], default_thread_priority);
		if (thread_affinity[i+1] != 0)
			SetThreadAffinityMask(sum_thread[i], thread_affinity[i+1]);
	}

	fd = CreateFileAsync(image_path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
	if (fd == NULL) {
		uprintf("Could not open file: %s", WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
		goto out;
	}

	read_bufnum = 0;
	proc_bufnum = 1;
	read_size[proc_bufnum] = 1;	// To avoid early loop exit
	UpdateProgressWithInfoInit(hMainDialog, FALSE);

	// Start the initial read
	ReadFileAsync(fd, buffer[read_bufnum], BUFFER_SIZE);

	for (processed_bytes = 0; read_size[proc_bufnum] != 0; processed_bytes += read_size[proc_bufnum]) {
		// 0. Update the progress and check for cancel
		UpdateProgressWithInfo(OP_NOOP_WITH_TASKBAR, MSG_271, processed_bytes, img_report.image_size);
		CHECK_FOR_USER_CANCEL;

		// 1. Wait for the current read operation to complete (and update the read size)
		if ((!WaitFileAsync(fd, DRIVE_ACCESS_TIMEOUT)) ||
			(!GetSizeAsync(fd, &read_size[read_bufnum]))) {
			uprintf("Read error: %s", WindowsErrorString());
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_READ_FAULT;
			goto out;
		}

		// 2. Switch to the next reading buffer
		read_bufnum = (read_bufnum + 1) % NUM_BUFFERS;

		// 3. Launch the next asynchronous read operation
		ReadFileAsync(fd, buffer[read_bufnum], BUFFER_SIZE);

		// 4. Wait for all the sum threads to indicate that they are ready to process data
		wr = WaitForMultipleObjects(num_checksums, thread_ready, TRUE, WAIT_TIME);
		if (wr != WAIT_OBJECT_0) {
			if (wr == STATUS_TIMEOUT)
				SetLastError(ERROR_TIMEOUT);
			uprintf("Checksum threads failed to signal: %s", WindowsErrorString());
			goto out;
		}

		// 5. Set the target buffer we want to process to the buffer we just read data into
		// Note that this variable should only be updated AFTER all the threads have signalled.
		proc_bufnum = (read_bufnum + NUM_BUFFERS - 1) % NUM_BUFFERS;

		// 6. Signal the waiting threads that there is data available
		for (i = 0; i < num_checksums; i++) {
			if (!SetEvent(data_ready[i])) {
				uprintf("Could not signal checksum thread %d: %s", i, WindowsErrorString());
				goto out;
			}
		}
	}

	// Our last event with read_size=0 signaled the threads to exit - wait for that to happen
	if (WaitForMultipleObjects(num_checksums, sum_thread, TRUE, WAIT_TIME) != WAIT_OBJECT_0) {
		uprintf("Checksum threads did not finalize: %s", WindowsErrorString());
		goto out;
	}

	uprintf("  MD5:    %s", sum_str[0]);
	uprintf("  SHA1:   %s", sum_str[1]);
	uprintf("  SHA256: %s", sum_str[2]);
	if (enable_extra_hashes) {
		char c = sum_str[3][SHA512_HASHSIZE];
		sum_str[3][SHA512_HASHSIZE] = 0;
		uprintf("  SHA512: %s", sum_str[3]);
		sum_str[3][SHA512_HASHSIZE] = c;
		uprintf("          %s", &sum_str[3][SHA512_HASHSIZE]);
	}
	r = 0;

out:
	for (i = 0; i < num_checksums; i++) {
		if (sum_thread[i] != NULL)
			TerminateThread(sum_thread[i], 1);
		safe_closehandle(data_ready[i]);
		safe_closehandle(thread_ready[i]);
	}
	CloseFileAsync(fd);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
	if (r == 0)
		MyDialogBox(hMainInstance, IDD_CHECKSUM, hMainDialog, ChecksumCallback);
	ExitThread(r);
}

/*
 * The following 2 calls are used to check whether a buffer/file is in our hash DB
 */
BOOL IsBufferInDB(const unsigned char* buf, const size_t len)
{
	int i;
	uint8_t sum[32];
	if (!HashBuffer(CHECKSUM_SHA256, buf, len, sum))
		return FALSE;
	for (i = 0; i < ARRAYSIZE(sha256db); i += 32)
		if (memcmp(sum, &sha256db[i], 32) == 0)
			return TRUE;
	return FALSE;
}

BOOL IsFileInDB(const char* path)
{
	int i;
	uint8_t sum[32];
	if (!HashFile(CHECKSUM_SHA256, path, sum))
		return FALSE;
	for (i = 0; i < ARRAYSIZE(sha256db); i += 32)
		if (memcmp(sum, &sha256db[i], 32) == 0)
			return TRUE;
	return FALSE;
}

#if defined(_DEBUG)
/* Convert a lowercase hex string to binary. Returned value must be freed */
uint8_t* to_bin(const char* str)
{
	size_t i, len = safe_strlen(str);
	uint8_t val = 0, *ret = NULL;

	if ((len < 2) || (len % 2))
		return NULL;
	ret = malloc(len / 2);
	if (ret == NULL)
		return NULL;

	for (i = 0; i < len; i++) {
		val <<= 4;
		val |= ((str[i] - '0') < 0xa) ? (str[i] - '0') : (str[i] - 'a' + 0xa);
		if (i % 2)
			ret[i / 2] = val;
	}

	return ret;
}

const char* test_msg = "Did you ever hear the tragedy of Darth Plagueis The Wise? "
	"I thought not. It's not a story the Jedi would tell you. It's a Sith legend. "
	"Darth Plagueis was a Dark Lord of the Sith, so powerful and so wise he could "
	"use the Force to influence the midichlorians to create life... He had such a "
	"knowledge of the dark side that he could even keep the ones he cared about "
	"from dying. The dark side of the Force is a pathway to many abilities some "
	"consider to be unnatural. He became so powerful... the only thing he was afraid "
	"of was losing his power, which eventually, of course, he did. Unfortunately, "
	"he taught his apprentice everything he knew, then his apprentice killed him "
	"in his sleep. Ironic. He could save others from death, but not himself.";

/*
 * Yeah, I'm not gonna bother with binary arrays of hash values since
 * they have different sizes and MSVC is an ass with initializing unions.
 * Much rather copy paste from md5sum/sha#sum output from Linux and just
 * convert the string.
 */
const char* test_hash[CHECKSUM_MAX][4] = {
	{
		"d41d8cd98f00b204e9800998ecf8427e",
		"74cac558072300385f7ab4dff7465e3c",
		"f99d37d3bee20f9c0ca3204991be2698",
		"e0ea372ac14a3574167543b851d4babb"
	}, {
		"da39a3ee5e6b4b0d3255bfef95601890afd80709",
		"a5bac908bf3e51ff0036a94d43b4f3bd2d01a75d",
		"8aa6c0064b013b8a6f4e88a0421d39bbf07e2e1b",
		"09463ec0b5917706c9cb1d6b164b2582c04018e0"
	}, {
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		"62c1a97974dfe6848942794be4f2f027b5f4815e1eb76db63a30f0e290b5c1c4",
		"dbca61af192edba49ea215c49a23feee302c98cc4d2c018347fe78db572f07a5",
		"c9b43c1058bc7f7661619e9d983fc9d31356e97f9195a2405ab972d0737b11bf"
	}, {
		"cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
		"4913ace12f1169e5a5f524ef87ab8fc39dff0418851fbbbb1f609d3261b2b4072bd1746e6accb91bf38f3b1b3d59b0a60af5de67aab87b76c2456fde523efc1c",
		"33df8a16dd624cbc4613b5ae902b722411c7e90f37dd3947c9a86e01c51ada68fcf5a0cd4ca928d7cc1ed469bb34c2ed008af069d8b28cc4512e6c8b2e7a5592",
		"999b4eae14de584cce5fa5962b768beda076b06df00d384bb502c6389df8159c006a5b94d1324f47e8d7bd2efe9d8d3dc1fa1429798e49826987ab5ae7ed5c21"
	},
};

/* Tests the message digest algorithms */
int TestChecksum(void)
{
	const uint32_t blocksize[CHECKSUM_MAX] = { MD5_BLOCKSIZE, SHA1_BLOCKSIZE, SHA256_BLOCKSIZE, SHA512_BLOCKSIZE };
	const char* hash_name[4] = { "MD5   ", "SHA1  ", "SHA256", "SHA512" };
	int i, j, errors = 0;
	uint8_t sum[MAX_HASHSIZE], *sum_expected;
	size_t full_msg_len = strlen(test_msg);
	char* msg = malloc(full_msg_len + 1);
	if (msg == NULL)
		return -1;

	for (j = 0; j < CHECKSUM_MAX; j++) {
		size_t copy_msg_len[4];
		copy_msg_len[0] = 0;
		copy_msg_len[1] = 3;
		// Designed to test the case where we pad into the total message length area
		// For SHA-512 this is 128 - 16 = 112 bytes, for others 64 - 8 = 56 bytes
		copy_msg_len[2] = blocksize[j] - (blocksize[j] >> 3);
		copy_msg_len[3] = full_msg_len;
		for (i = 0; i < 4; i++) {
			memset(msg, 0, full_msg_len + 1);
			if (i != 0)
				memcpy(msg, test_msg, copy_msg_len[i]);
			HashBuffer(j, msg, copy_msg_len[i], sum);
			sum_expected = to_bin(test_hash[j][i]);
			if (memcmp(sum, sum_expected, sum_count[j]) != 0) {
				uprintf("Test %s %d: FAIL", hash_name[j], i);
				errors++;
			} else {
				uprintf("Test %s %d: PASS", hash_name[j], i);
			}
			free(sum_expected);
		}
	}

	free(msg);
	return errors;
}
#endif
