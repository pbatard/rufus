/*
 * Rufus: The Resourceful USB Formatting Utility
 * Formatting function calls
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
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <process.h>
// #include <ctype.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "file.h"
#include "format.h"

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	int task_number = 0;

	if (IS_ERROR(FormatStatus))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)pData;
		PostMessage(hMainDialog, UM_FORMAT_PROGRESS, (WPARAM)*percent, (LPARAM)0);
		uprintf("%d percent completed.\n", *percent);
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		uprintf("Format task %d/? completed.\n", ++task_number);
		break;
	case FCC_DONE:
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:	// We get this message when formatting Small FAT16
		// pData Seems to be a struct with at least one (32 BIT!!!) string pointer to the size in MB
		uprintf("Done with that sort of things: Action=%d pData=%0p\n", Action, pData);
		DumpBufferHex(pData, 8);
		uprintf("Volume size: %s MB\n", (char*)(LONG_PTR)(*(ULONG32*)pData));
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INCOMPATIBLE_FS;
		break;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
		break;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_PROTECT;
		break;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_DEVICE_IN_USE;
		break;
	case FCC_CANT_QUICK_FORMAT:
		uprintf("Cannot quick format this volume\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANT_QUICK_FORMAT;
		break;
	case FCC_BAD_LABEL:
		uprintf("Bad label\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_LABEL_TOO_LONG;
		break;
	case FCC_OUTPUT:
		uprintf("%s\n", ((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_CLUSTER_SIZE_TOO_BIG:
	case FCC_CLUSTER_SIZE_TOO_SMALL:
		uprintf("Unsupported cluster size\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INVALID_CLUSTER_SIZE;
		break;
	case FCC_VOLUME_TOO_BIG:
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too %s\n", FCC_VOLUME_TOO_BIG?"big":"small");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INVALID_VOLUME_SIZE;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_MEDIA_IN_DRIVE;
		break;
	default:
		uprintf("FormatExCallback: received unhandled command %X\n", Command);
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_SUPPORTED;
		break;
	}
	return (!IS_ERROR(FormatStatus));
}

/*
 * Call on fmifs.dll's FormatEx() to format the drive
 */
static BOOL FormatDrive(char DriveLetter)
{
	BOOL r = FALSE;
	PF_DECL(FormatEx);
	WCHAR wDriveRoot[] = L"?:\\";
	WCHAR wFSType[32];
	WCHAR wLabel[128];
	size_t i;

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintStatus("Formatting...");
	PF_INIT_OR_OUT(FormatEx, fmifs);

	// TODO: properly set MediaType
	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			uprintf("removed %d\n", i);
			wFSType[i] = 0;
			break;
		}
	}
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	uprintf("Using cluster size: %d bytes\n", ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)));
	pfFormatEx(wDriveRoot, RemovableMedia, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), (ULONG)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)),
		FormatExCallback);
	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.\n");
		r = TRUE;
	}

out:
	return r;
}

static BOOL AnalyzeMBR(HANDLE hPhysicalDrive)
{
	FILE fake_fd;

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	// TODO: Apply this detection before partitioning
	// TODO: since we detect all these, might as well give some MBR choice to the user?
	if (is_br(&fake_fd)) {
		uprintf("Drive has an x86 boot sector\n");
	} else{
		uprintf("Drive is missing an x86 boot sector!\n");
		return FALSE;
	}
	// TODO: Add/Eliminate FAT12?
	if (is_fat_16_br(&fake_fd) || is_fat_32_br(&fake_fd)) {
		if (entire_fat_16_br_matches(&fake_fd)) {
			uprintf("Exact FAT16 DOS boot record match\n");
		} else if (entire_fat_16_fd_br_matches(&fake_fd)) {
			uprintf("Exact FAT16 FreeDOS boot record match\n");
		} else if (entire_fat_32_br_matches(&fake_fd)) {
			uprintf("Exact FAT32 DOS boot record match\n");
		} else if (entire_fat_32_nt_br_matches(&fake_fd)) {
			uprintf("Exact FAT32 NT boot record match\n");
		} else if (entire_fat_32_fd_br_matches(&fake_fd)) {
			uprintf("Exactly FAT32 FreeDOS boot record match\n");
		} else {
			uprintf("Unknown FAT16 or FAT32 boot record\n");
		}
	} else if (is_dos_mbr(&fake_fd)) {
		uprintf("Microsoft DOS/NT/95A master boot record match\n");
	} else if (is_dos_f2_mbr(&fake_fd)) {
		uprintf("Microsoft DOS/NT/95A master boot record with the undocumented\n");
		uprintf("F2 instruction match\n");
	} else if (is_95b_mbr(&fake_fd)) {
		uprintf("Microsoft 95B/98/98SE/ME master boot record match\n");
	} else if (is_2000_mbr(&fake_fd)) {
		uprintf("Microsoft 2000/XP/2003 master boot record match\n");
	} else if (is_vista_mbr(&fake_fd)) {
		uprintf("Microsoft Vista master boot record match\n");
	} else if (is_win7_mbr(&fake_fd)) {
		uprintf("Microsoft 7 master boot record match\n");
	} else if (is_zero_mbr(&fake_fd)) {
		uprintf("Zeroed non-bootable master boot record match\n");
	} else {
		uprintf("Unknown boot record\n");
	}
	return TRUE;
}

