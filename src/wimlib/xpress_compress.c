/*
 * xpress_compress.c
 *
 * A compressor for the XPRESS compression format (Huffman variant).
 */

/*
 * Copyright (C) 2012, 2013, 2014 Eric Biggers
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

/*
 * The maximum buffer size, in bytes, that can be compressed.  An XPRESS
 * compressor instance must be created with a 'max_bufsize' less than or equal
 * to this value.
 */
#define XPRESS_MAX_BUFSIZE		65536

/*
 * Define to 1 to enable the near-optimal parsing algorithm at high compression
 * levels.  The near-optimal parsing algorithm produces a compression ratio
 * significantly better than the greedy and lazy algorithms.  However, it is
 * much slower.
 */
#define SUPPORT_NEAR_OPTIMAL_PARSING	1

/*
 * The lowest compression level at which near-optimal parsing is enabled.
 */
#define MIN_LEVEL_FOR_NEAR_OPTIMAL	60

/*
 * Matchfinder definitions.  For XPRESS, only a 16-bit matchfinder is needed.
 */
#define mf_pos_t	u16
#define MF_SUFFIX

/*
 * Note: although XPRESS can potentially use a sliding window, it isn't well
 * suited for large buffers of data because there is no way to reset the Huffman
 * code.  Therefore, we only allow buffers in which there is no restriction on
 * match offsets (no sliding window).  This simplifies the code and allows some
 * optimizations.
 */

#include "wimlib/assert.h"
#include "wimlib/bitops.h"
#include "wimlib/compress_common.h"
#include "wimlib/compressor_ops.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/hc_matchfinder.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"
#include "wimlib/xpress_constants.h"

#if SUPPORT_NEAR_OPTIMAL_PARSING

/*
 * CACHE_RESERVE_PER_POS is the number of lz_match structures to reserve in the
 * match cache for each byte position.  This value should be high enough so that
 * virtually the time, all matches found in the input buffer can fit in the
 * match cache.  However, fallback behavior on cache overflow is still required.
 */
#define CACHE_RESERVE_PER_POS	8

/*
 * We use a binary-tree based matchfinder for optimal parsing because it can
 * find more matches in the same number of steps compared to hash-chain based
 * matchfinders.  In addition, since we need to find matches at almost every
 * position, there isn't much penalty for keeping the sequences sorted in the
 * binary trees.
 */
#include "wimlib/bt_matchfinder.h"

struct xpress_optimum_node;

#endif /* SUPPORT_NEAR_OPTIMAL_PARSING */

struct xpress_item;

/* The main XPRESS compressor structure  */
struct xpress_compressor {

	/* Pointer to the compress() implementation chosen at allocation time */
	size_t (*impl)(struct xpress_compressor * restrict,
		       const void * restrict, size_t, void *, size_t);

	/* Symbol frequency counters for the Huffman code  */
	u32 freqs[XPRESS_NUM_SYMBOLS];

	/* The Huffman codewords and their lengths  */
	u32 codewords[XPRESS_NUM_SYMBOLS];
	u8 lens[XPRESS_NUM_SYMBOLS];

	/* The "nice" match length: if a match of this length is found, then
	 * choose it immediately without further consideration.  */
	unsigned nice_match_length;

	/* The maximum search depth: consider at most this many potential
	 * matches at each position.  */
	unsigned max_search_depth;

	union {
		/* Data for greedy or lazy parsing  */
		struct {
			struct xpress_item *chosen_items;
			struct hc_matchfinder hc_mf;
			/* hc_mf must be last!  */
		};

	#if SUPPORT_NEAR_OPTIMAL_PARSING
		/* Data for near-optimal parsing  */
		struct {
			struct xpress_optimum_node *optimum_nodes;
			struct lz_match *match_cache;
			struct lz_match *cache_overflow_mark;
			unsigned num_optim_passes;
			u32 costs[XPRESS_NUM_SYMBOLS];
			struct bt_matchfinder bt_mf;
			/* bt_mf must be last!  */
		};
	#endif
	};
};

#if SUPPORT_NEAR_OPTIMAL_PARSING

/*
 * This structure represents a byte position in the input buffer and a node in
 * the graph of possible match/literal choices.
 *
 * Logically, each incoming edge to this node is labeled with a literal or a
 * match that can be taken to reach this position from an earlier position; and
 * each outgoing edge from this node is labeled with a literal or a match that
 * can be taken to advance from this position to a later position.
 *
 * But these "edges" are actually stored elsewhere (in 'match_cache').  Here we
 * associate with each node just two pieces of information:
 *
 *	'cost_to_end' is the minimum cost to reach the end of the buffer from
 *	this position.
 *
 *	'item' represents the literal or match that must be chosen from here to
 *	reach the end of the buffer with the minimum cost.  Equivalently, this
 *	can be interpreted as the label of the outgoing edge on the minimum cost
 *	path to the "end of buffer" node from this node.
 */
struct xpress_optimum_node {

