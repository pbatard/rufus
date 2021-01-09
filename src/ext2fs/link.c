/*
 * link.c --- create links in a ext2fs directory
 *
 * Copyright (C) 1993, 1994 Theodore Ts'o.
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

#include "ext2_fs.h"
#include "ext2fs.h"
#include "ext2fsP.h"

#define EXT2_DX_ROOT_OFF 24

struct dx_frame {
	__u8 *buf;
	blk64_t pblock;
	struct ext2_dx_countlimit *head;
	struct ext2_dx_entry *entries;
	struct ext2_dx_entry *at;
};

struct dx_lookup_info {
	const char *name;
	int namelen;
	int hash_alg;
	__u32 hash;
	int levels;
	struct dx_frame frames[EXT4_HTREE_LEVEL];
};

static errcode_t alloc_dx_frame(ext2_filsys fs, struct dx_frame *frame)
{
	return ext2fs_get_mem(fs->blocksize, &frame->buf);
}

static void dx_release(struct dx_lookup_info *info)
{
//	struct ext2_dx_root_info *root;
	int level;

	for (level = 0; level < info->levels; level++) {
		if (info->frames[level].buf == NULL)
			break;
		ext2fs_free_mem(&(info->frames[level].buf));
	}
	info->levels = 0;
}

static void dx_search_entry(struct dx_frame *frame, int count, __u32 hash)
{
	struct ext2_dx_entry *p, *q, *m;

	p = frame->entries + 1;
	q = frame->entries + count - 1;
	while (p <= q) {
		m = p + (q - p) / 2;
		if (ext2fs_le32_to_cpu(m->hash) > hash)
			q = m - 1;
		else
			p = m + 1;
	}
	frame->at = p - 1;
}

static errcode_t load_logical_dir_block(ext2_filsys fs, ext2_ino_t dir,
					struct ext2_inode *diri, blk64_t block,
					blk64_t *pblk, void *buf)
{
	errcode_t errcode;
	int ret_flags;

	errcode = ext2fs_bmap2(fs, dir, diri, NULL, 0, block, &ret_flags,
			       pblk);
	if (errcode)
		return errcode;
	if (ret_flags & BMAP_RET_UNINIT)
		return EXT2_ET_DIR_CORRUPTED;
	return ext2fs_read_dir_block4(fs, *pblk, buf, 0, dir);
}

static errcode_t dx_lookup(ext2_filsys fs, ext2_ino_t dir,
			   struct ext2_inode *diri, struct dx_lookup_info *info)
{
	struct ext2_dx_root_info *root;
	errcode_t errcode;
	int level = 0;
	int count, limit;
	int hash_alg;
	int hash_flags = diri->i_flags & EXT4_CASEFOLD_FL;
	__u32 minor_hash;
	struct dx_frame *frame;

	errcode = alloc_dx_frame(fs, &(info->frames[0]));
	if (errcode)
		return errcode;
	info->levels = 1;

	errcode = load_logical_dir_block(fs, dir, diri, 0,
					 &(info->frames[0].pblock),
					 info->frames[0].buf);
	if (errcode)
		goto out_err;
	root = (struct ext2_dx_root_info*)(info->frames[0].buf + EXT2_DX_ROOT_OFF);
	hash_alg = root->hash_version;
	if (hash_alg != EXT2_HASH_TEA && hash_alg != EXT2_HASH_HALF_MD4 &&
	    hash_alg != EXT2_HASH_LEGACY) {
		errcode = EXT2_ET_DIRHASH_UNSUPP;
		goto out_err;
	}
	if (hash_alg <= EXT2_HASH_TEA &&
	    fs->super->s_flags & EXT2_FLAGS_UNSIGNED_HASH)
		hash_alg += 3;
	if (root->indirect_levels >= ext2_dir_htree_level(fs)) {
		errcode = EXT2_ET_DIR_CORRUPTED;
		goto out_err;
	}
	info->hash_alg = hash_alg;

	errcode = ext2fs_dirhash2(hash_alg, info->name, info->namelen,
				  fs->encoding, hash_flags,
				  fs->super->s_hash_seed, &info->hash,
				  &minor_hash);
	if (errcode)
		goto out_err;

	for (level = 0; level <= root->indirect_levels; level++) {
		frame = &(info->frames[level]);
		if (level > 0) {
			errcode = alloc_dx_frame(fs, frame);
			if (errcode)
				goto out_err;
			info->levels++;

			errcode = load_logical_dir_block(fs, dir, diri,
				ext2fs_le32_to_cpu(info->frames[level-1].at->block) & 0x0fffffff,
				&(frame->pblock), frame->buf);
			if (errcode)
				goto out_err;
		}
		errcode = ext2fs_get_dx_countlimit(fs, (struct ext2_dir_entry*)frame->buf,
						   &(frame->head), NULL);
		if (errcode)
			goto out_err;
		count = ext2fs_le16_to_cpu(frame->head->count);
		limit = ext2fs_le16_to_cpu(frame->head->limit);
		frame->entries = (struct ext2_dx_entry *)(frame->head);
		if (!count || count > limit) {
			errcode = EXT2_ET_DIR_CORRUPTED;
			goto out_err;
		}

		dx_search_entry(frame, count, info->hash);
	}
	return 0;
out_err:
	dx_release(info);
	return errcode;
}

struct link_struct  {
	ext2_filsys	fs;
	const char	*name;
	int		namelen;
	ext2_ino_t	inode;
	int		flags;
	int		done;
	unsigned int	blocksize;
	errcode_t	err;
	struct ext2_super_block *sb;
};

static int link_proc(ext2_ino_t dir EXT2FS_ATTR((unused)),
		     int entru EXT2FS_ATTR((unused)),
		     struct ext2_dir_entry *dirent,
		     int	offset,
		     int	blocksize,
		     char	*buf,
		     void	*priv_data)
{
	struct link_struct *ls = (struct link_struct *) priv_data;
	struct ext2_dir_entry *next;
	unsigned int rec_len, min_rec_len, curr_rec_len;
	int ret = 0;
	int csum_size = 0;

	if (ls->done)
		return DIRENT_ABORT;

	rec_len = EXT2_DIR_REC_LEN(ls->namelen);

	ls->err = ext2fs_get_rec_len(ls->fs, dirent, &curr_rec_len);
	if (ls->err)
		return DIRENT_ABORT;

	if (ext2fs_has_feature_metadata_csum(ls->fs->super))
		csum_size = sizeof(struct ext2_dir_entry_tail);
	/*
	 * See if the following directory entry (if any) is unused;
	 * if so, absorb it into this one.
	 */
	next = (struct ext2_dir_entry *) (buf + offset + curr_rec_len);
	if ((offset + (int) curr_rec_len < blocksize - (8 + csum_size)) &&
	    (next->inode == 0) &&
	    (offset + (int) curr_rec_len + (int) next->rec_len <= blocksize)) {
		curr_rec_len += next->rec_len;
		ls->err = ext2fs_set_rec_len(ls->fs, curr_rec_len, dirent);
		if (ls->err)
			return DIRENT_ABORT;
		ret = DIRENT_CHANGED;
	}

	/*
	 * If the directory entry is used, see if we can split the
	 * directory entry to make room for the new name.  If so,
	 * truncate it and return.
	 */
	if (dirent->inode) {
		min_rec_len = EXT2_DIR_REC_LEN(ext2fs_dirent_name_len(dirent));
		if (curr_rec_len < (min_rec_len + rec_len))
			return ret;
		rec_len = curr_rec_len - min_rec_len;
		ls->err = ext2fs_set_rec_len(ls->fs, min_rec_len, dirent);
		if (ls->err)
			return DIRENT_ABORT;
		next = (struct ext2_dir_entry *) (buf + offset +
						  dirent->rec_len);
		next->inode = 0;
		ext2fs_dirent_set_name_len(next, 0);
		ext2fs_dirent_set_file_type(next, 0);
		ls->err = ext2fs_set_rec_len(ls->fs, rec_len, next);
		if (ls->err)
			return DIRENT_ABORT;
		return DIRENT_CHANGED;
	}

	/*
	 * If we get this far, then the directory entry is not used.
	 * See if we can fit the request entry in.  If so, do it.
	 */
	if (curr_rec_len < rec_len)
		return ret;
	dirent->inode = ls->inode;
	ext2fs_dirent_set_name_len(dirent, ls->namelen);
	strncpy(dirent->name, ls->name, ls->namelen);
	if (ext2fs_has_feature_filetype(ls->sb))
		ext2fs_dirent_set_file_type(dirent, ls->flags & 0x7);

	ls->done++;
	return DIRENT_ABORT|DIRENT_CHANGED;
}

