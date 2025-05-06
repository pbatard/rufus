/*
 * lzx_compress.c
 *
 * A compressor for the LZX compression format, as used in WIM archives.
 */

/*
 * Copyright (C) 2012-2017 Eric Biggers
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
 * This file contains a compressor for the LZX ("Lempel-Ziv eXtended")
 * compression format, as used in the WIM (Windows IMaging) file format.
 *
 * Two different LZX-compatible algorithms are implemented: "near-optimal" and
 * "lazy".  "Near-optimal" is significantly slower than "lazy", but results in a
 * better compression ratio.  The "near-optimal" algorithm is used at the
 * default compression level.
 *
 * This file may need some slight modifications to be used outside of the WIM
 * format.  In particular, in other situations the LZX block header might be
 * slightly different, and sliding window support might be required.
 *
 * LZX is a compression format derived from DEFLATE, the format used by zlib and
 * gzip.  Both LZX and DEFLATE use LZ77 matching and Huffman coding.  Certain
 * details are quite similar, such as the method for storing Huffman codes.
 * However, the main differences are:
 *
 * - LZX preprocesses the data to attempt to make x86 machine code slightly more
 *   compressible before attempting to compress it further.
 *
 * - LZX uses a "main" alphabet which combines literals and matches, with the
 *   match symbols containing a "length header" (giving all or part of the match
 *   length) and an "offset slot" (giving, roughly speaking, the order of
 *   magnitude of the match offset).
 *
 * - LZX does not have static Huffman blocks (that is, the kind with preset
 *   Huffman codes); however it does have two types of dynamic Huffman blocks
 *   ("verbatim" and "aligned").
 *
 * - LZX has a minimum match length of 2 rather than 3.  Length 2 matches can be
 *   useful, but generally only if the compressor is smart about choosing them.
 *
 * - In LZX, offset slots 0 through 2 actually represent entries in an LRU queue
 *   of match offsets.  This is very useful for certain types of files, such as
 *   binary files that have repeating records.
 */

/******************************************************************************/
/*                            General parameters                              */
/*----------------------------------------------------------------------------*/

/*
 * The compressor uses the faster algorithm at levels <= MAX_FAST_LEVEL.  It
 * uses the slower algorithm at levels > MAX_FAST_LEVEL.
 */
#define MAX_FAST_LEVEL				34

/*
 * The compressor-side limits on the codeword lengths (in bits) for each Huffman
 * code.  To make outputting bits slightly faster, some of these limits are
 * lower than the limits defined by the LZX format.  This does not significantly
 * affect the compression ratio.
 */
#define MAIN_CODEWORD_LIMIT			16
#define LENGTH_CODEWORD_LIMIT			12
#define ALIGNED_CODEWORD_LIMIT			7
#define PRE_CODEWORD_LIMIT			7


/******************************************************************************/
/*                         Block splitting parameters                         */
/*----------------------------------------------------------------------------*/

/*
 * The compressor always outputs blocks of at least this size in bytes, except
 * for the last block which may need to be smaller.
 */
#define MIN_BLOCK_SIZE				6500

/*
 * The compressor attempts to end a block when it reaches this size in bytes.
 * The final size might be slightly larger due to matches extending beyond the
 * end of the block.  Specifically:
 *
 *  - The near-optimal compressor may choose a match of up to LZX_MAX_MATCH_LEN
 *    bytes starting at position 'SOFT_MAX_BLOCK_SIZE - 1'.
 *
 *  - The lazy compressor may choose a sequence of literals starting at position
 *    'SOFT_MAX_BLOCK_SIZE - 1' when it sees a sequence of increasingly better
 *    matches.  The final match may be up to LZX_MAX_MATCH_LEN bytes.  The
 *    length of the literal sequence is approximately limited by the "nice match
 *    length" parameter.
 */
#define SOFT_MAX_BLOCK_SIZE			100000

/*
 * The number of observed items (matches and literals) that represents
 * sufficient data for the compressor to decide whether the current block should
 * be ended or not.
 */
#define NUM_OBSERVATIONS_PER_BLOCK_CHECK	400


/******************************************************************************/
/*                      Parameters for slower algorithm                       */
/*----------------------------------------------------------------------------*/

/*
 * The log base 2 of the number of entries in the hash table for finding length
 * 2 matches.  This could be as high as 16, but using a smaller hash table
 * speeds up compression due to reduced cache pressure.
 */
#define BT_MATCHFINDER_HASH2_ORDER		12

/*
 * The number of lz_match structures in the match cache, excluding the extra
 * "overflow" entries.  This value should be high enough so that nearly the
 * time, all matches found in a given block can fit in the match cache.
 * However, fallback behavior (immediately terminating the block) on cache
 * overflow is still required.
 */
#define CACHE_LENGTH				(SOFT_MAX_BLOCK_SIZE * 5)

/*
 * An upper bound on the number of matches that can ever be saved in the match
 * cache for a single position.  Since each match we save for a single position
 * has a distinct length, we can use the number of possible match lengths in LZX
 * as this bound.  This bound is guaranteed to be valid in all cases, although
 * if 'nice_match_length < LZX_MAX_MATCH_LEN', then it will never actually be
 * reached.
 */
#define MAX_MATCHES_PER_POS			LZX_NUM_LENS

/*
 * A scaling factor that makes it possible to consider fractional bit costs.  A
 * single bit has a cost of BIT_COST.
 *
 * Note: this is only useful as a statistical trick for when the true costs are
 * unknown.  Ultimately, each token in LZX requires a whole number of bits to
 * output.
 */
#define BIT_COST				64

/*
 * Should the compressor take into account the costs of aligned offset symbols
 * instead of assuming that all are equally likely?
 */
#define CONSIDER_ALIGNED_COSTS			1

/*
 * Should the "minimum" cost path search algorithm consider "gap" matches, where
 * a normal match is followed by a literal, then by a match with the same
 * offset?  This is one specific, somewhat common situation in which the true
 * minimum cost path is often different from the path found by looking only one
 * edge ahead.
 */
#define CONSIDER_GAP_MATCHES			1

/******************************************************************************/
/*                                  Includes                                  */
/*----------------------------------------------------------------------------*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/compress_common.h"
#include "wimlib/compressor_ops.h"
#include "wimlib/error.h"
#include "wimlib/lzx_common.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"

/* Note: BT_MATCHFINDER_HASH2_ORDER must be defined before including
 * bt_matchfinder.h. */

/* Matchfinders with 16-bit positions */
#define mf_pos_t	u16
#define MF_SUFFIX	_16
#include "wimlib/bt_matchfinder.h"
#include "wimlib/hc_matchfinder.h"

/* Matchfinders with 32-bit positions */
#undef mf_pos_t
#undef MF_SUFFIX
#define mf_pos_t	u32
#define MF_SUFFIX	_32
#include "wimlib/bt_matchfinder.h"
#include "wimlib/hc_matchfinder.h"

/******************************************************************************/
/*                            Compressor structure                            */
/*----------------------------------------------------------------------------*/

/* Codewords for the Huffman codes */
struct lzx_codewords {
	u32 main[LZX_MAINCODE_MAX_NUM_SYMBOLS];
	u32 len[LZX_LENCODE_NUM_SYMBOLS];
	u32 aligned[LZX_ALIGNEDCODE_NUM_SYMBOLS];
};

/*
 * Codeword lengths, in bits, for the Huffman codes.
 *
 * A codeword length of 0 means the corresponding codeword has zero frequency.
 *
 * The main and length codes each have one extra entry for use as a sentinel.
 * See lzx_write_compressed_code().
 */
struct lzx_lens {
	u8 main[LZX_MAINCODE_MAX_NUM_SYMBOLS + 1];
	u8 len[LZX_LENCODE_NUM_SYMBOLS + 1];
	u8 aligned[LZX_ALIGNEDCODE_NUM_SYMBOLS];
};

/* Codewords and lengths for the Huffman codes */
struct lzx_codes {
	struct lzx_codewords codewords;
	struct lzx_lens lens;
};

/* Symbol frequency counters for the Huffman-encoded alphabets */
struct lzx_freqs {
	u32 main[LZX_MAINCODE_MAX_NUM_SYMBOLS];
	u32 len[LZX_LENCODE_NUM_SYMBOLS];
	u32 aligned[LZX_ALIGNEDCODE_NUM_SYMBOLS];
};

/* Block split statistics.  See the "Block splitting algorithm" section later in
 * this file for details. */
#define NUM_LITERAL_OBSERVATION_TYPES 8
#define NUM_MATCH_OBSERVATION_TYPES 2
#define NUM_OBSERVATION_TYPES (NUM_LITERAL_OBSERVATION_TYPES + \
			       NUM_MATCH_OBSERVATION_TYPES)
struct lzx_block_split_stats {
	u32 new_observations[NUM_OBSERVATION_TYPES];
	u32 observations[NUM_OBSERVATION_TYPES];
	u32 num_new_observations;
	u32 num_observations;
};

/*
 * Represents a run of literals followed by a match or end-of-block.  This
 * structure is needed to temporarily store items chosen by the compressor,
 * since items cannot be written until all items for the block have been chosen
 * and the block's Huffman codes have been computed.
 */
PRAGMA_BEGIN_ALIGN(8)
struct lzx_sequence {

	/*
	 * Bits 9..31: the number of literals in this run.  This may be 0 and
	 * can be at most about SOFT_MAX_BLOCK_LENGTH.  The literals are not
	 * stored explicitly in this structure; instead, they are read directly
	 * from the uncompressed data.
	 *
	 * Bits 0..8: the length of the match which follows the literals, or 0
	 * if this literal run was the last in the block, so there is no match
	 * which follows it.  This can be at most LZX_MAX_MATCH_LEN.
	 */
	u32 litrunlen_and_matchlen;
#define SEQ_MATCHLEN_BITS	9
#define SEQ_MATCHLEN_MASK	(((u32)1 << SEQ_MATCHLEN_BITS) - 1)

	/*
	 * If 'matchlen' doesn't indicate end-of-block, then this contains:
	 *
	 * Bits 10..31: either the offset plus LZX_OFFSET_ADJUSTMENT or a recent
	 * offset code, depending on the offset slot encoded in the main symbol.
	 *
	 * Bits 0..9: the main symbol.
	 */
	u32 adjusted_offset_and_mainsym;
#define SEQ_MAINSYM_BITS	10
#define SEQ_MAINSYM_MASK	(((u32)1 << SEQ_MAINSYM_BITS) - 1)
} PRAGMA_END_ALIGN(8);

/*
 * This structure represents a byte position in the input buffer and a node in
 * the graph of possible match/literal choices.
 *
 * Logically, each incoming edge to this node is labeled with a literal or a
 * match that can be taken to reach this position from an earlier position; and
 * each outgoing edge from this node is labeled with a literal or a match that
 * can be taken to advance from this position to a later position.
 */
PRAGMA_BEGIN_ALIGN(8)
struct lzx_optimum_node {

	/* The cost, in bits, of the lowest-cost path that has been found to
	 * reach this position.  This can change as progressively lower cost
	 * paths are found to reach this position.  */
	u32 cost;

	/*
	 * The best arrival to this node, i.e. the match or literal that was
	 * used to arrive to this position at the given 'cost'.  This can change
	 * as progressively lower cost paths are found to reach this position.
	 *
	 * For non-gap matches, this variable is divided into two bitfields
	 * whose meanings depend on the item type:
	 *
	 * Literals:
	 *	Low bits are 0, high bits are the literal.
	 *
	 * Explicit offset matches:
	 *	Low bits are the match length, high bits are the offset plus
	 *	LZX_OFFSET_ADJUSTMENT.
	 *
	 * Repeat offset matches:
	 *	Low bits are the match length, high bits are the queue index.
	 *
	 * For gap matches, identified by OPTIMUM_GAP_MATCH set, special
	 * behavior applies --- see the code.
	 */
	u32 item;
#define OPTIMUM_OFFSET_SHIFT	SEQ_MATCHLEN_BITS
#define OPTIMUM_LEN_MASK	SEQ_MATCHLEN_MASK
#if CONSIDER_GAP_MATCHES
#  define OPTIMUM_GAP_MATCH 0x80000000
#endif

} PRAGMA_END_ALIGN(8);

/* The cost model for near-optimal parsing */
struct lzx_costs {

	/*
	 * 'match_cost[offset_slot][len - LZX_MIN_MATCH_LEN]' is the cost of a
	 * length 'len' match which has an offset belonging to 'offset_slot'.
	 * The cost includes the main symbol, the length symbol if required, and
	 * the extra offset bits if any, excluding any entropy-coded bits
	 * (aligned offset bits).  It does *not* include the cost of the aligned
	 * offset symbol which may be required.
	 */
	u16 match_cost[LZX_MAX_OFFSET_SLOTS][LZX_NUM_LENS];

	/* Cost of each symbol in the main code */
	u32 main[LZX_MAINCODE_MAX_NUM_SYMBOLS];

	/* Cost of each symbol in the length code */
	u32 len[LZX_LENCODE_NUM_SYMBOLS];

#if CONSIDER_ALIGNED_COSTS
	/* Cost of each symbol in the aligned offset code */
	u32 aligned[LZX_ALIGNEDCODE_NUM_SYMBOLS];
#endif
};

struct lzx_output_bitstream;

/* The main LZX compressor structure */
struct lzx_compressor {

	/* The buffer for preprocessed input data, if not using destructive
	 * compression */
	void *in_buffer;

	/* If true, then the compressor need not preserve the input buffer if it
	 * compresses the data successfully */
	bool destructive;

	/* Pointer to the compress() implementation chosen at allocation time */
	void (*impl)(struct lzx_compressor *, const u8 *, size_t,
		     struct lzx_output_bitstream *);

	/* The log base 2 of the window size for match offset encoding purposes.
	 * This will be >= LZX_MIN_WINDOW_ORDER and <= LZX_MAX_WINDOW_ORDER. */
	unsigned window_order;

	/* The number of symbols in the main alphabet.  This depends on the
	 * window order, since the window order determines the maximum possible
	 * match offset. */
	unsigned num_main_syms;

	/* The "nice" match length: if a match of this length is found, then it
	 * is chosen immediately without further consideration. */
	unsigned nice_match_length;

	/* The maximum search depth: at most this many potential matches are
	 * considered at each position. */
	unsigned max_search_depth;

