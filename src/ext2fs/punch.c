/*
 * punch.c --- deallocate blocks allocated to an inode
 *
 * Copyright (C) 2010 Theodore Ts'o.
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
#include <errno.h>

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

#undef PUNCH_DEBUG

/*
 * This function returns 1 if the specified block is all zeros
 */
static int check_zero_block(char *buf, int blocksize)
{
	char	*cp = buf;
	int	left = blocksize;

	while (left > 0) {
		if (*cp++)
			return 0;
		left--;
	}
	return 1;
}

/*
 * This clever recursive function handles i_blocks[] as well as
 * indirect, double indirect, and triple indirect blocks.  It iterates
 * over the entries in the i_blocks array or indirect blocks, and for
 * each one, will recursively handle any indirect blocks and then
 * frees and deallocates the blocks.
 */
static errcode_t ind_punch(ext2_filsys fs, struct ext2_inode *inode,
			   char *block_buf, blk_t *p, int level,
			   blk64_t start, blk64_t count, int max)
{
	errcode_t	retval;
	blk_t		b;
	int		i;
	blk64_t		offset, incr;
	int		freed = 0;

#ifdef PUNCH_DEBUG
	printf("Entering ind_punch, level %d, start %llu, count %llu, "
	       "max %d\n", level, start, count, max);
#endif
	incr = 1ULL << ((EXT2_BLOCK_SIZE_BITS(fs->super) - 2) * level);
	for (i = 0, offset = 0; i < max; i++, p++, offset += incr) {
		if (offset >= start + count)
			break;
		if (*p == 0 || (offset+incr) <= start)
			continue;
		b = *p;
		if (level > 0) {
			blk_t start2;
#ifdef PUNCH_DEBUG
			printf("Reading indirect block %u\n", b);
#endif
			retval = ext2fs_read_ind_block(fs, b, block_buf);
			if (retval)
				return retval;
			start2 = (start > offset) ? start - offset : 0;
			retval = ind_punch(fs, inode, block_buf + fs->blocksize,
					   (blk_t *) block_buf, level - 1,
					   start2, count - offset,
					   fs->blocksize >> 2);
			if (retval)
				return retval;
			retval = ext2fs_write_ind_block(fs, b, block_buf);
			if (retval)
				return retval;
			if (!check_zero_block(block_buf, fs->blocksize))
				continue;
		}
#ifdef PUNCH_DEBUG
		printf("Freeing block %u (offset %llu)\n", b, offset);
#endif
		ext2fs_block_alloc_stats(fs, b, -1);
		*p = 0;
		freed++;
	}
#ifdef PUNCH_DEBUG
	printf("Freed %d blocks\n", freed);
#endif
	return ext2fs_iblk_sub_blocks(fs, inode, freed);
}

#define BLK_T_MAX ((blk_t)~0ULL)
static errcode_t ext2fs_punch_ind(ext2_filsys fs, struct ext2_inode *inode,
				  char *block_buf, blk64_t start, blk64_t end)
{
	errcode_t		retval;
	char			*buf = 0;
	int			level;
	int			num = EXT2_NDIR_BLOCKS;
	blk_t			*bp = inode->i_block;
	blk_t			addr_per_block;
	blk64_t			max = EXT2_NDIR_BLOCKS;
	blk_t			count;

	/* Check start/end don't overflow the 2^32-1 indirect block limit */
	if (start > BLK_T_MAX)
		return 0;
	if (end >= BLK_T_MAX || end - start + 1 >= BLK_T_MAX)
		count = BLK_T_MAX - start;
	else
		count = end - start + 1;

	if (!block_buf) {
		retval = ext2fs_get_array(3, fs->blocksize, &buf);
		if (retval)
			return retval;
		block_buf = buf;
	}

	addr_per_block = (blk_t)fs->blocksize >> 2;

	for (level = 0; level < 4; level++, max *= (blk64_t)addr_per_block) {
#ifdef PUNCH_DEBUG
		printf("Main loop level %d, start %llu count %u "
		       "max %llu num %d\n", level, start, count, max, num);
#endif
		if (start < max) {
			retval = ind_punch(fs, inode, block_buf, bp, level,
					   start, count, num);
			if (retval)
				goto errout;
			if (count > max)
				count -= max - start;
			else
				break;
			start = 0;
		} else
			start -= max;
		bp += num;
		if (level == 0) {
			num = 1;
			max = 1;
		}
	}
	retval = 0;
errout:
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}
#undef BLK_T_MAX

