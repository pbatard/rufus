/* ----------------------------------------------------------------------- *
 *
 *   Copyright 1998-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009-2014 Intel Corporation; author H. Peter Anvin
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * syslxmod.c - Code to provide a SYSLINUX code set to an installer.
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


/*
 * Generate sector extents
 */
static void generate_extents(struct syslinux_extent _slimg *ex, int nptrs,
			     const sector_t *sectp, int nsect)
{
    uint32_t addr = 0x8000;	/* ldlinux.sys starts loading here */
    uint32_t base;
    sector_t sect, lba;
    unsigned int len;

    base = addr;
    len = 0;
    lba = 0;

    memset_sl(ex, 0, nptrs * sizeof *ex);

    while (nsect) {
	sect = *sectp++;

	if (len) {
	    uint32_t xbytes = (len + 1) * SECTOR_SIZE;

	    if (sect == lba + len && xbytes < 65536 &&
		((addr ^ (base + xbytes - 1)) & 0xffff0000) == 0) {
		/* We can add to the current extent */
		len++;
		goto next;
	    }

	    set_64_sl(&ex->lba, lba);
	    set_16_sl(&ex->len, len);
	    ex++;
	}

	base = addr;
	lba  = sect;
	len  = 1;

    next:
	addr += SECTOR_SIZE;
	nsect--;
    }

    if (len) {
	set_64_sl(&ex->lba, lba);
	set_16_sl(&ex->len, len);
	ex++;
    }
}

/*
 * Form a pointer based on a 16-bit patcharea/epa field
 */
static inline void *ptr(void *img, const uint16_t _slimg *offset_p)
{
    return (char *)img + get_16_sl(offset_p);
}
static inline void _slimg *slptr(void _slimg *img,
				 const uint16_t _slimg *offset_p)
{
    return (char _slimg *)img + get_16_sl(offset_p);
}

/*
 * This patches the boot sector and the beginning of ldlinux.sys
 * based on an ldlinux.sys sector map passed in.  Typically this is
 * handled by writing ldlinux.sys, mapping it, and then overwrite it
 * with the patched version.  If this isn't safe to do because of
 * an OS which does block reallocation, then overwrite it with
 * direct access since the location is known.
 *
 * Returns the number of modified bytes in ldlinux.sys if successful,
 * otherwise -1.
 */
int syslinux_patch(const sector_t *sectp, int nsectors,
		   int stupid, int raid_mode,
		   const char *subdir, const char *subvol)
{
    struct patch_area _slimg *patcharea;
    struct ext_patch_area _slimg *epa;
    struct syslinux_extent _slimg *ex;
    const uint32_t _slimg *wp;
    int nsect = ((boot_image_len + SECTOR_SIZE - 1) >> SECTOR_SHIFT) + 2;
    uint32_t csum;
    int i, dw, nptrs;
    struct fat_boot_sector *sbs = (struct fat_boot_sector *)boot_sector;
    uint64_t _slimg *advptrs;

    if (nsectors < nsect)
	return -1;		/* The actual file is too small for content */

    /* Search for LDLINUX_MAGIC to find the patch area */
    dw = (boot_image_len - sizeof(struct patch_area)) >> 2;
    for (i = 0, wp = (const uint32_t _slimg *)boot_image;
	 (i <= dw) && ((get_32_sl(wp) != LDLINUX_MAGIC));
	 i++, wp++);
    if (i > dw)	/* Not found */
	return -1;
    patcharea = (struct patch_area _slimg *)wp;
    epa = slptr(boot_image, &patcharea->epaoffset);

    /* First sector need pointer in boot sector */
    set_32(ptr(sbs, &epa->sect1ptr0), sectp[0]);
    set_32(ptr(sbs, &epa->sect1ptr1), sectp[0] >> 32);
    sectp++;

    /* Handle RAID mode */
    if (raid_mode) {
	/* Patch in INT 18h = CD 18 */
	set_16(ptr(sbs, &epa->raidpatch), 0x18CD);
    }

    /* Set up the totals */
    dw = boot_image_len >> 2;	/* COMPLETE dwords, excluding ADV */
    set_16_sl(&patcharea->data_sectors, nsect - 2); /* Not including ADVs */
    set_16_sl(&patcharea->adv_sectors, 2);	/* ADVs need 2 sectors */
    set_32_sl(&patcharea->dwords, dw);

    /* Handle Stupid mode */
    if (stupid) {
	/* Access only one sector at a time */
	set_16_sl(&patcharea->maxtransfer, 1);
    }

    /* Set the sector extents */
    ex = slptr(boot_image, &epa->secptroffset);
    nptrs = get_16_sl(&epa->secptrcnt);

#if 0
    if (nsect > nptrs) {
	/* Not necessarily an error in this case, but a general problem */
	fprintf(stderr, "Insufficient extent space, build error!\n");
	exit(1);
    }
#endif

    /* -1 for the pointer in the boot sector, -2 for the two ADVs */
    generate_extents(ex, nptrs, sectp, nsect-1-2);

    /* ADV pointers */
    advptrs = slptr(boot_image, &epa->advptroffset);
    set_64_sl(&advptrs[0], sectp[nsect-1-2]);
    set_64_sl(&advptrs[1], sectp[nsect-1-1]);

    /* Poke in the base directory path */
    if (subdir) {
	int sublen = strlen(subdir) + 1;
	if (get_16_sl(&epa->dirlen) < sublen) {
	    fprintf(stderr, "Subdirectory path too long... aborting install!\n");
	    exit(1);
	}
	memcpy_to_sl(slptr(boot_image, &epa->diroffset), subdir, sublen);
    }

    /* Poke in the subvolume information */
    if (subvol) {
	int sublen = strlen(subvol) + 1;
	if (get_16_sl(&epa->subvollen) < sublen) {
	    fprintf(stderr, "Subvol name too long... aborting install!\n");
	    exit(1);
	}
	memcpy_to_sl(slptr(boot_image, &epa->subvoloffset), subvol, sublen);
    }

    /* Now produce a checksum */
    set_32_sl(&patcharea->checksum, 0);

    csum = LDLINUX_MAGIC;
    for (i = 0, wp = (const uint32_t _slimg *)boot_image; i < dw; i++, wp++)
	csum -= get_32_sl(wp);	/* Negative checksum */

    set_32_sl(&patcharea->checksum, csum);

    /*
     * Assume all bytes modified.  This can be optimized at the expense
     * of keeping track of what the highest modified address ever was.
     */
    return dw << 2;
}
