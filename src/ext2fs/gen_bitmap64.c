/*
 * gen_bitmap64.c --- routines to read, write, and manipulate the new qinode and
 * block bitmaps.
 *
 * Copyright (C) 2007, 2008 Theodore Ts'o.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
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
#include <errno.h>
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include "ext2_fs.h"
#include "ext2fsP.h"
#include "bmap64.h"

/*
 * Design of 64-bit bitmaps
 *
 * In order maintain ABI compatibility with programs that don't
 * understand about 64-bit blocks/inodes,
 * ext2fs_allocate_inode_bitmap() and ext2fs_allocate_block_bitmap()
 * will create old-style bitmaps unless the application passes the
 * flag EXT2_FLAG_64BITS to ext2fs_open().  If this flag is
 * passed, then we know the application has been recompiled, so we can
 * use the new-style bitmaps.  If it is not passed, we have to return
 * an error if trying to open a filesystem which needs 64-bit bitmaps.
 *
 * The new bitmaps use a new set of structure magic numbers, so that
 * both the old-style and new-style interfaces can identify which
 * version of the data structure was used.  Both the old-style and
 * new-style interfaces will support either type of bitmap, although
 * of course 64-bit operation will only be possible when both the
 * new-style interface and the new-style bitmap are used.
 *
 * For example, the new bitmap interfaces will check the structure
 * magic numbers and so will be able to detect old-stype bitmap.  If
 * they see an old-style bitmap, they will pass it to the gen_bitmap.c
 * functions for handling.  The same will be true for the old
 * interfaces as well.
 *
 * The new-style interfaces will have several different back-end
 * implementations, so we can support different encodings that are
 * appropriate for different applications.  In general the default
 * should be whatever makes sense, and what the application/library
 * will use.  However, e2fsck may need specialized implementations for
 * its own uses.  For example, when doing parent directory pointer
 * loop detections in pass 3, the bitmap will *always* be sparse, so
 * e2fsck can request an encoding which is optimized for that.
 */

static void warn_bitmap(ext2fs_generic_bitmap_64 bitmap,
			int code, __u64 arg)
{
#ifndef OMIT_COM_ERR
	if (bitmap->description)
		com_err(0, bitmap->base_error_code+code,
			"#%llu for %s", arg, bitmap->description);
	else
		com_err(0, bitmap->base_error_code + code, "#%llu", arg);
#endif
}

#ifdef ENABLE_BMAP_STATS_OPS
#define INC_STAT(map, name) map->stats.name
#else
#define INC_STAT(map, name) ;;
#endif


errcode_t ext2fs_alloc_generic_bmap(ext2_filsys fs, errcode_t magic,
				    int type, __u64 start, __u64 end,
				    __u64 real_end,
				    const char *descr,
				    ext2fs_generic_bitmap *ret)
{
	ext2fs_generic_bitmap_64 bitmap;
	struct ext2_bitmap_ops	*ops;
	ext2_ino_t num_dirs;
	errcode_t retval;

	if (!type)
		type = EXT2FS_BMAP64_BITARRAY;

	switch (type) {
	case EXT2FS_BMAP64_BITARRAY:
		ops = &ext2fs_blkmap64_bitarray;
		break;
	case EXT2FS_BMAP64_RBTREE:
		ops = &ext2fs_blkmap64_rbtree;
		break;
	case EXT2FS_BMAP64_AUTODIR:
		retval = ext2fs_get_num_dirs(fs, &num_dirs);
		if (retval || num_dirs > (fs->super->s_inodes_count / 320))
			ops = &ext2fs_blkmap64_bitarray;
		else
			ops = &ext2fs_blkmap64_rbtree;
		break;
	default:
		return EINVAL;
	}

	retval = ext2fs_get_memzero(sizeof(struct ext2fs_struct_generic_bitmap_64),
				    &bitmap);
	if (retval)
		return retval;

#ifdef ENABLE_BMAP_STATS
	if (gettimeofday(&bitmap->stats.created,
			 (struct timezone *) NULL) == -1) {
		perror("gettimeofday");
		ext2fs_free_mem(&bitmap);
		return 1;
	}
	bitmap->stats.type = type;
#endif

	/* XXX factor out, repeated in copy_bmap */
	bitmap->magic = magic;
	bitmap->fs = fs;
	bitmap->start = start;
	bitmap->end = end;
	bitmap->real_end = real_end;
	bitmap->bitmap_ops = ops;
	bitmap->cluster_bits = 0;
	switch (magic) {
	case EXT2_ET_MAGIC_INODE_BITMAP64:
		bitmap->base_error_code = EXT2_ET_BAD_INODE_MARK;
		break;
	case EXT2_ET_MAGIC_BLOCK_BITMAP64:
		bitmap->base_error_code = EXT2_ET_BAD_BLOCK_MARK;
		bitmap->cluster_bits = fs->cluster_ratio_bits;
		break;
	default:
		bitmap->base_error_code = EXT2_ET_BAD_GENERIC_MARK;
	}
	if (descr) {
		retval = ext2fs_get_mem(strlen(descr)+1, &bitmap->description);
		if (retval) {
			ext2fs_free_mem(&bitmap);
			return retval;
		}
		strcpy(bitmap->description, descr);
	} else
		bitmap->description = 0;

	retval = bitmap->bitmap_ops->new_bmap(fs, bitmap);
	if (retval) {
		ext2fs_free_mem(&bitmap->description);
		ext2fs_free_mem(&bitmap);
		return retval;
	}

	*ret = (ext2fs_generic_bitmap) bitmap;
	return 0;
}