static errcode_t add_dirent_to_buf(ext2_filsys fs, e2_blkcnt_t blockcnt,
				   char *buf, ext2_ino_t dir,
				   struct ext2_inode *diri, const char *name,
				   ext2_ino_t ino, int flags, blk64_t *pblkp)
{
	struct dir_context ctx;
	struct link_struct ls;
	errcode_t retval;

	retval = load_logical_dir_block(fs, dir, diri, blockcnt, pblkp, buf);
	if (retval)
		return retval;
	ctx.errcode = 0;
	ctx.func = link_proc;
	ctx.dir = dir;
	ctx.flags = DIRENT_FLAG_INCLUDE_EMPTY;
	ctx.buf = buf;
	ctx.priv_data = &ls;

	ls.fs = fs;
	ls.name = name;
	ls.namelen = strlen(name);
	ls.inode = ino;
	ls.flags = flags;
	ls.done = 0;
	ls.sb = fs->super;
	ls.blocksize = fs->blocksize;
	ls.err = 0;

	ext2fs_process_dir_block(fs, pblkp, blockcnt, 0, 0, &ctx);
	if (ctx.errcode)
		return ctx.errcode;
	if (ls.err)
		return ls.err;
	if (!ls.done)
		return EXT2_ET_DIR_NO_SPACE;
	return 0;
}

struct dx_hash_map {
	__u32 hash;
	int size;
	int off;
};

static EXT2_QSORT_TYPE dx_hash_map_cmp(const void *ap, const void *bp)
{
	const struct dx_hash_map *a = ap, *b = bp;

	if (a->hash < b->hash)
		return -1;
	if (a->hash > b->hash)
		return 1;
	return 0;
}

static errcode_t dx_move_dirents(ext2_filsys fs, struct dx_hash_map *map,
				 int count, __u8 *from, __u8 *to)
{
	struct ext2_dir_entry *de;
	int i;
	int rec_len = 0;
	errcode_t retval;
	int csum_size = 0;
	__u8 *base = to;

	if (ext2fs_has_feature_metadata_csum(fs->super))
		csum_size = sizeof(struct ext2_dir_entry_tail);

	for (i = 0; i < count; i++) {
		de = (struct ext2_dir_entry*)(from + map[i].off);
		rec_len = EXT2_DIR_REC_LEN(ext2fs_dirent_name_len(de));
		memcpy(to, de, rec_len);
		retval = ext2fs_set_rec_len(fs, rec_len, (struct ext2_dir_entry*)to);
		if (retval)
			return retval;
		to += rec_len;
	}
	/*
	 * Update rec_len of the last dir entry to stretch to the end of block
	 */
	to -= rec_len;
	rec_len = fs->blocksize - (to - base) - csum_size;
	retval = ext2fs_set_rec_len(fs, rec_len, (struct ext2_dir_entry*)to);
	if (retval)
		return retval;
	if (csum_size)
		ext2fs_initialize_dirent_tail(fs,
				EXT2_DIRENT_TAIL(base, fs->blocksize));
	return 0;
}

static errcode_t dx_insert_entry(ext2_filsys fs, ext2_ino_t dir,
				 struct dx_lookup_info *info, int level,
				 __u32 hash, blk64_t lblk)
{
	int pcount;
	struct ext2_dx_entry *top, *new;

	pcount = ext2fs_le16_to_cpu(info->frames[level].head->count);
	top = info->frames[level].entries + pcount;
	new = info->frames[level].at + 1;
	memmove(new + 1, new, (char *)top - (char *)new);
	new->hash = ext2fs_cpu_to_le32(hash);
	new->block = ext2fs_cpu_to_le32(lblk);
	info->frames[level].head->count = ext2fs_cpu_to_le16(pcount + 1);
	return ext2fs_write_dir_block4(fs, info->frames[level].pblock,
				       info->frames[level].buf, 0, dir);
}