	u32 cost_to_end;

	/*
	 * Notes on the match/literal representation used here:
	 *
	 *	The low bits of 'item' are the length: 1 if the item is a
	 *	literal, or the match length if the item is a match.
	 *
	 *	The high bits of 'item' are the actual literal byte if the item
	 *	is a literal, or the match offset if the item is a match.
	 */
#define OPTIMUM_OFFSET_SHIFT	16
#define OPTIMUM_LEN_MASK	(((u32)1 << OPTIMUM_OFFSET_SHIFT) - 1)
	u32 item;
};

#endif /* SUPPORT_NEAR_OPTIMAL_PARSING */

/* An intermediate representation of an XPRESS match or literal  */
struct xpress_item {
	/*
	 * Bits 0  -  8: Symbol
	 * Bits 9  - 24: Length - XPRESS_MIN_MATCH_LEN
	 * Bits 25 - 28: Number of extra offset bits
	 * Bits 29+    : Extra offset bits
	 *
	 * Unfortunately, gcc generates worse code if we use real bitfields here.
	 */
	u64 data;
};

/*
 * Structure to keep track of the current state of sending compressed data to
 * the output buffer.
 *
 * The XPRESS bitstream is encoded as a sequence of little endian 16-bit coding
 * units interwoven with literal bytes.
 */
struct xpress_output_bitstream {

	/* Bits that haven't yet been written to the output buffer.  */
	u32 bitbuf;

	/* Number of bits currently held in @bitbuf.  */
	u32 bitcount;

	/* Pointer to the start of the output buffer.  */
	u8 *start;

	/* Pointer to the location in the output buffer at which to write the
	 * next 16 bits.  */
	u8 *next_bits;

	/* Pointer to the location in the output buffer at which to write the
	 * next 16 bits, after @next_bits.  */
	u8 *next_bits2;

	/* Pointer to the location in the output buffer at which to write the
	 * next literal byte.  */
	u8 *next_byte;

	/* Pointer to the end of the output buffer.  */
	u8 *end;
};

/* Reset the symbol frequencies for the XPRESS Huffman code.  */
static void
xpress_reset_symbol_frequencies(struct xpress_compressor *c)
{
	memset(c->freqs, 0, sizeof(c->freqs));
}

/*
 * Make the Huffman code for XPRESS.
 *
 * Input: c->freqs
 * Output: c->lens and c->codewords
 */
static void
xpress_make_huffman_code(struct xpress_compressor *c)
{
	make_canonical_huffman_code(XPRESS_NUM_SYMBOLS, XPRESS_MAX_CODEWORD_LEN,
				    c->freqs, c->lens, c->codewords);
}

/*
 * Initialize the output bitstream.
 *
 * @os
 *	The output bitstream structure to initialize.
 * @buffer
 *	The output buffer.
 * @size
 *	Size of @buffer, in bytes.  Must be at least 4.
 */
static void
xpress_init_output(struct xpress_output_bitstream *os, void *buffer, size_t size)
{
	os->bitbuf = 0;
	os->bitcount = 0;
	os->start = buffer;
	os->next_bits = os->start;
	os->next_bits2 = os->start + 2;
	os->next_byte = os->start + 4;
	os->end = os->start + size;
}

/*
 * Write some bits to the output bitstream.
 *
 * The bits are given by the low-order @num_bits bits of @bits.  Higher-order
 * bits in @bits cannot be set.  At most 16 bits can be written at once.
 *
 * If the output buffer space is exhausted, then the bits will be ignored, and
 * xpress_flush_output() will return 0 when it gets called.
 */
static forceinline void
xpress_write_bits(struct xpress_output_bitstream *os,
		  const u32 bits, const unsigned num_bits)
{
	/* This code is optimized for XPRESS, which never needs to write more
	 * than 16 bits at once.  */

	os->bitcount += num_bits;
	os->bitbuf = (os->bitbuf << num_bits) | bits;

	if (os->bitcount > 16) {
		os->bitcount -= 16;
		if (os->end - os->next_byte >= 2) {
			put_unaligned_le16(os->bitbuf >> os->bitcount, os->next_bits);
			os->next_bits = os->next_bits2;
			os->next_bits2 = os->next_byte;
			os->next_byte += 2;
		}
	}
}

/*
 * Interweave a literal byte into the output bitstream.
 */
static forceinline void
xpress_write_byte(struct xpress_output_bitstream *os, u8 byte)
{
	if (os->next_byte < os->end)
		*os->next_byte++ = byte;
}

/*
 * Interweave two literal bytes into the output bitstream.
 */
static forceinline void
xpress_write_u16(struct xpress_output_bitstream *os, u16 v)
{
	if (os->end - os->next_byte >= 2) {
		put_unaligned_le16(v, os->next_byte);
		os->next_byte += 2;
	}
}

/*
 * Flush the last coding unit to the output buffer if needed.  Return the total
 * number of bytes written to the output buffer, or 0 if an overflow occurred.
 */
