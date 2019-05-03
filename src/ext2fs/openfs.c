/*
 * openfs.c --- open an ext2 filesystem
 *
 * Copyright (C) 1993, 1994, 1995, 1996 Theodore Ts'o.
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
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include "ext2_fs.h"


#include "ext2fs.h"
#include "e2image.h"

blk64_t ext2fs_descriptor_block_loc2(ext2_filsys fs, blk64_t group_block,
				     dgrp_t i)
{
	int	bg;
	int	has_super = 0, group_zero_adjust = 0;
	blk64_t	ret_blk;

	/*
	 * On a bigalloc FS with 1K blocks, block 0 is reserved for non-ext4
	 * stuff, so adjust for that if we're being asked for group 0.
	 */
	if (i == 0 && fs->blocksize == 1024 && EXT2FS_CLUSTER_RATIO(fs) > 1)
		group_zero_adjust = 1;

	if (!ext2fs_has_feature_meta_bg(fs->super) ||
	    (i < fs->super->s_first_meta_bg))
		return group_block + i + 1 + group_zero_adjust;

	bg = EXT2_DESC_PER_BLOCK(fs->super) * i;
	if (ext2fs_bg_has_super(fs, bg))
		has_super = 1;
	ret_blk = ext2fs_group_first_block2(fs, bg);
	/*
	 * If group_block is not the normal value, we're trying to use
	 * the backup group descriptors and superblock --- so use the
	 * alternate location of the second block group in the
	 * metablock group.  Ideally we should be testing each bg
	 * descriptor block individually for correctness, but we don't
	 * have the infrastructure in place to do that.
	 */
	if (group_block != fs->super->s_first_data_block &&
	    ((ret_blk + has_super + fs->super->s_blocks_per_group) <
	     ext2fs_blocks_count(fs->super))) {
		ret_blk += fs->super->s_blocks_per_group;

		/*
		 * If we're going to jump forward a block group, make sure
		 * that we adjust has_super to account for the next group's
		 * backup superblock (or lack thereof).
		 */
		if (ext2fs_bg_has_super(fs, bg + 1))
			has_super = 1;
		else
			has_super = 0;
	}
	return ret_blk + has_super + group_zero_adjust;
}

blk_t ext2fs_descriptor_block_loc(ext2_filsys fs, blk_t group_block, dgrp_t i)
{
	return ext2fs_descriptor_block_loc2(fs, group_block, i);
}

errcode_t ext2fs_open(const char *name, int flags, int superblock,
		      unsigned int block_size, io_manager manager,
		      ext2_filsys *ret_fs)
{
	return ext2fs_open2(name, 0, flags, superblock, block_size,
			    manager, ret_fs);
}

static void block_sha_map_free_entry(void *data)
{
	free(data);
	return;
}

/*
 *  Note: if superblock is non-zero, block-size must also be non-zero.
 * 	Superblock and block_size can be zero to use the default size.
 *
 * Valid flags for ext2fs_open()
 *
 * 	EXT2_FLAG_RW	- Open the filesystem for read/write.
 * 	EXT2_FLAG_FORCE - Open the filesystem even if some of the
 *				features aren't supported.
 *	EXT2_FLAG_JOURNAL_DEV_OK - Open an ext3 journal device
 *	EXT2_FLAG_SKIP_MMP - Open without multi-mount protection check.
 *	EXT2_FLAG_64BITS - Allow 64-bit bitfields (needed for large
 *				filesystems)
 */
