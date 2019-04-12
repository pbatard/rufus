/*
 * bb_inode.c --- routines to update the bad block inode.
 *
 * WARNING: This routine modifies a lot of state in the filesystem; if
 * this routine returns an error, the bad block inode may be in an
 * inconsistent state.
 *
 * Copyright (C) 1994, 1995 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <time.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"

struct set_badblock_record {
	ext2_badblocks_iterate	bb_iter;
	int		bad_block_count;
	blk_t		*ind_blocks;
	int		max_ind_blocks;
	int		ind_blocks_size;
	int		ind_blocks_ptr;
	char		*block_buf;
	errcode_t	err;
};

static int set_bad_block_proc(ext2_filsys fs, blk_t *block_nr,
			      e2_blkcnt_t blockcnt,
			      blk_t ref_block, int ref_offset,
			      void *priv_data);
static int clear_bad_block_proc(ext2_filsys fs, blk_t *block_nr,
				e2_blkcnt_t blockcnt,
				blk_t ref_block, int ref_offset,
				void *priv_data);

/*
 * Given a bad blocks bitmap, update the bad blocks inode to reflect
 * the map.
 */
errcode_t ext2fs_update_bb_inode(ext2_filsys fs, ext2_badblocks_list bb_list)
{
	errcode_t			retval;
	struct set_badblock_record 	rec;
	struct ext2_inode		inode;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!fs->block_map)
		return EXT2_ET_NO_BLOCK_BITMAP;

	memset(&rec, 0, sizeof(rec));
	rec.max_ind_blocks = 10;
	retval = ext2fs_get_array(rec.max_ind_blocks, sizeof(blk_t),
				&rec.ind_blocks);
	if (retval)
		return retval;
	memset(rec.ind_blocks, 0, rec.max_ind_blocks * sizeof(blk_t));
	retval = ext2fs_get_mem(fs->blocksize, &rec.block_buf);
	if (retval)
		goto cleanup;
	memset(rec.block_buf, 0, fs->blocksize);
	rec.err = 0;

	/*
	 * First clear the old bad blocks (while saving the indirect blocks)
	 */
	retval = ext2fs_block_iterate2(fs, EXT2_BAD_INO,
				       BLOCK_FLAG_DEPTH_TRAVERSE, 0,
				       clear_bad_block_proc, &rec);
	if (retval)
		goto cleanup;
	if (rec.err) {
		retval = rec.err;
		goto cleanup;
	}

	/*
	 * Now set the bad blocks!
	 *
	 * First, mark the bad blocks as used.  This prevents a bad
	 * block from being used as an indirect block for the bad
	 * block inode (!).
	 */
	if (bb_list) {
		retval = ext2fs_badblocks_list_iterate_begin(bb_list,
							     &rec.bb_iter);
		if (retval)
			goto cleanup;
		retval = ext2fs_block_iterate2(fs, EXT2_BAD_INO,
					       BLOCK_FLAG_APPEND, 0,
					       set_bad_block_proc, &rec);
		ext2fs_badblocks_list_iterate_end(rec.bb_iter);
		if (retval)
			goto cleanup;
		if (rec.err) {
			retval = rec.err;
			goto cleanup;
		}
	}

	/*
	 * Update the bad block inode's mod time and block count
	 * field.
	 */
	retval = ext2fs_read_inode(fs, EXT2_BAD_INO, &inode);
	if (retval)
		goto cleanup;

	inode.i_atime = inode.i_mtime = fs->now ? fs->now : time(0);
	if (!inode.i_ctime)
		inode.i_ctime = fs->now ? fs->now : time(0);
	ext2fs_iblk_set(fs, &inode, rec.bad_block_count);
	retval = ext2fs_inode_size_set(fs, &inode,
				       rec.bad_block_count * fs->blocksize);
	if (retval)
		goto cleanup;

	retval = ext2fs_write_inode(fs, EXT2_BAD_INO, &inode);
	if (retval)
		goto cleanup;

