/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1998-2011 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009-2011 Intel Corporation; author H. Peter Anvin
 *   Copyright 2011 Paulo Alcantara <pcacjr@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * fs.c - Generic sanity check for FAT/NTFS-based installers
 */

#define _XOPEN_SOURCE 500	/* Required on glibc 2.x */
#define _BSD_SOURCE
/* glibc 2.20 deprecates _BSD_SOURCE in favour of _DEFAULT_SOURCE */
#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include "syslinux.h"
#include "syslxint.h"
#include "syslxcom.h"
#include "syslxfs.h"

void syslinux_make_bootsect(void *bs, int fs_type)
{
    if (fs_type == VFAT) {
	struct fat_boot_sector *bootsect = bs;
	const struct fat_boot_sector *sbs =
	    (const struct fat_boot_sector *)boot_sector;

	memcpy(&bootsect->FAT_bsHead, &sbs->FAT_bsHead, FAT_bsHeadLen);
	memcpy(&bootsect->FAT_bsCode, &sbs->FAT_bsCode, FAT_bsCodeLen);
    } else if (fs_type == NTFS) {
	struct ntfs_boot_sector *bootsect = bs;
	const struct ntfs_boot_sector *sbs =
	    (const struct ntfs_boot_sector *)boot_sector;

	memcpy(&bootsect->NTFS_bsHead, &sbs->NTFS_bsHead, NTFS_bsHeadLen);
	memcpy(&bootsect->NTFS_bsCode, &sbs->NTFS_bsCode, NTFS_bsCodeLen);
    }
}

static const char *check_fat_bootsect(const void *bs, int *fs_type)
{
    int sectorsize;
    const struct fat_boot_sector *sectbuf = bs;
    long long sectors, fatsectors, dsectors;
    long long clusters;
    int rootdirents, clustersize;

    sectorsize = get_16(&sectbuf->bsBytesPerSec);

    clustersize = get_8(&sectbuf->bsSecPerClust);
    if (clustersize == 0 || (clustersize & (clustersize - 1)))
	return "impossible cluster size on an FAT volume";

    sectors = get_16(&sectbuf->bsSectors);
    sectors = sectors ? sectors : get_32(&sectbuf->bsHugeSectors);

    dsectors = sectors - get_16(&sectbuf->bsResSectors);

    fatsectors = get_16(&sectbuf->bsFATsecs);
    fatsectors = fatsectors ? fatsectors : get_32(&sectbuf->bs32.FATSz32);
    fatsectors *= get_8(&sectbuf->bsFATs);
    dsectors -= fatsectors;

    rootdirents = get_16(&sectbuf->bsRootDirEnts);
    dsectors -= (rootdirents + sectorsize / 32 - 1) / sectorsize;

    if (dsectors < 0)
	return "negative number of data sectors on an FAT volume";

    clusters = dsectors / clustersize;

    fatsectors = get_16(&sectbuf->bsFATsecs);
    fatsectors = fatsectors ? fatsectors : get_32(&sectbuf->bs32.FATSz32);
    fatsectors *= get_8(&sectbuf->bsFATs);

    if (!fatsectors)
	return "zero FAT sectors";

    if (clusters < 0xFFF5) {
	/* FAT12 or FAT16 */
	if (!get_16(&sectbuf->bsFATsecs))
	    return "zero FAT sectors (FAT12/16)";

	if (get_8(&sectbuf->bs16.BootSignature) == 0x29) {
	    if (!memcmp(&sectbuf->bs16.FileSysType, "FAT12   ", 8)) {
		if (clusters >= 0xFF5)
		    return "more than 4084 clusters but claims FAT12";
	    } else if (!memcmp(&sectbuf->bs16.FileSysType, "FAT16   ", 8)) {
		if (clusters < 0xFF5)
		    return "less than 4084 clusters but claims FAT16";
	    } else if (!memcmp(&sectbuf->bs16.FileSysType, "FAT32   ", 8)) {
		return "less than 65525 clusters but claims FAT32";
	    } else if (memcmp(&sectbuf->bs16.FileSysType, "FAT     ", 8)) {
		static char fserr[] = "filesystem type \"????????\" not "
		    "supported";
		memcpy(fserr + 17, &sectbuf->bs16.FileSysType, 8);
		return fserr;
	    }
	}
    } else if (clusters < 0x0FFFFFF5) {
	/*
	 * FAT32...
	 *
	 * Moving the FileSysType and BootSignature was a lovely stroke
	 * of M$ idiocy...
	 */
	if (get_8(&sectbuf->bs32.BootSignature) != 0x29 ||
	    memcmp(&sectbuf->bs32.FileSysType, "FAT32   ", 8))
	    return "missing FAT32 signature";
    } else {
	return "impossibly large number of clusters on an FAT volume";
    }

    if (fs_type)
	*fs_type = VFAT;

    return NULL;
}

static const char *check_ntfs_bootsect(const void *bs, int *fs_type)
{
    const struct ntfs_boot_sector *sectbuf = bs;

    if (memcmp(&sectbuf->bsOemName, "NTFS    ", 8) &&
	memcmp(&sectbuf->bsOemName, "MSWIN4.0", 8) &&
	memcmp(&sectbuf->bsOemName, "MSWIN4.1", 8))
	return "unknown OEM name but claims NTFS";

    if (fs_type)
	*fs_type = NTFS;

    return NULL;
}

const char *syslinux_check_bootsect(const void *bs, int *fs_type)
{
    uint8_t media_sig;
    int sectorsize;
    const struct fat_boot_sector *sectbuf = bs;
    const char *retval;

    media_sig = get_8(&sectbuf->bsMedia);
    /* Must be 0xF0 or 0xF8-0xFF for FAT/NTFS volumes */
    if (media_sig != 0xF0 && media_sig < 0xF8)
	return "invalid media signature (not an FAT/NTFS volume?)";

    sectorsize = get_16(&sectbuf->bsBytesPerSec);
    if (sectorsize == SECTOR_SIZE) ;	/* ok */
    else if (sectorsize >= 512 && sectorsize <= 4096 &&
	     (sectorsize & (sectorsize - 1)) == 0)
	return "unsupported sectors size";
    else
	return "impossible sector size";

    if (ntfs_check_zero_fields((struct ntfs_boot_sector *)bs))
	retval = check_ntfs_bootsect(bs, fs_type);
    else
	retval = check_fat_bootsect(bs, fs_type);

    return retval;
}
