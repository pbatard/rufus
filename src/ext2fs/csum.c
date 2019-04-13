/*
 * csum.c --- checksumming of ext3 structures
 *
 * Copyright (C) 2006 Cluster File Systems, Inc.
 * Copyright (C) 2006, 2007 by Andreas Dilger <adilger@clusterfs.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "ext2_fs.h"
#include "ext2fs.h"
#include "crc16.h"
#include <assert.h>

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifdef DEBUG
#define STATIC
#else
#define STATIC static
#endif

void ext2fs_init_csum_seed(ext2_filsys fs)
{
	if (ext2fs_has_feature_csum_seed(fs->super))
		fs->csum_seed = fs->super->s_checksum_seed;
	else if (ext2fs_has_feature_metadata_csum(fs->super) ||
		 ext2fs_has_feature_ea_inode(fs->super))
		fs->csum_seed = ext2fs_crc32c_le(~0, fs->super->s_uuid,
						 sizeof(fs->super->s_uuid));
}

static __u32 ext2fs_mmp_csum(ext2_filsys fs, struct mmp_struct *mmp)
{
	int offset = offsetof(struct mmp_struct, mmp_checksum);

	return ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)mmp, offset);
}

int ext2fs_mmp_csum_verify(ext2_filsys fs, struct mmp_struct *mmp)
{
	__u32 calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	calculated = ext2fs_mmp_csum(fs, mmp);

	return ext2fs_le32_to_cpu(mmp->mmp_checksum) == calculated;
}

errcode_t ext2fs_mmp_csum_set(ext2_filsys fs, struct mmp_struct *mmp)
{
	__u32 crc;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_mmp_csum(fs, mmp);
	mmp->mmp_checksum = ext2fs_cpu_to_le32(crc);

	return 0;
}

int ext2fs_verify_csum_type(ext2_filsys fs, struct ext2_super_block *sb)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	return sb->s_checksum_type == EXT2_CRC32C_CHKSUM;
}

static __u32 ext2fs_superblock_csum(ext2_filsys fs EXT2FS_ATTR((unused)),
				    struct ext2_super_block *sb)
{
	int offset = offsetof(struct ext2_super_block, s_checksum);

	return ext2fs_crc32c_le(~0, (unsigned char *)sb, offset);
}

/* NOTE: The input to this function MUST be in LE order */
int ext2fs_superblock_csum_verify(ext2_filsys fs, struct ext2_super_block *sb)
{
	__u32 flag, calculated;

	if (fs->flags & EXT2_FLAG_SWAP_BYTES)
		flag = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	else
		flag = ext2fs_cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs->super, flag))
		return 1;

	calculated = ext2fs_superblock_csum(fs, sb);

	return ext2fs_le32_to_cpu(sb->s_checksum) == calculated;
}

/* NOTE: The input to this function MUST be in LE order */
errcode_t ext2fs_superblock_csum_set(ext2_filsys fs,
				     struct ext2_super_block *sb)
{
	__u32 flag, crc;

	if (fs->flags & EXT2_FLAG_SWAP_BYTES)
		flag = EXT4_FEATURE_RO_COMPAT_METADATA_CSUM;
	else
		flag = ext2fs_cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM);

	if (!EXT2_HAS_RO_COMPAT_FEATURE(fs->super, flag))
		return 0;

	crc = ext2fs_superblock_csum(fs, sb);
	sb->s_checksum = ext2fs_cpu_to_le32(crc);

	return 0;
}

static errcode_t ext2fs_ext_attr_block_csum(ext2_filsys fs,
					    ext2_ino_t inum EXT2FS_ATTR((unused)),
					    blk64_t block,
					    struct ext2_ext_attr_header *hdr,
					    __u32 *crc)
{
	char *buf = (char *)hdr;
	__u32 old_crc = hdr->h_checksum;

	hdr->h_checksum = 0;
	block = ext2fs_cpu_to_le64(block);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&block,
				sizeof(block));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, fs->blocksize);
	hdr->h_checksum = old_crc;

	return 0;
}

int ext2fs_ext_attr_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				      blk64_t block,
				      struct ext2_ext_attr_header *hdr)
{
	__u32 calculated;
	errcode_t retval;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	retval = ext2fs_ext_attr_block_csum(fs, inum, block, hdr, &calculated);
	if (retval)
		return 0;

