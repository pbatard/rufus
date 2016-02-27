/*
 * Rufus: The Reliable USB Formatting Utility
 * Message-Digest algorithms (md5sum, sha1sum, sha256sum)
 * Copyright © 1998-2001 Free Software Foundation, Inc.
 * Copyright © 2004 g10 Code GmbH
 * Copyright © 2006-2012 Brad Conte <brad@bradconte.com>
 * Copyright © 2015-2016 Pete Batard <pete@akeo.ie>
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
 * SHA-256 code modified from crypto-algorithms by Brad Conte:
 * https://github.com/B-Con/crypto-algorithms - Public Domain
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <windowsx.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#undef BIG_ENDIAN_HOST

/* Globals */
char sum_str[3][65];

#if defined(__GNUC__)
#define ALIGNED(m) __attribute__ ((__aligned__(m)))
#elif defined(_MSC_VER)
#define ALIGNED(m) __declspec(align(m))
#endif

/* Rotate a 32 bit integer by n bytes */
#if defined(__GNUC__) && defined(__i386__)
static inline uint32_t rol(uint32_t x, int n)
{
	__asm__("roll %%cl,%0"
		:"=r" (x)
		:"0" (x),"c" (n));
	return x;
}
#elif defined(_MSC_VER) && (_M_IX86 >= 300)
static __inline uint32_t rol(uint32_t x, int n)
{
	__asm {
		mov eax, x
		mov ecx, n
		rol eax, cl
	}
	/* returns with result in EAX */
}
#else
#define rol(x,n) ( ((x) << (n)) | ((x) >> (32-(n))) )
#endif