#ifdef ENABLE_BMAP_STATS
static void ext2fs_print_bmap_statistics(ext2fs_generic_bitmap_64 bitmap)
{
	struct ext2_bmap_statistics *stats = &bitmap->stats;
#ifdef ENABLE_BMAP_STATS_OPS
	float mark_seq_perc = 0.0, test_seq_perc = 0.0;
	float mark_back_perc = 0.0, test_back_perc = 0.0;
#endif
	double inuse;
	struct timeval now;

#ifdef ENABLE_BMAP_STATS_OPS
	if (stats->test_count) {
		test_seq_perc = ((float)stats->test_seq /
				 stats->test_count) * 100;
		test_back_perc = ((float)stats->test_back /
				  stats->test_count) * 100;
	}

	if (stats->mark_count) {
		mark_seq_perc = ((float)stats->mark_seq /
				 stats->mark_count) * 100;
		mark_back_perc = ((float)stats->mark_back /
				  stats->mark_count) * 100;
	}
#endif

	if (gettimeofday(&now, (struct timezone *) NULL) == -1) {
		perror("gettimeofday");
		return;
	}

	inuse = (double) now.tv_sec + \
		(((double) now.tv_usec) * 0.000001);
	inuse -= (double) stats->created.tv_sec + \
		(((double) stats->created.tv_usec) * 0.000001);

	fprintf(stderr, "\n[+] %s bitmap (type %d)\n", bitmap->description,
		stats->type);
	fprintf(stderr, "=================================================\n");
#ifdef ENABLE_BMAP_STATS_OPS
	fprintf(stderr, "%16llu bits long\n",
		bitmap->real_end - bitmap->start);
	fprintf(stderr, "%16lu copy_bmap\n%16lu resize_bmap\n",
		stats->copy_count, stats->resize_count);
	fprintf(stderr, "%16lu mark bmap\n%16lu unmark_bmap\n",
		stats->mark_count, stats->unmark_count);
	fprintf(stderr, "%16lu test_bmap\n%16lu mark_bmap_extent\n",
		stats->test_count, stats->mark_ext_count);
	fprintf(stderr, "%16lu unmark_bmap_extent\n"
		"%16lu test_clear_bmap_extent\n",
		stats->unmark_ext_count, stats->test_ext_count);
	fprintf(stderr, "%16lu set_bmap_range\n%16lu set_bmap_range\n",
		stats->set_range_count, stats->get_range_count);
	fprintf(stderr, "%16lu clear_bmap\n%16lu contiguous bit test (%.2f%%)\n",
		stats->clear_count, stats->test_seq, test_seq_perc);
	fprintf(stderr, "%16lu contiguous bit mark (%.2f%%)\n"
		"%16llu bits tested backwards (%.2f%%)\n",
		stats->mark_seq, mark_seq_perc,
		stats->test_back, test_back_perc);
	fprintf(stderr, "%16llu bits marked backwards (%.2f%%)\n"
		"%16.2f seconds in use\n",
		stats->mark_back, mark_back_perc, inuse);
#endif /* ENABLE_BMAP_STATS_OPS */
}
#endif