	/* The number of optimization passes per block */
	unsigned num_optim_passes;

	/* The symbol frequency counters for the current block */
	struct lzx_freqs freqs;

	/* Block split statistics for the current block */
	struct lzx_block_split_stats split_stats;

	/* The Huffman codes for the current and previous blocks.  The one with
	 * index 'codes_index' is for the current block, and the other one is
	 * for the previous block. */
	struct lzx_codes codes[2];
	unsigned codes_index;

	/* The matches and literals that the compressor has chosen for the
	 * current block.  The required length of this array is limited by the
	 * maximum number of matches that can ever be chosen for a single block,
	 * plus one for the special entry at the end. */
	struct lzx_sequence chosen_sequences[
		       DIV_ROUND_UP(SOFT_MAX_BLOCK_SIZE, LZX_MIN_MATCH_LEN) + 1];

	/* Tables for mapping adjusted offsets to offset slots */
	u8 offset_slot_tab_1[32768]; /* offset slots [0, 29] */
	u8 offset_slot_tab_2[128]; /* offset slots [30, 49] */

	union {
		/* Data for lzx_compress_lazy() */
		struct {
			/* Hash chains matchfinder (MUST BE LAST!!!) */
			union {
				struct hc_matchfinder_16 hc_mf_16;
				struct hc_matchfinder_32 hc_mf_32;
			};
		};

		/* Data for lzx_compress_near_optimal() */
		struct {
			/*
			 * Array of nodes, one per position, for running the
			 * minimum-cost path algorithm.
			 *
			 * This array must be large enough to accommodate the
			 * worst-case number of nodes, which occurs if the
			 * compressor finds a match of length LZX_MAX_MATCH_LEN
			 * at position 'SOFT_MAX_BLOCK_SIZE - 1', producing a
			 * block of size 'SOFT_MAX_BLOCK_SIZE - 1 +
			 * LZX_MAX_MATCH_LEN'.  Add one for the end-of-block
			 * node.
			 */
			struct lzx_optimum_node optimum_nodes[
						    SOFT_MAX_BLOCK_SIZE - 1 +
						    LZX_MAX_MATCH_LEN + 1];

			/* The cost model for the current optimization pass */
			struct lzx_costs costs;

			/*
			 * Cached matches for the current block.  This array
			 * contains the matches that were found at each position
			 * in the block.  Specifically, for each position, there
			 * is a special 'struct lz_match' whose 'length' field
			 * contains the number of matches that were found at
			 * that position; this is followed by the matches
			 * themselves, if any, sorted by strictly increasing
			 * length.
			 *
			 * Note: in rare cases, there will be a very high number
			 * of matches in the block and this array will overflow.
			 * If this happens, we force the end of the current
			 * block.  CACHE_LENGTH is the length at which we
			 * actually check for overflow.  The extra slots beyond
			 * this are enough to absorb the worst case overflow,
			 * which occurs if starting at &match_cache[CACHE_LENGTH
			 * - 1], we write the match count header, then write
			 * MAX_MATCHES_PER_POS matches, then skip searching for
			 * matches at 'LZX_MAX_MATCH_LEN - 1' positions and
			 * write the match count header for each.
			 */
			struct lz_match match_cache[CACHE_LENGTH +
						    MAX_MATCHES_PER_POS +
						    LZX_MAX_MATCH_LEN - 1];

			/* Binary trees matchfinder (MUST BE LAST!!!) */
			union {
				struct bt_matchfinder_16 bt_mf_16;
				struct bt_matchfinder_32 bt_mf_32;
			};
		};
	};
};

/******************************************************************************/
/*                            Matchfinder utilities                           */
/*----------------------------------------------------------------------------*/

/*
 * Will a matchfinder using 16-bit positions be sufficient for compressing
 * buffers of up to the specified size?  The limit could be 65536 bytes, but we
 * also want to optimize out the use of offset_slot_tab_2 in the 16-bit case.
 * This requires that the limit be no more than the length of offset_slot_tab_1
 * (currently 32768).
 */
static forceinline bool
lzx_is_16_bit(size_t max_bufsize)
{
	STATIC_ASSERT(ARRAY_LEN(((struct lzx_compressor *)0)->offset_slot_tab_1) == 32768);
	return max_bufsize <= 32768;
}

/*
 * Return the offset slot for the specified adjusted match offset.
 */
static forceinline unsigned
lzx_get_offset_slot(struct lzx_compressor *c, u32 adjusted_offset,
		    bool is_16_bit)
{
#ifndef _MSC_VER
	if (__builtin_constant_p(adjusted_offset) &&
	    adjusted_offset < LZX_NUM_RECENT_OFFSETS)
		return adjusted_offset;
#endif
	if (is_16_bit || adjusted_offset < ARRAY_LEN(c->offset_slot_tab_1))
		return c->offset_slot_tab_1[adjusted_offset];
	return c->offset_slot_tab_2[adjusted_offset >> 14];
}

/*
 * For a match that has the specified length and adjusted offset, tally its main
 * symbol, and if needed its length symbol; then return its main symbol.
 */
static forceinline unsigned
lzx_tally_main_and_lensyms(struct lzx_compressor *c, unsigned length,
			   u32 adjusted_offset, bool is_16_bit)
{
	unsigned mainsym;

	if (length >= LZX_MIN_SECONDARY_LEN) {
		/* Length symbol needed */
		c->freqs.len[length - LZX_MIN_SECONDARY_LEN]++;
		mainsym = LZX_NUM_CHARS + LZX_NUM_PRIMARY_LENS;
	} else {
		/* No length symbol needed */
		mainsym = LZX_NUM_CHARS + length - LZX_MIN_MATCH_LEN;
	}

	mainsym += LZX_NUM_LEN_HEADERS *
		   lzx_get_offset_slot(c, adjusted_offset, is_16_bit);
	c->freqs.main[mainsym]++;
	return mainsym;
}

/*
 * The following macros call either the 16-bit or the 32-bit version of a
 * matchfinder function based on the value of 'is_16_bit', which will be known
 * at compilation time.
 */

