/*
 * alloc_stats.c --- Update allocation statistics for ext2fs
 *
 * Copyright (C) 2001 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>

#include "ext2_fs.h"
#include "ext2fs.h"

void ext2fs_inode_alloc_stats2(ext2_filsys fs, ext2_ino_t ino,
			       int inuse, int isdir)
{
	int	group = ext2fs_group_of_ino(fs, ino);

	if (ino > fs->super->s_inodes_count) {
#ifndef OMIT_COM_ERR
		com_err("ext2fs_inode_alloc_stats2", 0,
			"Illegal inode number: %lu", (unsigned long) ino);
#endif
		return;
	}
	if (inuse > 0)
		ext2fs_mark_inode_bitmap2(fs->inode_map, ino);
	else
		ext2fs_unmark_inode_bitmap2(fs->inode_map, ino);
	ext2fs_bg_free_inodes_count_set(fs, group, ext2fs_bg_free_inodes_count(fs, group) - inuse);
	if (isdir)
		ext2fs_bg_used_dirs_count_set(fs, group, ext2fs_bg_used_dirs_count(fs, group) + inuse);

	/* We don't strictly need to be clearing the uninit flag if inuse < 0
	 * (i.e. freeing inodes) but it also means something is bad. */
	ext2fs_bg_flags_clear(fs, group, EXT2_BG_INODE_UNINIT);
	if (ext2fs_has_group_desc_csum(fs)) {
		ext2_ino_t first_unused_inode =	fs->super->s_inodes_per_group -
			ext2fs_bg_itable_unused(fs, group) +
			group * fs->super->s_inodes_per_group + 1;

		if (ino >= first_unused_inode)
			ext2fs_bg_itable_unused_set(fs, group, group * fs->super->s_inodes_per_group + fs->super->s_inodes_per_group - ino);
		ext2fs_group_desc_csum_set(fs, group);
	}

	fs->super->s_free_inodes_count -= inuse;
	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_ib_dirty(fs);
}

void ext2fs_inode_alloc_stats(ext2_filsys fs, ext2_ino_t ino, int inuse)
{
	ext2fs_inode_alloc_stats2(fs, ino, inuse, 0);
}

void ext2fs_block_alloc_stats2(ext2_filsys fs, blk64_t blk, int inuse)
{
	int	group = ext2fs_group_of_blk2(fs, blk);

	if (blk >= ext2fs_blocks_count(fs->super)) {
#ifndef OMIT_COM_ERR
		com_err("ext2fs_block_alloc_stats", 0,
			"Illegal block number: %lu", (unsigned long) blk);
#endif
		return;
	}
	if (inuse > 0)
		ext2fs_mark_block_bitmap2(fs->block_map, blk);
	else
		ext2fs_unmark_block_bitmap2(fs->block_map, blk);
	ext2fs_bg_free_blocks_count_set(fs, group, ext2fs_bg_free_blocks_count(fs, group) - inuse);
	ext2fs_bg_flags_clear(fs, group, EXT2_BG_BLOCK_UNINIT);
	ext2fs_group_desc_csum_set(fs, group);

	ext2fs_free_blocks_count_add(fs->super,
			     -inuse * (blk64_t) EXT2FS_CLUSTER_RATIO(fs));
	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	if (fs->block_alloc_stats)
		(fs->block_alloc_stats)(fs, (blk64_t) blk, inuse);
}

void ext2fs_block_alloc_stats(ext2_filsys fs, blk_t blk, int inuse)
{
	ext2fs_block_alloc_stats2(fs, blk, inuse);
}

void ext2fs_set_block_alloc_stats_callback(ext2_filsys fs,
					   void (*func)(ext2_filsys fs,
							blk64_t blk,
							int inuse),
					   void (**old)(ext2_filsys fs,
							blk64_t blk,
							int inuse))
{
	if (!fs || fs->magic != EXT2_ET_MAGIC_EXT2FS_FILSYS)
		return;
	if (old)
		*old = fs->block_alloc_stats;

	fs->block_alloc_stats = func;
}

void ext2fs_block_alloc_stats_range(ext2_filsys fs, blk64_t blk,
				    blk_t num, int inuse)
{
#ifndef OMIT_COM_ERR
	if (blk + num > ext2fs_blocks_count(fs->super)) {
		com_err("ext2fs_block_alloc_stats_range", 0,
			"Illegal block range: %llu (%u) ",
			(unsigned long long) blk, num);
		return;
	}
#endif
	if (inuse == 0)
		return;
	if (inuse > 0) {
		ext2fs_mark_block_bitmap_range2(fs->block_map, blk, num);
		inuse = 1;
	} else {
		ext2fs_unmark_block_bitmap_range2(fs->block_map, blk, num);
		inuse = -1;
	}
	while (num) {
		int group = ext2fs_group_of_blk2(fs, blk);
		blk64_t last_blk = ext2fs_group_last_block2(fs, group);
		blk64_t n = num;

		if (blk + num > last_blk)
			n = last_blk - blk + 1;

		ext2fs_bg_free_blocks_count_set(fs, group,
			ext2fs_bg_free_blocks_count(fs, group) -
			inuse*n/EXT2FS_CLUSTER_RATIO(fs));
		ext2fs_bg_flags_clear(fs, group, EXT2_BG_BLOCK_UNINIT);
		ext2fs_group_desc_csum_set(fs, group);
		ext2fs_free_blocks_count_add(fs->super, -inuse * (blk64_t) n);
		blk += n;
		num -= n;
	}
	ext2fs_mark_super_dirty(fs);
	ext2fs_mark_bb_dirty(fs);
	if (fs->block_alloc_stats_range)
		(fs->block_alloc_stats_range)(fs, blk, num, inuse);
}

void ext2fs_set_block_alloc_stats_range_callback(ext2_filsys fs,
	void (*func)(ext2_filsys fs, blk64_t blk,
				    blk_t num, int inuse),
	void (**old)(ext2_filsys fs, blk64_t blk,
				    blk_t num, int inuse))
{
	if (!fs || fs->magic != EXT2_ET_MAGIC_EXT2FS_FILSYS)
		return;
	if (old)
		*old = fs->block_alloc_stats_range;

	fs->block_alloc_stats_range = func;
}
