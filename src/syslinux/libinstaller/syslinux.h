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

#include <inttypes.h>
#include "advconst.h"
#include "setadv.h"

#ifdef __CHECKER__
# define _slimg __attribute__((noderef,address_space(1)))
# define _force __attribute__((force))
#else
# define _slimg
# define _force
#endif

/* The standard boot sector and ldlinux image */
extern unsigned char* syslinux_ldlinux[2];
extern unsigned long syslinux_ldlinux_len[2];
extern const int syslinux_ldlinux_mtime[2];

#define boot_sector	syslinux_ldlinux[1]
#define boot_sector_len syslinux_ldlinux_len[1]
#define boot_image	syslinux_ldlinux[0]
#define boot_image_len	syslinux_ldlinux_len[0]

extern unsigned char syslinux_mbr[];
extern const unsigned int syslinux_mbr_len;
extern const int syslinux_mbr_mtime;

/* Sector size variables are defined externally for 4K support */
extern uint32_t SECTOR_SHIFT;
extern uint32_t SECTOR_SIZE;

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
