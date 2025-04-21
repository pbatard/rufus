/*
 * endianness.h - macros and inline functions for endianness conversion
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

#ifndef _WIMLIB_ENDIANNESS_H
#define _WIMLIB_ENDIANNESS_H

#include "wimlib/compiler.h"
#include "wimlib/types.h"

#ifdef HAVE_SYS_ENDIAN_H
   /* Needed on NetBSD to stop system bswap macros from messing things up */
#  include <sys/endian.h>
#  undef bswap16
#  undef bswap32
#  undef bswap64
#endif

/* Watch out for conflict with ntfs-3g/endians.h ... */
#ifndef _NTFS_ENDIANS_H

#ifdef _MSC_VER

#include <intrin.h>
#define bswap16 _byteswap_ushort
#define bswap32 _byteswap_ulong
#define bswap64 _byteswap_uint64

#else

#define bswap16_const(n)			\
	((((u16)(n) & 0x00FF) << 8)	|	\
	 (((u16)(n) & 0xFF00) >> 8))

#define bswap32_const(n)				\
	((((u32)(n) & 0x000000FF) << 24)	|	\
	 (((u32)(n) & 0x0000FF00) << 8)		|	\
	 (((u32)(n) & 0x00FF0000) >> 8)		|	\
	 (((u32)(n) & 0xFF000000) >> 24))

#define bswap64_const(n)					\
	((((u64)(n) & 0x00000000000000FF) << 56)	|	\
	 (((u64)(n) & 0x000000000000FF00) << 40)	|	\
	 (((u64)(n) & 0x0000000000FF0000) << 24)	|	\
	 (((u64)(n) & 0x00000000FF000000) << 8)		|	\
	 (((u64)(n) & 0x000000FF00000000) >> 8)		|	\
	 (((u64)(n) & 0x0000FF0000000000) >> 24)	|	\
	 (((u64)(n) & 0x00FF000000000000) >> 40)	|	\
	 (((u64)(n) & 0xFF00000000000000) >> 56))

static forceinline u16 do_bswap16(u16 n)
{
#if GCC_PREREQ(4, 8) || __has_builtin(__builtin_bswap16)
	return __builtin_bswap16(n);
#else
	return bswap16_const(n);
#endif
}

static forceinline u32 do_bswap32(u32 n)
{
#if GCC_PREREQ(4, 3) || __has_builtin(__builtin_bswap32)
	return __builtin_bswap32(n);
#else
	return bswap32_const(n);
#endif
}

static forceinline u64 do_bswap64(u64 n)
{
#if GCC_PREREQ(4, 3) || __has_builtin(__builtin_bswap64)
	return __builtin_bswap64(n);
#else
	return bswap64_const(n);
#endif
}

#define bswap16(n) (__builtin_constant_p(n) ? bswap16_const(n) : do_bswap16(n))
#define bswap32(n) (__builtin_constant_p(n) ? bswap32_const(n) : do_bswap32(n))
#define bswap64(n) (__builtin_constant_p(n) ? bswap64_const(n) : do_bswap64(n))

#endif /* _MSC_VER */

#if CPU_IS_BIG_ENDIAN()
#  define cpu_to_le16(n) ((_force_attr le16)bswap16(n))
#  define cpu_to_le32(n) ((_force_attr le32)bswap32(n))
#  define cpu_to_le64(n) ((_force_attr le64)bswap64(n))
#  define le16_to_cpu(n) bswap16((_force_attr u16)(le16)(n))
#  define le32_to_cpu(n) bswap32((_force_attr u32)(le32)(n))
#  define le64_to_cpu(n) bswap64((_force_attr u64)(le64)(n))
#  define cpu_to_be16(n) ((_force_attr be16)(u16)(n))
#  define cpu_to_be32(n) ((_force_attr be32)(u32)(n))
#  define cpu_to_be64(n) ((_force_attr be64)(u64)(n))
#  define be16_to_cpu(n) ((_force_attr u16)(be16)(n))
#  define be32_to_cpu(n) ((_force_attr u32)(be32)(n))
#  define be64_to_cpu(n) ((_force_attr u64)(be64)(n))
#else
#  define cpu_to_le16(n) ((_force_attr le16)(u16)(n))
#  define cpu_to_le32(n) ((_force_attr le32)(u32)(n))
#  define cpu_to_le64(n) ((_force_attr le64)(u64)(n))
#  define le16_to_cpu(n) ((_force_attr u16)(le16)(n))
#  define le32_to_cpu(n) ((_force_attr u32)(le32)(n))
#  define le64_to_cpu(n) ((_force_attr u64)(le64)(n))
#  define cpu_to_be16(n) ((_force_attr be16)bswap16(n))
#  define cpu_to_be32(n) ((_force_attr be32)bswap32(n))
#  define cpu_to_be64(n) ((_force_attr be64)bswap64(n))
#  define be16_to_cpu(n) bswap16((_force_attr u16)(be16)(n))
#  define be32_to_cpu(n) bswap32((_force_attr u32)(be32)(n))
#  define be64_to_cpu(n) bswap64((_force_attr u64)(be64)(n))
#endif

#endif /* _NTFS_ENDIANS_H */
#endif /* _WIMLIB_ENDIANNESS_H */