errcode_t ext2fs_open2(const char *name, const char *io_options,
		       int flags, int superblock,
		       unsigned int block_size, io_manager manager,
		       ext2_filsys *ret_fs)
{
	ext2_filsys	fs;
	errcode_t	retval;
	unsigned long	i, first_meta_bg;
	__u32		features;
	unsigned int	blocks_per_group, io_flags;
	blk64_t		group_block, blk;
	char		*dest, *cp;
	int		group_zero_adjust = 0;
	unsigned int	inode_size;
	__u64		groups_cnt;
#ifdef WORDS_BIGENDIAN
	unsigned int	groups_per_block;
	struct ext2_group_desc *gdp;
	int		j;
#endif
	char		*time_env;

	EXT2_CHECK_MAGIC(manager, EXT2_ET_MAGIC_IO_MANAGER);

	retval = ext2fs_get_mem(sizeof(struct struct_ext2_filsys), &fs);
	if (retval)
		return retval;

	memset(fs, 0, sizeof(struct struct_ext2_filsys));
	fs->magic = EXT2_ET_MAGIC_EXT2FS_FILSYS;
	fs->flags = flags;
	/* don't overwrite sb backups unless flag is explicitly cleared */
	fs->flags |= EXT2_FLAG_MASTER_SB_ONLY;
	fs->umask = 022;

	time_env = getenv("E2FSPROGS_FAKE_TIME");
	if (time_env)
		fs->now = strtoul(time_env, NULL, 0);

	retval = ext2fs_get_mem(strlen(name)+1, &fs->device_name);
	if (retval)
		goto cleanup;
	strcpy(fs->device_name, name);
	cp = strchr(fs->device_name, '?');
	if (!io_options && cp) {
		*cp++ = 0;
		io_options = cp;
	}

	io_flags = 0;
	if (flags & EXT2_FLAG_RW)
		io_flags |= IO_FLAG_RW;
	if (flags & EXT2_FLAG_EXCLUSIVE)
		io_flags |= IO_FLAG_EXCLUSIVE;
	if (flags & EXT2_FLAG_DIRECT_IO)
		io_flags |= IO_FLAG_DIRECT_IO;
	retval = manager->open(fs->device_name, io_flags, &fs->io);
	if (retval)
		goto cleanup;
	if (io_options &&
	    (retval = io_channel_set_options(fs->io, io_options)))
		goto cleanup;
	fs->image_io = fs->io;
	fs->io->app_data = fs;
	retval = io_channel_alloc_buf(fs->io, -SUPERBLOCK_SIZE, &fs->super);
	if (retval)
		goto cleanup;
	if (flags & EXT2_FLAG_IMAGE_FILE) {
		retval = ext2fs_get_mem(sizeof(struct ext2_image_hdr),
					&fs->image_header);
		if (retval)
			goto cleanup;
		retval = io_channel_read_blk(fs->io, 0,
					     -(int)sizeof(struct ext2_image_hdr),
					     fs->image_header);
		if (retval)
			goto cleanup;
		if (ext2fs_le32_to_cpu(fs->image_header->magic_number) != EXT2_ET_MAGIC_E2IMAGE)
			return EXT2_ET_MAGIC_E2IMAGE;
		superblock = 1;
		block_size = ext2fs_le32_to_cpu(fs->image_header->fs_blocksize);
	}

	/*
	 * If the user specifies a specific block # for the
	 * superblock, then he/she must also specify the block size!
	 * Otherwise, read the master superblock located at offset
	 * SUPERBLOCK_OFFSET from the start of the partition.
	 *
	 * Note: we only save a backup copy of the superblock if we
	 * are reading the superblock from the primary superblock location.
	 */
	if (superblock) {
		if (!block_size) {
			retval = EXT2_ET_INVALID_ARGUMENT;
			goto cleanup;
		}
		io_channel_set_blksize(fs->io, block_size);
		group_block = superblock;
		fs->orig_super = 0;
	} else {
		io_channel_set_blksize(fs->io, SUPERBLOCK_OFFSET);
		superblock = 1;
		group_block = 0;
		retval = ext2fs_get_mem(SUPERBLOCK_SIZE, &fs->orig_super);
		if (retval)
			goto cleanup;
	}
	retval = io_channel_read_blk(fs->io, superblock, -SUPERBLOCK_SIZE,
				     fs->super);
	if (retval)
		goto cleanup;
	if (fs->orig_super)
		memcpy(fs->orig_super, fs->super, SUPERBLOCK_SIZE);

	if (!(fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS)) {
		retval = 0;
		if (!ext2fs_verify_csum_type(fs, fs->super))
			retval = EXT2_ET_UNKNOWN_CSUM;
		if (!ext2fs_superblock_csum_verify(fs, fs->super))
			retval = EXT2_ET_SB_CSUM_INVALID;
	}

#ifdef WORDS_BIGENDIAN
	fs->flags |= EXT2_FLAG_SWAP_BYTES;
	ext2fs_swap_super(fs->super);
#else
	if (fs->flags & EXT2_FLAG_SWAP_BYTES) {
		retval = EXT2_ET_UNIMPLEMENTED;
		goto cleanup;
	}
#endif

	if (fs->super->s_magic != EXT2_SUPER_MAGIC)
		retval = EXT2_ET_BAD_MAGIC;
	if (retval)
		goto cleanup;

	if (fs->super->s_rev_level > EXT2_LIB_CURRENT_REV) {
		retval = EXT2_ET_REV_TOO_HIGH;
		goto cleanup;
	}

	/*
	 * Check for feature set incompatibility
	 */
	if (!(flags & EXT2_FLAG_FORCE)) {
		features = fs->super->s_feature_incompat;
#ifdef EXT2_LIB_SOFTSUPP_INCOMPAT
		if (flags & EXT2_FLAG_SOFTSUPP_FEATURES)
			features &= ~EXT2_LIB_SOFTSUPP_INCOMPAT;
#endif
		if (features & ~EXT2_LIB_FEATURE_INCOMPAT_SUPP) {
			retval = EXT2_ET_UNSUPP_FEATURE;
			goto cleanup;
		}

		features = fs->super->s_feature_ro_compat;
#ifdef EXT2_LIB_SOFTSUPP_RO_COMPAT
		if (flags & EXT2_FLAG_SOFTSUPP_FEATURES)
			features &= ~EXT2_LIB_SOFTSUPP_RO_COMPAT;
#endif
		if ((flags & EXT2_FLAG_RW) &&
		    (features & ~EXT2_LIB_FEATURE_RO_COMPAT_SUPP)) {
			retval = EXT2_ET_RO_UNSUPP_FEATURE;
			goto cleanup;
		}

		if (!(flags & EXT2_FLAG_JOURNAL_DEV_OK) &&
		    ext2fs_has_feature_journal_dev(fs->super)) {
			retval = EXT2_ET_UNSUPP_FEATURE;
			goto cleanup;
		}
	}

	if (fs->super->s_log_block_size >
	    (unsigned) (EXT2_MAX_BLOCK_LOG_SIZE - EXT2_MIN_BLOCK_LOG_SIZE)) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}

	/*
	 * bigalloc requires cluster-aware bitfield operations, which at the
	 * moment means we need EXT2_FLAG_64BITS.
	 */
	if (ext2fs_has_feature_bigalloc(fs->super) &&
	    !(flags & EXT2_FLAG_64BITS)) {
		retval = EXT2_ET_CANT_USE_LEGACY_BITMAPS;
		goto cleanup;
	}

	if (!ext2fs_has_feature_bigalloc(fs->super) &&
	    (fs->super->s_log_block_size != fs->super->s_log_cluster_size)) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	fs->fragsize = fs->blocksize = EXT2_BLOCK_SIZE(fs->super);
	inode_size = EXT2_INODE_SIZE(fs->super);
	if ((inode_size < EXT2_GOOD_OLD_INODE_SIZE) ||
	    (inode_size > fs->blocksize) ||
	    (inode_size & (inode_size - 1))) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}

	/* Enforce the block group descriptor size */
	if (ext2fs_has_feature_64bit(fs->super)) {
		if (fs->super->s_desc_size < EXT2_MIN_DESC_SIZE_64BIT) {
			retval = EXT2_ET_BAD_DESC_SIZE;
			goto cleanup;
		}
	}

	fs->cluster_ratio_bits = fs->super->s_log_cluster_size -
		fs->super->s_log_block_size;
	if (EXT2_BLOCKS_PER_GROUP(fs->super) !=
	    EXT2_CLUSTERS_PER_GROUP(fs->super) << fs->cluster_ratio_bits) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	fs->inode_blocks_per_group = ((EXT2_INODES_PER_GROUP(fs->super) *
				       EXT2_INODE_SIZE(fs->super) +
				       EXT2_BLOCK_SIZE(fs->super) - 1) /
				      EXT2_BLOCK_SIZE(fs->super));
	if (block_size) {
		if (block_size != fs->blocksize) {
			retval = EXT2_ET_UNEXPECTED_BLOCK_SIZE;
			goto cleanup;
		}
	}
	/*
	 * Set the blocksize to the filesystem's blocksize.
	 */
	io_channel_set_blksize(fs->io, fs->blocksize);

	/*
	 * If this is an external journal device, don't try to read
	 * the group descriptors, because they're not there.
	 */
	if (ext2fs_has_feature_journal_dev(fs->super)) {
		fs->group_desc_count = 0;
		*ret_fs = fs;
		return 0;
	}

	if (EXT2_INODES_PER_GROUP(fs->super) == 0) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	/* Precompute the FS UUID to seed other checksums */
	ext2fs_init_csum_seed(fs);

	/*
	 * Read group descriptors
	 */
	blocks_per_group = EXT2_BLOCKS_PER_GROUP(fs->super);
	if (blocks_per_group == 0 ||
	    blocks_per_group > EXT2_MAX_BLOCKS_PER_GROUP(fs->super) ||
	    fs->inode_blocks_per_group > EXT2_MAX_INODES_PER_GROUP(fs->super) ||
           EXT2_DESC_PER_BLOCK(fs->super) == 0 ||
           fs->super->s_first_data_block >= ext2fs_blocks_count(fs->super)) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	groups_cnt = ext2fs_div64_ceil(ext2fs_blocks_count(fs->super) -
				       fs->super->s_first_data_block,
				       blocks_per_group);
	if (groups_cnt >> 32) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	fs->group_desc_count = 	groups_cnt;
	if (!(flags & EXT2_FLAG_IGNORE_SB_ERRORS) &&
	    (__u64)fs->group_desc_count * EXT2_INODES_PER_GROUP(fs->super) !=
	    fs->super->s_inodes_count) {
		retval = EXT2_ET_CORRUPT_SUPERBLOCK;
		goto cleanup;
	}
	fs->desc_blocks = ext2fs_div_ceil(fs->group_desc_count,
					  EXT2_DESC_PER_BLOCK(fs->super));
	retval = ext2fs_get_array(fs->desc_blocks, fs->blocksize,
				&fs->group_desc);
	if (retval)
		goto cleanup;
	if (!group_block)
		group_block = fs->super->s_first_data_block;
	/*
	 * On a FS with a 1K blocksize, block 0 is reserved for bootloaders
	 * so we must increment block numbers to any group 0 items.
	 *
	 * However, we cannot touch group_block directly because in the meta_bg
	 * case, the ext2fs_descriptor_block_loc2() function will interpret
	 * group_block != s_first_data_block to mean that we want to access the
	 * backup group descriptors.  This is not what we want if the caller
	 * set superblock == 0 (i.e. auto-detect the superblock), which is
	 * what's going on here.
	 */
	if (group_block == 0 && fs->blocksize == 1024)
		group_zero_adjust = 1;
	dest = (char *) fs->group_desc;
