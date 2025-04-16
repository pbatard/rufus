/*
 * lzx_common.c - Common code for LZX compression and decompression.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#ifdef __SSE2__
#  include <emmintrin.h>
#endif

#ifdef __AVX2__
#  include <immintrin.h>
#endif

#include "wimlib/bitops.h"
#include "wimlib/endianness.h"
#include "wimlib/lzx_common.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"

/* Mapping: offset slot => first match offset that uses that offset slot.
 * The offset slots for repeat offsets map to "fake" offsets < 1.  */
const s32 lzx_offset_slot_base[LZX_MAX_OFFSET_SLOTS + 1] = {
        -2     , -1     , 0      , 1      , 2      ,    /* 0  --- 4  */
        4      , 6      , 10     , 14     , 22     ,    /* 5  --- 9  */
        30     , 46     , 62     , 94     , 126    ,    /* 10 --- 14 */
        190    , 254    , 382    , 510    , 766    ,    /* 15 --- 19 */
        1022   , 1534   , 2046   , 3070   , 4094   ,    /* 20 --- 24 */
        6142   , 8190   , 12286  , 16382  , 24574  ,    /* 25 --- 29 */
        32766  , 49150  , 65534  , 98302  , 131070 ,    /* 30 --- 34 */
        196606 , 262142 , 393214 , 524286 , 655358 ,    /* 35 --- 39 */
        786430 , 917502 , 1048574, 1179646, 1310718,    /* 40 --- 44 */
        1441790, 1572862, 1703934, 1835006, 1966078,    /* 45 --- 49 */
        2097150                                         /* extra     */
};

/* Mapping: offset slot => how many extra bits must be read and added to the
 * corresponding offset slot base to decode the match offset.  */
const u8 lzx_extra_offset_bits[LZX_MAX_OFFSET_SLOTS] = {
	0 , 0 , 0 , 0 , 1 ,
	1 , 2 , 2 , 3 , 3 ,
	4 , 4 , 5 , 5 , 6 ,
	6 , 7 , 7 , 8 , 8 ,
	9 , 9 , 10, 10, 11,
	11, 12, 12, 13, 13,
	14, 14, 15, 15, 16,
	16, 17, 17, 17, 17,
	17, 17, 17, 17, 17,
	17, 17, 17, 17, 17,
};

/* Round the specified buffer size up to the next valid LZX window size, and
 * return its order (log2).  Or, if the buffer size is 0 or greater than the
 * largest valid LZX window size, return 0.  */
unsigned
lzx_get_window_order(size_t max_bufsize)
{
	if (max_bufsize == 0 || max_bufsize > LZX_MAX_WINDOW_SIZE)
		return 0;

	return max(ilog2_ceil(max_bufsize), LZX_MIN_WINDOW_ORDER);
}

/* Given a valid LZX window order, return the number of symbols that will exist
 * in the main Huffman code.  */
unsigned
lzx_get_num_main_syms(unsigned window_order)
{
	/* Note: one would expect that the maximum match offset would be
	 * 'window_size - LZX_MIN_MATCH_LEN', which would occur if the first two
	 * bytes were to match the last two bytes.  However, the format
	 * disallows this case.  This reduces the number of needed offset slots
	 * by 1.  */
	u32 window_size = (u32)1 << window_order;
	u32 max_offset = window_size - LZX_MIN_MATCH_LEN - 1;
	unsigned num_offset_slots = 30;
	while (max_offset >= lzx_offset_slot_base[num_offset_slots])
		num_offset_slots++;

	return LZX_NUM_CHARS + (num_offset_slots * LZX_NUM_LEN_HEADERS);
}

static void
do_translate_target(void *target, s32 input_pos)
{
	s32 abs_offset, rel_offset;

	rel_offset = get_unaligned_le32(target);
	if (rel_offset >= -input_pos && rel_offset < LZX_WIM_MAGIC_FILESIZE) {
		if (rel_offset < LZX_WIM_MAGIC_FILESIZE - input_pos) {
			/* "good translation" */
			abs_offset = rel_offset + input_pos;
		} else {
			/* "compensating translation" */
			abs_offset = rel_offset - LZX_WIM_MAGIC_FILESIZE;
		}
		put_unaligned_le32(abs_offset, target);
	}
}

static void
undo_translate_target(void *target, s32 input_pos)
{
	s32 abs_offset, rel_offset;

	abs_offset = get_unaligned_le32(target);
	if (abs_offset >= 0) {
		if (abs_offset < LZX_WIM_MAGIC_FILESIZE) {
			/* "good translation" */
			rel_offset = abs_offset - input_pos;
			put_unaligned_le32(rel_offset, target);
		}
	} else {
		if (abs_offset >= -input_pos) {
			/* "compensating translation" */
			rel_offset = abs_offset + LZX_WIM_MAGIC_FILESIZE;
			put_unaligned_le32(rel_offset, target);
		}
	}
}

/*
 * Do or undo the 'E8' preprocessing used in LZX.  Before compression, the
 * uncompressed data is preprocessed by changing the targets of x86 CALL
 * instructions from relative offsets to absolute offsets.  After decompression,
 * the translation is undone by changing the targets of x86 CALL instructions
 * from absolute offsets to relative offsets.
 *
 * Note that despite its intent, E8 preprocessing can be done on any data even
 * if it is not actually x86 machine code.  In fact, E8 preprocessing appears to
 * always be used in LZX-compressed resources in WIM files; there is no bit to
 * indicate whether it is used or not, unlike in the LZX compressed format as
 * used in cabinet files, where a bit is reserved for that purpose.
 *
 * E8 preprocessing is disabled in the last 6 bytes of the uncompressed data,
 * which really means the 5-byte call instruction cannot start in the last 10
 * bytes of the uncompressed data.  This is one of the errors in the LZX
 * documentation.
 *
 * E8 preprocessing does not appear to be disabled after the 32768th chunk of a
 * WIM resource, which apparently is another difference from the LZX compression
 * used in cabinet files.
 *
 * E8 processing is supposed to take the file size as a parameter, as it is used
 * in calculating the translated jump targets.  But in WIM files, this file size
 * is always the same (LZX_WIM_MAGIC_FILESIZE == 12000000).
 */
