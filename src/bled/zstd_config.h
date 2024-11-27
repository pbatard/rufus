#ifndef ZSTD_CONFIG
#define ZSTD_CONFIG

#define ZSTD_DEBUGLEVEL                 0
#define ZSTD_LEGACY_SUPPORT             0
#define ZSTD_LIB_DEPRECATED             0
#define ZSTD_NO_UNUSED_FUNCTIONS        1
#define ZSTD_STRIP_ERROR_STRINGS        0
#define ZSTD_TRACE                      0
#define ZSTD_DECOMPRESS_DICTIONARY      0
#define ZSTD_DECOMPRESS_MULTIFRAME      0
#define ZSTD_NO_TRACE                   1

#if CONFIG_FEATURE_ZSTD_SMALL >= 9
#define ZSTD_NO_INLINE 1
#endif

#if CONFIG_FEATURE_ZSTD_SMALL >= 7
#define HUF_FORCE_DECOMPRESS_X1 1
#define ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT 1
#elif CONFIG_FEATURE_ZSTD_SMALL >= 5
#define HUF_FORCE_DECOMPRESS_X1 1
#endif

#if CONFIG_FEATURE_ZSTD_SMALL <= 7
/* doesnt blow up code too much, -O3 is horrible */
#ifdef __GNUC__
#pragma GCC optimize ("O2")
#endif
#endif

#if CONFIG_FEATURE_ZSTD_SMALL > 0
/* no dynamic detection of bmi2 instruction,
 * prefer using CFLAGS setting to -march=haswell or similar */
# if !defined(__BMI2__)
#  define DYNAMIC_BMI2 0
# endif
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#ifndef __has_feature
#define __has_feature(x) 0
#endif

/* Include zstd_deps.h first with all the options we need enabled. */
#define ZSTD_DEPS_NEED_MALLOC
#define ZSTD_DEPS_NEED_MATH64
#define ZSTD_DEPS_NEED_STDINT

#endif
