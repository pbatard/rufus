/*
 * lzms_decompress.c
 *
 * A decompressor for the LZMS compression format.
 */

/*
 * Copyright (C) 2013-2016 Eric Biggers
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
 * This is a decompressor for the LZMS compression format used by Microsoft.
 * This format is not documented, but it is one of the formats supported by the
 * compression API available in Windows 8, and as of Windows 8 it is one of the
 * formats that can be used in WIM files.
 *
 * This decompressor only implements "raw" decompression, which decompresses a
 * single LZMS-compressed block.  This behavior is the same as that of
 * Decompress() in the Windows 8 compression API when using a compression handle
 * created with CreateDecompressor() with the Algorithm parameter specified as
 * COMPRESS_ALGORITHM_LZMS | COMPRESS_RAW.  Presumably, non-raw LZMS data is a
 * container format from which the locations and sizes (both compressed and
 * uncompressed) of the constituent blocks can be determined.
 *
 * An LZMS-compressed block must be read in 16-bit little endian units from both
 * directions.  One logical bitstream starts at the front of the block and
 * proceeds forwards.  Another logical bitstream starts at the end of the block
 * and proceeds backwards.  Bits read from the forwards bitstream constitute
 * binary range-encoded data, whereas bits read from the backwards bitstream
 * constitute Huffman-encoded symbols or verbatim bits.  For both bitstreams,
 * the ordering of the bits within the 16-bit coding units is such that the
 * first bit is the high-order bit and the last bit is the low-order bit.
 *
 * From these two logical bitstreams, an LZMS decompressor can reconstitute the
 * series of items that make up the LZMS data representation.  Each such item
 * may be a literal byte or a match.  Matches may be either traditional LZ77
 * matches or "delta" matches, either of which can have its offset encoded
 * explicitly or encoded via a reference to a recently used (repeat) offset.
 *
 * A traditional LZ77 match consists of a length and offset.  It asserts that
 * the sequence of bytes beginning at the current position and extending for the
 * length is equal to the same-length sequence of bytes at the offset back in
 * the data buffer.  This type of match can be visualized as follows, with the
 * caveat that the sequences may overlap:
 *
 *                                offset
 *                         --------------------
 *                         |                  |
 *                         B[1...len]         A[1...len]
 *
 * Decoding proceeds as follows:
 *
 *                      do {
 *                              *A++ = *B++;
 *                      } while (--length);
 *
 * On the other hand, a delta match consists of a "span" as well as a length and
 * offset.  A delta match can be visualized as follows, with the caveat that the
 * various sequences may overlap:
 *
 *                                       offset
 *                            -----------------------------
 *                            |                           |
 *                    span    |                   span    |
 *                -------------               -------------
 *                |           |               |           |
 *                D[1...len]  C[1...len]      B[1...len]  A[1...len]
 *
 * Decoding proceeds as follows:
 *
 *                      do {
 *                              *A++ = *B++ + *C++ - *D++;
 *                      } while (--length);
 *
 * A delta match asserts that the bytewise differences of the A and B sequences
 * are equal to the bytewise differences of the C and D sequences.  The
 * sequences within each pair are separated by the same number of bytes, the
 * "span".  The inter-pair distance is the "offset".  In LZMS, spans are
 * restricted to powers of 2 between 2**0 and 2**7 inclusively.  Offsets are
 * restricted to multiples of the span.  The stored value for the offset is the
 * "raw offset", which is the real offset divided by the span.
 *
 * Delta matches can cover data containing a series of power-of-2 sized integers
 * that is linearly increasing or decreasing.  Another way of thinking about it
 * is that a delta match can match a longer sequence that is interrupted by a
 * non-matching byte, provided that the non-matching byte is a continuation of a
 * linearly changing pattern.  Examples of files that may contain data like this
 * are uncompressed bitmap images, uncompressed digital audio, and Unicode data
 * tables.  To some extent, this match type is a replacement for delta filters
 * or multimedia filters that are sometimes used in other compression software
 * (e.g.  'xz --delta --lzma2').  However, on most types of files, delta matches
 * do not seem to be very useful.
 *
 * Both LZ and delta matches may use overlapping sequences.  Therefore, they
 * must be decoded as if only one byte is copied at a time.
 *
 * For both LZ and delta matches, any match length in [1, 1073809578] can be
 * represented.  Similarly, any match offset in [1, 1180427428] can be
 * represented.  For delta matches, this range applies to the raw offset, so the
 * real offset may be larger.
 *
 * For LZ matches, up to 3 repeat offsets are allowed, similar to some other
 * LZ-based formats such as LZX and LZMA.  They must updated in an LRU fashion,
 * except for a quirk: inserting anything to the front of the queue must be
 * delayed by one LZMS item.  The reason for this is presumably that there is
 * almost no reason to code the same match offset twice in a row, since you
 * might as well have coded a longer match at that offset.  For this same
 * reason, it also is a requirement that when an offset in the queue is used,
 * that offset is removed from the queue immediately (and made pending for
 * front-insertion after the following decoded item), and everything to the
 * right is shifted left one queue slot.  This creates a need for an "overflow"
 * fourth entry in the queue, even though it is only possible to decode
 * references to the first 3 entries at any given time.  The queue must be
 * initialized to the offsets {1, 2, 3, 4}.
 *
 * Repeat delta matches are handled similarly, but for them the queue contains
 * (power, raw offset) pairs.  This queue must be initialized to
 * {(0, 1), (0, 2), (0, 3), (0, 4)}.
 *
 * Bits from the binary range decoder must be used to disambiguate item types.
 * The range decoder must hold two state variables: the range, which must
 * initially be set to 0xffffffff, and the current code, which must initially be
 * set to the first 32 bits read from the forwards bitstream.  The range must be
 * maintained above 0xffff; when it falls below 0xffff, both the range and code
 * must be left-shifted by 16 bits and the low 16 bits of the code must be
 * filled in with the next 16 bits from the forwards bitstream.
 *
 * To decode each bit, the binary range decoder requires a probability that is
 * logically a real number between 0 and 1.  Multiplying this probability by the
 * current range and taking the floor gives the bound between the 0-bit region of
 * the range and the 1-bit region of the range.  However, in LZMS, probabilities
 * are restricted to values of n/64 where n is an integer is between 1 and 63
 * inclusively, so the implementation may use integer operations instead.
 * Following calculation of the bound, if the current code is in the 0-bit
 * region, the new range becomes the current code and the decoded bit is 0;
 * otherwise, the bound must be subtracted from both the range and the code, and
 * the decoded bit is 1.  More information about range coding can be found at
 * https://en.wikipedia.org/wiki/Range_encoding.  Furthermore, note that the
 * LZMA format also uses range coding and has public domain code available for
 * it.
 *
 * The probability used to range-decode each bit must be taken from a table, of
 * which one instance must exist for each distinct context, or "binary decision
 * class", in which a range-decoded bit is needed.  At each call of the range
 * decoder, the appropriate probability must be obtained by indexing the
 * appropriate probability table with the last 4 (in the context disambiguating
 * literals from matches), 5 (in the context disambiguating LZ matches from
 * delta matches), or 6 (in all other contexts) bits recently range-decoded in
 * that context, ordered such that the most recently decoded bit is the
 * low-order bit of the index.
 *
 * Furthermore, each probability entry itself is variable, as its value must be
 * maintained as n/64 where n is the number of 0 bits in the most recently
 * decoded 64 bits with that same entry.  This allows the compressed
 * representation to adapt to the input and use fewer bits to represent the most
 * likely data; note that LZMA uses a similar scheme.  Initially, the most
 * recently 64 decoded bits for each probability entry are assumed to be
 * 0x0000000055555555 (high order to low order); therefore, all probabilities
 * are initially 48/64.  During the course of decoding, each probability may be
 * updated to as low as 0/64 (as a result of reading many consecutive 1 bits
 * with that entry) or as high as 64/64 (as a result of reading many consecutive
 * 0 bits with that entry); however, probabilities of 0/64 and 64/64 cannot be
 * used as-is but rather must be adjusted to 1/64 and 63/64, respectively,
 * before being used for range decoding.
 *
 * Representations of the LZMS items themselves must be read from the backwards
 * bitstream.  For this, there are 5 different Huffman codes used:
 *
 *  - The literal code, used for decoding literal bytes.  Each of the 256
 *    symbols represents a literal byte.  This code must be rebuilt whenever
 *    1024 symbols have been decoded with it.
 *
 *  - The LZ offset code, used for decoding the offsets of standard LZ77
 *    matches.  Each symbol represents an offset slot, which corresponds to a
 *    base value and some number of extra bits which must be read and added to
 *    the base value to reconstitute the full offset.  The number of symbols in
 *    this code is the number of offset slots needed to represent all possible
 *    offsets in the uncompressed block.  This code must be rebuilt whenever
 *    1024 symbols have been decoded with it.
 *
 *  - The length code, used for decoding length symbols.  Each of the 54 symbols
 *    represents a length slot, which corresponds to a base value and some
 *    number of extra bits which must be read and added to the base value to
 *    reconstitute the full length.  This code must be rebuilt whenever 512
 *    symbols have been decoded with it.
 *
 *  - The delta offset code, used for decoding the raw offsets of delta matches.
 *    Each symbol corresponds to an offset slot, which corresponds to a base
 *    value and some number of extra bits which must be read and added to the
 *    base value to reconstitute the full raw offset.  The number of symbols in
 *    this code is equal to the number of symbols in the LZ offset code.  This
 *    code must be rebuilt whenever 1024 symbols have been decoded with it.
 *
 *  - The delta power code, used for decoding the powers of delta matches.  Each
 *    of the 8 symbols corresponds to a power.  This code must be rebuilt
 *    whenever 512 symbols have been decoded with it.
 *
 * Initially, each Huffman code must be built assuming that each symbol in that
 * code has frequency 1.  Following that, each code must be rebuilt each time a
 * certain number of symbols, as noted above, has been decoded with it.  The
 * symbol frequencies for a code must be halved after each rebuild of that code;
 * this makes the codes adapt to the more recent data.
 *
 * Like other compression formats such as XPRESS, LZX, and DEFLATE, the LZMS
 * format requires that all Huffman codes be constructed in canonical form.
 * This form requires that same-length codewords be lexicographically ordered
 * the same way as the corresponding symbols and that all shorter codewords
 * lexicographically precede longer codewords.  Such a code can be constructed
 * directly from codeword lengths.
 *
 * Even with the canonical code restriction, the same frequencies can be used to
 * construct multiple valid Huffman codes.  Therefore, the decompressor needs to
 * construct the right one.  Specifically, the LZMS format requires that the
 * Huffman code be constructed as if the well-known priority queue algorithm is
 * used and frequency ties are always broken in favor of leaf nodes.
 *
 * Codewords in LZMS are guaranteed to not exceed 15 bits.  The format otherwise
 * places no restrictions on codeword length.  Therefore, the Huffman code
 * construction algorithm that a correct LZMS decompressor uses need not
 * implement length-limited code construction.  But if it does (e.g. by virtue
 * of being shared among multiple compression algorithms), the details of how it
 * does so are unimportant, provided that the maximum codeword length parameter
 * is set to at least 15 bits.
 *
 * After all LZMS items have been decoded, the data must be postprocessed to
 * translate absolute address encoded in x86 instructions into their original
 * relative addresses.
 *
 * Details omitted above can be found in the code.  Note that in the absence of
 * an official specification there is no guarantee that this decompressor
 * handles all possible cases.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/compress_common.h"
#include "wimlib/decompress_common.h"
#include "wimlib/decompressor_ops.h"
#include "wimlib/error.h"
#include "wimlib/lzms_common.h"
#include "wimlib/util.h"

/* The TABLEBITS values can be changed; they only affect decoding speed.  */
#define LZMS_LITERAL_TABLEBITS		10
#define LZMS_LENGTH_TABLEBITS		9
#define LZMS_LZ_OFFSET_TABLEBITS	11
#define LZMS_DELTA_OFFSET_TABLEBITS	11
#define LZMS_DELTA_POWER_TABLEBITS	7

