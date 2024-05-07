/*
* Rufus: The Reliable USB Formatting Utility
* Constants and defines missing from various toolchains
* Copyright © 2016-2024 Pete Batard <pete@akeo.ie>
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

#include <windows.h>

#pragma once

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#define LO_ALIGN_X_TO_Y(x, y) (((x) / (y)) * (y))
#define HI_ALIGN_X_TO_Y(x, y) ((((x) + (y) - 1) / (y)) * (y))

#define IS_HEXASCII(c) (((c) >= '0' && (c) <= '9') || ((c) >= 'A' && (c) <= 'F') || ((c) >= 'a' && (c) <= 'f'))

/*
 * Prefetch 64 bytes at address m, for read-only operation
 * We account for these built-in calls doing nothing if the
 * line has already been fetched, or if the address is invalid.
 */
#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH64(m) do { __builtin_prefetch(m, 0, 0); __builtin_prefetch(m+32, 0, 0); } while(0)
#elif defined(_MSC_VER)
#if defined(_M_IX86) || defined (_M_X64)
#define PREFETCH64(m) do { _m_prefetch(m); _m_prefetch(m+32); } while(0)
#else
// _m_prefetch() doesn't seem to exist for MSVC/ARM
#define PREFETCH64(m)
#endif
#endif

/* Read/write with endianness swap */
#if defined (_MSC_VER) && (_MSC_VER >= 1300)
#include <stdlib.h>
#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)
#define bswap_uint64 _byteswap_uint64
#define bswap_uint32 _byteswap_ulong
#define bswap_uint16 _byteswap_ushort
#elif defined (__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)))
#define bswap_uint64 __builtin_bswap64
#define bswap_uint32 __builtin_bswap32
#define bswap_uint16 __builtin_bswap16
#endif
#define read_swap16(p) bswap_uint16(*(const uint16_t*)(const uint8_t*)(p))
#define read_swap32(p) bswap_uint32(*(const uint32_t*)(const uint8_t*)(p))
#define read_swap64(p) bswap_uint64(*(const uint64_t*)(const uint8_t*)(p))
#define write_swap16(p,v) (*(uint16_t*)(void*)(p)) = bswap_uint16(v)
#define write_swap32(p,v) (*(uint32_t*)(void*)(p)) = bswap_uint32(v)
#define write_swap64(p,v) (*(uint64_t*)(void*)(p)) = bswap_uint64(v)

/*
 * Nibbled from https://github.com/hanji/popcnt/blob/master/populationcount.cpp
 * Since MSVC x86_32 and/or ARM don't have intrinsic popcount and I don't have all day
 */
static __inline uint8_t popcnt8(uint8_t val)
{
	static const uint8_t nibble_lookup[16] = {
		0, 1, 1, 2, 1, 2, 2, 3,
		1, 2, 2, 3, 2, 3, 3, 4
	};
	return nibble_lookup[val & 0x0F] + nibble_lookup[val >> 4];
}

static __inline uint8_t popcnt64(register uint64_t u)
{
	u = (u & 0x5555555555555555ULL) + ((u >> 1) & 0x5555555555555555ULL);
	u = (u & 0x3333333333333333ULL) + ((u >> 2) & 0x3333333333333333ULL);
	u = (u & 0x0f0f0f0f0f0f0f0fULL) + ((u >> 4) & 0x0f0f0f0f0f0f0f0fULL);
	u = (u & 0x00ff00ff00ff00ffULL) + ((u >> 8) & 0x00ff00ff00ff00ffULL);
	u = (u & 0x0000ffff0000ffffULL) + ((u >> 16) & 0x0000ffff0000ffffULL);
	u = (u & 0x00000000ffffffffULL) + ((u >> 32) & 0x00000000ffffffffULL);
	return (uint8_t)u;
}

static __inline void *_reallocf(void *ptr, size_t size) {
	void *ret = realloc(ptr, size);
	if (!ret)
		free(ptr);
	return ret;
}

static __inline int _log2(register int val)
{
	int ret = 0;
	if (val < 0)
		return -2;
	while (val >>= 1)
		ret++;
	return ret;
}

/// <summary>
/// Remaps bits from a byte according to an 8x8 bit matrix.
/// </summary>
/// <param name="src">The byte to remap.</param>
/// <param name="map">An 8-byte array where each byte has a single bit set to the position to remap to.</param>
/// <param name="reverse">Indicates whether the reverse mapping operation should be applied.</param>
/// <returns>The remapped byte data.</returns>
static __inline uint8_t remap8(uint8_t src, uint8_t* map, const BOOL reverse)
{
	uint8_t i, m = 1, r = 0;
	for (i = 0, m = 1; i < (sizeof(src) * 8); i++, m <<= 1) {
		if (reverse) {
			if (src & map[i])
				r |= m;
		} else {
			if (src & m)
				r |= map[i];
		}
	}
	return r;
}

/// <summary>
/// Remaps bits from a 16-bit word according to a 16x16 bit matrix.
/// </summary>
/// <param name="src">The word to remap.</param>
/// <param name="map">A 16-word array where each word has a single bit set to the position to remap to.</param>
/// <param name="reverse">Indicates whether the reverse mapping operation should be applied.</param>
/// <returns>The remapped byte data.</returns>
static __inline uint16_t remap16(uint16_t src, uint16_t* map, const BOOL reverse)
{
	uint16_t i, m = 1, r = 0;
	for (i = 0, m = 1; i < (sizeof(src) * 8); i++, m <<= 1) {
		if (reverse) {
			if (src & map[i])
				r |= m;
		} else {
			if (src & m)
				r |= map[i];
		}
	}
	return r;
}

/* Why oh why does Microsoft have to make everybody suffer with their braindead use of Unicode? */
#define _RT_ICON			MAKEINTRESOURCEA(3)
#define _RT_DIALOG			MAKEINTRESOURCEA(5)
#define _RT_RCDATA			MAKEINTRESOURCEA(10)
#define _RT_GROUP_ICON		MAKEINTRESOURCEA((ULONG_PTR)(MAKEINTRESOURCEA(3) + 11))

/* MinGW doesn't know these */
#ifndef WM_CLIENTSHUTDOWN
#define WM_CLIENTSHUTDOWN                       0x3B
#endif
#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA                       0x49
#endif
#ifndef INTERNET_OPTION_ENABLE_HTTP_PROTOCOL
#define INTERNET_OPTION_ENABLE_HTTP_PROTOCOL    148
#endif
#ifndef INTERNET_OPTION_HTTP_DECODING
#define INTERNET_OPTION_HTTP_DECODING           65
#endif
#ifndef HTTP_PROTOCOL_FLAG_HTTP2
#define HTTP_PROTOCOL_FLAG_HTTP2                2
#endif
#ifndef ERROR_OFFSET_ALIGNMENT_VIOLATION
#define ERROR_OFFSET_ALIGNMENT_VIOLATION        327
#endif

/* RISC-V is still bleeding edge */
#ifndef IMAGE_FILE_MACHINE_RISCV32
#define IMAGE_FILE_MACHINE_RISCV32 0x5032
#endif
#ifndef IMAGE_FILE_MACHINE_RISCV64
#define IMAGE_FILE_MACHINE_RISCV64 0x5064
#endif
#ifndef IMAGE_FILE_MACHINE_RISCV128
#define IMAGE_FILE_MACHINE_RISCV128 0x5128
#endif