	return ext2fs_le32_to_cpu(hdr->h_checksum) == calculated;
}

errcode_t ext2fs_ext_attr_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
					 blk64_t block,
					 struct ext2_ext_attr_header *hdr)
{
	errcode_t retval;
	__u32 crc;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	retval = ext2fs_ext_attr_block_csum(fs, inum, block, hdr, &crc);
	if (retval)
		return retval;
	hdr->h_checksum = ext2fs_cpu_to_le32(crc);
	return 0;
}

static __u16 do_nothing16(__u16 x)
{
	return x;
}

static __u16 disk_to_host16(__u16 x)
{
	return ext2fs_le16_to_cpu(x);
}

static errcode_t __get_dx_countlimit(ext2_filsys fs,
				     struct ext2_dir_entry *dirent,
				     struct ext2_dx_countlimit **cc,
				     int *offset,
				     int need_swab)
{
	struct ext2_dir_entry *dp;
	struct ext2_dx_root_info *root;
	struct ext2_dx_countlimit *c;
	int count_offset, max_sane_entries;
	unsigned int rec_len;
	__u16 (*translate)(__u16) = (need_swab ? disk_to_host16 : do_nothing16);

	rec_len = translate(dirent->rec_len);

	if (rec_len == fs->blocksize && translate(dirent->name_len) == 0)
		count_offset = 8;
	else if (rec_len == 12) {
		dp = (struct ext2_dir_entry *)(((char *)dirent) + rec_len);
		rec_len = translate(dp->rec_len);
		if (rec_len != fs->blocksize - 12)
			return EXT2_ET_DB_NOT_FOUND;
		root = (struct ext2_dx_root_info *)(((char *)dp + 12));
		if (root->reserved_zero ||
		    root->info_length != sizeof(struct ext2_dx_root_info))
			return EXT2_ET_DB_NOT_FOUND;
		count_offset = 32;
	} else
		return EXT2_ET_DB_NOT_FOUND;

	c = (struct ext2_dx_countlimit *)(((char *)dirent) + count_offset);
	max_sane_entries = (fs->blocksize - count_offset) /
			   sizeof(struct ext2_dx_entry);
	if (ext2fs_le16_to_cpu(c->limit) > max_sane_entries ||
	    ext2fs_le16_to_cpu(c->count) > max_sane_entries)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	if (offset)
		*offset = count_offset;
	if (cc)
		*cc = c;

	return 0;
}

errcode_t ext2fs_get_dx_countlimit(ext2_filsys fs,
				   struct ext2_dir_entry *dirent,
				   struct ext2_dx_countlimit **cc,
				   int *offset)
{
	return __get_dx_countlimit(fs, dirent, cc, offset, 0);
}

void ext2fs_initialize_dirent_tail(ext2_filsys fs,
				   struct ext2_dir_entry_tail *t)
{
	memset(t, 0, sizeof(struct ext2_dir_entry_tail));
	ext2fs_set_rec_len(fs, sizeof(struct ext2_dir_entry_tail),
			   (struct ext2_dir_entry *)t);
	t->det_reserved_name_len = EXT2_DIR_NAME_LEN_CSUM;
}

static errcode_t __get_dirent_tail(ext2_filsys fs,
				   struct ext2_dir_entry *dirent,
				   struct ext2_dir_entry_tail **tt,
				   int need_swab)
{
	struct ext2_dir_entry *d;
	void *top;
	struct ext2_dir_entry_tail *t;
	unsigned int rec_len;
	errcode_t retval = 0;
	__u16 (*translate)(__u16) = (need_swab ? disk_to_host16 : do_nothing16);

	d = dirent;
	top = EXT2_DIRENT_TAIL(dirent, fs->blocksize);

	rec_len = translate(d->rec_len);
	while (rec_len && !(rec_len & 0x3)) {
		d = (struct ext2_dir_entry *)(((char *)d) + rec_len);
		if ((void *)d >= top)
			break;
		rec_len = translate(d->rec_len);
	}

	if (d != top)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	t = (struct ext2_dir_entry_tail *)d;
	if (t->det_reserved_zero1 ||
	    translate(t->det_rec_len) != sizeof(struct ext2_dir_entry_tail) ||
	    translate(t->det_reserved_name_len) != EXT2_DIR_NAME_LEN_CSUM)
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;

	if (tt)
		*tt = t;
	return retval;
}