void ext2fs_free_generic_bmap(ext2fs_generic_bitmap gen_bmap)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;

	if (!bmap)
		return;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		ext2fs_free_generic_bitmap(gen_bmap);
		return;
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return;

#ifdef ENABLE_BMAP_STATS
	if (getenv("E2FSPROGS_BITMAP_STATS")) {
		ext2fs_print_bmap_statistics(bmap);
		bmap->bitmap_ops->print_stats(bmap);
	}
#endif

	bmap->bitmap_ops->free_bmap(bmap);

	if (bmap->description) {
		ext2fs_free_mem(&bmap->description);
		bmap->description = 0;
	}
	bmap->magic = 0;
	ext2fs_free_mem(&bmap);
}

errcode_t ext2fs_copy_generic_bmap(ext2fs_generic_bitmap gen_src,
				   ext2fs_generic_bitmap *dest)
{
	ext2fs_generic_bitmap_64 src = (ext2fs_generic_bitmap_64) gen_src;
	char *descr, *new_descr;
	ext2fs_generic_bitmap_64 new_bmap;
	errcode_t retval;

	if (!src)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(src))
		return ext2fs_copy_generic_bitmap(gen_src, dest);

	if (!EXT2FS_IS_64_BITMAP(src))
		return EINVAL;

	/* Allocate a new bitmap struct */
	retval = ext2fs_get_memzero(sizeof(struct ext2fs_struct_generic_bitmap_64),
				    &new_bmap);
	if (retval)
		return retval;


#ifdef ENABLE_BMAP_STATS_OPS
	src->stats.copy_count++;
#endif
#ifdef ENABLE_BMAP_STATS
	if (gettimeofday(&new_bmap->stats.created,
			 (struct timezone *) NULL) == -1) {
		perror("gettimeofday");
		ext2fs_free_mem(&new_bmap);
		return 1;
	}
	new_bmap->stats.type = src->stats.type;
#endif

	/* Copy all the high-level parts over */
	new_bmap->magic = src->magic;
	new_bmap->fs = src->fs;
	new_bmap->start = src->start;
	new_bmap->end = src->end;
	new_bmap->real_end = src->real_end;
	new_bmap->bitmap_ops = src->bitmap_ops;
	new_bmap->base_error_code = src->base_error_code;
	new_bmap->cluster_bits = src->cluster_bits;

	descr = src->description;
	if (descr) {
		retval = ext2fs_get_mem(strlen(descr)+10, &new_descr);
		if (retval) {
			ext2fs_free_mem(&new_bmap);
			return retval;
		}
		strcpy(new_descr, "copy of ");
		strcat(new_descr, descr);
		new_bmap->description = new_descr;
	}

	retval = src->bitmap_ops->copy_bmap(src, new_bmap);
	if (retval) {
		ext2fs_free_mem(&new_bmap->description);
		ext2fs_free_mem(&new_bmap);
		return retval;
	}

	*dest = (ext2fs_generic_bitmap) new_bmap;

	return 0;
}

errcode_t ext2fs_resize_generic_bmap(ext2fs_generic_bitmap gen_bmap,
				     __u64 new_end,
				     __u64 new_real_end)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;

	if (!bmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bmap))
		return ext2fs_resize_generic_bitmap(gen_bmap->magic, new_end,
						    new_real_end, gen_bmap);

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return EINVAL;

	INC_STAT(bmap, resize_count);

	return bmap->bitmap_ops->resize_bmap(bmap, new_end, new_real_end);
}

errcode_t ext2fs_fudge_generic_bmap_end(ext2fs_generic_bitmap gen_bitmap,
					errcode_t neq,
					__u64 end, __u64 *oend)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (!bitmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		ext2_ino_t tmp_oend;
		int retval;

		retval = ext2fs_fudge_generic_bitmap_end(gen_bitmap,
							 bitmap->magic,
							 neq, end, &tmp_oend);
		if (oend)
			*oend = tmp_oend;
		return retval;
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return EINVAL;

	if (end > bitmap->real_end)
		return neq;
	if (oend)
		*oend = bitmap->end;
	bitmap->end = end;
	return 0;
}