#ifdef WORDS_BIGENDIAN
	groups_per_block = EXT2_DESC_PER_BLOCK(fs->super);
#endif
	if (ext2fs_has_feature_meta_bg(fs->super) &&
	    !(flags & EXT2_FLAG_IMAGE_FILE)) {
		first_meta_bg = fs->super->s_first_meta_bg;
		if (first_meta_bg > fs->desc_blocks)
			first_meta_bg = fs->desc_blocks;
	} else
		first_meta_bg = fs->desc_blocks;
	if (first_meta_bg) {
		retval = io_channel_read_blk(fs->io, group_block +
					     group_zero_adjust + 1,
					     first_meta_bg, dest);
		if (retval)
			goto cleanup;
#ifdef WORDS_BIGENDIAN
		gdp = (struct ext2_group_desc *) dest;
		for (j=0; j < groups_per_block*first_meta_bg; j++) {
			gdp = ext2fs_group_desc(fs, fs->group_desc, j);
			ext2fs_swap_group_desc2(fs, gdp);
		}
#endif
		dest += fs->blocksize*first_meta_bg;
	}

	for (i = first_meta_bg ; i < fs->desc_blocks; i++) {
		blk = ext2fs_descriptor_block_loc2(fs, group_block, i);
		io_channel_cache_readahead(fs->io, blk, 1);
	}

	for (i=first_meta_bg ; i < fs->desc_blocks; i++) {
		blk = ext2fs_descriptor_block_loc2(fs, group_block, i);
		retval = io_channel_read_blk64(fs->io, blk, 1, dest);
		if (retval)
			goto cleanup;
#ifdef WORDS_BIGENDIAN
		for (j=0; j < groups_per_block; j++) {
			gdp = ext2fs_group_desc(fs, fs->group_desc,
						i * groups_per_block + j);
			ext2fs_swap_group_desc2(fs, gdp);
		}
#endif
		dest += fs->blocksize;
	}

	fs->stride = fs->super->s_raid_stride;

	/*
	 * If recovery is from backup superblock, Clear _UNININT flags &
	 * reset bg_itable_unused to zero
	 */
	if (superblock > 1 && ext2fs_has_group_desc_csum(fs)) {
		dgrp_t group;

		for (group = 0; group < fs->group_desc_count; group++) {
			ext2fs_bg_flags_clear(fs, group, EXT2_BG_BLOCK_UNINIT);
			ext2fs_bg_flags_clear(fs, group, EXT2_BG_INODE_UNINIT);
			ext2fs_bg_itable_unused_set(fs, group, 0);
			/* The checksum will be reset later, but fix it here
			 * anyway to avoid printing a lot of spurious errors. */
			ext2fs_group_desc_csum_set(fs, group);
		}
		if (fs->flags & EXT2_FLAG_RW)
			ext2fs_mark_super_dirty(fs);
	}

	if (ext2fs_has_feature_mmp(fs->super) &&
	    !(flags & EXT2_FLAG_SKIP_MMP) &&
	    (flags & (EXT2_FLAG_RW | EXT2_FLAG_EXCLUSIVE))) {
		retval = ext2fs_mmp_start(fs);
		if (retval) {
			fs->flags |= EXT2_FLAG_SKIP_MMP; /* just do cleanup */
			ext2fs_mmp_stop(fs);
			goto cleanup;
		}
	}

	if (fs->flags & EXT2_FLAG_SHARE_DUP) {
		fs->block_sha_map = ext2fs_hashmap_create(ext2fs_djb2_hash,
					block_sha_map_free_entry, 4096);
		if (!fs->block_sha_map) {
			retval = EXT2_ET_NO_MEMORY;
			goto cleanup;
		}
		ext2fs_set_feature_shared_blocks(fs->super);
	}

	fs->flags &= ~EXT2_FLAG_NOFREE_ON_ERROR;
	*ret_fs = fs;

	return 0;