static size_t
xpress_flush_output(struct xpress_output_bitstream *os)
{
	if (os->end - os->next_byte < 2)
		return 0;

	put_unaligned_le16(os->bitbuf << (16 - os->bitcount), os->next_bits);
	put_unaligned_le16(0, os->next_bits2);

	return os->next_byte - os->start;
}

static forceinline void
xpress_write_extra_length_bytes(struct xpress_output_bitstream *os,
				unsigned adjusted_len)
{
	/* If length >= 18, output one extra length byte.
	 * If length >= 273, output three (total) extra length bytes.  */
	if (adjusted_len >= 0xF) {
		u8 byte1 = min(adjusted_len - 0xF, 0xFF);
		xpress_write_byte(os, byte1);
		if (byte1 == 0xFF)
			xpress_write_u16(os, adjusted_len);
	}
}

/* Output a match or literal.  */
static forceinline void
xpress_write_item(struct xpress_item item, struct xpress_output_bitstream *os,
		  const u32 codewords[], const u8 lens[])
{
	u64 data = item.data;
	unsigned symbol = data & 0x1FF;

	xpress_write_bits(os, codewords[symbol], lens[symbol]);

	if (symbol >= XPRESS_NUM_CHARS) {
		/* Match, not a literal  */
		xpress_write_extra_length_bytes(os, (data >> 9) & 0xFFFF);
		xpress_write_bits(os, data >> 29, (data >> 25) & 0xF);
	}
}

/* Output a sequence of XPRESS matches and literals.  */
static void
xpress_write_items(struct xpress_output_bitstream *os,
		   const struct xpress_item items[], size_t num_items,
		   const u32 codewords[], const u8 lens[])
{
	for (size_t i = 0; i < num_items; i++)
		xpress_write_item(items[i], os, codewords, lens);
}

#if SUPPORT_NEAR_OPTIMAL_PARSING

/*
 * Follow the minimum cost path in the graph of possible match/literal choices
 * and write out the matches/literals using the specified Huffman code.
 *
 * Note: this is slightly duplicated with xpress_write_items().  However, we
 * don't want to waste time translating between intermediate match/literal
 * representations.
 */
static void
xpress_write_item_list(struct xpress_output_bitstream *os,
		       struct xpress_optimum_node *optimum_nodes,
		       size_t count, const u32 codewords[], const u8 lens[])
{
	struct xpress_optimum_node *cur_node = optimum_nodes;
	struct xpress_optimum_node *end_node = optimum_nodes + count;
	do {
		unsigned length = cur_node->item & OPTIMUM_LEN_MASK;
		unsigned offset = cur_node->item >> OPTIMUM_OFFSET_SHIFT;

		if (length == 1) {
			/* Literal  */
			unsigned literal = offset;

			xpress_write_bits(os, codewords[literal], lens[literal]);
		} else {
			/* Match  */
			unsigned adjusted_len;
			unsigned log2_offset;
			unsigned len_hdr;
			unsigned sym;

			adjusted_len = length - XPRESS_MIN_MATCH_LEN;
			log2_offset = bsr32(offset);
			len_hdr = min(0xF, adjusted_len);
			sym = XPRESS_NUM_CHARS + ((log2_offset << 4) | len_hdr);

			xpress_write_bits(os, codewords[sym], lens[sym]);
			xpress_write_extra_length_bytes(os, adjusted_len);
			xpress_write_bits(os, offset - (1U << log2_offset),
					  log2_offset);
		}
		cur_node += length;
	} while (cur_node != end_node);
}
#endif /* SUPPORT_NEAR_OPTIMAL_PARSING */

/*
 * Output the XPRESS-compressed data, given the sequence of match/literal
 * "items" that was chosen to represent the input data.
 *
 * If @near_optimal is %false, then the items are taken from the array
 * c->chosen_items[0...count].
 *
 * If @near_optimal is %true, then the items are taken from the minimum cost
 * path stored in c->optimum_nodes[0...count].
 */
static size_t
xpress_write(struct xpress_compressor *c, void *out, size_t out_nbytes_avail,
	     size_t count, bool near_optimal)
{
	u8 *cptr;
	struct xpress_output_bitstream os;
	size_t out_size;

	/* Account for the end-of-data symbol and make the Huffman code.  */
	c->freqs[XPRESS_END_OF_DATA]++;
	xpress_make_huffman_code(c);

	/* Output the Huffman code as a series of 512 4-bit lengths.  */
	cptr = out;
	for (unsigned i = 0; i < XPRESS_NUM_SYMBOLS; i += 2)
		*cptr++ = (c->lens[i + 1] << 4) | c->lens[i];

	xpress_init_output(&os, cptr, out_nbytes_avail - XPRESS_NUM_SYMBOLS / 2);

	/* Output the Huffman-encoded items.  */
#if SUPPORT_NEAR_OPTIMAL_PARSING
	if (near_optimal) {
		xpress_write_item_list(&os, c->optimum_nodes, count,
				       c->codewords, c->lens);

	} else
#endif
	{
		xpress_write_items(&os, c->chosen_items, count,
				   c->codewords, c->lens);
	}

	/* Write the end-of-data symbol (needed for MS compatibility)  */
	xpress_write_bits(&os, c->codewords[XPRESS_END_OF_DATA],
			  c->lens[XPRESS_END_OF_DATA]);

	/* Flush any pending data.  Then return the compressed size if the
	 * compressed data fit in the output buffer, or 0 if it did not.  */
	out_size = xpress_flush_output(&os);
	if (out_size == 0)
		return 0;

	return out_size + XPRESS_NUM_SYMBOLS / 2;
}

