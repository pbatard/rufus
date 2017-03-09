/* config.h for libcdio (used by both MinGW and MSVC) */

#if defined(_MSC_VER)
/* Disable: warning C4996: The POSIX name for this item is deprecated. */
#pragma warning(disable:4996)
/* Disable: warning C4018: signed/unsigned mismatch */
#pragma warning(disable:4018)
/* Disable: warning C4242: conversion from 'x' to 'y', possible loss of data */
#pragma warning(disable:4242) /* 32 bit */
#pragma warning(disable:4244) /* 64 bit */
#pragma warning(disable:4267) /* 64 bit */
#endif

/* what to put between the brackets for empty arrays */
#define EMPTY_ARRAY_SIZE

/* Define to 1 if you have the `chdir' function. */
/* #undef HAVE_CHDIR */

/* Define if time.h defines extern long timezone and int daylight vars. */
#define HAVE_DAYLIGHT 1

/* Define to 1 if you have the `drand48' function. */
/* #undef HAVE_DRAND48 */

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `lseek64' function. */
#define HAVE_LSEEK64 1
/* The equivalent of lseek64 on MSVC is _lseeki64 */
#define lseek64 _lseeki64

/* Define to 1 if you have the `fseeko' function. */
/* #undef HAVE_FSEEKO */

/* Define to 1 if you have the `fseeko64' function. */
#define HAVE_FSEEKO64 1
/* The equivalent of fseeko64 for MSVC is _fseeki64 */
#define fseeko64 _fseeki64

/* Define to 1 if you have the `ftruncate' function. */
/* #undef HAVE_FTRUNCATE */

/* Define if you have the iconv() function and it works. */
/* #undef HAVE_ICONV */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1 /* provided in MSVC/missing if needed */

/* Supports ISO _Pragma() macro */
/* #undef HAVE_ISOC99_PRAGMA */

/* Define 1 if you want ISO-9660 Joliet extension support. You must have also
   libiconv installed to get Joliet extension support. */
#define HAVE_JOLIET 1

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the `lstat' function. */
/* #undef HAVE_LSTAT */

/* Define to 1 if you have the `memcpy' function. */
#define HAVE_MEMCPY 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the `rand' function. */
#define HAVE_RAND 1

/* Define to 1 if you have the `readlink' function. */
/* #undef HAVE_READLINK */

/* Define to 1 if you have the `realpath' function. */
/* #undef HAVE_REALPATH */

/* Define 1 if you want ISO-9660 Rock-Ridge extension support. */
#define HAVE_ROCK 1

/* Define to 1 if you have the `setegid' function. */
/* #undef HAVE_SETEGID */

/* Define to 1 if you have the `setenv' function. */
/* #undef HAVE_SETENV */

/* Define to 1 if you have the `seteuid' function. */
/* #undef HAVE_SETEUID */

/* Define to 1 if you have the `sleep' function. */
/* #undef HAVE_SLEEP */

/* Define to 1 if you have the `snprintf' function. */
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
#define HAVE_SNPRINTF 1
#endif

/* Define to 1 if you have the `vsnprintf' function. */
#if defined(_MSC_VER) && (_MSC_VER >= 1900)
#define HAVE_VSNPRINTF
#endif

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stdbool.h> header file. */
/* #undef HAVE_STDBOOL_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1 /* provided in MSVC/missing if needed */

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1
/* The equivalent of strdup on MSVC is _strdup */
#define strdup _strdup

/* Define to 1 if you have the <strings.h> header file. */
/* #undef HAVE_STRINGS_H */

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strndup' function. */
#if defined(__MINGW32__)
#define HAVE_STRNDUP 1
#endif

/* Define this if you have struct timespec */
/* #undef HAVE_STRUCT_TIMESPEC */

/* Define to 1 if you have the <sys/cdio.h> header file. */
/* #undef HAVE_SYS_CDIO_H */

/* Define to 1 if you have the <sys/param.h> header file. */
/* #undef HAVE_SYS_PARAM_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define this <sys/stat.h> defines S_ISLNK() */
/* #undef HAVE_S_ISLNK */

/* Define this <sys/stat.h> defines S_ISSOCK() */
/* #undef HAVE_S_ISSOCK */

/* Define if you have an extern long timenzone variable. */
#define HAVE_TIMEZONE_VAR 1

/* Define if struct tm has the tm_gmtoff member. */
/* #undef HAVE_TM_GMTOFF */

/* Define if time.h defines extern extern char *tzname[2] variable */
#define HAVE_TZNAME 1
/* The equivalent of tzset on MSVC is _tzset */
#define tzset _tzset

/* Define to 1 if you have the `tzset' function. */
#define HAVE_TZSET 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1 /* provided in MSVC/missing if needed */

/* Define to 1 if you have the `unsetenv' function. */
/* #undef HAVE_UNSETENV */

/* Define to 1 if you have the `usleep' function. */
/* #undef HAVE_USLEEP */

/* Define to 1 if you have the `vsnprintf' function. */
/* #undef HAVE_VSNPRINTF */

/* Define 1 if you have MinGW CD-ROM support */
/* #undef HAVE_WIN32_CDROM */

/* Define to 1 if you have the <windows.h> header file. */
#define HAVE_WINDOWS_H 1

/* Define to 1 if you have the `_stati64' function. */
#define HAVE__STATI64 1

/* Define as const if the declaration of iconv() needs const. */
/* #undef ICONV_CONST  */

/* Is set when libcdio's config.h has been included. Applications wishing to
   sue their own config.h values (such as set by the application's configure
   script can define this before including any of libcdio's headers. */
#define LIBCDIO_CONFIG_H 1

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Number of bits in a file offset, on hosts where this is settable. */
/* Note: This is defined as a preprocessor macro in the project files */
#define _FILE_OFFSET_BITS 64

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#define inline __inline
