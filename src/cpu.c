/*
 * Rufus: The Reliable USB Formatting Utility
 * CPU features detection
 * Copyright © 2022 Pete Batard <pete@akeo.ie>
 * Copyright © 2022 Jeffrey Walton <noloader@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"

#if (defined(CPU_X86_SHA1_ACCELERATION) || defined(CPU_X86_SHA256_ACCELERATION))
#if defined(RUFUS_MSC_VERSION)
#include <intrin.h>
#elif (defined(RUFUS_GCC_VERSION) || defined(RUFUS_CLANG_VERSION))
#include <x86Intrin.h>
#elif defined(RUFUS_INTEL_VERSION)
#include <immintrin.h>
#endif
#endif

BOOL cpu_has_sha1_accel = FALSE;
BOOL cpu_has_sha256_accel = FALSE;

/*
 * Three elements must be in place to make a meaningful call to the
 * DetectSHA###Acceleration() calls. First, the compiler must support
 * the underlying intrinsics. Second, the platform must provide a
 * cpuid() function. And third, the cpu must actually support the SHA-1
 * and SHA-256 instructions.
 *
 * If any of the conditions are not met, then DetectSHA###Acceleration()
 * returns FALSE.
 */

/*
 * Detect if the processor supports SHA-1 acceleration. We only check for
 * the three ISAs we need - SSSE3, SSE4.1 and SHA. We don't check for OS
 * support or XSAVE because that's been enabled since Windows 2000.
 */
BOOL DetectSHA1Acceleration(void)
{
#if defined(CPU_X86_SHA1_ACCELERATION)
#if defined(_MSC_VER)
	uint32_t regs0[4] = {0,0,0,0}, regs1[4] = {0,0,0,0}, regs7[4] = {0,0,0,0};
	const uint32_t SSSE3_BIT = 1u <<  9; /* Function 1, Bit  9 of ECX */
	const uint32_t SSE41_BIT = 1u << 19; /* Function 1, Bit 19 of ECX */
	const uint32_t SHA_BIT   = 1u << 29; /* Function 7, Bit 29 of EBX */

	__cpuid(regs0, 0);
	const uint32_t highest = regs0[0]; /*EAX*/

	if (highest >= 0x01) {
		__cpuidex(regs1, 1, 0);
	}
	if (highest >= 0x07) {
		__cpuidex(regs7, 7, 0);
	}

	return (regs1[2] /*ECX*/ & SSSE3_BIT) && (regs1[2] /*ECX*/ & SSE41_BIT) && (regs7[1] /*EBX*/ & SHA_BIT) ? TRUE : FALSE;
#elif defined(__GNUC__) || defined(__clang__)
	/* __builtin_cpu_supports available in GCC 4.8.1 and above */
	return __builtin_cpu_supports("ssse3") && __builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("sha") ? TRUE : FALSE;
#elif defined(__INTEL_COMPILER)
	/* https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_may_i_use_cpu_feature */
	return _may_i_use_cpu_feature(_FEATURE_SSSE3|_FEATURE_SSE4_1|_FEATURE_SHA) ? TRUE : FALSE;
#else
	return FALSE;
#endif
#else
	return FALSE;
#endif
}

/*
 * Detect if the processor supports SHA-256 acceleration. We only check for
 * the three ISAs we need - SSSE3, SSE4.1 and SHA. We don't check for OS
 * support or XSAVE because that's been enabled since Windows 2000.
 */
BOOL DetectSHA256Acceleration(void)
{
#if defined(CPU_X86_SHA256_ACCELERATION)
#if defined(_MSC_VER)
	uint32_t regs0[4] = {0,0,0,0}, regs1[4] = {0,0,0,0}, regs7[4] = {0,0,0,0};
	const uint32_t SSSE3_BIT = 1u <<  9; /* Function 1, Bit  9 of ECX */
	const uint32_t SSE41_BIT = 1u << 19; /* Function 1, Bit 19 of ECX */
	const uint32_t SHA_BIT   = 1u << 29; /* Function 7, Bit 29 of EBX */

	__cpuid(regs0, 0);
	const uint32_t highest = regs0[0]; /*EAX*/

	if (highest >= 0x01) {
		__cpuidex(regs1, 1, 0);
	}
	if (highest >= 0x07) {
		__cpuidex(regs7, 7, 0);
	}

	return (regs1[2] /*ECX*/ & SSSE3_BIT) && (regs1[2] /*ECX*/ & SSE41_BIT) && (regs7[1] /*EBX*/ & SHA_BIT) ? TRUE : FALSE;
#elif defined(__GNUC__) || defined(__clang__)
	/* __builtin_cpu_supports available in GCC 4.8.1 and above */
	return __builtin_cpu_supports("ssse3") && __builtin_cpu_supports("sse4.1") && __builtin_cpu_supports("sha") ? TRUE : FALSE;
#elif defined(__INTEL_COMPILER)
	/* https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_may_i_use_cpu_feature */
	return _may_i_use_cpu_feature(_FEATURE_SSSE3|_FEATURE_SSE4_1|_FEATURE_SHA) ? TRUE : FALSE;
#else
	return FALSE;
#endif
#else
	return FALSE;
#endif
}