#ifdef PUNCH_DEBUG

#define dbg_printf(f, a...)  printf(f, ## a)

static void dbg_print_extent(char *desc, struct ext2fs_extent *extent)
{
	if (desc)
		printf("%s: ", desc);
	printf("extent: lblk %llu--%llu, len %u, pblk %llu, flags: ",
	       extent->e_lblk, extent->e_lblk + extent->e_len - 1,
	       extent->e_len, extent->e_pblk);
	if (extent->e_flags & EXT2_EXTENT_FLAGS_LEAF)
		fputs("LEAF ", stdout);
	if (extent->e_flags & EXT2_EXTENT_FLAGS_UNINIT)
		fputs("UNINIT ", stdout);
	if (extent->e_flags & EXT2_EXTENT_FLAGS_SECOND_VISIT)
		fputs("2ND_VISIT ", stdout);
	if (!extent->e_flags)
		fputs("(none)", stdout);
	fputc('\n', stdout);

}
#else
#define dbg_print_extent(desc, ex)	do { } while (0)
#define dbg_printf(f, ...)		do { } while (0)
#endif

/* Free a range of blocks, respecting cluster boundaries */
static errcode_t punch_extent_blocks(ext2_filsys fs, ext2_ino_t ino,
				     struct ext2_inode *inode,
				     blk64_t lfree_start, blk64_t free_start,
				     __u32 free_count, int *freed)
{
	blk64_t		pblk;
	int		freed_now = 0;
	__u32		cluster_freed;
	errcode_t	retval = 0;

	/* No bigalloc?  Just free each block. */
	if (EXT2FS_CLUSTER_RATIO(fs) == 1) {
		*freed += free_count;
		while (free_count-- > 0)
			ext2fs_block_alloc_stats2(fs, free_start++, -1);
		return retval;
	}

	/*
	 * Try to free up to the next cluster boundary.  We assume that all
	 * blocks in a logical cluster map to blocks from the same physical
	 * cluster, and that the offsets within the [pl]clusters match.
	 */
	if (free_start & EXT2FS_CLUSTER_MASK(fs)) {
		retval = ext2fs_map_cluster_block(fs, ino, inode,
						  lfree_start, &pblk);
		if (retval)
			goto errout;
		if (!pblk) {
			ext2fs_block_alloc_stats2(fs, free_start, -1);
			freed_now++;
		}
		cluster_freed = EXT2FS_CLUSTER_RATIO(fs) -
			(free_start & EXT2FS_CLUSTER_MASK(fs));
		if (cluster_freed > free_count)
			cluster_freed = free_count;
		free_count -= cluster_freed;
		free_start += cluster_freed;
		lfree_start += cluster_freed;
	}

	/* Free whole clusters from the middle of the range. */
	while (free_count > 0 && free_count >= (unsigned) EXT2FS_CLUSTER_RATIO(fs)) {
		ext2fs_block_alloc_stats2(fs, free_start, -1);
		freed_now++;
		cluster_freed = EXT2FS_CLUSTER_RATIO(fs);
		free_count -= cluster_freed;
		free_start += cluster_freed;
		lfree_start += cluster_freed;
	}

	/* Try to free the last cluster. */
	if (free_count > 0) {
		retval = ext2fs_map_cluster_block(fs, ino, inode,
						  lfree_start, &pblk);
		if (retval)
			goto errout;
		if (!pblk) {
			ext2fs_block_alloc_stats2(fs, free_start, -1);
			freed_now++;
		}
	}

errout:
	*freed += freed_now;
	return retval;
}

