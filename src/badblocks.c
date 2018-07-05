/*
 * badblocks.c		- Bad blocks checker
 *
 * Copyright (C) 1992, 1993, 1994  Remy Card <card@masi.ibp.fr>
 *                                 Laboratoire MASI, Institut Blaise Pascal
 *                                 Universite Pierre et Marie Curie (Paris VI)
 *
 * Copyright 1995, 1996, 1997, 1998, 1999 by Theodore Ts'o
 * Copyright 1999 by David Beattie
 * Copyright 2011-2018 by Pete Batard
 *
 * This file is based on the minix file system programs fsck and mkfs
 * written and copyrighted by Linus Torvalds <Linus.Torvalds@cs.helsinki.fi>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

/*
 * History:
 * 93/05/26	- Creation from e2fsck
 * 94/02/27	- Made a separate bad blocks checker
 * 99/06/30...99/07/26 - Added non-destructive write-testing,
 *                       configurable blocks-at-once parameter,
 * 			 loading of badblocks list to avoid testing
 * 			 blocks known to be bad, multiple passes to
 * 			 make sure that no new blocks are added to the
 * 			 list.  (Work done by David Beattie)
 * 11/12/04	- Windows/Rufus integration (Pete Batard)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include <windows.h>
#include <stdint.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "badblocks.h"
#include "file.h"

FILE* log_fd = NULL;
static const char abort_msg[] = "Too many bad blocks, aborting test\n";
static const char bb_prefix[] = "Bad Blocks: ";

/*
 *From e2fsprogs/lib/ext2fs/badblocks.c
 */

/*
 * Badblocks list
 */
struct bb_struct_u64_list {
	int   magic;
	int   num;
	int   size;
	uint64_t *list;
	int   badblocks_flags;
};

struct bb_struct_u64_iterate {
	int         magic;
	bb_u64_list bb;
	int         ptr;
};

static errcode_t make_u64_list(int size, int num, uint64_t *list, bb_u64_list *ret)
{
	bb_u64_list bb;

	bb = calloc(1, sizeof(struct bb_struct_u64_list));
	if (bb == NULL)
		return BB_ET_NO_MEMORY;
	bb->magic = BB_ET_MAGIC_BADBLOCKS_LIST;
	bb->size = size ? size : 10;
	bb->num = num;
	bb->list = malloc(sizeof(blk_t) * bb->size);
	if (bb->list == NULL) {
		free(bb);
		bb = NULL;
		return BB_ET_NO_MEMORY;
	}
	if (list)
		memcpy(bb->list, list, bb->size * sizeof(blk_t));
	else
		memset(bb->list, 0, bb->size * sizeof(blk_t));
	*ret = bb;
	return 0;
}

/*
 * This procedure creates an empty badblocks list.
 */
static errcode_t bb_badblocks_list_create(bb_badblocks_list *ret, int size)
{
	return make_u64_list(size, 0, 0, (bb_badblocks_list *) ret);
}

/*
 * This procedure adds a block to a badblocks list.
 */
static errcode_t bb_u64_list_add(bb_u64_list bb, uint64_t blk)
{
	int		i, j;
	uint64_t* old_bb_list = bb->list;

	BB_CHECK_MAGIC(bb, BB_ET_MAGIC_BADBLOCKS_LIST);

	if (bb->num >= bb->size) {
		bb->size += 100;
		bb->list = realloc(bb->list, bb->size * sizeof(uint64_t));
		if (bb->list == NULL) {
			bb->list = old_bb_list;
			bb->size -= 100;
			return BB_ET_NO_MEMORY;
		}
		// coverity[suspicious_sizeof]
		memset(&bb->list[bb->size-100], 0, 100 * sizeof(uint64_t));
	}

	/*
	 * Add special case code for appending to the end of the list
	 */
	i = bb->num-1;
	if ((bb->num != 0) && (bb->list[i] == blk))
		return 0;
	if ((bb->num == 0) || (bb->list[i] < blk)) {
		bb->list[bb->num++] = blk;
		return 0;
	}

	j = bb->num;
	for (i=0; i < bb->num; i++) {
		if (bb->list[i] == blk)
			return 0;
		if (bb->list[i] > blk) {
			j = i;
			break;
		}
	}
	for (i=bb->num; i > j; i--)
		bb->list[i] = bb->list[i-1];
	bb->list[j] = blk;
	bb->num++;
	return 0;
}