#define CALL_HC_MF(is_16_bit, c, funcname, ...)				      \
	((is_16_bit) ? CONCAT(funcname, _16)(&(c)->hc_mf_16, ##__VA_ARGS__) : \
		       CONCAT(funcname, _32)(&(c)->hc_mf_32, ##__VA_ARGS__));

#define CALL_BT_MF(is_16_bit, c, funcname, ...)				      \
	((is_16_bit) ? CONCAT(funcname, _16)(&(c)->bt_mf_16, ##__VA_ARGS__) : \
		       CONCAT(funcname, _32)(&(c)->bt_mf_32, ##__VA_ARGS__));

/******************************************************************************/
/*                             Output bitstream                               */
/*----------------------------------------------------------------------------*/

/*
 * The LZX bitstream is encoded as a sequence of little endian 16-bit coding
 * units.  Bits are ordered from most significant to least significant within
 * each coding unit.
 */

/*
 * Structure to keep track of the current state of sending bits to the
 * compressed output buffer.
 */
struct lzx_output_bitstream {

	/* Bits that haven't yet been written to the output buffer */
	machine_word_t bitbuf;

	/* Number of bits currently held in @bitbuf */
	machine_word_t bitcount;

	/* Pointer to the start of the output buffer */
	u8 *start;

	/* Pointer to the position in the output buffer at which the next coding
	 * unit should be written */
	u8 *next;

	/* Pointer to just past the end of the output buffer, rounded down by
	 * one byte if needed to make 'end - start' a multiple of 2 */
	u8 *end;
};

/* Can the specified number of bits always be added to 'bitbuf' after all
 * pending 16-bit coding units have been flushed?  */
#define CAN_BUFFER(n)	((n) <= WORDBITS - 15)

/* Initialize the output bitstream to write to the specified buffer. */
static void
lzx_init_output(struct lzx_output_bitstream *os, void *buffer, size_t size)
{
	os->bitbuf = 0;
	os->bitcount = 0;
	os->start = buffer;
	os->next = buffer;
	os->end = (u8 *)buffer + (size & ~1);
}

/*
 * Add some bits to the bitbuffer variable of the output bitstream.  The caller
 * must make sure there is enough room.
 */
static forceinline void
lzx_add_bits(struct lzx_output_bitstream *os, u32 bits, unsigned num_bits)
{
	os->bitbuf = (os->bitbuf << num_bits) | bits;
	os->bitcount += num_bits;
}

/*
 * Flush bits from the bitbuffer variable to the output buffer.  'max_num_bits'
 * specifies the maximum number of bits that may have been added since the last
 * flush.
 */
static forceinline void
lzx_flush_bits(struct lzx_output_bitstream *os, unsigned max_num_bits)
{
	/* Masking the number of bits to shift is only needed to avoid undefined
	 * behavior; we don't actually care about the results of bad shifts.  On
	 * x86, the explicit masking generates no extra code.  */
	const u32 shift_mask = WORDBITS - 1;

	if (os->end - os->next < 6)
		return;
	put_unaligned_le16(os->bitbuf >> ((os->bitcount - 16) &
					    shift_mask), os->next + 0);
	if (max_num_bits > 16)
		put_unaligned_le16(os->bitbuf >> ((os->bitcount - 32) &
						shift_mask), os->next + 2);
	if (max_num_bits > 32)
		put_unaligned_le16(os->bitbuf >> ((os->bitcount - 48) &
						shift_mask), os->next + 4);
	os->next += (os->bitcount >> 4) << 1;
	os->bitcount &= 15;
}

/* Add at most 16 bits to the bitbuffer and flush it.  */
static forceinline void
lzx_write_bits(struct lzx_output_bitstream *os, u32 bits, unsigned num_bits)
{
	lzx_add_bits(os, bits, num_bits);
	lzx_flush_bits(os, 16);
}

/*
 * Flush the last coding unit to the output buffer if needed.  Return the total
 * number of bytes written to the output buffer, or 0 if an overflow occurred.
 */
static size_t
lzx_flush_output(struct lzx_output_bitstream *os)
{
	if (os->end - os->next < 6)
		return 0;

	if (os->bitcount != 0) {
		put_unaligned_le16(os->bitbuf << (16 - os->bitcount), os->next);
		os->next += 2;
	}

	return os->next - os->start;
}

/******************************************************************************/
/*                           Preparing Huffman codes                          */
/*----------------------------------------------------------------------------*/

/*
 * Build the Huffman codes.  This takes as input the frequency tables for each
 * code and produces as output a set of tables that map symbols to codewords and
 * codeword lengths.
 */
static void
lzx_build_huffman_codes(struct lzx_compressor *c)
{
	const struct lzx_freqs *freqs = &c->freqs;
	struct lzx_codes *codes = &c->codes[c->codes_index];

	STATIC_ASSERT(MAIN_CODEWORD_LIMIT >= 9 &&
		      MAIN_CODEWORD_LIMIT <= LZX_MAX_MAIN_CODEWORD_LEN);
	make_canonical_huffman_code(c->num_main_syms,
				    MAIN_CODEWORD_LIMIT,
				    freqs->main,
				    codes->lens.main,
				    codes->codewords.main);

	STATIC_ASSERT(LENGTH_CODEWORD_LIMIT >= 8 &&
		      LENGTH_CODEWORD_LIMIT <= LZX_MAX_LEN_CODEWORD_LEN);
	make_canonical_huffman_code(LZX_LENCODE_NUM_SYMBOLS,
				    LENGTH_CODEWORD_LIMIT,
				    freqs->len,
				    codes->lens.len,
				    codes->codewords.len);

	STATIC_ASSERT(ALIGNED_CODEWORD_LIMIT >= LZX_NUM_ALIGNED_OFFSET_BITS &&
		      ALIGNED_CODEWORD_LIMIT <= LZX_MAX_ALIGNED_CODEWORD_LEN);
	make_canonical_huffman_code(LZX_ALIGNEDCODE_NUM_SYMBOLS,
				    ALIGNED_CODEWORD_LIMIT,
				    freqs->aligned,
				    codes->lens.aligned,
				    codes->codewords.aligned);
}

/* Reset the symbol frequencies for the current block. */
static void
lzx_reset_symbol_frequencies(struct lzx_compressor *c)
{
	memset(&c->freqs, 0, sizeof(c->freqs));
}

static unsigned
lzx_compute_precode_items(const u8* restrict lens,
			  const u8* restrict prev_lens,
			  u32* restrict precode_freqs,
			  unsigned* restrict precode_items)
{
	unsigned *itemptr;
	unsigned run_start;
	unsigned run_end;
	unsigned extra_bits;
	int delta;
	u8 len;

	itemptr = precode_items;
	run_start = 0;

	while (!((len = lens[run_start]) & 0x80)) {

		/* len = the length being repeated  */

		/* Find the next run of codeword lengths.  */

		run_end = run_start + 1;

		/* Fast case for a single length.  */
		if (likely(len != lens[run_end])) {
			delta = prev_lens[run_start] - len;
			if (delta < 0)
				delta += 17;
			precode_freqs[delta]++;
			*itemptr++ = delta;
			run_start++;
			continue;
		}

		/* Extend the run.  */
		do {
			run_end++;
		} while (len == lens[run_end]);

		if (len == 0) {
			/* Run of zeroes.  */

			/* Symbol 18: RLE 20 to 51 zeroes at a time.  */
			while ((run_end - run_start) >= 20) {
				extra_bits = min((run_end - run_start) - 20, 0x1F);
				precode_freqs[18]++;
				*itemptr++ = 18 | (extra_bits << 5);
				run_start += 20 + extra_bits;
			}

			/* Symbol 17: RLE 4 to 19 zeroes at a time.  */
			if ((run_end - run_start) >= 4) {
				extra_bits = min((run_end - run_start) - 4, 0xF);
				precode_freqs[17]++;
				*itemptr++ = 17 | (extra_bits << 5);
				run_start += 4 + extra_bits;
			}
		} else {

			/* A run of nonzero lengths. */

			/* Symbol 19: RLE 4 to 5 of any length at a time.  */
			while ((run_end - run_start) >= 4) {
				extra_bits = (run_end - run_start) > 4;
				delta = prev_lens[run_start] - len;
				if (delta < 0)
					delta += 17;
				precode_freqs[19]++;
				precode_freqs[delta]++;
				*itemptr++ = 19 | (extra_bits << 5) | (delta << 6);
				run_start += 4 + extra_bits;
			}
		}

		/* Output any remaining lengths without RLE.  */
		while (run_start != run_end) {
			delta = prev_lens[run_start] - len;
			if (delta < 0)
				delta += 17;
			precode_freqs[delta]++;
			*itemptr++ = delta;
			run_start++;
		}
	}

	return itemptr - precode_items;
}

/******************************************************************************/
/*                          Outputting compressed data                        */
/*----------------------------------------------------------------------------*/

/*
 * Output a Huffman code in the compressed form used in LZX.
 *
 * The Huffman code is represented in the output as a logical series of codeword
 * lengths from which the Huffman code, which must be in canonical form, can be
 * reconstructed.
 *
 * The codeword lengths are themselves compressed using a separate Huffman code,
 * the "precode", which contains a symbol for each possible codeword length in
 * the larger code as well as several special symbols to represent repeated
 * codeword lengths (a form of run-length encoding).  The precode is itself
 * constructed in canonical form, and its codeword lengths are represented
 * literally in 20 4-bit fields that immediately precede the compressed codeword
 * lengths of the larger code.
 *
 * Furthermore, the codeword lengths of the larger code are actually represented
 * as deltas from the codeword lengths of the corresponding code in the previous
 * block.
 *
 * @os:
 *	Bitstream to which to write the compressed Huffman code.
 * @lens:
 *	The codeword lengths, indexed by symbol, in the Huffman code.
 * @prev_lens:
 *	The codeword lengths, indexed by symbol, in the corresponding Huffman
 *	code in the previous block, or all zeroes if this is the first block.
 * @num_lens:
 *	The number of symbols in the Huffman code.
 */
static void
lzx_write_compressed_code(struct lzx_output_bitstream *os,
			  const u8* restrict lens,
			  const u8* restrict prev_lens,
			  unsigned num_lens)
{
	u32 precode_freqs[LZX_PRECODE_NUM_SYMBOLS];
	u8 precode_lens[LZX_PRECODE_NUM_SYMBOLS] = { 0 };
	u32 precode_codewords[LZX_PRECODE_NUM_SYMBOLS] = { 0 };
	unsigned* precode_items = alloca(num_lens * sizeof(unsigned));
	unsigned num_precode_items;
	unsigned precode_item;
	unsigned precode_sym;
	unsigned i;
	u8 saved = lens[num_lens];
	*(u8 *)(lens + num_lens) = 0x80;

	for (i = 0; i < LZX_PRECODE_NUM_SYMBOLS; i++)
		precode_freqs[i] = 0;

	/* Compute the "items" (RLE / literal tokens and extra bits) with which
	 * the codeword lengths in the larger code will be output.  */
	num_precode_items = lzx_compute_precode_items(lens,
						      prev_lens,
						      precode_freqs,
						      precode_items);

	/* Build the precode.  */
	STATIC_ASSERT(PRE_CODEWORD_LIMIT >= 5 &&
		      PRE_CODEWORD_LIMIT <= LZX_MAX_PRE_CODEWORD_LEN);
	make_canonical_huffman_code(LZX_PRECODE_NUM_SYMBOLS, PRE_CODEWORD_LIMIT,
				    precode_freqs, precode_lens,
				    precode_codewords);

	/* Output the lengths of the codewords in the precode.  */
	for (i = 0; i < LZX_PRECODE_NUM_SYMBOLS; i++)
		lzx_write_bits(os, precode_lens[i], LZX_PRECODE_ELEMENT_SIZE);

	/* Output the encoded lengths of the codewords in the larger code.  */
	for (i = 0; i < num_precode_items; i++) {
		precode_item = precode_items[i];
		precode_sym = precode_item & 0x1F;
		lzx_add_bits(os, precode_codewords[precode_sym],
			     precode_lens[precode_sym]);
		if (precode_sym >= 17) {
			if (precode_sym == 17) {
				lzx_add_bits(os, precode_item >> 5, 4);
			} else if (precode_sym == 18) {
				lzx_add_bits(os, precode_item >> 5, 5);
			} else {
				lzx_add_bits(os, (precode_item >> 5) & 1, 1);
				precode_sym = precode_item >> 6;
				lzx_add_bits(os, precode_codewords[precode_sym],
					     precode_lens[precode_sym]);
			}
		}
		STATIC_ASSERT(CAN_BUFFER(2 * PRE_CODEWORD_LIMIT + 1));
		lzx_flush_bits(os, 2 * PRE_CODEWORD_LIMIT + 1);
	}

	*(u8 *)(lens + num_lens) = saved;
}

/*
 * Write all matches and literal bytes (which were precomputed) in an LZX
 * compressed block to the output bitstream in the final compressed
 * representation.
 *
 * @os
 *	The output bitstream.
 * @block_type
 *	The chosen type of the LZX compressed block (LZX_BLOCKTYPE_ALIGNED or
 *	LZX_BLOCKTYPE_VERBATIM).
 * @block_data
 *	The uncompressed data of the block.
 * @sequences
 *	The matches and literals to output, given as a series of sequences.
 * @codes
 *	The main, length, and aligned offset Huffman codes for the block.
 */
static void
lzx_write_sequences(struct lzx_output_bitstream *os, int block_type,
		    const u8 *block_data, const struct lzx_sequence sequences[],
		    const struct lzx_codes *codes)
{
	const struct lzx_sequence *seq = sequences;
	unsigned min_aligned_offset_slot;

	if (block_type == LZX_BLOCKTYPE_ALIGNED)
		min_aligned_offset_slot = LZX_MIN_ALIGNED_OFFSET_SLOT;
	else
		min_aligned_offset_slot = LZX_MAX_OFFSET_SLOTS;

	for (;;) {
		/* Output the next sequence.  */

		u32 litrunlen = seq->litrunlen_and_matchlen >> SEQ_MATCHLEN_BITS;
		unsigned matchlen = seq->litrunlen_and_matchlen & SEQ_MATCHLEN_MASK;
		STATIC_ASSERT((u32)~SEQ_MATCHLEN_MASK >> SEQ_MATCHLEN_BITS >=
			      SOFT_MAX_BLOCK_SIZE);
		u32 adjusted_offset;
		unsigned main_symbol;
		unsigned offset_slot;
		unsigned num_extra_bits;
		u32 extra_bits;

		/* Output the literal run of the sequence.  */

		if (litrunlen) {  /* Is the literal run nonempty?  */

			/* Verify optimization is enabled on 64-bit  */
			STATIC_ASSERT(WORDBITS < 64 ||
				      CAN_BUFFER(3 * MAIN_CODEWORD_LIMIT));

			if (CAN_BUFFER(3 * MAIN_CODEWORD_LIMIT)) {

				/* 64-bit: write 3 literals at a time.  */
				while (litrunlen >= 3) {
					unsigned lit0 = block_data[0];
					unsigned lit1 = block_data[1];
					unsigned lit2 = block_data[2];
					lzx_add_bits(os, codes->codewords.main[lit0],
						     codes->lens.main[lit0]);
					lzx_add_bits(os, codes->codewords.main[lit1],
						     codes->lens.main[lit1]);
					lzx_add_bits(os, codes->codewords.main[lit2],
						     codes->lens.main[lit2]);
					lzx_flush_bits(os, 3 * MAIN_CODEWORD_LIMIT);
					block_data += 3;
					litrunlen -= 3;
				}
				if (litrunlen--) {
					unsigned lit = *block_data++;
					lzx_add_bits(os, codes->codewords.main[lit],
						     codes->lens.main[lit]);
					if (litrunlen--) {
						unsigned lit = *block_data++;
						lzx_add_bits(os, codes->codewords.main[lit],
							     codes->lens.main[lit]);
						lzx_flush_bits(os, 2 * MAIN_CODEWORD_LIMIT);
					} else {
						lzx_flush_bits(os, 1 * MAIN_CODEWORD_LIMIT);
					}
				}
			} else {
				/* 32-bit: write 1 literal at a time.  */
				do {
					unsigned lit = *block_data++;
					lzx_add_bits(os, codes->codewords.main[lit],
						     codes->lens.main[lit]);
					lzx_flush_bits(os, MAIN_CODEWORD_LIMIT);
				} while (--litrunlen);
			}
		}

		/* Was this the last literal run?  */
		if (matchlen == 0)
			return;

		/* Nope; output the match.  */

		block_data += matchlen;

		adjusted_offset = seq->adjusted_offset_and_mainsym >> SEQ_MAINSYM_BITS;
		main_symbol = seq->adjusted_offset_and_mainsym & SEQ_MAINSYM_MASK;

		offset_slot = (main_symbol - LZX_NUM_CHARS) / LZX_NUM_LEN_HEADERS;
		if (offset_slot >= LZX_MAX_OFFSET_SLOTS)
			return;
		num_extra_bits = lzx_extra_offset_bits[offset_slot];
		extra_bits = adjusted_offset - (lzx_offset_slot_base[offset_slot] +
						LZX_OFFSET_ADJUSTMENT);

	#define MAX_MATCH_BITS (MAIN_CODEWORD_LIMIT +		\
				LENGTH_CODEWORD_LIMIT +		\
				LZX_MAX_NUM_EXTRA_BITS -	\
				LZX_NUM_ALIGNED_OFFSET_BITS +	\
				ALIGNED_CODEWORD_LIMIT)

		/* Verify optimization is enabled on 64-bit  */
		STATIC_ASSERT(WORDBITS < 64 || CAN_BUFFER(MAX_MATCH_BITS));

		/* Output the main symbol for the match.  */
		if (main_symbol >= LZX_MAINCODE_MAX_NUM_SYMBOLS)
			return;
		lzx_add_bits(os, codes->codewords.main[main_symbol],
			     codes->lens.main[main_symbol]);
		if (!CAN_BUFFER(MAX_MATCH_BITS))
			lzx_flush_bits(os, MAIN_CODEWORD_LIMIT);

		/* If needed, output the length symbol for the match.  */

		if (matchlen >= LZX_MIN_SECONDARY_LEN &&
			matchlen < LZX_MIN_SECONDARY_LEN + LZX_LENCODE_NUM_SYMBOLS) {
			lzx_add_bits(os, codes->codewords.len[matchlen -
							      LZX_MIN_SECONDARY_LEN],
				     codes->lens.len[matchlen -
						     LZX_MIN_SECONDARY_LEN]);
			if (!CAN_BUFFER(MAX_MATCH_BITS))
				lzx_flush_bits(os, LENGTH_CODEWORD_LIMIT);
		}

		/* Output the extra offset bits for the match.  In aligned
		 * offset blocks, the lowest 3 bits of the adjusted offset are
		 * Huffman-encoded using the aligned offset code, provided that
		 * there are at least extra 3 offset bits required.  All other
		 * extra offset bits are output verbatim.  */

		if (offset_slot >= min_aligned_offset_slot) {

			lzx_add_bits(os, extra_bits >> LZX_NUM_ALIGNED_OFFSET_BITS,
				     num_extra_bits - LZX_NUM_ALIGNED_OFFSET_BITS);
			if (!CAN_BUFFER(MAX_MATCH_BITS))
				lzx_flush_bits(os, LZX_MAX_NUM_EXTRA_BITS -
						   LZX_NUM_ALIGNED_OFFSET_BITS);

			lzx_add_bits(os, codes->codewords.aligned[adjusted_offset &
								  LZX_ALIGNED_OFFSET_BITMASK],
				     codes->lens.aligned[adjusted_offset &
							 LZX_ALIGNED_OFFSET_BITMASK]);
			if (!CAN_BUFFER(MAX_MATCH_BITS))
				lzx_flush_bits(os, ALIGNED_CODEWORD_LIMIT);
		} else {
			STATIC_ASSERT(CAN_BUFFER(LZX_MAX_NUM_EXTRA_BITS));

			lzx_add_bits(os, extra_bits, num_extra_bits);
			if (!CAN_BUFFER(MAX_MATCH_BITS))
				lzx_flush_bits(os, LZX_MAX_NUM_EXTRA_BITS);
		}

		if (CAN_BUFFER(MAX_MATCH_BITS))
			lzx_flush_bits(os, MAX_MATCH_BITS);

		/* Advance to the next sequence.  */
		seq++;
	}
}

static void
lzx_write_compressed_block(const u8 *block_begin,
			   int block_type,
			   u32 block_size,
			   unsigned window_order,
			   unsigned num_main_syms,
			   const struct lzx_sequence sequences[],
			   const struct lzx_codes * codes,
			   const struct lzx_lens * prev_lens,
			   struct lzx_output_bitstream * os)
{
	/* The first three bits indicate the type of block and are one of the
	 * LZX_BLOCKTYPE_* constants.  */
	lzx_write_bits(os, block_type, 3);

	/*
	 * Output the block size.
	 *
	 * The original LZX format encoded the block size in 24 bits.  However,
	 * the LZX format used in WIM archives uses 1 bit to specify whether the
	 * block has the default size of 32768 bytes, then optionally 16 bits to
	 * specify a non-default size.  This works fine for Microsoft's WIM
	 * software (WIMGAPI), which never compresses more than 32768 bytes at a
	 * time with LZX.  However, as an extension, our LZX compressor supports
	 * compressing up to 2097152 bytes, with a corresponding increase in
	 * window size.  It is possible for blocks in these larger buffers to
	 * exceed 65535 bytes; such blocks cannot have their size represented in
	 * 16 bits.
	 *
	 * The chosen solution was to use 24 bits for the block size when
	 * possibly required --- specifically, when the compressor has been
	 * allocated to be capable of compressing more than 32768 bytes at once
	 * (which also causes the number of main symbols to be increased).
	 */
	if (block_size == LZX_DEFAULT_BLOCK_SIZE) {
		lzx_write_bits(os, 1, 1);
	} else {
		lzx_write_bits(os, 0, 1);

		if (window_order >= 16)
			lzx_write_bits(os, block_size >> 16, 8);

		lzx_write_bits(os, block_size & 0xFFFF, 16);
	}

	/* If it's an aligned offset block, output the aligned offset code.  */
	if (block_type == LZX_BLOCKTYPE_ALIGNED) {
		for (int i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
			lzx_write_bits(os, codes->lens.aligned[i],
				       LZX_ALIGNEDCODE_ELEMENT_SIZE);
		}
	}

	/* Output the main code (two parts).  */
	lzx_write_compressed_code(os, codes->lens.main,
				  prev_lens->main,
				  LZX_NUM_CHARS);
	lzx_write_compressed_code(os, codes->lens.main + LZX_NUM_CHARS,
				  prev_lens->main + LZX_NUM_CHARS,
				  num_main_syms - LZX_NUM_CHARS);

	/* Output the length code.  */
	lzx_write_compressed_code(os, codes->lens.len,
				  prev_lens->len,
				  LZX_LENCODE_NUM_SYMBOLS);

	/* Output the compressed matches and literals.  */
	lzx_write_sequences(os, block_type, block_begin, sequences, codes);
}

/*
 * Given the frequencies of symbols in an LZX-compressed block and the
 * corresponding Huffman codes, return LZX_BLOCKTYPE_ALIGNED or
 * LZX_BLOCKTYPE_VERBATIM if an aligned offset or verbatim block, respectively,
 * will take fewer bits to output.
 */
static int
lzx_choose_verbatim_or_aligned(const struct lzx_freqs * freqs,
			       const struct lzx_codes * codes)
{
	u32 verbatim_cost = 0;
	u32 aligned_cost = 0;

	/* A verbatim block requires 3 bits in each place that an aligned offset
	 * symbol would be used in an aligned offset block.  */
	for (unsigned i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
		verbatim_cost += LZX_NUM_ALIGNED_OFFSET_BITS * freqs->aligned[i];
		aligned_cost += codes->lens.aligned[i] * freqs->aligned[i];
	}

	/* Account for the cost of sending the codeword lengths of the aligned
	 * offset code.  */
	aligned_cost += LZX_ALIGNEDCODE_ELEMENT_SIZE *
			LZX_ALIGNEDCODE_NUM_SYMBOLS;

	if (aligned_cost < verbatim_cost)
		return LZX_BLOCKTYPE_ALIGNED;
	else
		return LZX_BLOCKTYPE_VERBATIM;
}

/*
 * Flush an LZX block:
 *
 * 1. Build the Huffman codes.
 * 2. Decide whether to output the block as VERBATIM or ALIGNED.
 * 3. Write the block.
 * 4. Swap the indices of the current and previous Huffman codes.
 *
 * Note: we never output UNCOMPRESSED blocks.  This probably should be
 * implemented sometime, but it doesn't make much difference.
 */
static void
lzx_flush_block(struct lzx_compressor *c, struct lzx_output_bitstream *os,
		const u8 *block_begin, u32 block_size, u32 seq_idx)
{
	int block_type;

	lzx_build_huffman_codes(c);

	block_type = lzx_choose_verbatim_or_aligned(&c->freqs,
						    &c->codes[c->codes_index]);
	lzx_write_compressed_block(block_begin,
				   block_type,
				   block_size,
				   c->window_order,
				   c->num_main_syms,
				   &c->chosen_sequences[seq_idx],
				   &c->codes[c->codes_index],
				   &c->codes[c->codes_index ^ 1].lens,
				   os);
	c->codes_index ^= 1;
}

/******************************************************************************/
/*                          Block splitting algorithm                         */
/*----------------------------------------------------------------------------*/

/*
 * The problem of block splitting is to decide when it is worthwhile to start a
 * new block with new entropy codes.  There is a theoretically optimal solution:
 * recursively consider every possible block split, considering the exact cost
 * of each block, and choose the minimum cost approach.  But this is far too
 * slow.  Instead, as an approximation, we can count symbols and after every N
 * symbols, compare the expected distribution of symbols based on the previous
 * data with the actual distribution.  If they differ "by enough", then start a
 * new block.
 *
 * As an optimization and heuristic, we don't distinguish between every symbol
 * but rather we combine many symbols into a single "observation type".  For
 * literals we only look at the high bits and low bits, and for matches we only
 * look at whether the match is long or not.  The assumption is that for typical
 * "real" data, places that are good block boundaries will tend to be noticeable
 * based only on changes in these aggregate frequencies, without looking for
 * subtle differences in individual symbols.  For example, a change from ASCII
 * bytes to non-ASCII bytes, or from few matches (generally less compressible)
 * to many matches (generally more compressible), would be easily noticed based
 * on the aggregates.
 *
 * For determining whether the frequency distributions are "different enough" to
 * start a new block, the simply heuristic of splitting when the sum of absolute
 * differences exceeds a constant seems to be good enough.
 *
 * Finally, for an approximation, it is not strictly necessary that the exact
 * symbols being used are considered.  With "near-optimal parsing", for example,
 * the actual symbols that will be used are unknown until after the block
 * boundary is chosen and the block has been optimized.  Since the final choices
 * cannot be used, we can use preliminary "greedy" choices instead.
 */

/* Initialize the block split statistics when starting a new block. */
static void
lzx_init_block_split_stats(struct lzx_block_split_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
}

/* Literal observation.  Heuristic: use the top 2 bits and low 1 bits of the
 * literal, for 8 possible literal observation types.  */
static forceinline void
lzx_observe_literal(struct lzx_block_split_stats *stats, u8 lit)
{
	stats->new_observations[((lit >> 5) & 0x6) | (lit & 1)]++;
	stats->num_new_observations++;
}

/* Match observation.  Heuristic: use one observation type for "short match" and
 * one observation type for "long match".  */
static forceinline void
lzx_observe_match(struct lzx_block_split_stats *stats, unsigned length)
{
	stats->new_observations[NUM_LITERAL_OBSERVATION_TYPES + (length >= 5)]++;
	stats->num_new_observations++;
}

static bool
lzx_should_end_block(struct lzx_block_split_stats *stats)
{
	if (stats->num_observations > 0) {

		/* Note: to avoid slow divisions, we do not divide by
		 * 'num_observations', but rather do all math with the numbers
		 * multiplied by 'num_observations'. */
		u32 total_delta = 0;
		for (int i = 0; i < NUM_OBSERVATION_TYPES; i++) {
			u32 expected = stats->observations[i] *
				       stats->num_new_observations;
			u32 actual = stats->new_observations[i] *
				     stats->num_observations;
			u32 delta = (actual > expected) ? actual - expected :
							  expected - actual;
			total_delta += delta;
		}

		/* Ready to end the block? */
		if (total_delta >=
		    stats->num_new_observations * 7 / 8 * stats->num_observations)
			return true;
	}

	for (int i = 0; i < NUM_OBSERVATION_TYPES; i++) {
		stats->num_observations += stats->new_observations[i];
		stats->observations[i] += stats->new_observations[i];
		stats->new_observations[i] = 0;
	}
	stats->num_new_observations = 0;
	return false;
}

/******************************************************************************/
/*                   Slower ("near-optimal") compression algorithm            */
/*----------------------------------------------------------------------------*/

/*
 * Least-recently-used queue for match offsets.
 *
 * This is represented as a 64-bit integer for efficiency.  There are three
 * offsets of 21 bits each.  Bit 64 is garbage.
 */
PRAGMA_BEGIN_ALIGN(8)
struct lzx_lru_queue {
	u64 R;
} PRAGMA_END_ALIGN(8);

#define LZX_QUEUE_OFFSET_SHIFT	21
#define LZX_QUEUE_OFFSET_MASK	(((u64)1 << LZX_QUEUE_OFFSET_SHIFT) - 1)

#define LZX_QUEUE_R0_SHIFT (0 * LZX_QUEUE_OFFSET_SHIFT)
#define LZX_QUEUE_R1_SHIFT (1 * LZX_QUEUE_OFFSET_SHIFT)
#define LZX_QUEUE_R2_SHIFT (2 * LZX_QUEUE_OFFSET_SHIFT)

#define LZX_QUEUE_R0_MASK (LZX_QUEUE_OFFSET_MASK << LZX_QUEUE_R0_SHIFT)
#define LZX_QUEUE_R1_MASK (LZX_QUEUE_OFFSET_MASK << LZX_QUEUE_R1_SHIFT)
#define LZX_QUEUE_R2_MASK (LZX_QUEUE_OFFSET_MASK << LZX_QUEUE_R2_SHIFT)

#define LZX_QUEUE_INITIALIZER {			\
	((u64)1 << LZX_QUEUE_R0_SHIFT) |	\
	((u64)1 << LZX_QUEUE_R1_SHIFT) |	\
	((u64)1 << LZX_QUEUE_R2_SHIFT) }

static forceinline u64
lzx_lru_queue_R0(struct lzx_lru_queue queue)
{
	return (queue.R >> LZX_QUEUE_R0_SHIFT) & LZX_QUEUE_OFFSET_MASK;
}

static forceinline u64
lzx_lru_queue_R1(struct lzx_lru_queue queue)
{
	return (queue.R >> LZX_QUEUE_R1_SHIFT) & LZX_QUEUE_OFFSET_MASK;
}

static forceinline u64
lzx_lru_queue_R2(struct lzx_lru_queue queue)
{
	return (queue.R >> LZX_QUEUE_R2_SHIFT) & LZX_QUEUE_OFFSET_MASK;
}

/* Push a match offset onto the front (most recently used) end of the queue.  */
static forceinline struct lzx_lru_queue
lzx_lru_queue_push(struct lzx_lru_queue queue, u32 offset)
{
	return (struct lzx_lru_queue) {
		.R = (queue.R << LZX_QUEUE_OFFSET_SHIFT) | offset,
	};
}

/* Swap a match offset to the front of the queue.  */
static forceinline struct lzx_lru_queue
lzx_lru_queue_swap(struct lzx_lru_queue queue, unsigned idx)
{
	unsigned shift = idx * 21;
	const u64 mask = LZX_QUEUE_R0_MASK;
	const u64 mask_high = mask << shift;

	return (struct lzx_lru_queue) {
		(queue.R & ~(mask | mask_high)) |
		((queue.R & mask_high) >> shift) |
		((queue.R & mask) << shift)
	};
}

static forceinline u32
lzx_walk_item_list(struct lzx_compressor *c, u32 block_size, bool is_16_bit,
		   bool record)
{
	struct lzx_sequence *seq =
		&c->chosen_sequences[ARRAY_LEN(c->chosen_sequences) - 1];
	u32 node_idx = block_size;
	u32 litrun_end; /* if record=true: end of the current literal run */

	if (record) {
		/* The last sequence has matchlen 0 */
		seq->litrunlen_and_matchlen = 0;
		litrun_end = node_idx;
	}

	for (;;) {
		u32 item;
		unsigned matchlen;
		u32 adjusted_offset;
		unsigned mainsym;

		/* Tally literals until either a match or the beginning of the
		 * block is reached.  Note: the item in the node at the
		 * beginning of the block (c->optimum_nodes[0]) has all bits
		 * set, causing this loop to end when it is reached. */
		for (;;) {
			item = c->optimum_nodes[node_idx].item;
			if (item & OPTIMUM_LEN_MASK)
				break;
			c->freqs.main[item >> OPTIMUM_OFFSET_SHIFT]++;
			node_idx--;
		}

	#if CONSIDER_GAP_MATCHES
		if (item & OPTIMUM_GAP_MATCH) {
			if (node_idx == 0)
				break;
			/* Tally/record the rep0 match after the gap. */
			matchlen = item & OPTIMUM_LEN_MASK;
			mainsym = lzx_tally_main_and_lensyms(c, matchlen, 0,
							     is_16_bit);
			if (record) {
				seq->litrunlen_and_matchlen |=
					(litrun_end - node_idx) <<
					 SEQ_MATCHLEN_BITS;
				seq--;
				seq->litrunlen_and_matchlen = matchlen;
				seq->adjusted_offset_and_mainsym = mainsym;
				litrun_end = node_idx - matchlen;
			}

			/* Tally the literal in the gap. */
			c->freqs.main[(u8)(item >> OPTIMUM_OFFSET_SHIFT)]++;

			/* Fall through and tally the match before the gap.
			 * (It was temporarily saved in the 'cost' field of the
			 * previous node, which was free to reuse.) */
			item = c->optimum_nodes[--node_idx].cost;
			node_idx -= matchlen;
		}
	#else /* CONSIDER_GAP_MATCHES */
		if (node_idx == 0)
			break;
	#endif /* !CONSIDER_GAP_MATCHES */

		/* Tally/record a match. */
		matchlen = item & OPTIMUM_LEN_MASK;
		adjusted_offset = item >> OPTIMUM_OFFSET_SHIFT;
		// coverity[overrun-call]
		mainsym = lzx_tally_main_and_lensyms(c, matchlen,
						     adjusted_offset,
						     is_16_bit);
		if (adjusted_offset >= LZX_MIN_ALIGNED_OFFSET +
				       LZX_OFFSET_ADJUSTMENT)
			c->freqs.aligned[adjusted_offset &
					 LZX_ALIGNED_OFFSET_BITMASK]++;
		if (record) {
			seq->litrunlen_and_matchlen |=
				(litrun_end - node_idx) << SEQ_MATCHLEN_BITS;
			seq--;
			seq->litrunlen_and_matchlen = matchlen;
			seq->adjusted_offset_and_mainsym =
				(adjusted_offset << SEQ_MAINSYM_BITS) | mainsym;
			litrun_end = node_idx - matchlen;
		}
		node_idx -= matchlen;
	}

	/* Record the literal run length for the first sequence. */
	if (record) {
		seq->litrunlen_and_matchlen |=
			(litrun_end - node_idx) << SEQ_MATCHLEN_BITS;
	}

	/* Return the index in chosen_sequences at which the sequences begin. */
	return seq - &c->chosen_sequences[0];
}

/*
 * Given the minimum-cost path computed through the item graph for the current
 * block, walk the path and count how many of each symbol in each Huffman-coded
 * alphabet would be required to output the items (matches and literals) along
 * the path.
 *
 * Note that the path will be walked backwards (from the end of the block to the
 * beginning of the block), but this doesn't matter because this function only
 * computes frequencies.
 */
static forceinline void
lzx_tally_item_list(struct lzx_compressor *c, u32 block_size, bool is_16_bit)
{
	lzx_walk_item_list(c, block_size, is_16_bit, false);
}

/*
 * Like lzx_tally_item_list(), but this function also generates the list of
 * lzx_sequences for the minimum-cost path and writes it to c->chosen_sequences,
 * ready to be output to the bitstream after the Huffman codes are computed.
 * The lzx_sequences will be written to decreasing memory addresses as the path
 * is walked backwards, which means they will end up in the expected
 * first-to-last order.  The return value is the index in c->chosen_sequences at
 * which the lzx_sequences begin.
 */
static forceinline u32
lzx_record_item_list(struct lzx_compressor *c, u32 block_size, bool is_16_bit)
{
	return lzx_walk_item_list(c, block_size, is_16_bit, true);
}

/*
 * Find an inexpensive path through the graph of possible match/literal choices
 * for the current block.  The nodes of the graph are
 * c->optimum_nodes[0...block_size].  They correspond directly to the bytes in
 * the current block, plus one extra node for end-of-block.  The edges of the
 * graph are matches and literals.  The goal is to find the minimum cost path
 * from 'c->optimum_nodes[0]' to 'c->optimum_nodes[block_size]', given the cost
 * model 'c->costs'.
 *
 * The algorithm works forwards, starting at 'c->optimum_nodes[0]' and
 * proceeding forwards one node at a time.  At each node, a selection of matches
 * (len >= 2), as well as the literal byte (len = 1), is considered.  An item of
 * length 'len' provides a new path to reach the node 'len' bytes later.  If
 * such a path is the lowest cost found so far to reach that later node, then
 * that later node is updated with the new cost and the "arrival" which provided
 * that cost.
 *
 * Note that although this algorithm is based on minimum cost path search, due
 * to various simplifying assumptions the result is not guaranteed to be the
 * true minimum cost, or "optimal", path over the graph of all valid LZX
 * representations of this block.
 *
 * Also, note that because of the presence of the recent offsets queue (which is
 * a type of adaptive state), the algorithm cannot work backwards and compute
 * "cost to end" instead of "cost to beginning".  Furthermore, the way the
 * algorithm handles this adaptive state in the "minimum cost" parse is actually
 * only an approximation.  It's possible for the globally optimal, minimum cost
 * path to contain a prefix, ending at a position, where that path prefix is
 * *not* the minimum cost path to that position.  This can happen if such a path
 * prefix results in a different adaptive state which results in lower costs
 * later.  The algorithm does not solve this problem in general; it only looks
 * one step ahead, with the exception of special consideration for "gap
 * matches".
 */
static forceinline struct lzx_lru_queue
lzx_find_min_cost_path(struct lzx_compressor * const restrict c,
		       const u8 * const restrict block_begin,
		       const u32 block_size,
		       const struct lzx_lru_queue initial_queue,
		       bool is_16_bit)
{
	struct lzx_optimum_node *cur_node = c->optimum_nodes;
	struct lzx_optimum_node * const end_node = cur_node + block_size;
	struct lz_match *cache_ptr = c->match_cache;
	const u8 *in_next = block_begin;
	const u8 * const block_end = block_begin + block_size;

	/*
	 * Instead of storing the match offset LRU queues in the
	 * 'lzx_optimum_node' structures, we save memory (and cache lines) by
	 * storing them in a smaller array.  This works because the algorithm
	 * only requires a limited history of the adaptive state.  Once a given
	 * state is more than LZX_MAX_MATCH_LEN bytes behind the current node
	 * (more if gap match consideration is enabled; we just round up to 512
	 * so it's a power of 2), it is no longer needed.
	 *
	 * The QUEUE() macro finds the queue for the given node.  This macro has
	 * been optimized by taking advantage of 'struct lzx_lru_queue' and
	 * 'struct lzx_optimum_node' both being 8 bytes in size and alignment.
	 */
	struct lzx_lru_queue queues[512] = { 0 };
	STATIC_ASSERT(ARRAY_LEN(queues) >= LZX_MAX_MATCH_LEN + 1);
	STATIC_ASSERT(sizeof(c->optimum_nodes[0]) == sizeof(queues[0]));
#define QUEUE(node) \
	(*(struct lzx_lru_queue *)((char *)queues + \
			((uintptr_t)(node) % (ARRAY_LEN(queues) * sizeof(queues[0])))))
	/*(queues[(uintptr_t)(node) / sizeof(*(node)) % ARRAY_LEN(queues)])*/

#if CONSIDER_GAP_MATCHES
	u32 matches_before_gap[ARRAY_LEN(queues)] = { 0 };
#define MATCH_BEFORE_GAP(node) \
	(matches_before_gap[(uintptr_t)(node) / sizeof(*(node)) % \
			    ARRAY_LEN(matches_before_gap)])
#endif

	/*
	 * Initially, the cost to reach each node is "infinity".
	 *
	 * The first node actually should have cost 0, but "infinity"
	 * (0xFFFFFFFF) works just as well because it immediately overflows.
	 *
	 * The following statement also intentionally sets the 'item' of the
	 * first node, which would otherwise have no meaning, to 0xFFFFFFFF for
	 * use as a sentinel.  See lzx_walk_item_list().
	 */
	memset(c->optimum_nodes, 0xFF,
	       (block_size + 1) * sizeof(c->optimum_nodes[0]));

	/* Initialize the recent offsets queue for the first node. */
	QUEUE(cur_node) = initial_queue;

	do { /* For each node in the block in position order... */

		unsigned num_matches;
		unsigned literal;
		u32 cost;

		/*
		 * A selection of matches for the block was already saved in
		 * memory so that we don't have to run the uncompressed data
		 * through the matchfinder on every optimization pass.  However,
		 * we still search for repeat offset matches during each
		 * optimization pass because we cannot predict the state of the
		 * recent offsets queue.  But as a heuristic, we don't bother
		 * searching for repeat offset matches if the general-purpose
		 * matchfinder failed to find any matches.
		 *
		 * Note that a match of length n at some offset implies there is
		 * also a match of length l for LZX_MIN_MATCH_LEN <= l <= n at
		 * that same offset.  In other words, we don't necessarily need
		 * to use the full length of a match.  The key heuristic that
		 * saves a significicant amount of time is that for each
		 * distinct length, we only consider the smallest offset for
		 * which that length is available.  This heuristic also applies
		 * to repeat offsets, which we order specially: R0 < R1 < R2 <
		 * any explicit offset.  Of course, this heuristic may be
		 * produce suboptimal results because offset slots in LZX are
		 * subject to entropy encoding, but in practice this is a useful
		 * heuristic.
		 */

		num_matches = cache_ptr->length;
		cache_ptr++;

		if (num_matches) {
			struct lz_match *end_matches = cache_ptr + num_matches;
			unsigned next_len = LZX_MIN_MATCH_LEN;
			unsigned max_len = min(block_end - in_next, LZX_MAX_MATCH_LEN);
			const u8 *matchptr;

			/* Consider rep0 matches. */
			matchptr = in_next - lzx_lru_queue_R0(QUEUE(cur_node));
			if (load_u16_unaligned(matchptr) != load_u16_unaligned(in_next))
				goto rep0_done;
			STATIC_ASSERT(LZX_MIN_MATCH_LEN == 2);
			do {
				u32 cost = cur_node->cost +
					   c->costs.match_cost[0][
							next_len - LZX_MIN_MATCH_LEN];
				if (cost <= (cur_node + next_len)->cost) {
					(cur_node + next_len)->cost = cost;
					(cur_node + next_len)->item =
						(0 << OPTIMUM_OFFSET_SHIFT) | next_len;
				}
				if (unlikely(++next_len > max_len)) {
					cache_ptr = end_matches;
					goto done_matches;
				}
			} while (in_next[next_len - 1] == matchptr[next_len - 1]);

		rep0_done:

			/* Consider rep1 matches. */
			matchptr = in_next - lzx_lru_queue_R1(QUEUE(cur_node));
			if (load_u16_unaligned(matchptr) != load_u16_unaligned(in_next))
				goto rep1_done;
			if (matchptr[next_len - 1] != in_next[next_len - 1])
				goto rep1_done;
			for (unsigned len = 2; len < next_len - 1; len++)
				if (matchptr[len] != in_next[len])
					goto rep1_done;
			do {
				u32 cost = cur_node->cost +
					   c->costs.match_cost[1][
							next_len - LZX_MIN_MATCH_LEN];
				if (cost <= (cur_node + next_len)->cost) {
					(cur_node + next_len)->cost = cost;
					(cur_node + next_len)->item =
						(1 << OPTIMUM_OFFSET_SHIFT) | next_len;
				}
				if (unlikely(++next_len > max_len)) {
					cache_ptr = end_matches;
					goto done_matches;
				}
			} while (in_next[next_len - 1] == matchptr[next_len - 1]);

		rep1_done:

			/* Consider rep2 matches. */
			matchptr = in_next - lzx_lru_queue_R2(QUEUE(cur_node));
			if (load_u16_unaligned(matchptr) != load_u16_unaligned(in_next))
				goto rep2_done;
			if (matchptr[next_len - 1] != in_next[next_len - 1])
				goto rep2_done;
			for (unsigned len = 2; len < next_len - 1; len++)
				if (matchptr[len] != in_next[len])
					goto rep2_done;
			do {
				u32 cost = cur_node->cost +
					   c->costs.match_cost[2][
							next_len - LZX_MIN_MATCH_LEN];
				if (cost <= (cur_node + next_len)->cost) {
					(cur_node + next_len)->cost = cost;
					(cur_node + next_len)->item =
						(2 << OPTIMUM_OFFSET_SHIFT) | next_len;
				}
				if (unlikely(++next_len > max_len)) {
					cache_ptr = end_matches;
					goto done_matches;
				}
			} while (in_next[next_len - 1] == matchptr[next_len - 1]);

		rep2_done:

			while (next_len > cache_ptr->length)
				if (++cache_ptr == end_matches)
					goto done_matches;

			/* Consider explicit offset matches. */
			for (;;) {
				u32 offset = cache_ptr->offset;
				u32 adjusted_offset = offset + LZX_OFFSET_ADJUSTMENT;
				unsigned offset_slot = lzx_get_offset_slot(c, adjusted_offset, is_16_bit);
				u32 base_cost = cur_node->cost;
				u32 cost;

			#if CONSIDER_ALIGNED_COSTS
				if (offset >= LZX_MIN_ALIGNED_OFFSET)
					base_cost += c->costs.aligned[adjusted_offset &
								      LZX_ALIGNED_OFFSET_BITMASK];
			#endif
				do {
					cost = base_cost +
					       c->costs.match_cost[offset_slot][
								next_len - LZX_MIN_MATCH_LEN];
					if (cost < (cur_node + next_len)->cost) {
						(cur_node + next_len)->cost = cost;
						(cur_node + next_len)->item =
							(adjusted_offset << OPTIMUM_OFFSET_SHIFT) | next_len;
					}
				} while (++next_len <= cache_ptr->length);

				if (++cache_ptr == end_matches) {
				#if CONSIDER_GAP_MATCHES
					/* Also consider the longest explicit
					 * offset match as a "gap match": match
					 * + lit + rep0. */
					s32 remaining = (block_end - in_next) - (s32)next_len;
					if (likely(remaining >= 2)) {
						const u8 *strptr = in_next + next_len;
						const u8 *matchptr = strptr - offset;
						if (load_u16_unaligned(strptr) == load_u16_unaligned(matchptr)) {
							STATIC_ASSERT(ARRAY_LEN(queues) - LZX_MAX_MATCH_LEN - 2 >= 250);
							STATIC_ASSERT(ARRAY_LEN(queues) == ARRAY_LEN(matches_before_gap));
							unsigned limit = min(remaining,
									     min(ARRAY_LEN(queues) - LZX_MAX_MATCH_LEN - 2,
										 LZX_MAX_MATCH_LEN));
							unsigned rep0_len = lz_extend(strptr, matchptr, 2, limit);
							u8 lit = strptr[-1];
							cost += c->costs.main[lit] +
								c->costs.match_cost[0][rep0_len - LZX_MIN_MATCH_LEN];
							unsigned total_len = next_len + rep0_len;
							if (cost < (cur_node + total_len)->cost) {
								(cur_node + total_len)->cost = cost;
								(cur_node + total_len)->item =
									OPTIMUM_GAP_MATCH |
									((u32)lit << OPTIMUM_OFFSET_SHIFT) |
									rep0_len;
								MATCH_BEFORE_GAP(cur_node + total_len) =
									(adjusted_offset << OPTIMUM_OFFSET_SHIFT) |
									(next_len - 1);
							}
						}
					}
				#endif /* CONSIDER_GAP_MATCHES */
					break;
				}
			}
		}

	done_matches:

		/* Consider coding a literal.

		 * To avoid an extra branch, actually checking the preferability
		 * of coding the literal is integrated into the queue update
		 * code below. */
		literal = *in_next++;
		cost = cur_node->cost + c->costs.main[literal];

		/* Advance to the next position. */
		cur_node++;

		/* The lowest-cost path to the current position is now known.
		 * Finalize the recent offsets queue that results from taking
		 * this lowest-cost path. */

		if (cost <= cur_node->cost) {
			/* Literal: queue remains unchanged. */
			cur_node->cost = cost;
			cur_node->item = (u32)literal << OPTIMUM_OFFSET_SHIFT;
			QUEUE(cur_node) = QUEUE(cur_node - 1);
		} else {
			/* Match: queue update is needed. */
			unsigned len = cur_node->item & OPTIMUM_LEN_MASK;
		#if CONSIDER_GAP_MATCHES
			s32 adjusted_offset = (s32)cur_node->item >> OPTIMUM_OFFSET_SHIFT;
			STATIC_ASSERT(OPTIMUM_GAP_MATCH == 0x80000000); /* assuming sign extension */
		#else
			u32 adjusted_offset = cur_node->item >> OPTIMUM_OFFSET_SHIFT;
		#endif

			if (adjusted_offset >= LZX_NUM_RECENT_OFFSETS) {
				/* Explicit offset match: insert offset at front. */
				QUEUE(cur_node) =
					lzx_lru_queue_push(QUEUE(cur_node - len),
							   adjusted_offset - LZX_OFFSET_ADJUSTMENT);
			}
		#if CONSIDER_GAP_MATCHES
			else if (adjusted_offset < 0) {
				/* "Gap match": Explicit offset match, then a
				 * literal, then rep0 match.  Save the explicit
				 * offset match information in the cost field of
				 * the previous node, which isn't needed
				 * anymore.  Then insert the offset at the front
				 * of the queue. */
				u32 match_before_gap = MATCH_BEFORE_GAP(cur_node);
				(cur_node - 1)->cost = match_before_gap;
				QUEUE(cur_node) =
					lzx_lru_queue_push(QUEUE(cur_node - len - 1 -
								 (match_before_gap & OPTIMUM_LEN_MASK)),
							   (match_before_gap >> OPTIMUM_OFFSET_SHIFT) -
							   LZX_OFFSET_ADJUSTMENT);
			}
		#endif
			else {
				/* Repeat offset match: swap offset to front. */
				QUEUE(cur_node) =
					lzx_lru_queue_swap(QUEUE(cur_node - len),
							   adjusted_offset);
			}
		}
	} while (cur_node != end_node);

	/* Return the recent offsets queue at the end of the path. */
	return QUEUE(cur_node);
}

/*
 * Given the costs for the main and length codewords (c->costs.main and
 * c->costs.len), initialize the match cost array (c->costs.match_cost) which
 * directly provides the cost of every possible (length, offset slot) pair.
 */
static void
lzx_compute_match_costs(struct lzx_compressor *c)
{
	unsigned num_offset_slots = (c->num_main_syms - LZX_NUM_CHARS) /
					LZX_NUM_LEN_HEADERS;
	struct lzx_costs *costs = &c->costs;
	unsigned main_symbol = LZX_NUM_CHARS;

	for (unsigned offset_slot = 0; offset_slot < num_offset_slots;
	     offset_slot++)
	{
		u32 extra_cost = lzx_extra_offset_bits[offset_slot] * BIT_COST;
		unsigned i;

	#if CONSIDER_ALIGNED_COSTS
		if (offset_slot >= LZX_MIN_ALIGNED_OFFSET_SLOT)
			extra_cost -= LZX_NUM_ALIGNED_OFFSET_BITS * BIT_COST;
	#endif

		for (i = 0; i < LZX_NUM_PRIMARY_LENS; i++) {
			costs->match_cost[offset_slot][i] =
				costs->main[main_symbol++] + extra_cost;
		}

		extra_cost += costs->main[main_symbol++];

		for (; i < LZX_NUM_LENS; i++) {
			costs->match_cost[offset_slot][i] =
				costs->len[i - LZX_NUM_PRIMARY_LENS] +
				extra_cost;
		}
	}
}

/*
 * Fast approximation for log2f(x).  This is not as accurate as the standard C
 * version.  It does not need to be perfectly accurate because it is only used
 * for estimating symbol costs, which is very approximate anyway.
 */
static float
log2f_fast(float x)
{
        union {
		float f;
		s32 i;
	} u = { .f = x };

	/* Extract the exponent and subtract 127 to remove the bias.  This gives
	 * the integer part of the result. */
        float res = ((u.i >> 23) & 0xFF) - 127;

	/* Set the exponent to 0 (plus bias of 127).  This transforms the number
	 * to the range [1, 2) while retaining the same mantissa. */
	u.i = (u.i & ~(0xFF << 23)) | (127 << 23);

	/*
	 * Approximate the log2 of the transformed number using a degree 2
	 * interpolating polynomial for log2(x) over the interval [1, 2).  Then
	 * add this to the extracted exponent to produce the final approximation
	 * of log2(x).
	 *
	 * The coefficients of the interpolating polynomial used here were found
	 * using the script tools/log2_interpolation.r.
	 */
        return res - 1.653124006f + u.f * (1.9941812f - u.f * 0.3347490189f);

}

/*
 * Return the estimated cost of a symbol which has been estimated to have the
 * given probability.
 */
static u32
lzx_cost_for_probability(float prob)
{
	/*
	 * The basic formula is:
	 *
	 *	entropy = -log2(probability)
	 *
	 * Use this to get the cost in fractional bits.  Then multiply by our
	 * scaling factor of BIT_COST and convert to an integer.
	 *
	 * In addition, the minimum cost is BIT_COST (one bit) because the
	 * entropy coding method will be Huffman codes.
	 *
	 * Careful: even though 'prob' should be <= 1.0, 'log2f_fast(prob)' may
	 * be positive due to inaccuracy in our log2 approximation.  Therefore,
	 * we cannot, in general, assume the computed cost is non-negative, and
	 * we should make sure negative costs get rounded up correctly.
	 */
	s32 cost = -log2f_fast(prob) * BIT_COST;
	return max(cost, BIT_COST);
}

/*
 * Mapping: number of used literals => heuristic probability of a literal times
 * 6870.  Generated by running this R command:
 *
 *	cat(paste(round(6870*2^-((304+(0:256))/64)), collapse=", "))
 */
static const u8 literal_scaled_probs[257] = {
	255, 253, 250, 247, 244, 242, 239, 237, 234, 232, 229, 227, 224, 222,
	219, 217, 215, 212, 210, 208, 206, 203, 201, 199, 197, 195, 193, 191,
	189, 186, 184, 182, 181, 179, 177, 175, 173, 171, 169, 167, 166, 164,
	162, 160, 159, 157, 155, 153, 152, 150, 149, 147, 145, 144, 142, 141,
	139, 138, 136, 135, 133, 132, 130, 129, 128, 126, 125, 124, 122, 121,
	120, 118, 117, 116, 115, 113, 112, 111, 110, 109, 107, 106, 105, 104,
	103, 102, 101, 100, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86,
	86, 85, 84, 83, 82, 81, 80, 79, 78, 78, 77, 76, 75, 74, 73, 73, 72, 71,
	70, 70, 69, 68, 67, 67, 66, 65, 65, 64, 63, 62, 62, 61, 60, 60, 59, 59,
	58, 57, 57, 56, 55, 55, 54, 54, 53, 53, 52, 51, 51, 50, 50, 49, 49, 48,
	48, 47, 47, 46, 46, 45, 45, 44, 44, 43, 43, 42, 42, 41, 41, 40, 40, 40,
	39, 39, 38, 38, 38, 37, 37, 36, 36, 36, 35, 35, 34, 34, 34, 33, 33, 33,
	32, 32, 32, 31, 31, 31, 30, 30, 30, 29, 29, 29, 28, 28, 28, 27, 27, 27,
	27, 26, 26, 26, 25, 25, 25, 25, 24, 24, 24, 24, 23, 23, 23, 23, 22, 22,
	22, 22, 21, 21, 21, 21, 20, 20, 20, 20, 20, 19, 19, 19, 19, 19, 18, 18,
	18, 18, 18, 17, 17, 17, 17, 17, 16, 16, 16, 16
};

/*
 * Mapping: length symbol => default cost of that symbol.  This is derived from
 * sample data but has been slightly edited to add more bias towards the
 * shortest lengths, which are the most common.
 */
static const u16 lzx_default_len_costs[LZX_LENCODE_NUM_SYMBOLS] = {
	300, 310, 320, 330, 360, 396, 399, 416, 451, 448, 463, 466, 505, 492,
	503, 514, 547, 531, 566, 561, 589, 563, 592, 586, 623, 602, 639, 627,
	659, 643, 657, 650, 685, 662, 661, 672, 685, 686, 696, 680, 657, 682,
	666, 699, 674, 699, 679, 709, 688, 712, 692, 714, 694, 716, 698, 712,
	706, 727, 714, 727, 713, 723, 712, 718, 719, 719, 720, 735, 725, 735,
	728, 740, 727, 739, 727, 742, 716, 733, 733, 740, 738, 746, 737, 747,
	738, 745, 736, 748, 742, 749, 745, 749, 743, 748, 741, 752, 745, 752,
	747, 750, 747, 752, 748, 753, 750, 752, 753, 753, 749, 744, 752, 755,
	753, 756, 745, 748, 746, 745, 723, 757, 755, 758, 755, 758, 752, 757,
	754, 757, 755, 759, 755, 758, 753, 755, 755, 758, 757, 761, 755, 750,
	758, 759, 759, 760, 758, 751, 757, 757, 759, 759, 758, 759, 758, 761,
	750, 761, 758, 760, 759, 761, 758, 761, 760, 752, 759, 760, 759, 759,
	757, 762, 760, 761, 761, 748, 761, 760, 762, 763, 752, 762, 762, 763,
	762, 762, 763, 763, 762, 763, 762, 763, 762, 763, 763, 764, 763, 762,
	763, 762, 762, 762, 764, 764, 763, 764, 763, 763, 763, 762, 763, 763,
	762, 764, 764, 763, 762, 763, 763, 763, 763, 762, 764, 763, 762, 764,
	764, 763, 763, 765, 764, 764, 762, 763, 764, 765, 763, 764, 763, 764,
	762, 764, 764, 754, 763, 764, 763, 763, 762, 763, 584,
};

/* Set default costs to bootstrap the iterative optimization algorithm. */
static void
lzx_set_default_costs(struct lzx_compressor *c)
{
	unsigned i;
	u32 num_literals = 0;
	u32 num_used_literals = 0;
	float inv_num_matches = 1.0f / c->freqs.main[LZX_NUM_CHARS];
	float inv_num_items;
	float prob_match = 1.0f;
	u32 match_cost;
	float base_literal_prob;

	/* Some numbers here have been hardcoded to assume a bit cost of 64. */
	STATIC_ASSERT(BIT_COST == 64);

	/* Estimate the number of literals that will used.  'num_literals' is
	 * the total number, whereas 'num_used_literals' is the number of
	 * distinct symbols. */
	for (i = 0; i < LZX_NUM_CHARS; i++) {
		num_literals += c->freqs.main[i];
		num_used_literals += (c->freqs.main[i] != 0);
	}

	/* Note: all match headers were tallied as symbol 'LZX_NUM_CHARS'.  We
	 * don't attempt to estimate which ones will be used. */

	inv_num_items = 1.0f / (num_literals + c->freqs.main[LZX_NUM_CHARS]);
	base_literal_prob = literal_scaled_probs[num_used_literals] *
			    (1.0f / 6870.0f);

	/* Literal costs.  We use two different methods to compute the
	 * probability of each literal and mix together their results. */
	for (i = 0; i < LZX_NUM_CHARS; i++) {
		u32 freq = c->freqs.main[i];
		if (freq != 0) {
			float prob = 0.5f * ((freq * inv_num_items) +
					     base_literal_prob);
			c->costs.main[i] = lzx_cost_for_probability(prob);
			prob_match -= prob;
		} else {
			c->costs.main[i] = 11 * BIT_COST;
		}
	}

	/* Match header costs.  We just assume that all match headers are
	 * equally probable, but we do take into account the relative cost of a
	 * match header vs. a literal depending on how common matches are
	 * expected to be vs. literals. */
	prob_match = max(prob_match, 0.15f);
	match_cost = lzx_cost_for_probability(prob_match / (c->num_main_syms -
							    LZX_NUM_CHARS));
	for (; i < c->num_main_syms; i++)
		c->costs.main[i] = match_cost;

	/* Length symbol costs.  These are just set to fixed values which
	 * reflect the fact the smallest lengths are typically the most common,
	 * and therefore are typically the cheapest. */
	for (i = 0; i < LZX_LENCODE_NUM_SYMBOLS; i++)
		c->costs.len[i] = lzx_default_len_costs[i];

#if CONSIDER_ALIGNED_COSTS
	/* Aligned offset symbol costs.  These are derived from the estimated
	 * probability of each aligned offset symbol. */
	for (i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
		/* We intentionally tallied the frequencies in the wrong slots,
		 * not accounting for LZX_OFFSET_ADJUSTMENT, since doing the
		 * fixup here is faster: a constant 8 subtractions here vs. one
		 * addition for every match. */
		unsigned j = (i - LZX_OFFSET_ADJUSTMENT) & LZX_ALIGNED_OFFSET_BITMASK;
		if (c->freqs.aligned[j] != 0) {
			float prob = c->freqs.aligned[j] * inv_num_matches;
			c->costs.aligned[i] = lzx_cost_for_probability(prob);
		} else {
			c->costs.aligned[i] =
				(2 * LZX_NUM_ALIGNED_OFFSET_BITS) * BIT_COST;
		}
	}
#endif
}

/* Update the current cost model to reflect the computed Huffman codes.  */
static void
lzx_set_costs_from_codes(struct lzx_compressor *c)
{
	unsigned i;
	const struct lzx_lens *lens = &c->codes[c->codes_index].lens;

	for (i = 0; i < c->num_main_syms; i++) {
		c->costs.main[i] = (lens->main[i] ? lens->main[i] :
				    MAIN_CODEWORD_LIMIT) * BIT_COST;
	}

	for (i = 0; i < LZX_LENCODE_NUM_SYMBOLS; i++) {
		c->costs.len[i] = (lens->len[i] ? lens->len[i] :
				   LENGTH_CODEWORD_LIMIT) * BIT_COST;
	}

#if CONSIDER_ALIGNED_COSTS
	for (i = 0; i < LZX_ALIGNEDCODE_NUM_SYMBOLS; i++) {
		c->costs.aligned[i] = (lens->aligned[i] ? lens->aligned[i] :
				       ALIGNED_CODEWORD_LIMIT) * BIT_COST;
	}
#endif
}

/*
 * Choose a "near-optimal" literal/match sequence to use for the current block,
 * then flush the block.  Because the cost of each Huffman symbol is unknown
 * until the Huffman codes have been built and the Huffman codes themselves
 * depend on the symbol frequencies, this uses an iterative optimization
 * algorithm to approximate an optimal solution.  The first optimization pass
 * for the block uses default costs; additional passes use costs derived from
 * the Huffman codes computed in the previous pass.
 */
static forceinline struct lzx_lru_queue
lzx_optimize_and_flush_block(struct lzx_compressor * const restrict c,
			     struct lzx_output_bitstream * const restrict os,
			     const u8 * const restrict block_begin,
			     const u32 block_size,
			     const struct lzx_lru_queue initial_queue,
			     bool is_16_bit)
{
	unsigned num_passes_remaining = c->num_optim_passes;
	struct lzx_lru_queue new_queue;
	u32 seq_idx;

	lzx_set_default_costs(c);

	for (;;) {
		lzx_compute_match_costs(c);
		new_queue = lzx_find_min_cost_path(c, block_begin, block_size,
						   initial_queue, is_16_bit);

		if (--num_passes_remaining == 0)
			break;

		/* At least one optimization pass remains.  Update the costs. */
		lzx_reset_symbol_frequencies(c);
		lzx_tally_item_list(c, block_size, is_16_bit);
		lzx_build_huffman_codes(c);
		lzx_set_costs_from_codes(c);
	}

	/* Done optimizing.  Generate the sequence list and flush the block. */
	lzx_reset_symbol_frequencies(c);
	seq_idx = lzx_record_item_list(c, block_size, is_16_bit);
	lzx_flush_block(c, os, block_begin, block_size, seq_idx);
	return new_queue;
}

/*
 * This is the "near-optimal" LZX compressor.
 *
 * For each block, it performs a relatively thorough graph search to find an
 * inexpensive (in terms of compressed size) way to output the block.
 *
 * Note: there are actually many things this algorithm leaves on the table in
 * terms of compression ratio.  So although it may be "near-optimal", it is
 * certainly not "optimal".  The goal is not to produce the optimal compression
 * ratio, which for LZX is probably impossible within any practical amount of
 * time, but rather to produce a compression ratio significantly better than a
 * simpler "greedy" or "lazy" parse while still being relatively fast.
 */
static forceinline void
lzx_compress_near_optimal(struct lzx_compressor * restrict c,
			  const u8 * const restrict in_begin, size_t in_nbytes,
			  struct lzx_output_bitstream * restrict os,
			  bool is_16_bit)
{
	const u8 *	 in_next = in_begin;
	const u8 * const in_end  = in_begin + in_nbytes;
	u32 max_len = LZX_MAX_MATCH_LEN;
	u32 nice_len = min(c->nice_match_length, max_len);
	u32 next_hashes[2] = {0, 0};
	struct lzx_lru_queue queue = LZX_QUEUE_INITIALIZER;

	/* Initialize the matchfinder. */
	CALL_BT_MF(is_16_bit, c, bt_matchfinder_init);

	do {
		/* Starting a new block */

		const u8 * const in_block_begin = in_next;
		const u8 * const in_max_block_end =
			in_next + min(SOFT_MAX_BLOCK_SIZE, in_end - in_next);
		struct lz_match *cache_ptr = c->match_cache;
		const u8 *next_search_pos = in_next;
		const u8 *next_observation = in_next;
		const u8 *next_pause_point =
			min(in_next + min(MIN_BLOCK_SIZE,
					  in_max_block_end - in_next),
			    in_max_block_end - min(LZX_MAX_MATCH_LEN - 1,
						   in_max_block_end - in_next));

		lzx_init_block_split_stats(&c->split_stats);
		lzx_reset_symbol_frequencies(c);

		if (in_next >= next_pause_point)
			goto pause;

		/*
		 * Run the input buffer through the matchfinder, caching the
		 * matches, until we decide to end the block.
		 *
		 * For a tighter matchfinding loop, we compute a "pause point",
		 * which is the next position at which we may need to check
		 * whether to end the block or to decrease max_len.  We then
		 * only do these extra checks upon reaching the pause point.
		 */
	resume_matchfinding:
		do {
			if (in_next >= next_search_pos) {
				/* Search for matches at this position. */
				struct lz_match *lz_matchptr;
				u32 best_len;

				lz_matchptr = CALL_BT_MF(is_16_bit, c,
							 bt_matchfinder_get_matches,
							 in_begin,
							 in_next - in_begin,
							 max_len,
							 nice_len,
							 c->max_search_depth,
							 next_hashes,
							 &best_len,
							 cache_ptr + 1);
				cache_ptr->length = lz_matchptr - (cache_ptr + 1);
				cache_ptr = lz_matchptr;

				/* Accumulate literal/match statistics for block
				 * splitting and for generating the initial cost
				 * model. */
				if (in_next >= next_observation) {
					best_len = cache_ptr[-1].length;
					if (best_len >= 3) {
						/* Match (len >= 3) */

						/*
						 * Note: for performance reasons this has
						 * been simplified significantly:
						 *
						 * - We wait until later to account for
						 *   LZX_OFFSET_ADJUSTMENT.
						 * - We don't account for repeat offsets.
						 * - We don't account for different match headers.
						 */
						c->freqs.aligned[cache_ptr[-1].offset &
							LZX_ALIGNED_OFFSET_BITMASK]++;
						c->freqs.main[LZX_NUM_CHARS]++;

						lzx_observe_match(&c->split_stats, best_len);
						next_observation = in_next + best_len;
					} else {
						/* Literal */
						c->freqs.main[*in_next]++;
						lzx_observe_literal(&c->split_stats, *in_next);
						next_observation = in_next + 1;
					}
				}

				/*
				 * If there was a very long match found, then
				 * don't cache any matches for the bytes covered
				 * by that match.  This avoids degenerate
				 * behavior when compressing highly redundant
				 * data, where the number of matches can be very
				 * large.
				 *
				 * This heuristic doesn't actually hurt the
				 * compression ratio *too* much.  If there's a
				 * long match, then the data must be highly
				 * compressible, so it doesn't matter as much
				 * what we do.
				 */
				if (best_len >= nice_len)
					next_search_pos = in_next + best_len;
			} else {
				/* Don't search for matches at this position. */
				CALL_BT_MF(is_16_bit, c,
					   bt_matchfinder_skip_byte,
					   in_begin,
					   in_next - in_begin,
					   nice_len,
					   c->max_search_depth,
					   next_hashes);
				cache_ptr->length = 0;
				cache_ptr++;
			}
		} while (++in_next < next_pause_point &&
			 likely(cache_ptr < &c->match_cache[CACHE_LENGTH]));

	pause:

		/* Adjust max_len and nice_len if we're nearing the end of the
		 * input buffer.  In addition, if we are so close to the end of
		 * the input buffer that there cannot be any more matches, then
		 * just advance through the last few positions and record no
		 * matches. */
		if (unlikely(max_len > in_end - in_next)) {
			max_len = in_end - in_next;
			nice_len = min(max_len, nice_len);
			if (max_len < BT_MATCHFINDER_REQUIRED_NBYTES) {
				while (in_next != in_end) {
					cache_ptr->length = 0;
					cache_ptr++;
					in_next++;
				}
			}
		}

		/* End the block if the match cache may overflow. */
		if (unlikely(cache_ptr >= &c->match_cache[CACHE_LENGTH]))
			goto end_block;

		/* End the block if the soft maximum size has been reached. */
		if (in_next >= in_max_block_end)
			goto end_block;

		/* End the block if the block splitting algorithm thinks this is
		 * a good place to do so. */
		if (c->split_stats.num_new_observations >=
				NUM_OBSERVATIONS_PER_BLOCK_CHECK &&
		    in_max_block_end - in_next >= MIN_BLOCK_SIZE &&
		    lzx_should_end_block(&c->split_stats))
			goto end_block;

		/* It's not time to end the block yet.  Compute the next pause
		 * point and resume matchfinding. */
		next_pause_point =
			min(in_next + min(NUM_OBSERVATIONS_PER_BLOCK_CHECK * 2 -
					    c->split_stats.num_new_observations,
					  in_max_block_end - in_next),
			    in_max_block_end - min(LZX_MAX_MATCH_LEN - 1,
						   in_max_block_end - in_next));
		goto resume_matchfinding;

	end_block:
		/* We've decided on a block boundary and cached matches.  Now
		 * choose a match/literal sequence and flush the block. */
		queue = lzx_optimize_and_flush_block(c, os, in_block_begin,
						     in_next - in_block_begin,
						     queue, is_16_bit);
	} while (in_next != in_end);
}

static void
lzx_compress_near_optimal_16(struct lzx_compressor *c, const u8 *in,
			     size_t in_nbytes, struct lzx_output_bitstream *os)
{
	lzx_compress_near_optimal(c, in, in_nbytes, os, true);
}

static void
lzx_compress_near_optimal_32(struct lzx_compressor *c, const u8 *in,
			     size_t in_nbytes, struct lzx_output_bitstream *os)
{
	lzx_compress_near_optimal(c, in, in_nbytes, os, false);
}

/******************************************************************************/
/*                     Faster ("lazy") compression algorithm                  */
/*----------------------------------------------------------------------------*/

/*
 * Called when the compressor chooses to use a literal.  This tallies the
 * Huffman symbol for the literal, increments the current literal run length,
 * and "observes" the literal for the block split statistics.
 */
static forceinline void
lzx_choose_literal(struct lzx_compressor *c, unsigned literal, u32 *litrunlen_p)
{
	lzx_observe_literal(&c->split_stats, literal);
	c->freqs.main[literal]++;
	++*litrunlen_p;
}

/*
 * Called when the compressor chooses to use a match.  This tallies the Huffman
 * symbol(s) for a match, saves the match data and the length of the preceding
 * literal run, updates the recent offsets queue, and "observes" the match for
 * the block split statistics.
 */
static forceinline void
lzx_choose_match(struct lzx_compressor *c, unsigned length, u32 adjusted_offset,
		 u32 recent_offsets[LZX_NUM_RECENT_OFFSETS], bool is_16_bit,
		 u32 *litrunlen_p, struct lzx_sequence **next_seq_p)
{
	struct lzx_sequence *next_seq = *next_seq_p;
	unsigned mainsym;

	lzx_observe_match(&c->split_stats, length);

	mainsym = lzx_tally_main_and_lensyms(c, length, adjusted_offset,
					     is_16_bit);
	next_seq->litrunlen_and_matchlen =
		(*litrunlen_p << SEQ_MATCHLEN_BITS) | length;
	next_seq->adjusted_offset_and_mainsym =
		(adjusted_offset << SEQ_MAINSYM_BITS) | mainsym;

	/* Update the recent offsets queue. */
	if (adjusted_offset < LZX_NUM_RECENT_OFFSETS) {
		/* Repeat offset match. */
		swap(recent_offsets[0], recent_offsets[adjusted_offset]);
	} else {
		/* Explicit offset match. */

		/* Tally the aligned offset symbol if needed. */
		if (adjusted_offset >= LZX_MIN_ALIGNED_OFFSET + LZX_OFFSET_ADJUSTMENT)
			c->freqs.aligned[adjusted_offset & LZX_ALIGNED_OFFSET_BITMASK]++;

		recent_offsets[2] = recent_offsets[1];
		recent_offsets[1] = recent_offsets[0];
		recent_offsets[0] = adjusted_offset - LZX_OFFSET_ADJUSTMENT;
	}

	/* Reset the literal run length and advance to the next sequence. */
	*next_seq_p = next_seq + 1;
	*litrunlen_p = 0;
}

/*
 * Called when the compressor ends a block.  This finshes the last lzx_sequence,
 * which is just a literal run with no following match.  This literal run might
 * be empty.
 */
static forceinline void
lzx_finish_sequence(struct lzx_sequence *last_seq, u32 litrunlen)
{
	last_seq->litrunlen_and_matchlen = litrunlen << SEQ_MATCHLEN_BITS;
}

/*
 * Find the longest repeat offset match with the current position.  If a match
 * is found, return its length and set *best_rep_idx_ret to the index of its
 * offset in @recent_offsets.  Otherwise, return 0.
 *
 * Don't bother with length 2 matches; consider matches of length >= 3 only.
 * Also assume that max_len >= 3.
 */
static unsigned
lzx_find_longest_repeat_offset_match(const u8 * const in_next,
				     const u32 recent_offsets[],
				     const unsigned max_len,
				     unsigned *best_rep_idx_ret)
{
	STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3); /* loop is unrolled */

	const u32 seq3 = load_u24_unaligned(in_next);
	const u8 *matchptr;
	unsigned best_rep_len = 0;
	unsigned best_rep_idx = 0;
	unsigned rep_len;

	/* Check for rep0 match (most recent offset) */
	matchptr = in_next - recent_offsets[0];
	if (load_u24_unaligned(matchptr) == seq3)
		best_rep_len = lz_extend(in_next, matchptr, 3, max_len);

	/* Check for rep1 match (second most recent offset) */
	matchptr = in_next - recent_offsets[1];
	if (load_u24_unaligned(matchptr) == seq3) {
		rep_len = lz_extend(in_next, matchptr, 3, max_len);
		if (rep_len > best_rep_len) {
			best_rep_len = rep_len;
			best_rep_idx = 1;
		}
	}

	/* Check for rep2 match (third most recent offset) */
	matchptr = in_next - recent_offsets[2];
	if (load_u24_unaligned(matchptr) == seq3) {
		rep_len = lz_extend(in_next, matchptr, 3, max_len);
		if (rep_len > best_rep_len) {
			best_rep_len = rep_len;
			best_rep_idx = 2;
		}
	}

	*best_rep_idx_ret = best_rep_idx;
	return best_rep_len;
}

/*
 * Fast heuristic scoring for lazy parsing: how "good" is this match?
 * This is mainly determined by the length: longer matches are better.
 * However, we also give a bonus to close (small offset) matches and to repeat
 * offset matches, since those require fewer bits to encode.
 */

static forceinline unsigned
lzx_explicit_offset_match_score(unsigned len, u32 adjusted_offset)
{
	unsigned score = len;

	if (adjusted_offset < 4096)
		score++;
	if (adjusted_offset < 256)
		score++;

	return score;
}

static forceinline unsigned
lzx_repeat_offset_match_score(unsigned rep_len, unsigned rep_idx)
{
	return rep_len + 3;
}

/*
 * This is the "lazy" LZX compressor.  The basic idea is that before it chooses
 * a match, it checks to see if there's a longer match at the next position.  If
 * yes, it chooses a literal and continues to the next position.  If no, it
 * chooses the match.
 *
 * Some additional heuristics are used as well.  Repeat offset matches are
 * considered favorably and sometimes are chosen immediately.  In addition, long
 * matches (at least "nice_len" bytes) are chosen immediately as well.  Finally,
 * when we decide whether a match is "better" than another, we take the offset
 * into consideration as well as the length.
 */
static forceinline void
lzx_compress_lazy(struct lzx_compressor * restrict c,
		  const u8 * const restrict in_begin, size_t in_nbytes,
		  struct lzx_output_bitstream * restrict os, bool is_16_bit)
{
	const u8 *	 in_next = in_begin;
	const u8 * const in_end  = in_begin + in_nbytes;
	unsigned max_len = LZX_MAX_MATCH_LEN;
	unsigned nice_len = min(c->nice_match_length, max_len);
	STATIC_ASSERT(LZX_NUM_RECENT_OFFSETS == 3);
	u32 recent_offsets[LZX_NUM_RECENT_OFFSETS] = {1, 1, 1};
	u32 next_hashes[2] = {0, 0};

	/* Initialize the matchfinder. */
	CALL_HC_MF(is_16_bit, c, hc_matchfinder_init);

	do {
		/* Starting a new block */

		const u8 * const in_block_begin = in_next;
		const u8 * const in_max_block_end =
			in_next + min(SOFT_MAX_BLOCK_SIZE, in_end - in_next);
		struct lzx_sequence *next_seq = c->chosen_sequences;
		u32 litrunlen = 0;
		unsigned cur_len;
		u32 cur_offset;
		u32 cur_adjusted_offset;
		unsigned cur_score;
		unsigned next_len;
		u32 next_offset;
		u32 next_adjusted_offset;
		unsigned next_score;
		unsigned best_rep_len;
		unsigned best_rep_idx;
		unsigned rep_score;
		unsigned skip_len;

		lzx_reset_symbol_frequencies(c);
		lzx_init_block_split_stats(&c->split_stats);

		do {
			/* Adjust max_len and nice_len if we're nearing the end
			 * of the input buffer. */
			if (unlikely(max_len > in_end - in_next)) {
				max_len = in_end - in_next;
				nice_len = min(max_len, nice_len);
			}

			/* Find the longest match (subject to the
			 * max_search_depth cutoff parameter) with the current
			 * position.  Don't bother with length 2 matches; only
			 * look for matches of length >= 3. */
			cur_len = CALL_HC_MF(is_16_bit, c,
					     hc_matchfinder_longest_match,
					     in_begin,
					     in_next,
					     2,
					     max_len,
					     nice_len,
					     c->max_search_depth,
					     next_hashes,
					     &cur_offset);

			/* If there was no match found, or the only match found
			 * was a distant short match, then choose a literal. */
			if (cur_len < 3 ||
			    (cur_len == 3 &&
			     cur_offset >= 8192 - LZX_OFFSET_ADJUSTMENT &&
			     cur_offset != recent_offsets[0] &&
			     cur_offset != recent_offsets[1] &&
			     cur_offset != recent_offsets[2]))
			{
				lzx_choose_literal(c, *in_next, &litrunlen);
				in_next++;
				continue;
			}

			/* Heuristic: if this match has the most recent offset,
			 * then go ahead and choose it as a rep0 match. */
			if (cur_offset == recent_offsets[0]) {
				in_next++;
				skip_len = cur_len - 1;
				cur_adjusted_offset = 0;
				goto choose_cur_match;
			}

			/* Compute the longest match's score as an explicit
			 * offset match. */
			cur_adjusted_offset = cur_offset + LZX_OFFSET_ADJUSTMENT;
			cur_score = lzx_explicit_offset_match_score(cur_len, cur_adjusted_offset);

			/* Find the longest repeat offset match at this
			 * position.  If we find one and it's "better" than the
			 * explicit offset match we found, then go ahead and
			 * choose the repeat offset match immediately. */
			best_rep_len = lzx_find_longest_repeat_offset_match(in_next,
									    recent_offsets,
									    max_len,
									    &best_rep_idx);
			in_next++;

			if (best_rep_len != 0 &&
			    (rep_score = lzx_repeat_offset_match_score(best_rep_len,
								       best_rep_idx)) >= cur_score)
			{
				cur_len = best_rep_len;
				cur_adjusted_offset = best_rep_idx;
				skip_len = best_rep_len - 1;
				goto choose_cur_match;
			}

		have_cur_match:
			/*
			 * We have a match at the current position.  If the
			 * match is very long, then choose it immediately.
			 * Otherwise, see if there's a better match at the next
			 * position.
			 */

			if (cur_len >= nice_len) {
				skip_len = cur_len - 1;
				goto choose_cur_match;
			}

			if (unlikely(max_len > in_end - in_next)) {
				max_len = in_end - in_next;
				nice_len = min(max_len, nice_len);
			}

			next_len = CALL_HC_MF(is_16_bit, c,
					      hc_matchfinder_longest_match,
					      in_begin,
					      in_next,
					      cur_len - 2,
					      max_len,
					      nice_len,
					      c->max_search_depth / 2,
					      next_hashes,
					      &next_offset);

			if (next_len <= cur_len - 2) {
				/* No potentially better match was found. */
				in_next++;
				skip_len = cur_len - 2;
				goto choose_cur_match;
			}

			next_adjusted_offset = next_offset + LZX_OFFSET_ADJUSTMENT;
			next_score = lzx_explicit_offset_match_score(next_len, next_adjusted_offset);

			best_rep_len = lzx_find_longest_repeat_offset_match(in_next,
									    recent_offsets,
									    max_len,
									    &best_rep_idx);
			in_next++;

			if (best_rep_len != 0 &&
			    (rep_score = lzx_repeat_offset_match_score(best_rep_len,
								       best_rep_idx)) >= next_score)
			{

				if (rep_score > cur_score) {
					/* The next match is better, and it's a
					 * repeat offset match. */
					lzx_choose_literal(c, *(in_next - 2),
							   &litrunlen);
					cur_len = best_rep_len;
					cur_adjusted_offset = best_rep_idx;
					skip_len = cur_len - 1;
					goto choose_cur_match;
				}
			} else {
				if (next_score > cur_score) {
					/* The next match is better, and it's an
					 * explicit offset match. */
					lzx_choose_literal(c, *(in_next - 2),
							   &litrunlen);
					cur_len = next_len;
					cur_adjusted_offset = next_adjusted_offset;
					cur_score = next_score;
					goto have_cur_match;
				}
			}

			/* The original match was better; choose it. */
			skip_len = cur_len - 2;

		choose_cur_match:
			/* Choose a match and have the matchfinder skip over its
			 * remaining bytes. */
			lzx_choose_match(c, cur_len, cur_adjusted_offset,
					 recent_offsets, is_16_bit,
					 &litrunlen, &next_seq);
			CALL_HC_MF(is_16_bit, c,
				   hc_matchfinder_skip_bytes,
				   in_begin,
				   in_next,
				   in_end,
				   skip_len,
				   next_hashes);
			in_next += skip_len;

			/* Keep going until it's time to end the block. */
		} while (in_next < in_max_block_end &&
			 !(c->split_stats.num_new_observations >=
					NUM_OBSERVATIONS_PER_BLOCK_CHECK &&
			   in_next - in_block_begin >= MIN_BLOCK_SIZE &&
			   in_end - in_next >= MIN_BLOCK_SIZE &&
			   lzx_should_end_block(&c->split_stats)));

		/* Flush the block. */
		lzx_finish_sequence(next_seq, litrunlen);
		lzx_flush_block(c, os, in_block_begin, in_next - in_block_begin, 0);

		/* Keep going until we've reached the end of the input buffer. */
	} while (in_next != in_end);
}

static void
lzx_compress_lazy_16(struct lzx_compressor *c, const u8 *in, size_t in_nbytes,
		     struct lzx_output_bitstream *os)
{
	lzx_compress_lazy(c, in, in_nbytes, os, true);
}

static void
lzx_compress_lazy_32(struct lzx_compressor *c, const u8 *in, size_t in_nbytes,
		     struct lzx_output_bitstream *os)
{
	lzx_compress_lazy(c, in, in_nbytes, os, false);
}

/******************************************************************************/
/*                          Compressor operations                             */
/*----------------------------------------------------------------------------*/

/*
 * Generate tables for mapping match offsets (actually, "adjusted" match
 * offsets) to offset slots.
 */
static void
lzx_init_offset_slot_tabs(struct lzx_compressor *c)
{
	u32 adjusted_offset = 0;
	unsigned slot = 0;

	/* slots [0, 29] */
	for (; adjusted_offset < ARRAY_LEN(c->offset_slot_tab_1);
	     adjusted_offset++)
	{
		if (adjusted_offset >= lzx_offset_slot_base[slot + 1] +
				       LZX_OFFSET_ADJUSTMENT)
			slot++;
		c->offset_slot_tab_1[adjusted_offset] = slot;
	}

	/* slots [30, 49] */
	for (; adjusted_offset < LZX_MAX_WINDOW_SIZE;
	     adjusted_offset += (u32)1 << 14)
	{
		if (adjusted_offset >= lzx_offset_slot_base[slot + 1] +
				       LZX_OFFSET_ADJUSTMENT)
			slot++;
		c->offset_slot_tab_2[adjusted_offset >> 14] = slot;
	}
}

static size_t
lzx_get_compressor_size(size_t max_bufsize, unsigned compression_level)
{
	if (compression_level <= MAX_FAST_LEVEL) {
		if (lzx_is_16_bit(max_bufsize))
			return offsetof(struct lzx_compressor, hc_mf_16) +
			       hc_matchfinder_size_16(max_bufsize);
		else
			return offsetof(struct lzx_compressor, hc_mf_32) +
			       hc_matchfinder_size_32(max_bufsize);
	} else {
		if (lzx_is_16_bit(max_bufsize))
			return offsetof(struct lzx_compressor, bt_mf_16) +
			       bt_matchfinder_size_16(max_bufsize);
		else
			return offsetof(struct lzx_compressor, bt_mf_32) +
			       bt_matchfinder_size_32(max_bufsize);
	}
}

/* Compute the amount of memory needed to allocate an LZX compressor. */
static u64
lzx_get_needed_memory(size_t max_bufsize, unsigned compression_level,
		      bool destructive)
{
	u64 size = 0;

	if (max_bufsize > LZX_MAX_WINDOW_SIZE)
		return 0;

	size += lzx_get_compressor_size(max_bufsize, compression_level);
	if (!destructive)
		size += max_bufsize; /* account for in_buffer */
	return size;
}

/* Allocate an LZX compressor. */
static int
lzx_create_compressor(size_t max_bufsize, unsigned compression_level,
		      bool destructive, void **c_ret)
{
	unsigned window_order;
	struct lzx_compressor *c;

	/* Validate the maximum buffer size and get the window order from it. */
	window_order = lzx_get_window_order(max_bufsize);
	if (window_order == 0)
		return WIMLIB_ERR_INVALID_PARAM;

	/* Allocate the compressor. */
	c = MALLOC(lzx_get_compressor_size(max_bufsize, compression_level));
	if (!c)
		goto oom0;

	c->window_order = window_order;
	c->num_main_syms = lzx_get_num_main_syms(window_order);
	c->destructive = destructive;

	/* Allocate the buffer for preprocessed data if needed. */
	if (!c->destructive) {
		c->in_buffer = MALLOC(max_bufsize);
		if (!c->in_buffer)
			goto oom1;
	}

	if (compression_level <= MAX_FAST_LEVEL) {

		/* Fast compression: Use lazy parsing. */
		if (lzx_is_16_bit(max_bufsize))
			c->impl = lzx_compress_lazy_16;
		else
			c->impl = lzx_compress_lazy_32;

		/* Scale max_search_depth and nice_match_length with the
		 * compression level. */
		c->max_search_depth = (60 * compression_level) / 20;
		c->nice_match_length = (80 * compression_level) / 20;

		/* lzx_compress_lazy() needs max_search_depth >= 2 because it
		 * halves the max_search_depth when attempting a lazy match, and
		 * max_search_depth must be at least 1. */
		c->max_search_depth = max(c->max_search_depth, 2);
	} else {

		/* Normal / high compression: Use near-optimal parsing. */
		if (lzx_is_16_bit(max_bufsize))
			c->impl = lzx_compress_near_optimal_16;
		else
			c->impl = lzx_compress_near_optimal_32;

		/* Scale max_search_depth and nice_match_length with the
		 * compression level. */
		c->max_search_depth = (24 * compression_level) / 50;
		c->nice_match_length = (48 * compression_level) / 50;

		/* Also scale num_optim_passes with the compression level.  But
		 * the more passes there are, the less they help --- so don't
		 * add them linearly.  */
		c->num_optim_passes = 1;
		c->num_optim_passes += (compression_level >= 45);
		c->num_optim_passes += (compression_level >= 70);
		c->num_optim_passes += (compression_level >= 100);
		c->num_optim_passes += (compression_level >= 150);
		c->num_optim_passes += (compression_level >= 200);
		c->num_optim_passes += (compression_level >= 300);

		/* max_search_depth must be at least 1. */
		c->max_search_depth = max(c->max_search_depth, 1);
	}

	/* Prepare the offset => offset slot mapping. */
	lzx_init_offset_slot_tabs(c);

	*c_ret = c;
	return 0;

oom1:
	FREE(c);
oom0:
	return WIMLIB_ERR_NOMEM;
}

/* Compress a buffer of data. */
static size_t
lzx_compress(const void *restrict in, size_t in_nbytes,
	     void *restrict out, size_t out_nbytes_avail, void *restrict _c)
{
	struct lzx_compressor *c = _c;
	struct lzx_output_bitstream os;
	size_t result;

	/* Don't bother trying to compress very small inputs. */
	if (in_nbytes < 64)
		return 0;

	/* If the compressor is in "destructive" mode, then we can directly
	 * preprocess the input data.  Otherwise, we need to copy it into an
	 * internal buffer first. */
	if (!c->destructive) {
		memcpy(c->in_buffer, in, in_nbytes);
		in = c->in_buffer;
	}

	/* Preprocess the input data. */
	lzx_preprocess((void *)in, in_nbytes);

	/* Initially, the previous Huffman codeword lengths are all zeroes. */
	c->codes_index = 0;
	memset(&c->codes[1].lens, 0, sizeof(struct lzx_lens));

	/* Initialize the output bitstream. */
	lzx_init_output(&os, out, out_nbytes_avail);

	/* Call the compression level-specific compress() function. */
	(*c->impl)(c, in, in_nbytes, &os);

	/* Flush the output bitstream. */
	result = lzx_flush_output(&os);

	/* If the data did not compress to less than its original size and we
	 * preprocessed the original buffer, then postprocess it to restore it
	 * to its original state. */
	if (result == 0 && c->destructive)
		lzx_postprocess((void *)in, in_nbytes);

	/* Return the number of compressed bytes, or 0 if the input did not
	 * compress to less than its original size. */
	return result;
}

/* Free an LZX compressor. */
static void
lzx_free_compressor(void *_c)
{
	struct lzx_compressor *c = _c;

	if (!c->destructive)
		FREE(c->in_buffer);
	FREE(c);
}

const struct compressor_ops lzx_compressor_ops = {
	.get_needed_memory  = lzx_get_needed_memory,
	.create_compressor  = lzx_create_compressor,
	.compress	    = lzx_compress,
	.free_compressor    = lzx_free_compressor,
};
