/*
 * inline_data.c --- data in inode
 *
 * Copyright (C) 2012 Zheng Liu <wenqing.lz@taobao.com>
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU library
 * General Public License, version 2.
 * %End-Header%
 */

#include "config.h"
#include <stdio.h>
#include <time.h>
#include <limits.h> /* for PATH_MAX */

#include "ext2_fs.h"
#include "ext2_ext_attr.h"

#include "ext2fs.h"
#include "ext2fsP.h"

struct ext2_inline_data {
	ext2_filsys fs;
	ext2_ino_t ino;
	size_t ea_size;	/* the size of inline data in ea area */
	void *ea_data;
};

static errcode_t ext2fs_inline_data_ea_set(struct ext2_inline_data *data)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	retval = ext2fs_xattrs_open(data->fs, data->ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_set(handle, "system.data",
				  data->ea_data, data->ea_size);
err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

static errcode_t ext2fs_inline_data_ea_get(struct ext2_inline_data *data)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	data->ea_size = 0;
	data->ea_data = 0;

	retval = ext2fs_xattrs_open(data->fs, data->ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_get(handle, "system.data",
				  (void **)&data->ea_data, &data->ea_size);
	if (retval == EXT2_ET_EA_KEY_NOT_FOUND) {
		data->ea_size = 0;
		data->ea_data = NULL;
		retval = 0;
	} else if (retval)
		goto err;

err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

errcode_t ext2fs_inline_data_init(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inline_data data;
	char empty[1] = { '\0' };

	data.fs = fs;
	data.ino = ino;
	data.ea_size = 0;
	data.ea_data = empty;
	return ext2fs_inline_data_ea_set(&data);
}

errcode_t ext2fs_inline_data_size(ext2_filsys fs, ext2_ino_t ino, size_t *size)
{
	struct ext2_inode inode;
	struct ext2_inline_data data;
	errcode_t retval;

	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL))
		return EXT2_ET_NO_INLINE_DATA;

	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;

	*size = EXT4_MIN_INLINE_DATA_SIZE + data.ea_size;
	return ext2fs_free_mem(&data.ea_data);
}

int ext2fs_inline_data_dir_iterate(ext2_filsys fs, ext2_ino_t ino,
				   void *priv_data)
{
	struct dir_context *ctx;
	struct ext2_inode inode;
	struct ext2_dir_entry dirent;
	struct ext2_inline_data data;
	int ret = BLOCK_ABORT;
	e2_blkcnt_t blockcnt = 0;
	char *old_buf;
	unsigned int old_buflen;
	int old_flags;

	ctx = (struct dir_context *)priv_data;
	old_buf = ctx->buf;
	old_buflen = ctx->buflen;
	old_flags = ctx->flags;
	ctx->flags |= DIRENT_FLAG_INCLUDE_INLINE_DATA;

	ctx->errcode = ext2fs_read_inode(fs, ino, &inode);
	if (ctx->errcode)
		goto out;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL)) {
		ctx->errcode = EXT2_ET_NO_INLINE_DATA;
		goto out;
	}

	if (!LINUX_S_ISDIR(inode.i_mode)) {
		ctx->errcode = EXT2_ET_NO_DIRECTORY;
		goto out;
	}
	ret = 0;

	/* we first check '.' and '..' dir */
	dirent.inode = ino;
	dirent.name_len = 1;
	ext2fs_set_rec_len(fs, EXT2_DIR_REC_LEN(2), &dirent);
	dirent.name[0] = '.';
	dirent.name[1] = '\0';
	ctx->buf = (char *)&dirent;
	ext2fs_get_rec_len(fs, &dirent, &ctx->buflen);
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_ABORT)
		goto out;

	dirent.inode = ext2fs_le32_to_cpu(inode.i_block[0]);
	dirent.name_len = 2;
	ext2fs_set_rec_len(fs, EXT2_DIR_REC_LEN(3), &dirent);
	dirent.name[0] = '.';
	dirent.name[1] = '.';
	dirent.name[2] = '\0';
	ctx->buf = (char *)&dirent;
	ext2fs_get_rec_len(fs, &dirent, &ctx->buflen);
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
		errcode_t err;

		inode.i_block[0] = ext2fs_cpu_to_le32(dirent.inode);
		err = ext2fs_write_inode(fs, ino, &inode);
		if (err)
			goto out;
		ret &= ~BLOCK_INLINE_DATA_CHANGED;
	}
	if (ret & BLOCK_ABORT)
		goto out;

	ctx->buf = (char *)inode.i_block + EXT4_INLINE_DATA_DOTDOT_SIZE;
	ctx->buflen = EXT4_MIN_INLINE_DATA_SIZE - EXT4_INLINE_DATA_DOTDOT_SIZE;
