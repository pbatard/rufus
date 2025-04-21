/*
 * cpu_features.c - runtime CPU feature detection
 *
 * Copyright 2022-2023 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/cpu_features.h"

#if CPU_FEATURES_ENABLED

#include "wimlib/util.h"

#include <stdlib.h>
#include <string.h>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)

/*
 * With old GCC versions we have to manually save and restore the x86_32 PIC
 * register (ebx).  See: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=47602
 */
#if defined(__i386__) && defined(__PIC__)
#  define EBX_CONSTRAINT "=&r"
#else
#  define EBX_CONSTRAINT "=b"
#endif

#if defined(_MSC_VER)
#include <intrin.h>

#define read_xcr _xgetbv

static inline void
cpuid(u32 leaf, u32 subleaf, u32* a, u32* b, u32* c, u32* d)
{
	int regs[4];
	__cpuidex(regs, leaf, subleaf);
	*a = (uint32_t)regs[0];
	*b = (uint32_t)regs[1];
	*c = (uint32_t)regs[2];
	*d = (uint32_t)regs[3];
}

#else

/* Execute the CPUID instruction. */
static inline void
cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
	asm volatile(".ifnc %%ebx, %1; mov  %%ebx, %1; .endif\n"
		     "cpuid                                  \n"
		     ".ifnc %%ebx, %1; xchg %%ebx, %1; .endif\n"
		     : "=a" (*a), EBX_CONSTRAINT (*b), "=c" (*c), "=d" (*d)
		     : "a" (leaf), "c" (subleaf));
}

/* Read an extended control register. */
static inline u64
read_xcr(u32 index)
{
	u32 d, a;

	/*
	 * Execute the "xgetbv" instruction.  Old versions of binutils do not
	 * recognize this instruction, so list the raw bytes instead.
	 *
	 * This must be 'volatile' to prevent this code from being moved out
	 * from under the check for OSXSAVE.
	 */
	asm volatile(".byte 0x0f, 0x01, 0xd0" :
		     "=d" (d), "=a" (a) : "c" (index));

	return ((u64)d << 32) | a;
}
#endif

static u32
get_cpu_features(void)
{
	u32 max_leaf, a, b, c, d;
	u64 xcr0 = 0;
	u32 features = 0;

	/* EAX=0: Highest Function Parameter and Manufacturer ID */
	cpuid(0, 0, &max_leaf, &b, &c, &d);
	if (max_leaf < 1)
		return features;

	/* EAX=1: Processor Info and Feature Bits */
	cpuid(1, 0, &a, &b, &c, &d);
	if (c & (1 << 9))
		features |= X86_CPU_FEATURE_SSSE3;
	if (c & (1 << 19))
		features |= X86_CPU_FEATURE_SSE4_1;
	if (c & (1 << 20))
		features |= X86_CPU_FEATURE_SSE4_2;
	if (c & (1 << 27))
		xcr0 = read_xcr(0);
	if ((c & (1 << 28)) && ((xcr0 & 0x6) == 0x6))
		features |= X86_CPU_FEATURE_AVX;

	if (max_leaf < 7)
		return features;

	/* EAX=7, ECX=0: Extended Features */
	cpuid(7, 0, &a, &b, &c, &d);
	if (b & (1 << 8))
		features |= X86_CPU_FEATURE_BMI2;
	if (b & (1 << 29))
		features |= X86_CPU_FEATURE_SHA;

	return features;
}

#elif defined(__aarch64__) && defined(__linux__)

