/*
 * compress_serial.c
 *
 * Compress chunks of data (serial version).
 */

/*
 * Copyright (C) 2013 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "wimlib.h"
#include "wimlib/assert.h"
#include "wimlib/chunk_compressor.h"
#include "wimlib/util.h"

struct serial_chunk_compressor {
	struct chunk_compressor base;
	struct wimlib_compressor *compressor;
	u8 *udata;
	u8 *cdata;
	u32 usize;
	u8 *result_data;
	u32 result_size;
};

static void
serial_chunk_compressor_destroy(struct chunk_compressor *_ctx)
{
	struct serial_chunk_compressor *ctx = (struct serial_chunk_compressor*)_ctx;

	if (ctx == NULL)
		return;

	wimlib_free_compressor(ctx->compressor);
	FREE(ctx->udata);
	FREE(ctx->cdata);
	FREE(ctx);
}

static void *
serial_chunk_compressor_get_chunk_buffer(struct chunk_compressor *_ctx)
{
	struct serial_chunk_compressor *ctx = (struct serial_chunk_compressor*)_ctx;

	if (ctx->result_data)
		return NULL;
	return ctx->udata;
}

static void
serial_chunk_compressor_signal_chunk_filled(struct chunk_compressor *_ctx, u32 usize)
{
	struct serial_chunk_compressor *ctx = (struct serial_chunk_compressor*)_ctx;
	u32 csize;

	wimlib_assert(usize > 0);
	wimlib_assert(usize <= ctx->base.out_chunk_size);

	ctx->usize = usize;
	csize = wimlib_compress(ctx->udata, usize, ctx->cdata, usize - 1,
				ctx->compressor);
	if (csize) {
		ctx->result_data = ctx->cdata;
		ctx->result_size = csize;
	} else {
		ctx->result_data = ctx->udata;
		ctx->result_size = ctx->usize;
	}
}

static bool
serial_chunk_compressor_get_compression_result(struct chunk_compressor *_ctx,
					       const void **cdata_ret, u32 *csize_ret,
					       u32 *usize_ret)
{
	struct serial_chunk_compressor *ctx = (struct serial_chunk_compressor *)_ctx;

	if (!ctx->result_data)
		return false;

	*cdata_ret = ctx->result_data;
	*csize_ret = ctx->result_size;
	*usize_ret = ctx->usize;

	ctx->result_data = NULL;
	return true;
}

int
new_serial_chunk_compressor(int out_ctype, u32 out_chunk_size,
			    struct chunk_compressor **compressor_ret)
{
	struct serial_chunk_compressor *ctx;
	int ret;

	wimlib_assert(out_chunk_size > 0);

	ctx = CALLOC(1, sizeof(*ctx));
	if (ctx == NULL)
		return WIMLIB_ERR_NOMEM;

	ctx->base.out_ctype = out_ctype;
	ctx->base.out_chunk_size = out_chunk_size;
	ctx->base.num_threads = 1;
	ctx->base.destroy = serial_chunk_compressor_destroy;
	ctx->base.get_chunk_buffer = serial_chunk_compressor_get_chunk_buffer;
	ctx->base.signal_chunk_filled = serial_chunk_compressor_signal_chunk_filled;
	ctx->base.get_compression_result = serial_chunk_compressor_get_compression_result;

	ret = wimlib_create_compressor(out_ctype, out_chunk_size,
				       WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE,
				       &ctx->compressor);
	if (ret)
		goto err;

	ctx->udata = MALLOC(out_chunk_size);
	ctx->cdata = MALLOC(out_chunk_size - 1);
	if (ctx->udata == NULL || ctx->cdata == NULL) {
		ret = WIMLIB_ERR_NOMEM;
		goto err;
	}
	ctx->result_data = NULL;

	*compressor_ret = &ctx->base;
	return 0;

err:
	serial_chunk_compressor_destroy(&ctx->base);
	return ret;
}