// For SHA-256
static const uint32_t k[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/*
 * For convenience, we use a common context for all the checksums algorithms,
 * which means some elements may be unused...
 */
typedef struct ALIGNED(8) {
	unsigned char buf[64];
	uint32_t state[8];
	uint64_t bitcount;
	uint32_t bytecount;
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


/* Transform the message X which consists of 16 32-bit-words (SHA-1) */
static void sha1_transform(SUM_CONTEXT *ctx, const unsigned char *data)
{
	uint32_t a, b, c, d, e, tm;
	uint32_t x[16];

	/* get values from the chaining vars */
	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];

#ifdef BIG_ENDIAN_HOST
	memcpy(x, data, sizeof(x));
#else
	{
		int i;
		unsigned char *p2;
		for (i = 0, p2 = (unsigned char*)x; i < 16; i++, p2 += 4) {
			p2[3] = *data++;
			p2[2] = *data++;
			p2[1] = *data++;
			p2[0] = *data++;
		}
	}
#endif

#define K1  0x5A827999L
#define K2  0x6ED9EBA1L
#define K3  0x8F1BBCDCL
#define K4  0xCA62C1D6L
#define F1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )
#define F2(x,y,z)   ( x ^ y ^ z )
#define F3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )
#define F4(x,y,z)   ( x ^ y ^ z )

#define M(i) ( tm = x[i&0x0f] ^ x[(i-14)&0x0f] ^ x[(i-8)&0x0f] ^ x[(i-3)&0x0f], (x[i&0x0f] = rol(tm,1)) )

#define SHA1STEP(a,b,c,d,e,f,k,m) do { e += rol(a, 5) + f(b, c, d) + k + m; \
                                       b = rol(b, 30); } while(0)
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

static void sha256_transform(SUM_CONTEXT *ctx, const unsigned char *data)
{
	uint32_t a, b, c, d, e, f, g, h, i, t1, t2, m[64];

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];

#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))


#ifdef BIG_ENDIAN_HOST
	memcpy(m, data, sizeof(m));
#else
	{
		unsigned char *p2;
		for (i = 0, p2 = (unsigned char*)m; i < 16; i++, p2 += 4) {
			p2[3] = *data++;
			p2[2] = *data++;
			p2[1] = *data++;
			p2[0] = *data++;
		}
	}
#endif

	for (i = 16; i < 64; ++i)
		m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

	for (i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

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
static void md5_transform(SUM_CONTEXT *ctx, const unsigned char *data)
{
	uint32_t a, b, c, d;
	uint32_t x[16];

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];

#ifndef BIG_ENDIAN_HOST
	memcpy(x, data, sizeof(x));
#else
	{
		int i;
		unsigned char *p;
		for (i = 0, p = (unsigned char*)x; i < 16; i++, p += 4) {
			p[3] = *data++;
			p[2] = *data++;
			p[1] = *data++;
			p[0] = *data++;
		}
	}
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
static void sha1_write(SUM_CONTEXT *ctx, const unsigned char *buf, size_t len)
{
	if (ctx->bytecount == 64) { /* flush the buffer */
		sha1_transform(ctx, ctx->buf);
		ctx->bytecount = 0;
		ctx->bitcount += 64 * 8;
	}
	if (!buf)
		return;
	if (ctx->bytecount) {
		for (; len && ctx->bytecount < 64; len--)
			ctx->buf[ctx->bytecount++] = *buf++;
		sha1_write(ctx, NULL, 0);
		if (!len)
			return;
	}

	while (len >= 64) {
		sha1_transform(ctx, buf);
		ctx->bytecount = 0;
		ctx->bitcount += 64 * 8;
		len -= 64;
		buf += 64;
	}
	for (; len && ctx->bytecount < 64; len--)
		ctx->buf[ctx->bytecount++] = *buf++;
}

static void sha256_write(SUM_CONTEXT *ctx, const unsigned char *buf, size_t len)
{
	uint32_t i;

	for (i = 0; i < len; ++i) {
		ctx->buf[ctx->bytecount] = buf[i];
		ctx->bytecount++;
		if (ctx->bytecount == 64) {
			sha256_transform(ctx, ctx->buf);
			ctx->bitcount += 64 * 8;
			ctx->bytecount = 0;
		}
	}
}

/* Update the message digest with the contents of the buffer (MD5) */
static void md5_write(SUM_CONTEXT *ctx, const unsigned char *buf, size_t len)
{
	uint32_t t;

	/* Update bitcount */
	ctx->bitcount += (len << 3);

	t = (ctx->bitcount >> 3) & 0x3f;

	/* Handle any leading odd-sized chunks */
	if (t) {
		unsigned char *p = ctx->buf + t;

		t = 64 - t;
		if (len < t) {
			memcpy(p, buf, len);
			return;
		}
		memcpy(p, buf, t);
		md5_transform(ctx, ctx->buf);
		buf += t;
		len -= t;
	}

	/* Process data in 64-byte chunks */
	while (len >= 64) {
		memcpy(ctx->buf, buf, 64);
		md5_transform(ctx, ctx->buf);
		buf += 64;
		len -= 64;
	}

	/* Handle any remaining bytes of data. */
	memcpy(ctx->buf, buf, len);
}

/* The routine final terminates the computation and returns the digest (SHA-1) */
static void sha1_final(SUM_CONTEXT *ctx)
{
	unsigned char *p;

	sha1_write(ctx, NULL, 0); /* flush */;

	if (ctx->bytecount < 56) { /* enough room */
		ctx->buf[ctx->bytecount++] = 0x80; /* pad */
		while (ctx->bytecount < 56)
			ctx->buf[ctx->bytecount++] = 0; /* pad */
	} else { /* need one extra block */
		ctx->buf[ctx->bytecount++] = 0x80; /* pad character */
		while (ctx->bytecount < 64)
			ctx->buf[ctx->bytecount++] = 0;
		sha1_write(ctx, NULL, 0); /* flush */;
		memset(ctx->buf, 0, 56); /* fill next block with zeroes */
	}

	/* append the 64 bit count (big-endian) */
	ctx->buf[56] = (unsigned char) (ctx->bitcount >> 56);
	ctx->buf[57] = (unsigned char) (ctx->bitcount >> 48);
	ctx->buf[58] = (unsigned char) (ctx->bitcount >> 40);
	ctx->buf[59] = (unsigned char) (ctx->bitcount >> 32);
	ctx->buf[60] = (unsigned char) (ctx->bitcount >> 24);
	ctx->buf[61] = (unsigned char) (ctx->bitcount >> 16);
	ctx->buf[62] = (unsigned char) (ctx->bitcount >> 8);
	ctx->buf[63] = (unsigned char) ctx->bitcount;

	sha1_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(uint32_t*)p = ctx->state[a]; p += 4; } while(0)
#else /* little endian */
#define X(a) do { *p++ = (unsigned char) (ctx->state[a] >> 24); *p++ = (unsigned char) (ctx->state[a] >> 16); \
                  *p++ = (unsigned char) (ctx->state[a] >> 8); *p++ = (unsigned char) ctx->state[a]; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
#undef X
}

static void sha256_final(SUM_CONTEXT *ctx)
{
	uint32_t i;
	unsigned char *p;

	i = ctx->bytecount;

	// Pad whatever data is left in the buffer.
	if (ctx->bytecount < 56) {
		ctx->buf[i++] = 0x80;
		while (i < 56)
			ctx->buf[i++] = 0x00;
	}
	else {
		ctx->buf[i++] = 0x80;
		while (i < 64)
			ctx->buf[i++] = 0x00;
		sha256_transform(ctx, ctx->buf);
		memset(ctx->buf, 0, 56);
	}

	// Append to the padding the total message's length in bits and transform.
	ctx->bitcount += ctx->bytecount * 8;
	ctx->buf[63] = (unsigned char) (ctx->bitcount);
	ctx->buf[62] = (unsigned char) (ctx->bitcount >> 8);
	ctx->buf[61] = (unsigned char) (ctx->bitcount >> 16);
	ctx->buf[60] = (unsigned char) (ctx->bitcount >> 24);
	ctx->buf[59] = (unsigned char) (ctx->bitcount >> 32);
	ctx->buf[58] = (unsigned char) (ctx->bitcount >> 40);
	ctx->buf[57] = (unsigned char) (ctx->bitcount >> 48);
	ctx->buf[56] = (unsigned char) (ctx->bitcount >> 56);

	sha256_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(uint32_t*)p = ctx->state[a]; p += 4; } while(0)
#else /* little endian */
#define X(a) do { *p++ = (unsigned char) (ctx->state[a] >> 24); *p++ = (unsigned char) (ctx->state[a] >> 16); \
                  *p++ = (unsigned char) (ctx->state[a] >> 8); *p++ = (unsigned char) ctx->state[a]; } while(0)
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