static errcode_t bb_badblocks_list_add(bb_badblocks_list bb, blk_t blk)
{
	return bb_u64_list_add((bb_u64_list) bb, blk);
}

/*
 * This procedure finds a particular block is on a badblocks
 * list.
 */
static int bb_u64_list_find(bb_u64_list bb, uint64_t blk)
{
	int	low, high, mid;

	if (bb->magic != BB_ET_MAGIC_BADBLOCKS_LIST)
		return -1;

	if (bb->num == 0)
		return -1;

	low = 0;
	high = bb->num-1;
	if (blk == bb->list[low])
		return low;
	if (blk == bb->list[high])
		return high;

	while (low < high) {
		mid = ((unsigned)low + (unsigned)high)/2;
		if (mid == low || mid == high)
			break;
		if (blk == bb->list[mid])
			return mid;
		if (blk < bb->list[mid])
			high = mid;
		else
			low = mid;
	}
	return -1;
}

/*
 * This procedure tests to see if a particular block is on a badblocks
 * list.
 */
static int bb_u64_list_test(bb_u64_list bb, uint64_t blk)
{
	if (bb_u64_list_find(bb, blk) < 0)
		return 0;
	else
		return 1;
}

static int bb_badblocks_list_test(bb_badblocks_list bb, blk_t blk)
{
	return bb_u64_list_test((bb_u64_list) bb, blk);
}

static int bb_u64_list_iterate(bb_u64_iterate iter, uint64_t *blk)
{
	bb_u64_list bb;

	if (iter->magic != BB_ET_MAGIC_BADBLOCKS_ITERATE)
		return 0;

	bb = iter->bb;

	if (bb->magic != BB_ET_MAGIC_BADBLOCKS_LIST)
		return 0;

	if (iter->ptr < bb->num) {
		*blk = bb->list[iter->ptr++];
		return 1;
	}
	*blk = 0;
	return 0;
}

static int bb_badblocks_list_iterate(bb_badblocks_iterate iter, blk_t *blk)
{
	return bb_u64_list_iterate((bb_u64_iterate) iter, blk);
}

/*
 * from e2fsprogs/misc/badblocks.c
 */
static int v_flag = 1;					/* verbose */
static int s_flag = 1;					/* show progress of test */
static int cancel_ops = 0;				/* abort current operation */
static int cur_pattern, nr_pattern;
static int cur_op;
/* Abort test if more than this number of bad blocks has been encountered */
static unsigned int max_bb = BB_BAD_BLOCKS_THRESHOLD;
static blk_t currently_testing = 0;
static blk_t num_blocks = 0;
static uint32_t num_read_errors = 0;
static uint32_t num_write_errors = 0;
static uint32_t num_corruption_errors = 0;
static bb_badblocks_list bb_list = NULL;
static blk_t next_bad = 0;
static bb_badblocks_iterate bb_iter = NULL;

static __inline void *allocate_buffer(size_t size) {
	return _mm_malloc(size, BB_SYS_PAGE_SIZE);
}

static __inline void free_buffer(void* p) {
	_mm_free(p);
}

/*
 * This routine reports a new bad block.  If the bad block has already
 * been seen before, then it returns 0; otherwise it returns 1.
 */
