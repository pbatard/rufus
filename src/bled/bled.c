/*
 * Bled (Base Library for Easy Decompression)
 *
 * Copyright © 2014-2023 Pete Batard <pete@akeo.ie>
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
read_t bled_read = NULL;
write_t bled_write = NULL;
progress_t bled_progress = NULL;
switch_t bled_switch = NULL;
unsigned long* bled_cancel_request;
static bool bled_initialized = 0;
jmp_buf bb_error_jmp;
char* bb_virtual_buf = NULL;
size_t bb_virtual_len = 0, bb_virtual_pos = 0;
int bb_virtual_fd = -1;
uint32_t BB_BUFSIZE = 0x10000;

static long long int unpack_none(transformer_state_t *xstate)
{
	bb_error_msg("This compression type is not supported");
	return -1;
}

unpacker_t unpacker[BLED_COMPRESSION_MAX] = {
	unpack_none,
	unpack_zip_stream,
	unpack_Z_stream,
	unpack_gz_stream,
	unpack_lzma_stream,
	unpack_bz2_stream,
	unpack_xz_stream,
	unpack_none,
	unpack_vtsi_stream,
};

/* Uncompress file 'src', compressed using 'type', to file 'dst' */
int64_t bled_uncompress(const char* src, const char* dst, int type)
{
	transformer_state_t xstate;
	int64_t ret = -1;

	if (!bled_initialized) {
		bb_error_msg("The library has not been initialized");
		return -1;
	}

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;

	xstate.src_fd = _openU(src, _O_RDONLY | _O_BINARY, 0);
	if (xstate.src_fd < 0) {
		bb_error_msg("Could not open '%s' (errno: %d)", src, errno);
		goto err;
	}

	xstate.dst_fd = _openU(dst, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
	if (xstate.dst_fd < 0) {
		bb_error_msg("Could not open '%s' (errno: %d)", dst, errno);
		goto err;
	}

	if ((type < 0) || (type >= BLED_COMPRESSION_MAX)) {
		bb_error_msg("Unsupported compression format");
		goto err;
	}

	if (setjmp(bb_error_jmp))
		goto err;

	ret = unpacker[type](&xstate);

err:
	free(xstate.dst_name);
	if (xstate.src_fd > 0)
		_close(xstate.src_fd);
	if (xstate.dst_fd > 0)
		_close(xstate.dst_fd);
	return ret;
}

/* Uncompress using Windows handles */
int64_t bled_uncompress_with_handles(HANDLE hSrc, HANDLE hDst, int type)
{
	transformer_state_t xstate;

	if (!bled_initialized) {
		bb_error_msg("The library has not been initialized");
		return -1;
	}

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;

	xstate.src_fd = _open_osfhandle((intptr_t)hSrc, _O_RDONLY);
	if (xstate.src_fd < 0) {
		bb_error_msg("Could not get source descriptor (errno: %d)", errno);
		return -1;
	}

	xstate.dst_fd = _open_osfhandle((intptr_t)hDst, 0);
	if (xstate.dst_fd < 0) {
		bb_error_msg("Could not get target descriptor (errno: %d)", errno);
		return -1;
	}

	if ((type < 0) || (type >= BLED_COMPRESSION_MAX)) {
		bb_error_msg("Unsupported compression format");
		return -1;
	}

	if (setjmp(bb_error_jmp))
		return -1;

	return unpacker[type](&xstate);
}

/* Uncompress file 'src', compressed using 'type', to buffer 'buf' of size 'size' */
int64_t bled_uncompress_to_buffer(const char* src, char* buf, size_t size, int type)
{
	transformer_state_t xstate;
	int64_t ret = -1;

	if (!bled_initialized) {
		bb_error_msg("The library has not been initialized");
		return -1;
	}

	if ((src == NULL) || (buf == NULL)) {
		bb_error_msg("Invalid parameter");
		return -1;
	}

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;

	if (src[0] == 0) {
		xstate.src_fd = bb_virtual_fd;
	} else {
		xstate.src_fd = _openU(src, _O_RDONLY | _O_BINARY, 0);
	}
	if (xstate.src_fd < 0) {
		bb_error_msg("Could not open '%s' (errno: %d)", src, errno);
		goto err;
	}

	xstate.mem_output_buf = buf;
	xstate.mem_output_size = 0;
	xstate.mem_output_size_max = size;

	if ((type < 0) || (type >= BLED_COMPRESSION_MAX)) {
		bb_error_msg("Unsupported compression format");
		goto err;
	}

	if (setjmp(bb_error_jmp))
		goto err;

	ret = unpacker[type](&xstate);

err:
	free(xstate.dst_name);
	if ((src[0] != 0) && (xstate.src_fd > 0))
		_close(xstate.src_fd);
	return ret;
}

/* Uncompress all files from archive 'src', compressed using 'type', to destination dir 'dir' */
int64_t bled_uncompress_to_dir(const char* src, const char* dir, int type)
{
	transformer_state_t xstate;
	int64_t ret = -1;

	if (!bled_initialized) {
		bb_error_msg("The library has not been initialized");
		return -1;
	}

	bb_total_rb = 0;
	init_transformer_state(&xstate);
	xstate.src_fd = -1;
	xstate.dst_fd = -1;

	xstate.src_fd = _openU(src, _O_RDONLY | _O_BINARY, 0);
	if (xstate.src_fd < 0) {
		bb_error_msg("Could not open '%s' (errno: %d)", src, errno);
		goto err;
	}

	xstate.dst_dir = dir;

	// Only zip archives are supported for now
	if (type != BLED_COMPRESSION_ZIP) {
		bb_error_msg("This compression format is not supported for directory extraction");
		goto err;
	}

	if (setjmp(bb_error_jmp))
		goto err;

	ret = unpacker[type](&xstate);

err:
	free(xstate.dst_name);
	if (xstate.src_fd > 0)
		_close(xstate.src_fd);
	if (xstate.dst_fd > 0)
		_close(xstate.dst_fd);
	return ret;
}

int64_t bled_uncompress_from_buffer_to_buffer(const char* src, const size_t src_len, char* dst, size_t dst_len, int type)
{
	int64_t ret;

	if (!bled_initialized) {
		bb_error_msg("The library has not been initialized");
		return -1;
	}

	if ((src == NULL) || (dst == NULL)) {
		bb_error_msg("Invalid parameter");
		return -1;
	}

	if (bb_virtual_buf != NULL) {
		bb_error_msg("Can not decompress more than one buffer at once");
		return -1;
	}

	bb_virtual_buf = (char*)src;
	bb_virtual_len = src_len;
	bb_virtual_pos = 0;
	bb_virtual_fd = 0;

	ret = bled_uncompress_to_buffer("", dst, dst_len, type);

	bb_virtual_buf = NULL;
	bb_virtual_len = 0;
	bb_virtual_fd = -1;

	return ret;
}

/* Initialize the library.
 * When the parameters are not NULL or zero you can:
 * - specify the buffer size to use (must be larger than 64KB and a power of two)
 * - specify the printf-like function you want to use to output message
 *   void print_function(const char* format, ...);
 * - specify the read/write functions you want to use;
 * - specify the function you want to use to display progress, based on number of source archive bytes read
 *   void progress_function(const uint64_t read_bytes);
 * - specify the function you want to use when switching files in an archive
 *   void switch_function(const char* filename, const uint64_t filesize);
 * - point to an unsigned long variable, to be used to cancel operations when set to non zero
 */
int bled_init(uint32_t buffer_size, printf_t print_function, read_t read_function, write_t write_function,
	progress_t progress_function, switch_t switch_function, unsigned long* cancel_request)
{
	if (bled_initialized)
		return -1;
	BB_BUFSIZE = buffer_size;
	/* buffer_size must be larger than 64 KB and a power of two */
	if (buffer_size < 0x10000 || (buffer_size & (buffer_size - 1)) != 0) {
		if (buffer_size != 0 && print_function != NULL)
			print_function("bled_init: invalid buffer_size, defaulting to 64 KB");
		BB_BUFSIZE = 0x10000;
	}
	bled_printf = print_function;
	bled_read = read_function;
	bled_write = write_function;
	bled_progress = progress_function;
	bled_switch = switch_function;
	bled_cancel_request = cancel_request;
	bled_initialized = true;
	return 0;
}

/* This call frees any resource used by the library */
void bled_exit(void)
{
	bled_printf = NULL;
	bled_progress = NULL;
	bled_switch = NULL;
	bled_cancel_request = NULL;
	if (global_crc32_table) {
		free(global_crc32_table);
		global_crc32_table = NULL;
	}
	bled_initialized = false;
}
