/*
 * lzms_compress.c
 *
 * A compressor for the LZMS compression format.
 */

/*
 * Copyright (C) 2013, 2014, 2015 Eric Biggers
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

#include <limits.h>
#include <string.h>

#include "wimlib/compress_common.h"
#include "wimlib/compressor_ops.h"
#include "wimlib/error.h"
#include "wimlib/lcpit_matchfinder.h"
#include "wimlib/lzms_common.h"
#include "wimlib/matchfinder_common.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"

/*
 * MAX_FAST_LENGTH is the maximum match length for which the length slot can be
 * looked up directly in 'fast_length_slot_tab' and the length cost can be
 * looked up directly in 'fast_length_cost_tab'.
 *
 * We also limit the 'nice_match_len' parameter to this value.  Consequently, if
 * the longest match found is shorter than 'nice_match_len', then it must also
 * be shorter than MAX_FAST_LENGTH.  This makes it possible to do fast lookups
 * of length costs using 'fast_length_cost_tab' without having to keep checking
 * whether the length exceeds MAX_FAST_LENGTH or not.
 */
#define MAX_FAST_LENGTH		255

/* NUM_OPTIM_NODES is the maximum number of bytes the parsing algorithm will
 * step forward before forcing the pending items to be encoded.  If this value
 * is increased, then there will be fewer forced flushes, but the probability
 * entries and Huffman codes will be more likely to become outdated.  */
#define NUM_OPTIM_NODES		2048

/* COST_SHIFT is a scaling factor that makes it possible to consider fractional
 * bit costs.  A single bit has a cost of (1 << COST_SHIFT).  */
#define COST_SHIFT		6

/* Length of the hash table for finding delta matches  */
#define DELTA_HASH_ORDER	17
#define DELTA_HASH_LENGTH	((u32)1 << DELTA_HASH_ORDER)

/* The number of bytes to hash when finding delta matches; also taken to be the
 * minimum length of an explicit offset delta match  */
#define NBYTES_HASHED_FOR_DELTA	3

/* The number of delta match powers to consider (must be <=
 * LZMS_NUM_DELTA_POWER_SYMS)  */
#define NUM_POWERS_TO_CONSIDER	6

/* This structure tracks the state of writing bits as a series of 16-bit coding
 * units, starting at the end of the output buffer and proceeding backwards.  */
struct lzms_output_bitstream {

	/* Bits that haven't yet been written to the output buffer  */
	u64 bitbuf;

	/* Number of bits currently held in @bitbuf  */
	unsigned bitcount;

	/* Pointer to the beginning of the output buffer (this is the "end" when
	 * writing backwards!)  */
	u8 *begin;

	/* Pointer to just past the next position in the output buffer at which
	 * to output a 16-bit coding unit  */
	u8 *next;
};

/* This structure tracks the state of range encoding and its output, which
 * starts at the beginning of the output buffer and proceeds forwards.  */
struct lzms_range_encoder {

	/* The lower boundary of the current range.  Logically, this is a 33-bit
	 * integer whose high bit is needed to detect carries.  */
	u64 lower_bound;

	/* The size of the current range  */
	u32 range_size;

	/* The next 16-bit coding unit to output  */
	u16 cache;

	/* The number of 16-bit coding units whose output has been delayed due
	 * to possible carrying.  The first such coding unit is @cache; all
	 * subsequent such coding units are 0xffff.  */
	u32 cache_size;

	/* Pointer to the beginning of the output buffer  */
	u8 *begin;

	/* Pointer to the position in the output buffer at which the next coding
	 * unit must be written  */
	u8 *next;

	/* Pointer to just past the end of the output buffer  */
	u8 *end;
};

/* Bookkeeping information for an adaptive Huffman code  */
struct lzms_huffman_rebuild_info {

	/* The remaining number of symbols to encode until this code must be
	 * rebuilt  */
	unsigned num_syms_until_rebuild;

	/* The number of symbols in this code  */
	unsigned num_syms;

	/* The rebuild frequency of this code, in symbols  */
	unsigned rebuild_freq;

	/* The Huffman codeword of each symbol in this code  */
	u32 *codewords;

	/* The length of each Huffman codeword, in bits  */
	u8 *lens;

	/* The frequency of each symbol in this code  */
	u32 *freqs;
};

/*
 * The compressor-internal representation of a match or literal.
 *
 * Literals have length=1; matches have length > 1.  (We disallow matches of
 * length 1, even though this is a valid length in LZMS.)
 *
 * The source is encoded as follows:
 *
 * - Literals: the literal byte itself
 * - Explicit offset LZ matches: the match offset plus (LZMS_NUM_LZ_REPS - 1)
 * - Repeat offset LZ matches: the index of the offset in recent_lz_offsets
 * - Explicit offset delta matches: DELTA_SOURCE_TAG is set, the next 3 bits are
 *   the power, and the remainder is the raw offset plus (LZMS_NUM_DELTA_REPS-1)
 * - Repeat offset delta matches: DELTA_SOURCE_TAG is set, and the remainder is
 *   the index of the (power, raw_offset) pair in recent_delta_pairs
 */
struct lzms_item {
	u32 length;
	u32 source;
};

#define DELTA_SOURCE_TAG		((u32)1 << 31)
#define DELTA_SOURCE_POWER_SHIFT	28
#define DELTA_SOURCE_RAW_OFFSET_MASK	(((u32)1 << DELTA_SOURCE_POWER_SHIFT) - 1)

static void __attribute__((unused))
check_that_powers_fit_in_bitfield(void)
{
	STATIC_ASSERT(LZMS_NUM_DELTA_POWER_SYMS <= (1 << (31 - DELTA_SOURCE_POWER_SHIFT)));
}

/* A stripped-down version of the adaptive state in LZMS which excludes the
 * probability entries and Huffman codes  */
PRAGMA_BEGIN_ALIGN(64)
struct lzms_adaptive_state {

	/* Recent offsets for LZ matches  */
	u32 recent_lz_offsets[LZMS_NUM_LZ_REPS + 1];
	u32 prev_lz_offset; /* 0 means none */
	u32 upcoming_lz_offset; /* 0 means none */

	/* Recent (power, raw offset) pairs for delta matches.
	 * The low DELTA_SOURCE_POWER_SHIFT bits of each entry are the raw
	 * offset, and the high bits are the power.  */
	u32 recent_delta_pairs[LZMS_NUM_DELTA_REPS + 1];
	u32 prev_delta_pair; /* 0 means none */
	u32 upcoming_delta_pair; /* 0 means none  */

	/* States for predicting the probabilities of item types  */
	u8 main_state;
	u8 match_state;
	u8 lz_state;
	u8 lz_rep_states[LZMS_NUM_LZ_REP_DECISIONS];
	u8 delta_state;
	u8 delta_rep_states[LZMS_NUM_DELTA_REP_DECISIONS];
} PRAGMA_END_ALIGN(64);

/*
 * This structure represents a byte position in the preprocessed input data and
 * a node in the graph of possible match/literal choices.
 *
 * Logically, each incoming edge to this node is labeled with a literal or a
 * match that can be taken to reach this position from an earlier position; and
 * each outgoing edge from this node is labeled with a literal or a match that
 * can be taken to advance from this position to a later position.
 */
PRAGMA_BEGIN_ALIGN(64)
struct lzms_optimum_node {

	/*
	 * The cost of the lowest-cost path that has been found to reach this
	 * position.  This can change as progressively lower cost paths are
	 * found to reach this position.
	 */
	u32 cost;
#define INFINITE_COST UINT32_MAX

	/*
	 * @item is the last item that was taken to reach this position to reach
	 * it with the stored @cost.  This can change as progressively lower
	 * cost paths are found to reach this position.
	 *
	 * In some cases we look ahead more than one item.  If we looked ahead n
	 * items to reach this position, then @item is the last item taken,
	 * @extra_items contains the other items ordered from second-to-last to
	 * first, and @num_extra_items is n - 1.
	 */
	unsigned num_extra_items;
	struct lzms_item item;
	struct lzms_item extra_items[2];

	/*
	 * The adaptive state that exists at this position.  This is filled in
	 * lazily, only after the minimum-cost path to this position is found.
	 *
	 * Note: the way the algorithm handles this adaptive state in the
	 * "minimum-cost" parse is actually only an approximation.  It's
	 * possible for the globally optimal, minimum cost path to contain a
	 * prefix, ending at a position, where that path prefix is *not* the
	 * minimum cost path to that position.  This can happen if such a path
	 * prefix results in a different adaptive state which results in lower
	 * costs later.  Although the algorithm does do some heuristic
	 * multi-item lookaheads, it does not solve this problem in general.
	 *
	 * Note: this adaptive state structure also does not include the
	 * probability entries or current Huffman codewords.  Those aren't
	 * maintained per-position and are only updated occasionally.
	 */
	struct lzms_adaptive_state state;
} PRAGMA_END_ALIGN(64);

/* The main compressor structure  */
struct lzms_compressor {

	/* The matchfinder for LZ matches  */
	struct lcpit_matchfinder mf;

	/* The preprocessed buffer of data being compressed  */
	u8 *in_buffer;

	/* The number of bytes of data to be compressed, which is the number of
	 * bytes of data in @in_buffer that are actually valid  */
	size_t in_nbytes;

	/*
	 * Boolean flags to enable consideration of various types of multi-step
	 * operations during parsing.
	 *
	 * Among other cases, multi-step operations can help with gaps where two
	 * matches are separated by a non-matching byte.
	 *
	 * This idea is borrowed from Igor Pavlov's LZMA encoder.
	 */
	bool try_lit_lzrep0;
	bool try_lzrep_lit_lzrep0;
	bool try_lzmatch_lit_lzrep0;

	/*
	 * If true, the compressor can use delta matches.  This slows down
	 * compression.  It improves the compression ratio greatly, slightly, or
	 * not at all, depending on the input data.
	 */
	bool use_delta_matches;

	/* If true, the compressor need not preserve the input buffer if it
	 * compresses the data successfully.  */
	bool destructive;

	/* 'last_target_usages' is a large array that is only needed for
	 * preprocessing, so it is in union with fields that don't need to be
	 * initialized until after preprocessing.  */
	union {
	struct {

	/* Temporary space to store matches found by the LZ matchfinder  */
	struct lz_match matches[MAX_FAST_LENGTH - LZMS_MIN_MATCH_LENGTH + 1];