int ext2fs_dirent_has_tail(ext2_filsys fs, struct ext2_dir_entry *dirent)
{
	return __get_dirent_tail(fs, dirent, NULL, 0) == 0;
}

static errcode_t ext2fs_dirent_csum(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent, __u32 *crc,
				    int size)
{
	errcode_t retval;
	char *buf = (char *)dirent;
	__u32 gen;
	struct ext2_inode inode;

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;

	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, size);

	return 0;
}

int ext2fs_dirent_csum_verify(ext2_filsys fs, ext2_ino_t inum,
			      struct ext2_dir_entry *dirent)
{
	errcode_t retval;
	__u32 calculated;
	struct ext2_dir_entry_tail *t;

	retval = __get_dirent_tail(fs, dirent, &t, 1);
	if (retval)
		return 1;

	/*
	 * The checksum field is overlaid with the dirent->name field
	 * so the swapfs.c functions won't change the endianness.
	 */
	retval = ext2fs_dirent_csum(fs, inum, dirent, &calculated,
				    (char *)t - (char *)dirent);
	if (retval)
		return 0;
	return ext2fs_le32_to_cpu(t->det_checksum) == calculated;
}

static errcode_t ext2fs_dirent_csum_set(ext2_filsys fs, ext2_ino_t inum,
					struct ext2_dir_entry *dirent)
{
	errcode_t retval;
	__u32 crc;
	struct ext2_dir_entry_tail *t;

	retval = __get_dirent_tail(fs, dirent, &t, 1);
	if (retval)
		return retval;

	/* swapfs.c functions don't change the checksum endianness */
	retval = ext2fs_dirent_csum(fs, inum, dirent, &crc,
				    (char *)t - (char *)dirent);
	if (retval)
		return retval;
	t->det_checksum = ext2fs_cpu_to_le32(crc);
	return 0;
}

static errcode_t ext2fs_dx_csum(ext2_filsys fs, ext2_ino_t inum,
				struct ext2_dir_entry *dirent,
				__u32 *crc, int count_offset, int count,
				struct ext2_dx_tail *t)
{
	errcode_t retval;
	char *buf = (char *)dirent;
	int size;
	__u32 old_csum, gen;
	struct ext2_inode inode;

	size = count_offset + (count * sizeof(struct ext2_dx_entry));
	old_csum = t->dt_checksum;
	t->dt_checksum = 0;

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;

	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)buf, size);
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)t,
				sizeof(struct ext2_dx_tail));
	t->dt_checksum = old_csum;

	return 0;
}

static int ext2fs_dx_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				 struct ext2_dir_entry *dirent)
{
	__u32 calculated;
	errcode_t retval;
	struct ext2_dx_countlimit *c;
	struct ext2_dx_tail *t;
	int count_offset, limit, count;

	retval = __get_dx_countlimit(fs, dirent, &c, &count_offset, 1);
	if (retval)
		return 1;
	limit = ext2fs_le16_to_cpu(c->limit);
	count = ext2fs_le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct ext2_dx_entry)) >
	    fs->blocksize - sizeof(struct ext2_dx_tail))
		return 0;
	/* htree structs are accessed in LE order */
	t = (struct ext2_dx_tail *)(((struct ext2_dx_entry *)c) + limit);
	retval = ext2fs_dx_csum(fs, inum, dirent, &calculated, count_offset,
				count, t);
	if (retval)
		return 0;

	return ext2fs_le32_to_cpu(t->dt_checksum) == calculated;
}

static errcode_t ext2fs_dx_csum_set(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent)
{
	__u32 crc;
	errcode_t retval = 0;
	struct ext2_dx_countlimit *c;
	struct ext2_dx_tail *t;
	int count_offset, limit, count;

	retval = __get_dx_countlimit(fs, dirent, &c, &count_offset, 1);
	if (retval)
		return retval;
	limit = ext2fs_le16_to_cpu(c->limit);
	count = ext2fs_le16_to_cpu(c->count);
	if (count_offset + (limit * sizeof(struct ext2_dx_entry)) >
	    fs->blocksize - sizeof(struct ext2_dx_tail))
		return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
	t = (struct ext2_dx_tail *)(((struct ext2_dx_entry *)c) + limit);

	/* htree structs are accessed in LE order */
	retval = ext2fs_dx_csum(fs, inum, dirent, &crc, count_offset, count, t);
	if (retval)
		return retval;
	t->dt_checksum = ext2fs_cpu_to_le32(crc);
	return retval;
}