#ifdef WORDS_BIGENDIAN
	ctx->errcode = ext2fs_dirent_swab_in2(fs, ctx->buf, ctx->buflen, 0);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out;
	}
#endif
	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
#ifdef WORDS_BIGENDIAN
		ctx->errcode = ext2fs_dirent_swab_out2(fs, ctx->buf,
						       ctx->buflen, 0);
		if (ctx->errcode) {
			ret |= BLOCK_ABORT;
			goto out;
		}
#endif
		ctx->errcode = ext2fs_write_inode(fs, ino, &inode);
		if (ctx->errcode)
			ret |= BLOCK_ABORT;
		ret &= ~BLOCK_INLINE_DATA_CHANGED;
	}
	if (ret & BLOCK_ABORT)
		goto out;

	data.fs = fs;
	data.ino = ino;
	ctx->errcode = ext2fs_inline_data_ea_get(&data);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out;
	}
	if (data.ea_size <= 0)
		goto out1;

	ctx->buf = data.ea_data;
	ctx->buflen = data.ea_size;
#ifdef WORDS_BIGENDIAN
	ctx->errcode = ext2fs_dirent_swab_in2(fs, ctx->buf, ctx->buflen, 0);
	if (ctx->errcode) {
		ret |= BLOCK_ABORT;
		goto out1;
	}
#endif

	ret |= ext2fs_process_dir_block(fs, 0, blockcnt++, 0, 0, priv_data);
	if (ret & BLOCK_INLINE_DATA_CHANGED) {
#ifdef WORDS_BIGENDIAN
		ctx->errcode = ext2fs_dirent_swab_out2(fs, ctx->buf,
						      ctx->buflen, 0);
		if (ctx->errcode) {
			ret |= BLOCK_ABORT;
			goto out1;
		}
#endif
		ctx->errcode = ext2fs_inline_data_ea_set(&data);
		if (ctx->errcode)
			ret |= BLOCK_ABORT;
	}

out1:
	ext2fs_free_mem(&data.ea_data);
out:
	ctx->buf = old_buf;
	ctx->buflen = old_buflen;
	ctx->flags = old_flags;
	ret &= ~(BLOCK_ABORT | BLOCK_INLINE_DATA_CHANGED);
	return ret;
}

errcode_t ext2fs_inline_data_ea_remove(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_xattr_handle *handle;
	errcode_t retval;

	retval = ext2fs_xattrs_open(fs, ino, &handle);
	if (retval)
		return retval;

	retval = ext2fs_xattrs_read(handle);
	if (retval)
		goto err;

	retval = ext2fs_xattr_remove(handle, "system.data");
err:
	(void) ext2fs_xattrs_close(&handle);
	return retval;
}