static int bb_output (blk_t bad, enum error_types error_type)
{
	errcode_t error_code;

	if (bb_badblocks_list_test(bb_list, bad))
		return 0;

	uprintf("%s%lu\n", bb_prefix, (unsigned long)bad);
	fprintf(log_fd, "Block %lu: %s error\n", (unsigned long)bad, (error_type==READ_ERROR)?"read":
		((error_type == WRITE_ERROR)?"write":"corruption"));
	fflush(log_fd);

	error_code = bb_badblocks_list_add(bb_list, bad);
	if (error_code) {
		uprintf("%sError %d adding to in-memory bad block list", bb_prefix, error_code);
		return 0;
	}

	/* kludge:
	   increment the iteration through the bb_list if
	   an element was just added before the current iteration
	   position.  This should not cause next_bad to change. */
	if (bb_iter && bad < next_bad)
		bb_badblocks_list_iterate (bb_iter, &next_bad);

	if (error_type == READ_ERROR) {
	  num_read_errors++;
	} else if (error_type == WRITE_ERROR) {
	  num_write_errors++;
	} else if (error_type == CORRUPTION_ERROR) {
	  num_corruption_errors++;
	}
	return 1;
}

static float calc_percent(unsigned long current, unsigned long total) {
	float percent = 0.0;
	if (total <= 0)
		return percent;
	if (current >= total) {
		percent = 100.0f;
	} else {
		percent=(100.0f*(float)current/(float)total);
	}
	return percent;
}

static void print_status(void)
{
	float percent;

	percent = calc_percent((unsigned long) currently_testing,
					(unsigned long) num_blocks);
	PrintInfo(0, MSG_235, lmprintf(MSG_191 + ((cur_op==OP_WRITE)?0:1)),
				cur_pattern, nr_pattern,
				percent,
				num_read_errors,
				num_write_errors,
				num_corruption_errors);
	percent = (percent/2.0f) + ((cur_op==OP_READ)? 50.0f : 0.0f);
	UpdateProgress(OP_BADBLOCKS, (((cur_pattern-1)*100.0f) + percent) / nr_pattern);
}

static void CALLBACK alarm_intr(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	if (!num_blocks)
		return;
	if (FormatStatus) {
		uprintf("%sInterrupting at block %" PRIu64 "\n", bb_prefix,
			(unsigned long long) currently_testing);
		cancel_ops = -1;
	}
	print_status();
}

static void pattern_fill(unsigned char *buffer, unsigned int pattern,
			 size_t n)
{
	unsigned int	i, nb;
	unsigned char	bpattern[sizeof(pattern)], *ptr;

	if (pattern == (unsigned int) ~0) {
		PrintInfo(3500, MSG_236);
		srand((unsigned int)GetTickCount64());
		for (ptr = buffer; ptr < buffer + n; ptr++) {
			// coverity[dont_call]
			(*ptr) = rand() % (1 << (8 * sizeof(char)));
		}
	} else {
		PrintInfo(3500, MSG_237, pattern);
		bpattern[0] = 0;
		for (i = 0; i < sizeof(bpattern); i++) {
			if (pattern == 0)
				break;
			bpattern[i] = pattern & 0xFF;
			pattern = pattern >> 8;
		}
		nb = i ? (i-1) : 0;
		for (ptr = buffer, i = nb; ptr < buffer + n; ptr++) {
			*ptr = bpattern[i];
			if (i == 0)
				i = nb;
			else
				i--;
		}
		cur_pattern++;
	}
}

/*
 * Perform a read of a sequence of blocks; return the number of blocks
 *    successfully sequentially read.
 */
static int64_t do_read (HANDLE hDrive, unsigned char * buffer, uint64_t tryout, uint64_t block_size,
		    blk_t current_block)
{
	int64_t got;

	if (v_flag > 1)
		print_status();

	/* Try the read */
	got = read_sectors(hDrive, block_size, current_block, tryout, buffer);
	if (got < 0)
		got = 0;
	if (got & 511)
		uprintf("%sWeird value (%ld) in do_read\n", bb_prefix, got);
	got /= block_size;
	return got;
}

/*
 * Perform a write of a sequence of blocks; return the number of blocks
 *    successfully sequentially written.
 */