cleanup:
	if (!(flags & EXT2_FLAG_NOFREE_ON_ERROR)) {
		ext2fs_free(fs);
		fs = NULL;
	}
	*ret_fs = fs;
	return retval;
}

/*
 * Set/get the filesystem data I/O channel.
 *
 * These functions are only valid if EXT2_FLAG_IMAGE_FILE is true.
 */
errcode_t ext2fs_get_data_io(ext2_filsys fs, io_channel *old_io)
{
	if ((fs->flags & EXT2_FLAG_IMAGE_FILE) == 0)
		return EXT2_ET_NOT_IMAGE_FILE;
	if (old_io) {
		*old_io = (fs->image_io == fs->io) ? 0 : fs->io;
	}
	return 0;
}

errcode_t ext2fs_set_data_io(ext2_filsys fs, io_channel new_io)
{
	if ((fs->flags & EXT2_FLAG_IMAGE_FILE) == 0)
		return EXT2_ET_NOT_IMAGE_FILE;
	fs->io = new_io ? new_io : fs->image_io;
	return 0;
}

errcode_t ext2fs_rewrite_to_io(ext2_filsys fs, io_channel new_io)
{
	errcode_t err;

	if ((fs->flags & EXT2_FLAG_IMAGE_FILE) == 0)
		return EXT2_ET_NOT_IMAGE_FILE;
	err = io_channel_set_blksize(new_io, fs->blocksize);
	if (err)
		return err;
	if ((new_io == fs->image_io) || (new_io == fs->io))
		return 0;
	if ((fs->image_io != fs->io) &&
	    fs->image_io)
		io_channel_close(fs->image_io);
	if (fs->io)
		io_channel_close(fs->io);
	fs->io = fs->image_io = new_io;
	fs->flags |= EXT2_FLAG_DIRTY | EXT2_FLAG_RW |
		EXT2_FLAG_BB_DIRTY | EXT2_FLAG_IB_DIRTY;
	fs->flags &= ~EXT2_FLAG_IMAGE_FILE;
	return 0;
}
