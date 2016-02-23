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
 * fatchain.c
 *
 * Follow a FAT chain
 */

#include "libfatint.h"
#include "ulint.h"

/*
 * Convert a cluster number (or 0 for the root directory) to a
 * sector number.  Return -1 on failure.
 */
libfat_sector_t libfat_clustertosector(const struct libfat_filesystem *fs,
				       int32_t cluster)
{
    if (cluster == 0)
	cluster = fs->rootcluster;

    if (cluster == 0)
	return fs->rootdir;
    else if (cluster < 2 || cluster >= fs->endcluster)
	return -1;
    else
	return fs->data + ((libfat_sector_t) (cluster - 2) << fs->clustshift);
}

/*
 * Get the next sector of either the root directory or a FAT chain.
 * Returns 0 on end of file and -1 on error.
 */

libfat_sector_t libfat_nextsector(struct libfat_filesystem * fs,
				  libfat_sector_t s)
{
    int32_t cluster, nextcluster;
    uint32_t fatoffset;
    libfat_sector_t fatsect;
    uint8_t *fsdata;
    uint32_t clustmask = fs->clustsize - 1;
    libfat_sector_t rs;

    if (s < fs->data) {
	if (s < fs->rootdir)
	    return -1;

	/* Root directory */
	s++;
	return (s < fs->data) ? s : 0;
    }

    rs = s - fs->data;

    if (~rs & clustmask)
	return s + 1;		/* Next sector in cluster */

    cluster = 2 + (rs >> fs->clustshift);

    if (cluster >= fs->endcluster)
	return -1;

    switch (fs->fat_type) {
    case FAT12:
	/* Get first byte */
	fatoffset = cluster + (cluster >> 1);
	fatsect = fs->fat + (fatoffset >> LIBFAT_SECTOR_SHIFT);
	fsdata = libfat_get_sector(fs, fatsect);
	if (!fsdata)
	    return -1;
	nextcluster = fsdata[fatoffset & LIBFAT_SECTOR_MASK];

	/* Get second byte */
	fatoffset++;
	fatsect = fs->fat + (fatoffset >> LIBFAT_SECTOR_SHIFT);
	fsdata = libfat_get_sector(fs, fatsect);
	if (!fsdata)
	    return -1;
	nextcluster |= fsdata[fatoffset & LIBFAT_SECTOR_MASK] << 8;

	/* Extract the FAT entry */
	if (cluster & 1)
	    nextcluster >>= 4;
	else
	    nextcluster &= 0x0FFF;

	if (nextcluster >= 0x0FF8)
	    return 0;
	break;

    case FAT16:
	fatoffset = cluster << 1;
	fatsect = fs->fat + (fatoffset >> LIBFAT_SECTOR_SHIFT);
	fsdata = libfat_get_sector(fs, fatsect);
	if (!fsdata)
	    return -1;
	nextcluster =
	    read16((le16_t *) & fsdata[fatoffset & LIBFAT_SECTOR_MASK]);

	if (nextcluster >= 0x0FFF8)
	    return 0;
	break;

    case FAT28:
	fatoffset = cluster << 2;
	fatsect = fs->fat + (fatoffset >> LIBFAT_SECTOR_SHIFT);
	fsdata = libfat_get_sector(fs, fatsect);
	if (!fsdata)
	    return -1;
	nextcluster =
	    read32((le32_t *) & fsdata[fatoffset & LIBFAT_SECTOR_MASK]);
	nextcluster &= 0x0FFFFFFF;

	if (nextcluster >= 0x0FFFFFF8)
	    return 0;
	break;

    default:
	return -1;		/* WTF? */
    }

    return libfat_clustertosector(fs, nextcluster);
}