static int64_t do_write(HANDLE hDrive, unsigned char * buffer, uint64_t tryout, uint64_t block_size,
		    blk_t current_block)
{
	int64_t got;

	if (v_flag > 1)
		print_status();

	/* Try the write */
	got = write_sectors(hDrive, block_size, current_block, tryout, buffer);
	if (got < 0)
		got = 0;
	if (got & 511)
		uprintf("%sWeird value (%ld) in do_write\n", bb_prefix, got);
	got /= block_size;
	return got;
}

static unsigned int test_rw(HANDLE hDrive, blk_t last_block, size_t block_size, blk_t first_block,
	size_t blocks_at_once, int pattern_type, int nb_passes)
{
	const unsigned int pattern[BADLOCKS_PATTERN_TYPES][BADBLOCK_PATTERN_COUNT] =
		{ BADBLOCK_PATTERN_SLC, BADCLOCK_PATTERN_MLC, BADBLOCK_PATTERN_TLC };
	unsigned char *buffer = NULL, *read_buffer;
	int i, pat_idx;
	unsigned int bb_count = 0;
	blk_t got, tryout, recover_block = ~0, *blk_id;
	size_t id_offset = 0;

	if ((pattern_type < 0) || (pattern_type >= BADLOCKS_PATTERN_TYPES)) {
		uprintf("%sInvalid pattern type\n", bb_prefix);
		cancel_ops = -1;
		return 0;
	}
	if ((nb_passes < 1) || (nb_passes > BADBLOCK_PATTERN_COUNT)) {
		uprintf("%sInvalid number of passes\n", bb_prefix);
		cancel_ops = -1;
		return 0;
	}

	buffer = allocate_buffer(2 * blocks_at_once * block_size);
	read_buffer = buffer + blocks_at_once * block_size;

	if (!buffer) {
		uprintf("%sError while allocating buffers\n", bb_prefix);
		cancel_ops = -1;
		return 0;
	}

	uprintf("%sChecking from block %lu to %lu (1 block = %s)\n", bb_prefix,
		(unsigned long) first_block, (unsigned long) last_block - 1,
		SizeToHumanReadable(BADBLOCK_BLOCK_SIZE, FALSE, FALSE));
	nr_pattern = nb_passes;
	cur_pattern = 0;

	for (pat_idx = 0; pat_idx < nb_passes; pat_idx++) {
		if (cancel_ops)
			goto out;
		if (detect_fakes && (pat_idx == 0)) {
			srand((unsigned int)GetTickCount64());
			id_offset = rand() * (block_size - sizeof(blk_t)) / RAND_MAX;
			uprintf("%sUsing offset %d for fake device check\n", bb_prefix, id_offset);
		}
		// coverity[dont_call]
		pattern_fill(buffer, pattern[pattern_type][pat_idx], blocks_at_once * block_size);
		num_blocks = last_block - 1;
		currently_testing = first_block;
		if (s_flag | v_flag)
			uprintf("%sWriting test pattern 0x%02X\n", bb_prefix, pattern[pattern_type][pat_idx]);
		cur_op = OP_WRITE;
		tryout = blocks_at_once;
		while (currently_testing < last_block) {
			if (cancel_ops)
				goto out;
			if (max_bb && bb_count >= max_bb) {
				if (s_flag || v_flag) {
					uprintf(abort_msg);
					fprintf(log_fd, abort_msg);
					fflush(log_fd);
				}
				cancel_ops = -1;
				goto out;
			}
			if (currently_testing + tryout > last_block)
				tryout = last_block - currently_testing;
			if (detect_fakes && (pat_idx == 0)) {
				/* Add the block number at a fixed (random) offset during each pass to
				   allow for the detection of 'fake' media (eg. 2GB USB masquerading as 16GB) */
				for (i=0; i<(int)blocks_at_once; i++) {
					blk_id = (blk_t*)(intptr_t)(buffer + id_offset+ i*block_size);
					*blk_id = (blk_t)(currently_testing + i);
				}
			}
			got = do_write(hDrive, buffer, tryout, block_size, currently_testing);
			if (v_flag > 1)
				print_status();

			if (got == 0 && tryout == 1)
				bb_count += bb_output(currently_testing++, WRITE_ERROR);
			currently_testing += got;
			if (got != tryout) {
				tryout = 1;
				if (recover_block == ~0)
					recover_block = currently_testing -
						got + blocks_at_once;
				continue;
			} else if (currently_testing == recover_block) {
				tryout = blocks_at_once;
				recover_block = ~0;
			}
		}

		num_blocks = 0;
		if (s_flag | v_flag)
			uprintf("%sReading and comparing\n", bb_prefix);
		cur_op = OP_READ;
		num_blocks = last_block;
		currently_testing = first_block;

		tryout = blocks_at_once;
		while (currently_testing < last_block) {
			if (cancel_ops) goto out;
			if (max_bb && bb_count >= max_bb) {
				if (s_flag || v_flag) {
					uprintf(abort_msg);
					fprintf(log_fd, abort_msg);
					fflush(log_fd);
				}
				cancel_ops = -1;
				goto out;
			}
			if (currently_testing + tryout > last_block)
				tryout = last_block - currently_testing;
			if (detect_fakes && (pat_idx == 0)) {
				for (i=0; i<(int)blocks_at_once; i++) {
					blk_id = (blk_t*)(intptr_t)(buffer + id_offset+ i*block_size);
					*blk_id = (blk_t)(currently_testing + i);
				}
			}
			got = do_read(hDrive, read_buffer, tryout, block_size,
				       currently_testing);
			if (got == 0 && tryout == 1)
				bb_count += bb_output(currently_testing++, READ_ERROR);
			currently_testing += got;
			if (got != tryout) {
				tryout = 1;
				if (recover_block == ~0)
					recover_block = currently_testing -
						got + blocks_at_once;
				continue;
			} else if (currently_testing == recover_block) {
				tryout = blocks_at_once;
				recover_block = ~0;
			}
			for (i=0; i < got; i++) {
				if (memcmp(read_buffer + i * block_size,
					   buffer + i * block_size,
					   block_size))
					bb_count += bb_output(currently_testing+i-got, CORRUPTION_ERROR);
			}
			if (v_flag > 1)
				print_status();
		}

		num_blocks = 0;
	}
out:
	free_buffer(buffer);
	return bb_count;
}