/* Tally the Huffman symbol for a literal and return the intermediate
 * representation of that literal.  */
static forceinline struct xpress_item
xpress_record_literal(struct xpress_compressor *c, unsigned literal)
{
	c->freqs[literal]++;

	return (struct xpress_item) {
		.data = literal,
	};
}

/* Tally the Huffman symbol for a match and return the intermediate
 * representation of that match.  */
static forceinline struct xpress_item
xpress_record_match(struct xpress_compressor *c, unsigned length, unsigned offset)
{
	unsigned adjusted_len = length - XPRESS_MIN_MATCH_LEN;
	unsigned len_hdr = min(adjusted_len, 0xF);
	unsigned log2_offset = bsr32(offset);
	unsigned sym = XPRESS_NUM_CHARS + ((log2_offset << 4) | len_hdr);

	wimlib_assert(sym < XPRESS_NUM_SYMBOLS);
	// coverity[overrun-local]
	c->freqs[sym]++;

	return (struct xpress_item) {
		.data = (u64)sym |
			((u64)adjusted_len << 9) |
			((u64)log2_offset << 25) |
			((u64)(offset ^ (1U << log2_offset)) << 29),
	};
}

/*
 * This is the "greedy" XPRESS compressor. It always chooses the longest match.
 * (Exception: as a heuristic, we pass up length 3 matches that have large
 * offsets.)
 */
static size_t
xpress_compress_greedy(struct xpress_compressor * restrict c,
		       const void * restrict in, size_t in_nbytes,
		       void * restrict out, size_t out_nbytes_avail)
{
	const u8 * const in_begin = in;
	const u8 *	 in_next = in_begin;
	const u8 * const in_end = in_begin + in_nbytes;
	struct xpress_item *next_chosen_item = c->chosen_items;
	unsigned len_3_too_far;
	u32 next_hashes[2] = { 0 };

	if (in_nbytes <= 8192)
		len_3_too_far = 2048;
	else
		len_3_too_far = 4096;

	hc_matchfinder_init(&c->hc_mf);

	do {
		unsigned length;
		unsigned offset;

		length = hc_matchfinder_longest_match(&c->hc_mf,
						      in_begin,
						      in_next,
						      XPRESS_MIN_MATCH_LEN - 1,
						      in_end - in_next,
						      min(in_end - in_next, c->nice_match_length),
						      c->max_search_depth,
						      next_hashes,
						      &offset);
		if (length >= XPRESS_MIN_MATCH_LEN &&
		    !(length == XPRESS_MIN_MATCH_LEN && offset >= len_3_too_far))
		{
			/* Match found  */
			*next_chosen_item++ =
				xpress_record_match(c, length, offset);
			in_next += 1;
			hc_matchfinder_skip_bytes(&c->hc_mf,
						  in_begin,
						  in_next,
						  in_end,
						  length - 1,
						  next_hashes);
			in_next += length - 1;
		} else {
			/* No match found  */
			*next_chosen_item++ =
				xpress_record_literal(c, *in_next);
			in_next += 1;
		}
	} while (in_next != in_end);

	return xpress_write(c, out, out_nbytes_avail,
			    next_chosen_item - c->chosen_items, false);
}

/*
 * This is the "lazy" XPRESS compressor.  Before choosing a match, it checks to
 * see if there's a longer match at the next position.  If yes, it outputs a
 * literal and continues to the next position.  If no, it outputs the match.
 */