int ext2fs_dir_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				 struct ext2_dir_entry *dirent)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	if (__get_dirent_tail(fs, dirent, NULL, 1) == 0)
		return ext2fs_dirent_csum_verify(fs, inum, dirent);
	if (__get_dx_countlimit(fs, dirent, NULL, NULL, 1) == 0)
		return ext2fs_dx_csum_verify(fs, inum, dirent);

	return 0;
}

errcode_t ext2fs_dir_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
				    struct ext2_dir_entry *dirent)
{
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	if (__get_dirent_tail(fs, dirent, NULL, 1) == 0)
		return ext2fs_dirent_csum_set(fs, inum, dirent);
	if (__get_dx_countlimit(fs, dirent, NULL, NULL, 1) == 0)
		return ext2fs_dx_csum_set(fs, inum, dirent);

	if (fs->flags & EXT2_FLAG_IGNORE_CSUM_ERRORS)
		return 0;
	return EXT2_ET_DIR_NO_SPACE_FOR_CSUM;
}

#define EXT3_EXTENT_TAIL_OFFSET(hdr)	(sizeof(struct ext3_extent_header) + \
	(sizeof(struct ext3_extent) * ext2fs_le16_to_cpu((hdr)->eh_max)))

static struct ext3_extent_tail *get_extent_tail(struct ext3_extent_header *h)
{
	return (struct ext3_extent_tail *)(((char *)h) +
					   EXT3_EXTENT_TAIL_OFFSET(h));
}

static errcode_t ext2fs_extent_block_csum(ext2_filsys fs, ext2_ino_t inum,
					  struct ext3_extent_header *eh,
					  __u32 *crc)
{
	int size;
	__u32 gen;
	errcode_t retval;
	struct ext2_inode inode;

	size = EXT3_EXTENT_TAIL_OFFSET(eh) + offsetof(struct ext3_extent_tail,
						      et_checksum);

	retval = ext2fs_read_inode(fs, inum, &inode);
	if (retval)
		return retval;
	inum = ext2fs_cpu_to_le32(inum);
	gen = ext2fs_cpu_to_le32(inode.i_generation);
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)eh, size);

	return 0;
}

int ext2fs_extent_block_csum_verify(ext2_filsys fs, ext2_ino_t inum,
				    struct ext3_extent_header *eh)
{
	errcode_t retval;
	__u32 provided, calculated;
	struct ext3_extent_tail *t = get_extent_tail(eh);

	/*
	 * The extent tree structures are accessed in LE order, so we must
	 * swap the checksum bytes here.
	 */
	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	provided = ext2fs_le32_to_cpu(t->et_checksum);
	retval = ext2fs_extent_block_csum(fs, inum, eh, &calculated);
	if (retval)
		return 0;

	return provided == calculated;
}

errcode_t ext2fs_extent_block_csum_set(ext2_filsys fs, ext2_ino_t inum,
				       struct ext3_extent_header *eh)
{
	errcode_t retval;
	__u32 crc;
	struct ext3_extent_tail *t = get_extent_tail(eh);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	/*
	 * The extent tree structures are accessed in LE order, so we must
	 * swap the checksum bytes here.
	 */
	retval = ext2fs_extent_block_csum(fs, inum, eh, &crc);
	if (retval)
		return retval;
	t->et_checksum = ext2fs_cpu_to_le32(crc);
	return retval;
}

int ext2fs_inode_bitmap_csum_verify(ext2_filsys fs, dgrp_t group,
				    char *bitmap, int size)
{
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);
	__u32 provided, calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;
	provided = gdp->bg_inode_bitmap_csum_lo;
	calculated = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap,
				      size);
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
		provided |= (__u32)gdp->bg_inode_bitmap_csum_hi << 16;
	else
		calculated &= 0xFFFF;

	return provided == calculated;
}