static errcode_t dx_split_leaf(ext2_filsys fs, ext2_ino_t dir,
			       struct ext2_inode *diri,
			       struct dx_lookup_info *info, __u8 *buf,
			       blk64_t leaf_pblk, blk64_t new_lblk,
			       blk64_t new_pblk)
{
	int hash_flags = diri->i_flags & EXT4_CASEFOLD_FL;
	struct ext2_dir_entry *de;
	void *buf2;
	errcode_t retval = 0;
	int rec_len;
	int offset, move_size;
	int i, count = 0;
	struct dx_hash_map *map;
	int continued;
	__u32 minor_hash;

	retval = ext2fs_get_mem(fs->blocksize, &buf2);
	if (retval)
		return retval;
	retval = ext2fs_get_array(fs->blocksize / 12,
				  sizeof(struct dx_hash_map), &map);
	if (retval) {
		ext2fs_free_mem(&buf2);
		return retval;
	}
	for (offset = 0; offset < fs->blocksize; offset += rec_len) {
		de = (struct ext2_dir_entry*)(buf + offset);
		retval = ext2fs_get_rec_len(fs, de, &rec_len);
		if (retval)
			goto out;
		if (ext2fs_dirent_name_len(de) > 0 && de->inode) {
			map[count].off = offset;
			map[count].size = rec_len;
			retval = ext2fs_dirhash2(info->hash_alg, de->name,
					ext2fs_dirent_name_len(de),
					fs->encoding, hash_flags,
					fs->super->s_hash_seed,
					&(map[count].hash),
					&minor_hash);
			if (retval)
				goto out;
			count++;
		}
	}
	qsort(map, count, sizeof(struct dx_hash_map), dx_hash_map_cmp);
	move_size = 0;
	/* Find place to split block */
	for (i = count - 1; i >= 0; i--) {
		if (move_size + map[i].size / 2 > fs->blocksize / 2)
			break;
		move_size += map[i].size;
	}
	/* Let i be the first entry to move */
	i++;
	/* Move selected directory entries to new block */
	retval = dx_move_dirents(fs, map + i, count - i, buf, buf2);
	if (retval)
		goto out;
	retval = ext2fs_write_dir_block4(fs, new_pblk, buf2, 0, dir);
	if (retval)
		goto out;
	/* Repack remaining entries in the old block */
	retval = dx_move_dirents(fs, map, i, buf, buf2);
	if (retval)
		goto out;
	retval = ext2fs_write_dir_block4(fs, leaf_pblk, buf2, 0, dir);
	if (retval)
		goto out;
	/* Update parent node */
	continued = map[i].hash == map[i-1].hash;
	retval = dx_insert_entry(fs, dir, info, info->levels - 1,
				 map[i].hash + continued, new_lblk);
out:
	ext2fs_free_mem(&buf2);
	ext2fs_free_mem(&map);
	return retval;
}

static errcode_t dx_grow_tree(ext2_filsys fs, ext2_ino_t dir,
			      struct ext2_inode *diri,
			      struct dx_lookup_info *info, __u8 *buf,
			      blk64_t leaf_pblk)
{
	int i;
	errcode_t retval;
	ext2_off64_t size = EXT2_I_SIZE(diri);
	blk64_t lblk, pblk;
	struct ext2_dir_entry *de;
	struct ext2_dx_countlimit *head;
	int csum_size = 0;
	int count;

	if (ext2fs_has_feature_metadata_csum(fs->super))
		csum_size = sizeof(struct ext2_dx_tail);

	/* Find level which can accommodate new child */
	for (i = info->levels - 1; i >= 0; i--)
		if (ext2fs_le16_to_cpu(info->frames[i].head->count) <
		    ext2fs_le16_to_cpu(info->frames[i].head->limit))
			break;
	/* Need to grow tree depth? */
	if (i < 0 && info->levels >= ext2_dir_htree_level(fs))
		return EXT2_ET_DIR_NO_SPACE;
	lblk = size / fs->blocksize;
	size += fs->blocksize;
	retval = ext2fs_inode_size_set(fs, diri, size);
	if (retval)
		return retval;
	retval = ext2fs_fallocate(fs,
			EXT2_FALLOCATE_FORCE_INIT | EXT2_FALLOCATE_ZERO_BLOCKS,
			dir, diri, 0, lblk, 1);
	if (retval)
		return retval;
	retval = ext2fs_write_inode(fs, dir, diri);
	if (retval)
		return retval;
	retval = ext2fs_bmap2(fs, dir, diri, NULL, 0, lblk, NULL, &pblk);
	if (retval)
		return retval;
	/* Only leaf addition needed? */
	if (i == info->levels - 1)
		return dx_split_leaf(fs, dir, diri, info, buf, leaf_pblk,
				     lblk, pblk);

	de = (struct ext2_dir_entry*)buf;
	de->inode = 0;
	ext2fs_dirent_set_name_len(de, 0);
	ext2fs_dirent_set_file_type(de, 0);
	retval = ext2fs_set_rec_len(fs, fs->blocksize, de);
	if (retval)
		return retval;
	head = (struct ext2_dx_countlimit*)(buf + 8);
	count = ext2fs_le16_to_cpu(info->frames[i+1].head->count);
	/* Growing tree depth? */
	if (i < 0) {
		struct ext2_dx_root_info *root;

		memcpy(head, info->frames[0].entries,
		       count * sizeof(struct ext2_dx_entry));
		head->limit = ext2fs_cpu_to_le16(
				(fs->blocksize - (8 + csum_size)) /
				sizeof(struct ext2_dx_entry));
		/* head->count gets set by memcpy above to correct value */

		/* Now update tree root */
		info->frames[0].head->count = ext2fs_cpu_to_le16(1);
		info->frames[0].entries[0].block = ext2fs_cpu_to_le32(lblk);
		root = (struct ext2_dx_root_info*)(info->frames[0].buf + EXT2_DX_ROOT_OFF);
		root->indirect_levels++;
	} else {
		/* Splitting internal node in two */
		int count1 = count / 2;
		int count2 = count - count1;
		__u32 split_hash = ext2fs_le32_to_cpu(info->frames[i+1].entries[count1].hash);

		memcpy(head, info->frames[i+1].entries + count1,
		       count2 * sizeof(struct ext2_dx_entry));
		head->count = ext2fs_cpu_to_le16(count2);
		head->limit = ext2fs_cpu_to_le16(
				(fs->blocksize - (8ULL + csum_size)) /
				sizeof(struct ext2_dx_entry));
		info->frames[i+1].head->count = ext2fs_cpu_to_le16(count1);

		/* Update parent node */
		retval = dx_insert_entry(fs, dir, info, i, split_hash, lblk);
		if (retval)
			return retval;

	}
	/* Writeout split block / updated root */
	retval = ext2fs_write_dir_block4(fs, info->frames[i+1].pblock,
					 info->frames[i+1].buf, 0, dir);
	if (retval)
		return retval;
	/* Writeout new tree block */
	retval = ext2fs_write_dir_block4(fs, pblk, buf, 0, dir);
	if (retval)
		return retval;
	return 0;
}