	/* Hash table for finding delta matches  */
	u32 delta_hash_table[DELTA_HASH_LENGTH];

	/* For each delta power, the hash code for the next sequence  */
	u32 next_delta_hashes[NUM_POWERS_TO_CONSIDER];

	/* The per-byte graph nodes for near-optimal parsing  */
	struct lzms_optimum_node optimum_nodes[NUM_OPTIM_NODES + MAX_FAST_LENGTH +
					       1 + MAX_FAST_LENGTH];

	/* Table: length => current cost for small match lengths  */
	u32 fast_length_cost_tab[MAX_FAST_LENGTH + 1];

	/* Range encoder which outputs to the beginning of the compressed data
	 * buffer, proceeding forwards  */
	struct lzms_range_encoder rc;

	/* Bitstream which outputs to the end of the compressed data buffer,
	 * proceeding backwards  */
	struct lzms_output_bitstream os;

	/* States and probability entries for item type disambiguation  */
	unsigned main_state;
	unsigned match_state;
	unsigned lz_state;
	unsigned lz_rep_states[LZMS_NUM_LZ_REP_DECISIONS];
	unsigned delta_state;
	unsigned delta_rep_states[LZMS_NUM_DELTA_REP_DECISIONS];
	struct lzms_probabilites probs;

	/* Huffman codes  */

	struct lzms_huffman_rebuild_info literal_rebuild_info;
	u32 literal_codewords[LZMS_NUM_LITERAL_SYMS];
	u8 literal_lens[LZMS_NUM_LITERAL_SYMS];
	u32 literal_freqs[LZMS_NUM_LITERAL_SYMS];

	struct lzms_huffman_rebuild_info lz_offset_rebuild_info;
	u32 lz_offset_codewords[LZMS_MAX_NUM_OFFSET_SYMS];
	u8 lz_offset_lens[LZMS_MAX_NUM_OFFSET_SYMS];
	u32 lz_offset_freqs[LZMS_MAX_NUM_OFFSET_SYMS];

	struct lzms_huffman_rebuild_info length_rebuild_info;
	u32 length_codewords[LZMS_NUM_LENGTH_SYMS];
	u8 length_lens[LZMS_NUM_LENGTH_SYMS];
	u32 length_freqs[LZMS_NUM_LENGTH_SYMS];

	struct lzms_huffman_rebuild_info delta_offset_rebuild_info;
	u32 delta_offset_codewords[LZMS_MAX_NUM_OFFSET_SYMS];
	u8 delta_offset_lens[LZMS_MAX_NUM_OFFSET_SYMS];
	u32 delta_offset_freqs[LZMS_MAX_NUM_OFFSET_SYMS];

	struct lzms_huffman_rebuild_info delta_power_rebuild_info;
	u32 delta_power_codewords[LZMS_NUM_DELTA_POWER_SYMS];
	u8 delta_power_lens[LZMS_NUM_DELTA_POWER_SYMS];
	u32 delta_power_freqs[LZMS_NUM_DELTA_POWER_SYMS];

	}; /* struct */

	s32 last_target_usages[65536];

	}; /* union */

	/* Table: length => length slot for small match lengths  */
	u8 fast_length_slot_tab[MAX_FAST_LENGTH + 1];

	/* Tables for mapping offsets to offset slots  */

	/* slots [0, 167); 0 <= num_extra_bits <= 10 */
	u8 offset_slot_tab_1[0xe4a5];

	/* slots [167, 427); 11 <= num_extra_bits <= 15 */
	u16 offset_slot_tab_2[0x3d0000 >> 11];

	/* slots [427, 799); 16 <= num_extra_bits  */
	u16 offset_slot_tab_3[((LZMS_MAX_MATCH_OFFSET + 1) - 0xe4a5) >> 16];
};

/******************************************************************************
 *                   Offset and length slot acceleration                      *
 ******************************************************************************/

/* Generate the acceleration table for length slots.  */
static void
lzms_init_fast_length_slot_tab(struct lzms_compressor *c)
{
	unsigned slot = 0;
	for (u32 len = LZMS_MIN_MATCH_LENGTH; len <= MAX_FAST_LENGTH; len++) {
		if (len >= lzms_length_slot_base[slot + 1])
			slot++;
		c->fast_length_slot_tab[len] = slot;
	}
}

/* Generate the acceleration tables for offset slots.  */
static void
lzms_init_offset_slot_tabs(struct lzms_compressor *c)
{
	u32 offset;
	unsigned slot = 0;

	/* slots [0, 167); 0 <= num_extra_bits <= 10 */
	for (offset = 1; offset < 0xe4a5; offset++) {
		if (offset >= lzms_offset_slot_base[slot + 1])
			slot++;
		c->offset_slot_tab_1[offset] = slot;
	}

	/* slots [167, 427); 11 <= num_extra_bits <= 15 */
	for (; offset < 0x3de4a5; offset += (u32)1 << 11) {
		if (offset >= lzms_offset_slot_base[slot + 1])
			slot++;
		c->offset_slot_tab_2[(offset - 0xe4a5) >> 11] = slot;
	}

	/* slots [427, 799); 16 <= num_extra_bits  */
	for (; offset < LZMS_MAX_MATCH_OFFSET + 1; offset += (u32)1 << 16) {
		if (offset >= lzms_offset_slot_base[slot + 1])
			slot++;
		c->offset_slot_tab_3[(offset - 0xe4a5) >> 16] = slot;
	}
}

/*
 * Return the length slot for the specified match length, using the compressor's
 * acceleration table if the length is small enough.
 */
static forceinline unsigned
lzms_comp_get_length_slot(const struct lzms_compressor *c, u32 length)
{
	if (likely(length <= MAX_FAST_LENGTH))
		return c->fast_length_slot_tab[length];
	return lzms_get_length_slot(length);
}

/*
 * Return the offset slot for the specified match offset, using the compressor's
 * acceleration tables to speed up the mapping.
 */
static forceinline unsigned
lzms_comp_get_offset_slot(const struct lzms_compressor *c, u32 offset)
{
	if (offset < 0xe4a5)
		return c->offset_slot_tab_1[offset];
	offset -= 0xe4a5;
	if (offset < 0x3d0000)
		return c->offset_slot_tab_2[offset >> 11];
	return c->offset_slot_tab_3[offset >> 16];
}

/******************************************************************************
 *                             Range encoding                                 *
 ******************************************************************************/

/*
 * Initialize the range encoder @rc to write forwards to the specified buffer
 * @out that is @size bytes long.
 */
static void
lzms_range_encoder_init(struct lzms_range_encoder *rc, u8 *out, size_t size)
{
	rc->lower_bound = 0;
	rc->range_size = 0xffffffff;
	rc->cache = 0;
	rc->cache_size = 1;
	rc->begin = out;
	rc->next = out - sizeof(le16);
	rc->end = out + (size & ~1);
}

/*
 * Attempt to flush bits from the range encoder.
 *
 * The basic idea is that we're writing bits from @rc->lower_bound to the
 * output.  However, due to carrying, the writing of coding units with the
 * maximum value, as well as one prior coding unit, must be delayed until it is
 * determined whether a carry is needed.
 *
 * This is based on the public domain code for LZMA written by Igor Pavlov, but
 * with the following differences:
 *
 *	- In LZMS, 16-bit coding units are required rather than 8-bit.
 *
 *	- In LZMS, the first coding unit is not ignored by the decompressor, so
 *	  the encoder cannot output a dummy value to that position.
 */
static void
lzms_range_encoder_shift_low(struct lzms_range_encoder *rc)
{
	if ((u32)(rc->lower_bound) < 0xffff0000 ||
	    (u32)(rc->lower_bound >> 32) != 0)
	{
		/* Carry not needed (rc->lower_bound < 0xffff0000), or carry
		 * occurred ((rc->lower_bound >> 32) != 0, a.k.a. the carry bit
		 * is 1).  */
		do {
			if (likely(rc->next >= rc->begin)) {
				if (rc->next != rc->end) {
					put_unaligned_le16(rc->cache +
							   (u16)(rc->lower_bound >> 32),
							   rc->next);
					rc->next += sizeof(le16);
				}
			} else {
				rc->next += sizeof(le16);
			}
			rc->cache = 0xffff;
		} while (--rc->cache_size != 0);

		rc->cache = (rc->lower_bound >> 16) & 0xffff;
	}
	++rc->cache_size;
	rc->lower_bound = (rc->lower_bound & 0xffff) << 16;
}

static bool
lzms_range_encoder_flush(struct lzms_range_encoder *rc)
{
	for (int i = 0; i < 4; i++)
		lzms_range_encoder_shift_low(rc);
	return rc->next != rc->end;
}

/*
 * Encode the next bit using the range encoder.
 *
 * @prob is the probability out of LZMS_PROBABILITY_DENOMINATOR that the next
 * bit is 0 rather than 1.
 */
static forceinline void
lzms_range_encode_bit(struct lzms_range_encoder *rc, int bit, u32 prob)
{
	/* Normalize if needed.  */
	if (rc->range_size <= 0xffff) {
		rc->range_size <<= 16;
		lzms_range_encoder_shift_low(rc);
	}

	u32 bound = (rc->range_size >> LZMS_PROBABILITY_BITS) * prob;
	if (bit == 0) {
		rc->range_size = bound;
	} else {
		rc->lower_bound += bound;
		rc->range_size -= bound;
	}
}

/*
 * Encode a bit.  This wraps around lzms_range_encode_bit() to handle using and
 * updating the state and its corresponding probability entry.
 */
static forceinline void
lzms_encode_bit(int bit, unsigned *state_p, unsigned num_states,
		struct lzms_probability_entry *probs,
		struct lzms_range_encoder *rc)
{
	struct lzms_probability_entry *prob_entry;
	u32 prob;

	/* Load the probability entry for the current state.  */
	prob_entry = &probs[*state_p];

	/* Update the state based on the next bit.  */
	*state_p = ((*state_p << 1) | bit) & (num_states - 1);

	/* Get the probability that the bit is 0.  */
	prob = lzms_get_probability(prob_entry);

	/* Update the probability entry.  */
	lzms_update_probability_entry(prob_entry, bit);

	/* Encode the bit using the range encoder.  */
	lzms_range_encode_bit(rc, bit, prob);
}