__u64 ext2fs_get_generic_bmap_start(ext2fs_generic_bitmap gen_bitmap)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (!bitmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bitmap))
		return ext2fs_get_generic_bitmap_start(gen_bitmap);

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return EINVAL;

	return bitmap->start;
}

__u64 ext2fs_get_generic_bmap_end(ext2fs_generic_bitmap gen_bitmap)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (!bitmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bitmap))
		return ext2fs_get_generic_bitmap_end(gen_bitmap);

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return EINVAL;

	return bitmap->end;
}

void ext2fs_clear_generic_bmap(ext2fs_generic_bitmap gen_bitmap)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (EXT2FS_IS_32_BITMAP(bitmap))
		ext2fs_clear_generic_bitmap(gen_bitmap);
	else
		bitmap->bitmap_ops->clear_bmap(bitmap);
}

int ext2fs_mark_generic_bmap(ext2fs_generic_bitmap gen_bitmap,
			     __u64 arg)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (!bitmap)
		return 0;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		if (arg & ~0xffffffffULL) {
			ext2fs_warn_bitmap2(gen_bitmap,
					    EXT2FS_MARK_ERROR, 0xffffffff);
			return 0;
		}
		return ext2fs_mark_generic_bitmap(gen_bitmap, arg);
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return 0;

	arg >>= bitmap->cluster_bits;

#ifdef ENABLE_BMAP_STATS_OPS
	if (arg == bitmap->stats.last_marked + 1)
		bitmap->stats.mark_seq++;
	if (arg < bitmap->stats.last_marked)
		bitmap->stats.mark_back++;
	bitmap->stats.last_marked = arg;
	bitmap->stats.mark_count++;
#endif

	if ((arg < bitmap->start) || (arg > bitmap->end)) {
		warn_bitmap(bitmap, EXT2FS_MARK_ERROR, arg);
		return 0;
	}

	return bitmap->bitmap_ops->mark_bmap(bitmap, arg);
}

int ext2fs_unmark_generic_bmap(ext2fs_generic_bitmap gen_bitmap,
			       __u64 arg)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

	if (!bitmap)
		return 0;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		if (arg & ~0xffffffffULL) {
			ext2fs_warn_bitmap2(gen_bitmap, EXT2FS_UNMARK_ERROR,
					    0xffffffff);
			return 0;
		}
		return ext2fs_unmark_generic_bitmap(gen_bitmap, arg);
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return 0;

	arg >>= bitmap->cluster_bits;

	INC_STAT(bitmap, unmark_count);

	if ((arg < bitmap->start) || (arg > bitmap->end)) {
		warn_bitmap(bitmap, EXT2FS_UNMARK_ERROR, arg);
		return 0;
	}

	return bitmap->bitmap_ops->unmark_bmap(bitmap, arg);
}

int ext2fs_test_generic_bmap(ext2fs_generic_bitmap gen_bitmap,
			     __u64 arg)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;
	if (!bitmap)
		return 0;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		if (arg & ~0xffffffffULL) {
			ext2fs_warn_bitmap2(gen_bitmap, EXT2FS_TEST_ERROR,
					    0xffffffff);
			return 0;
		}
		return ext2fs_test_generic_bitmap(gen_bitmap, arg);
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return 0;

	arg >>= bitmap->cluster_bits;

#ifdef ENABLE_BMAP_STATS_OPS
	bitmap->stats.test_count++;
	if (arg == bitmap->stats.last_tested + 1)
		bitmap->stats.test_seq++;
	if (arg < bitmap->stats.last_tested)
		bitmap->stats.test_back++;
	bitmap->stats.last_tested = arg;
#endif

	if ((arg < bitmap->start) || (arg > bitmap->end)) {
		warn_bitmap(bitmap, EXT2FS_TEST_ERROR, arg);
		return 0;
	}

	return bitmap->bitmap_ops->test_bmap(bitmap, arg);
}