static errcode_t ext2fs_inline_data_convert_dir(ext2_filsys fs, ext2_ino_t ino,
						char *bbuf, char *ibuf, int size)
{
	struct ext2_dir_entry *dir, *dir2;
	struct ext2_dir_entry_tail *t;
	errcode_t retval;
	int offset;
	unsigned int rec_len;
	int csum_size = 0;
	int filetype = 0;

	if (ext2fs_has_feature_metadata_csum(fs->super))
		csum_size = sizeof(struct ext2_dir_entry_tail);

	/* Create '.' and '..' */
	if (ext2fs_has_feature_filetype(fs->super))
		filetype = EXT2_FT_DIR;

	/*
	 * Set up entry for '.'
	 */
	dir = (struct ext2_dir_entry *) bbuf;
	dir->inode = ino;
	ext2fs_dirent_set_name_len(dir, 1);
	ext2fs_dirent_set_file_type(dir, filetype);
	dir->name[0] = '.';
	rec_len = (fs->blocksize - csum_size) - EXT2_DIR_REC_LEN(1);
	dir->rec_len = EXT2_DIR_REC_LEN(1);

	/*
	 * Set up entry for '..'
	 */
	dir = (struct ext2_dir_entry *) (bbuf + dir->rec_len);
	dir->rec_len = EXT2_DIR_REC_LEN(2);
	dir->inode = ext2fs_le32_to_cpu(((__u32 *)ibuf)[0]);
	ext2fs_dirent_set_name_len(dir, 2);
	ext2fs_dirent_set_file_type(dir, filetype);
	dir->name[0] = '.';
	dir->name[1] = '.';

	/*
	 * Adjust the last rec_len
	 */
	offset = EXT2_DIR_REC_LEN(1) + EXT2_DIR_REC_LEN(2);
	dir = (struct ext2_dir_entry *) (bbuf + offset);
	memcpy(bbuf + offset, ibuf + EXT4_INLINE_DATA_DOTDOT_SIZE,
	       size - EXT4_INLINE_DATA_DOTDOT_SIZE);
	size += EXT2_DIR_REC_LEN(1) + EXT2_DIR_REC_LEN(2) -
		EXT4_INLINE_DATA_DOTDOT_SIZE;

	do {
		dir2 = dir;
		retval = ext2fs_get_rec_len(fs, dir, &rec_len);
		if (retval)
			goto err;
		offset += rec_len;
		dir = (struct ext2_dir_entry *) (bbuf + offset);
	} while (offset < size);
	rec_len += fs->blocksize - csum_size - offset;
	retval = ext2fs_set_rec_len(fs, rec_len, dir2);
	if (retval)
		goto err;

	if (csum_size) {
		t = EXT2_DIRENT_TAIL(bbuf, fs->blocksize);
		ext2fs_initialize_dirent_tail(fs, t);
	}

err:
	return retval;
}

static errcode_t
ext2fs_inline_data_dir_expand(ext2_filsys fs, ext2_ino_t ino,
			      struct ext2_inode *inode, char *buf, size_t size)
{
	errcode_t retval;
	blk64_t blk;
	char *blk_buf;

	retval = ext2fs_get_memzero(fs->blocksize, &blk_buf);
	if (retval)
		return retval;

#ifdef WORDS_BIGENDIAN
	retval = ext2fs_dirent_swab_in2(fs, buf + EXT4_INLINE_DATA_DOTDOT_SIZE,
					size, 0);
	if (retval)
		goto errout;
#endif

	/* Adjust the rec_len */
	retval = ext2fs_inline_data_convert_dir(fs, ino, blk_buf, buf, size);
	if (retval)
		goto errout;
	/* Allocate a new block */
	retval = ext2fs_new_block2(fs, 0, 0, &blk);
	if (retval)
		goto errout;
	retval = ext2fs_write_dir_block4(fs, blk, blk_buf, 0, ino);
	if (retval)
		goto errout;

	/* Update inode */
	if (ext2fs_has_feature_extents(fs->super))
		inode->i_flags |= EXT4_EXTENTS_FL;
	inode->i_flags &= ~EXT4_INLINE_DATA_FL;
	retval = ext2fs_iblk_add_blocks(fs, inode, 1);
	if (retval)
		goto errout;
	inode->i_size = fs->blocksize;
	retval = ext2fs_bmap2(fs, ino, inode, 0, BMAP_SET, 0, 0, &blk);
	if (retval)
		goto errout;
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		goto errout;
	ext2fs_block_alloc_stats(fs, blk, +1);

errout:
	ext2fs_free_mem(&blk_buf);
	return retval;
}

static errcode_t
ext2fs_inline_data_file_expand(ext2_filsys fs, ext2_ino_t ino,
			       struct ext2_inode *inode, char *buf, size_t size)
{
	ext2_file_t e2_file;
	errcode_t retval;

	/* Update inode */
	memset(inode->i_block, 0, sizeof(inode->i_block));
	if (ext2fs_has_feature_extents(fs->super)) {
		ext2_extent_handle_t handle;

		inode->i_flags &= ~EXT4_EXTENTS_FL;
		retval = ext2fs_extent_open2(fs, ino, inode, &handle);
		if (retval)
			return retval;
		ext2fs_extent_free(handle);
	}
	inode->i_flags &= ~EXT4_INLINE_DATA_FL;
	inode->i_size = 0;
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		return retval;

	/* Write out the block buffer */
	retval = ext2fs_file_open(fs, ino, EXT2_FILE_WRITE, &e2_file);
	if (retval)
		return retval;
	retval = ext2fs_file_write(e2_file, buf, size, 0);
	ext2fs_file_close(e2_file);
	return retval;
}

