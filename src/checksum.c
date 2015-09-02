/*
 * Rufus: The Reliable USB Formatting Utility
 * Message-Digest algorithms (sha1sum, md5sum)
 * Copyright © 1998-2001 Free Software Foundation, Inc.
 * Copyright © 2004 g10 Code GmbH
 * Copyright © 2015 Pete Batard <pete@akeo.ie>
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
#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "localization.h"

#undef BIG_ENDIAN_HOST

/* Globals */
char sha1str[41], md5str[33];

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

typedef struct ALIGNED(8) {
	unsigned char buf[64];
	uint32_t h0, h1, h2, h3, h4;
	uint32_t count;
	uint64_t nblocks;
} SHA1_CONTEXT;

typedef struct ALIGNED(8) {
	unsigned char buf[64];
	uint32_t h0, h1, h2, h3;
	uint64_t bitcount;
} MD5_CONTEXT;

void sha1_init(SHA1_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->h0 = 0x67452301;
	ctx->h1 = 0xefcdab89;
	ctx->h2 = 0x98badcfe;
	ctx->h3 = 0x10325476;
	ctx->h4 = 0xc3d2e1f0;
}

void md5_init(MD5_CONTEXT *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->h0 = 0x67452301;
	ctx->h1 = 0xefcdab89;
	ctx->h2 = 0x98badcfe;
	ctx->h3 = 0x10325476;
}

/* Transform the message X which consists of 16 32-bit-words (SHA-1) */
static void sha1_transform(SHA1_CONTEXT *ctx, const unsigned char *data)
{
	uint32_t a, b, c, d, e, tm;
	uint32_t x[16];

	/* get values from the chaining vars */
	a = ctx->h0;
	b = ctx->h1;
	c = ctx->h2;
	d = ctx->h3;
	e = ctx->h4;

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
	ctx->h0 += a;
	ctx->h1 += b;
	ctx->h2 += c;
	ctx->h3 += d;
	ctx->h4 += e;
}

/* Transform the message X which consists of 16 32-bit-words (MD5) */
static void md5_transform(MD5_CONTEXT *ctx, const unsigned char *data)
{
	uint32_t a, b, c, d;
	uint32_t x[16];

	a = ctx->h0;
	b = ctx->h1;
	c = ctx->h2;
	d = ctx->h3;

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
	ctx->h0 += a;
	ctx->h1 += b;
	ctx->h2 += c;
	ctx->h3 += d;
}

/* Update the message digest with the contents of the buffer (SHA-1) */
static void sha1_write(SHA1_CONTEXT *ctx, const unsigned char *buf, size_t len)
{
	if (ctx->count == 64) { /* flush the buffer */
		sha1_transform(ctx, ctx->buf);
		ctx->count = 0;
		ctx->nblocks++;
	}
	if (!buf)
		return;
	if (ctx->count) {
		for (; len && ctx->count < 64; len--)
			ctx->buf[ctx->count++] = *buf++;
		sha1_write(ctx, NULL, 0);
		if (!len)
			return;
	}

	while (len >= 64) {
		sha1_transform(ctx, buf);
		ctx->count = 0;
		ctx->nblocks++;
		len -= 64;
		buf += 64;
	}
	for (; len && ctx->count < 64; len--)
		ctx->buf[ctx->count++] = *buf++;
}

