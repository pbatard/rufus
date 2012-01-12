/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2003 Lars Munch Christensen - All Rights Reserved
 *   Copyright 1998-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2011 Pete Batard
 *
 *   Based on the Linux installer program for SYSLINUX by H. Peter Anvin
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <malloc.h>
#include <ctype.h>

#include "rufus.h"
#include "resource.h"

#include "syslinux.h"
#include "libfat.h"
#include "setadv.h"

unsigned char* syslinux_ldlinux;
unsigned int syslinux_ldlinux_len;
unsigned char* syslinux_bootsect;
unsigned int syslinux_bootsect_len;

/*
 * Wrapper for ReadFile suitable for libfat
 */
int libfat_readfile(intptr_t pp, void *buf, size_t secsize,
		    libfat_sector_t sector)
{
	uint64_t offset = (uint64_t) sector * secsize;
	LONG loword = (LONG) offset;
	LONG hiword = (LONG) (offset >> 32);
	LONG hiwordx = hiword;
	DWORD bytes_read;

	if (SetFilePointer((HANDLE) pp, loword, &hiwordx, FILE_BEGIN) != loword ||
		hiword != hiwordx ||
		!ReadFile((HANDLE) pp, buf, (DWORD)secsize, &bytes_read, NULL) ||
		bytes_read != secsize) {
		uprintf("Cannot read sector %u\n", sector);
		// TODO -1?
		return 0;
	}

	return (int)secsize;
}

/*
 * Extract the ldlinux.sys and ldlinux.bss from resources,
 * then patch and install them
 */