errcode_t ext2fs_inode_bitmap_csum_set(ext2_filsys fs, dgrp_t group,
				       char *bitmap, int size)
{
	__u32 crc;
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap, size);
	gdp->bg_inode_bitmap_csum_lo = crc & 0xFFFF;
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_INODE_BITMAP_CSUM_HI_END)
		gdp->bg_inode_bitmap_csum_hi = crc >> 16;

	return 0;
}

int ext2fs_block_bitmap_csum_verify(ext2_filsys fs, dgrp_t group,
				    char *bitmap, int size)
{
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);
	__u32 provided, calculated;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;
	provided = gdp->bg_block_bitmap_csum_lo;
	calculated = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap,
				      size);
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_LOCATION)
		provided |= (__u32)gdp->bg_block_bitmap_csum_hi << 16;
	else
		calculated &= 0xFFFF;

	return provided == calculated;
}

errcode_t ext2fs_block_bitmap_csum_set(ext2_filsys fs, dgrp_t group,
				       char *bitmap, int size)
{
	__u32 crc;
	struct ext4_group_desc *gdp = (struct ext4_group_desc *)
			ext2fs_group_desc(fs, fs->group_desc, group);

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)bitmap, size);
	gdp->bg_block_bitmap_csum_lo = crc & 0xFFFF;
	if (EXT2_DESC_SIZE(fs->super) >= EXT4_BG_BLOCK_BITMAP_CSUM_HI_LOCATION)
		gdp->bg_block_bitmap_csum_hi = crc >> 16;

	return 0;
}

static errcode_t ext2fs_inode_csum(ext2_filsys fs, ext2_ino_t inum,
			       struct ext2_inode_large *inode,
			       __u32 *crc, int has_hi)
{
	__u32 gen;
	struct ext2_inode_large *desc = inode;
	size_t size = EXT2_INODE_SIZE(fs->super);
	__u16 old_lo;
	__u16 old_hi = 0;

	old_lo = inode->i_checksum_lo;
	inode->i_checksum_lo = 0;
	if (has_hi) {
		old_hi = inode->i_checksum_hi;
		inode->i_checksum_hi = 0;
	}

	inum = ext2fs_cpu_to_le32(inum);
	gen = inode->i_generation;
	*crc = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&inum,
				sizeof(inum));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)&gen, sizeof(gen));
	*crc = ext2fs_crc32c_le(*crc, (unsigned char *)desc, size);

	inode->i_checksum_lo = old_lo;
	if (has_hi)
		inode->i_checksum_hi = old_hi;
	return 0;
}

int ext2fs_inode_csum_verify(ext2_filsys fs, ext2_ino_t inum,
			     struct ext2_inode_large *inode)
{
	errcode_t retval;
	__u32 provided, calculated;
	unsigned int i, has_hi;
	char *cp;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 1;

	has_hi = (EXT2_INODE_SIZE(fs->super) > EXT2_GOOD_OLD_INODE_SIZE &&
		  inode->i_extra_isize >= EXT4_INODE_CSUM_HI_EXTRA_END);

	provided = ext2fs_le16_to_cpu(inode->i_checksum_lo);
	retval = ext2fs_inode_csum(fs, inum, inode, &calculated, has_hi);
	if (retval)
		return 0;
	if (has_hi) {
		__u32 hi = ext2fs_le16_to_cpu(inode->i_checksum_hi);
		provided |= hi << 16;
	} else
		calculated &= 0xFFFF;

	if (provided == calculated)
		return 1;

	/*
	 * If the checksum didn't match, it's possible it was due to
	 * the inode being all zero's.  It's unlikely this is the
	 * case, but it can happen.  So check for it here.  (We only
	 * check the base inode since that's good enough, and it's not
	 * worth the bother to figure out how much of the extended
	 * inode, if any, is present.)
	 */
	for (cp = (char *) inode, i = 0;
	     i < sizeof(struct ext2_inode);
	     cp++, i++)
		if (*cp)
			return 0;
	return 1;		/* Inode must have been all zero's */
}

errcode_t ext2fs_inode_csum_set(ext2_filsys fs, ext2_ino_t inum,
			   struct ext2_inode_large *inode)
{
	errcode_t retval;
	__u32 crc;
	int has_hi;