/* Helper functions for encoding bits in the various decision classes  */

static void
lzms_encode_main_bit(struct lzms_compressor *c, int bit)
{
	lzms_encode_bit(bit, &c->main_state, LZMS_NUM_MAIN_PROBS,
			c->probs.main, &c->rc);
}

static void
lzms_encode_match_bit(struct lzms_compressor *c, int bit)
{
	lzms_encode_bit(bit, &c->match_state, LZMS_NUM_MATCH_PROBS,
			c->probs.match, &c->rc);
}

static void
lzms_encode_lz_bit(struct lzms_compressor *c, int bit)
{
	lzms_encode_bit(bit, &c->lz_state, LZMS_NUM_LZ_PROBS,
			c->probs.lz, &c->rc);
}

static void
lzms_encode_lz_rep_bit(struct lzms_compressor *c, int bit, int idx)
{
	lzms_encode_bit(bit, &c->lz_rep_states[idx], LZMS_NUM_LZ_REP_PROBS,
			c->probs.lz_rep[idx], &c->rc);
}

static void
lzms_encode_delta_bit(struct lzms_compressor *c, int bit)
{
	lzms_encode_bit(bit, &c->delta_state, LZMS_NUM_DELTA_PROBS,
			c->probs.delta, &c->rc);
}

static void
lzms_encode_delta_rep_bit(struct lzms_compressor *c, int bit, int idx)
{
	lzms_encode_bit(bit, &c->delta_rep_states[idx], LZMS_NUM_DELTA_REP_PROBS,
			c->probs.delta_rep[idx], &c->rc);
}

/******************************************************************************
 *                   Huffman encoding and verbatim bits                       *
 ******************************************************************************/

/*
 * Initialize the output bitstream @os to write backwards to the specified
 * buffer @out that is @size bytes long.
 */
static void
lzms_output_bitstream_init(struct lzms_output_bitstream *os,
			   u8 *out, size_t size)
{
	os->bitbuf = 0;
	os->bitcount = 0;
	os->begin = out;
	os->next = out + (size & ~1);
}

/*
 * Write some bits, contained in the low-order @num_bits bits of @bits, to the
 * output bitstream @os.
 *
 * @max_num_bits is a compile-time constant that specifies the maximum number of
 * bits that can ever be written at this call site.
 */
static forceinline void
lzms_write_bits(struct lzms_output_bitstream *os, const u32 bits,
		const unsigned num_bits, const unsigned max_num_bits)
{
	/* Add the bits to the bit buffer variable.  */
	os->bitcount += num_bits;
	os->bitbuf = (os->bitbuf << num_bits) | bits;

	/* Check whether any coding units need to be written.  */
	while (os->bitcount >= 16) {

		os->bitcount -= 16;

		/* Write a coding unit, unless it would underflow the buffer. */
		if (os->next != os->begin) {
			os->next -= sizeof(le16);
			put_unaligned_le16(os->bitbuf >> os->bitcount, os->next);
		}

		/* Optimization for call sites that never write more than 16
		 * bits at once.  */
		if (max_num_bits <= 16)
			break;
	}
}

/*
 * Flush the output bitstream, ensuring that all bits written to it have been
 * written to memory.  Return %true if all bits have been output successfully,
 * or %false if an overrun occurred.
 */
static bool
lzms_output_bitstream_flush(struct lzms_output_bitstream *os)
{
	if (os->next == os->begin)
		return false;

	if (os->bitcount != 0) {
		os->next -= sizeof(le16);
		put_unaligned_le16(os->bitbuf << (16 - os->bitcount), os->next);
	}

	return true;
}

static void
lzms_build_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info)
{
	make_canonical_huffman_code(rebuild_info->num_syms,
				    LZMS_MAX_CODEWORD_LENGTH,
				    rebuild_info->freqs,
				    rebuild_info->lens,
				    rebuild_info->codewords);
	rebuild_info->num_syms_until_rebuild = rebuild_info->rebuild_freq;
}

static void
lzms_init_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info,
		       unsigned num_syms, unsigned rebuild_freq,
		       u32 *codewords, u8 *lens, u32 *freqs)
{
	rebuild_info->num_syms = num_syms;
	rebuild_info->rebuild_freq = rebuild_freq;
	rebuild_info->codewords = codewords;
	rebuild_info->lens = lens;
	rebuild_info->freqs = freqs;
	lzms_init_symbol_frequencies(freqs, num_syms);
	lzms_build_huffman_code(rebuild_info);
}

static void
lzms_rebuild_huffman_code(struct lzms_huffman_rebuild_info *rebuild_info)
{
	lzms_build_huffman_code(rebuild_info);
	lzms_dilute_symbol_frequencies(rebuild_info->freqs, rebuild_info->num_syms);
}

/*
 * Encode a symbol using the specified Huffman code.  Then, if the Huffman code
 * needs to be rebuilt, rebuild it and return true; otherwise return false.
 */
static forceinline bool
lzms_huffman_encode_symbol(unsigned sym,
			   const u32 *codewords, const u8 *lens, u32 *freqs,
			   struct lzms_output_bitstream *os,
			   struct lzms_huffman_rebuild_info *rebuild_info)
{
	lzms_write_bits(os, codewords[sym], lens[sym], LZMS_MAX_CODEWORD_LENGTH);
	++freqs[sym];
	if (--rebuild_info->num_syms_until_rebuild == 0) {
		lzms_rebuild_huffman_code(rebuild_info);
		return true;
	}
	return false;
}

/* Helper routines to encode symbols using the various Huffman codes  */

static bool
lzms_encode_literal_symbol(struct lzms_compressor *c, unsigned sym)
{
	return lzms_huffman_encode_symbol(sym, c->literal_codewords,
					  c->literal_lens, c->literal_freqs,
					  &c->os, &c->literal_rebuild_info);
}

static bool
lzms_encode_lz_offset_symbol(struct lzms_compressor *c, unsigned sym)
{
	return lzms_huffman_encode_symbol(sym, c->lz_offset_codewords,
					  c->lz_offset_lens, c->lz_offset_freqs,
					  &c->os, &c->lz_offset_rebuild_info);
}

static bool
lzms_encode_length_symbol(struct lzms_compressor *c, unsigned sym)
{
	return lzms_huffman_encode_symbol(sym, c->length_codewords,
					  c->length_lens, c->length_freqs,
					  &c->os, &c->length_rebuild_info);
}

static bool
lzms_encode_delta_offset_symbol(struct lzms_compressor *c, unsigned sym)
{
	return lzms_huffman_encode_symbol(sym, c->delta_offset_codewords,
					  c->delta_offset_lens, c->delta_offset_freqs,
					  &c->os, &c->delta_offset_rebuild_info);
}

static bool
lzms_encode_delta_power_symbol(struct lzms_compressor *c, unsigned sym)
{
	return lzms_huffman_encode_symbol(sym, c->delta_power_codewords,
					  c->delta_power_lens, c->delta_power_freqs,
					  &c->os, &c->delta_power_rebuild_info);
}

static void
lzms_update_fast_length_costs(struct lzms_compressor *c);

/*
 * Encode a match length.  If this causes the Huffman code for length symbols to
 * be rebuilt, also update the length costs array used by the parser.
 */
static void
lzms_encode_length(struct lzms_compressor *c, u32 length)
{
	unsigned slot = lzms_comp_get_length_slot(c, length);

	if (lzms_encode_length_symbol(c, slot))
		lzms_update_fast_length_costs(c);

	lzms_write_bits(&c->os, length - lzms_length_slot_base[slot],
			lzms_extra_length_bits[slot],
			LZMS_MAX_EXTRA_LENGTH_BITS);
}

/* Encode the offset of an LZ match.  */
static void
lzms_encode_lz_offset(struct lzms_compressor *c, u32 offset)
{
	unsigned slot = lzms_comp_get_offset_slot(c, offset);

	lzms_encode_lz_offset_symbol(c, slot);
	lzms_write_bits(&c->os, offset - lzms_offset_slot_base[slot],
			lzms_extra_offset_bits[slot],
			LZMS_MAX_EXTRA_OFFSET_BITS);
}

/* Encode the raw offset of a delta match.  */
static void
lzms_encode_delta_raw_offset(struct lzms_compressor *c, u32 raw_offset)
{
	unsigned slot = lzms_comp_get_offset_slot(c, raw_offset);

	lzms_encode_delta_offset_symbol(c, slot);
	lzms_write_bits(&c->os, raw_offset - lzms_offset_slot_base[slot],
			lzms_extra_offset_bits[slot],
			LZMS_MAX_EXTRA_OFFSET_BITS);
}

/******************************************************************************
 *                             Item encoding                                  *
 ******************************************************************************/

/* Encode the specified item, which may be a literal or any type of match.  */
static void
lzms_encode_item(struct lzms_compressor *c, u32 length, u32 source)
{
	/* Main bit: 0 = literal, 1 = match  */
	int main_bit = (length > 1);
	lzms_encode_main_bit(c, main_bit);

	if (!main_bit) {
		/* Literal  */
		unsigned literal = source;
		lzms_encode_literal_symbol(c, literal);
	} else {
		/* Match  */

		/* Match bit: 0 = LZ match, 1 = delta match  */
		int match_bit = (source & DELTA_SOURCE_TAG) != 0;
		lzms_encode_match_bit(c, match_bit);

		if (!match_bit) {
			/* LZ match  */

			/* LZ bit: 0 = explicit offset, 1 = repeat offset  */
			int lz_bit = (source < LZMS_NUM_LZ_REPS);
			lzms_encode_lz_bit(c, lz_bit);

			if (!lz_bit) {
				/* Explicit offset LZ match  */
				u32 offset = source - (LZMS_NUM_LZ_REPS - 1);
				lzms_encode_lz_offset(c, offset);
			} else {
				/* Repeat offset LZ match  */
				int rep_idx = source;
				for (int i = 0; i < rep_idx; i++)
					lzms_encode_lz_rep_bit(c, 1, i);
				if (rep_idx < LZMS_NUM_LZ_REP_DECISIONS)
					lzms_encode_lz_rep_bit(c, 0, rep_idx);
			}
		} else {
			/* Delta match  */

			source &= ~DELTA_SOURCE_TAG;

			/* Delta bit: 0 = explicit offset, 1 = repeat offset  */
			int delta_bit = (source < LZMS_NUM_DELTA_REPS);
			lzms_encode_delta_bit(c, delta_bit);

			if (!delta_bit) {
				/* Explicit offset delta match  */
				u32 power = source >> DELTA_SOURCE_POWER_SHIFT;
				u32 raw_offset = (source & DELTA_SOURCE_RAW_OFFSET_MASK) -
						 (LZMS_NUM_DELTA_REPS - 1);
				lzms_encode_delta_power_symbol(c, power);
				lzms_encode_delta_raw_offset(c, raw_offset);
			} else {
				/* Repeat offset delta match  */
				int rep_idx = source;
				for (int i = 0; i < rep_idx; i++)
					lzms_encode_delta_rep_bit(c, 1, i);
				if (rep_idx < LZMS_NUM_DELTA_REP_DECISIONS)
					lzms_encode_delta_rep_bit(c, 0, rep_idx);
			}
		}

		/* Match length (encoded the same way for any match type)  */
		lzms_encode_length(c, length);
	}
}