struct lzms_range_decoder {

	/* The relevant part of the current range.  Although the logical range
	 * for range decoding is a very large integer, only a small portion
	 * matters at any given time, and it can be normalized (shifted left)
	 * whenever it gets too small.  */
	u32 range;

	/* The current position in the range encoded by the portion of the input
	 * read so far.  */
	u32 code;

	/* Pointer to the next little-endian 16-bit integer in the compressed
	 * input data (reading forwards).  */
	const u8 *next;

	/* Pointer to the end of the compressed input data.  */
	const u8 *end;
};

typedef u64 bitbuf_t;

struct lzms_input_bitstream {

	/* Holding variable for bits that have been read from the compressed
	 * data.  The bit ordering is high to low.  */
	bitbuf_t bitbuf;

	/* Number of bits currently held in @bitbuf.  */
	unsigned bitsleft;

	/* Pointer to the one past the next little-endian 16-bit integer in the
	 * compressed input data (reading backwards).  */
	const u8 *next;

	/* Pointer to the beginning of the compressed input data.  */
	const u8 *begin;
};

#define BITBUF_NBITS	(8 * sizeof(bitbuf_t))

/* Bookkeeping information for an adaptive Huffman code  */
struct lzms_huffman_rebuild_info {
	unsigned num_syms_until_rebuild;
	unsigned num_syms;
	unsigned rebuild_freq;
	u32 *codewords;
	u32 *freqs;
	u16 *decode_table;
	unsigned table_bits;
};