errcode_t ext2fs_inline_data_expand(ext2_filsys fs, ext2_ino_t ino)
{
	struct ext2_inode inode;
	struct ext2_inline_data data;
	errcode_t retval;
	size_t inline_size;
	char *inline_buf = 0;

	EXT2_CHECK_MAGIC(fs, EXT2_ET_MAGIC_EXT2FS_FILSYS);

	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		return retval;

	if (!(inode.i_flags & EXT4_INLINE_DATA_FL))
		return EXT2_ET_NO_INLINE_DATA;

	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;
	inline_size = data.ea_size + EXT4_MIN_INLINE_DATA_SIZE;
	retval = ext2fs_get_mem(inline_size, &inline_buf);
	if (retval)
		goto errout;

	memcpy(inline_buf, (void *)inode.i_block, EXT4_MIN_INLINE_DATA_SIZE);
	if (data.ea_size > 0) {
		memcpy(inline_buf + EXT4_MIN_INLINE_DATA_SIZE,
		       data.ea_data, data.ea_size);
	}

	memset((void *)inode.i_block, 0, EXT4_MIN_INLINE_DATA_SIZE);
	/*
	 * NOTE: We must do this write -> ea_remove -> read cycle here because
	 * removing the inline data EA can free the EA block, which is a change
	 * that our stack copy of the inode will never see.  If that happens,
	 * we can end up with the EA block and lblk 0 pointing to the same
	 * pblk, which is bad news.
	 */
	retval = ext2fs_write_inode(fs, ino, &inode);
	if (retval)
		goto errout;
	retval = ext2fs_inline_data_ea_remove(fs, ino);
	if (retval)
		goto errout;
	retval = ext2fs_read_inode(fs, ino, &inode);
	if (retval)
		goto errout;

	if (LINUX_S_ISDIR(inode.i_mode)) {
		retval = ext2fs_inline_data_dir_expand(fs, ino, &inode,
						inline_buf, inline_size);
	} else {
		retval = ext2fs_inline_data_file_expand(fs, ino, &inode,
						inline_buf, inline_size);
	}

errout:
	if (inline_buf)
		ext2fs_free_mem(&inline_buf);
	ext2fs_free_mem(&data.ea_data);
	return retval;
}

/*
 * When caller uses this function to retrieve the inline data, it must
 * allocate a buffer which has the size of inline data.  The size of
 * inline data can be know by ext2fs_inline_data_get_size().
 */
errcode_t ext2fs_inline_data_get(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 void *buf, size_t *size)
{
	struct ext2_inode inode_buf;
	struct ext2_inline_data data;
	errcode_t retval;

	if (!inode) {
		retval = ext2fs_read_inode(fs, ino, &inode_buf);
		if (retval)
			return retval;
		inode = &inode_buf;
	}

	data.fs = fs;
	data.ino = ino;
	retval = ext2fs_inline_data_ea_get(&data);
	if (retval)
		return retval;

	memcpy(buf, (void *)inode->i_block, EXT4_MIN_INLINE_DATA_SIZE);
	if (data.ea_size > 0)
		memcpy((char *) buf + EXT4_MIN_INLINE_DATA_SIZE,
		       data.ea_data, data.ea_size);

	if (size)
		*size = EXT4_MIN_INLINE_DATA_SIZE + data.ea_size;
	ext2fs_free_mem(&data.ea_data);
	return 0;
}