/* Encode a list of matches and literals chosen by the parsing algorithm.  */
static void
lzms_encode_nonempty_item_list(struct lzms_compressor *c,
			       struct lzms_optimum_node *end_node)
{
	/* Since we've stored at each node the item we took to arrive at that
	 * node, we can trace our chosen path in backwards order.  However, for
	 * encoding we need to trace our chosen path in forwards order.  To make
	 * this possible, the following loop moves the items from their
	 * destination nodes to their source nodes, which effectively reverses
	 * the path.  (Think of it like reversing a singly-linked list.)  */
	struct lzms_optimum_node *cur_node = end_node;
	struct lzms_item saved_item = cur_node->item;
	do {
		struct lzms_item item = saved_item;
		if (cur_node->num_extra_items > 0) {
			/* Handle an arrival via multi-item lookahead.  */
			unsigned i = 0;
			struct lzms_optimum_node *orig_node = cur_node;
			do {
				cur_node -= item.length;
				cur_node->item = item;
				item = orig_node->extra_items[i];
			} while (++i != orig_node->num_extra_items && i < 2);
		}
		cur_node -= item.length;
		saved_item = cur_node->item;
		cur_node->item = item;
	} while (cur_node != c->optimum_nodes);

	/* Now trace the chosen path in forwards order, encoding each item.  */
	do {
		lzms_encode_item(c, cur_node->item.length, cur_node->item.source);
		cur_node += cur_node->item.length;
	} while (cur_node != end_node);
}

static forceinline void
lzms_encode_item_list(struct lzms_compressor *c,
		      struct lzms_optimum_node *end_node)
{
	if (end_node != c->optimum_nodes)
		lzms_encode_nonempty_item_list(c, end_node);
}

/******************************************************************************
 *                             Cost evaluation                                *
 ******************************************************************************/

/*
 * If p is the predicted probability of the next bit being a 0, then the number
 * of bits required to encode a 0 bit using a binary range encoder is the real
 * number -log2(p), and the number of bits required to encode a 1 bit is the
 * real number -log2(1 - p).  To avoid computing either of these expressions at
 * runtime, 'lzms_bit_costs' is a precomputed table that stores a mapping from
 * probability to cost for each possible probability.  Specifically, the array
 * indices are the numerators of the possible probabilities in LZMS, where the
 * denominators are LZMS_PROBABILITY_DENOMINATOR; and the stored costs are the
 * bit costs multiplied by 1<<COST_SHIFT and rounded to the nearest integer.
 * Furthermore, the values stored for 0% and 100% probabilities are equal to the
 * adjacent values, since these probabilities are not actually permitted.  This
 * allows us to use the num_recent_zero_bits value from the
 * lzms_probability_entry as the array index without fixing up these two special
 * cases.
 */
static const u32 lzms_bit_costs[LZMS_PROBABILITY_DENOMINATOR + 1] = {
	384, 384, 320, 283, 256, 235, 219, 204,
	192, 181, 171, 163, 155, 147, 140, 134,
	128, 122, 117, 112, 107, 103, 99,  94,
	91,  87,  83,  80,  76,  73,  70,  67,
	64,  61,  58,  56,  53,  51,  48,  46,
	43,  41,  39,  37,  35,  33,  30,  29,
	27,  25,  23,  21,  19,  17,  16,  14,
	12,  11,  9,   8,   6,   4,   3,   1,
	1
};

static void __attribute__((unused))
check_cost_shift(void)
{
	/* lzms_bit_costs is hard-coded to the current COST_SHIFT.  */
	STATIC_ASSERT(COST_SHIFT == 6);
}

#if 0
#include <math.h>

static void
lzms_compute_bit_costs(void)
{
	for (u32 i = 0; i <= LZMS_PROBABILITY_DENOMINATOR; i++) {
		u32 prob = i;
		if (prob == 0)
			prob++;
		else if (prob == LZMS_PROBABILITY_DENOMINATOR)
			prob--;

		lzms_bit_costs[i] = round(-log2((double)prob / LZMS_PROBABILITY_DENOMINATOR) *
					  (1 << COST_SHIFT));
	}
}
#endif

/* Return the cost to encode a 0 bit in the specified context.  */
static forceinline u32
lzms_bit_0_cost(unsigned state, const struct lzms_probability_entry *probs)
{
	return lzms_bit_costs[probs[state].num_recent_zero_bits];
}

/* Return the cost to encode a 1 bit in the specified context.  */
static forceinline u32
lzms_bit_1_cost(unsigned state, const struct lzms_probability_entry *probs)
{
	return lzms_bit_costs[LZMS_PROBABILITY_DENOMINATOR -
			      probs[state].num_recent_zero_bits];
}

/* Return the cost to encode a literal, including the main bit.  */
static forceinline u32
lzms_literal_cost(struct lzms_compressor *c, unsigned main_state, unsigned literal)
{
	return lzms_bit_0_cost(main_state, c->probs.main) +
		((u32)c->literal_lens[literal] << COST_SHIFT);
}

/* Update 'fast_length_cost_tab' to use the latest Huffman code.  */
static void
lzms_update_fast_length_costs(struct lzms_compressor *c)
{
	int slot = -1;
	u32 cost = 0;
	for (u32 len = LZMS_MIN_MATCH_LENGTH; len <= MAX_FAST_LENGTH; len++) {
		if (len >= lzms_length_slot_base[slot + 1]) {
			slot++;
			cost = (u32)(c->length_lens[slot] +
				     lzms_extra_length_bits[slot]) << COST_SHIFT;
		}
		c->fast_length_cost_tab[len] = cost;
	}
}

/* Return the cost to encode the specified match length, which must not exceed
 * MAX_FAST_LENGTH.  */
static forceinline u32
lzms_fast_length_cost(const struct lzms_compressor *c, u32 length)
{
	return c->fast_length_cost_tab[length];
}

/* Return the cost to encode the specified LZ match offset.  */
static forceinline u32
lzms_lz_offset_cost(const struct lzms_compressor *c, u32 offset)
{
	unsigned slot = lzms_comp_get_offset_slot(c, offset);
	u32 num_bits = c->lz_offset_lens[slot] + lzms_extra_offset_bits[slot];
	return num_bits << COST_SHIFT;
}

/* Return the cost to encode the specified delta power and raw offset.  */
static forceinline u32
lzms_delta_source_cost(const struct lzms_compressor *c, u32 power, u32 raw_offset)
{
	unsigned slot = lzms_comp_get_offset_slot(c, raw_offset);
	u32 num_bits = c->delta_power_lens[power] + c->delta_offset_lens[slot] +
		       lzms_extra_offset_bits[slot];
	return num_bits << COST_SHIFT;
}

/******************************************************************************
 *                              Adaptive state                                *
 ******************************************************************************/

static void
lzms_init_adaptive_state(struct lzms_adaptive_state *state)
{
	for (int i = 0; i < LZMS_NUM_LZ_REPS + 1; i++)
		state->recent_lz_offsets[i] = i + 1;
	state->prev_lz_offset = 0;
	state->upcoming_lz_offset = 0;

	for (int i = 0; i < LZMS_NUM_DELTA_REPS + 1; i++)
		state->recent_delta_pairs[i] = i + 1;
	state->prev_delta_pair = 0;
	state->upcoming_delta_pair = 0;

	state->main_state = 0;
	state->match_state = 0;
	state->lz_state = 0;
	for (int i = 0; i < LZMS_NUM_LZ_REP_DECISIONS; i++)
		state->lz_rep_states[i] = 0;
	state->delta_state = 0;
	for (int i = 0; i < LZMS_NUM_DELTA_REP_DECISIONS; i++)
		state->delta_rep_states[i] = 0;
}

/*
 * Update the LRU queues for match sources when advancing by one item.
 *
 * Note: using LZMA as a point of comparison, the LRU queues in LZMS are more
 * complex because:
 *	- there are separate queues for LZ and delta matches
 *	- updates to the queues are delayed by one encoded item (this prevents
 *	  sources from being bumped up to index 0 too early)
 */
static void
lzms_update_lru_queues(struct lzms_adaptive_state *state)
{
	if (state->prev_lz_offset != 0) {
		for (int i = LZMS_NUM_LZ_REPS - 1; i >= 0; i--)
			state->recent_lz_offsets[i + 1] = state->recent_lz_offsets[i];
		state->recent_lz_offsets[0] = state->prev_lz_offset;
	}
	state->prev_lz_offset = state->upcoming_lz_offset;

	if (state->prev_delta_pair != 0) {
		for (int i = LZMS_NUM_DELTA_REPS - 1; i >= 0; i--)
			state->recent_delta_pairs[i + 1] = state->recent_delta_pairs[i];
		state->recent_delta_pairs[0] = state->prev_delta_pair;
	}
	state->prev_delta_pair = state->upcoming_delta_pair;
}

static forceinline void
lzms_update_state(u8 *state_p, int bit, unsigned num_states)
{
	*state_p = ((*state_p << 1) | bit) & (num_states - 1);
}

static forceinline void
lzms_update_main_state(struct lzms_adaptive_state *state, int is_match)
{
	lzms_update_state(&state->main_state, is_match, LZMS_NUM_MAIN_PROBS);
}

static forceinline void
lzms_update_match_state(struct lzms_adaptive_state *state, int is_delta)
{
	lzms_update_state(&state->match_state, is_delta, LZMS_NUM_MATCH_PROBS);
}

