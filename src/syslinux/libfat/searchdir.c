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
 * searchdir.c
 *
 * Search a FAT directory for a particular pre-mangled filename.
 * Copies the directory entry into direntry and returns the starting cluster
 * if found; returns -2 on not found, -1 on error, 0 on empty file.
 */

#include <string.h>
#include "libfatint.h"

int32_t libfat_searchdir(struct libfat_filesystem *fs, int32_t dirclust,
			 const void *name, struct libfat_direntry *direntry)
{
    struct fat_dirent *dep;
    int nent;
    libfat_sector_t s = libfat_clustertosector(fs, dirclust);

    while (1) {
	if (s == 0)
	    return -2;		/* Not found */
	else if (s == (libfat_sector_t) - 1)
	    return -1;		/* Error */

	dep = libfat_get_sector(fs, s);
	if (!dep)
	    return -1;		/* Read error */

	for (nent = 0; nent < LIBFAT_SECTOR_SIZE;
	     nent += sizeof(struct fat_dirent)) {
	    if (!memcmp(dep->name, name, 11)) {
		if (direntry) {
		    memcpy(direntry->entry, dep, sizeof(*dep));
		    direntry->sector = s;
		    direntry->offset = nent;
		}
		if (read32(&dep->size) == 0)
		    return 0;	/* An empty file has no clusters */
		else
		    return read16(&dep->clustlo) +
			(read16(&dep->clusthi) << 16);
	    }

	    if (dep->name[0] == 0)
		return -2;	/* Hit high water mark */

	    dep++;
	}

	s = libfat_nextsector(fs, s);
    }
}