static size_t
xpress_compress_lazy(struct xpress_compressor * restrict c,
		     const void * restrict in, size_t in_nbytes,
		     void * restrict out, size_t out_nbytes_avail)
{
	const u8 * const in_begin = in;
	const u8 *	 in_next = in_begin;
	const u8 * const in_end = in_begin + in_nbytes;
	struct xpress_item *next_chosen_item = c->chosen_items;
	unsigned len_3_too_far;
	u32 next_hashes[2] = { 0 };

	if (in_nbytes <= 8192)
		len_3_too_far = 2048;
	else
		len_3_too_far = 4096;

	hc_matchfinder_init(&c->hc_mf);

	do {
		unsigned cur_len;
		unsigned cur_offset;
		unsigned next_len;
		unsigned next_offset;

		/* Find the longest match at the current position.  */
		cur_len = hc_matchfinder_longest_match(&c->hc_mf,
						       in_begin,
						       in_next,
						       XPRESS_MIN_MATCH_LEN - 1,
						       in_end - in_next,
						       min(in_end - in_next, c->nice_match_length),
						       c->max_search_depth,
						       next_hashes,
						       &cur_offset);
		in_next += 1;

		if (cur_len < XPRESS_MIN_MATCH_LEN ||
		    (cur_len == XPRESS_MIN_MATCH_LEN &&
		     cur_offset >= len_3_too_far))
		{
			/* No match found.  Choose a literal.  */
			*next_chosen_item++ =
				xpress_record_literal(c, *(in_next - 1));
			continue;
		}

	have_cur_match:
		/* We have a match at the current position.  */

		/* If the current match is very long, choose it immediately.  */
		if (cur_len >= c->nice_match_length) {

			*next_chosen_item++ =
				xpress_record_match(c, cur_len, cur_offset);

			hc_matchfinder_skip_bytes(&c->hc_mf,
						  in_begin,
						  in_next,
						  in_end,
						  cur_len - 1,
						  next_hashes);
			in_next += cur_len - 1;
			continue;
		}

		/*
		 * Try to find a match at the next position.
		 *
		 * Note: since we already have a match at the *current*
		 * position, we use only half the 'max_search_depth' when
		 * checking the *next* position.  This is a useful trade-off
		 * because it's more worthwhile to use a greater search depth on
		 * the initial match than on the next match (since a lot of the
		 * time, that next match won't even be used).
		 *
		 * Note: it's possible to structure the code such that there's
		 * only one call to longest_match(), which handles both the
		 * "find the initial match" and "try to find a longer match"
		 * cases.  However, it is faster to have two call sites, with
		 * longest_match() inlined at each.
		 */
		next_len = hc_matchfinder_longest_match(&c->hc_mf,
							in_begin,
							in_next,
							cur_len,
						        in_end - in_next,
						        min(in_end - in_next, c->nice_match_length),
							c->max_search_depth / 2,
							next_hashes,
							&next_offset);
		in_next += 1;

		if (next_len > cur_len) {
			/* Found a longer match at the next position, so output
			 * a literal.  */
			*next_chosen_item++ =
				xpress_record_literal(c, *(in_next - 2));
			cur_len = next_len;
			cur_offset = next_offset;
			goto have_cur_match;
		} else {
			/* Didn't find a longer match at the next position, so
			 * output the current match.  */
			*next_chosen_item++ =
				xpress_record_match(c, cur_len, cur_offset);
			hc_matchfinder_skip_bytes(&c->hc_mf,
						  in_begin,
						  in_next,
						  in_end,
						  cur_len - 2,
						  next_hashes);
			in_next += cur_len - 2;
			continue;
		}
	} while (in_next != in_end);

	return xpress_write(c, out, out_nbytes_avail,
			    next_chosen_item - c->chosen_items, false);
}

#if SUPPORT_NEAR_OPTIMAL_PARSING

/*
 * Set Huffman symbol costs for the first optimization pass.
 *
 * It works well to assume that each Huffman symbol is equally probable.  This
 * results in each symbol being assigned a cost of -log2(1.0/num_syms) where
 * 'num_syms' is the number of symbols in the alphabet.
 */
static void
xpress_set_default_costs(struct xpress_compressor *c)
{
	for (unsigned i = 0; i < XPRESS_NUM_SYMBOLS; i++)
		c->costs[i] = 9;
}

/* Update the cost model based on the codeword lengths @c->lens.  */
static void
xpress_update_costs(struct xpress_compressor *c)
{
	for (unsigned i = 0; i < XPRESS_NUM_SYMBOLS; i++)
		c->costs[i] = c->lens[i] ? c->lens[i] : XPRESS_MAX_CODEWORD_LEN;
}

/*
 * Follow the minimum cost path in the graph of possible match/literal choices
 * and compute the frequencies of the Huffman symbols that are needed to output
 * those matches and literals.
 */
static void
xpress_tally_item_list(struct xpress_compressor *c,
		       struct xpress_optimum_node *end_node)
{
	struct xpress_optimum_node *cur_node = c->optimum_nodes;

	do {
		unsigned length = cur_node->item & OPTIMUM_LEN_MASK;
		unsigned offset = cur_node->item >> OPTIMUM_OFFSET_SHIFT;

		if (length == 1) {
			/* Literal  */
			unsigned literal = offset;

			c->freqs[literal]++;
		} else {
			/* Match  */
			unsigned adjusted_len;
			unsigned log2_offset;
			unsigned len_hdr;
			unsigned sym;

			adjusted_len = length - XPRESS_MIN_MATCH_LEN;
			log2_offset = bsr32(offset);
			len_hdr = min(0xF, adjusted_len);
			sym = XPRESS_NUM_CHARS + ((log2_offset << 4) | len_hdr);

			wimlib_assert(sym < XPRESS_NUM_SYMBOLS);
			// coverity[overrun-local]
			c->freqs[sym]++;
		}
		cur_node += length;
	} while (cur_node != end_node);
}

