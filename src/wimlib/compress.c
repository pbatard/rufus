/*
 * compress.c
 *
 * Generic functions for compression, wrapping around actual compression
 * implementations.
 */

/*
 * Copyright (C) 2013, 2014 Eric Biggers
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

#include "wimlib.h"
#include "wimlib/error.h"
#include "wimlib/compressor_ops.h"
#include "wimlib/util.h"

struct wimlib_compressor {
	const struct compressor_ops *ops;
	void *private;
	enum wimlib_compression_type ctype;
	size_t max_block_size;
};

static const struct compressor_ops * const compressor_ops[] = {
	[WIMLIB_COMPRESSION_TYPE_XPRESS] = &xpress_compressor_ops,
	[WIMLIB_COMPRESSION_TYPE_LZX]    = &lzx_compressor_ops,
	[WIMLIB_COMPRESSION_TYPE_LZMS]   = &lzms_compressor_ops,
};

/* Scale: 10 = low, 50 = medium, 100 = high */

#define DEFAULT_COMPRESSION_LEVEL 50

static unsigned int default_compression_levels[ARRAY_LEN(compressor_ops)];

static bool
compressor_ctype_valid(int ctype)
{
	return (ctype >= 0 &&
		ctype < ARRAY_LEN(compressor_ops) &&
		compressor_ops[ctype] != NULL);
}

WIMLIBAPI int
wimlib_set_default_compression_level(int ctype, unsigned int compression_level)
{
	if (ctype == -1) {
		for (int i = 0; i < ARRAY_LEN(default_compression_levels); i++)
			default_compression_levels[i] = compression_level;
	} else {
		if (!compressor_ctype_valid(ctype))
			return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

		default_compression_levels[ctype] = compression_level;
	}
	return 0;
}

WIMLIBAPI u64
wimlib_get_compressor_needed_memory(enum wimlib_compression_type ctype,
				    size_t max_block_size,
				    unsigned int compression_level)
{
	bool destructive;
	const struct compressor_ops *ops;
	u64 size;

	destructive = (compression_level & WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE);
	compression_level &= ~WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE;

	if (!compressor_ctype_valid(ctype))
		return 0;

	if (compression_level > 0xFFFFFF)
		return 0;

	if (max_block_size == 0)
		return 0;

	ops = compressor_ops[ctype];

	if (compression_level == 0)
		compression_level = default_compression_levels[ctype];
	if (compression_level == 0)
		compression_level = DEFAULT_COMPRESSION_LEVEL;

	if (ops->get_needed_memory) {
		size = ops->get_needed_memory(max_block_size, compression_level,
					      destructive);

		/* 0 is never valid and indicates an invalid max_block_size.  */
		if (size == 0)
			return 0;
	} else {
		size = 0;
	}
	return size + sizeof(struct wimlib_compressor);
}

WIMLIBAPI int
wimlib_create_compressor(enum wimlib_compression_type ctype,
			 size_t max_block_size,
			 unsigned int compression_level,
			 struct wimlib_compressor **c_ret)
{
	bool destructive;
	struct wimlib_compressor *c;
	int ret;

	ret = wimlib_global_init(0);
	if (ret)
		return ret;

	destructive = (compression_level & WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE);
	compression_level &= ~WIMLIB_COMPRESSOR_FLAG_DESTRUCTIVE;

	if (!compressor_ctype_valid(ctype))
		return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

	if (compression_level > 0xFFFFFF)
		return WIMLIB_ERR_INVALID_PARAM;

	if (c_ret == NULL)
		return WIMLIB_ERR_INVALID_PARAM;

	if (max_block_size == 0)
		return WIMLIB_ERR_INVALID_PARAM;

	c = MALLOC(sizeof(*c));
	if (c == NULL)
		return WIMLIB_ERR_NOMEM;
	c->ops = compressor_ops[ctype];
	c->private = NULL;
	c->ctype = ctype;
	c->max_block_size = max_block_size;
	if (c->ops->create_compressor) {
		if (compression_level == 0)
			compression_level = default_compression_levels[ctype];
		if (compression_level == 0)
			compression_level = DEFAULT_COMPRESSION_LEVEL;

		ret = c->ops->create_compressor(max_block_size,
						compression_level,
						destructive,
						&c->private);
		if (ret) {
			FREE(c);
			return ret;
		}
	}
	*c_ret = c;
	return 0;
}

WIMLIBAPI size_t
wimlib_compress(const void *uncompressed_data, size_t uncompressed_size,
		void *compressed_data, size_t compressed_size_avail,
		struct wimlib_compressor *c)
{
	if (unlikely(uncompressed_size == 0 || uncompressed_size > c->max_block_size))
		return 0;

	return c->ops->compress(uncompressed_data, uncompressed_size,
				compressed_data, compressed_size_avail,
				c->private);
}

WIMLIBAPI void
wimlib_free_compressor(struct wimlib_compressor *c)
{
	if (c) {
		if (c->ops->free_compressor)
			c->ops->free_compressor(c->private);
		FREE(c);
	}
}
