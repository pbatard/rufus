/*
 * Rufus: The Resourceful USB Formatting Utility
 * MS-DOS boot file extraction, from the FAT12 floppy image in diskcopy.dll
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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
#include "msdos.h"

static BYTE* DiskImage;
static size_t DiskImageSize;

/* Extract the file identified by FAT RootDir index 'entry' to 'path' */
static BOOL ExtractFAT(int entry, const char* path)
{
	FILE* fd;
	char filename[MAX_PATH];
	size_t i, pos;
	size_t filestart;
	size_t filesize;
	size_t FATFile = FAT12_ROOTDIR_OFFSET + entry*FAT12_ROOTDIR_ENTRY_SIZE;

	if ((path == NULL) || ((safe_strlen(path) + 14) > sizeof(filename))) {
		uprintf("invalid path supplied for MS-DOS FAT extraction\n");
		return FALSE;
	}
	strcpy(filename, path);
	pos = strlen(path);
	filename[pos++] = '\\';

	for(i=0; i<8; i++) {
		if (DiskImage[FATFile + i] == ' ')
			break;
		filename[pos++] = DiskImage[FATFile + i];
	}
	filename[pos++] = '.';
	for (i=8; i<11; i++) {
		if (DiskImage[FATFile + i] == ' ')
			break;
		filename[pos++] = DiskImage[FATFile + i];
	}
	filename[pos] = 0;
	GET_ULONG_LE(filesize, DiskImage, FATFile + FAT12_ROOTDIR_FILESIZE);
	GET_USHORT_LE(filestart, DiskImage, FATFile + FAT12_ROOTDIR_FIRSTCLUSTER);
	filestart += FAT12_CLUSTER_OFFSET;
	filestart *= FAT12_CLUSTER_SIZE;
	if ((filestart + filesize) > DiskImageSize) {
		uprintf("FAT File %s would be out of bounds\n", filename);
		return FALSE;
	}

	fd = fopen(filename, "wb");
	if (fd == NULL) {
		uprintf("Unable to create file '%s'.\n", filename);
		return FALSE;
	}

	if (fwrite(&DiskImage[filestart], 1, filesize, fd) != filesize) {
		uprintf("Couldn't write file '%s'.\n", filename);
		fclose(fd);
		return FALSE;
	}
	fclose(fd);

	uprintf("Succesfully wrote '%s' (%d bytes)\n", filename, filesize);

	// TODO: MSDOS.SYS and IO.SYS should have 'rahs' attributes
	return TRUE;
}

/* Extract the MS-DOS files contained in the FAT12 1.4MB floppy
   image included as resource "BINFILE" in diskcopy.dll */
BOOL ExtractMSDOS(const char* path)
{
	char dllname[MAX_PATH] = "C:\\Windows\\System32";
	int i, j;
	HMODULE hDLL;
	HRSRC hDiskImage;

	// TODO: optionally extract some more, including "deleted" entries
	char* extractlist[] = {"MSDOS   SYS", "COMMAND COM", "IO      SYS"};

	GetSystemDirectoryA(dllname, sizeof(dllname));
	safe_strcat(dllname, sizeof(dllname), "\\diskcopy.dll");
	hDLL = LoadLibraryA(dllname);
	if (hDLL == NULL) {
		uprintf("Unable to open %s: %s\n", dllname, WindowsErrorString());
		return FALSE;
	}
	hDiskImage = FindResourceA(hDLL, MAKEINTRESOURCEA(1), "BINFILE");
	if (hDiskImage == NULL) {
		uprintf("Unable to locate disk image in %s: %s\n", dllname, WindowsErrorString());
		FreeLibrary(hDLL);
		return FALSE;
	}
	DiskImage = (BYTE*)LockResource(LoadResource(hDLL, hDiskImage));
	if (DiskImage == NULL) {
		uprintf("Unable to access disk image in %s: %s\n", dllname, WindowsErrorString());
		FreeLibrary(hDLL);
		return FALSE;
	}
	DiskImageSize = (size_t)SizeofResource(hDLL, hDiskImage);
	// Sanity check
	if (DiskImageSize < 700*1024) {
		uprintf("MS-DOS disk image is too small (%d bytes)\n", dllname, DiskImageSize);
		FreeLibrary(hDLL);
		return FALSE;
	}

	for (i=0; i<FAT12_ROOTDIR_NB_ENTRIES; i++) {
		if (DiskImage[FAT12_ROOTDIR_OFFSET + i*FAT12_ROOTDIR_ENTRY_SIZE] == FAT12_DELETED_ENTRY)
			continue;
		for (j=0; j<ARRAYSIZE(extractlist); j++) {
			if (memcmp(extractlist[j], &DiskImage[FAT12_ROOTDIR_OFFSET + i*FAT12_ROOTDIR_ENTRY_SIZE], 8+3) == 0) {
				ExtractFAT(i, path);
			}
		}
	}

	FreeLibrary(hDLL);
	return TRUE;
}
