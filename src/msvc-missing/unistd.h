/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file was originally part of the w64 mingw-runtime package.
 */

/* Workaround unistd.h for MS compilers */

#ifndef _MSC_VER
#error This header should only be used with Microsoft compilers
#endif

#include <windows.h>

#ifndef _UNISTD_H_
#define _UNISTD_H_

/* mode_t is used in the libcdio headers */
#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif /* _MODE_T_DEFINED */

/* ssize_t is also not available (copy/paste from MinGW) */
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#undef ssize_t
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef int ssize_t;
#endif /* _WIN64 */
#endif /* _SSIZE_T_DEFINED */

/* ext2fs needs this, which we picked from libcdio-driver/filemode.h */
#if !defined S_IFBLK && defined _WIN32
# define S_IFBLK 0x3000
#endif
#if !defined S_ISBLK && defined S_IFBLK
# define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif

#endif
