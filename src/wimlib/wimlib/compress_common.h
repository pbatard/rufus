/*
 * compress_common.h
 *
 * Header for compression code shared by multiple compression formats.
 */

#ifndef _WIMLIB_COMPRESS_COMMON_H
#define _WIMLIB_COMPRESS_COMMON_H

#include "wimlib/types.h"

#define MAX_NUM_SYMS		799	/* LZMS_MAX_NUM_SYMS */
#define MAX_CODEWORD_LEN	16

void
make_canonical_huffman_code(unsigned num_syms, unsigned max_codeword_len,
			    const u32 freqs[], u8 lens[], u32 codewords[]);

#endif /* _WIMLIB_COMPRESS_COMMON_H */