static errcode_t ext2fs_punch_extent(ext2_filsys fs, ext2_ino_t ino,
				     struct ext2_inode *inode,
				     blk64_t start, blk64_t end)
{
	ext2_extent_handle_t	handle = 0;
	struct ext2fs_extent	extent;
	errcode_t		retval;
	blk64_t			free_start, next, lfree_start;
	__u32			free_count, newlen;
	int			freed = 0;
	int			op;

	retval = ext2fs_extent_open2(fs, ino, inode, &handle);
	if (retval)
		return retval;
	/*
	 * Find the extent closest to the start of the punch range.  We don't
	 * check the return value because _goto() sets the current node to the
	 * next-lowest extent if 'start' is in a hole, and doesn't set a
	 * current node if there was a real error reading the extent tree.
	 * In that case, _get() will error out.
	 *
	 * Note: If _get() returns 'no current node', that simply means that
	 * there aren't any blocks mapped past this point in the file, so we're
	 * done.
	 */
	retval = ext2fs_extent_goto(handle, start);
	if (retval)
		goto errout;
	retval = ext2fs_extent_get(handle, EXT2_EXTENT_CURRENT, &extent);
	if (retval == EXT2_ET_NO_CURRENT_NODE) {
		retval = 0;
		goto errout;
	} else if (retval)
		goto errout;
	while (1) {
		op = EXT2_EXTENT_NEXT_LEAF;
		dbg_print_extent("main loop", &extent);
		next = extent.e_lblk + extent.e_len;
		dbg_printf("start %llu, end %llu, next %llu\n",
			   (unsigned long long) start,
			   (unsigned long long) end,
			   (unsigned long long) next);
		if (start <= extent.e_lblk) {
			/*
			 * Have we iterated past the end of the punch region?
			 * If so, we can stop.
			 */
			if (end < extent.e_lblk)
				break;
			dbg_printf("Case #%d\n", 1);
			/* Start of deleted region before extent; 
			   adjust beginning of extent */
			free_start = extent.e_pblk;
			lfree_start = extent.e_lblk;
			if (next > end)
				free_count = end - extent.e_lblk + 1;
			else
				free_count = extent.e_len;
			extent.e_len -= free_count;
			extent.e_lblk += free_count;
			extent.e_pblk += free_count;
		} else if (end >= next-1) {
			/*
			 * Is the punch region beyond this extent?  This can
			 * happen if start is already inside a hole.  Try to
			 * advance to the next extent if this is the case.
			 */
			if (start >= next)
				goto next_extent;
			/* End of deleted region after extent;
			   adjust end of extent */
			dbg_printf("Case #%d\n", 2);
			newlen = start - extent.e_lblk;
			free_start = extent.e_pblk + newlen;
			lfree_start = extent.e_lblk + newlen;
			free_count = extent.e_len - newlen;
			extent.e_len = newlen;
		} else {
			struct ext2fs_extent	newex;

			dbg_printf("Case #%d\n", 3);
			/* The hard case; we need to split the extent */
			newex.e_pblk = extent.e_pblk +
				(end + 1 - extent.e_lblk);
			newex.e_lblk = end + 1;
			newex.e_len = next - end - 1;
			newex.e_flags = extent.e_flags;

			extent.e_len = start - extent.e_lblk;
			free_start = extent.e_pblk + extent.e_len;
			lfree_start = extent.e_lblk + extent.e_len;
			free_count = end - start + 1;

			dbg_print_extent("inserting", &newex);
			retval = ext2fs_extent_insert(handle,
					EXT2_EXTENT_INSERT_AFTER, &newex);
			if (retval)
				goto errout;
			retval = ext2fs_extent_fix_parents(handle);
			if (retval)
				goto errout;
			/*
			 * Now pointing at inserted extent; so go back.
			 *
			 * We cannot use EXT2_EXTENT_PREV to go back; note the
			 * subtlety in the comment for fix_parents().
			 */
			retval = ext2fs_extent_goto(handle, extent.e_lblk);
			if (retval)
				goto errout;
		} 
		if (extent.e_len) {
			dbg_print_extent("replacing", &extent);
			retval = ext2fs_extent_replace(handle, 0, &extent);
			if (retval)
				goto errout;
			retval = ext2fs_extent_fix_parents(handle);
		} else {
			struct ext2fs_extent	newex;
			blk64_t			old_lblk, next_lblk;
			dbg_printf("deleting current extent%s\n", "");

			/*
			 * Save the location of the next leaf, then slip
			 * back to the current extent.
			 */
			retval = ext2fs_extent_get(handle, EXT2_EXTENT_CURRENT,
						   &newex);
			if (retval)
				goto errout;
			old_lblk = newex.e_lblk;

			retval = ext2fs_extent_get(handle,
						   EXT2_EXTENT_NEXT_LEAF,
						   &newex);
			if (retval == EXT2_ET_EXTENT_NO_NEXT)
				next_lblk = old_lblk;
			else if (retval)
				goto errout;
			else
				next_lblk = newex.e_lblk;

			retval = ext2fs_extent_goto(handle, old_lblk);
			if (retval)
				goto errout;

			/* Now delete the extent. */
			retval = ext2fs_extent_delete(handle, 0);
			if (retval)
				goto errout;

			retval = ext2fs_extent_fix_parents(handle);
			if (retval && retval != EXT2_ET_NO_CURRENT_NODE)
				goto errout;
			retval = 0;

			/*
			 * Jump forward to the next extent.  If there are
			 * errors, the ext2fs_extent_get down below will
			 * capture them for us.
			 */
			(void)ext2fs_extent_goto(handle, next_lblk);
			op = EXT2_EXTENT_CURRENT;
		}
		if (retval)
			goto errout;
		dbg_printf("Free start %llu, free count = %u\n",
		       free_start, free_count);
		retval = punch_extent_blocks(fs, ino, inode, lfree_start,
					     free_start, free_count, &freed);
		if (retval)
			goto errout;
	next_extent:
		retval = ext2fs_extent_get(handle, op,
					   &extent);
		if (retval == EXT2_ET_EXTENT_NO_NEXT ||
		    retval == EXT2_ET_NO_CURRENT_NODE)
			break;
		if (retval)
			goto errout;
	}
	dbg_printf("Freed %d blocks\n", freed);
	retval = ext2fs_iblk_sub_blocks(fs, inode, freed);
errout:
	ext2fs_extent_free(handle);
	return retval;
}
	
