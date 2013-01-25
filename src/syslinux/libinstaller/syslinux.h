/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1998-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef SYSLINUX_H
#define SYSLINUX_H

#include <windows.h>
#include <inttypes.h>
#include "advconst.h"
#include "setadv.h"

/* The standard boot sector and ldlinux image */
extern unsigned char* syslinux_bootsect;
extern DWORD syslinux_bootsect_len;
extern const int syslinux_bootsect_mtime;

extern unsigned char* syslinux_ldlinux;
extern DWORD syslinux_ldlinux_len;
extern const int syslinux_ldlinux_mtime;

#define boot_sector	syslinux_bootsect
#define boot_sector_len syslinux_bootsect_len
#define boot_image	syslinux_ldlinux
#define boot_image_len	syslinux_ldlinux_len

extern unsigned char syslinux_mbr[];
extern const unsigned int syslinux_mbr_len;
extern const int syslinux_mbr_mtime;

/* Sector size assumptions... */
#define SECTOR_SHIFT	9
#define SECTOR_SIZE	(1 << SECTOR_SHIFT)

/* This takes a boot sector and merges in the syslinux fields */
void syslinux_make_bootsect(void *bs, int fs_type);

/* Check to see that what we got was indeed an MS-DOS boot sector/superblock */
const char *syslinux_check_bootsect(const void *bs, int *fs_type);

/* This patches the boot sector and ldlinux.sys based on a sector map */
typedef uint64_t sector_t;
int syslinux_patch(const sector_t *sectors, int nsectors,
		   int stupid, int raid_mode,
		   const char *subdir, const char *subvol);

#endif
