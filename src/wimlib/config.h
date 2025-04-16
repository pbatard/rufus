/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define to 1 to enable supporting code for tests */
/* #undef ENABLE_TEST_SUPPORT */

/* Define to 1 if you have the <alloca.h> header file. */
/* #undef HAVE_ALLOCA_H */

/* Define to 1 if you have the <byteswap.h> header file. */
/* #undef HAVE_BYTESWAP_H */

/* Define to 1 if you have the <dlfcn.h> header file. */
/* #undef HAVE_DLFCN_H */

/* Define to 1 if you have the <endian.h> header file. */
/* #undef HAVE_ENDIAN_H */

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the 'fdopendir' function. */
/* #undef HAVE_FDOPENDIR */

/* Define to 1 if you have the 'flock' function. */
/* #undef HAVE_FLOCK */

/* Define to 1 if you have the 'fsetxattr' function. */
/* #undef HAVE_FSETXATTR */

/* Define to 1 if you have the 'fstatat' function. */
/* #undef HAVE_FSTATAT */

/* Define to 1 if you have the 'futimens' function. */
/* #undef HAVE_FUTIMENS */

/* Define to 1 if you have the 'getopt_long_only' function. */
#define HAVE_GETOPT_LONG_ONLY 1

/* Define to 1 if you have the <glob.h> header file. */
/* #undef HAVE_GLOB_H */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the 'lgetxattr' function. */
/* #undef HAVE_LGETXATTR */

/* Define to 1 if you have the 'rt' library (-lrt). */
/* #undef HAVE_LIBRT */

/* Define to 1 if you have the 'llistxattr' function. */
/* #undef HAVE_LLISTXATTR */

/* Define to 1 if you have the 'lsetxattr' function. */
/* #undef HAVE_LSETXATTR */

/* Define to 1 if you have the <machine/endian.h> header file. */
/* #undef HAVE_MACHINE_ENDIAN_H */

/* Define to 1 if you have the 'mempcpy' function. */
/* #undef HAVE_MEMPCPY */

/* Define to 1 if you have the 'openat' function. */
/* #undef HAVE_OPENAT */

/* Define to 1 if you have the 'posix_fallocate' function. */
/* #undef HAVE_POSIX_FALLOCATE */

/* Define if you have POSIX threads libraries and header files. */
/* #undef HAVE_PTHREAD */

/* Have PTHREAD_PRIO_INHERIT. */
/* #undef HAVE_PTHREAD_PRIO_INHERIT */

/* Define to 1 if you have the 'readlinkat' function. */
/* #undef HAVE_READLINKAT */

/* Define to 1 if stat() supports nanosecond precision timestamps */
/* #undef HAVE_STAT_NANOSECOND_PRECISION */

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stddef.h> header file. */
#define HAVE_STDDEF_H 1

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

/* Define to 1 if you have the <sys/byteorder.h> header file. */
/* #undef HAVE_SYS_BYTEORDER_H */

/* Define to 1 if you have the <sys/endian.h> header file. */
/* #undef HAVE_SYS_ENDIAN_H */

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/syscall.h> header file. */
/* #undef HAVE_SYS_SYSCALL_H */

/* Define to 1 if you have the <sys/sysctl.h> header file. */
/* #undef HAVE_SYS_SYSCTL_H */

/* Define to 1 if you have the <sys/times.h> header file. */
/* #undef HAVE_SYS_TIMES_H */

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/xattr.h> header file. */
/* #undef HAVE_SYS_XATTR_H */

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the 'utimensat' function. */
/* #undef HAVE_UTIMENSAT */

/* Define to 1 if you have the <utime.h> header file. */
#define HAVE_UTIME_H 1

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

#ifndef PACKAGE
/* Name of package */
#define PACKAGE "wimlib"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "https://wimlib.net/forums/"

/* Define to the full name of this package. */
#define PACKAGE_NAME "wimlib"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "wimlib 1.14.4-3-g4a34203c"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "wimlib"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.14.4-3-g4a34203c"

/* Version number of package */
#define VERSION "1.14.4-3-g4a34203c"
#endif

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* Define to 1 if all of the C89 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Define to 1 if using FUSE support */
/* #undef WITH_FUSE */

/* Define to 1 if using NTFS-3G support */
/* #undef WITH_NTFS_3G */

/* Define to 1 if using libcdio support */
#define WITH_LIBCDIO 1

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif
