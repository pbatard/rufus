/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2019 Pete Batard <pete@akeo.ie>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * dumpdir.c
 *
 * Returns all files and directory items from a FAT directory.
 */

#include <string.h>
#include "libfatint.h"

static struct fat_dirent* get_next_dirent(struct libfat_filesystem *fs,
					  libfat_sector_t *sector, int *offset)
{
    struct fat_dirent *dep;

    *offset += sizeof(struct fat_dirent);
    if (*offset >= LIBFAT_SECTOR_SIZE) {
	*offset = 0;
	*sector = libfat_nextsector(fs, *sector);
	if ((*sector == 0) || (*sector == (libfat_sector_t)-1))
	    return NULL;
    }
    dep = libfat_get_sector(fs, *sector);
    if (!dep)
	return NULL;
    dep = (struct fat_dirent*) &((char*)dep)[*offset];
    return dep;
}

static void fill_utf16(wchar_t *name, unsigned char *entry)
{
    int i;
    for (i=0; i<5; i++)
	name[i] = read16((le16_t*)&entry[1 + 2*i]);
    for (i=5; i<11; i++)
	name[i] = read16((le16_t*)&entry[4 + 2*i]);
    for (i=11; i<12; i++)
	name[i] = read16((le16_t*)&entry[6 + 2*i]);
}

int libfat_dumpdir(struct libfat_filesystem *fs, libfat_dirpos_t *dp,
		   libfat_diritem_t *di)
{
    int i, j;
    struct fat_dirent *dep;

    memset(di->name, 0, sizeof(di->name));
    di->size = 0;
    di->attributes = 0;
    if (dp->offset < 0) {
	/* First entry */
	dp->offset = 0;
	dp->sector = libfat_clustertosector(fs, dp->cluster);
	if ((dp->sector == 0) || (dp->sector == (libfat_sector_t)-1))
	    return -1;
	dep = libfat_get_sector(fs, dp->sector);
    } else {
	dep = get_next_dirent(fs, &dp->sector, &dp->offset);
    }
    if (!dep)
	return -1;	/* Read error */

    /* Ignore volume labels, deleted entries as well as '.' and '..' entries */
    while ((dep->attribute == 0x08) || (dep->name[0] == 0xe5) ||
	   ((dep->name[0] == '.') && (dep->name[2] == ' ') &&
	    ((dep->name[1] == ' ') || (dep->name[1] == '.')))) {
	dep = get_next_dirent(fs, &dp->sector, &dp->offset);
	if (!dep)
	    return -1;
    }

    if (dep->name[0] == 0)
	return -2;	/* Last entry */

    /* Build UCS-2 name */
    j = -1;
    while (dep->attribute == 0x0F) {	/* LNF (Long File Name) entry */
	i = dep->name[0];
	if ((j < 0) && ((i & 0xF0) != 0x40))  /* End of LFN marker was not found */
	    break;
	/* Isolate and check the sequence number, which should be decrementing */
	i = (i & 0x0F) - 1;
	if ((j >= 0) && (i != j - 1))
	    return -3;
	j = i;
	fill_utf16(&di->name[13 * i], dep->name);
	dep = get_next_dirent(fs, &dp->sector, &dp->offset);
	if (!dep)
	    return -1;
    }

    if (di->name[0] == 0) {
	for (i = 0, j = 0; i < 12; i++) {
	    if ((i >= 8) && (dep->name[i] == ' '))
		break;
	    if (i == 8)
		di->name[j++] = '.';
	    if (dep->name[i] == ' ')
		continue;
	    di->name[j] = dep->name[i];
	    /* Caseflags: bit 3 = lowercase basename, bit 4 = lowercase extension */
	    if ((di->name[j] >= 'A') && (di->name[j] <= 'Z')) {
		if ((dep->caseflags & 0x02) && (i < 8))
		    di->name[j] += 0x20;
		if ((dep->caseflags & 0x04) && (i >= 8))
		    di->name[j] += 0x20;
	    }
	    j++;
	}
    }

    di->attributes = dep->attribute & 0x37;
    di->size = read32(&dep->size);
    return read16(&dep->clustlo) + (read16(&dep->clusthi) << 16);
}