errcode_t ext2fs_inline_data_set(ext2_filsys fs, ext2_ino_t ino,
				 struct ext2_inode *inode,
				 void *buf, size_t size)
{
	struct ext2_inode inode_buf;
	struct ext2_inline_data data = {
		.fs = fs,
		.ino = ino,
	};
	errcode_t retval;
	size_t free_ea_size, existing_size, free_inode_size;

	if (!inode) {
		retval = ext2fs_read_inode(fs, ino, &inode_buf);
		if (retval)
			return retval;
		inode = &inode_buf;
	}

	if (size <= EXT4_MIN_INLINE_DATA_SIZE) {
		memcpy((void *)inode->i_block, buf, size);
	} else {
		retval = ext2fs_xattr_inode_max_size(fs, ino, &free_ea_size);
		if (retval)
			return retval;

		retval = ext2fs_inline_data_size(fs, ino, &existing_size);
		if (retval)
			return retval;

		if (existing_size < EXT4_MIN_INLINE_DATA_SIZE) {
			free_inode_size = EXT4_MIN_INLINE_DATA_SIZE -
					  existing_size;
		} else {
			free_inode_size = 0;
		}

		if (size != existing_size &&
		    size > existing_size + free_ea_size + free_inode_size)
			return EXT2_ET_INLINE_DATA_NO_SPACE;

		memcpy((void *)inode->i_block, buf, EXT4_MIN_INLINE_DATA_SIZE);
		if (size > EXT4_MIN_INLINE_DATA_SIZE)
			data.ea_size = size - EXT4_MIN_INLINE_DATA_SIZE;
		data.ea_data = (char *) buf + EXT4_MIN_INLINE_DATA_SIZE;
	}
	retval = ext2fs_write_inode(fs, ino, inode);
	if (retval)
		return retval;
	return ext2fs_inline_data_ea_set(&data);
}

#ifdef DEBUG
#include "e2p/e2p.h"

/*
 * The length of buffer is set to 64 because in inode's i_block member it only
 * can save 60 bytes.  Thus this value can let the data being saved in extra
 * space.
 */
#define BUFF_SIZE (64)

static errcode_t file_test(ext2_filsys fs)
{
	struct ext2_inode inode;
	ext2_ino_t newfile;
	errcode_t retval;
	size_t size;
	char *buf = 0, *cmpbuf = 0;

	/* create a new file */
	retval = ext2fs_new_inode(fs, 2, 010755, 0, &newfile);
	if (retval) {
		com_err("file_test", retval, "while allocating a new inode");
		return 1;
	}

	memset(&inode, 0, sizeof(inode));
	inode.i_flags |= EXT4_INLINE_DATA_FL;
	inode.i_size = EXT4_MIN_INLINE_DATA_SIZE;
	inode.i_mode = LINUX_S_IFREG;
	retval = ext2fs_write_new_inode(fs, newfile, &inode);
	if (retval) {
		com_err("file_test", retval, "while writing a new inode");
		return 1;
	}

	retval = ext2fs_inline_data_init(fs, newfile);
	if (retval) {
		com_err("file_test", retval, "while init 'system.data'");
		return 1;
	}

	retval = ext2fs_inline_data_size(fs, newfile, &size);
	if (retval) {
		com_err("file_test", retval, "while getting size");
		return 1;
	}

	if (size != EXT4_MIN_INLINE_DATA_SIZE) {
		fprintf(stderr,
			"tst_inline_data: size of inline data is wrong\n");
		return 1;
	}

	ext2fs_get_mem(BUFF_SIZE, &buf);
	memset(buf, 'a', BUFF_SIZE);
	retval = ext2fs_inline_data_set(fs, newfile, 0, buf, BUFF_SIZE);
	if (retval) {
		com_err("file_test", retval,
			"while setting inline data %s", buf);
		goto err;
	}

	ext2fs_get_mem(BUFF_SIZE, &cmpbuf);
	retval = ext2fs_inline_data_get(fs, newfile, 0, cmpbuf, &size);
	if (retval) {
		com_err("file_test", retval, "while getting inline data");
		goto err;
	}

	if (size != BUFF_SIZE) {
		fprintf(stderr,
			"tst_inline_data: size %lu != buflen %u\n",
			size, BUFF_SIZE);
		retval = 1;
		goto err;
	}

	if (memcmp(buf, cmpbuf, BUFF_SIZE)) {
		fprintf(stderr, "tst_inline_data: buf != cmpbuf\n");
		retval = 1;
		goto err;
	}

	retval = ext2fs_punch(fs, newfile, 0, 0, 0, ~0ULL);
	if (retval) {
		com_err("file_test", retval, "while truncating inode");
		goto err;
	}

	/* reload inode and check isize */
	ext2fs_read_inode(fs, newfile, &inode);
	if (inode.i_size != 0) {
		fprintf(stderr, "tst_inline_data: i_size should be 0\n");
		retval = 1;
	}

err:
	if (cmpbuf)
		ext2fs_free_mem(&cmpbuf);
	if (buf)
		ext2fs_free_mem(&buf);
	return retval;
}