errcode_t ext2fs_set_generic_bmap_range(ext2fs_generic_bitmap gen_bmap,
					__u64 start, unsigned int num,
					void *in)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;

	if (!bmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		if ((start+num-1) & ~0xffffffffULL) {
			ext2fs_warn_bitmap2(gen_bmap, EXT2FS_UNMARK_ERROR,
					    0xffffffff);
			return EINVAL;
		}
		return ext2fs_set_generic_bitmap_range(gen_bmap, bmap->magic,
						       start, num, in);
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return EINVAL;

	INC_STAT(bmap, set_range_count);

	return bmap->bitmap_ops->set_bmap_range(bmap, start, num, in);
}

errcode_t ext2fs_get_generic_bmap_range(ext2fs_generic_bitmap gen_bmap,
					__u64 start, unsigned int num,
					void *out)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;

	if (!bmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		if ((start+num-1) & ~0xffffffffULL) {
			ext2fs_warn_bitmap2(gen_bmap,
					    EXT2FS_UNMARK_ERROR, 0xffffffff);
			return EINVAL;
		}
		return ext2fs_get_generic_bitmap_range(gen_bmap, bmap->magic,
						       start, num, out);
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return EINVAL;

	INC_STAT(bmap, get_range_count);

	return bmap->bitmap_ops->get_bmap_range(bmap, start, num, out);
}

errcode_t ext2fs_compare_generic_bmap(errcode_t neq,
				      ext2fs_generic_bitmap gen_bm1,
				      ext2fs_generic_bitmap gen_bm2)
{
	ext2fs_generic_bitmap_64 bm1 = (ext2fs_generic_bitmap_64) gen_bm1;
	ext2fs_generic_bitmap_64 bm2 = (ext2fs_generic_bitmap_64) gen_bm2;
	blk64_t	i;

	if (!bm1 || !bm2)
		return EINVAL;
	if (bm1->magic != bm2->magic)
		return EINVAL;

	/* Now we know both bitmaps have the same magic */
	if (EXT2FS_IS_32_BITMAP(bm1))
		return ext2fs_compare_generic_bitmap(bm1->magic, neq,
						     gen_bm1, gen_bm2);

	if (!EXT2FS_IS_64_BITMAP(bm1))
		return EINVAL;

	if ((bm1->start != bm2->start) ||
	    (bm1->end != bm2->end))
		return neq;

	for (i = bm1->end - ((bm1->end - bm1->start) % 8); i <= bm1->end; i++)
		if (ext2fs_test_generic_bmap(gen_bm1, i) !=
		    ext2fs_test_generic_bmap(gen_bm2, i))
			return neq;

	return 0;
}

void ext2fs_set_generic_bmap_padding(ext2fs_generic_bitmap gen_bmap)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;
	__u64	start, num;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		ext2fs_set_generic_bitmap_padding(gen_bmap);
		return;
	}

	start = bmap->end + 1;
	num = bmap->real_end - bmap->end;
	bmap->bitmap_ops->mark_bmap_extent(bmap, start, num);
	/* XXX ought to warn on error */
}

int ext2fs_test_block_bitmap_range2(ext2fs_block_bitmap gen_bmap,
				    blk64_t block, unsigned int num)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;
	__u64	end = block + num;

	if (!bmap)
		return EINVAL;

	if (num == 1)
		return !ext2fs_test_generic_bmap((ext2fs_generic_bitmap)
						 bmap, block);

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		if ((block & ~0xffffffffULL) ||
		    ((block+num-1) & ~0xffffffffULL)) {
			ext2fs_warn_bitmap2((ext2fs_generic_bitmap) bmap,
					    EXT2FS_UNMARK_ERROR, 0xffffffff);
			return EINVAL;
		}
		return ext2fs_test_block_bitmap_range(
			(ext2fs_generic_bitmap) bmap, block, num);
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return EINVAL;

	INC_STAT(bmap, test_ext_count);

	/* convert to clusters if necessary */
	block >>= bmap->cluster_bits;
	end += (1 << bmap->cluster_bits) - 1;
	end >>= bmap->cluster_bits;
	num = end - block;

	if ((block < bmap->start) || (block > bmap->end) ||
	    (block+num-1 > bmap->end)) {
		ext2fs_warn_bitmap(EXT2_ET_BAD_BLOCK_TEST, block,
				   bmap->description);
		return EINVAL;
	}

	return bmap->bitmap_ops->test_clear_bmap_extent(bmap, block, num);
}

