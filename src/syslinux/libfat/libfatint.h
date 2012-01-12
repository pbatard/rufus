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
 * libfatint.h
 *
 * Internals for the libfat filesystem
 */

#ifndef LIBFATINT_H
#define LIBFATINT_H

#include "libfat.h"
#include "fat.h"

struct libfat_sector {
    libfat_sector_t n;		/* Sector number */
    struct libfat_sector *next;	/* Next in list */
    char data[LIBFAT_SECTOR_SIZE];
};

enum fat_type {
    FAT12,
    FAT16,
    FAT28
};

struct libfat_filesystem {
    int (*read) (intptr_t, void *, size_t, libfat_sector_t);
    intptr_t readptr;

    enum fat_type fat_type;
    unsigned int clustsize;
    int clustshift;
    int32_t endcluster;		/* Highest legal cluster number + 1 */
    int32_t rootcluster;	/* Root directory cluster */

    libfat_sector_t fat;	/* Start of FAT */
    libfat_sector_t rootdir;	/* Start of root directory */
    libfat_sector_t data;	/* Start of data area */
    libfat_sector_t end;	/* End of filesystem */

    struct libfat_sector *sectors;
};

#endif /* LIBFATINT_H */
