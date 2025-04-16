#ifndef _WIMLIB_DIVSUFSORT_H
#define _WIMLIB_DIVSUFSORT_H

#include "wimlib/types.h"

void
divsufsort(const u8 *T, u32 *SA, u32 n, u32 *tmp);

#define DIVSUFSORT_TMP_LEN (256 + (256 * 256))

#endif /* _WIMLIB_DIVSUFSORT_H */
