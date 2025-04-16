/*
 * lzx_constants.h
 *
 * Constants for the LZX compression format.
 */

#ifndef _LZX_CONSTANTS_H
#define _LZX_CONSTANTS_H

/* Number of literal byte values.  */
#define LZX_NUM_CHARS	256

/* The smallest and largest allowed match lengths.  */
#define LZX_MIN_MATCH_LEN	2
#define LZX_MAX_MATCH_LEN	257

/* Number of distinct match lengths that can be represented.  */
#define LZX_NUM_LENS		(LZX_MAX_MATCH_LEN - LZX_MIN_MATCH_LEN + 1)

/* Number of match lengths for which no length symbol is required.  */
#define LZX_NUM_PRIMARY_LENS	7
#define LZX_NUM_LEN_HEADERS	(LZX_NUM_PRIMARY_LENS + 1)

/* The first length which requires a length symbol.  */
#define LZX_MIN_SECONDARY_LEN	(LZX_MIN_MATCH_LEN + LZX_NUM_PRIMARY_LENS)

/* Valid values of the 3-bit block type field.  */
#define LZX_BLOCKTYPE_VERBATIM       1
#define LZX_BLOCKTYPE_ALIGNED        2
#define LZX_BLOCKTYPE_UNCOMPRESSED   3

/* 'LZX_MIN_WINDOW_SIZE' and 'LZX_MAX_WINDOW_SIZE' are the minimum and maximum
 * sizes of the sliding window.  */
#define LZX_MIN_WINDOW_ORDER	15
#define LZX_MAX_WINDOW_ORDER	21
#define LZX_MIN_WINDOW_SIZE	(1UL << LZX_MIN_WINDOW_ORDER)  /* 32768   */
#define LZX_MAX_WINDOW_SIZE	(1UL << LZX_MAX_WINDOW_ORDER)  /* 2097152 */

/* Maximum number of offset slots.  (The actual number of offset slots depends
 * on the window size.)  */
#define LZX_MAX_OFFSET_SLOTS	50

/* Maximum number of symbols in the main code.  (The actual number of symbols in
 * the main code depends on the window size.)  */
#define LZX_MAINCODE_MAX_NUM_SYMBOLS	\
	(LZX_NUM_CHARS + (LZX_MAX_OFFSET_SLOTS * LZX_NUM_LEN_HEADERS))

/* Number of symbols in the length code.  */
#define LZX_LENCODE_NUM_SYMBOLS		(LZX_NUM_LENS - LZX_NUM_PRIMARY_LENS)

/* Number of symbols in the pre-code.  */
#define LZX_PRECODE_NUM_SYMBOLS		20

/* Number of bits in which each pre-code codeword length is represented.  */
#define LZX_PRECODE_ELEMENT_SIZE	4

/* Number of low-order bits of each match offset that are entropy-encoded in
 * aligned offset blocks.  */
#define LZX_NUM_ALIGNED_OFFSET_BITS	3

/* Number of symbols in the aligned offset code.  */
#define LZX_ALIGNEDCODE_NUM_SYMBOLS	(1 << LZX_NUM_ALIGNED_OFFSET_BITS)

/* Mask for the match offset bits that are entropy-encoded in aligned offset
 * blocks.  */
#define LZX_ALIGNED_OFFSET_BITMASK	((1 << LZX_NUM_ALIGNED_OFFSET_BITS) - 1)

/* Number of bits in which each aligned offset codeword length is represented.  */
#define LZX_ALIGNEDCODE_ELEMENT_SIZE	3

/* The first offset slot which requires an aligned offset symbol in aligned
 * offset blocks.  */
#define LZX_MIN_ALIGNED_OFFSET_SLOT	8

/* The offset slot base for LZX_MIN_ALIGNED_OFFSET_SLOT.  */
#define LZX_MIN_ALIGNED_OFFSET		14

/* The maximum number of extra offset bits in verbatim blocks.  (One would need
 * to subtract LZX_NUM_ALIGNED_OFFSET_BITS to get the number of extra offset
 * bits in *aligned* blocks.)  */
#define LZX_MAX_NUM_EXTRA_BITS		17

/* Maximum lengths (in bits) for length-limited Huffman code construction.  */
#define LZX_MAX_MAIN_CODEWORD_LEN	16
#define LZX_MAX_LEN_CODEWORD_LEN	16
#define LZX_MAX_PRE_CODEWORD_LEN	((1 << LZX_PRECODE_ELEMENT_SIZE) - 1)
#define LZX_MAX_ALIGNED_CODEWORD_LEN	((1 << LZX_ALIGNEDCODE_ELEMENT_SIZE) - 1)

/* For LZX-compressed blocks in WIM resources, this value is always used as the
 * filesize parameter for the call instruction (0xe8 byte) preprocessing, even
 * though the blocks themselves are not this size, and the size of the actual
 * file resource in the WIM file is very likely to be something entirely
 * different as well.  */
#define LZX_WIM_MAGIC_FILESIZE	12000000

/* Assumed LZX block size when the encoded block size begins with a 0 bit.
 * This is probably WIM-specific.  */
#define LZX_DEFAULT_BLOCK_SIZE	32768

/* Number of offsets in the recent (or "repeat") offsets queue.  */
#define LZX_NUM_RECENT_OFFSETS	3

/* An offset of n bytes is actually encoded as (n + LZX_OFFSET_ADJUSTMENT).  */
#define LZX_OFFSET_ADJUSTMENT	(LZX_NUM_RECENT_OFFSETS - 1)

#endif /* _LZX_CONSTANTS_H */
