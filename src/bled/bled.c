/*
 * Bled (Busybox Library for Easy Decompression)
 *
 * Copyright © 2014-2015 Pete Batard <pete@akeo.ie>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include "libbb.h"
#include "bb_archive.h"
#include "bled.h"

typedef long long int(*unpacker_t)(transformer_state_t *xstate);

/* Globals */
smallint bb_got_signal;
uint64_t bb_total_rb;
printf_t bled_printf = NULL;
progress_t bled_progress = NULL;
unsigned long* bled_cancel_request;
static bool bled_initialized = 0;
jmp_buf bb_error_jmp;

static long long int unpack_none(transformer_state_t *xstate)
{
	bb_printf("BLED_COMPRESSION_NONE is not supported");
	return -1;
}

unpacker_t unpacker[BLED_COMPRESSION_MAX] = {
	unpack_none,
	inflate_unzip,
	unpack_Z_stream,
	unpack_gz_stream,
	unpack_lzma_stream,
	unpack_bz2_stream,
	unpack_xz_stream
};

/* Uncompress file 'src', compressed using 'type', to file 'dst' */
int64_t bled_uncompress(const char* src, const char* dst, int type)
{
	transformer_state_t xstate;
	int64_t ret;

	if (!bled_initialized)
		return -1;

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;
	xstate.check_signature = 1;

	xstate.src_fd = _openU(src, _O_RDONLY | _O_BINARY, 0);
	if (xstate.src_fd < 0) {
		bb_printf("Could not open '%s' (errno: %d)", src, errno);
		goto err;
	}

	xstate.dst_fd = _openU(dst, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
	if (xstate.dst_fd < 0) {
		bb_printf("Could not open '%s' (errno: %d)", dst, errno);
		goto err;
	}

	if ((type < 0) || (type >= BLED_COMPRESSION_MAX)) {
		bb_printf("unsupported compression format");
		goto err;
	}

	if (setjmp(bb_error_jmp))
		goto err;
	ret = unpacker[type](&xstate);
	_close(xstate.src_fd);
	_close(xstate.dst_fd);
	return ret;

err:
	if (xstate.src_fd > 0)
		_close(xstate.src_fd);
	if (xstate.dst_fd > 0)
		_close(xstate.dst_fd);
	return -1;
}

/* Uncompress using Windows handles */
int64_t bled_uncompress_with_handles(HANDLE hSrc, HANDLE hDst, int type)
{
	transformer_state_t xstate;

	if (!bled_initialized)
		return -1;

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;
	xstate.check_signature = 1;

	xstate.src_fd = _open_osfhandle((intptr_t)hSrc, _O_RDONLY);
	if (xstate.src_fd < 0) {
		bb_printf("Could not get source descriptor (errno: %d)", errno);
		return -1;
	}

	xstate.dst_fd = _open_osfhandle((intptr_t)hDst, 0);
	if (xstate.dst_fd < 0) {
		bb_printf("Could not get target descriptor (errno: %d)", errno);
		return -1;
	}

	if ((type < 0) || (type >= BLED_COMPRESSION_MAX)) {
		bb_printf("unsupported compression format");
		return -1;
	}

	if (setjmp(bb_error_jmp))
		return -1;
	return unpacker[type](&xstate);
}

/* Initialize the library.
 * When the parameters are not NULL you can:
 * - specify the printf-like function you want to use to output message
 *   void print_function(const char* format, ...);
 * - specify the function you want to use to display progress, based on number of source archive bytes read
 *   void progress_function(const uint64_t read_bytes);
 * - point to an unsigned long variable, to be used to cancel operations when set to non zero
 */
int bled_init(printf_t print_function, progress_t progress_function, unsigned long* cancel_request)
{
	if (bled_initialized)
		return -1;
	bled_initialized = true;
	bled_printf = print_function;
	bled_progress = progress_function;
	bled_cancel_request = cancel_request;
	return 0;
}

/* This call frees any resource used by the library */
void bled_exit(void)
{
	bled_printf = NULL;
	bled_progress = NULL;
	bled_cancel_request = NULL;
	if (global_crc32_table)
		free(global_crc32_table);
	bled_initialized = false;
}
