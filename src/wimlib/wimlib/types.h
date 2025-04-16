#ifndef _WIMLIB_TYPES_H
#define _WIMLIB_TYPES_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "wimlib_tchar.h"
#include "wimlib/compiler.h"

#ifndef _NTFS_TYPES_H
/* Unsigned integer types of exact size in bits */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Signed integer types of exact size in bits */
typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* Unsigned little endian types of exact size */
typedef uint16_t _bitwise_attr le16;
typedef uint32_t _bitwise_attr le32;
typedef uint64_t _bitwise_attr le64;

/* Unsigned big endian types of exact size */
typedef uint16_t _bitwise_attr be16;
typedef uint32_t _bitwise_attr be32;
typedef uint64_t _bitwise_attr be64;
#endif

/* A pointer to 'utf16lechar' indicates a UTF-16LE encoded string */
typedef le16 utf16lechar;

#ifndef WIMLIB_WIMSTRUCT_DECLARED
typedef struct WIMStruct WIMStruct;
#  define WIMLIB_WIMSTRUCT_DECLARED
#endif

/*
 * Type of a machine word.  'unsigned long' would be logical, but that is only
 * 32 bits on x86_64 Windows.  The same applies to 'uint_fast32_t'.  So the best
 * we can do without a bunch of #ifdefs appears to be 'size_t'.
 */
typedef size_t machine_word_t;

#define WORDBYTES	sizeof(machine_word_t)
#define WORDBITS	(8 * WORDBYTES)

#endif /* _WIMLIB_TYPES_H */
