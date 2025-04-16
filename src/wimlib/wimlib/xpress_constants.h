/*
 * xpress_constants.h
 *
 * Constants for the XPRESS compression format.
 */

#ifndef _XPRESS_CONSTANTS_H
#define _XPRESS_CONSTANTS_H

#define XPRESS_NUM_CHARS	256
#define XPRESS_NUM_SYMBOLS	512
#define XPRESS_MAX_CODEWORD_LEN	15

#define XPRESS_END_OF_DATA	256

#define XPRESS_MIN_OFFSET	1
#define XPRESS_MAX_OFFSET	65535

#define XPRESS_MIN_MATCH_LEN	3
#define XPRESS_MAX_MATCH_LEN	65538

#endif /* _XPRESS_CONSTANTS_H */
