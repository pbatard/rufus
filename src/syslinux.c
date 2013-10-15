/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2003 Lars Munch Christensen - All Rights Reserved
 *   Copyright 1998-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2012-2013 Pete Batard
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
#include <windowsx.h>
#include <stdio.h>
#include <malloc.h>
#include <ctype.h>

#include "rufus.h"
#include "resource.h"
#include "localization.h"

#include "syslinux.h"
#include "syslxfs.h"
#include "libfat.h"
#include "setadv.h"

unsigned char* syslinux_ldlinux = NULL;
DWORD syslinux_ldlinux_len;
unsigned char* syslinux_bootsect = NULL;
DWORD syslinux_bootsect_len;

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
		return 0;
	}

	return (int)secsize;
}

/*
 * Extract the ldlinux.sys and ldlinux.bss from resources,
 * then patch and install them
 */
BOOL InstallSyslinux(DWORD drive_index, char drive_letter)
{
	HANDLE f_handle = INVALID_HANDLE_VALUE;
	HANDLE d_handle = INVALID_HANDLE_VALUE;
	DWORD bytes_read;
	DWORD bytes_written;
	BOOL r = FALSE;
	FILE* fd;

	static unsigned char sectbuf[SECTOR_SIZE];
	static LPSTR resource[2][2] = {
		{ MAKEINTRESOURCEA(IDR_SL_LDLINUX_V4_SYS),  MAKEINTRESOURCEA(IDR_SL_LDLINUX_V4_BSS) },
		{ MAKEINTRESOURCEA(IDR_SL_LDLINUX_V5_SYS),  MAKEINTRESOURCEA(IDR_SL_LDLINUX_V5_BSS) } };
	static char ldlinux_path[] = "?:\\ldlinux.sys";
	static char* ldlinux_sys = &ldlinux_path[3];
	const char* ldlinux_c32 = "ldlinux.c32";
	struct libfat_filesystem *fs;
	libfat_sector_t s, *secp;
	libfat_sector_t *sectors = NULL;
	int ldlinux_sectors;
	uint32_t ldlinux_cluster;
	int nsectors;
	int dt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	BOOL use_v5 = (dt == DT_SYSLINUX_V5) || ((dt == DT_ISO) && (iso_report.has_syslinux_v5));

	PrintStatus(0, TRUE, lmprintf(MSG_234, use_v5?5:4));

	ldlinux_path[0] = drive_letter;

	/* Initialize the ADV -- this should be smarter */
	syslinux_reset_adv(syslinux_adv);

	/* Access a copy of the ldlinux.sys & ldlinux.bss resources */
	syslinux_ldlinux = GetResource(hMainInstance, resource[use_v5?1:0][0],
		_RT_RCDATA, ldlinux_sys, &syslinux_ldlinux_len, TRUE);
	syslinux_bootsect = GetResource(hMainInstance, resource[use_v5?1:0][1],
		_RT_RCDATA, "ldlinux.bss", &syslinux_bootsect_len, TRUE);
	if ((syslinux_ldlinux == NULL) || (syslinux_bootsect == NULL)) {
		goto out;
	}

	/* Create ldlinux.sys file */
	f_handle = CreateFileA(ldlinux_path, GENERIC_READ | GENERIC_WRITE,
			  FILE_SHARE_READ | FILE_SHARE_WRITE,
			  NULL, CREATE_ALWAYS,
			  FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM |
			  FILE_ATTRIBUTE_HIDDEN, NULL);

	if (f_handle == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create '%s'\n", ldlinux_sys);
		goto out;
	}

	/* Write ldlinux.sys file */
	if (!WriteFile(f_handle, syslinux_ldlinux, syslinux_ldlinux_len,
		   &bytes_written, NULL) ||
		bytes_written != syslinux_ldlinux_len) {
		uprintf("Could not write '%s'\n", ldlinux_sys);
		goto out;
	}
	if (!WriteFile(f_handle, syslinux_adv, 2 * ADV_SIZE,
		   &bytes_written, NULL) ||
		bytes_written != 2 * ADV_SIZE) {
		uprintf("Could not write ADV to '%s'\n", ldlinux_sys);
		goto out;
	}

	uprintf("Successfully wrote '%s'\n", ldlinux_sys);
	if (dt != DT_ISO)
		UpdateProgress(OP_DOS, -1.0f);

	/* Now flush the media */
	if (!FlushFileBuffers(f_handle)) {
		uprintf("FlushFileBuffers failed\n");
		goto out;
	}

	/* Reopen the volume (we already have a lock) */
	d_handle = GetLogicalHandle(drive_index, TRUE, FALSE);
	if (d_handle == INVALID_HANDLE_VALUE) {
		uprintf("Could open volume for Syslinux installation\n");
		goto out;
	}

	/* Map the file (is there a better way to do this?) */
	ldlinux_sectors = (syslinux_ldlinux_len + 2 * ADV_SIZE + SECTOR_SIZE - 1) >> SECTOR_SHIFT;
	sectors = (libfat_sector_t*) calloc(ldlinux_sectors, sizeof *sectors);
	if (sectors == NULL)
		goto out;
	fs = libfat_open(libfat_readfile, (intptr_t) d_handle);
	if (fs == NULL) {
		uprintf("Syslinux FAT access error\n");
		goto out;
	}
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
		uprintf("Could not write '%s': %s\n", ldlinux_sys, WindowsErrorString());
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
	syslinux_make_bootsect(sectbuf, VFAT);

	/* Write boot sector back */
	if (SetFilePointer(d_handle, 0, NULL, FILE_BEGIN) != 0 ||
		!WriteFile(d_handle, sectbuf, SECTOR_SIZE,
			   &bytes_written, NULL)
		|| bytes_written != SECTOR_SIZE) {
		uprintf("Could not write Syslinux boot record: %s\n", WindowsErrorString());
		goto out;
	}

	uprintf("Successfully wrote Syslinux boot record\n");

	if (dt == DT_SYSLINUX_V5) {
		fd = fopen(ldlinux_c32, "rb");
		if (fd == NULL) {
			uprintf("Caution: No '%s' was provided. The target will be missing a required Syslinux file!\n", ldlinux_c32);
		} else {
			fclose(fd);
			ldlinux_path[11] = 'c'; ldlinux_path[12] = '3'; ldlinux_path[13] = '2';
			if (CopyFileA(ldlinux_c32, ldlinux_path, TRUE)) {
				uprintf("Created '%s' (from local copy)", ldlinux_path);
			} else {
				uprintf("Failed to create '%s': %s\n", ldlinux_path, WindowsErrorString());
			}
		}
	}

	if (dt != DT_ISO)
		UpdateProgress(OP_DOS, -1.0f);

	r = TRUE;

out:
	safe_free(syslinux_ldlinux);
	safe_free(syslinux_bootsect);
	safe_free(sectors);
	safe_closehandle(d_handle);
	safe_closehandle(f_handle);
	return r;
}
