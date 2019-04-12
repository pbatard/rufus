/*
 * rw_bitmaps.c --- routines to read and write the  inode and block bitmaps.
 *
 * Copyright (C) 1993, 1994, 1994, 1996 Theodore Ts'o.
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
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "e2image.h"

static errcode_t write_bitmaps(ext2_filsys fs, int do_inode, int do_block)
{
	dgrp_t 		i;
	unsigned int	j;
	int		block_nbytes, inode_nbytes;
	unsigned int	nbits;
	errcode_t	retval;
	char		*block_buf = NULL, *inode_buf = NULL;
	int		csum_flag;
	blk64_t		blk;
	blk64_t		blk_itr = EXT2FS_B2C(fs, fs->super->s_first_data_block);
	ext2_ino_t	ino_itr = 1;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

	csum_flag = ext2fs_has_group_desc_csum(fs);

	inode_nbytes = block_nbytes = 0;
	if (do_block) {
		block_nbytes = EXT2_CLUSTERS_PER_GROUP(fs->super) / 8;
		retval = io_channel_alloc_buf(fs->io, 0, &block_buf);
		if (retval)
			goto errout;
		memset(block_buf, 0xff, fs->blocksize);
	}
	if (do_inode) {
		inode_nbytes = (size_t)
			((EXT2_INODES_PER_GROUP(fs->super)+7) / 8);
		retval = io_channel_alloc_buf(fs->io, 0, &inode_buf);
		if (retval)
			goto errout;
		memset(inode_buf, 0xff, fs->blocksize);
	}

	for (i = 0; i < fs->group_desc_count; i++) {
		if (!do_block)
			goto skip_block_bitmap;

		if (csum_flag && ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT)
		    )
			goto skip_this_block_bitmap;

		retval = ext2fs_get_block_bitmap_range2(fs->block_map,
				blk_itr, block_nbytes << 3, block_buf);
		if (retval)
			goto errout;

		if (i == fs->group_desc_count - 1) {
			/* Force bitmap padding for the last group */
			nbits = EXT2FS_NUM_B2C(fs,
				((ext2fs_blocks_count(fs->super)
				  - (__u64) fs->super->s_first_data_block)
				 % (__u64) EXT2_BLOCKS_PER_GROUP(fs->super)));
			if (nbits)
				for (j = nbits; j < fs->blocksize * 8; j++)
					ext2fs_set_bit(j, block_buf);
		}

		retval = ext2fs_block_bitmap_csum_set(fs, i, block_buf,
						      block_nbytes);
		if (retval)
			return retval;
		ext2fs_group_desc_csum_set(fs, i);
		fs->flags |= EXT2_FLAG_DIRTY;

		blk = ext2fs_block_bitmap_loc(fs, i);
		if (blk) {
			retval = io_channel_write_blk64(fs->io, blk, 1,
							block_buf);
			if (retval) {
				retval = EXT2_ET_BLOCK_BITMAP_WRITE;
				goto errout;
			}
		}
	skip_this_block_bitmap:
		blk_itr += block_nbytes << 3;
	skip_block_bitmap:

		if (!do_inode)
			continue;

		if (csum_flag && ext2fs_bg_flags_test(fs, i, EXT2_BG_INODE_UNINIT)
		    )
			goto skip_this_inode_bitmap;

		retval = ext2fs_get_inode_bitmap_range2(fs->inode_map,
				ino_itr, inode_nbytes << 3, inode_buf);
		if (retval)
			goto errout;

		retval = ext2fs_inode_bitmap_csum_set(fs, i, inode_buf,
						      inode_nbytes);
		if (retval)
			goto errout;
		ext2fs_group_desc_csum_set(fs, i);
		fs->flags |= EXT2_FLAG_DIRTY;

		blk = ext2fs_inode_bitmap_loc(fs, i);
		if (blk) {
			retval = io_channel_write_blk64(fs->io, blk, 1,
						      inode_buf);
			if (retval) {
				retval = EXT2_ET_INODE_BITMAP_WRITE;
				goto errout;
			}
		}
	skip_this_inode_bitmap:
		ino_itr += inode_nbytes << 3;

	}
	if (do_block) {
		fs->flags &= ~EXT2_FLAG_BB_DIRTY;
		ext2fs_free_mem(&block_buf);
	}
	if (do_inode) {
		fs->flags &= ~EXT2_FLAG_IB_DIRTY;
		ext2fs_free_mem(&inode_buf);
	}
	return 0;
errout:
	if (inode_buf)
		ext2fs_free_mem(&inode_buf);
	if (block_buf)
		ext2fs_free_mem(&block_buf);
	return retval;
}

