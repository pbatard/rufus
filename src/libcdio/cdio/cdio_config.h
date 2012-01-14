/* config.h.in.  Generated from configure.ac by autoheader.  */

/* compiler does lsbf in struct bitfields */
#undef BITFIELD_LSBF

/* Define 1 if you are compiling using cygwin */
#undef CYGWIN

/* what to put between the brackets for empty arrays */
#ifdef _MSC_VER
/* Very disputable hack! -- good thing we use MinGW for the release */
#define EMPTY_ARRAY_SIZE 256
#else
#define EMPTY_ARRAY_SIZE
#endif

/* Define to 1 if you have the `bzero' function. */
#undef HAVE_BZERO

/* Define if time.h defines extern long timezone and int daylight vars. */
#undef HAVE_DAYLIGHT

/* Define to 1 if you have the <dlfcn.h> header file. */
#undef HAVE_DLFCN_H

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the <fcntl.h> header file. */
#undef HAVE_FCNTL_H

/* Define to 1 if you have the <glob.h> header file. */
#undef HAVE_GLOB_H

/* Define if you have the iconv() function. */
#undef HAVE_ICONV

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Supports ISO _Pragma() macro */
#undef HAVE_ISOC99_PRAGMA

/* Define 1 if you want ISO-9660 Joliet extension support. You must have also
   libiconv installed to get Joliet extension support. */
#undef HAVE_JOLIET

/* Define to 1 if you have the `memcpy' function. */
#define HAVE_MEMCPY 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the `snprintf' function. */
#undef HAVE_SNPRINTF

/* Define to 1 if you have the `nsl' library (-lnsl). */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the <stdbool.h> header file. */
#undef HAVE_STDBOOL_H

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define if struct tm has the tm_gmtoff member. */
#undef HAVE_TM_GMTOFF

/* Define if time.h defines extern extern char *tzname[2] variable */
#undef HAVE_TZNAME

/* Define to 1 if you have the `tzset' function. */
#undef HAVE_TZSET

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vsnprintf' function. */
#undef HAVE_VSNPRINTF

/* Define 1 if you have MinGW CD-ROM support */
#undef HAVE_WIN32_CDROM

/* Define as const if the declaration of iconv() needs const. */
#undef ICONV_CONST 

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#define inline  __inline
