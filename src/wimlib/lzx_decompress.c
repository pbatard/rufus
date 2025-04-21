/*
 * lzx_decompress.c
 *
 * A decompressor for the LZX compression format, as used in WIM files.
 */

/*
 * Copyright (C) 2012-2016 Eric Biggers
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

/*
 * LZX is an LZ77 and Huffman-code based compression format that has many
 * similarities to DEFLATE (the format used by zlib/gzip).  The compression
 * ratio is as good or better than DEFLATE.  See lzx_compress.c for a format
 * overview, and see https://en.wikipedia.org/wiki/LZX_(algorithm) for a
 * historical overview.  Here I make some pragmatic notes.
 *
 * The old specification for LZX is the document "Microsoft LZX Data Compression
 * Format" (1997).  It defines the LZX format as used in cabinet files.  Allowed
 * window sizes are 2^n where 15 <= n <= 21.  However, this document contains
 * several errors, so don't read too much into it...
 *
 * The new specification for LZX is the document "[MS-PATCH]: LZX DELTA
 * Compression and Decompression" (2014).  It defines the LZX format as used by
 * Microsoft's binary patcher.  It corrects several errors in the 1997 document
 * and extends the format in several ways --- namely, optional reference data,
 * up to 2^25 byte windows, and longer match lengths.
 *
 * WIM files use a more restricted form of LZX.  No LZX DELTA extensions are
 * present, the window is not "sliding", E8 preprocessing is done
 * unconditionally with a fixed file size, and the maximum window size is always
 * 2^15 bytes (equal to the size of each "chunk" in a compressed WIM resource).
 * This code is primarily intended to implement this form of LZX.  But although
 * not compatible with WIMGAPI, this code also supports maximum window sizes up
 * to 2^21 bytes.
 *
 * TODO: Add support for window sizes up to 2^25 bytes.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "wimlib/decompressor_ops.h"
#include "wimlib/decompress_common.h"
#include "wimlib/error.h"
#include "wimlib/lzx_common.h"
#include "wimlib/util.h"

/* These values are chosen for fast decompression.  */
#define LZX_MAINCODE_TABLEBITS		11
#define LZX_LENCODE_TABLEBITS		9
#define LZX_PRECODE_TABLEBITS		6
#define LZX_ALIGNEDCODE_TABLEBITS	7

#define LZX_READ_LENS_MAX_OVERRUN 50

PRAGMA_BEGIN_ALIGN(DECODE_TABLE_ALIGNMENT)
struct lzx_decompressor {

	DECODE_TABLE(maincode_decode_table, LZX_MAINCODE_MAX_NUM_SYMBOLS,
		     LZX_MAINCODE_TABLEBITS, LZX_MAX_MAIN_CODEWORD_LEN);
	u8 maincode_lens[LZX_MAINCODE_MAX_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];

	DECODE_TABLE(lencode_decode_table, LZX_LENCODE_NUM_SYMBOLS,
		     LZX_LENCODE_TABLEBITS, LZX_MAX_LEN_CODEWORD_LEN);
	u8 lencode_lens[LZX_LENCODE_NUM_SYMBOLS + LZX_READ_LENS_MAX_OVERRUN];

	union {
		DECODE_TABLE(alignedcode_decode_table, LZX_ALIGNEDCODE_NUM_SYMBOLS,
			     LZX_ALIGNEDCODE_TABLEBITS, LZX_MAX_ALIGNED_CODEWORD_LEN);
		u8 alignedcode_lens[LZX_ALIGNEDCODE_NUM_SYMBOLS];
	};

	union {
		DECODE_TABLE(precode_decode_table, LZX_PRECODE_NUM_SYMBOLS,
			     LZX_PRECODE_TABLEBITS, LZX_MAX_PRE_CODEWORD_LEN);
		u8 precode_lens[LZX_PRECODE_NUM_SYMBOLS];
		u8 extra_offset_bits[LZX_MAX_OFFSET_SLOTS];
	};