static errcode_t mark_uninit_bg_group_blocks(ext2_filsys fs)
{
	dgrp_t			i;
	blk64_t			blk;
	ext2fs_block_bitmap	bmap = fs->block_map;

	for (i = 0; i < fs->group_desc_count; i++) {
		if (!ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT))
			continue;

		ext2fs_reserve_super_and_bgd(fs, i, bmap);

		/*
		 * Mark the blocks used for the inode table
		 */
		blk = ext2fs_inode_table_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap_range2(bmap, blk,
						fs->inode_blocks_per_group);

		/*
		 * Mark block used for the block bitmap
		 */
		blk = ext2fs_block_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);

		/*
		 * Mark block used for the inode bitmap
		 */
		blk = ext2fs_inode_bitmap_loc(fs, i);
		if (blk)
			ext2fs_mark_block_bitmap2(bmap, blk);
	}
	return 0;
}

static errcode_t read_bitmaps(ext2_filsys fs, int do_inode, int do_block)
{
	dgrp_t i;
	char *block_bitmap = 0, *inode_bitmap = 0;
	char *buf;
	errcode_t retval;
	int block_nbytes = EXT2_CLUSTERS_PER_GROUP(fs->super) / 8;
	int inode_nbytes = EXT2_INODES_PER_GROUP(fs->super) / 8;
	int csum_flag;
	unsigned int	cnt;
	blk64_t	blk;
	blk64_t	blk_itr = EXT2FS_B2C(fs, fs->super->s_first_data_block);
	blk64_t   blk_cnt;
	ext2_ino_t ino_itr = 1;
	ext2_ino_t ino_cnt;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if ((block_nbytes > (int) fs->blocksize) ||
	    (inode_nbytes > (int) fs->blocksize))
		return EXT2_ET_CORRUPT_SUPERBLOCK;

	fs->write_bitmaps = ext2fs_write_bitmaps;

	csum_flag = ext2fs_has_group_desc_csum(fs);

	retval = ext2fs_get_mem(strlen(fs->device_name) + 80, &buf);
	if (retval)
		return retval;
	if (do_block) {
		if (fs->block_map)
			ext2fs_free_block_bitmap(fs->block_map);
		strcpy(buf, "block bitmap for ");
		strcat(buf, fs->device_name);
		retval = ext2fs_allocate_block_bitmap(fs, buf, &fs->block_map);
		if (retval)
			goto cleanup;
		retval = io_channel_alloc_buf(fs->io, 0, &block_bitmap);
		if (retval)
			goto cleanup;
	} else
		block_nbytes = 0;
	if (do_inode) {
		if (fs->inode_map)
			ext2fs_free_inode_bitmap(fs->inode_map);
		strcpy(buf, "inode bitmap for ");
		strcat(buf, fs->device_name);
		retval = ext2fs_allocate_inode_bitmap(fs, buf, &fs->inode_map);
		if (retval)
			goto cleanup;
		retval = io_channel_alloc_buf(fs->io, 0, &inode_bitmap);
		if (retval)
			goto cleanup;
	} else
		inode_nbytes = 0;
	ext2fs_free_mem(&buf);

	if (fs->flags & EXT2_FLAG_IMAGE_FILE) {
		blk = (ext2fs_le32_to_cpu(fs->image_header->offset_inodemap) / fs->blocksize);
		ino_cnt = fs->super->s_inodes_count;
		while (inode_bitmap && ino_cnt > 0) {
			retval = io_channel_read_blk64(fs->image_io, blk++,
						     1, inode_bitmap);
			if (retval)
				goto cleanup;
			cnt = fs->blocksize << 3;
			if (cnt > ino_cnt)
				cnt = ino_cnt;
			retval = ext2fs_set_inode_bitmap_range2(fs->inode_map,
					       ino_itr, cnt, inode_bitmap);
			if (retval)
				goto cleanup;
			ino_itr += cnt;
			ino_cnt -= cnt;
		}
		blk = (ext2fs_le32_to_cpu(fs->image_header->offset_blockmap) /
		       fs->blocksize);
		blk_cnt = EXT2_GROUPS_TO_CLUSTERS(fs->super,
						  fs->group_desc_count);
		while (block_bitmap && blk_cnt > 0) {
			retval = io_channel_read_blk64(fs->image_io, blk++,
						     1, block_bitmap);
			if (retval)
				goto cleanup;
			cnt = fs->blocksize << 3;
			if (cnt > blk_cnt)
				cnt = blk_cnt;
			retval = ext2fs_set_block_bitmap_range2(fs->block_map,
				       blk_itr, cnt, block_bitmap);
			if (retval)
				goto cleanup;
			blk_itr += cnt;
			blk_cnt -= cnt;
		}
		goto success_cleanup;
	}

	for (i = 0; i < fs->group_desc_count; i++) {
		if (block_bitmap) {
			blk = ext2fs_block_bitmap_loc(fs, i);
			if (csum_flag &&
			    ext2fs_bg_flags_test(fs, i, EXT2_BG_BLOCK_UNINIT) &&
			    ext2fs_group_desc_csum_verify(fs, i))
				blk = 0;
			if (blk) {
				retval = io_channel_read_blk64(fs->io, blk,
							       1, block_bitmap);
				if (retval) {
					retval = EXT2_ET_BLOCK_BITMAP_READ;
					goto cleanup;
				}
				/* verify block bitmap checksum */
				if (!(fs->flags &
				      EXT2_FLAG_IGNORE_CSUM_ERRORS) &&
				    !ext2fs_block_bitmap_csum_verify(fs, i,
						block_bitmap, block_nbytes)) {
					retval =
					EXT2_ET_BLOCK_BITMAP_CSUM_INVALID;
					goto cleanup;
				}
			} else
				memset(block_bitmap, 0, block_nbytes);
			cnt = block_nbytes << 3;
			retval = ext2fs_set_block_bitmap_range2(fs->block_map,
					       blk_itr, cnt, block_bitmap);
			if (retval)
				goto cleanup;
			blk_itr += block_nbytes << 3;
		}
		if (inode_bitmap) {
			blk = ext2fs_inode_bitmap_loc(fs, i);
			if (csum_flag &&
			    ext2fs_bg_flags_test(fs, i, EXT2_BG_INODE_UNINIT) &&
			    ext2fs_group_desc_csum_verify(fs, i))
				blk = 0;
			if (blk) {
				retval = io_channel_read_blk64(fs->io, blk,
							       1, inode_bitmap);
				if (retval) {
					retval = EXT2_ET_INODE_BITMAP_READ;
					goto cleanup;
				}

				/* verify inode bitmap checksum */
				if (!(fs->flags &
				      EXT2_FLAG_IGNORE_CSUM_ERRORS) &&
				    !ext2fs_inode_bitmap_csum_verify(fs, i,
						inode_bitmap, inode_nbytes)) {
					retval =
					EXT2_ET_INODE_BITMAP_CSUM_INVALID;
					goto cleanup;
				}
			} else
				memset(inode_bitmap, 0, inode_nbytes);
			cnt = inode_nbytes << 3;
			retval = ext2fs_set_inode_bitmap_range2(fs->inode_map,
					       ino_itr, cnt, inode_bitmap);
			if (retval)
				goto cleanup;
			ino_itr += inode_nbytes << 3;
		}
	}

	/* Mark group blocks for any BLOCK_UNINIT groups */
	if (do_block) {
		retval = mark_uninit_bg_group_blocks(fs);
		if (retval)
			goto cleanup;
	}

success_cleanup:
	if (inode_bitmap)
		ext2fs_free_mem(&inode_bitmap);
	if (block_bitmap)
		ext2fs_free_mem(&block_bitmap);
	return 0;

cleanup:
	if (do_block) {
		ext2fs_free_mem(&fs->block_map);
		fs->block_map = 0;
	}
	if (do_inode) {
		ext2fs_free_mem(&fs->inode_map);
		fs->inode_map = 0;
	}
	if (inode_bitmap)
		ext2fs_free_mem(&inode_bitmap);
	if (block_bitmap)
		ext2fs_free_mem(&block_bitmap);
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}

errcode_t ext2fs_read_inode_bitmap(ext2_filsys fs)
{
	return read_bitmaps(fs, 1, 0);
}

errcode_t ext2fs_read_block_bitmap(ext2_filsys fs)
{
	return read_bitmaps(fs, 0, 1);
}

errcode_t ext2fs_write_inode_bitmap(ext2_filsys fs)
{
	return write_bitmaps(fs, 1, 0);
}

errcode_t ext2fs_write_block_bitmap (ext2_filsys fs)
{
	return write_bitmaps(fs, 0, 1);
}

errcode_t ext2fs_read_bitmaps(ext2_filsys fs)
{
	if (fs->inode_map && fs->block_map)
		return 0;

	return read_bitmaps(fs, !fs->inode_map, !fs->block_map);
}

errcode_t ext2fs_write_bitmaps(ext2_filsys fs)
{
	int do_inode = fs->inode_map && ext2fs_test_ib_dirty(fs);
	int do_block = fs->block_map && ext2fs_test_bb_dirty(fs);

	if (!do_inode && !do_block)
		return 0;

	return write_bitmaps(fs, do_inode, do_block);
}