/* Update the message digest with the contents of the buffer (MD5) */
void md5_write(MD5_CONTEXT *ctx, const unsigned char *buf, size_t len)
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
static void sha1_final(SHA1_CONTEXT *ctx)
{
	uint64_t bitcount;
	unsigned char *p;

	sha1_write(ctx, NULL, 0); /* flush */;

	bitcount = ctx->nblocks * 64 * 8;

	if (ctx->count < 56) { /* enough room */
		ctx->buf[ctx->count++] = 0x80; /* pad */
		while (ctx->count < 56)
			ctx->buf[ctx->count++] = 0; /* pad */
	} else { /* need one extra block */
		ctx->buf[ctx->count++] = 0x80; /* pad character */
		while (ctx->count < 64)
			ctx->buf[ctx->count++] = 0;
		sha1_write(ctx, NULL, 0); /* flush */;
		memset(ctx->buf, 0, 56); /* fill next block with zeroes */
	}

	/* append the 64 bit count (big-endian) */
	ctx->buf[56] = (unsigned char) (bitcount >> 56);
	ctx->buf[57] = (unsigned char) (bitcount >> 48);
	ctx->buf[58] = (unsigned char) (bitcount >> 40);
	ctx->buf[59] = (unsigned char) (bitcount >> 32);
	ctx->buf[60] = (unsigned char) (bitcount >> 24);
	ctx->buf[61] = (unsigned char) (bitcount >> 16);
	ctx->buf[62] = (unsigned char) (bitcount >> 8);
	ctx->buf[63] = (unsigned char) bitcount;

	sha1_transform(ctx, ctx->buf);

	p = ctx->buf;
#ifdef BIG_ENDIAN_HOST
#define X(a) do { *(uint32_t*)p = ctx->h##a ; p += 4; } while(0)
#else /* little endian */
#define X(a) do { *p++ = (unsigned char) (ctx->h##a >> 24); *p++ = (unsigned char) (ctx->h##a >> 16); \
                  *p++ = (unsigned char) (ctx->h##a >> 8); *p++ = (unsigned char) ctx->h##a; } while(0)
#endif
	X(0);
	X(1);
	X(2);
	X(3);
	X(4);
#undef X
}

/* The routine final terminates the computation and returns the digest (MD5) */
static void md5_final(MD5_CONTEXT *ctx)
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
#define X(a) do { *p++ = (unsigned char) (ctx->h##a >> 24); *p++ = (unsigned char) (ctx->h##a >> 16); \
                  *p++ = (unsigned char) (ctx->h##a >> 8); *p++ = (unsigned char) ctx->h##a; } while(0)
#else /* little endian */
#define X(a) do { *(uint32_t*)p = ctx->h##a ; p += 4; } while(0)
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
	int i, dw;
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
		if (hDC != NULL)
			ReleaseDC(hDlg, hDC);
		SendDlgItemMessageA(hDlg, IDC_MD5, WM_SETFONT, (WPARAM)hFont, TRUE);
		SendDlgItemMessageA(hDlg, IDC_SHA1, WM_SETFONT, (WPARAM)hFont, TRUE);
		SetWindowTextA(GetDlgItem(hDlg, IDC_MD5), md5str);
		SetWindowTextA(GetDlgItem(hDlg, IDC_SHA1), sha1str);

		// Move/Resize the controls as needed to fit our text
		hDC = GetDC(GetDlgItem(hDlg, IDC_SHA1));
		SelectFont(hDC, hFont);	// Yes, you *MUST* reapply the font to the DC, even after SetWindowText!
		GetWindowRect(GetDlgItem(hDlg, IDC_SHA1), &rect);
		dw = rect.right - rect.left;
		DrawTextU(hDC, sha1str, -1, &rect, DT_CALCRECT);
		if (hDC != NULL)
			ReleaseDC(GetDlgItem(hDlg, IDC_SHA1), hDC);
		dw = rect.right - rect.left - dw + 12;	// Ideally we'd compute the field borders from the system, but hey...
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_MD5), 0, 0, dw, 0, 1.0f);
		ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, IDC_SHA1), 0, 0, dw, 0, 1.0f);

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


DWORD WINAPI SumThread(void* param)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	DWORD rSize = 0, LastRefresh = 0;
	uint64_t rb;
	char buffer[4096];
	SHA1_CONTEXT sha1_ctx;
	MD5_CONTEXT md5_ctx;
	int i, r = -1;
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

	sha1_init(&sha1_ctx);
	md5_init(&md5_ctx);

	for (rb = 0; ; rb += rSize) {
		if (GetTickCount() > LastRefresh + 25) {
			LastRefresh = GetTickCount();
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
		sha1_write(&sha1_ctx, buffer, (size_t)rSize);
		md5_write(&md5_ctx, buffer, (size_t)rSize);
	}

	sha1_final(&sha1_ctx);
	md5_final(&md5_ctx);

	for (i = 0; i < 16; i++)
		safe_sprintf(&md5str[2 * i], sizeof(md5str) - 2 * i, "%02x", md5_ctx.buf[i]);
	uprintf("  MD5:\t%s", md5str);
	for (i = 0; i < 20; i++)
		safe_sprintf(&sha1str[2*i], sizeof(sha1str) - 2*i, "%02x", sha1_ctx.buf[i]);
	uprintf("  SHA1:\t%s", sha1str);
	r = 0;

out:
	safe_closehandle(h);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
	if (r == 0)
		MyDialogBox(hMainInstance, IDD_CHECKSUM, hMainDialog, ChecksumCallback);
	ExitThread(r);
}