struct lzms_decompressor {

	/* 'last_target_usages' is in union with everything else because it is
	 * only used for postprocessing.  */
	union {
	struct {

	struct lzms_probabilites probs;

	DECODE_TABLE(literal_decode_table, LZMS_NUM_LITERAL_SYMS,
		     LZMS_LITERAL_TABLEBITS, LZMS_MAX_CODEWORD_LENGTH);
	u32 literal_freqs[LZMS_NUM_LITERAL_SYMS];
	struct lzms_huffman_rebuild_info literal_rebuild_info;

	DECODE_TABLE(lz_offset_decode_table, LZMS_MAX_NUM_OFFSET_SYMS,
		     LZMS_LZ_OFFSET_TABLEBITS, LZMS_MAX_CODEWORD_LENGTH);
	u32 lz_offset_freqs[LZMS_MAX_NUM_OFFSET_SYMS];
	struct lzms_huffman_rebuild_info lz_offset_rebuild_info;

	DECODE_TABLE(length_decode_table, LZMS_NUM_LENGTH_SYMS,
		     LZMS_LENGTH_TABLEBITS, LZMS_MAX_CODEWORD_LENGTH);
	u32 length_freqs[LZMS_NUM_LENGTH_SYMS];
	struct lzms_huffman_rebuild_info length_rebuild_info;

