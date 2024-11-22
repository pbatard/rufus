/* vi: set sw=4 ts=4: */
/*
 * Glue for zstd decompression
 * Copyright (c) 2021 Norbert Lange <nolange79@gmail.com>
 * Copyright (c) 2024 Pete Batard <pete@akeo.ie>
 *
 * Based on compress.c from the systemd project,
 * provided by Norbert Lange <nolange79@gmail.com>.
 * Which originally was copied from the streaming_decompression.c
 * example from the zstd project, written by Yann Collet 
 * 
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "bb_archive.h"
#include "zstd_deps.h"
#include "zstd_internal.h"

ZSTD_customMem ZSTD_defaultCMem = { ZSTD_customMalloc, ZSTD_customFree, NULL };

ZSTDLIB_API const char* ZSTD_getErrorName(size_t code) {
	return ERR_getErrorName(code);
}

ALWAYS_INLINE static size_t roundupsize(size_t size, size_t align)
{
	return (size + align - 1U) & ~(align - 1);
}

ALWAYS_INLINE static IF_DESKTOP(long long) int
unpack_zstd_stream_inner(transformer_state_t *xstate,
	ZSTD_DStream *dctx, void *out_buff)
{
	const U32 zstd_magic = ZSTD_MAGIC;
	const size_t in_allocsize = roundupsize(ZSTD_DStreamInSize(), 1024),
		out_allocsize = roundupsize(ZSTD_DStreamOutSize(), 1024);

	IF_DESKTOP(long long int total = 0;)
	size_t last_result = ZSTD_error_maxCode + 1;
	ssize_t nwrote = 0;
	unsigned input_fixup;
	void *in_buff = (char *)out_buff + out_allocsize;

	memcpy(in_buff, &zstd_magic, 4);
	input_fixup = xstate->signature_skipped ? 4 : 0;

	/* This loop assumes that the input file is one or more concatenated
	 * zstd streams. This example won't work if there is trailing non-zstd
	 * data at the end, but streaming decompression in general handles this
	 * case. ZSTD_decompressStream() returns 0 exactly when the frame is
	 * completed, and doesn't consume input after the frame.
	 */
	for (;;) {
		bool has_error = false;
		ZSTD_inBuffer input;
		ssize_t red;

		red = safe_read(xstate->src_fd, (char *)in_buff + input_fixup, (unsigned int)(in_allocsize - input_fixup));
		if (red < 0) {
			bb_perror_msg(bb_msg_read_error);
			return -1;
		}
		if (red == 0) {
			break;
		}

		input.src = in_buff;
		input.size = (size_t)red + input_fixup;
		input.pos = 0;
		input_fixup = 0;

		/* Given a valid frame, zstd won't consume the last byte of the
		 * frame until it has flushed all of the decompressed data of
		 * the frame. So input.pos < input.size means frame is not done
		 * or there is still output available.
		 */
		while (input.pos < input.size) {
			ZSTD_outBuffer output = { out_buff, out_allocsize, 0 };
			/* The return code is zero if the frame is complete, but
			 * there may be multiple frames concatenated together.
			 * Zstd will automatically reset the context when a
			 * frame is complete. Still, calling ZSTD_DCtx_reset()
			 * can be useful to reset the context to a clean state,
			 * for instance if the last decompression call returned
			 * an error.
			 */
			last_result = ZSTD_decompressStream(dctx, &output, &input);
			if (ZSTD_isError(last_result)) {
				has_error = true;
				break;
			}

			nwrote = transformer_write(xstate, output.dst, output.pos);
			if (nwrote < 0 && nwrote != -ENOSPC) {
				has_error = true;
				break;
			}
			IF_DESKTOP(total = (nwrote == -ENOSPC) ? xstate->mem_output_size_max : total + output.pos);
		}
		if (has_error)
			break;
	}

	if (last_result != 0) {
		/* The last return value from ZSTD_decompressStream did not end
		 * on a frame, but we reached the end of the file! We assume
		 * this is an error, and the input was truncated.
		 */
		if (last_result == ZSTD_error_maxCode + 1) {
			bb_simple_error_msg("could not read zstd data");
		} else {
#if defined(ZSTD_STRIP_ERROR_STRINGS) && ZSTD_STRIP_ERROR_STRINGS == 1
			bb_error_msg("zstd decoder error: %u", (unsigned)last_result);
#else
			bb_error_msg("zstd decoder error: %s", ZSTD_getErrorName(last_result));
#endif
		}
		return -1;
	}

	return IF_DESKTOP(total) + 0;
}

IF_DESKTOP(long long) int FAST_FUNC
unpack_zstd_stream(transformer_state_t *xstate)
{
	const size_t in_allocsize = roundupsize(ZSTD_DStreamInSize(), 1024),
		   out_allocsize = roundupsize(ZSTD_DStreamOutSize(), 1024);

	IF_DESKTOP(long long) int result;
	void *out_buff;
	ZSTD_DStream *dctx;

	dctx = ZSTD_createDStream();
	if (!dctx) {
		/* should be the only possibly reason of failure */
		bb_error_msg_and_die("memory exhausted");
	}

	out_buff = xmalloc(in_allocsize + out_allocsize);

	result = unpack_zstd_stream_inner(xstate, dctx, out_buff);
	free(out_buff);
	ZSTD_freeDStream(dctx);
	return result;
}
