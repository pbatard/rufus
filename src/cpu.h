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

/*
 * Primarily added to support SHA instructions on x86 machines.
 * SHA acceleration is becoming as ubiquitous as AES acceleration.
 * SHA support was introduced in Intel Goldmont architecture, like
 * Celeron J3455 and Pentium J4205. The instructions are now present
 * in AMD Ryzen 3 (Zen architecture) and above, and Intel Core
 * 10th-gen processors (Ice Lake), 11th-gen processors (Rocket Lake)
 * and above.
 *
 * Typical benchmarks for x86 SHA acceleration is about a 6x to 10x
 * speedup over a C/C++ implementation. The rough measurements are
 * 1.0 to 1.8 cpb for SHA-1, and 1.5 to 2.5 cpb for SHA-256. On a
 * Celeron J3455, that's 1.1 GB/s for SHA-1 and 800 MB/s for SHA-256.
 * On a 10th-gen Core i5, that's about 1.65 GB/s for SHA-1 and about
 * 1.3 GB/s for SHA-256.
 */

#include "rufus.h"

#pragma once

#ifdef _MSC_VER
#define RUFUS_MSC_VERSION (_MSC_VER)
#if (RUFUS_MSC_VERSION < 1900)
#error "Your compiler is too old to build this application"
#endif
#endif

#if defined(__GNUC__)
#define RUFUS_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if (RUFUS_GCC_VERSION < 40900)
#error "Your compiler is too old to build this application"
#endif
#endif

#ifdef __INTEL_COMPILER
#define RUFUS_INTEL_VERSION (__INTEL_COMPILER)
#if (RUFUS_INTEL_VERSION < 1600)
#error "Your compiler is too old to build this application"
#endif
#endif

#if defined(__clang__)
#define RUFUS_CLANG_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#if (RUFUS_CLANG_VERSION < 30400)
#error "Your compiler is too old to build this application"
#endif
#endif

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__i386) || \
     defined(_X86_) || defined(__I86__) || defined(__x86_64__))
#define CPU_X86_SHA1_ACCELERATION 1
#define CPU_X86_SHA256_ACCELERATION 1
#endif

extern BOOL cpu_has_sha1_accel, cpu_has_sha256_accel;

extern BOOL DetectSHA1Acceleration(void);
extern BOOL DetectSHA256Acceleration(void);