/* The routine final terminates the computation and returns the digest (MD5) */
static void md5_final(SUM_CONTEXT *ctx)
{
	uint32_t count;
	unsigned char *p;

	/* Compute number of bytes mod 64 */
	count = (ctx->bitcount >> 3) & 0x3F;

	/* Set the first char of padding to 0x80.
	 * This is safe since there is always at least one byte free
	 */
	p = ctx->buf + count;
	*p++ = 0x80;

	/* Bytes of padding needed to make 64 bytes */
	count = 64 - 1 - count;

	/* Pad out to 56 mod 64 */
	if (count < 8) {
		/* Two lots of padding: Pad the first block to 64 bytes */
		memset(p, 0, count);
		md5_transform(ctx, ctx->buf);

		/* Now fill the next block with 56 bytes */
		memset(ctx->buf, 0, 56);
	} else {
		/* Pad block to 56 bytes */
		memset(p, 0, count - 8);
	}

	/* append the 64 bit count (little endian) */
	ctx->buf[56] = (unsigned char) ctx->bitcount;
	ctx->buf[57] = (unsigned char) (ctx->bitcount >> 8);
	ctx->buf[58] = (unsigned char) (ctx->bitcount >> 16);
	ctx->buf[59] = (unsigned char) (ctx->bitcount >> 24);
	ctx->buf[60] = (unsigned char) (ctx->bitcount >> 32);
	ctx->buf[61] = (unsigned char) (ctx->bitcount >> 40);
	ctx->buf[62] = (unsigned char) (ctx->bitcount >> 48);
	ctx->buf[63] = (unsigned char) (ctx->bitcount >> 56);

	md5_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *p++ = (unsigned char) (ctx->state[a] >> 24); *p++ = (unsigned char) (ctx->state[a] >> 16); \
                  *p++ = (unsigned char) (ctx->state[a] >> 8); *p++ = (unsigned char) ctx->state[a]; } while(0)
#else /* little endian */
#define X(a) do { *(uint32_t*)p = ctx->state[a]; p += 4; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
#undef X
}

/*
 * Checksum dialog callback
 */
