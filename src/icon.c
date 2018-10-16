/*
 * Rufus: The Reliable USB Formatting Utility
 * Extract icon from executable and set autorun.inf
 * Copyright © 2012-2016 Pete Batard <pete@akeo.ie>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"

#pragma pack(push)
#pragma pack(2)

typedef struct
{
	BYTE		bWidth;			// Width, in pixels, of the image
	BYTE		bHeight;		// Height, in pixels, of the image
	BYTE		bColorCount;	// Number of colors in image (0 if >=8bpp)
	BYTE		bReserved;		// Reserved ( must be 0)
	WORD		wPlanes;		// Color Planes
	WORD		wBitCount;		// Bits per pixel
	DWORD		dwBytesInRes;	// How many bytes in this resource?
	DWORD		dwImageOffset;	// Where in the file is this image?
} ICONDIRENTRY, *LPICONDIRENTRY;

typedef struct
{
	WORD			idReserved;		// Reserved (must be 0)
	WORD			idType;			// Resource Type (1 for icons)
	WORD			idCount;		// How many images?
	ICONDIRENTRY	idEntries[1];	// An entry for each image (idCount of 'em)
} ICONDIR, *LPICONDIR;

typedef struct
{
	BITMAPINFOHEADER	icHeader;		// DIB header
	RGBQUAD				icColors[1];	// Color table
	BYTE				icXOR[1];		// DIB bits for XOR mask
	BYTE				icAND[1];		// DIB bits for AND mask
} ICONIMAGE, *LPICONIMAGE;

typedef struct
{
	BYTE	bWidth;			// Width, in pixels, of the image
	BYTE	bHeight;		// Height, in pixels, of the image
	BYTE	bColorCount;	// Number of colors in image (0 if >=8bpp)
	BYTE	bReserved;		// Reserved
	WORD	wPlanes;		// Color Planes
	WORD	wBitCount;		// Bits per pixel
	DWORD	dwBytesInRes;	// how many bytes in this resource?
	WORD	nID;			// the ID
} GRPICONDIRENTRY, *LPGRPICONDIRENTRY;

typedef struct
{
	WORD			idReserved;		// Reserved (must be 0)
	WORD			idType;			// Resource type (1 for icons)
	WORD			idCount;		// How many images?
	GRPICONDIRENTRY	idEntries[1];	// The entries for each image
} GRPICONDIR, *LPGRPICONDIR;

#pragma pack(pop)

/*
 * Extract an icon set from the exe and save it as .ico
 */
static BOOL SaveIcon(const char* filename)
{
	HGLOBAL res_handle;
	HRSRC res;
	WORD i;
	BYTE* res_data;
	DWORD res_size, Size, offset;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	BOOL r = FALSE;
	GRPICONDIR* icondir;

	icondir = (GRPICONDIR*)GetResource(hMainInstance, MAKEINTRESOURCEA(IDI_ICON), _RT_GROUP_ICON, "icon", &res_size, FALSE);

	hFile = CreateFileA(filename, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		uprintf("Unable to create icon '%s': %s.", filename, WindowsErrorString());
		goto out;
	}

	// Write .ico header
	if (!WriteFileWithRetry(hFile, icondir, 3*sizeof(WORD), &Size, WRITE_RETRIES)) {
		uprintf("Could not write icon header: %s.", WindowsErrorString());
		goto out;
	}

	// Write icon data
	offset = 3*sizeof(WORD) + icondir->idCount*sizeof(ICONDIRENTRY);
	for (i=0; i<icondir->idCount; i++) {
		// Write the common part of ICONDIRENTRY
		if (!WriteFileWithRetry(hFile, &icondir->idEntries[i], sizeof(GRPICONDIRENTRY)-sizeof(WORD), &Size, WRITE_RETRIES)) {
			uprintf("Could not write ICONDIRENTRY[%d]: %s.", i, WindowsErrorString());
			goto out;
		}
		res = FindResourceA(hMainInstance, MAKEINTRESOURCEA(icondir->idEntries[i].nID), _RT_ICON);
		// Write the DWORD offset
		if (!WriteFileWithRetry(hFile, &offset, sizeof(offset), &Size, WRITE_RETRIES)) {
			uprintf("Could not write ICONDIRENTRY[%d] offset: %s.", i, WindowsErrorString());
			goto out;
		}
		offset += SizeofResource(NULL, res);
	}
	for (i=0; i<icondir->idCount; i++) {
		// Write icon data
		res = FindResourceA(hMainInstance, MAKEINTRESOURCEA(icondir->idEntries[i].nID), _RT_ICON);
		res_handle = LoadResource(NULL, res);
		res_data = (BYTE*)LockResource(res_handle);
		res_size = SizeofResource(NULL, res);
		if (!WriteFileWithRetry(hFile, res_data, res_size, &Size, WRITE_RETRIES)) {
			uprintf("Could not write icon data #%d: %s.", i, WindowsErrorString());
			goto out;
		}
	}
	uprintf("Created: %s", filename);
	r = TRUE;

out:
	safe_closehandle(hFile);
	return r;
}

/*
 * Create an autorun.inf, if none exists
 * We use this to set the icon as well as labels that are longer than 11/32 chars or,
 * in the case of FAT, contain non-English characters
 */
BOOL SetAutorun(const char* path)
{
	FILE* fd;
	char filename[64];
	wchar_t wlabel[128], wRufusVersion[32];

	static_sprintf(filename, "%sautorun.inf", path);
	fd = fopen(filename, "r");	// If there's an existing autorun, don't overwrite
	if (fd != NULL) {
		uprintf("%s already exists - keeping it", filename);
		fclose(fd);
		return FALSE;
	}
	// No "/autorun.inf" => create a new one in UTF-16 LE mode
	fd = fopen(filename, "w, ccs=UTF-16LE");
	if (fd == NULL) {
		uprintf("Unable to create %s", filename);
		uprintf("NOTE: This may be caused by a poorly designed security solution. See https://goo.gl/QTobxX.");
		return FALSE;
	}

	GetWindowTextW(hLabel, wlabel, ARRAYSIZE(wlabel));
	GetWindowTextW(hMainDialog, wRufusVersion, ARRAYSIZE(wRufusVersion));
	fwprintf(fd, L"; Created by %s\n; " LTEXT(RUFUS_URL) L"\n", wRufusVersion);
	fwprintf(fd, L"[autorun]\nicon  = autorun.ico\nlabel = %s\n", wlabel);
	fclose(fd);
	uprintf("Created: %s", filename);

	// .inf -> .ico
	filename[strlen(filename)-1] = 'o';
	filename[strlen(filename)-2] = 'c';
	return SaveIcon(filename);
}
