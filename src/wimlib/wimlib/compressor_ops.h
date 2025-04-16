/*
 * compressor_ops.h
 *
 * Interface implemented by compressors for specific formats.
 */

#ifndef _WIMLIB_COMPRESSOR_OPS_H
#define _WIMLIB_COMPRESSOR_OPS_H

#include "wimlib/types.h"

struct compressor_ops {

	u64 (*get_needed_memory)(size_t max_block_size,
				 unsigned int compression_level,
				 bool destructive);

	int (*create_compressor)(size_t max_block_size,
				 unsigned int compression_level,
				 bool destructive,
				 void **private_ret);

	size_t (*compress)(const void *uncompressed_data,
			   size_t uncompressed_size,
			   void *compressed_data,
			   size_t compressed_size_avail,
			   void *private);

	void (*free_compressor)(void *private);
};

extern const struct compressor_ops lzx_compressor_ops;
extern const struct compressor_ops xpress_compressor_ops;
extern const struct compressor_ops lzms_compressor_ops;

#endif /* _WIMLIB_COMPRESSOR_OPS_H */
