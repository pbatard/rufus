#ifndef _MSC_VER
#error This header should only be used with Microsoft compilers
#endif

#include <windows.h>

/* Workaround unisdt.h for MS compilers */

#ifndef _UNISTD_H_
#define _UNISTD_H_

typedef unsigned short mode_t;

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

#endif