	if (!ext2fs_has_feature_metadata_csum(fs->super))
		return 0;

	has_hi = (EXT2_INODE_SIZE(fs->super) > EXT2_GOOD_OLD_INODE_SIZE &&
		  inode->i_extra_isize >= EXT4_INODE_CSUM_HI_EXTRA_END);

	retval = ext2fs_inode_csum(fs, inum, inode, &crc, has_hi);
	if (retval)
		return retval;
	inode->i_checksum_lo = ext2fs_cpu_to_le16(crc & 0xFFFF);
	if (has_hi)
		inode->i_checksum_hi = ext2fs_cpu_to_le16(crc >> 16);
	return 0;
}

__u16 ext2fs_group_desc_csum(ext2_filsys fs, dgrp_t group)
{
	struct ext2_group_desc *desc = ext2fs_group_desc(fs, fs->group_desc,
							 group);
	size_t offset, size = EXT2_DESC_SIZE(fs->super);
	__u16 crc = 0;
#ifdef WORDS_BIGENDIAN
	struct ext4_group_desc swabdesc;
	size_t save_size = size;
	const size_t ext4_bg_size = sizeof(struct ext4_group_desc);
	struct ext2_group_desc *save_desc = desc;

	/* Have to swab back to little-endian to do the checksum */
	if (size > ext4_bg_size)
		size = ext4_bg_size;
	memcpy(&swabdesc, desc, size);
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &swabdesc);
	desc = (struct ext2_group_desc *) &swabdesc;
	group = ext2fs_swab32(group);
#endif

	if (ext2fs_has_feature_metadata_csum(fs->super)) {
		/* new metadata csum code */
		__u16 old_crc;
		__u32 crc32;

		old_crc = desc->bg_checksum;
		desc->bg_checksum = 0;
		crc32 = ext2fs_crc32c_le(fs->csum_seed, (unsigned char *)&group,
					 sizeof(group));
		crc32 = ext2fs_crc32c_le(crc32, (unsigned char *)desc,
					 size);
		desc->bg_checksum = old_crc;
#ifdef WORDS_BIGENDIAN
		if (save_size > ext4_bg_size)
			crc32 = ext2fs_crc32c_le(crc32,
				     (unsigned char *)save_desc + ext4_bg_size,
	                             save_size - ext4_bg_size);
#endif
		crc = crc32 & 0xFFFF;
		goto out;
	}

	/* old crc16 code */
	offset = offsetof(struct ext2_group_desc, bg_checksum);
	crc = ext2fs_crc16(~0, fs->super->s_uuid,
			   sizeof(fs->super->s_uuid));
	crc = ext2fs_crc16(crc, &group, sizeof(group));
	crc = ext2fs_crc16(crc, desc, offset);
	offset += sizeof(desc->bg_checksum); /* skip checksum */
	/* for checksum of struct ext4_group_desc do the rest...*/
	if (offset < size) {
		crc = ext2fs_crc16(crc, (char *)desc + offset,
				   size - offset);
	}
#ifdef WORDS_BIGENDIAN
	/*
	 * If the size of the bg descriptor is greater than 64
	 * bytes, which is the size of the traditional ext4 bg
	 * descriptor, checksum the rest of the descriptor here
	 */
	if (save_size > ext4_bg_size)
		crc = ext2fs_crc16(crc, (char *)save_desc + ext4_bg_size,
				   save_size - ext4_bg_size);
#endif

out:
	return crc;
}

int ext2fs_group_desc_csum_verify(ext2_filsys fs, dgrp_t group)
{
	if (ext2fs_has_group_desc_csum(fs) &&
	    (ext2fs_bg_checksum(fs, group) !=
	     ext2fs_group_desc_csum(fs, group)))
		return 0;

	return 1;
}

void ext2fs_group_desc_csum_set(ext2_filsys fs, dgrp_t group)
{
	if (!ext2fs_has_group_desc_csum(fs))
		return;

	/* ext2fs_bg_checksum_set() sets the actual checksum field but
	 * does not calculate the checksum itself. */
	ext2fs_bg_checksum_set(fs, group, ext2fs_group_desc_csum(fs, group));
}

