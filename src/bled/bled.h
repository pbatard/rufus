/*
 * Bled (Busybox Library for Easy Decompression)
 *
 * Copyright © 2014 Pete Batard <pete@akeo.ie>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include <windows.h>
#include <stdint.h>

#pragma once

#ifndef ARRAYSIZE
#define ARRAYSIZE(A)                (sizeof(A)/sizeof((A)[0]))
#endif

typedef void (*printf_t) (const char* format, ...);
typedef void (*progress_t) (const uint64_t processed_bytes);

typedef enum {
	BLED_COMPRESSION_NONE = 0,
	BLED_COMPRESSION_ZIP,		// .zip
	BLED_COMPRESSION_LZW,		// .Z
	BLED_COMPRESSION_GZIP,		// .gz
	BLED_COMPRESSION_LZMA,		// .lzma
	BLED_COMPRESSION_BZIP2,		// .bz2
	BLED_COMPRESSION_XZ,		// .xz
//	BLED_COMPRESSION_7ZIP		// .7z
	BLED_COMPRESSION_MAX
} bled_compression_type;

/* Uncompress file 'src', compressed using 'type', to file 'dst' */
int64_t bled_uncompress(const char* src, const char* dst, int type);

int64_t bled_uncompress_with_handles(HANDLE hSrc, HANDLE hDst, int type);

/* Initialize the library */
int bled_init(printf_t print_function, progress_t progress_function);

void bled_exit(void);
