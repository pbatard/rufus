/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2004-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * open.c
 *
 * Open a FAT filesystem and compute some initial values; return NULL
 * on failure.
 */

#include <stdlib.h>
#include "libfatint.h"
#include "ulint.h"

struct libfat_filesystem *
libfat_open(int (*readfunc) (intptr_t, void *, size_t, libfat_sector_t),
	    intptr_t readptr)
{
    struct libfat_filesystem *fs = NULL;
    struct fat_bootsect *bs;
    int i;
    uint32_t sectors, fatsize, minfatsize, rootdirsize;
    uint32_t nclusters;

    fs = malloc(sizeof(struct libfat_filesystem));
    if (!fs)
	goto barf;

    fs->sectors = NULL;
    fs->read = readfunc;
    fs->readptr = readptr;

    bs = libfat_get_sector(fs, 0);
    if (!bs)
	goto barf;

    if (read16(&bs->bsBytesPerSec) != LIBFAT_SECTOR_SIZE)
	goto barf;

    for (i = 0; i <= 8; i++) {
	if ((uint8_t) (1 << i) == read8(&bs->bsSecPerClust))
	    break;
    }
    if (i > 8)
	goto barf;
    fs->clustsize = 1 << i;	/* Treat 0 as 2^8 = 64K */
    fs->clustshift = i;

    sectors = read16(&bs->bsSectors);
    if (!sectors)
	sectors = read32(&bs->bsHugeSectors);

    fs->end = sectors;

    fs->fat = read16(&bs->bsResSectors);
    fatsize = read16(&bs->bsFATsecs);
    if (!fatsize)
	fatsize = read32(&bs->u.fat32.bpb_fatsz32);

    fs->rootdir = fs->fat + fatsize * read8(&bs->bsFATs);

    rootdirsize = ((read16(&bs->bsRootDirEnts) << 5) + LIBFAT_SECTOR_MASK)
	>> LIBFAT_SECTOR_SHIFT;
    fs->data = fs->rootdir + rootdirsize;

    /* Sanity checking */
    if (fs->data >= fs->end)
	goto barf;

    /* Figure out how many clusters */
    nclusters = (fs->end - fs->data) >> fs->clustshift;
    fs->endcluster = nclusters + 2;

    if (nclusters <= 0xff4) {
	fs->fat_type = FAT12;
	minfatsize = fs->endcluster + (fs->endcluster >> 1);
    } else if (nclusters <= 0xfff4) {
	fs->fat_type = FAT16;
	minfatsize = fs->endcluster << 1;
    } else if (nclusters <= 0xffffff4) {
	fs->fat_type = FAT28;
	minfatsize = fs->endcluster << 2;
    } else
	goto barf;		/* Impossibly many clusters */

    minfatsize = (minfatsize + LIBFAT_SECTOR_SIZE - 1) >> LIBFAT_SECTOR_SHIFT;

    if (minfatsize > fatsize)
	goto barf;		/* The FATs don't fit */

    if (fs->fat_type == FAT28)
	fs->rootcluster = read32(&bs->u.fat32.bpb_rootclus);
    else
	fs->rootcluster = 0;

    return fs;			/* All good */

barf:
    if (fs)
	free(fs);
    return NULL;
}

void libfat_close(struct libfat_filesystem *fs)
{
    libfat_flush(fs);
    free(fs);
}