	DECODE_TABLE(delta_offset_decode_table, LZMS_MAX_NUM_OFFSET_SYMS,
		     LZMS_DELTA_OFFSET_TABLEBITS, LZMS_MAX_CODEWORD_LENGTH);
	u32 delta_offset_freqs[LZMS_MAX_NUM_OFFSET_SYMS];
	struct lzms_huffman_rebuild_info delta_offset_rebuild_info;

	DECODE_TABLE(delta_power_decode_table, LZMS_NUM_DELTA_POWER_SYMS,
		     LZMS_DELTA_POWER_TABLEBITS, LZMS_MAX_CODEWORD_LENGTH);
	u32 delta_power_freqs[LZMS_NUM_DELTA_POWER_SYMS];
	struct lzms_huffman_rebuild_info delta_power_rebuild_info;

	/* Temporary space for lzms_build_huffman_code() */
	union {
		u32 codewords[LZMS_MAX_NUM_SYMS];
		DECODE_TABLE_WORKING_SPACE(working_space, LZMS_MAX_NUM_SYMS,
					   LZMS_MAX_CODEWORD_LENGTH);
	};

	}; // struct

	s32 last_target_usages[65536];

	}; // union
};

/* Initialize the input bitstream @is to read backwards from the compressed data
 * buffer @in that is @count bytes long.  */
static void
lzms_input_bitstream_init(struct lzms_input_bitstream *is,
			  const u8 *in, size_t count)
{
	is->bitbuf = 0;
	is->bitsleft = 0;
	is->next = in + count;
	is->begin = in;
}

/* Ensure that at least @num_bits bits are in the bitbuffer variable.
 * @num_bits cannot be more than 32.  */
static forceinline void
lzms_ensure_bits(struct lzms_input_bitstream *is, unsigned num_bits)
{
	unsigned avail;

	if (is->bitsleft >= num_bits)
		return;

	avail = BITBUF_NBITS - is->bitsleft;

	if (UNALIGNED_ACCESS_IS_FAST && CPU_IS_LITTLE_ENDIAN() &&
	    WORDBYTES == 8 && likely(is->next - is->begin >= 8))
	{
		is->next -= (avail & ~15) >> 3;
		is->bitbuf |= load_u64_unaligned(is->next) << (avail & 15);
		is->bitsleft += avail & ~15;
	} else {
		if (likely(is->next != is->begin)) {
			is->next -= sizeof(le16);
			is->bitbuf |= (bitbuf_t)get_unaligned_le16(is->next)
					<< (avail - 16);
		}
		if (likely(is->next != is->begin)) {
			is->next -= sizeof(le16);
			is->bitbuf |= (bitbuf_t)get_unaligned_le16(is->next)
					<< (avail - 32);
		}
		is->bitsleft += 32;
	}
}

/* Get @num_bits bits from the bitbuffer variable.  */
static forceinline bitbuf_t
lzms_peek_bits(struct lzms_input_bitstream *is, unsigned num_bits)
{
	return (is->bitbuf >> 1) >> (BITBUF_NBITS - num_bits - 1);
}

/* Remove @num_bits bits from the bitbuffer variable.  */
static forceinline void
lzms_remove_bits(struct lzms_input_bitstream *is, unsigned num_bits)
{
	is->bitbuf <<= num_bits;
	is->bitsleft -= num_bits;
}

/* Remove and return @num_bits bits from the bitbuffer variable.  */
static forceinline bitbuf_t
lzms_pop_bits(struct lzms_input_bitstream *is, unsigned num_bits)
{
	bitbuf_t bits = lzms_peek_bits(is, num_bits);
	lzms_remove_bits(is, num_bits);
	return bits;
}