static forceinline void
lzms_update_lz_state(struct lzms_adaptive_state *state, int is_rep)
{
	lzms_update_state(&state->lz_state, is_rep, LZMS_NUM_LZ_PROBS);
}

static forceinline void
lzms_update_lz_rep_states(struct lzms_adaptive_state *state, int rep_idx)
{
	for (int i = 0; i < rep_idx; i++)
		lzms_update_state(&state->lz_rep_states[i], 1, LZMS_NUM_LZ_REP_PROBS);

	if (rep_idx < LZMS_NUM_LZ_REP_DECISIONS)
		lzms_update_state(&state->lz_rep_states[rep_idx], 0, LZMS_NUM_LZ_REP_PROBS);
}

static forceinline void
lzms_update_delta_state(struct lzms_adaptive_state *state, int is_rep)
{
	lzms_update_state(&state->delta_state, is_rep, LZMS_NUM_DELTA_PROBS);
}

static forceinline void
lzms_update_delta_rep_states(struct lzms_adaptive_state *state, int rep_idx)
{
	for (int i = 0; i < rep_idx; i++)
		lzms_update_state(&state->delta_rep_states[i], 1, LZMS_NUM_DELTA_REP_PROBS);

	if (rep_idx < LZMS_NUM_DELTA_REP_DECISIONS)
		lzms_update_state(&state->delta_rep_states[rep_idx], 0, LZMS_NUM_DELTA_REP_PROBS);
}

/******************************************************************************
 *                              Matchfinding                                  *
 ******************************************************************************/

/* Note: this code just handles finding delta matches.  The code for finding LZ
 * matches is elsewhere.  */


/* Initialize the delta matchfinder for a new input buffer.  */
static void
lzms_init_delta_matchfinder(struct lzms_compressor *c)
{
	/* Set all entries to use an invalid power, which will never match.  */
	STATIC_ASSERT(NUM_POWERS_TO_CONSIDER < (1 << (32 - DELTA_SOURCE_POWER_SHIFT)));
	memset(c->delta_hash_table, 0xFF, sizeof(c->delta_hash_table));

	/* Initialize the next hash code for each power.  We can just use zeroes
	 * initially; it doesn't really matter.  */
	for (u32 i = 0; i < NUM_POWERS_TO_CONSIDER; i++)
		c->next_delta_hashes[i] = 0;
}

/*
 * Compute a DELTA_HASH_ORDER-bit hash code for the first
 * NBYTES_HASHED_FOR_DELTA bytes of the sequence beginning at @p when taken in a
 * delta context with the specified @span.
 */
static forceinline u32
lzms_delta_hash(const u8 *p, const u32 pos, u32 span)
{
	/* A delta match has a certain span and an offset that is a multiple of
	 * that span.  To reduce wasted space we use a single combined hash
	 * table for all spans and positions, but to minimize collisions we
	 * include in the hash code computation the span and the low-order bits
	 * of the current position.  */

	STATIC_ASSERT(NBYTES_HASHED_FOR_DELTA == 3);
	u8 d0 = *(p + 0) - *(p + 0 - span);
	u8 d1 = *(p + 1) - *(p + 1 - span);
	u8 d2 = *(p + 2) - *(p + 2 - span);
	u32 v = ((span + (pos & (span - 1))) << 24) |
		((u32)d2 << 16) | ((u32)d1 << 8) | d0;
	return lz_hash(v, DELTA_HASH_ORDER);
}

/*
 * Given a match between @in_next and @matchptr in a delta context with the
 * specified @span and having the initial @len, extend the match as far as
 * possible, up to a limit of @max_len.
 */
static forceinline u32
lzms_extend_delta_match(const u8 *in_next, const u8 *matchptr,
			u32 len, u32 max_len, u32 span)
{
	while (len < max_len &&
	       (u8)(*(in_next + len) - *(in_next + len - span)) ==
	       (u8)(*(matchptr + len) - *(matchptr + len - span)))
	{
		len++;
	}
	return len;
}

static void
lzms_delta_matchfinder_skip_bytes(struct lzms_compressor *c,
				  const u8 *in_next, u32 count)
{
	u32 pos = in_next - c->in_buffer;
	if (unlikely(c->in_nbytes - (pos + count) <= NBYTES_HASHED_FOR_DELTA + 1))
		return;
	do {
		/* Update the hash table for each power.  */
		for (u32 power = 0; power < NUM_POWERS_TO_CONSIDER; power++) {
			const u32 span = (u32)1 << power;
			if (unlikely(pos < span))
				continue;
			const u32 next_hash = lzms_delta_hash(in_next + 1, pos + 1, span);
			const u32 hash = c->next_delta_hashes[power];
			c->delta_hash_table[hash] =
				(power << DELTA_SOURCE_POWER_SHIFT) | pos;
			c->next_delta_hashes[power] = next_hash;
			prefetchw(&c->delta_hash_table[next_hash]);
		}
	} while (in_next++, pos++, --count);
}

/*
 * Skip the next @count bytes (don't search for matches at them).  @in_next
 * points to the first byte to skip.  The return value is @in_next + count.
 */
static const u8 *
lzms_skip_bytes(struct lzms_compressor *c, u32 count, const u8 *in_next)
{
	lcpit_matchfinder_skip_bytes(&c->mf, count);
	if (c->use_delta_matches)
		lzms_delta_matchfinder_skip_bytes(c, in_next, count);
	return in_next + count;
}

/******************************************************************************
 *                          "Near-optimal" parsing                            *
 ******************************************************************************/

/*
 * The main near-optimal parsing routine.
 *
 * Briefly, the algorithm does an approximate minimum-cost path search to find a
 * "near-optimal" sequence of matches and literals to output, based on the
 * current cost model.  The algorithm steps forward, position by position (byte
 * by byte), and updates the minimum cost path to reach each later position that
 * can be reached using a match or literal from the current position.  This is
 * essentially Dijkstra's algorithm in disguise: the graph nodes are positions,
 * the graph edges are possible matches/literals to code, and the cost of each
 * edge is the estimated number of bits (scaled up by COST_SHIFT) that will be
 * required to output the corresponding match or literal.  But one difference is
 * that we actually compute the lowest-cost path in pieces, where each piece is
 * terminated when there are no choices to be made.
 *
 * The costs of literals and matches are estimated using the range encoder
 * states and the semi-adaptive Huffman codes.  Except for range encoding
 * states, costs are assumed to be constant throughout a single run of the
 * parsing algorithm, which can parse up to NUM_OPTIM_NODES bytes of data.  This
 * introduces a source of non-optimality because the probabilities and Huffman
 * codes can change over this part of the data.  And of course, there are
 * various other reasons why the result isn't optimal in terms of compression
 * ratio.
 */
