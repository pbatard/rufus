/* Workaround stdbool.h for WDK compilers - Public Domain */

#include <windows.h>

#ifndef _STDBOOL
#define _STDBOOL

#ifndef __cplusplus

#define bool BOOL
#define false FALSE
#define true TRUE

#endif

#endif