BOOL BadBlocks(HANDLE hPhysicalDrive, ULONGLONG disk_size, int nb_passes,
	int flash_type, badblocks_report *report, FILE* fd)
{
	errcode_t error_code;
	blk_t last_block = disk_size / BADBLOCK_BLOCK_SIZE;

	if (report == NULL) return FALSE;
	num_read_errors = 0;
	num_write_errors = 0;
	num_corruption_errors = 0;
	report->bb_count = 0;
	if (fd != NULL) {
		log_fd = fd;
	} else {
		log_fd = freopen(NULL, "w", stderr);
	}

	error_code = bb_badblocks_list_create(&bb_list, 0);
	if (error_code) {
		uprintf("%sError %d while creating in-memory bad blocks list", bb_prefix, error_code);
		return FALSE;
	}

	cancel_ops = 0;
	/* use a timer to update status every second */
	SetTimer(hMainDialog, TID_BADBLOCKS_UPDATE, 1000, alarm_intr);
	report->bb_count = test_rw(hPhysicalDrive, last_block, BADBLOCK_BLOCK_SIZE, 0, BB_BLOCKS_AT_ONCE, flash_type, nb_passes);
	KillTimer(hMainDialog, TID_BADBLOCKS_UPDATE);
	free(bb_list->list);
	free(bb_list);
	report->num_read_errors = num_read_errors;
	report->num_write_errors = num_write_errors;
	report->num_corruption_errors = num_corruption_errors;

	if ((cancel_ops) && (!report->bb_count))
		return FALSE;
	return TRUE;
}
