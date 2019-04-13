/*
 * If linux/types.h is already been included, assume it has defined
 * everything we need.  (cross fingers)  Other header files may have
 * also defined the types that we need.
 */
#if (!defined(_LINUX_TYPES_H) && !defined(_BLKID_TYPES_H) && \
	!defined(_EXT2_TYPES_H))
#define _EXT2_TYPES_H

#include <stdint.h>

#ifndef HAVE___U8
#define HAVE___U8
typedef uint8_t __u8;
#endif /* HAVE___U8 */

#ifndef HAVE___S8
#define HAVE___S8
typedef int8_t __s8;
#endif /* HAVE___S8 */

#ifndef HAVE___U16
#define HAVE___U16
typedef uint16_t __u16;
#endif /* HAVE___U16 */

#ifndef HAVE___S16
#define HAVE___S16
typedef int16_t __s16;
#endif /* HAVE___S16 */

#ifndef HAVE___U32
#define HAVE___U32
typedef uint32_t __u32;
#endif /* HAVE___U32 */

#ifndef HAVE___S32
#define HAVE___S32
typedef int32_t __s32;
#endif /* HAVE___S32 */

#ifndef HAVE___U64
#define HAVE___U64
typedef uint64_t __u64;
#endif /* HAVE___U64 */

#ifndef HAVE___S64
#define HAVE___S64
typedef int64_t __s64;
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
