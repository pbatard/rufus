/*
 * If linux/types.h is already been included, assume it has defined
 * everything we need.  (cross fingers)  Other header files may have
 * also defined the types that we need.
 */
#if (!defined(_LINUX_TYPES_H) && !defined(_BLKID_TYPES_H) && \
	!defined(_EXT2_TYPES_H))
#define _EXT2_TYPES_H


#ifndef HAVE___U8
#define HAVE___U8
#ifdef __U8_TYPEDEF
typedef __U8_TYPEDEF __u8;
#else
typedef unsigned char __u8;
#endif
#endif /* HAVE___U8 */

#ifndef HAVE___S8
#define HAVE___S8
#ifdef __S8_TYPEDEF
typedef __S8_TYPEDEF __s8;
#else
typedef signed char __s8;
#endif
#endif /* HAVE___S8 */

#ifndef HAVE___U16
#define HAVE___U16
#ifdef __U16_TYPEDEF
typedef __U16_TYPEDEF __u16;
#else
#if (4 == 2)
typedef	unsigned int	__u16;
#else
#if (2 == 2)
typedef	unsigned short	__u16;
#else
#undef HAVE___U16
  ?==error: undefined 16 bit type
#endif /* SIZEOF_SHORT == 2 */
#endif /* SIZEOF_INT == 2 */
#endif /* __U16_TYPEDEF */
#endif /* HAVE___U16 */

#ifndef HAVE___S16
#define HAVE___S16
#ifdef __S16_TYPEDEF
typedef __S16_TYPEDEF __s16;
#else
#if (4 == 2)
typedef	int		__s16;
#else
#if (2 == 2)
typedef	short		__s16;
#else
#undef HAVE___S16
  ?==error: undefined 16 bit type
#endif /* SIZEOF_SHORT == 2 */
#endif /* SIZEOF_INT == 2 */
#endif /* __S16_TYPEDEF */
#endif /* HAVE___S16 */

#ifndef HAVE___U32
#define HAVE___U32
#ifdef __U32_TYPEDEF
typedef __U32_TYPEDEF __u32;
#else
#if (4 == 4)
typedef	unsigned int	__u32;
#else
#if (4 == 4)
typedef	unsigned long	__u32;
#else
#if (2 == 4)
typedef	unsigned short	__u32;
#else
#undef HAVE___U32
 ?== error: undefined 32 bit type
#endif /* SIZEOF_SHORT == 4 */
#endif /* SIZEOF_LONG == 4 */
#endif /* SIZEOF_INT == 4 */
#endif /* __U32_TYPEDEF */
#endif /* HAVE___U32 */

#ifndef HAVE___S32
#define HAVE___S32
#ifdef __S32_TYPEDEF
typedef __S32_TYPEDEF __s32;
#else
#if (4 == 4)
typedef	int		__s32;
#else
#if (4 == 4)
typedef	long		__s32;
#else
#if (2 == 4)
typedef	short		__s32;
#else
#undef HAVE___S32
 ?== error: undefined 32 bit type
#endif /* SIZEOF_SHORT == 4 */
#endif /* SIZEOF_LONG == 4 */
#endif /* SIZEOF_INT == 4 */
#endif /* __S32_TYPEDEF */
#endif /* HAVE___S32 */

#ifndef HAVE___U64
#define HAVE___U64
#ifdef __U64_TYPEDEF
typedef __U64_TYPEDEF __u64;
#else
#if (4 == 8)
typedef unsigned int	__u64;
#else
#if (8 == 8)
typedef unsigned long long	__u64;
#else
#if (4 == 8)
typedef unsigned long	__u64;
#else
#undef HAVE___U64
 ?== error: undefined 64 bit type
#endif /* SIZEOF_LONG_LONG == 8 */
#endif /* SIZEOF_LONG == 8 */
#endif /* SIZEOF_INT == 8 */
#endif /* __U64_TYPEDEF */
#endif /* HAVE___U64 */

#ifndef HAVE___S64
#define HAVE___S64
#ifdef __S64_TYPEDEF
typedef __S64_TYPEDEF __s64;
#else
#if (4 == 8)
typedef int		__s64;
#else
#if (8 == 8)
#if defined(__GNUC__)
typedef __signed__ long long	__s64;
#else
typedef signed long long	__s64;
#endif /* __GNUC__ */
#else
#if (4 == 8)
typedef long		__s64;
#else
#undef HAVE___S64
 ?== error: undefined 64 bit type
#endif /* SIZEOF_LONG_LONG == 8 */
#endif /* SIZEOF_LONG == 8 */
#endif /* SIZEOF_INT == 8 */
#endif /* __S64_TYPEDEF */
#endif /* HAVE___S64 */

#undef __S8_TYPEDEF
#undef __U8_TYPEDEF
#undef __S16_TYPEDEF
#undef __U16_TYPEDEF
#undef __S32_TYPEDEF
#undef __U32_TYPEDEF
#undef __S64_TYPEDEF
#undef __U64_TYPEDEF

#endif /* _*_TYPES_H */

#include <stdint.h>

/* endian checking stuff */
#ifndef EXT2_ENDIAN_H_
#define EXT2_ENDIAN_H_

#ifdef __CHECKER__
# ifndef __bitwise
#  define __bitwise		__attribute__((bitwise))
# endif
#define __force			__attribute__((force))
#else
# ifndef __bitwise
#  define __bitwise
# endif
#define __force
#endif

typedef __u16	__bitwise	__le16;
typedef __u32	__bitwise	__le32;
typedef __u64	__bitwise	__le64;
typedef __u16	__bitwise	__be16;
typedef __u32	__bitwise	__be32;
typedef __u64	__bitwise	__be64;

#endif /* EXT2_ENDIAN_H_ */

/* These defines are needed for the public ext2fs.h header file */
#define HAVE_SYS_TYPES_H 1
#undef WORDS_BIGENDIAN
