/*
 * ind_block.c --- indirect block I/O routines
 *
 * Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
 * 	2001, 2002, 2003, 2004, 2005 by  Theodore Ts'o.
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

errcode_t ext2fs_read_ind_block(ext2_filsys fs, blk_t blk, void *buf)
{
	errcode_t	retval;
#ifdef WORDS_BIGENDIAN
	blk_t		*block_nr;
	int		i;
	int		limit = fs->blocksize >> 2;
#endif

	if ((fs->flags & EXT2_FLAG_IMAGE_FILE) &&
	    (fs->io != fs->image_io))
		memset(buf, 0, fs->blocksize);
	else {
		retval = io_channel_read_blk(fs->io, blk, 1, buf);
		if (retval)
			return retval;
	}
#ifdef WORDS_BIGENDIAN
	block_nr = (blk_t *) buf;
	for (i = 0; i < limit; i++, block_nr++)
		*block_nr = ext2fs_swab32(*block_nr);
#endif
	return 0;
}

errcode_t ext2fs_write_ind_block(ext2_filsys fs, blk_t blk, void *buf)
{
#ifdef WORDS_BIGENDIAN
	blk_t		*block_nr;
	int		i;
	int		limit = fs->blocksize >> 2;
#endif

	if (fs->flags & EXT2_FLAG_IMAGE_FILE)
		return 0;

#ifdef WORDS_BIGENDIAN
	block_nr = (blk_t *) buf;
	for (i = 0; i < limit; i++, block_nr++)
		*block_nr = ext2fs_swab32(*block_nr);
#endif
	return io_channel_write_blk(fs->io, blk, 1, buf);
}