static void
lzms_near_optimal_parse(struct lzms_compressor *c)
{
	const u8 *in_next = c->in_buffer;
	const u8 * const in_end = &c->in_buffer[c->in_nbytes];
	struct lzms_optimum_node *cur_node;
	struct lzms_optimum_node *end_node;

	/* Set initial length costs for lengths <= MAX_FAST_LENGTH.  */
	lzms_update_fast_length_costs(c);

	/* Set up the initial adaptive state.  */
	lzms_init_adaptive_state(&c->optimum_nodes[0].state);

begin:
	/* Start building a new list of items, which will correspond to the next
	 * piece of the overall minimum-cost path.  */

	cur_node = c->optimum_nodes;
	cur_node->cost = 0;
	end_node = cur_node;

	if (in_next == in_end)
		return;

	/* The following loop runs once for each per byte in the input buffer,
	 * except in a few shortcut cases.  */
	for (;;) {
		u32 num_matches;

		/* Repeat offset LZ matches  */
		if (likely(in_next - c->in_buffer >= LZMS_NUM_LZ_REPS &&
			   in_end - in_next >= 2))
		{
			for (int rep_idx = 0; rep_idx < LZMS_NUM_LZ_REPS; rep_idx++) {

				/* Looking for a repeat offset LZ match at queue
				 * index @rep_idx  */

				const u32 offset = cur_node->state.recent_lz_offsets[rep_idx];
				const u8 * const matchptr = in_next - offset;

				/* Check the first 2 bytes before entering the extension loop.  */
				if (load_u16_unaligned(in_next) != load_u16_unaligned(matchptr))
					continue;

				/* Extend the match to its full length.  */
				const u32 rep_len = lz_extend(in_next, matchptr, 2, in_end - in_next);

				/* Early out for long repeat offset LZ match */
				if (rep_len >= c->mf.nice_match_len) {

					in_next = lzms_skip_bytes(c, rep_len, in_next);

					lzms_encode_item_list(c, cur_node);
					lzms_encode_item(c, rep_len, rep_idx);

					c->optimum_nodes[0].state = cur_node->state;
					cur_node = &c->optimum_nodes[0];

					cur_node->state.upcoming_lz_offset =
						cur_node->state.recent_lz_offsets[rep_idx];
					cur_node->state.upcoming_delta_pair = 0;
					for (int i = rep_idx; i < LZMS_NUM_LZ_REPS; i++)
						cur_node->state.recent_lz_offsets[i] =
							cur_node->state.recent_lz_offsets[i + 1];
					lzms_update_lru_queues(&cur_node->state);
					lzms_update_main_state(&cur_node->state, 1);
					lzms_update_match_state(&cur_node->state, 0);
					lzms_update_lz_state(&cur_node->state, 1);
					lzms_update_lz_rep_states(&cur_node->state, rep_idx);
					goto begin;
				}

				while (end_node < cur_node + rep_len)
					(++end_node)->cost = INFINITE_COST;

				u32 base_cost = cur_node->cost +
						lzms_bit_1_cost(cur_node->state.main_state,
								c->probs.main) +
						lzms_bit_0_cost(cur_node->state.match_state,
								c->probs.match) +
						lzms_bit_1_cost(cur_node->state.lz_state,
								c->probs.lz);

				for (int i = 0; i < rep_idx; i++)
					base_cost += lzms_bit_1_cost(cur_node->state.lz_rep_states[i],
								     c->probs.lz_rep[i]);

				if (rep_idx < LZMS_NUM_LZ_REP_DECISIONS)
					base_cost += lzms_bit_0_cost(cur_node->state.lz_rep_states[rep_idx],
								     c->probs.lz_rep[rep_idx]);

				u32 len = 2;
				do {
					u32 cost = base_cost + lzms_fast_length_cost(c, len);
					if (cost < (cur_node + len)->cost) {
						(cur_node + len)->cost = cost;
						(cur_node + len)->item = (struct lzms_item) {
							.length = len,
							.source = rep_idx,
						};
						(cur_node + len)->num_extra_items = 0;
					}
				} while (++len <= rep_len);


				/* try LZ-rep + lit + LZ-rep0  */
				if (c->try_lzrep_lit_lzrep0 &&
				    in_end - (in_next + rep_len) >= 3 &&
				    load_u16_unaligned(in_next + rep_len + 1) ==
				    load_u16_unaligned(matchptr + rep_len + 1))
				{
					const u32 rep0_len = lz_extend(in_next + rep_len + 1,
								       matchptr + rep_len + 1,
								       2,
								       min(c->mf.nice_match_len,
									   in_end - (in_next + rep_len + 1)));

					unsigned main_state = cur_node->state.main_state;
					unsigned match_state = cur_node->state.match_state;
					unsigned lz_state = cur_node->state.lz_state;
					unsigned lz_rep0_state = cur_node->state.lz_rep_states[0];

					/* update states for LZ-rep  */
					main_state = ((main_state << 1) | 1) % LZMS_NUM_MAIN_PROBS;
					match_state = ((match_state << 1) | 0) % LZMS_NUM_MATCH_PROBS;
					lz_state = ((lz_state << 1) | 1) % LZMS_NUM_LZ_PROBS;
					lz_rep0_state = ((lz_rep0_state << 1) | (rep_idx > 0)) %
								LZMS_NUM_LZ_REP_PROBS;

					/* LZ-rep cost  */
					u32 cost = base_cost + lzms_fast_length_cost(c, rep_len);

					/* add literal cost  */
					cost += lzms_literal_cost(c, main_state, *(in_next + rep_len));

					/* update state for literal  */
					main_state = ((main_state << 1) | 0) % LZMS_NUM_MAIN_PROBS;

					/* add LZ-rep0 cost  */
					cost += lzms_bit_1_cost(main_state, c->probs.main) +
						lzms_bit_0_cost(match_state, c->probs.match) +
						lzms_bit_1_cost(lz_state, c->probs.lz) +
						lzms_bit_0_cost(lz_rep0_state, c->probs.lz_rep[0]) +
						lzms_fast_length_cost(c, rep0_len);

					const u32 total_len = rep_len + 1 + rep0_len;

					while (end_node < cur_node + total_len)
						(++end_node)->cost = INFINITE_COST;

					if (cost < (cur_node + total_len)->cost) {
						(cur_node + total_len)->cost = cost;
						(cur_node + total_len)->item = (struct lzms_item) {
							.length = rep0_len,
							.source = 0,
						};
						(cur_node + total_len)->extra_items[0] = (struct lzms_item) {
							.length = 1,
							.source = *(in_next + rep_len),
						};
						(cur_node + total_len)->extra_items[1] = (struct lzms_item) {
							.length = rep_len,
							.source = rep_idx,
						};
						(cur_node + total_len)->num_extra_items = 2;
					}
				}
			}
		}

		/* Repeat offset delta matches  */
		if (c->use_delta_matches &&
		    likely(in_next - c->in_buffer >= LZMS_NUM_DELTA_REPS + 1 &&
			   (in_end - in_next >= 2)))
		{
			for (int rep_idx = 0; rep_idx < LZMS_NUM_DELTA_REPS; rep_idx++) {

				/* Looking for a repeat offset delta match at
				 * queue index @rep_idx  */

				const u32 pair = cur_node->state.recent_delta_pairs[rep_idx];
				const u32 power = pair >> DELTA_SOURCE_POWER_SHIFT;
				const u32 raw_offset = pair & DELTA_SOURCE_RAW_OFFSET_MASK;
				const u32 span = (u32)1 << power;
				const u32 offset = raw_offset << power;
				const u8 * const matchptr = in_next - offset;

				/* Check the first 2 bytes before entering the
				 * extension loop.  */
				if (((u8)(*(in_next + 0) - *(in_next + 0 - span)) !=
				     (u8)(*(matchptr + 0) - *(matchptr + 0 - span))) ||
				    ((u8)(*(in_next + 1) - *(in_next + 1 - span)) !=
				     (u8)(*(matchptr + 1) - *(matchptr + 1 - span))))
					continue;

				/* Extend the match to its full length.  */
				const u32 rep_len = lzms_extend_delta_match(in_next, matchptr,
									    2, in_end - in_next,
									    span);

				/* Early out for long repeat offset delta match */
				if (rep_len >= c->mf.nice_match_len) {

					in_next = lzms_skip_bytes(c, rep_len, in_next);

					lzms_encode_item_list(c, cur_node);
					lzms_encode_item(c, rep_len, DELTA_SOURCE_TAG | rep_idx);

					c->optimum_nodes[0].state = cur_node->state;
					cur_node = &c->optimum_nodes[0];

					cur_node->state.upcoming_delta_pair = pair;
					cur_node->state.upcoming_lz_offset = 0;
					for (int i = rep_idx; i < LZMS_NUM_DELTA_REPS; i++)
						cur_node->state.recent_delta_pairs[i] =
							cur_node->state.recent_delta_pairs[i + 1];
					lzms_update_lru_queues(&cur_node->state);
					lzms_update_main_state(&cur_node->state, 1);
					lzms_update_match_state(&cur_node->state, 1);
					lzms_update_delta_state(&cur_node->state, 1);
					lzms_update_delta_rep_states(&cur_node->state, rep_idx);
					goto begin;
				}

				while (end_node < cur_node + rep_len)
					(++end_node)->cost = INFINITE_COST;

				u32 base_cost = cur_node->cost +
						lzms_bit_1_cost(cur_node->state.main_state,
								c->probs.main) +
						lzms_bit_1_cost(cur_node->state.match_state,
								c->probs.match) +
						lzms_bit_1_cost(cur_node->state.delta_state,
								c->probs.delta);

				for (int i = 0; i < rep_idx; i++)
					base_cost += lzms_bit_1_cost(cur_node->state.delta_rep_states[i],
								     c->probs.delta_rep[i]);

				if (rep_idx < LZMS_NUM_DELTA_REP_DECISIONS)
					base_cost += lzms_bit_0_cost(cur_node->state.delta_rep_states[rep_idx],
								     c->probs.delta_rep[rep_idx]);

				u32 len = 2;
				do {
					u32 cost = base_cost + lzms_fast_length_cost(c, len);
					if (cost < (cur_node + len)->cost) {
						(cur_node + len)->cost = cost;
						(cur_node + len)->item = (struct lzms_item) {
							.length = len,
							.source = DELTA_SOURCE_TAG | rep_idx,
						};
						(cur_node + len)->num_extra_items = 0;
					}
				} while (++len <= rep_len);
			}
		}

		/* Explicit offset LZ matches  */
		num_matches = lcpit_matchfinder_get_matches(&c->mf, c->matches);
		if (num_matches) {

			u32 best_len = c->matches[0].length;

			/* Early out for long explicit offset LZ match  */
			if (best_len >= c->mf.nice_match_len) {

				const u32 offset = c->matches[0].offset;

				/* Extend the match as far as possible.
				 * This is necessary because the LCP-interval
				 * tree matchfinder only reports up to
				 * nice_match_len bytes.  */
				best_len = lz_extend(in_next, in_next - offset,
						     best_len, in_end - in_next);

				in_next = lzms_skip_bytes(c, best_len - 1, in_next + 1);

				lzms_encode_item_list(c, cur_node);
				lzms_encode_item(c, best_len, offset + LZMS_NUM_LZ_REPS - 1);

				c->optimum_nodes[0].state = cur_node->state;
				cur_node = &c->optimum_nodes[0];

				cur_node->state.upcoming_lz_offset = offset;
				cur_node->state.upcoming_delta_pair = 0;
				lzms_update_lru_queues(&cur_node->state);
				lzms_update_main_state(&cur_node->state, 1);
				lzms_update_match_state(&cur_node->state, 0);
				lzms_update_lz_state(&cur_node->state, 0);
				goto begin;
			}

			while (end_node < cur_node + best_len)
				(++end_node)->cost = INFINITE_COST;

			u32 base_cost = cur_node->cost +
					lzms_bit_1_cost(cur_node->state.main_state,
							c->probs.main) +
					lzms_bit_0_cost(cur_node->state.match_state,
							c->probs.match) +
					lzms_bit_0_cost(cur_node->state.lz_state,
							c->probs.lz);

			if (c->try_lzmatch_lit_lzrep0 &&
			    likely(in_end - (in_next + c->matches[0].length) >= 3))
			{
				/* try LZ-match + lit + LZ-rep0  */

				u32 l = 2;
				u32 i = num_matches - 1;
				do {
					const u32 len = c->matches[i].length;
					const u32 offset = c->matches[i].offset;
					const u32 position_cost = base_cost +
								  lzms_lz_offset_cost(c, offset);
					do {
						u32 cost = position_cost + lzms_fast_length_cost(c, l);
						if (cost < (cur_node + l)->cost) {
							(cur_node + l)->cost = cost;
							(cur_node + l)->item = (struct lzms_item) {
								.length = l,
								.source = offset + (LZMS_NUM_LZ_REPS - 1),
							};
							(cur_node + l)->num_extra_items = 0;
						}
					} while (++l <= len);

					const u8 * const matchptr = in_next - offset;
					if (load_u16_unaligned(matchptr + len + 1) !=
					    load_u16_unaligned(in_next + len + 1))
						continue;

					const u32 rep0_len = lz_extend(in_next + len + 1,
								       matchptr + len + 1,
								       2,
								       min(c->mf.nice_match_len,
									   in_end - (in_next + len + 1)));

					unsigned main_state = cur_node->state.main_state;
					unsigned match_state = cur_node->state.match_state;
					unsigned lz_state = cur_node->state.lz_state;

					/* update states for LZ-match  */
					main_state = ((main_state << 1) | 1) % LZMS_NUM_MAIN_PROBS;
					match_state = ((match_state << 1) | 0) % LZMS_NUM_MATCH_PROBS;
					lz_state = ((lz_state << 1) | 0) % LZMS_NUM_LZ_PROBS;

					/* LZ-match cost  */
					u32 cost = position_cost + lzms_fast_length_cost(c, len);

					/* add literal cost  */
					cost += lzms_literal_cost(c, main_state, *(in_next + len));

					/* update state for literal  */
					main_state = ((main_state << 1) | 0) % LZMS_NUM_MAIN_PROBS;

					/* add LZ-rep0 cost  */
					cost += lzms_bit_1_cost(main_state, c->probs.main) +
						lzms_bit_0_cost(match_state, c->probs.match) +
						lzms_bit_1_cost(lz_state, c->probs.lz) +
						lzms_bit_0_cost(cur_node->state.lz_rep_states[0],
								c->probs.lz_rep[0]) +
						lzms_fast_length_cost(c, rep0_len);

					const u32 total_len = len + 1 + rep0_len;

					while (end_node < cur_node + total_len)
						(++end_node)->cost = INFINITE_COST;

					if (cost < (cur_node + total_len)->cost) {
						(cur_node + total_len)->cost = cost;
						(cur_node + total_len)->item = (struct lzms_item) {
							.length = rep0_len,
							.source = 0,
						};
						(cur_node + total_len)->extra_items[0] = (struct lzms_item) {
							.length = 1,
							.source = *(in_next + len),
						};
						(cur_node + total_len)->extra_items[1] = (struct lzms_item) {
							.length = len,
							.source = offset + LZMS_NUM_LZ_REPS - 1,
						};
						(cur_node + total_len)->num_extra_items = 2;
					}
				} while (i--);
			} else {
				u32 l = 2;
				u32 i = num_matches - 1;
				do {
					u32 position_cost = base_cost +
							    lzms_lz_offset_cost(c, c->matches[i].offset);
					do {
						u32 cost = position_cost + lzms_fast_length_cost(c, l);
						if (cost < (cur_node + l)->cost) {
							(cur_node + l)->cost = cost;
							(cur_node + l)->item = (struct lzms_item) {
								.length = l,
								.source = c->matches[i].offset +
									  (LZMS_NUM_LZ_REPS - 1),
							};
							(cur_node + l)->num_extra_items = 0;
						}
					} while (++l <= c->matches[i].length);
				} while (i--);
			}
		}

		/* Explicit offset delta matches  */
		if (c->use_delta_matches &&
		    likely(in_end - in_next >= NBYTES_HASHED_FOR_DELTA + 1))
		{
			const u32 pos = in_next - c->in_buffer;

			/* Consider each possible power (log2 of span)  */
			STATIC_ASSERT(NUM_POWERS_TO_CONSIDER <= LZMS_NUM_DELTA_POWER_SYMS);
			for (u32 power = 0; power < NUM_POWERS_TO_CONSIDER; power++) {

				const u32 span = (u32)1 << power;

				if (unlikely(pos < span))
					continue;

				const u32 next_hash = lzms_delta_hash(in_next + 1, pos + 1, span);
				const u32 hash = c->next_delta_hashes[power];
				const u32 cur_match = c->delta_hash_table[hash];

				c->delta_hash_table[hash] = (power << DELTA_SOURCE_POWER_SHIFT) | pos;
				c->next_delta_hashes[power] = next_hash;
				prefetchw(&c->delta_hash_table[next_hash]);

				if (power != cur_match >> DELTA_SOURCE_POWER_SHIFT)
					continue;

				const u32 offset = pos - (cur_match & DELTA_SOURCE_RAW_OFFSET_MASK);

				/* The offset must be a multiple of span.  */
				if (offset & (span - 1))
					continue;

				const u8 * const matchptr = in_next - offset;

				/* Check the first 3 bytes before entering the
				 * extension loop.  */
				STATIC_ASSERT(NBYTES_HASHED_FOR_DELTA == 3);
				if (((u8)(*(in_next + 0) - *(in_next + 0 - span)) !=
				     (u8)(*(matchptr + 0) - *(matchptr + 0 - span))) ||
				    ((u8)(*(in_next + 1) - *(in_next + 1 - span)) !=
				     (u8)(*(matchptr + 1) - *(matchptr + 1 - span))) ||
				    ((u8)(*(in_next + 2) - *(in_next + 2 - span)) !=
				     (u8)(*(matchptr + 2) - *(matchptr + 2 - span))))
					continue;

				/* Extend the delta match to its full length.  */
				const u32 len = lzms_extend_delta_match(in_next,
									matchptr,
									NBYTES_HASHED_FOR_DELTA,
									in_end - in_next,
									span);

				const u32 raw_offset = offset >> power;

				if (unlikely(raw_offset > DELTA_SOURCE_RAW_OFFSET_MASK -
							  (LZMS_NUM_DELTA_REPS - 1)))
					continue;

				const u32 pair = (power << DELTA_SOURCE_POWER_SHIFT) |
						 raw_offset;
				const u32 source = DELTA_SOURCE_TAG |
						   (pair + LZMS_NUM_DELTA_REPS - 1);

				/* Early out for long explicit offset delta match  */
				if (len >= c->mf.nice_match_len) {

					in_next = lzms_skip_bytes(c, len - 1, in_next + 1);

					lzms_encode_item_list(c, cur_node);
					lzms_encode_item(c, len, source);

					c->optimum_nodes[0].state = cur_node->state;
					cur_node = &c->optimum_nodes[0];

					cur_node->state.upcoming_lz_offset = 0;
					cur_node->state.upcoming_delta_pair = pair;
					lzms_update_lru_queues(&cur_node->state);
					lzms_update_main_state(&cur_node->state, 1);
					lzms_update_match_state(&cur_node->state, 1);
					lzms_update_delta_state(&cur_node->state, 0);
					goto begin;
				}

				while (end_node < cur_node + len)
					(++end_node)->cost = INFINITE_COST;

				u32 base_cost = cur_node->cost +
						lzms_bit_1_cost(cur_node->state.main_state,
								c->probs.main) +
						lzms_bit_1_cost(cur_node->state.match_state,
								c->probs.match) +
						lzms_bit_0_cost(cur_node->state.delta_state,
								c->probs.delta) +
						lzms_delta_source_cost(c, power, raw_offset);

				u32 l = NBYTES_HASHED_FOR_DELTA;
				do {
					u32 cost = base_cost + lzms_fast_length_cost(c, l);
					if (cost < (cur_node + l)->cost) {
						(cur_node + l)->cost = cost;
						(cur_node + l)->item = (struct lzms_item) {
							.length = l,
							.source = source,
						};
						(cur_node + l)->num_extra_items = 0;
					}
				} while (++l <= len);
			}
		}

		/* Literal  */
		if (end_node < cur_node + 1)
			(++end_node)->cost = INFINITE_COST;
		const u32 cur_and_lit_cost = cur_node->cost +
					     lzms_literal_cost(c, cur_node->state.main_state,
							       *in_next);
		if (cur_and_lit_cost < (cur_node + 1)->cost) {
			(cur_node + 1)->cost = cur_and_lit_cost;
			(cur_node + 1)->item = (struct lzms_item) {
				.length = 1,
				.source = *in_next,
			};
			(cur_node + 1)->num_extra_items = 0;
		} else if (c->try_lit_lzrep0 && in_end - (in_next + 1) >= 2) {
			/* try lit + LZ-rep0  */
			const u32 offset =
				(cur_node->state.prev_lz_offset) ?
					cur_node->state.prev_lz_offset :
					cur_node->state.recent_lz_offsets[0];

			if (load_u16_unaligned(in_next + 1) ==
			    load_u16_unaligned(in_next + 1 - offset))
			{
				const u32 rep0_len = lz_extend(in_next + 1,
							       in_next + 1 - offset,
							       2,
							       min(in_end - (in_next + 1),
								   c->mf.nice_match_len));

				unsigned main_state = cur_node->state.main_state;

				/* Update main_state after literal  */
				main_state = (main_state << 1 | 0) % LZMS_NUM_MAIN_PROBS;

				/* Add cost of LZ-rep0  */
				const u32 cost = cur_and_lit_cost +
						 lzms_bit_1_cost(main_state, c->probs.main) +
						 lzms_bit_0_cost(cur_node->state.match_state,
								 c->probs.match) +
						 lzms_bit_1_cost(cur_node->state.lz_state,
								 c->probs.lz) +
						 lzms_bit_0_cost(cur_node->state.lz_rep_states[0],
								 c->probs.lz_rep[0]) +
						 lzms_fast_length_cost(c, rep0_len);

				const u32 total_len = 1 + rep0_len;

				while (end_node < cur_node + total_len)
					(++end_node)->cost = INFINITE_COST;

				if (cost < (cur_node + total_len)->cost) {
					(cur_node + total_len)->cost = cost;
					(cur_node + total_len)->item = (struct lzms_item) {
						.length = rep0_len,
						.source = 0,
					};
					(cur_node + total_len)->extra_items[0] = (struct lzms_item) {
						.length = 1,
						.source = *in_next,
					};
					(cur_node + total_len)->num_extra_items = 1;
				}
			}
		}

		/* Advance to the next position.  */
		in_next++;
		cur_node++;

		/* The lowest-cost path to the current position is now known.
		 * Finalize the adaptive state that results from taking this
		 * lowest-cost path.  */
		struct lzms_item item_to_take = cur_node->item;
		struct lzms_optimum_node *source_node = cur_node - item_to_take.length;
		int next_item_idx = -1;
		for (unsigned i = 0; i < cur_node->num_extra_items; i++) {
			item_to_take = cur_node->extra_items[i];
			source_node -= item_to_take.length;
			next_item_idx++;
		}
		cur_node->state = source_node->state;
		for (;;) {
			const u32 length = item_to_take.length;
			u32 source = item_to_take.source;

			cur_node->state.upcoming_lz_offset = 0;
			cur_node->state.upcoming_delta_pair = 0;
			if (length > 1) {
				/* Match  */

				lzms_update_main_state(&cur_node->state, 1);

				if (source & DELTA_SOURCE_TAG) {
					/* Delta match  */

					lzms_update_match_state(&cur_node->state, 1);
					source &= ~DELTA_SOURCE_TAG;

					if (source >= LZMS_NUM_DELTA_REPS) {
						/* Explicit offset delta match  */
						lzms_update_delta_state(&cur_node->state, 0);
						cur_node->state.upcoming_delta_pair =
							source - (LZMS_NUM_DELTA_REPS - 1);
					} else {
						/* Repeat offset delta match  */
						int rep_idx = source;

						lzms_update_delta_state(&cur_node->state, 1);
						lzms_update_delta_rep_states(&cur_node->state, rep_idx);

						cur_node->state.upcoming_delta_pair =
							cur_node->state.recent_delta_pairs[rep_idx];

						for (int i = rep_idx; i < LZMS_NUM_DELTA_REPS; i++)
							cur_node->state.recent_delta_pairs[i] =
								cur_node->state.recent_delta_pairs[i + 1];
					}
				} else {
					lzms_update_match_state(&cur_node->state, 0);

					if (source >= LZMS_NUM_LZ_REPS) {
						/* Explicit offset LZ match  */
						lzms_update_lz_state(&cur_node->state, 0);
						cur_node->state.upcoming_lz_offset =
							source - (LZMS_NUM_LZ_REPS - 1);
					} else {
						/* Repeat offset LZ match  */
						int rep_idx = source;

						lzms_update_lz_state(&cur_node->state, 1);
						lzms_update_lz_rep_states(&cur_node->state, rep_idx);

						cur_node->state.upcoming_lz_offset =
							cur_node->state.recent_lz_offsets[rep_idx];

						for (int i = rep_idx; i < LZMS_NUM_LZ_REPS; i++)
							cur_node->state.recent_lz_offsets[i] =
								cur_node->state.recent_lz_offsets[i + 1];
					}
				}
			} else {
				/* Literal  */
				lzms_update_main_state(&cur_node->state, 0);
			}

			lzms_update_lru_queues(&cur_node->state);

			if (next_item_idx < 0)
				break;
			if (next_item_idx == 0)
				// coverity[assigned_value]
				item_to_take = cur_node->item;
			else
				// coverity[assigned_value]
				item_to_take = cur_node->extra_items[next_item_idx - 1];
			--next_item_idx;
		}

		/*
		 * This loop will terminate when either of the following
		 * conditions is true:
		 *
		 * (1) cur_node == end_node
		 *
		 *	There are no paths that extend beyond the current
		 *	position.  In this case, any path to a later position
		 *	must pass through the current position, so we can go
		 *	ahead and choose the list of items that led to this
		 *	position.
		 *
		 * (2) cur_node == &c->optimum_nodes[NUM_OPTIM_NODES]
		 *
		 *	This bounds the number of times the algorithm can step
		 *	forward before it is guaranteed to start choosing items.
		 *	This limits the memory usage.  It also guarantees that
		 *	the parser will not go too long without updating the
		 *	probability tables.
		 *
		 * Note: no check for end-of-buffer is needed because
		 * end-of-buffer will trigger condition (1).
		 */
		if (cur_node == end_node ||
		    cur_node == &c->optimum_nodes[NUM_OPTIM_NODES])
		{
			lzms_encode_nonempty_item_list(c, cur_node);
			c->optimum_nodes[0].state = cur_node->state;
			goto begin;
		}
	}
}

