/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1998-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009-2010 Intel Corporation; author H. Peter Anvin
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * fat.c - Initial sanity check for FAT-based installers
 */

#define _XOPEN_SOURCE 500	/* Required on glibc 2.x */
#define _BSD_SOURCE
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include "syslinux.h"
#include "syslxint.h"

void syslinux_make_bootsect(void *bs)
{
    struct boot_sector *bootsect = bs;
    const struct boot_sector *sbs =
	(const struct boot_sector *)boot_sector;

    memcpy(&bootsect->bsHead, &sbs->bsHead, bsHeadLen);
    memcpy(&bootsect->bsCode, &sbs->bsCode, bsCodeLen);
}

/*
 * Check to see that what we got was indeed an MS-DOS boot sector/superblock;
 * Return NULL if OK and otherwise an error message;
 */
const char *syslinux_check_bootsect(const void *bs)
{
    int sectorsize;
    long long sectors, fatsectors, dsectors;
    long long clusters;
    int rootdirents, clustersize;
    const struct boot_sector *sectbuf = bs;

    /* Must be 0xF0 or 0xF8..0xFF */
    if (get_8(&sectbuf->bsMedia) != 0xF0 && get_8(&sectbuf->bsMedia) < 0xF8)
	return "invalid media signature (not a FAT filesystem?)";

    sectorsize = get_16(&sectbuf->bsBytesPerSec);
    if (sectorsize == SECTOR_SIZE)
	;			/* ok */
    else if (sectorsize >= 512 && sectorsize <= 4096 &&
	     (sectorsize & (sectorsize - 1)) == 0)
	return "unsupported sectors size";
    else
	return "impossible sector size";

    clustersize = get_8(&sectbuf->bsSecPerClust);
    if (clustersize == 0 || (clustersize & (clustersize - 1)))
	return "impossible cluster size";

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
	return "negative number of data sectors";

    if (fatsectors == 0)
	return "zero FAT sectors";

    clusters = dsectors / clustersize;

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
		static char fserr[] =
		    "filesystem type \"????????\" not supported";
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
	return "impossibly large number of clusters";
    }

    return NULL;
}