/* Read @num_bits bits from the input bitstream.  */
static forceinline bitbuf_t
lzms_read_bits(struct lzms_input_bitstream *is, unsigned num_bits)
{
	lzms_ensure_bits(is, num_bits);
	return lzms_pop_bits(is, num_bits);
}

/* Initialize the range decoder @rd to read forwards from the compressed data
 * buffer @in that is @count bytes long.  */
static void
lzms_range_decoder_init(struct lzms_range_decoder *rd,
			const u8 *in, size_t count)
{
	rd->range = 0xffffffff;
	rd->code = ((u32)get_unaligned_le16(in) << 16) |
		   get_unaligned_le16(in + 2);
	rd->next = in + 4;
	rd->end = in + count;
}

/*
 * Decode a bit using the range coder.  The current state specifies the
 * probability entry to use.  The state and probability entry will be updated
 * based on the decoded bit.
 */
static forceinline int
lzms_decode_bit(struct lzms_range_decoder *rd, u32 *state_p, u32 num_states,
		struct lzms_probability_entry *probs)
{
	struct lzms_probability_entry *prob_entry;
	u32 prob;
	u32 bound;

	/* Load the probability entry corresponding to the current state.  */
	prob_entry = &probs[*state_p];

	/* Update the state early.  We'll still need to OR the state with 1
	 * later if the decoded bit is a 1.  */
	*state_p = (*state_p << 1) & (num_states - 1);

	/* Get the probability (out of LZMS_PROBABILITY_DENOMINATOR) that the
	 * next bit is 0.  */
	prob = lzms_get_probability(prob_entry);

	/* Normalize if needed.  */
	if (!(rd->range & 0xFFFF0000)) {
		rd->range <<= 16;
		rd->code <<= 16;
		if (likely(rd->next != rd->end)) {
			rd->code |= get_unaligned_le16(rd->next);
			rd->next += sizeof(le16);
		}
	}

	/* Based on the probability, calculate the bound between the 0-bit
	 * region and the 1-bit region of the range.  */
	bound = (rd->range >> LZMS_PROBABILITY_BITS) * prob;

	if (rd->code < bound) {
		/* Current code is in the 0-bit region of the range.  */
		rd->range = bound;

		/* Update the state and probability entry based on the decoded bit.  */
		lzms_update_probability_entry(prob_entry, 0);
		return 0;
	} else {
		/* Current code is in the 1-bit region of the range.  */
		rd->range -= bound;
		rd->code -= bound;

		/* Update the state and probability entry based on the decoded bit.  */
		lzms_update_probability_entry(prob_entry, 1);
		*state_p |= 1;
		return 1;
	}
}

static void
lzms_build_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info)
{
	make_canonical_huffman_code(rebuild_info->num_syms,
				    LZMS_MAX_CODEWORD_LENGTH,
				    rebuild_info->freqs,
				    (u8 *)rebuild_info->decode_table,
				    rebuild_info->codewords);

	make_huffman_decode_table(rebuild_info->decode_table,
				  rebuild_info->num_syms,
				  rebuild_info->table_bits,
				  (u8 *)rebuild_info->decode_table,
				  LZMS_MAX_CODEWORD_LENGTH,
				  (u16 *)rebuild_info->codewords);

	rebuild_info->num_syms_until_rebuild = rebuild_info->rebuild_freq;
}

static void
lzms_init_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info,
		       unsigned num_syms, unsigned rebuild_freq,
		       u32 *codewords, u32 *freqs,
		       u16 *decode_table, unsigned table_bits)
{
	rebuild_info->num_syms = num_syms;
	rebuild_info->rebuild_freq = rebuild_freq;
	rebuild_info->codewords = codewords;
	rebuild_info->freqs = freqs;
	rebuild_info->decode_table = decode_table;
	rebuild_info->table_bits = table_bits;
	lzms_init_symbol_frequencies(freqs, num_syms);
	lzms_build_huffman_code(rebuild_info);
}