/*
 * Process the MBR
 */
static BOOL ProcessMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x200 + SecSize -1) / SecSize;
	FILE fake_fd;

	if (!AnalyzeMBR(hPhysicalDrive)) return FALSE;

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	// TODO: something else for bootable GPT
	buf = (unsigned char*)malloc(SecSize * nSecs);
	if (buf == NULL) {
		uprintf("Could not allocate memory for MBR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hPhysicalDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize)) {
		uprintf("Could not read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}
//	DumpBufferHex(buf, 0x200);
	switch (ComboBox_GetCurSel(hFileSystem)) {
	// TODO: check for 0x06 & 0x0b?
	case FS_FAT16:
		buf[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		buf[0x1c2] = 0x0c;
		break;
	}
	if (IsChecked(IDC_DOSSTARTUP)) {
		buf[0x1be] = 0x80;		// Set first partition bootable
	}

	if (!write_sectors(hPhysicalDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize*nSecs)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	r = write_95b_mbr(&fake_fd);

	if (!read_sectors(hPhysicalDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize)) {
		uprintf("Could not re-read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}
	DumpBufferHex(buf, 0x200);

out:
	safe_free(buf);
	return r;
}

static BOOL ProcessFS_BR(HANDLE hLogicalVolume)
{
	BOOL r = FALSE;
	unsigned char* buf = NULL;
	FILE fake_fd;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x400 + SecSize -1) / SecSize;

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	write_fat_32_br(&fake_fd, 0);

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	// TODO: something else for bootable GPT
	buf = (unsigned char*)malloc(SecSize * nSecs);
	if (buf == NULL) {
		uprintf("Could not allocate memory for FS BR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hLogicalVolume, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize*nSecs)) {
		uprintf("Could not read FS BR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}
	uprintf("FS_BR:\n");
	DumpBufferHex(buf, 0x400);

out:
	safe_free(buf);
	return r;
}

/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa364562%28v=vs.85%29.aspx
   Dismounting a volume is useful when a volume needs to disappear for a while. For
   example, an application that changes a volume file system from the FAT file system
   to the NTFS file system might use the following procedure.

   To change a volume file system

    Open a volume.
    Lock the volume.
    Format the volume.
    Dismount the volume.
    Unlock the volume.
    Close the volume handle.

   A dismounting operation removes the volume from the FAT file system awareness.
   When the operating system mounts the volume, it appears as an NTFS file system volume.
*/

/*
 * Standalone thread for the formatting operation
 */
void __cdecl FormatThread(void* param)
{
	DWORD num = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	char drive_name[] = "?:";
	int i;
//	DWORD size;

	hPhysicalDrive = GetDriveHandle(num, NULL, TRUE, TRUE);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	// At this stage with have both a handle and a lock to the physical drive

	if (!CreatePartition(hPhysicalDrive)) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}

	// Make sure we can access the volume again before trying to format it
	for (i=0; i<10; i++) {
		Sleep(500);
		hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, TRUE);
		if (hLogicalVolume != INVALID_HANDLE_VALUE) {
			break;
		}
	}
	if (i >= 10) {
		uprintf("Could not access volume after partitioning\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	// Handle needs to be closed for FormatEx to be happy - we keep a lock though
	safe_closehandle(hLogicalVolume);

	if (!FormatDrive(drive_name[0])) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: 0x%08X\n", FormatStatus);
		goto out;
	}

	// TODO: Enable compression on NTFS
	// TODO: optionally disable indexing on NTFS
	// TODO: use progress bar during MBR/FSBR/MSDOS copy

	// Ideally we would lock, FSCTL_DISMOUNT_VOLUME, unlock and close our volume
	// handle, but some explorer versions have problems with volumes disappear
// #define VOL_DISMOUNT
#ifdef VOL_DISMOUNT
	// Dismount the volume
	hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, TRUE);
	if (hLogicalVolume == INVALID_HANDLE_VALUE) {
		uprintf("Could not open the volume for dismount\n");
		goto out;
	}

	if (!DeviceIoControl(hLogicalVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &size, NULL)) {
		uprintf("Could not dismount volume\n");
		goto out;
	}
#endif
	PrintStatus("Writing master boot record...\n");
	if (!ProcessMBR(hPhysicalDrive)) {
		// Errorcode has already been set
		goto out;
	}

#ifdef VOL_DISMOUNT
	safe_unlockclose(hLogicalVolume);
//	Sleep(10000);
	hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, FALSE);
	if (hLogicalVolume == INVALID_HANDLE_VALUE) {
		uprintf("Could not re-mount volume\n");
		goto out;
	}
#endif

	if (IsChecked(IDC_DOSSTARTUP)) {
		hLogicalVolume = GetDriveHandle(num, drive_name, TRUE, FALSE);
		if (hLogicalVolume == INVALID_HANDLE_VALUE) {
			uprintf("Could not re-mount volume\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
			goto out;
		}
		PrintStatus("Writing filesystem boot record...\n");
		if (!ProcessFS_BR(hLogicalVolume)) {
			// Errorcode has already been set
			goto out;
		}
		PrintStatus("Copying MS-DOS files...\n");
		if (!ExtractMSDOS(drive_name)) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
			goto out;
		}
	}

out:
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	_endthread();
}
