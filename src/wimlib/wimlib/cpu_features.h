#ifndef _WIMLIB_CPU_FEATURES_H
#define _WIMLIB_CPU_FEATURES_H

#include "wimlib/types.h"

#define X86_CPU_FEATURE_SSSE3		0x00000001
#define X86_CPU_FEATURE_SSE4_1		0x00000002
#define X86_CPU_FEATURE_SSE4_2		0x00000004
#define X86_CPU_FEATURE_AVX		0x00000008
#define X86_CPU_FEATURE_BMI2		0x00000010
#define X86_CPU_FEATURE_SHA		0x00000020

#define ARM_CPU_FEATURE_SHA1		0x00000001

#if (defined(__i386__) || defined(__x86_64__)) || \
    (defined (_M_IX86) || defined (_M_X64) || defined(_M_ARM64)) || \
    (defined(__aarch64__) && defined(__linux__)) || \
    (defined(__aarch64__) && defined(__linux__)) || \
    (defined(__aarch64__) && defined(__APPLE__)) || \
    (defined(__aarch64__) && defined(_WIN32))

#define CPU_FEATURES_ENABLED	1
extern u32 cpu_features;

void init_cpu_features(void);

#else

#define CPU_FEATURES_ENABLED	0
#define cpu_features 0

static inline void
init_cpu_features(void)
{
}

#endif

#endif /* _WIMLIB_CPU_FEATURES_H */