INT_PTR CALLBACK ChecksumCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i, dw, dh;
	RECT rect;
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
		SetWindowTextA(GetDlgItem(hDlg, IDC_MD5), sum_str[0]);
		SetWindowTextA(GetDlgItem(hDlg, IDC_SHA1), sum_str[1]);
		SetWindowTextA(GetDlgItem(hDlg, IDC_SHA256), sum_str[2]);

		// Move/Resize the controls as needed to fit our text
		hDC = GetDC(GetDlgItem(hDlg, IDC_MD5));
		SelectFont(hDC, hFont);	// Yes, you *MUST* reapply the font to the DC, even after SetWindowText!

		GetWindowRect(GetDlgItem(hDlg, IDC_MD5), &rect);
		dw = rect.right - rect.left;
		dh = rect.bottom - rect.top;
		DrawTextU(hDC, sum_str[0], -1, &rect, DT_CALCRECT);
		dw = rect.right - rect.left - dw + 12;	// Ideally we'd compute the field borders from the system, but hey...
		dh = rect.bottom - rect.top - dh + 6;
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA256), 0, 0, dw, dh, 1.0f);

		GetWindowRect(GetDlgItem(hDlg, IDC_SHA1), &rect);
		dw = rect.right - rect.left;
		DrawTextU(hDC, sum_str[1], -1, &rect, DT_CALCRECT);
		dw = rect.right - rect.left - dw + 12;
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_MD5), 0, 0, dw, 0, 1.0f);
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA1), 0, 0, dw, 0, 1.0f);

		safe_release_dc(GetDlgItem(hDlg, IDC_MD5), hDC);

		for (i=(int)safe_strlen(image_path); (i>0)&&(image_path[i]!='\\'); i--);
		if (image_path != NULL)	// VS code analysis has a false positive on this one
			SetWindowTextU(hDlg, &image_path[i+1]);
		// Set focus on the OK button
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDOK), TRUE);
		CenterDialog(hDlg);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			reset_localization(IDD_CHECKSUM);
			EndDialog(hDlg, LOWORD(wParam));
			// Reset focus to our toolbar
			PostMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)hStatusToolbar, TRUE);
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

typedef void sum_init_t(SUM_CONTEXT *ctx);
typedef void sum_write_t(SUM_CONTEXT *ctx, const unsigned char *buf, size_t len);
typedef void sum_final_t(SUM_CONTEXT *ctx);
sum_init_t *sum_init[3] = { md5_init, sha1_init , sha256_init };
sum_write_t *sum_write[3] = { md5_write, sha1_write , sha256_write };
sum_final_t *sum_final[3] = { md5_final, sha1_final , sha256_final };
int sum_count[3] = { 16, 20, 32 };

DWORD WINAPI SumThread(void* param)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD rSize = 0;
	uint64_t rb, LastRefresh = 0;
	char buffer[4096];
	SUM_CONTEXT sum_ctx[3];
	int i, j, r = -1;
	float format_percent = 0.0f;

	if (image_path == NULL)
		goto out;

	uprintf("\r\nComputing checksum for '%s'...", image_path);
	h = CreateFileU(image_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		uprintf("Could not open file: %s", WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
		goto out;
	}

	for (i = 0; i < ARRAYSIZE(sum_init); i++)
		sum_init[i](&sum_ctx[i]);

	for (rb = 0; ; rb += rSize) {
		if (_GetTickCount64() > LastRefresh + 25) {
			LastRefresh = _GetTickCount64();
			format_percent = (100.0f*rb) / (1.0f*img_report.projected_size);
			PrintInfo(0, MSG_271, format_percent);
			SendMessage(hProgress, PBM_SETPOS, (WPARAM)((format_percent/100.0f)*MAX_PROGRESS), 0);
			SetTaskbarProgressValue(rb, img_report.projected_size);
		}
		CHECK_FOR_USER_CANCEL;
		if (!ReadFile(h, buffer, sizeof(buffer), &rSize, NULL)) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_READ_FAULT;
			uprintf("  Read error: %s", WindowsErrorString());
			goto out;
		}
		if (rSize == 0)
			break;
		for (i = 0; i < ARRAYSIZE(sum_init); i++)
			sum_write[i](&sum_ctx[i], buffer, (size_t)rSize);
	}

	for (i = 0; i < ARRAYSIZE(sum_init); i++) {
		memset(&sum_str[i], 0, ARRAYSIZE(sum_str[i]));
		sum_final[i](&sum_ctx[i]);
		for (j = 0; j < sum_count[i]; j++)
			safe_sprintf(&sum_str[i][2 * j], ARRAYSIZE(sum_str[i]) - 2 * j, "%02x", sum_ctx[i].buf[j]);
	}
	uprintf("  MD5:\t %s", sum_str[0]);
	uprintf("  SHA1:\t %s", sum_str[1]);
	uprintf("  SHA256: %s", sum_str[2]);

	r = 0;

out:
	safe_closehandle(h);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
	if (r == 0)
		MyDialogBox(hMainInstance, IDD_CHECKSUM, hMainDialog, ChecksumCallback);
	ExitThread(r);
}