	union {
		DECODE_TABLE_WORKING_SPACE(maincode_working_space,
					   LZX_MAINCODE_MAX_NUM_SYMBOLS,
					   LZX_MAX_MAIN_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(lencode_working_space,
					   LZX_LENCODE_NUM_SYMBOLS,
					   LZX_MAX_LEN_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(alignedcode_working_space,
					   LZX_ALIGNEDCODE_NUM_SYMBOLS,
					   LZX_MAX_ALIGNED_CODEWORD_LEN);
		DECODE_TABLE_WORKING_SPACE(precode_working_space,
					   LZX_PRECODE_NUM_SYMBOLS,
					   LZX_MAX_PRE_CODEWORD_LEN);
	};

	unsigned window_order;
	unsigned num_main_syms;

	/* Like lzx_extra_offset_bits[], but does not include the entropy-coded
	 * bits of aligned offset blocks */
	u8 extra_offset_bits_minus_aligned[LZX_MAX_OFFSET_SLOTS];

} PRAGMA_END_ALIGN(DECODE_TABLE_ALIGNMENT);

/* Read a Huffman-encoded symbol using the precode. */
static forceinline unsigned
read_presym(const struct lzx_decompressor *d, struct input_bitstream *is)
{
	return read_huffsym(is, d->precode_decode_table,
			    LZX_PRECODE_TABLEBITS, LZX_MAX_PRE_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the main code. */
static forceinline unsigned
read_mainsym(const struct lzx_decompressor *d, struct input_bitstream *is)
{
	return read_huffsym(is, d->maincode_decode_table,
			    LZX_MAINCODE_TABLEBITS, LZX_MAX_MAIN_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the length code. */
static forceinline unsigned
read_lensym(const struct lzx_decompressor *d, struct input_bitstream *is)
{
	return read_huffsym(is, d->lencode_decode_table,
			    LZX_LENCODE_TABLEBITS, LZX_MAX_LEN_CODEWORD_LEN);
}

/* Read a Huffman-encoded symbol using the aligned offset code. */
static forceinline unsigned
read_alignedsym(const struct lzx_decompressor *d, struct input_bitstream *is)
{
	return read_huffsym(is, d->alignedcode_decode_table,
			    LZX_ALIGNEDCODE_TABLEBITS, LZX_MAX_ALIGNED_CODEWORD_LEN);
}

/*
 * Read a precode from the compressed input bitstream, then use it to decode
 * @num_lens codeword length values and write them to @lens.
 */
static int
lzx_read_codeword_lens(struct lzx_decompressor *d, struct input_bitstream *is,
		       u8 *lens, unsigned num_lens)
{
	u8 *len_ptr = lens;
	u8 *lens_end = lens + num_lens;

	/* Read the lengths of the precode codewords.  These are stored
	 * explicitly. */
	for (int i = 0; i < LZX_PRECODE_NUM_SYMBOLS; i++) {
		d->precode_lens[i] =
			bitstream_read_bits(is, LZX_PRECODE_ELEMENT_SIZE);
	}

	/* Build the decoding table for the precode. */
	if (make_huffman_decode_table(d->precode_decode_table,
				      LZX_PRECODE_NUM_SYMBOLS,
				      LZX_PRECODE_TABLEBITS,
				      d->precode_lens,
				      LZX_MAX_PRE_CODEWORD_LEN,
				      d->precode_working_space))
		return -1;

	/* Decode the codeword lengths.  */
	do {
		unsigned presym;
		u8 len;

		/* Read the next precode symbol.  */
		presym = read_presym(d, is);
		if (presym < 17) {
			/* Difference from old length  */
			len = *len_ptr - presym;
			if ((s8)len < 0)
				len += 17;
			*len_ptr++ = len;
		} else {
			/* Special RLE values  */

			unsigned run_len;

			if (presym == 17) {
				/* Run of 0's  */
				run_len = 4 + bitstream_read_bits(is, 4);
				len = 0;
			} else if (presym == 18) {
				/* Longer run of 0's  */
				run_len = 20 + bitstream_read_bits(is, 5);
				len = 0;
			} else {
				/* Run of identical lengths  */
				run_len = 4 + bitstream_read_bits(is, 1);
				presym = read_presym(d, is);
				if (unlikely(presym > 17))
					return -1;
				len = *len_ptr - presym;
				if ((s8)len < 0)
					len += 17;
			}

			do {
				*len_ptr++ = len;
			} while (--run_len);
			/*
			 * The worst case overrun is when presym == 18,
			 * run_len == 20 + 31, and only 1 length was remaining.
			 * So LZX_READ_LENS_MAX_OVERRUN == 50.
			 *
			 * Overrun while reading the first half of maincode_lens
			 * can corrupt the previous values in the second half.
			 * This doesn't really matter because the resulting
			 * lengths will still be in range, and data that
			 * generates overruns is invalid anyway.
			 */
		}
	} while (len_ptr < lens_end);

	return 0;
}

/*
 * Read the header of an LZX block.  For all block types, the block type and
 * size is saved in *block_type_ret and *block_size_ret, respectively.  For
 * compressed blocks, the codeword lengths are also saved.  For uncompressed
 * blocks, the recent offsets queue is also updated.
 */
static int
lzx_read_block_header(struct lzx_decompressor *d, struct input_bitstream *is,
		      u32 recent_offsets[], int *block_type_ret,
		      u32 *block_size_ret)
{
	int block_type;
	u32 block_size;

	bitstream_ensure_bits(is, 4);

	/* Read the block type. */
	block_type = bitstream_pop_bits(is, 3);

	/* Read the block size. */
	if (bitstream_pop_bits(is, 1)) {
		block_size = LZX_DEFAULT_BLOCK_SIZE;
	} else {
		block_size = bitstream_read_bits(is, 16);
		if (d->window_order >= 16) {
			block_size <<= 8;
			block_size |= bitstream_read_bits(is, 8);
		}
	}

	switch (block_type) {

	case LZX_BLOCKTYPE_ALIGNED:

		/* Read the aligned offset codeword lengths. */

		for (int i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
			d->alignedcode_lens[i] =
				bitstream_read_bits(is,
						    LZX_ALIGNEDCODE_ELEMENT_SIZE);
		}

		/* Fall though, since the rest of the header for aligned offset
		 * blocks is the same as that for verbatim blocks.  */

	case LZX_BLOCKTYPE_VERBATIM:

		/* Read the main codeword lengths, which are divided into two
		 * parts: literal symbols and match headers. */

		if (lzx_read_codeword_lens(d, is, d->maincode_lens,
					   LZX_NUM_CHARS))
			return -1;

		if (lzx_read_codeword_lens(d, is, d->maincode_lens + LZX_NUM_CHARS,
					   d->num_main_syms - LZX_NUM_CHARS))
			return -1;


		/* Read the length codeword lengths. */

		if (lzx_read_codeword_lens(d, is, d->lencode_lens,
					   LZX_LENCODE_NUM_SYMBOLS))
			return -1;

		break;

	case LZX_BLOCKTYPE_UNCOMPRESSED:
		/*
		 * The header of an uncompressed block contains new values for
		 * the recent offsets queue, starting on the next 16-bit
		 * boundary in the bitstream.  Careful: if the stream is
		 * *already* aligned, the correct thing to do is to throw away
		 * the next 16 bits (this is probably a mistake in the format).
		 */
		bitstream_ensure_bits(is, 1);
		bitstream_align(is);
		recent_offsets[0] = bitstream_read_u32(is);
		recent_offsets[1] = bitstream_read_u32(is);
		recent_offsets[2] = bitstream_read_u32(is);

		/* Offsets of 0 are invalid.  */
		if (recent_offsets[0] == 0 || recent_offsets[1] == 0 ||
		    recent_offsets[2] == 0)
			return -1;
		break;

	default:
		/* Unrecognized block type.  */
		return -1;
	}

	*block_type_ret = block_type;
	*block_size_ret = block_size;
	return 0;
}

/* Decompress a block of LZX-compressed data. */
static int
lzx_decompress_block(struct lzx_decompressor *d, struct input_bitstream *_is,
		     int block_type, u32 block_size,
		     u8 * const out_begin, u8 *out_next, u32 recent_offsets[])
{
	/*
	 * Redeclare the input bitstream on the stack.  This shouldn't be
	 * needed, but it can improve the main loop's performance significantly
	 * with both gcc and clang, apparently because the compiler otherwise
	 * gets confused and doesn't properly allocate registers for
	 * 'is->bitbuf' et al. and/or thinks 'is->next' may point into 'is'.
	 */
	struct input_bitstream is_onstack = *_is;
	struct input_bitstream *is = &is_onstack;
	u8 * const block_end = out_next + block_size;
	unsigned min_aligned_offset_slot;

	/*
	 * Build the Huffman decode tables.  We always need to build the main
	 * and length decode tables.  For aligned blocks we additionally need to
	 * build the aligned offset decode table.
	 */

	if (make_huffman_decode_table(d->maincode_decode_table,
				      d->num_main_syms,
				      LZX_MAINCODE_TABLEBITS,
				      d->maincode_lens,
				      LZX_MAX_MAIN_CODEWORD_LEN,
				      d->maincode_working_space))
		return -1;

	if (make_huffman_decode_table(d->lencode_decode_table,
				      LZX_LENCODE_NUM_SYMBOLS,
				      LZX_LENCODE_TABLEBITS,
				      d->lencode_lens,
				      LZX_MAX_LEN_CODEWORD_LEN,
				      d->lencode_working_space))
		return -1;

	if (block_type == LZX_BLOCKTYPE_ALIGNED) {
		if (make_huffman_decode_table(d->alignedcode_decode_table,
					      LZX_ALIGNEDCODE_NUM_SYMBOLS,
					      LZX_ALIGNEDCODE_TABLEBITS,
					      d->alignedcode_lens,
					      LZX_MAX_ALIGNED_CODEWORD_LEN,
					      d->alignedcode_working_space))
			return -1;
		min_aligned_offset_slot = LZX_MIN_ALIGNED_OFFSET_SLOT;
		memcpy(d->extra_offset_bits, d->extra_offset_bits_minus_aligned,
		       sizeof(lzx_extra_offset_bits));
	} else {
		min_aligned_offset_slot = LZX_MAX_OFFSET_SLOTS;
		memcpy(d->extra_offset_bits, lzx_extra_offset_bits,
		       sizeof(lzx_extra_offset_bits));
	}

	/* Decode the literals and matches. */

	do {
		unsigned mainsym;
		unsigned length;
		u32 offset;
		unsigned offset_slot;

		mainsym = read_mainsym(d, is);
		if (mainsym < LZX_NUM_CHARS) {
			/* Literal */
			*out_next++ = mainsym;
			continue;
		}

		/* Match */

		/* Decode the length header and offset slot.  */
		STATIC_ASSERT(LZX_NUM_CHARS % LZX_NUM_LEN_HEADERS == 0);
		length = mainsym % LZX_NUM_LEN_HEADERS;
		offset_slot = (mainsym - LZX_NUM_CHARS) / LZX_NUM_LEN_HEADERS;

		/* If needed, read a length symbol to decode the full length. */
		if (length == LZX_NUM_PRIMARY_LENS)
			length += read_lensym(d, is);
		length += LZX_MIN_MATCH_LEN;

		if (offset_slot < LZX_NUM_RECENT_OFFSETS) {
			/* Repeat offset  */

			/* Note: This isn't a real LRU queue, since using the R2
			 * offset doesn't bump the R1 offset down to R2. */
			offset = recent_offsets[offset_slot];
			recent_offsets[offset_slot] = recent_offsets[0];
		} else {
			/* Explicit offset  */
			offset = bitstream_read_bits(is, d->extra_offset_bits[offset_slot]);
			if (offset_slot >= min_aligned_offset_slot) {
				offset = (offset << LZX_NUM_ALIGNED_OFFSET_BITS) |
					 read_alignedsym(d, is);
			}
			offset += lzx_offset_slot_base[offset_slot];

			/* Update the match offset LRU queue.  */
			STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3);
			recent_offsets[2] = recent_offsets[1];
			recent_offsets[1] = recent_offsets[0];
		}
		recent_offsets[0] = offset;

		/* Validate the match and copy it to the current position.  */
		if (unlikely(lz_copy(length, offset, out_begin,
				     out_next, block_end, LZX_MIN_MATCH_LEN)))
			return -1;
		out_next += length;
	} while (out_next != block_end);

	*_is = is_onstack;
	return 0;
}

static int
lzx_decompress(const void *restrict compressed_data, size_t compressed_size,
	       void *restrict uncompressed_data, size_t uncompressed_size,
	       void *restrict _d)
{
	struct lzx_decompressor *d = _d;
	u8 * const out_begin = uncompressed_data;
	u8 *out_next = out_begin;
	u8 * const out_end = out_begin + uncompressed_size;
	struct input_bitstream is;
	STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3);
	u32 recent_offsets[LZX_NUM_RECENT_OFFSETS] = {1, 1, 1};
	unsigned may_have_e8_byte = 0;

	init_input_bitstream(&is, compressed_data, compressed_size);

	/* Codeword lengths begin as all 0's for delta encoding purposes. */
	memset(d->maincode_lens, 0, d->num_main_syms);
	memset(d->lencode_lens, 0, LZX_LENCODE_NUM_SYMBOLS);

	/* Decompress blocks until we have all the uncompressed data. */

	while (out_next != out_end) {
		int block_type;
		u32 block_size;

		if (lzx_read_block_header(d, &is, recent_offsets,
					  &block_type, &block_size))
			return -1;

		if (block_size < 1 || block_size > out_end - out_next)
			return -1;

		if (likely(block_type != LZX_BLOCKTYPE_UNCOMPRESSED)) {

			/* Compressed block */
			if (lzx_decompress_block(d, &is, block_type, block_size,
						 out_begin, out_next,
						 recent_offsets))
				return -1;

			/* If the first E8 byte was in this block, then it must
			 * have been encoded as a literal using mainsym E8. */
			may_have_e8_byte |= d->maincode_lens[0xE8];
		} else {

			/* Uncompressed block */
			if (bitstream_read_bytes(&is, out_next, block_size))
				return -1;

			/* Re-align the bitstream if needed. */
			if (block_size & 1)
				bitstream_read_byte(&is);

			/* There may have been an E8 byte in the block. */
			may_have_e8_byte = 1;
		}
		out_next += block_size;
	}

	/* Postprocess the data unless it cannot possibly contain E8 bytes. */
	if (may_have_e8_byte)
		lzx_postprocess(uncompressed_data, uncompressed_size);

	return 0;
}

static int
lzx_create_decompressor(size_t max_block_size, void **d_ret)
{
	unsigned window_order;
	struct lzx_decompressor *d;

	window_order = lzx_get_window_order(max_block_size);
	if (window_order == 0)
		return WIMLIB_ERR_INVALID_PARAM;

	d = ALIGNED_MALLOC(sizeof(*d), DECODE_TABLE_ALIGNMENT);
	if (!d)
		return WIMLIB_ERR_NOMEM;

	d->window_order = window_order;
	d->num_main_syms = lzx_get_num_main_syms(window_order);

	/* Initialize 'd->extra_offset_bits_minus_aligned'. */
	STATIC_ASSERT(sizeof(d->extra_offset_bits_minus_aligned) ==
		      sizeof(lzx_extra_offset_bits));
	STATIC_ASSERT(sizeof(d->extra_offset_bits) ==
		      sizeof(lzx_extra_offset_bits));
	memcpy(d->extra_offset_bits_minus_aligned, lzx_extra_offset_bits,
	       sizeof(lzx_extra_offset_bits));
	for (unsigned offset_slot = LZX_MIN_ALIGNED_OFFSET_SLOT;
	     offset_slot < LZX_MAX_OFFSET_SLOTS; offset_slot++)
	{
		d->extra_offset_bits_minus_aligned[offset_slot] -=
				LZX_NUM_ALIGNED_OFFSET_BITS;
	}

	*d_ret = d;
	return 0;
}

static void
lzx_free_decompressor(void *_d)
{
	ALIGNED_FREE(_d);
}

const struct decompressor_ops lzx_decompressor_ops = {
	.create_decompressor = lzx_create_decompressor,
	.decompress	     = lzx_decompress,
	.free_decompressor   = lzx_free_decompressor,
};