static errcode_t dir_test(ext2_filsys fs)
{
	const char *dot_name = ".";
	const char *stub_name = "stub";
	const char *parent_name = "test";
	ext2_ino_t parent, dir, tmp;
	errcode_t retval;
	char dirname[32];
	int i;

	retval = ext2fs_mkdir(fs, 11, 11, stub_name);
	if (retval) {
		com_err("dir_test", retval, "while creating %s dir", stub_name);
		return retval;
	}

	retval = ext2fs_mkdir(fs, 11, 0, parent_name);
	if (retval) {
		com_err("dir_test", retval,
			"while creating %s dir", parent_name);
		return retval;
	}

	retval = ext2fs_lookup(fs, 11, parent_name, strlen(parent_name),
			       0, &parent);
	if (retval) {
		com_err("dir_test", retval,
			"while looking up %s dir", parent_name);
		return retval;
	}

	retval = ext2fs_lookup(fs, parent, dot_name, strlen(dot_name),
			       0, &tmp);
	if (retval) {
		com_err("dir_test", retval,
			"while looking up %s dir", parent_name);
		return retval;
	}

	if (parent != tmp) {
		fprintf(stderr, "tst_inline_data: parent (%u) != tmp (%u)\n",
			parent, tmp);
		return 1;
	}

	for (i = 0, dir = 13; i < 4; i++, dir++) {
		tmp = 0;
		snprintf(dirname, sizeof(dirname), "%d", i);
		retval = ext2fs_mkdir(fs, parent, 0, dirname);
		if (retval) {
			com_err("dir_test", retval,
				"while creating %s dir", dirname);
			return retval;
		}

		retval = ext2fs_lookup(fs, parent, dirname, strlen(dirname),
				       0, &tmp);
		if (retval) {
			com_err("dir_test", retval,
				"while looking up %s dir", parent_name);
			return retval;
		}

		if (dir != tmp) {
			fprintf(stderr,
				"tst_inline_data: dir (%u) != tmp (%u)\n",
				dir, tmp);
			return 1;
		}
	}

	snprintf(dirname, sizeof(dirname), "%d", i);
	retval = ext2fs_mkdir(fs, parent, 0, dirname);
	if (retval && retval != EXT2_ET_DIR_NO_SPACE) {
		com_err("dir_test", retval, "while creating %s dir", dirname);
		return retval;
	}

	retval = ext2fs_expand_dir(fs, parent);
	if (retval) {
		com_err("dir_test", retval, "while expanding %s dir", parent_name);
		return retval;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	ext2_filsys		fs;
	struct ext2_super_block param;
	errcode_t		retval;

	/* setup */
	initialize_ext2_error_table();

	memset(&param, 0, sizeof(param));
	ext2fs_blocks_count_set(&param, 32768);
	param.s_inodes_count = 100;

	param.s_feature_incompat |= EXT4_FEATURE_INCOMPAT_INLINE_DATA;
	param.s_rev_level = EXT2_DYNAMIC_REV;
	param.s_inode_size = 256;

	retval = ext2fs_initialize("test fs", EXT2_FLAG_64BITS, &param,
				   test_io_manager, &fs);
	if (retval) {
		com_err("setup", retval,
			"while initializing filesystem");
		exit(1);
	}

	retval = ext2fs_allocate_tables(fs);
	if (retval) {
		com_err("setup", retval,
			"while allocating tables for test filesystem");
		exit(1);
	}

	/* initialize inode cache */
	if (!fs->icache) {
		ext2_ino_t first_ino = EXT2_FIRST_INO(fs->super);
		int i;

		/* we just want to init inode cache.  So ignore error */
		ext2fs_create_inode_cache(fs, 16);
		if (!fs->icache) {
			fprintf(stderr,
				"tst_inline_data: init inode cache failed\n");
			exit(1);
		}

		/* setup inode cache */
		for (i = 0; i < fs->icache->cache_size; i++)
			fs->icache->cache[i].ino = first_ino++;
	}

	/* test */
	if (file_test(fs)) {
		fprintf(stderr, "tst_inline_data(FILE): FAILED\n");
		return 1;
	}
	printf("tst_inline_data(FILE): OK\n");

	if (dir_test(fs)) {
		fprintf(stderr, "tst_inline_data(DIR): FAILED\n");
		return 1;
	}
	printf("tst_inline_data(DIR): OK\n");
	ext2fs_free(fs);

	return 0;
}
#endif