void ext2fs_mark_block_bitmap_range2(ext2fs_block_bitmap gen_bmap,
				     blk64_t block, unsigned int num)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;
	__u64	end = block + num;

	if (!bmap)
		return;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		if ((block & ~0xffffffffULL) ||
		    ((block+num-1) & ~0xffffffffULL)) {
			ext2fs_warn_bitmap2((ext2fs_generic_bitmap) bmap,
					    EXT2FS_UNMARK_ERROR, 0xffffffff);
			return;
		}
		ext2fs_mark_block_bitmap_range((ext2fs_generic_bitmap) bmap,
					       block, num);
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return;

	INC_STAT(bmap, mark_ext_count);

	/* convert to clusters if necessary */
	block >>= bmap->cluster_bits;
	end += (1 << bmap->cluster_bits) - 1;
	end >>= bmap->cluster_bits;
	num = end - block;

	if ((block < bmap->start) || (block > bmap->end) ||
	    (block+num-1 > bmap->end)) {
		ext2fs_warn_bitmap(EXT2_ET_BAD_BLOCK_MARK, block,
				   bmap->description);
		return;
	}

	bmap->bitmap_ops->mark_bmap_extent(bmap, block, num);
}

void ext2fs_unmark_block_bitmap_range2(ext2fs_block_bitmap gen_bmap,
				       blk64_t block, unsigned int num)
{
	ext2fs_generic_bitmap_64 bmap = (ext2fs_generic_bitmap_64) gen_bmap;
	__u64	end = block + num;

	if (!bmap)
		return;

	if (EXT2FS_IS_32_BITMAP(bmap)) {
		if ((block & ~0xffffffffULL) ||
		    ((block+num-1) & ~0xffffffffULL)) {
			ext2fs_warn_bitmap2((ext2fs_generic_bitmap) bmap,
					    EXT2FS_UNMARK_ERROR, 0xffffffff);
			return;
		}
		ext2fs_unmark_block_bitmap_range((ext2fs_generic_bitmap) bmap,
						 block, num);
	}

	if (!EXT2FS_IS_64_BITMAP(bmap))
		return;

	INC_STAT(bmap, unmark_ext_count);

	/* convert to clusters if necessary */
	block >>= bmap->cluster_bits;
	end += (1 << bmap->cluster_bits) - 1;
	end >>= bmap->cluster_bits;
	num = end - block;

	if ((block < bmap->start) || (block > bmap->end) ||
	    (block+num-1 > bmap->end)) {
		ext2fs_warn_bitmap(EXT2_ET_BAD_BLOCK_UNMARK, block,
				   bmap->description);
		return;
	}

	bmap->bitmap_ops->unmark_bmap_extent(bmap, block, num);
}

void ext2fs_warn_bitmap32(ext2fs_generic_bitmap gen_bitmap, const char *func)
{
	ext2fs_generic_bitmap_64 bitmap = (ext2fs_generic_bitmap_64) gen_bitmap;

#ifndef OMIT_COM_ERR
	if (bitmap && bitmap->description)
		com_err(0, EXT2_ET_MAGIC_GENERIC_BITMAP,
			"called %s with 64-bit bitmap for %s", func,
			bitmap->description);
	else
		com_err(0, EXT2_ET_MAGIC_GENERIC_BITMAP,
			"called %s with 64-bit bitmap", func);
#endif
}

errcode_t ext2fs_convert_subcluster_bitmap(ext2_filsys fs,
					   ext2fs_block_bitmap *bitmap)
{
	ext2fs_generic_bitmap_64 bmap, cmap;
	ext2fs_block_bitmap	gen_bmap = *bitmap, gen_cmap;
	errcode_t		retval;
	blk64_t			i, b_end, c_end;
	int			n, ratio;

	bmap = (ext2fs_generic_bitmap_64) gen_bmap;
	if (fs->cluster_ratio_bits == ext2fs_get_bitmap_granularity(gen_bmap))
		return 0;	/* Nothing to do */

	retval = ext2fs_allocate_block_bitmap(fs, "converted cluster bitmap",
					      &gen_cmap);
	if (retval)
		return retval;

	cmap = (ext2fs_generic_bitmap_64) gen_cmap;
	i = bmap->start;
	b_end = bmap->end;
	bmap->end = bmap->real_end;
	c_end = cmap->end;
	cmap->end = cmap->real_end;
	n = 0;
	ratio = 1 << fs->cluster_ratio_bits;
	while (i < bmap->real_end) {
		if (ext2fs_test_block_bitmap2(gen_bmap, i)) {
			ext2fs_mark_block_bitmap2(gen_cmap, i);
			i += ratio - n;
			n = 0;
			continue;
		}
		i++; n++;
		if (n >= ratio)
			n = 0;
	}
	bmap->end = b_end;
	cmap->end = c_end;
	ext2fs_free_block_bitmap(gen_bmap);
	*bitmap = (ext2fs_block_bitmap) cmap;
	return 0;
}