/*
 * On Linux, arm32 and arm64 CPU features can be detected by reading the
 * AT_HWCAP and AT_HWCAP2 values from /proc/self/auxv.
 *
 * Ideally we'd use the C library function getauxval(), but it's not guaranteed
 * to be available: it was only added to glibc in 2.16, and in Android it was
 * added to API level 18 for arm32 and level 21 for arm64.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define AT_HWCAP	16
#define AT_HWCAP2	26

static void scan_auxv(unsigned long *hwcap, unsigned long *hwcap2)
{
	int fd;
	unsigned long auxbuf[32];
	int filled = 0;
	int i;

	fd = open("/proc/self/auxv", O_RDONLY);
	if (fd < 0)
		return;

	for (;;) {
		do {
			int ret = read(fd, &((char *)auxbuf)[filled],
				       sizeof(auxbuf) - filled);
			if (ret <= 0) {
				if (ret < 0 && errno == EINTR)
					continue;
				goto out;
			}
			filled += ret;
		} while (filled < 2 * sizeof(long));

		i = 0;
		do {
			unsigned long type = auxbuf[i];
			unsigned long value = auxbuf[i + 1];

			if (type == AT_HWCAP)
				*hwcap = value;
			else if (type == AT_HWCAP2)
				*hwcap2 = value;
			i += 2;
			filled -= 2 * sizeof(long);
		} while (filled >= 2 * sizeof(long));

		memmove(auxbuf, &auxbuf[i], filled);
	}
out:
	close(fd);
}

static u32
get_cpu_features(void)
{
	unsigned long hwcap = 0;
	unsigned long hwcap2 = 0;
	u32 features = 0;

	scan_auxv(&hwcap, &hwcap2);

	if (hwcap & (1 << 5))	/* HWCAP_SHA1 */
		features |= ARM_CPU_FEATURE_SHA1;

	return features;
}

#elif defined(__aarch64__) && defined(__APPLE__)

/* On Apple platforms, arm64 CPU features can be detected via sysctlbyname(). */

#include <sys/types.h>
#include <sys/sysctl.h>

static const struct {
	const char *name;
	u32 feature;
} feature_sysctls[] = {
	{ "hw.optional.arm.FEAT_SHA1",	ARM_CPU_FEATURE_SHA1 },
};

static u32
get_cpu_features(void)
{
	u32 features = 0;

	for (size_t i = 0; i < ARRAY_LEN(feature_sysctls); i++) {
		const char *name = feature_sysctls[i].name;
		u32 val = 0;
		size_t valsize = sizeof(val);

		if (sysctlbyname(name, &val, &valsize, NULL, 0) == 0 &&
		    valsize == sizeof(val) && val == 1)
			features |= feature_sysctls[i].feature;
	}
	return features;
}

#elif (defined(__aarch64__) || (defined(_M_ARM64)) && defined(_WIN32))

#include <windows.h>

static u32
get_cpu_features(void)
{
	u32 features = 0;

	if (IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE))
		features |= ARM_CPU_FEATURE_SHA1;

	return features;
}

#else
#  error "CPU_FEATURES_ENABLED was set but no implementation is available!"
#endif

static const struct {
	const char *name;
	u32 feature;
} feature_table[] = {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
	{"ssse3",	X86_CPU_FEATURE_SSSE3},
	{"sse4.1",	X86_CPU_FEATURE_SSE4_1},
	{"sse4.2",	X86_CPU_FEATURE_SSE4_2},
	{"avx",		X86_CPU_FEATURE_AVX},
	{"bmi2",	X86_CPU_FEATURE_BMI2},
	{"sha",		X86_CPU_FEATURE_SHA},
	{"sha1",	X86_CPU_FEATURE_SHA},
#elif defined(__aarch64__) || defined(_M_ARM64)
	{"sha1",	ARM_CPU_FEATURE_SHA1},
#else
#  error "CPU_FEATURES_ENABLED was set but no features are defined!"
#endif
	{"*",		0xFFFFFFFF},
};

static u32
find_cpu_feature(const char *name, size_t namelen)
{
	for (size_t i = 0; i < ARRAY_LEN(feature_table); i++) {
		if (namelen == strlen(feature_table[i].name) &&
		    memcmp(name, feature_table[i].name, namelen) == 0)
			return feature_table[i].feature;
	}
	return 0;
}

u32 cpu_features;

void init_cpu_features(void)
{
	char *p, *sep;

	cpu_features = get_cpu_features();

	/*
	 * Allow disabling CPU features via an environmental variable for
	 * testing purposes.  Syntax is comma-separated list of feature names.
	 */
	p = getenv("WIMLIB_DISABLE_CPU_FEATURES");
	if (likely(p == NULL))
		return;
	for (; (sep = strchr(p, ',')) != NULL; p = sep + 1)
		cpu_features &= ~find_cpu_feature(p, sep - p);
	cpu_features &= ~find_cpu_feature(p, strlen(p));
}

#endif /* CPU_FEATURES_ENABLED */