static void
lzx_e8_filter(u8 *data, u32 size, void (*process_target)(void *, s32))
{

#if !defined(__SSE2__) && !defined(__AVX2__)
	/*
	 * A worthwhile optimization is to push the end-of-buffer check into the
	 * relatively rare E8 case.  This is possible if we replace the last six
	 * bytes of data with E8 bytes; then we are guaranteed to hit an E8 byte
	 * before reaching end-of-buffer.  In addition, this scheme guarantees
	 * that no translation can begin following an E8 byte in the last 10
	 * bytes because a 4-byte offset containing E8 as its high byte is a
	 * large negative number that is not valid for translation.  That is
	 * exactly what we need.
	 */
	u8 *tail;
	u8 saved_bytes[6];
	u8 *p;

	if (size <= 10)
		return;

	tail = &data[size - 6];
	memcpy(saved_bytes, tail, 6);
	memset(tail, 0xE8, 6);
	p = data;
	for (;;) {
		while (*p != 0xE8)
			p++;
		if (p >= tail)
			break;
		(*process_target)(p + 1, p - data);
		p += 5;
	}
	memcpy(tail, saved_bytes, 6);
#else
	/* SSE2 or AVX-2 optimized version for x86_64  */

	u8 *p = data;
	u64 valid_mask = ~0;

	if (size <= 10)
		return;
#ifdef __AVX2__
#  define ALIGNMENT_REQUIRED 32
#else
#  define ALIGNMENT_REQUIRED 16
#endif

	/* Process one byte at a time until the pointer is properly aligned.  */
	while ((uintptr_t)p % ALIGNMENT_REQUIRED != 0) {
		if (p >= data + size - 10)
			return;
		if (*p == 0xE8 && (valid_mask & 1)) {
			(*process_target)(p + 1, p - data);
			valid_mask &= ~0x1F;
		}
		p++;
		valid_mask >>= 1;
		valid_mask |= (u64)1 << 63;
	}

	if (data + size - p >= 64) {

		/* Vectorized processing  */

		/* Note: we use a "trap" E8 byte to eliminate the need to check
		 * for end-of-buffer in the inner loop.  This byte is carefully
		 * positioned so that it will never be changed by a previous
		 * translation before it is detected.  */

		u8 *trap = p + ((data + size - p) & ~31) - 32 + 4;
		u8 saved_byte = *trap;
		*trap = 0xE8;

		for (;;) {
			u32 e8_mask;
			u8 *orig_p = p;
		#ifdef __AVX2__
			const __m256i e8_bytes = _mm256_set1_epi8(0xE8);
			for (;;) {
				__m256i bytes = *(const __m256i *)p;
				__m256i cmpresult = _mm256_cmpeq_epi8(bytes, e8_bytes);
				e8_mask = _mm256_movemask_epi8(cmpresult);
				if (e8_mask)
					break;
				p += 32;
			}
		#else
			const __m128i e8_bytes = _mm_set1_epi8(0xE8);
			for (;;) {
				/* Read the next 32 bytes of data and test them
				 * for E8 bytes.  */
				__m128i bytes1 = *(const __m128i *)p;
				__m128i bytes2 = *(const __m128i *)(p + 16);
				__m128i cmpresult1 = _mm_cmpeq_epi8(bytes1, e8_bytes);
				__m128i cmpresult2 = _mm_cmpeq_epi8(bytes2, e8_bytes);
				u32 mask1 = _mm_movemask_epi8(cmpresult1);
				u32 mask2 = _mm_movemask_epi8(cmpresult2);
				/* The masks have a bit set for each E8 byte.
				 * We stay in this fast inner loop as long as
				 * there are no E8 bytes.  */
				if (mask1 | mask2) {
					e8_mask = mask1 | (mask2 << 16);
					break;
				}
				p += 32;
			}
		#endif

			/* Did we pass over data with no E8 bytes?  */
			if (p != orig_p)
				valid_mask = ~0;

			/* Are we nearing end-of-buffer?  */
			if (p == trap - 4)
				break;

			/* Process the E8 bytes.  However, the AND with
			 * 'valid_mask' ensures we never process an E8 byte that
			 * was itself part of a translation target.  */
			while ((e8_mask &= valid_mask)) {
				unsigned bit = bsf32(e8_mask);
				(*process_target)(p + bit + 1, p + bit - data);
				valid_mask &= ~((u64)0x1F << bit);
			}

			valid_mask >>= 32;
			valid_mask |= 0xFFFFFFFF00000000;
			p += 32;
		}

		*trap = saved_byte;
	}

	/* Approaching the end of the buffer; process one byte a time.  */
	while (p < data + size - 10) {
		if (*p == 0xE8 && (valid_mask & 1)) {
			(*process_target)(p + 1, p - data);
			valid_mask &= ~0x1F;
		}
		p++;
		valid_mask >>= 1;
		valid_mask |= (u64)1 << 63;
	}
#endif /* __SSE2__ || __AVX2__ */
}

void
lzx_preprocess(u8 *data, u32 size)
{
	lzx_e8_filter(data, size, do_translate_target);
}

void
lzx_postprocess(u8 *data, u32 size)
{
	lzx_e8_filter(data, size, undo_translate_target);
}