static void
lzms_init_states_and_probabilities(struct lzms_compressor *c)
{
	c->main_state = 0;
	c->match_state = 0;
	c->lz_state = 0;
	for (int i = 0; i < LZMS_NUM_LZ_REP_DECISIONS; i++)
		c->lz_rep_states[i] = 0;
	c->delta_state = 0;
	for (int i = 0; i < LZMS_NUM_DELTA_REP_DECISIONS; i++)
		c->delta_rep_states[i] = 0;

	lzms_init_probabilities(&c->probs);
}

static void
lzms_init_huffman_codes(struct lzms_compressor *c, unsigned num_offset_slots)
{
	lzms_init_huffman_code(&c->literal_rebuild_info,
			       LZMS_NUM_LITERAL_SYMS,
			       LZMS_LITERAL_CODE_REBUILD_FREQ,
			       c->literal_codewords,
			       c->literal_lens,
			       c->literal_freqs);

	lzms_init_huffman_code(&c->lz_offset_rebuild_info,
			       num_offset_slots,
			       LZMS_LZ_OFFSET_CODE_REBUILD_FREQ,
			       c->lz_offset_codewords,
			       c->lz_offset_lens,
			       c->lz_offset_freqs);

	lzms_init_huffman_code(&c->length_rebuild_info,
			       LZMS_NUM_LENGTH_SYMS,
			       LZMS_LENGTH_CODE_REBUILD_FREQ,
			       c->length_codewords,
			       c->length_lens,
			       c->length_freqs);

	lzms_init_huffman_code(&c->delta_offset_rebuild_info,
			       num_offset_slots,
			       LZMS_DELTA_OFFSET_CODE_REBUILD_FREQ,
			       c->delta_offset_codewords,
			       c->delta_offset_lens,
			       c->delta_offset_freqs);

	lzms_init_huffman_code(&c->delta_power_rebuild_info,
			       LZMS_NUM_DELTA_POWER_SYMS,
			       LZMS_DELTA_POWER_CODE_REBUILD_FREQ,
			       c->delta_power_codewords,
			       c->delta_power_lens,
			       c->delta_power_freqs);
}