static void
lzms_init_huffman_codes(struct lzms_decompressor *d, unsigned num_offset_slots)
{
	lzms_init_huffman_code(&d->literal_rebuild_info,
			       LZMS_NUM_LITERAL_SYMS,
			       LZMS_LITERAL_CODE_REBUILD_FREQ,
			       d->codewords,
			       d->literal_freqs,
			       d->literal_decode_table,
			       LZMS_LITERAL_TABLEBITS);

	lzms_init_huffman_code(&d->lz_offset_rebuild_info,
			       num_offset_slots,
			       LZMS_LZ_OFFSET_CODE_REBUILD_FREQ,
			       d->codewords,
			       d->lz_offset_freqs,
			       d->lz_offset_decode_table,
			       LZMS_LZ_OFFSET_TABLEBITS);

	lzms_init_huffman_code(&d->length_rebuild_info,
			       LZMS_NUM_LENGTH_SYMS,
			       LZMS_LENGTH_CODE_REBUILD_FREQ,
			       d->codewords,
			       d->length_freqs,
			       d->length_decode_table,
			       LZMS_LENGTH_TABLEBITS);

	lzms_init_huffman_code(&d->delta_offset_rebuild_info,
			       num_offset_slots,
			       LZMS_DELTA_OFFSET_CODE_REBUILD_FREQ,
			       d->codewords,
			       d->delta_offset_freqs,
			       d->delta_offset_decode_table,
			       LZMS_DELTA_OFFSET_TABLEBITS);

	lzms_init_huffman_code(&d->delta_power_rebuild_info,
			       LZMS_NUM_DELTA_POWER_SYMS,
			       LZMS_DELTA_POWER_CODE_REBUILD_FREQ,
			       d->codewords,
			       d->delta_power_freqs,
			       d->delta_power_decode_table,
			       LZMS_DELTA_POWER_TABLEBITS);
}

static _noinline void
lzms_rebuild_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info)
{
	lzms_build_huffman_code(rebuild_info);
	lzms_dilute_symbol_frequencies(rebuild_info->freqs, rebuild_info->num_syms);
}

/* XXX: mostly copied from read_huffsym() in decompress_common.h because LZMS
 * needs its own bitstream */
static forceinline unsigned
lzms_decode_huffman_symbol(struct lzms_input_bitstream *is, u16 decode_table[],
			   unsigned table_bits, u32 freqs[],
			   struct lzms_huffman_rebuild_info *rebuild_info)
{
	unsigned entry;
	unsigned symbol;
	unsigned length;

	lzms_ensure_bits(is, LZMS_MAX_CODEWORD_LENGTH);

	entry = decode_table[lzms_peek_bits(is, table_bits)];
	symbol = entry >> DECODE_TABLE_SYMBOL_SHIFT;
	length = entry & DECODE_TABLE_LENGTH_MASK;

	if (entry >= (1U << (table_bits + DECODE_TABLE_SYMBOL_SHIFT))) {
		lzms_remove_bits(is, table_bits);
		entry = decode_table[symbol + lzms_peek_bits(is, length)];
		symbol = entry >> DECODE_TABLE_SYMBOL_SHIFT;
		length = entry & DECODE_TABLE_LENGTH_MASK;
	}

	lzms_remove_bits(is, length);

	freqs[symbol]++;
	if (--rebuild_info->num_syms_until_rebuild == 0)
		lzms_rebuild_huffman_code(rebuild_info);
	return symbol;
}

static forceinline unsigned
lzms_decode_literal(struct lzms_decompressor *d,
		    struct lzms_input_bitstream *is)
{
	return lzms_decode_huffman_symbol(is,
					  d->literal_decode_table,
					  LZMS_LITERAL_TABLEBITS,
					  d->literal_freqs,
					  &d->literal_rebuild_info);
}

static forceinline u32
lzms_decode_lz_offset(struct lzms_decompressor *d,
		      struct lzms_input_bitstream *is)
{
	unsigned slot = lzms_decode_huffman_symbol(is,
						   d->lz_offset_decode_table,
						   LZMS_LZ_OFFSET_TABLEBITS,
						   d->lz_offset_freqs,
						   &d->lz_offset_rebuild_info);
	return lzms_offset_slot_base[slot] +
	       lzms_read_bits(is, lzms_extra_offset_bits[slot]);
}

static forceinline u32
lzms_decode_length(struct lzms_decompressor *d,
		   struct lzms_input_bitstream *is)
{
	unsigned slot = lzms_decode_huffman_symbol(is,
						   d->length_decode_table,
						   LZMS_LENGTH_TABLEBITS,
						   d->length_freqs,
						   &d->length_rebuild_info);
	u32 length = lzms_length_slot_base[slot];
	unsigned num_extra_bits = lzms_extra_length_bits[slot];
	/* Usually most lengths are short and have no extra bits.  */
	if (num_extra_bits)
		length += lzms_read_bits(is, num_extra_bits);
	return length;
}

static forceinline u32
lzms_decode_delta_offset(struct lzms_decompressor *d,
			 struct lzms_input_bitstream *is)
{
	unsigned slot = lzms_decode_huffman_symbol(is,
						   d->delta_offset_decode_table,
						   LZMS_DELTA_OFFSET_TABLEBITS,
						   d->delta_offset_freqs,
						   &d->delta_offset_rebuild_info);
	return lzms_offset_slot_base[slot] +
	       lzms_read_bits(is, lzms_extra_offset_bits[slot]);
}