/*
 * Find a new minimum cost path through the graph of possible match/literal
 * choices.  We find the minimum cost path from 'c->optimum_nodes[0]', which
 * represents the node at the beginning of the input buffer, to
 * 'c->optimum_nodes[in_nbytes]', which represents the node at the end of the
 * input buffer.  Edge costs are evaluated using the cost model 'c->costs'.
 *
 * The algorithm works backward, starting at 'c->optimum_nodes[in_nbytes]' and
 * proceeding backwards one position at a time.  At each position, the minimum
 * cost to reach 'c->optimum_nodes[in_nbytes]' from that position is computed
 * and the match/literal choice is saved.
 */
static void
xpress_find_min_cost_path(struct xpress_compressor *c, size_t in_nbytes,
			  struct lz_match *end_cache_ptr)
{
	struct xpress_optimum_node *cur_node = c->optimum_nodes + in_nbytes;
	struct lz_match *cache_ptr = end_cache_ptr;

	cur_node->cost_to_end = 0;
	do {
		unsigned literal;
		u32 best_item;
		u32 best_cost_to_end;
		unsigned num_matches;
		struct lz_match *match;
		unsigned len;

		cur_node--;
		cache_ptr--;

		literal = cache_ptr->offset;

		/* Consider coding a literal.  */
		best_item = ((u32)literal << OPTIMUM_OFFSET_SHIFT) | 1;
		best_cost_to_end = c->costs[literal] +
				   (cur_node + 1)->cost_to_end;

		num_matches = cache_ptr->length;

		if (num_matches == 0) {
			/* No matches; the only choice is the literal.  */
			cur_node->cost_to_end = best_cost_to_end;
			cur_node->item = best_item;
			continue;
		}

		/*
		 * Consider each match length from the minimum
		 * (XPRESS_MIN_MATCH_LEN) to the length of the longest match
		 * found at this position.  For each length, consider only the
		 * smallest offset for which that length is available.  Although
		 * this is not guaranteed to be optimal due to the possibility
		 * of a larger offset costing less than a smaller offset to
		 * code, this is a very useful heuristic.
		 */
		match = cache_ptr - num_matches;
		len = XPRESS_MIN_MATCH_LEN;
		if (cache_ptr[-1].length < 0xF + XPRESS_MIN_MATCH_LEN) {
			/* All lengths are small.  Optimize accordingly.  */
			do {
				unsigned offset;
				unsigned log2_offset;
				u32 offset_cost;

				offset = match->offset;
				log2_offset = bsr32(offset);
				offset_cost = log2_offset;
				do {
					unsigned len_hdr;
					unsigned sym;
					u32 cost_to_end;

					len_hdr = len - XPRESS_MIN_MATCH_LEN;
					sym = XPRESS_NUM_CHARS +
					      ((log2_offset << 4) | len_hdr);
					cost_to_end =
						offset_cost + c->costs[sym] +
						(cur_node + len)->cost_to_end;
					if (cost_to_end < best_cost_to_end) {
						best_cost_to_end = cost_to_end;
						best_item =
							((u32)offset <<
							 OPTIMUM_OFFSET_SHIFT) | len;
					}
				} while (++len <= match->length);
			} while (++match != cache_ptr);
		} else {
			/* Some lengths are big.  */
			do {
				unsigned offset;
				unsigned log2_offset;
				u32 offset_cost;

				offset = match->offset;
				log2_offset = bsr32(offset);
				offset_cost = log2_offset;
				do {
					unsigned adjusted_len;
					unsigned len_hdr;
					unsigned sym;
					u32 cost_to_end;

					adjusted_len = len - XPRESS_MIN_MATCH_LEN;
					len_hdr = min(adjusted_len, 0xF);
					sym = XPRESS_NUM_CHARS +
					      ((log2_offset << 4) | len_hdr);
					wimlib_assert(sym < XPRESS_NUM_SYMBOLS);
					// coverity[overrun-local]
					cost_to_end =
						offset_cost + c->costs[sym] +
						(cur_node + len)->cost_to_end;
					if (adjusted_len >= 0xF) {
						cost_to_end += 8;
						if (adjusted_len - 0xF >= 0xFF)
							cost_to_end += 16;
					}
					if (cost_to_end < best_cost_to_end) {
						best_cost_to_end = cost_to_end;
						best_item =
							((u32)offset <<
							 OPTIMUM_OFFSET_SHIFT) | len;
					}
				} while (++len <= match->length);
			} while (++match != cache_ptr);
		}
		cache_ptr -= num_matches;
		cur_node->cost_to_end = best_cost_to_end;
		cur_node->item = best_item;
	} while (cur_node != c->optimum_nodes);
}

/*
 * This routine finds matches at each position in the buffer in[0...in_nbytes].
 * The matches are cached in the array c->match_cache, and the return value is a
 * pointer past the last slot in this array that was filled.
 */