static __u32 find_last_inode_ingrp(ext2fs_inode_bitmap bitmap,
				   __u32 inodes_per_grp, dgrp_t grp_no)
{
	ext2_ino_t i, start_ino, end_ino;

	start_ino = grp_no * inodes_per_grp + 1;
	end_ino = start_ino + inodes_per_grp - 1;

	for (i = end_ino; i >= start_ino; i--) {
		if (ext2fs_fast_test_inode_bitmap2(bitmap, i))
			return i - start_ino + 1;
	}
	return inodes_per_grp;
}

/* update the bitmap flags, set the itable high watermark, and calculate
 * checksums for the group descriptors */
errcode_t ext2fs_set_gdt_csum(ext2_filsys fs)
{
	struct ext2_super_block *sb = fs->super;
	int dirty = 0;
	dgrp_t i;

	if (!fs->inode_map)
		return EXT2_ET_NO_INODE_BITMAP;

	if (!ext2fs_has_group_desc_csum(fs))
		return 0;

	for (i = 0; i < fs->group_desc_count; i++) {
		__u32 old_csum = ext2fs_bg_checksum(fs, i);
		__u32 old_unused = ext2fs_bg_itable_unused(fs, i);
		__u32 old_flags = ext2fs_bg_flags(fs, i);
		__u32 old_free_inodes_count = ext2fs_bg_free_inodes_count(fs, i);
		__u32 old_free_blocks_count = ext2fs_bg_free_blocks_count(fs, i);

		if (old_free_blocks_count == sb->s_blocks_per_group &&
		    i != fs->group_desc_count - 1)
			ext2fs_bg_flags_set(fs, i, EXT2_BG_BLOCK_UNINIT);

		if (old_free_inodes_count == sb->s_inodes_per_group) {
			ext2fs_bg_flags_set(fs, i, EXT2_BG_INODE_UNINIT);
			ext2fs_bg_itable_unused_set(fs, i, sb->s_inodes_per_group);
		} else {
			int unused =
				sb->s_inodes_per_group -
				find_last_inode_ingrp(fs->inode_map,
						      sb->s_inodes_per_group, i);

			ext2fs_bg_flags_clear(fs, i, EXT2_BG_INODE_UNINIT);
			ext2fs_bg_itable_unused_set(fs, i, unused);
		}

		ext2fs_group_desc_csum_set(fs, i);
		if (old_flags != ext2fs_bg_flags(fs, i))
			dirty = 1;
		if (old_unused != ext2fs_bg_itable_unused(fs, i))
			dirty = 1;
		if (old_csum != ext2fs_bg_checksum(fs, i))
			dirty = 1;
	}
	if (dirty)
		ext2fs_mark_super_dirty(fs);
	return 0;
}

#ifdef DEBUG
#include "e2p/e2p.h"

void print_csum(const char *msg, ext2_filsys fs, dgrp_t group)
{
	__u16 crc1, crc2, crc3;
	dgrp_t swabgroup;
	struct ext2_group_desc *desc = ext2fs_group_desc(fs, fs->group_desc,
							 group);
	size_t size = EXT2_DESC_SIZE(fs->super);
	struct ext2_super_block *sb = fs->super;
	int offset = offsetof(struct ext2_group_desc, bg_checksum);
#ifdef WORDS_BIGENDIAN
	struct ext4_group_desc swabdesc;
	struct ext2_group_desc *save_desc = desc;
	const size_t ext4_bg_size = sizeof(struct ext4_group_desc);
	size_t save_size = size;
#endif

#ifdef WORDS_BIGENDIAN
	/* Have to swab back to little-endian to do the checksum */
	if (size > ext4_bg_size)
		size = ext4_bg_size;
	memcpy(&swabdesc, desc, size);
	ext2fs_swap_group_desc2(fs, (struct ext2_group_desc *) &swabdesc);
	desc = (struct ext2_group_desc *) &swabdesc;

	swabgroup = ext2fs_swab32(group);
#else
	swabgroup = group;
#endif

	crc1 = ext2fs_crc16(~0, sb->s_uuid, sizeof(fs->super->s_uuid));
	crc2 = ext2fs_crc16(crc1, &swabgroup, sizeof(swabgroup));
	crc3 = ext2fs_crc16(crc2, desc, offset);
	offset += sizeof(desc->bg_checksum); /* skip checksum */
	/* for checksum of struct ext4_group_desc do the rest...*/
	if (offset < size)
		crc3 = ext2fs_crc16(crc3, (char *)desc + offset, size - offset);
#ifdef WORDS_BIGENDIAN
	if (save_size > ext4_bg_size)
		crc3 = ext2fs_crc16(crc3, (char *)save_desc + ext4_bg_size,
				    save_size - ext4_bg_size);
#endif

	printf("%s UUID %s=%04x, grp %u=%04x: %04x=%04x\n",
	       msg, e2p_uuid2str(sb->s_uuid), crc1, group, crc2, crc3,
	       ext2fs_group_desc_csum(fs, group));
}