/*
 * Flush the output streams, prepare the final compressed data, and return its
 * size in bytes.
 *
 * A return value of 0 indicates that the data could not be compressed to fit in
 * the available space.
 */
static size_t
lzms_finalize(struct lzms_compressor *c)
{
	size_t num_forwards_bytes;
	size_t num_backwards_bytes;

	/* Flush both the forwards and backwards streams, and make sure they
	 * didn't cross each other and start overwriting each other's data.  */
	if (!lzms_output_bitstream_flush(&c->os))
		return 0;

	if (!lzms_range_encoder_flush(&c->rc))
		return 0;

	if (c->rc.next > c->os.next)
		return 0;

	/* Now the compressed buffer contains the data output by the forwards
	 * bitstream, then empty space, then data output by the backwards
	 * bitstream.  Move the data output by the backwards bitstream to be
	 * adjacent to the data output by the forward bitstream, and calculate
	 * the compressed size that this results in.  */
	num_forwards_bytes = c->rc.next - c->rc.begin;
	num_backwards_bytes = c->rc.end - c->os.next;

	memmove(c->rc.next, c->os.next, num_backwards_bytes);

	return num_forwards_bytes + num_backwards_bytes;
}

static u64
lzms_get_needed_memory(size_t max_bufsize, unsigned compression_level,
		       bool destructive)
{
	u64 size = 0;

	if (max_bufsize > LZMS_MAX_BUFFER_SIZE)
		return 0;

	size += sizeof(struct lzms_compressor);

	if (!destructive)
		size += max_bufsize; /* in_buffer */

	/* mf */
	size += lcpit_matchfinder_get_needed_memory(max_bufsize);

	return size;
}

static int
lzms_create_compressor(size_t max_bufsize, unsigned compression_level,
		       bool destructive, void **c_ret)
{
	struct lzms_compressor *c;
	u32 nice_match_len;

	if (max_bufsize > LZMS_MAX_BUFFER_SIZE)
		return WIMLIB_ERR_INVALID_PARAM;

	c = ALIGNED_MALLOC(sizeof(struct lzms_compressor), 64);
	if (!c)
		goto oom0;

	c->destructive = destructive;

	/* Scale nice_match_len with the compression level.  But to allow an
	 * optimization for length cost calculations, don't allow nice_match_len
	 * to exceed MAX_FAST_LENGTH.  */
	nice_match_len = min(((u64)compression_level * 63) / 50, MAX_FAST_LENGTH);

	c->use_delta_matches = (compression_level >= 35);
	c->try_lzmatch_lit_lzrep0 = (compression_level >= 45);
	c->try_lit_lzrep0 = (compression_level >= 60);
	c->try_lzrep_lit_lzrep0 = (compression_level >= 60);

	if (!c->destructive) {
		c->in_buffer = MALLOC(max_bufsize);
		if (!c->in_buffer)
			goto oom1;
	}

	if (!lcpit_matchfinder_init(&c->mf, max_bufsize, 2, nice_match_len))
		goto oom2;

	lzms_init_fast_length_slot_tab(c);
	lzms_init_offset_slot_tabs(c);

	*c_ret = c;
	return 0;

oom2:
	if (!c->destructive)
		FREE(c->in_buffer);
oom1:
	ALIGNED_FREE(c);
oom0:
	return WIMLIB_ERR_NOMEM;
}

static size_t
lzms_compress(const void *restrict in, size_t in_nbytes,
	      void *restrict out, size_t out_nbytes_avail, void *restrict _c)
{
	struct lzms_compressor *c = _c;
	size_t result;

	/* Don't bother trying to compress extremely small inputs.  */
	if (in_nbytes < 4)
		return 0;

	/* Copy the input data into the internal buffer and preprocess it.  */
	if (c->destructive)
		c->in_buffer = (void *)in;
	else
		memcpy(c->in_buffer, in, in_nbytes);
	c->in_nbytes = in_nbytes;
	lzms_x86_filter(c->in_buffer, in_nbytes, c->last_target_usages, false);

	/* Prepare the matchfinders.  */
	lcpit_matchfinder_load_buffer(&c->mf, c->in_buffer, c->in_nbytes);
	if (c->use_delta_matches)
		lzms_init_delta_matchfinder(c);

	/* Initialize the encoder structures.  */
	lzms_range_encoder_init(&c->rc, out, out_nbytes_avail);
	lzms_output_bitstream_init(&c->os, out, out_nbytes_avail);
	lzms_init_states_and_probabilities(c);
	lzms_init_huffman_codes(c, lzms_get_num_offset_slots(c->in_nbytes));

	/* The main loop: parse and encode.  */
	lzms_near_optimal_parse(c);

	/* Return the compressed data size or 0.  */
	result = lzms_finalize(c);
	if (!result && c->destructive)
		lzms_x86_filter(c->in_buffer, c->in_nbytes, c->last_target_usages, true);
	return result;
}

static void
lzms_free_compressor(void *_c)
{
	struct lzms_compressor *c = _c;

	if (!c->destructive)
		FREE(c->in_buffer);
	lcpit_matchfinder_destroy(&c->mf);
	ALIGNED_FREE(c);
}

const struct compressor_ops lzms_compressor_ops = {
	.get_needed_memory  = lzms_get_needed_memory,
	.create_compressor  = lzms_create_compressor,
	.compress	    = lzms_compress,
	.free_compressor    = lzms_free_compressor,
};