static errcode_t dx_link(ext2_filsys fs, ext2_ino_t dir,
			 struct ext2_inode *diri, const char *name,
			 ext2_ino_t ino, int flags)
{
	struct dx_lookup_info dx_info;
	errcode_t retval;
	void *blockbuf;
	int restart = 0;
	blk64_t leaf_pblk;

	retval = ext2fs_get_mem(fs->blocksize, &blockbuf);
	if (retval)
		return retval;

	dx_info.name = name;
	dx_info.namelen = strlen(name);
again:
	retval = dx_lookup(fs, dir, diri, &dx_info);
	if (retval)
		goto free_buf;

	retval = add_dirent_to_buf(fs,
		ext2fs_le32_to_cpu(dx_info.frames[dx_info.levels-1].at->block) & 0x0fffffff,
		blockbuf, dir, diri, name, ino, flags, &leaf_pblk);
	/*
	 * Success or error other than ENOSPC...? We are done. We may need upto
	 * two tries to add entry. One to split htree node and another to add
	 * new leaf block.
	 */
	if (restart >= dx_info.levels || retval != EXT2_ET_DIR_NO_SPACE)
		goto free_frames;
	retval = dx_grow_tree(fs, dir, diri, &dx_info, blockbuf, leaf_pblk);
	if (retval)
		goto free_frames;
	/* Restart everything now that the tree is larger */
	restart++;
	dx_release(&dx_info);
	goto again;
free_frames:
	dx_release(&dx_info);
free_buf:
	ext2fs_free_mem(&blockbuf);
	return retval;
}

/*
 * Note: the low 3 bits of the flags field are used as the directory
 * entry filetype.
 */
#ifdef __TURBOC__
 #pragma argsused
#endif
errcode_t ext2fs_link(ext2_filsys fs, ext2_ino_t dir, const char *name,
		      ext2_ino_t ino, int flags)
{
	errcode_t		retval;
	struct link_struct	ls;
	struct ext2_inode	inode;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	if (!(fs->flags & EXT2_FLAG_RW))
		return EXT2_ET_RO_FILSYS;

	if ((retval = ext2fs_read_inode(fs, dir, &inode)) != 0)
		return retval;

	if (inode.i_flags & EXT2_INDEX_FL)
		return dx_link(fs, dir, &inode, name, ino, flags);

	ls.fs = fs;
	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = flags;
	ls.done = 0;
	ls.sb = fs->super;
	ls.blocksize = fs->blocksize;
	ls.err = 0;

	retval = ext2fs_dir_iterate2(fs, dir, DIRENT_FLAG_INCLUDE_EMPTY,
				     NULL, link_proc, &ls);
	if (retval)
		return retval;
	if (ls.err)
		return ls.err;

	if (!ls.done)
		return EXT2_ET_DIR_NO_SPACE;
	return 0;
}