static forceinline unsigned
lzms_decode_delta_power(struct lzms_decompressor *d,
			struct lzms_input_bitstream *is)
{
	return lzms_decode_huffman_symbol(is,
					  d->delta_power_decode_table,
					  LZMS_DELTA_POWER_TABLEBITS,
					  d->delta_power_freqs,
					  &d->delta_power_rebuild_info);
}

static int
lzms_create_decompressor(size_t max_bufsize, void **d_ret)
{
	struct lzms_decompressor *d;

	if (max_bufsize > LZMS_MAX_BUFFER_SIZE)
		return WIMLIB_ERR_INVALID_PARAM;

	d = ALIGNED_MALLOC(sizeof(struct lzms_decompressor),
			   DECODE_TABLE_ALIGNMENT);
	if (!d)
		return WIMLIB_ERR_NOMEM;

	*d_ret = d;
	return 0;
}

/*
 * Decompress @in_nbytes bytes of LZMS-compressed data at @in and write the
 * uncompressed data, which had original size @out_nbytes, to @out.  Return 0 if
 * successful or -1 if the compressed data is invalid.
 */
static int
lzms_decompress(const void * const restrict in, const size_t in_nbytes,
		void * const restrict out, const size_t out_nbytes,
		void * const restrict _d)
{
	struct lzms_decompressor *d = _d;
	u8 *out_next = out;
	u8 * const out_end = _PTR(out + out_nbytes);
	struct lzms_range_decoder rd;
	struct lzms_input_bitstream is;

	/* LRU queues for match sources  */
	u32 recent_lz_offsets[LZMS_NUM_LZ_REPS + 1];
	u64 recent_delta_pairs[LZMS_NUM_DELTA_REPS + 1];

	/* Previous item type: 0 = literal, 1 = LZ match, 2 = delta match.
	 * This is used to handle delayed updates of the LRU queues.  Instead of
	 * actually delaying the updates, we can check when decoding each rep
	 * match whether a delayed update needs to be taken into account, and if
	 * so get the match source from slot 'rep_idx + 1' instead of from slot
	 * 'rep_idx'.  */
	unsigned prev_item_type = 0;

	/* States and probability entries for item type disambiguation  */
	u32 main_state = 0;
	u32 match_state = 0;
	u32 lz_state = 0;
	u32 delta_state = 0;
	u32 lz_rep_states[LZMS_NUM_LZ_REP_DECISIONS] = { 0 };
	u32 delta_rep_states[LZMS_NUM_DELTA_REP_DECISIONS] = { 0 };

	/*
	 * Requirements on the compressed data:
	 *
	 * 1. LZMS-compressed data is a series of 16-bit integers, so the
	 *    compressed data buffer cannot take up an odd number of bytes.
	 * 2. There must be at least 4 bytes of compressed data, since otherwise
	 *    we cannot even initialize the range decoder.
	 */
	if ((in_nbytes & 1) || (in_nbytes < 4))
		return -1;

	lzms_range_decoder_init(&rd, in, in_nbytes);

	lzms_input_bitstream_init(&is, in, in_nbytes);

	lzms_init_probabilities(&d->probs);

	lzms_init_huffman_codes(d, lzms_get_num_offset_slots(out_nbytes));

	for (int i = 0; i < LZMS_NUM_LZ_REPS + 1; i++)
		recent_lz_offsets[i] = i + 1;

	for (int i = 0; i < LZMS_NUM_DELTA_REPS + 1; i++)
		recent_delta_pairs[i] = i + 1;

	/* Main decode loop  */
	while (out_next != out_end) {

		if (!lzms_decode_bit(&rd, &main_state,
				     LZMS_NUM_MAIN_PROBS, d->probs.main))
		{
			/* Literal  */
			*out_next++ = lzms_decode_literal(d, &is);
			prev_item_type = 0;

		} else if (!lzms_decode_bit(&rd, &match_state,
					    LZMS_NUM_MATCH_PROBS,
					    d->probs.match))
		{
			/* LZ match  */

			u32 offset;
			u32 length;

			STATIC_ASSERT(LZMS_NUM_LZ_REPS == 3);

			if (!lzms_decode_bit(&rd, &lz_state,
					     LZMS_NUM_LZ_PROBS, d->probs.lz))
			{
				/* Explicit offset  */
				offset = lzms_decode_lz_offset(d, &is);

				recent_lz_offsets[3] = recent_lz_offsets[2];
				recent_lz_offsets[2] = recent_lz_offsets[1];
				recent_lz_offsets[1] = recent_lz_offsets[0];
			} else {
				/* Repeat offset  */

				if (!lzms_decode_bit(&rd, &lz_rep_states[0],
						     LZMS_NUM_LZ_REP_PROBS,
						     d->probs.lz_rep[0]))
				{
					offset = recent_lz_offsets[0 + (prev_item_type & 1)];
					recent_lz_offsets[0 + (prev_item_type & 1)] = recent_lz_offsets[0];
				} else if (!lzms_decode_bit(&rd, &lz_rep_states[1],
							    LZMS_NUM_LZ_REP_PROBS,
							    d->probs.lz_rep[1]))
				{
					offset = recent_lz_offsets[1 + (prev_item_type & 1)];
					recent_lz_offsets[1 + (prev_item_type & 1)] = recent_lz_offsets[1];
					recent_lz_offsets[1] = recent_lz_offsets[0];
				} else {
					offset = recent_lz_offsets[2 + (prev_item_type & 1)];
					recent_lz_offsets[2 + (prev_item_type & 1)] = recent_lz_offsets[2];
					recent_lz_offsets[2] = recent_lz_offsets[1];
					recent_lz_offsets[1] = recent_lz_offsets[0];
				}
			}
			recent_lz_offsets[0] = offset;
			prev_item_type = 1;

			length = lzms_decode_length(d, &is);

			if (unlikely(lz_copy(length, offset, out, out_next, out_end,
					     LZMS_MIN_MATCH_LENGTH)))
				return -1;

			out_next += length;
		} else {
			/* Delta match  */

			/* (See beginning of file for more information.)  */

			u32 power;
			u32 raw_offset;
			u32 span;
			u32 offset;
			const u8 *matchptr;
			u32 length;
			u64 pair;

			STATIC_ASSERT(LZMS_NUM_DELTA_REPS == 3);

			if (!lzms_decode_bit(&rd, &delta_state,
					     LZMS_NUM_DELTA_PROBS,
					     d->probs.delta))
			{
				/* Explicit offset  */
				power = lzms_decode_delta_power(d, &is);
				raw_offset = lzms_decode_delta_offset(d, &is);

				pair = ((u64)power << 32) | raw_offset;
				recent_delta_pairs[3] = recent_delta_pairs[2];
				recent_delta_pairs[2] = recent_delta_pairs[1];
				recent_delta_pairs[1] = recent_delta_pairs[0];
			} else {
				if (!lzms_decode_bit(&rd, &delta_rep_states[0],
						     LZMS_NUM_DELTA_REP_PROBS,
						     d->probs.delta_rep[0]))
				{
					pair = recent_delta_pairs[0 + (prev_item_type >> 1)];
					recent_delta_pairs[0 + (prev_item_type >> 1)] = recent_delta_pairs[0];
				} else if (!lzms_decode_bit(&rd, &delta_rep_states[1],
							    LZMS_NUM_DELTA_REP_PROBS,
							    d->probs.delta_rep[1]))
				{
					pair = recent_delta_pairs[1 + (prev_item_type >> 1)];
					recent_delta_pairs[1 + (prev_item_type >> 1)] = recent_delta_pairs[1];
					recent_delta_pairs[1] = recent_delta_pairs[0];
				} else {
					pair = recent_delta_pairs[2 + (prev_item_type >> 1)];
					recent_delta_pairs[2 + (prev_item_type >> 1)] = recent_delta_pairs[2];
					recent_delta_pairs[2] = recent_delta_pairs[1];
					recent_delta_pairs[1] = recent_delta_pairs[0];
				}

				power = pair >> 32;
				raw_offset = (u32)pair;
			}
			recent_delta_pairs[0] = pair;
			prev_item_type = 2;

			length = lzms_decode_length(d, &is);

			span = (u32)1 << power;
			offset = raw_offset << power;

			/* raw_offset<<power overflows?  */
			if (unlikely(offset >> power != raw_offset))
				return -1;

			/* offset+span overflows?  */
			if (unlikely(offset + span < offset))
				return -1;

			/* buffer underrun?  */
			if (unlikely(offset + span > out_next - (u8 *)out))
				return -1;

			/* buffer overrun?  */
			if (unlikely(length > out_end - out_next))
				return -1;

			matchptr = out_next - offset;
			do {
				*out_next = *matchptr + *(out_next - span) -
					    *(matchptr - span);
				out_next++;
				matchptr++;
			} while (--length);
		}
	}

	lzms_x86_filter(out, out_nbytes, d->last_target_usages, true);
	return 0;
}

static void
lzms_free_decompressor(void *_d)
{
	struct lzms_decompressor *d = _d;

	ALIGNED_FREE(d);
}

const struct decompressor_ops lzms_decompressor_ops = {
	.create_decompressor  = lzms_create_decompressor,
	.decompress	      = lzms_decompress,
	.free_decompressor    = lzms_free_decompressor,
};
