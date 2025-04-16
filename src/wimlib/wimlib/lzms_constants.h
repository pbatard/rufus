/*
 * lzms_constants.h
 *
 * Constants for the LZMS compression format.
 */

#ifndef _LZMS_CONSTANTS_H
#define _LZMS_CONSTANTS_H

/* Limits on match lengths and offsets (in bytes)  */
#define LZMS_MIN_MATCH_LENGTH			1
#define LZMS_MAX_MATCH_LENGTH			1073809578
#define LZMS_MIN_MATCH_OFFSET			1
#define LZMS_MAX_MATCH_OFFSET			1180427428

/* The value to which buffer sizes should be limited.  Microsoft's
 * implementation seems to use 67108864 (2^26) bytes.  However, since the format
 * itself supports higher match lengths and offsets, we'll use 2^30 bytes.  */
#define LZMS_MAX_BUFFER_SIZE			1073741824

/* The length of each LRU queue for match sources:
 *
 *    1. offsets of LZ matches
 *    2. (power, raw offset) pairs of delta matches */
#define LZMS_NUM_LZ_REPS			3
#define LZMS_NUM_DELTA_REPS			3

/* The numbers of binary decision classes needed for encoding queue indices  */
#define LZMS_NUM_LZ_REP_DECISIONS		(LZMS_NUM_LZ_REPS - 1)
#define LZMS_NUM_DELTA_REP_DECISIONS		(LZMS_NUM_DELTA_REPS - 1)

/* These are the numbers of probability entries for each binary decision class.
 * The logarithm base 2 of each of these values is the number of recently
 * encoded bits that are remembered for each decision class and are used to
 * select the appropriate probability entry when decoding/encoding the next
 * binary decision (bit).  */
#define LZMS_NUM_MAIN_PROBS			16
#define LZMS_NUM_MATCH_PROBS			32
#define LZMS_NUM_LZ_PROBS			64
#define LZMS_NUM_LZ_REP_PROBS			64
#define LZMS_NUM_DELTA_PROBS			64
#define LZMS_NUM_DELTA_REP_PROBS		64

/* These values define the precision for probabilities in LZMS, which are always
 * given as a numerator; the denominator is implied.  */
#define LZMS_PROBABILITY_BITS			6
#define LZMS_PROBABILITY_DENOMINATOR		(1 << LZMS_PROBABILITY_BITS)

/* These values define the initial state of each probability entry.  */
#define LZMS_INITIAL_PROBABILITY		48
#define LZMS_INITIAL_RECENT_BITS		0x0000000055555555

/* The number of symbols in each Huffman-coded alphabet  */
#define LZMS_NUM_LITERAL_SYMS			256
#define LZMS_NUM_LENGTH_SYMS			54
#define LZMS_NUM_DELTA_POWER_SYMS		8
#define LZMS_MAX_NUM_OFFSET_SYMS		799
#define LZMS_MAX_NUM_SYMS			799

/* The rebuild frequencies, in symbols, for each Huffman code  */
#define LZMS_LITERAL_CODE_REBUILD_FREQ		1024
#define LZMS_LZ_OFFSET_CODE_REBUILD_FREQ	1024
#define LZMS_LENGTH_CODE_REBUILD_FREQ		512
#define LZMS_DELTA_OFFSET_CODE_REBUILD_FREQ	1024
#define LZMS_DELTA_POWER_CODE_REBUILD_FREQ	512

/* The maximum length, in bits, of any Huffman codeword.  This is guaranteed by
 * the way frequencies are updated.  */
#define LZMS_MAX_CODEWORD_LENGTH		15

/* The maximum number of verbatim bits, in addition to the Huffman-encoded
 * length slot symbol, that may be required to encode a match length  */
#define LZMS_MAX_EXTRA_LENGTH_BITS		30

/* The maximum number of verbatim bits, in addition to the Huffman-encoded
 * offset slot symbol, that may be required to encode a match offset  */
#define LZMS_MAX_EXTRA_OFFSET_BITS		30

/* Parameters for x86 machine code pre/post-processing  */
#define LZMS_X86_ID_WINDOW_SIZE			65535
#define LZMS_X86_MAX_TRANSLATION_OFFSET		1023

#endif /* _LZMS_CONSTANTS_H */