BOOL InstallSyslinux(DWORD num, const char* drive_name)
{
	HANDLE f_handle = INVALID_HANDLE_VALUE;
	HANDLE d_handle = INVALID_HANDLE_VALUE;
	DWORD bytes_read;
	DWORD bytes_written;
	BOOL r = FALSE;
	HGLOBAL res_handle;
	HRSRC res;

	static unsigned char sectbuf[SECTOR_SIZE];
	static char ldlinux_name[] = "?:\\ldlinux.sys";
	struct libfat_filesystem *fs;
	libfat_sector_t s, *secp;
	libfat_sector_t *sectors = NULL;
	int ldlinux_sectors;
	uint32_t ldlinux_cluster;
	int nsectors;

	ldlinux_name[0] = drive_name[0];

	/* Initialize the ADV -- this should be smarter */
	syslinux_reset_adv(syslinux_adv);

	/* Access ldlinux.sys resource */
	res = FindResource(hMainInstance, MAKEINTRESOURCE(IDR_SL_LDLINUX_SYS), RT_RCDATA);
	if (res == NULL) {
		uprintf("Unable to locate ldlinux.sys resource: %s\n", WindowsErrorString());
		goto out;
	}
	res_handle = LoadResource(NULL, res);
	if (res_handle == NULL) {
		uprintf("Unable to load ldlinux.sys resource: %s\n", WindowsErrorString());
		goto out;
	}
	syslinux_ldlinux = (unsigned char*)LockResource(res_handle);
	syslinux_ldlinux_len = SizeofResource(NULL, res);

	/* Access ldlinux.bss resource */
	res = FindResource(hMainInstance, MAKEINTRESOURCE(IDR_SL_LDLINUX_BSS), RT_RCDATA);
	if (res == NULL) {
		uprintf("Unable to locate ldlinux.bss resource: %s\n", WindowsErrorString());
		goto out;
	}
	res_handle = LoadResource(NULL, res);
	if (res_handle == NULL) {
		uprintf("Unable to load ldlinux.bss resource: %s\n", WindowsErrorString());
		goto out;
	}
	syslinux_bootsect = (unsigned char*)LockResource(res_handle);
	syslinux_bootsect_len = SizeofResource(NULL, res);

	/* Create ldlinux.sys file */
	f_handle = CreateFileA(ldlinux_name, GENERIC_READ | GENERIC_WRITE,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, CREATE_ALWAYS,
			  FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM |
			  FILE_ATTRIBUTE_HIDDEN, NULL);

	if (f_handle == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create ldlinux.sys\n");
		goto out;
	}

	/* Write ldlinux.sys file */
	if (!WriteFile(f_handle, syslinux_ldlinux, syslinux_ldlinux_len,
		   &bytes_written, NULL) ||
		bytes_written != syslinux_ldlinux_len) {
		uprintf("Could not write ldlinux.sys\n");
		goto out;
	}
	if (!WriteFile(f_handle, syslinux_adv, 2 * ADV_SIZE,
		   &bytes_written, NULL) ||
		bytes_written != 2 * ADV_SIZE) {
		uprintf("Could not write ADV to ldlinux.sys\n");
		goto out;
	}

	uprintf("Succesfully wrote 'ldlinux.sys'\n");
	UpdateProgress(OP_DOS, -1.0f);

	/* Now flush the media */
	if (!FlushFileBuffers(f_handle)) {
		uprintf("FlushFileBuffers failed\n");
		goto out;
	}

	/* Reopen the volume (we already have a lock) */
	d_handle = GetDriveHandle(num, (char*)drive_name, TRUE, FALSE);
	if (d_handle == INVALID_HANDLE_VALUE) {
		uprintf("Could open volume for syslinux operation\n");
		goto out;
	}

	/* Map the file (is there a better way to do this?) */
	ldlinux_sectors = (syslinux_ldlinux_len + 2 * ADV_SIZE + SECTOR_SIZE - 1) >> SECTOR_SHIFT;
	sectors = (libfat_sector_t*) calloc(ldlinux_sectors, sizeof *sectors);
	fs = libfat_open(libfat_readfile, (intptr_t) d_handle);
	ldlinux_cluster = libfat_searchdir(fs, 0, "LDLINUX SYS", NULL);
	secp = sectors;
	nsectors = 0;
	s = libfat_clustertosector(fs, ldlinux_cluster);
	while (s && nsectors < ldlinux_sectors) {
		*secp++ = s;
		nsectors++;
		s = libfat_nextsector(fs, s);
	}
	libfat_close(fs);

	/* Patch ldlinux.sys and the boot sector */
	syslinux_patch(sectors, nsectors, 0, 0, NULL, NULL);

	/* Rewrite the file */
	if (SetFilePointer(f_handle, 0, NULL, FILE_BEGIN) != 0 ||
		!WriteFile(f_handle, syslinux_ldlinux, syslinux_ldlinux_len,
			   &bytes_written, NULL)
		|| bytes_written != syslinux_ldlinux_len) {
		uprintf("Could not write ldlinux.sys: %s\n", WindowsErrorString());
		goto out;
	}

	/* Close file */
	safe_closehandle(f_handle);

	/* Read existing FAT data into boot sector */
	if (SetFilePointer(d_handle, 0, NULL, FILE_BEGIN) != 0 ||
		!ReadFile(d_handle, sectbuf, SECTOR_SIZE,
			   &bytes_read, NULL)
		|| bytes_read != SECTOR_SIZE) {
		uprintf("Could not read boot record: %s\n", WindowsErrorString());
		goto out;
	}

	/* Make the syslinux boot sector */
	syslinux_make_bootsect(sectbuf);

	/* Write boot sector back */
	if (SetFilePointer(d_handle, 0, NULL, FILE_BEGIN) != 0 ||
		!WriteFile(d_handle, sectbuf, SECTOR_SIZE,
			   &bytes_written, NULL)
		|| bytes_written != SECTOR_SIZE) {
		uprintf("Could not write boot record: %s\n", WindowsErrorString());
		goto out;
	}

	uprintf("Succesfully wrote Syslinux boot record\n");
	UpdateProgress(OP_DOS, -1.0f);

	r = TRUE;

out:
	safe_free(sectors);
	safe_closehandle(d_handle);
	safe_closehandle(f_handle);
	return r;
}
