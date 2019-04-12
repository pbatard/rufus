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

static int link_proc(struct ext2_dir_entry *dirent,
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
	struct ext2_dir_entry_tail *t;

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
	 * Since ext2fs_link blows away htree data, we need to be
	 * careful -- if metadata_csum is enabled and we're passed in
	 * a dirent that contains htree data, we need to create the
	 * fake entry at the end of the block that hides the checksum.
	 */

	/* De-convert a dx_node block */
	if (csum_size &&
	    curr_rec_len == ls->fs->blocksize &&
	    !dirent->inode) {
		curr_rec_len -= csum_size;
		ls->err = ext2fs_set_rec_len(ls->fs, curr_rec_len, dirent);
		if (ls->err)
			return DIRENT_ABORT;
		t = EXT2_DIRENT_TAIL(buf, ls->fs->blocksize);
		ext2fs_initialize_dirent_tail(ls->fs, t);
		ret = DIRENT_CHANGED;
	}

	/* De-convert a dx_root block */
	if (csum_size &&
	    curr_rec_len == ls->fs->blocksize - EXT2_DIR_REC_LEN(1) &&
	    offset == EXT2_DIR_REC_LEN(1) &&
	    dirent->name[0] == '.' && dirent->name[1] == '.') {
		curr_rec_len -= csum_size;
		ls->err = ext2fs_set_rec_len(ls->fs, curr_rec_len, dirent);
		if (ls->err)
			return DIRENT_ABORT;
		t = EXT2_DIRENT_TAIL(buf, ls->fs->blocksize);
		ext2fs_initialize_dirent_tail(ls->fs, t);
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

	ls.fs = fs;
	ls.name = name;
	ls.namelen = name ? strlen(name) : 0;
	ls.inode = ino;
	ls.flags = flags;
	ls.done = 0;
	ls.sb = fs->super;
	ls.blocksize = fs->blocksize;
	ls.err = 0;

	retval = ext2fs_dir_iterate(fs, dir, DIRENT_FLAG_INCLUDE_EMPTY,
				    0, link_proc, &ls);
	if (retval)
		return retval;
	if (ls.err)
		return ls.err;

	if (!ls.done)
		return EXT2_ET_DIR_NO_SPACE;

	if ((retval = ext2fs_read_inode(fs, dir, &inode)) != 0)
		return retval;

	/*
	 * If this function changes to preserve the htree, remove the
	 * two hunks in link_proc that shove checksum tails into the
	 * former dx_root/dx_node blocks.
	 */
	if (inode.i_flags & EXT2_INDEX_FL) {
		inode.i_flags &= ~EXT2_INDEX_FL;
		if ((retval = ext2fs_write_inode(fs, dir, &inode)) != 0)
			return retval;
	}

	return 0;
}