unsigned char sb_uuid[16] = { 0x4f, 0x25, 0xe8, 0xcf, 0xe7, 0x97, 0x48, 0x23,
			      0xbe, 0xfa, 0xa7, 0x88, 0x4b, 0xae, 0xec, 0xdb };

int main(int argc, char **argv)
{
	struct ext2_super_block param;
	errcode_t		retval;
	ext2_filsys		fs;
	int			i;
	__u16 csum1, csum2, csum_known = 0xd3a4;

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
#if 0
	param.s_feature_incompat |= EXT4_FEATURE_INCOMPAT_64BIT;
	param.s_desc_size = 128;
	csum_known = 0x5b6e;
#endif

	retval = ext2fs_initialize("test fs", EXT2_FLAG_64BITS, &param,
				   test_io_manager, &fs);
	if (retval) {
		com_err("setup", retval,
			"While initializing filesystem");
		exit(1);
	}
	memcpy(fs->super->s_uuid, sb_uuid, 16);
	fs->super->s_feature_ro_compat = EXT4_FEATURE_RO_COMPAT_GDT_CSUM;

	for (i=0; i < fs->group_desc_count; i++) {
		ext2fs_block_bitmap_loc_set(fs, i, 124);
		ext2fs_inode_bitmap_loc_set(fs, i, 125);
		ext2fs_inode_table_loc_set(fs, i, 126);
		ext2fs_bg_free_blocks_count_set(fs, i, 31119);
		ext2fs_bg_free_inodes_count_set(fs, i, 15701);
		ext2fs_bg_used_dirs_count_set(fs, i, 2);
		ext2fs_bg_flags_zap(fs, i);
	};

	csum1 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum0000", fs, 0);

	if (csum1 != csum_known) {
		printf("checksum for group 0 should be %04x\n", csum_known);
		exit(1);
	}
	csum2 = ext2fs_group_desc_csum(fs, 1);
	print_csum("csum0001", fs, 1);
	if (csum1 == csum2) {
		printf("checksums for different groups shouldn't match\n");
		exit(1);
	}
	csum2 = ext2fs_group_desc_csum(fs, 2);
	print_csum("csumffff", fs, 2);
	if (csum1 == csum2) {
		printf("checksums for different groups shouldn't match\n");
		exit(1);
	}
	ext2fs_bg_checksum_set(fs, 0, csum1);
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_set", fs, 0);
	if (csum1 != csum2) {
		printf("checksums should not depend on checksum field\n");
		exit(1);
	}
	if (!ext2fs_group_desc_csum_verify(fs, 0)) {
		printf("checksums should verify against gd_checksum\n");
		exit(1);
	}
	memset(fs->super->s_uuid, 0x30, sizeof(fs->super->s_uuid));
	print_csum("new_uuid", fs, 0);
	if (ext2fs_group_desc_csum_verify(fs, 0) != 0) {
		printf("checksums for different filesystems shouldn't match\n");
		exit(1);
	}
	csum1 = ext2fs_group_desc_csum(fs, 0);
	ext2fs_bg_checksum_set(fs, 0, csum1);
	print_csum("csum_new", fs, 0);
	ext2fs_bg_free_blocks_count_set(fs, 0, 1);
	csum2 = ext2fs_group_desc_csum(fs, 0);
	print_csum("csum_blk", fs, 0);
	if (csum1 == csum2) {
		printf("checksums for different data shouldn't match\n");
		exit(1);
	}
	ext2fs_free(fs);

	return 0;
}
#endif