static struct lz_match *
xpress_find_matches(struct xpress_compressor * restrict c,
		    const void * restrict in, size_t in_nbytes)
{
	const u8 * const in_begin = in;
	const u8 *in_next = in_begin;
	struct lz_match *cache_ptr = c->match_cache;
	u32 next_hashes[2] = { 0 };
	u32 max_len = in_nbytes;
	u32 nice_len = min(max_len, c->nice_match_length);

	bt_matchfinder_init(&c->bt_mf);

	for (;;) {
		struct lz_match *matches;
		u32 best_len;

		/* If we've found so many matches that the cache might overflow
		 * if we keep finding more, then stop finding matches.  This
		 * case is very unlikely.  */
		if (unlikely(cache_ptr >= c->cache_overflow_mark ||
			     max_len < BT_MATCHFINDER_REQUIRED_NBYTES))
			break;

		matches = cache_ptr;

		/* Find matches with the current position using the binary tree
		 * matchfinder and save them in the next available slots in
		 * the match cache.  */
		cache_ptr =
			bt_matchfinder_get_matches(&c->bt_mf,
						   in_begin,
						   in_next - in_begin,
						   max_len,
						   nice_len,
						   c->max_search_depth,
						   next_hashes,
						   &best_len,
						   cache_ptr);
		cache_ptr->length = cache_ptr - matches;
		cache_ptr->offset = *in_next++;
		cache_ptr++;
		max_len--;
		nice_len = min(nice_len, max_len);

		/*
		 * If there was a very long match found, then don't cache any
		 * matches for the bytes covered by that match.  This avoids
		 * degenerate behavior when compressing highly redundant data,
		 * where the number of matches can be very large.
		 *
		 * This heuristic doesn't actually hurt the compression ratio
		 * very much.  If there's a long match, then the data must be
		 * highly compressible, so it doesn't matter as much what we do.
		 */
		if (best_len >= nice_len) {
			if (unlikely(best_len +
				     BT_MATCHFINDER_REQUIRED_NBYTES >= max_len))
				break;
			--best_len;
			do {
				bt_matchfinder_skip_byte(&c->bt_mf,
							 in_begin,
							 in_next - in_begin,
							 nice_len,
							 c->max_search_depth,
							 next_hashes);
				cache_ptr->length = 0;
				cache_ptr->offset = *in_next++;
				cache_ptr++;
				max_len--;
				nice_len = min(nice_len, max_len);
			} while (--best_len);
		}
	}

	while (max_len--) {
		cache_ptr->length = 0;
		cache_ptr->offset = *in_next++;
		cache_ptr++;
	}

	return cache_ptr;
}

/*
 * This is the "near-optimal" XPRESS compressor.  It computes a compressed
 * representation of the input buffer by executing a minimum cost path search
 * over the graph of possible match/literal choices, assuming a certain cost for
 * each Huffman symbol.  The result is usually close to optimal, but it is *not*
 * guaranteed to be optimal because of (a) heuristic restrictions in which
 * matches are considered, and (b) symbol costs are unknown until those symbols
 * have already been chosen --- so iterative optimization must be used, and the
 * algorithm might converge on a local optimum rather than a global optimum.
 */
static size_t
xpress_compress_near_optimal(struct xpress_compressor * restrict c,
			     const void * restrict in, size_t in_nbytes,
			     void * restrict out, size_t out_nbytes_avail)
{
	struct lz_match *end_cache_ptr;
	unsigned num_passes_remaining = c->num_optim_passes;

	/* Run the input buffer through the matchfinder and save the results. */
	end_cache_ptr = xpress_find_matches(c, in, in_nbytes);

	/* The first optimization pass uses a default cost model.  Each
	 * additional optimization pass uses a cost model derived from the
	 * Huffman code computed in the previous pass.  */
	xpress_set_default_costs(c);
	do {
		xpress_find_min_cost_path(c, in_nbytes, end_cache_ptr);
		xpress_tally_item_list(c, c->optimum_nodes + in_nbytes);
		if (num_passes_remaining > 1) {
			c->freqs[XPRESS_END_OF_DATA]++;
			xpress_make_huffman_code(c);
			xpress_update_costs(c);
			xpress_reset_symbol_frequencies(c);
		}
	} while (--num_passes_remaining);

	return xpress_write(c, out, out_nbytes_avail, in_nbytes, true);
}

#endif /* SUPPORT_NEAR_OPTIMAL_PARSING */

static size_t
xpress_get_compressor_size(size_t max_bufsize, unsigned compression_level)
{
#if SUPPORT_NEAR_OPTIMAL_PARSING
	if (compression_level >= MIN_LEVEL_FOR_NEAR_OPTIMAL)
		return offsetof(struct xpress_compressor, bt_mf) +
			bt_matchfinder_size(max_bufsize);
#endif

	return offsetof(struct xpress_compressor, hc_mf) +
		hc_matchfinder_size(max_bufsize);
}