errcode_t ext2fs_find_first_zero_generic_bmap(ext2fs_generic_bitmap bitmap,
					      __u64 start, __u64 end, __u64 *out)
{
	ext2fs_generic_bitmap_64 bmap64 = (ext2fs_generic_bitmap_64) bitmap;
	__u64 cstart, cend, cout;
	errcode_t retval;

	if (!bitmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		blk_t blk = 0;

		if (((start) & ~0xffffffffULL) ||
		    ((end) & ~0xffffffffULL)) {
			ext2fs_warn_bitmap2(bitmap, EXT2FS_TEST_ERROR, start);
			return EINVAL;
		}

		retval = ext2fs_find_first_zero_generic_bitmap(bitmap, start,
							       end, &blk);
		if (retval == 0)
			*out = blk;
		return retval;
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return EINVAL;

	cstart = start >> bmap64->cluster_bits;
	cend = end >> bmap64->cluster_bits;

	if (cstart < bmap64->start || cend > bmap64->end || start > end) {
		warn_bitmap(bmap64, EXT2FS_TEST_ERROR, start);
		return EINVAL;
	}

	if (bmap64->bitmap_ops->find_first_zero) {
		retval = bmap64->bitmap_ops->find_first_zero(bmap64, cstart,
							     cend, &cout);
		if (retval)
			return retval;
	found:
		cout <<= bmap64->cluster_bits;
		*out = (cout >= start) ? cout : start;
		return 0;
	}

	for (cout = cstart; cout <= cend; cout++)
		if (!bmap64->bitmap_ops->test_bmap(bmap64, cout))
			goto found;

	return ENOENT;
}

errcode_t ext2fs_find_first_set_generic_bmap(ext2fs_generic_bitmap bitmap,
					     __u64 start, __u64 end, __u64 *out)
{
	ext2fs_generic_bitmap_64 bmap64 = (ext2fs_generic_bitmap_64) bitmap;
	__u64 cstart, cend, cout;
	errcode_t retval;

	if (!bitmap)
		return EINVAL;

	if (EXT2FS_IS_32_BITMAP(bitmap)) {
		blk_t blk = 0;

		if (((start) & ~0xffffffffULL) ||
		    ((end) & ~0xffffffffULL)) {
			ext2fs_warn_bitmap2(bitmap, EXT2FS_TEST_ERROR, start);
			return EINVAL;
		}

		retval = ext2fs_find_first_set_generic_bitmap(bitmap, start,
							      end, &blk);
		if (retval == 0)
			*out = blk;
		return retval;
	}

	if (!EXT2FS_IS_64_BITMAP(bitmap))
		return EINVAL;

	cstart = start >> bmap64->cluster_bits;
	cend = end >> bmap64->cluster_bits;

	if (cstart < bmap64->start || cend > bmap64->end || start > end) {
		warn_bitmap(bmap64, EXT2FS_TEST_ERROR, start);
		return EINVAL;
	}

	if (bmap64->bitmap_ops->find_first_set) {
		retval = bmap64->bitmap_ops->find_first_set(bmap64, cstart,
							    cend, &cout);
		if (retval)
			return retval;
	found:
		cout <<= bmap64->cluster_bits;
		*out = (cout >= start) ? cout : start;
		return 0;
	}

	for (cout = cstart; cout <= cend; cout++)
		if (bmap64->bitmap_ops->test_bmap(bmap64, cout))
			goto found;

	return ENOENT;
}