static errcode_t ext2fs_punch_inline_data(ext2_filsys fs, ext2_ino_t ino,
					  struct ext2_inode *inode,
					  blk64_t start,
					  blk64_t end EXT2FS_ATTR((unused)))
{
	errcode_t retval;

	/*
	 * In libext2fs ext2fs_punch is based on block unit.  So that
	 * means that if start > 0 we don't need to do nothing.  Due
	 * to this we will remove all inline data in ext2fs_punch()
	 * now.
	 */
	if (start > 0)
		return 0;

	memset((char *)inode->i_block, 0, EXT4_MIN_INLINE_DATA_SIZE);
	inode->i_size = 0;
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		return retval;

	return ext2fs_inline_data_ea_remove(fs, ino);
}

/*
 * Deallocate all logical _blocks_ starting at start to end, inclusive.
 * If end is ~0ULL, then this is effectively truncate.
 */
errcode_t ext2fs_punch(ext2_filsys fs, ext2_ino_t ino,
		       struct ext2_inode *inode,
		       char *block_buf, blk64_t start,
		       blk64_t end)
{
	errcode_t		retval;
	struct ext2_inode	inode_buf;

	if (start > end)
		return EINVAL;

	/* Read inode structure if necessary */
	if (!inode) {
		retval = ext2fs_read_inode(fs, ino, &inode_buf);
		if (retval)
			return retval;
		inode = &inode_buf;
	}
	if (inode->i_flags & EXT4_INLINE_DATA_FL)
		return ext2fs_punch_inline_data(fs, ino, inode, start, end);
	else if (inode->i_flags & EXT4_EXTENTS_FL)
		retval = ext2fs_punch_extent(fs, ino, inode, start, end);
	else
		retval = ext2fs_punch_ind(fs, inode, block_buf, start, end);
	if (retval)
		return retval;

#ifdef PUNCH_DEBUG
	printf("%u: write inode size now %u blocks %u\n",
		ino, inode->i_size, inode->i_blocks);
#endif
	return ext2fs_write_inode(fs, ino, inode);
}
