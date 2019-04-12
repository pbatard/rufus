/*
 * Copyright (c) 2003,2004 Cluster File Systems, Inc, info@clusterfs.com
 * Written by Alex Tomas <alex@clusterfs.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Library
 * General Public License, version 2.
 * %End-Header%
 */

#ifndef _LINUX_EXT3_EXTENTS
#define _LINUX_EXT3_EXTENTS

/*
 * ext3_inode has i_block array (total 60 bytes)
 * first 4 bytes are used to store:
 *  - tree depth (0 mean there is no tree yet. all extents in the inode)
 *  - number of alive extents in the inode
 */

/*
 * This is extent tail on-disk structure.
 * All other extent structures are 12 bytes long.  It turns out that
 * block_size % 12 >= 4 for at least all powers of 2 greater than 512, which
 * covers all valid ext4 block sizes.  Therefore, this tail structure can be
 * crammed into the end of the block without having to rebalance the tree.
 */
struct ext3_extent_tail {
	__le32	et_checksum;	/* crc32c(uuid+inum+extent_block) */
};

/*
 * this is extent on-disk structure
 * it's used at the bottom of the tree
 */
struct ext3_extent {
	__le32	ee_block;	/* first logical block extent covers */
	__le16	ee_len;		/* number of blocks covered by extent */
	__le16	ee_start_hi;	/* high 16 bits of physical block */
	__le32	ee_start;	/* low 32 bigs of physical block */
};

/*
 * this is index on-disk structure
 * it's used at all the levels, but the bottom
 */
struct ext3_extent_idx {
	__le32	ei_block;	/* index covers logical blocks from 'block' */
	__le32	ei_leaf;	/* pointer to the physical block of the next *
				 * level. leaf or next index could bet here */
	__le16	ei_leaf_hi;	/* high 16 bits of physical block */
	__le16	ei_unused;
};

/*
 * each block (leaves and indexes), even inode-stored has header
 */
struct ext3_extent_header {
	__le16	eh_magic;	/* probably will support different formats */
	__le16	eh_entries;	/* number of valid entries */
	__le16	eh_max;		/* capacity of store in entries */
	__le16	eh_depth;	/* has tree real underlying blocks? */
	__le32	eh_generation;	/* generation of the tree */
};

#define EXT3_EXT_MAGIC		0xf30a

/*
 * array of ext3_ext_path contains path to some extent
 * creation/lookup routines use it for traversal/splitting/etc
 * truncate uses it to simulate recursive walking
 */
struct ext3_ext_path {
	__u32				p_block;
	__u16				p_depth;
	struct ext3_extent		*p_ext;
	struct ext3_extent_idx		*p_idx;
	struct ext3_extent_header	*p_hdr;
	struct buffer_head		*p_bh;
};

/*
 * EXT_INIT_MAX_LEN is the maximum number of blocks we can have in an
 * initialized extent. This is 2^15 and not (2^16 - 1), since we use the
 * MSB of ee_len field in the extent datastructure to signify if this
 * particular extent is an initialized extent or an uninitialized (i.e.
 * preallocated).
 * EXT_UNINIT_MAX_LEN is the maximum number of blocks we can have in an
 * uninitialized extent.
 * If ee_len is <= 0x8000, it is an initialized extent. Otherwise, it is an
 * uninitialized one. In other words, if MSB of ee_len is set, it is an
 * uninitialized extent with only one special scenario when ee_len = 0x8000.
 * In this case we can not have an uninitialized extent of zero length and
 * thus we make it as a special case of initialized extent with 0x8000 length.
 * This way we get better extent-to-group alignment for initialized extents.
 * Hence, the maximum number of blocks we can have in an *initialized*
 * extent is 2^15 (32768) and in an *uninitialized* extent is 2^15-1 (32767).
 */
#define EXT_INIT_MAX_LEN	(1UL << 15)
#define EXT_UNINIT_MAX_LEN	(EXT_INIT_MAX_LEN - 1)
#define EXT_MAX_EXTENT_LBLK	(((__u64) 1 << 32) - 1)
#define EXT_MAX_EXTENT_PBLK	(((__u64) 1 << 48) - 1)

#define EXT_FIRST_EXTENT(__hdr__) \
	((struct ext3_extent *) (((char *) (__hdr__)) +		\
				 sizeof(struct ext3_extent_header)))
#define EXT_FIRST_INDEX(__hdr__) \
	((struct ext3_extent_idx *) (((char *) (__hdr__)) +	\
				     sizeof(struct ext3_extent_header)))
#define EXT_HAS_FREE_INDEX(__path__) \
	(ext2fs_le16_to_cpu((__path__)->p_hdr->eh_entries) < \
	 ext2fs_le16_to_cpu((__path__)->p_hdr->eh_max))
#define EXT_LAST_EXTENT(__hdr__) \
	(EXT_FIRST_EXTENT((__hdr__)) + \
	ext2fs_le16_to_cpu((__hdr__)->eh_entries) - 1)
#define EXT_LAST_INDEX(__hdr__) \
	(EXT_FIRST_INDEX((__hdr__)) + \
	ext2fs_le16_to_cpu((__hdr__)->eh_entries) - 1)
#define EXT_MAX_EXTENT(__hdr__) \
	(EXT_FIRST_EXTENT((__hdr__)) + \
	ext2fs_le16_to_cpu((__hdr__)->eh_max) - 1)
#define EXT_MAX_INDEX(__hdr__) \
	(EXT_FIRST_INDEX((__hdr__)) + \
	ext2fs_le16_to_cpu((__hdr__)->eh_max) - 1)

#endif /* _LINUX_EXT3_EXTENTS */