static u64
xpress_get_needed_memory(size_t max_bufsize, unsigned compression_level,
			 bool destructive)
{
	u64 size = 0;

	if (max_bufsize > XPRESS_MAX_BUFSIZE)
		return 0;

	size += xpress_get_compressor_size(max_bufsize, compression_level);

	if (compression_level < MIN_LEVEL_FOR_NEAR_OPTIMAL ||
	    !SUPPORT_NEAR_OPTIMAL_PARSING) {
		/* chosen_items  */
		size += max_bufsize * sizeof(struct xpress_item);
	}
#if SUPPORT_NEAR_OPTIMAL_PARSING
	else {
		/* optimum_nodes  */
		size += (max_bufsize + 1) * sizeof(struct xpress_optimum_node);
		/* match_cache */
		size += ((max_bufsize * CACHE_RESERVE_PER_POS) +
			 XPRESS_MAX_MATCH_LEN + max_bufsize) *
				sizeof(struct lz_match);
	}
#endif
	return size;
}

static int
xpress_create_compressor(size_t max_bufsize, unsigned compression_level,
			 bool destructive, void **c_ret)
{
	struct xpress_compressor *c;

	if (max_bufsize > XPRESS_MAX_BUFSIZE)
		return WIMLIB_ERR_INVALID_PARAM;

	c = MALLOC(xpress_get_compressor_size(max_bufsize, compression_level));
	if (!c)
		goto oom0;

	if (compression_level < MIN_LEVEL_FOR_NEAR_OPTIMAL ||
	    !SUPPORT_NEAR_OPTIMAL_PARSING)
	{

		c->chosen_items = MALLOC(max_bufsize * sizeof(struct xpress_item));
		if (!c->chosen_items)
			goto oom1;

		if (compression_level < 30) {
			c->impl = xpress_compress_greedy;
			c->max_search_depth = (compression_level * 30) / 16;
			c->nice_match_length = (compression_level * 60) / 16;
		} else {
			c->impl = xpress_compress_lazy;
			c->max_search_depth = (compression_level * 30) / 32;
			c->nice_match_length = (compression_level * 60) / 32;

			/* xpress_compress_lazy() needs max_search_depth >= 2
			 * because it halves the max_search_depth when
			 * attempting a lazy match, and max_search_depth cannot
			 * be 0.  */
			if (c->max_search_depth < 2)
				c->max_search_depth = 2;
		}
	}
#if SUPPORT_NEAR_OPTIMAL_PARSING
	else {

		c->optimum_nodes = MALLOC((max_bufsize + 1) *
					  sizeof(struct xpress_optimum_node));
		c->match_cache = MALLOC(((max_bufsize * CACHE_RESERVE_PER_POS) +
					 XPRESS_MAX_MATCH_LEN + max_bufsize) *
					sizeof(struct lz_match));
		if (!c->optimum_nodes || !c->match_cache) {
			FREE(c->optimum_nodes);
			FREE(c->match_cache);
			goto oom1;
		}
		c->cache_overflow_mark =
			&c->match_cache[max_bufsize * CACHE_RESERVE_PER_POS];

		c->impl = xpress_compress_near_optimal;
		c->max_search_depth = (compression_level * 28) / 100;
		c->nice_match_length = (compression_level * 56) / 100;
		c->num_optim_passes = compression_level / 40;
	}
#endif /* SUPPORT_NEAR_OPTIMAL_PARSING */

	/* max_search_depth == 0 is invalid.  */
	if (c->max_search_depth < 1)
		c->max_search_depth = 1;

	*c_ret = c;
	return 0;

oom1:
	FREE(c);
oom0:
	return WIMLIB_ERR_NOMEM;
}

static size_t
xpress_compress(const void *restrict in, size_t in_nbytes,
		void *restrict out, size_t out_nbytes_avail, void *restrict _c)
{
	struct xpress_compressor *c = _c;

	/* Don't bother trying to compress very small inputs.  */
	if (in_nbytes < 25)
		return 0;

	if (out_nbytes_avail <= XPRESS_NUM_SYMBOLS / 2 + 4)
		return 0;

	xpress_reset_symbol_frequencies(c);

	return (*c->impl)(c, in, in_nbytes, out, out_nbytes_avail);
}

static void
xpress_free_compressor(void *_c)
{
	struct xpress_compressor *c = _c;

#if SUPPORT_NEAR_OPTIMAL_PARSING
	if (c->impl == xpress_compress_near_optimal) {
		FREE(c->optimum_nodes);
		FREE(c->match_cache);
	} else
#endif
		FREE(c->chosen_items);
	FREE(c);
}

const struct compressor_ops xpress_compressor_ops = {
	.get_needed_memory  = xpress_get_needed_memory,
	.create_compressor  = xpress_create_compressor,
	.compress	    = xpress_compress,
	.free_compressor    = xpress_free_compressor,
};
