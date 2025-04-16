/*
 * lcpit_matchfinder.h
 *
 * A match-finder for Lempel-Ziv compression based on bottom-up construction and
 * traversal of the Longest Common Prefix (LCP) interval tree.
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _LCPIT_MATCHFINDER_H
#define _LCPIT_MATCHFINDER_H

#include "wimlib/types.h"

struct lcpit_matchfinder {
	bool huge_mode;
	u32 cur_pos;
	u32 *pos_data;
	union {
		u32 *intervals;
		u64 *intervals64;
	};
	u32 min_match_len;
	u32 nice_match_len;
	u32 next[2];
	u32 orig_nice_match_len;
};

struct lz_match {
	u32 length;
	u32 offset;
};

u64
lcpit_matchfinder_get_needed_memory(size_t max_bufsize);

bool
lcpit_matchfinder_init(struct lcpit_matchfinder *mf, size_t max_bufsize,
		       u32 min_match_len, u32 nice_match_len);

void
lcpit_matchfinder_load_buffer(struct lcpit_matchfinder *mf, const u8 *T, u32 n);

u32
lcpit_matchfinder_get_matches(struct lcpit_matchfinder *mf,
                              struct lz_match *matches);

void
lcpit_matchfinder_skip_bytes(struct lcpit_matchfinder *mf, u32 count);

void
lcpit_matchfinder_destroy(struct lcpit_matchfinder *mf);

#endif /* _LCPIT_MATCHFINDER_H */