cleanup:
	ext2fs_free_mem(&rec.ind_blocks);
	ext2fs_free_mem(&rec.block_buf);
	return retval;
}

/*
 * Helper function for update_bb_inode()
 *
 * Clear the bad blocks in the bad block inode, while saving the
 * indirect blocks.
 */
#ifdef __TURBOC__
 #pragma argsused
#endif
static int clear_bad_block_proc(ext2_filsys fs, blk_t *block_nr,
				e2_blkcnt_t blockcnt,
				blk_t ref_block EXT2FS_ATTR((unused)),
				int ref_offset EXT2FS_ATTR((unused)),
				void *priv_data)
{
	struct set_badblock_record *rec = (struct set_badblock_record *)
		priv_data;
	errcode_t	retval;
	unsigned long 	old_size;

	if (!*block_nr)
		return 0;

	/*
	 * If the block number is outrageous, clear it and ignore it.
	 */
	if (*block_nr >= ext2fs_blocks_count(fs->super) ||
	    *block_nr < fs->super->s_first_data_block) {
		*block_nr = 0;
		return BLOCK_CHANGED;
	}

	if (blockcnt < 0) {
		if (rec->ind_blocks_size >= rec->max_ind_blocks) {
			old_size = rec->max_ind_blocks * sizeof(blk_t);
			rec->max_ind_blocks += 10;
			retval = ext2fs_resize_mem(old_size,
				   rec->max_ind_blocks * sizeof(blk_t),
				   &rec->ind_blocks);
			if (retval) {
				rec->max_ind_blocks -= 10;
				rec->err = retval;
				return BLOCK_ABORT;
			}
		}
		rec->ind_blocks[rec->ind_blocks_size++] = *block_nr;
	}

	/*
	 * Mark the block as unused, and update accounting information
	 */
	ext2fs_block_alloc_stats2(fs, *block_nr, -1);

	*block_nr = 0;
	return BLOCK_CHANGED;
}


/*
 * Helper function for update_bb_inode()
 *
 * Set the block list in the bad block inode, using the supplied bitmap.
 */
#ifdef __TURBOC__
 #pragma argsused
#endif
static int set_bad_block_proc(ext2_filsys fs, blk_t *block_nr,
			      e2_blkcnt_t blockcnt,
			      blk_t ref_block EXT2FS_ATTR((unused)),
			      int ref_offset EXT2FS_ATTR((unused)),
			      void *priv_data)
{
	struct set_badblock_record *rec = (struct set_badblock_record *)
		priv_data;
	errcode_t	retval;
	blk_t		blk;

	if (blockcnt >= 0) {
		/*
		 * Get the next bad block.
		 */
		if (!ext2fs_badblocks_list_iterate(rec->bb_iter, &blk))
			return BLOCK_ABORT;
		rec->bad_block_count++;
	} else {
		/*
		 * An indirect block; fetch a block from the
		 * previously used indirect block list.  The block
		 * most be not marked as used; if so, get another one.
		 * If we run out of reserved indirect blocks, allocate
		 * a new one.
		 */
	retry:
		if (rec->ind_blocks_ptr < rec->ind_blocks_size) {
			blk = rec->ind_blocks[rec->ind_blocks_ptr++];
			if (ext2fs_test_block_bitmap2(fs->block_map, blk))
				goto retry;
		} else {
			retval = ext2fs_new_block(fs, 0, 0, &blk);
			if (retval) {
				rec->err = retval;
				return BLOCK_ABORT;
			}
		}
		retval = io_channel_write_blk64(fs->io, blk, 1, rec->block_buf);
		if (retval) {
			rec->err = retval;
			return BLOCK_ABORT;
		}
	}

	/*
	 * Update block counts
	 */
	ext2fs_block_alloc_stats2(fs, blk, +1);

	*block_nr = blk;
	return BLOCK_CHANGED;
}






