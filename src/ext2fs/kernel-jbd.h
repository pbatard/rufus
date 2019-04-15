/*
 * linux/include/linux/jbd.h
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>
 *
 * Copyright 1998-2000 Red Hat, Inc --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Definitions for transaction data structures for the buffer cache
 * filesystem journaling support.
 */

#ifndef _LINUX_JBD_H
#define _LINUX_JBD_H

#include "jfs_compat.h"
#define JFS_DEBUG
#define jfs_debug jbd_debug

#if !defined(__GNUC__) && !defined(_MSC_VER)
#define __FUNCTION__ ""
#endif

#define journal_oom_retry 1

#ifdef __STDC__
#ifdef CONFIG_JBD_DEBUG
/*
 * Define JBD_EXPENSIVE_CHECKING to enable more expensive internal
 * consistency checks.  By default we don't do this unless
 * CONFIG_JBD_DEBUG is on.
 */
#define JBD_EXPENSIVE_CHECKING
extern int journal_enable_debug;

#define jbd_debug(n, f, a...)						\
	do {								\
		if ((n) <= journal_enable_debug) {			\
			printk (KERN_DEBUG "(%s, %d): %s: ",		\
				__FILE__, __LINE__, __FUNCTION__);	\
		  	printk (f, ## a);				\
		}							\
	} while (0)
#else
#ifdef __GNUC__
#if defined(__KERNEL__) || !defined(CONFIG_JBD_DEBUG)
#define jbd_debug(f, a...)	/**/
#else
extern int journal_enable_debug;
#define jbd_debug(n, f, a...)						\
	do {								\
		if ((n) <= journal_enable_debug) {			\
			printf("(%s, %d): %s: ",			\
				__FILE__, __LINE__, __func__);		\
			printf(f, ## a);				\
		}							\
	} while (0)
#endif /*__KERNEL__ */
#else
#define jbd_debug(f, ...)	/**/
#endif
#endif
#else
#define jbd_debug(x)		/* AIX doesn't do STDC */
#endif

extern void * __jbd_kmalloc (char *where, size_t size, int flags, int retry);
#define jbd_kmalloc(size, flags) \
	__jbd_kmalloc(__FUNCTION__, (size), (flags), journal_oom_retry)
#define jbd_rep_kmalloc(size, flags) \
	__jbd_kmalloc(__FUNCTION__, (size), (flags), 1)

#define JFS_MIN_JOURNAL_BLOCKS 1024

/*
 * Internal structures used by the logging mechanism:
 */

#define JFS_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */

/*
 * On-disk structures
 */

/*
 * Descriptor block types:
 */

#define JFS_DESCRIPTOR_BLOCK	1
#define JFS_COMMIT_BLOCK	2
#define JFS_SUPERBLOCK_V1	3
#define JFS_SUPERBLOCK_V2	4
#define JFS_REVOKE_BLOCK	5

/*
 * Standard header for all descriptor blocks:
 */
typedef struct journal_header_s
{
	__u32		h_magic;
	__u32		h_blocktype;
	__u32		h_sequence;
} journal_header_t;

/*
 * Checksum types.
 */
#define JBD2_CRC32_CHKSUM   1
#define JBD2_MD5_CHKSUM     2
#define JBD2_SHA1_CHKSUM    3
#define JBD2_CRC32C_CHKSUM  4

#define JBD2_CRC32_CHKSUM_SIZE 4

#define JBD2_CHECKSUM_BYTES (32 / sizeof(__u32))
/*
 * Commit block header for storing transactional checksums:
 *
 * NOTE: If FEATURE_COMPAT_CHECKSUM (checksum v1) is set, the h_chksum*
 * fields are used to store a checksum of the descriptor and data blocks.
 *
 * If FEATURE_INCOMPAT_CSUM_V2 (checksum v2) is set, then the h_chksum
 * field is used to store crc32c(uuid+commit_block).  Each journal metadata
 * block gets its own checksum, and data block checksums are stored in
 * journal_block_tag (in the descriptor).  The other h_chksum* fields are
 * not used.
 *
 * If FEATURE_INCOMPAT_CSUM_V3 is set, the descriptor block uses
 * journal_block_tag3_t to store a full 32-bit checksum.  Everything else
 * is the same as v2.
 *
 * Checksum v1, v2, and v3 are mutually exclusive features.
 */
struct commit_header {
	__u32		h_magic;
	__u32		h_blocktype;
	__u32		h_sequence;
	unsigned char	h_chksum_type;
	unsigned char	h_chksum_size;
	unsigned char	h_padding[2];
	__u32		h_chksum[JBD2_CHECKSUM_BYTES];
	__u64		h_commit_sec;
	__u32		h_commit_nsec;
};

/*
 * The block tag: used to describe a single buffer in the journal
 */
typedef struct journal_block_tag3_s
{
	__u32		t_blocknr;	/* The on-disk block number */
	__u32		t_flags;	/* See below */
	__u32		t_blocknr_high; /* most-significant high 32bits. */
	__u32		t_checksum;	/* crc32c(uuid+seq+block) */
} journal_block_tag3_t;

typedef struct journal_block_tag_s
{
	__u32		t_blocknr;	/* The on-disk block number */
	__u16		t_checksum;	/* truncated crc32c(uuid+seq+block) */
	__u16		t_flags;	/* See below */
	__u32		t_blocknr_high; /* most-significant high 32bits. */
} journal_block_tag_t;

/* Tail of descriptor block, for checksumming */
struct journal_block_tail {
	__be32		t_checksum;
};

/*
 * The revoke descriptor: used on disk to describe a series of blocks to
 * be revoked from the log
 */
typedef struct journal_revoke_header_s
{
	journal_header_t r_header;
	int		 r_count;	/* Count of bytes used in the block */
} journal_revoke_header_t;

/* Tail of revoke block, for checksumming */
struct journal_revoke_tail {
	__be32		r_checksum;
};

/* Definitions for the journal tag flags word: */
#define JFS_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define JFS_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JFS_FLAG_DELETED	4	/* block deleted by this transaction */
#define JFS_FLAG_LAST_TAG	8	/* last tag in this descriptor block */


#define UUID_SIZE 16
#define JFS_USERS_MAX 48
#define JFS_USERS_SIZE (UUID_SIZE * JFS_USERS_MAX)
/*
 * The journal superblock.  All fields are in big-endian byte order.
 */
typedef struct journal_superblock_s
{
/* 0x0000 */
	journal_header_t s_header;

/* 0x000C */
	/* Static information describing the journal */
	__u32	s_blocksize;		/* journal device blocksize */
	__u32	s_maxlen;		/* total blocks in journal file */
	__u32	s_first;		/* first block of log information */

/* 0x0018 */
	/* Dynamic information describing the current state of the log */
	__u32	s_sequence;		/* first commit ID expected in log */
	__u32	s_start;		/* blocknr of start of log */

/* 0x0020 */
	/* Error value, as set by journal_abort(). */
	__s32	s_errno;

/* 0x0024 */
	/* Remaining fields are only valid in a version-2 superblock */
	__u32	s_feature_compat; 	/* compatible feature set */
	__u32	s_feature_incompat; 	/* incompatible feature set */
	__u32	s_feature_ro_compat; 	/* readonly-compatible feature set */
/* 0x0030 */
	__u8	s_uuid[16];		/* 128-bit uuid for journal */

/* 0x0040 */
	__u32	s_nr_users;		/* Nr of filesystems sharing log */

	__u32	s_dynsuper;		/* Blocknr of dynamic superblock copy*/

/* 0x0048 */
	__u32	s_max_transaction;	/* Limit of journal blocks per trans.*/
	__u32	s_max_trans_data;	/* Limit of data blocks per trans. */

/* 0x0050 */
	__u8	s_checksum_type;	/* checksum type */
	__u8	s_padding2[3];
	__u32	s_padding[42];
	__u32	s_checksum;		/* crc32c(superblock) */

/* 0x0100 */
	__u8	s_users[JFS_USERS_SIZE];		/* ids of all fs'es sharing the log */

/* 0x0400 */
} journal_superblock_t;

#define JFS_HAS_COMPAT_FEATURE(j,mask)					\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_compat & ext2fs_cpu_to_be32((mask))))
#define JFS_HAS_RO_COMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_ro_compat & ext2fs_cpu_to_be32((mask))))
#define JFS_HAS_INCOMPAT_FEATURE(j,mask)				\
	((j)->j_format_version >= 2 &&					\
	 ((j)->j_superblock->s_feature_incompat & ext2fs_cpu_to_be32((mask))))

#define JFS_FEATURE_COMPAT_CHECKSUM		0x00000001

#define JFS_FEATURE_INCOMPAT_REVOKE		0x00000001
#define JFS_FEATURE_INCOMPAT_64BIT		0x00000002
#define JFS_FEATURE_INCOMPAT_ASYNC_COMMIT	0x00000004
#define JFS_FEATURE_INCOMPAT_CSUM_V2		0x00000008
#define JFS_FEATURE_INCOMPAT_CSUM_V3		0x00000010

/* Features known to this kernel version: */
#define JFS_KNOWN_COMPAT_FEATURES	0
#define JFS_KNOWN_ROCOMPAT_FEATURES	0
#define JFS_KNOWN_INCOMPAT_FEATURES	(JFS_FEATURE_INCOMPAT_REVOKE|\
					 JFS_FEATURE_INCOMPAT_ASYNC_COMMIT|\
					 JFS_FEATURE_INCOMPAT_64BIT|\
					 JFS_FEATURE_INCOMPAT_CSUM_V2|\
					 JFS_FEATURE_INCOMPAT_CSUM_V3)

#ifdef NO_INLINE_FUNCS
extern size_t journal_tag_bytes(journal_t *journal);
extern int journal_has_csum_v2or3(journal_t *journal);
extern int tid_gt(tid_t x, tid_t y) EXT2FS_ATTR((unused));
extern int tid_geq(tid_t x, tid_t y) EXT2FS_ATTR((unused));
#endif

#if (defined(E2FSCK_INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS))
#ifdef E2FSCK_INCLUDE_INLINE_FUNCS
#if (__STDC_VERSION__ >= 199901L)
#define _INLINE_ extern inline
#else
#define _INLINE_ inline
#endif
#else /* !E2FSCK_INCLUDE_INLINE FUNCS */
#if (__STDC_VERSION__ >= 199901L)
#define _INLINE_ inline
#else /* not C99 */
#ifdef __GNUC__
#define _INLINE_ extern __inline__
#else				/* For Watcom C */
#define _INLINE_ extern inline
#endif /* __GNUC__ */
#endif /* __STDC_VERSION__ >= 199901L */
#endif /* INCLUDE_INLINE_FUNCS */

/* journal feature predicate functions */
#define JFS_FEATURE_COMPAT_FUNCS(name, flagname) \
_INLINE_ int jfs_has_feature_##name(journal_t *j); \
_INLINE_ int jfs_has_feature_##name(journal_t *j) \
{ \
	return ((j)->j_format_version >= 2 && \
		((j)->j_superblock->s_feature_compat & \
		 ext2fs_cpu_to_be32(JFS_FEATURE_COMPAT_##flagname)) != 0); \
} \
_INLINE_ void jfs_set_feature_##name(journal_t *j); \
_INLINE_ void jfs_set_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_compat |= \
		ext2fs_cpu_to_be32(JFS_FEATURE_COMPAT_##flagname); \
} \
_INLINE_ void jfs_clear_feature_##name(journal_t *j); \
_INLINE_ void jfs_clear_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_compat &= \
		~ext2fs_cpu_to_be32(JFS_FEATURE_COMPAT_##flagname); \
}

#define JFS_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
_INLINE_ int jfs_has_feature_##name(journal_t *j);	\
_INLINE_ int jfs_has_feature_##name(journal_t *j) \
{ \
	return ((j)->j_format_version >= 2 && \
		((j)->j_superblock->s_feature_ro_compat & \
		 ext2fs_cpu_to_be32(JFS_FEATURE_RO_COMPAT_##flagname)) != 0); \
} \
_INLINE_ void jfs_set_feature_##name(journal_t *j); \
_INLINE_ void jfs_set_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_ro_compat |= \
		ext2fs_cpu_to_be32(JFS_FEATURE_RO_COMPAT_##flagname); \
} \
_INLINE_ void jfs_clear_feature_##name(journal_t *j); \
_INLINE_ void jfs_clear_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_ro_compat &= \
		~ext2fs_cpu_to_be32(JFS_FEATURE_RO_COMPAT_##flagname); \
}

#define JFS_FEATURE_INCOMPAT_FUNCS(name, flagname) \
_INLINE_ int jfs_has_feature_##name(journal_t *j); \
_INLINE_ int jfs_has_feature_##name(journal_t *j) \
{ \
	return ((j)->j_format_version >= 2 && \
		((j)->j_superblock->s_feature_incompat & \
		 ext2fs_cpu_to_be32(JFS_FEATURE_INCOMPAT_##flagname)) != 0); \
} \
_INLINE_ void jfs_set_feature_##name(journal_t *j); \
_INLINE_ void jfs_set_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_incompat |= \
		ext2fs_cpu_to_be32(JFS_FEATURE_INCOMPAT_##flagname); \
} \
_INLINE_ void jfs_clear_feature_##name(journal_t *j); \
_INLINE_ void jfs_clear_feature_##name(journal_t *j) \
{ \
	(j)->j_superblock->s_feature_incompat &= \
		~ext2fs_cpu_to_be32(JFS_FEATURE_INCOMPAT_##flagname); \
}

#else
#define JFS_FEATURE_COMPAT_FUNCS(name, flagname) \
extern int jfs_has_feature_##name(journal_t *j); \
extern void jfs_set_feature_##name(journal_t *j); \
extern void jfs_clear_feature_##name(journal_t *j);

#define JFS_FEATURE_RO_COMPAT_FUNCS(name, flagname) \
extern int jfs_has_feature_##name(journal_t *j); \
extern void jfs_set_feature_##name(journal_t *j); \
extern void jfs_clear_feature_##name(journal_t *j);

#define JFS_FEATURE_INCOMPAT_FUNCS(name, flagname) \
extern int jfs_has_feature_##name(journal_t *j); \
extern void jfs_set_feature_##name(journal_t *j); \
extern void jfs_clear_feature_##name(journal_t *j);

#endif /* (defined(E2FSCK_INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS)) */

JFS_FEATURE_COMPAT_FUNCS(checksum,		CHECKSUM)

JFS_FEATURE_INCOMPAT_FUNCS(revoke,		REVOKE)
JFS_FEATURE_INCOMPAT_FUNCS(64bit,		64BIT)
JFS_FEATURE_INCOMPAT_FUNCS(async_commit,	ASYNC_COMMIT)
JFS_FEATURE_INCOMPAT_FUNCS(csum2,		CSUM_V2)
JFS_FEATURE_INCOMPAT_FUNCS(csum3,		CSUM_V3)

#if (defined(E2FSCK_INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS))
/*
 * helper functions to deal with 32 or 64bit block numbers.
 */
_INLINE_ size_t journal_tag_bytes(journal_t *journal)
{
	size_t sz;

	if (jfs_has_feature_csum3(journal))
		return sizeof(journal_block_tag3_t);

	sz = sizeof(journal_block_tag_t);

	if (jfs_has_feature_csum2(journal))
		sz += sizeof(__u16);

	if (jfs_has_feature_64bit(journal))
		return sz;

	return sz - sizeof(__u32);
}

_INLINE_ int journal_has_csum_v2or3(journal_t *journal)
{
	if (jfs_has_feature_csum2(journal) || jfs_has_feature_csum3(journal))
		return 1;

	return 0;
}

/* Comparison functions for transaction IDs: perform comparisons using
 * modulo arithmetic so that they work over sequence number wraps. */

_INLINE_ int tid_gt(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference > 0);
}

_INLINE_ int tid_geq(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference >= 0);
}
#endif /* (defined(E2FSCK_INCLUDE_INLINE_FUNCS) || !defined(NO_INLINE_FUNCS)) */

#undef _INLINE_

extern int journal_blocks_per_page(struct inode *inode);

/*
 * Definitions which augment the buffer_head layer
 */

/* journaling buffer types */
#define BJ_None		0	/* Not journaled */
#define BJ_SyncData	1	/* Normal data: flush before commit */
#define BJ_AsyncData	2	/* writepage data: wait on it before commit */
#define BJ_Metadata	3	/* Normal journaled metadata */
#define BJ_Forget	4	/* Buffer superceded by this transaction */
#define BJ_IO		5	/* Buffer is for temporary IO use */
#define BJ_Shadow	6	/* Buffer contents being shadowed to the log */
#define BJ_LogCtl	7	/* Buffer contains log descriptors */
#define BJ_Reserved	8	/* Buffer is reserved for access by journal */
#define BJ_Types	9

extern int jbd_blocks_per_page(struct inode *inode);

#endif	/* _LINUX_JBD_H */
