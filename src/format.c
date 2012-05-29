/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
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
#include <stddef.h>
#include <ctype.h>
#include <locale.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "partition_info.h"
#include "file.h"
#include "format.h"
#include "badblocks.h"

/*
 * Globals
 */
DWORD FormatStatus;
badblocks_report report;
static float format_percent = 0.0f;
static int task_number = 0;
/* Number of steps for each FS for FCC_STRUCTURE_PROGRESS */
const int nb_steps[FS_MAX] = { 5, 5, 12, 10 };
static int fs_index = 0;

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	if (IS_ERROR(FormatStatus))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)pData;
		PrintStatus(0, FALSE, "Formatting: %d%% completed.", *percent);
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		PrintStatus(0, TRUE, "Creating file system: Task %d/%d completed.", ++task_number, nb_steps[fs_index]);
		format_percent += 100.0f / (1.0f * nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, format_percent);
		break;
	case FCC_DONE:
		PrintStatus(0, TRUE, "Creating file system: Task %d/%d completed.", nb_steps[fs_index], nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:	// We get this message when formatting Small FAT16
		// pData Seems to be a struct with at least one (32 BIT!!!) string pointer to the size in MB
		uprintf("Done with that sort of thing: Action=%d pData=%0p\n", Action, pData);
		// /!\ THE FOLLOWING ONLY WORKS ON VISTA OR LATER - DO NOT ENABLE ON XP!
		// DumpBufferHex(pData, 8);
		// uprintf("Volume size: %s MB\n", (char*)(LONG_PTR)(*(ULONG32*)pData));
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INCOMPATIBLE_FS);
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
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_QUICK_FORMAT);
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
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_CLUSTER_SIZE);
		break;
	case FCC_VOLUME_TOO_BIG:
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too %s\n", FCC_VOLUME_TOO_BIG?"big":"small");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_VOLUME_SIZE);
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
 * Chkdsk callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall ChkdskCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	if (IS_ERROR(FormatStatus))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
	case FCC_CHECKDISK_PROGRESS:
		percent = (DWORD*)pData;
		PrintStatus(0, FALSE, "NTFS Fixup: %d%% completed.", *percent);
		break;
	case FCC_DONE:
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while checking disk.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_UNKNOWN1A:
	case FCC_DONE_WITH_STRUCTURE:
		// Silence these specific calls
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INCOMPATIBLE_FS);
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
	case FCC_OUTPUT:
		uprintf("%s\n", ((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_MEDIA_IN_DRIVE;
		break;
	default:
		uprintf("ChkdskExCallback: received unhandled command %X\n", Command);
		// Assume the command isn't an error
		break;
	}
	return (!IS_ERROR(FormatStatus));
}

/*
 * Converts an UTF-16 label to a valid FAT/NTFS one
 */
static void ToValidLabel(WCHAR* name, BOOL bFAT)
{
	size_t i, j, k;
	BOOL found;
	WCHAR unauthorized[] = L"*?.,;:/\\|+=<>[]";
	WCHAR to_underscore[] = L"\t";

	if (name == NULL)
		return;

	for (i=0, k=0; i<wcslen(name); i++) {
		if (bFAT) {	// NTFS does allows all the FAT unauthorized above
			found = FALSE;
			for (j=0; j<wcslen(unauthorized); j++) {
				if (name[i] == unauthorized[j]) {
					found = TRUE; break;
				}
			}
			// A FAT label that contains extended chars will be rejected
			if (name[i] >= 0x80) {
				name[k++] = '_';
				found = TRUE;
			}
			if (found) continue;
		}
		found = FALSE;
		for (j=0; j<wcslen(to_underscore); j++) {
			if (name[i] == to_underscore[j]) {
				name[k++] = '_';
				found = TRUE; break;
			}
		}
		if (found) continue;
		name[k++] = bFAT?toupper(name[i]):name[i];
	}
	name[k] = 0;
	if (bFAT) {
		name[11] = 0;
		for (i=0, j=0; name[i]!=0; i++)
			if (name[i] == '_') j++;
		if (i<2*j) {
			// If the final label is mostly underscore, use the proposed label
			uprintf("FAT label is mostly undercores. Using '%s' label instead.\n", SelectedDrive.proposed_label);
			for(i=0; SelectedDrive.proposed_label[i]!=0; i++)
				name[i] = SelectedDrive.proposed_label[i];
			name[i] = 0;
		}
	} else {
		name[32] = 0;
	}

	// Needed for disk by label isolinux.cfg workaround
	wchar_to_utf8_no_alloc(name, iso_report.usb_label, sizeof(iso_report.usb_label));
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
	WCHAR wLabel[64];
	size_t i;
	char* locale;

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintStatus(0, TRUE, "Formatting...");
	// LoadLibrary("fmifs.dll") appears to changes the locale, which can lead to
	// problems with tolower(). Make sure we restore the locale. For more details,
	// see http://comments.gmane.org/gmane.comp.gnu.mingw.user/39300
	locale = setlocale(LC_ALL, NULL);
	PF_INIT_OR_OUT(FormatEx, fmifs);
	setlocale(LC_ALL, locale);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	// Make sure the label is valid
	ToValidLabel(wLabel, (wFSType[0] == 'F') && (wFSType[1] == 'A') && (wFSType[2] == 'T'));
	uprintf("Using cluster size: %d bytes\n", ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)));
	format_percent = 0.0f;
	task_number = 0;
	fs_index = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	pfFormatEx(wDriveRoot, SelectedDrive.Geometry.MediaType, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), (ULONG)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)),
		FormatExCallback);
	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.\n");
		r = TRUE;
	}

out:
	return r;
}

/*
 * Call on fmifs.dll's Chkdsk() to fixup the filesystem
 */
static BOOL CheckDisk(char DriveLetter)
{
	BOOL r = FALSE;
	PF_DECL(Chkdsk);
	WCHAR wDriveRoot[] = L"?:\\";
	WCHAR wFSType[32];
	size_t i;

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintStatus(0, TRUE, "NTFS Fixup (Checkdisk)...");

	PF_INIT_OR_OUT(Chkdsk, fmifs);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}

	pfChkdsk(wDriveRoot, wFSType, FALSE, FALSE, FALSE, FALSE, NULL, NULL, ChkdskCallback);
	if (!IS_ERROR(FormatStatus)) {
		uprintf("NTFS Fixup completed.\n");
		r = TRUE;
	}

out:
	return r;
}

static BOOL AnalyzeMBR(HANDLE hPhysicalDrive)
{
	FILE fake_fd = { 0 };

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	if (!is_br(&fake_fd)) {
		uprintf("Drive does not have an x86 master boot record\n");
		return FALSE;
	}
	if (is_dos_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft DOS/NT/95A master boot record\n");
	} else if (is_dos_f2_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft DOS/NT/95A master boot record "
			"with the undocumented F2 instruction\n");
	} else if (is_95b_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft 95B/98/98SE/ME master boot record\n");
	} else if (is_2000_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft 2000/XP/2003 master boot record\n");
	} else if (is_vista_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft Vista master boot record\n");
	} else if (is_win7_mbr(&fake_fd)) {
		uprintf("Drive has a Microsoft 7 master boot record\n");
	} else if (is_zero_mbr(&fake_fd)) {
		uprintf("Drive has a zeroed non-bootable master boot record\n");
	} else {
		uprintf("Unknown boot record\n");
	}
	return TRUE;
}

static BOOL AnalyzePBR(HANDLE hLogicalVolume)
{
	FILE fake_fd = { 0 };

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	if (!is_br(&fake_fd)) {
		uprintf("Volume does not have an x86 partition boot record\n");
		return FALSE;
	}
	if (is_fat_16_br(&fake_fd) || is_fat_32_br(&fake_fd)) {
		if (entire_fat_16_br_matches(&fake_fd)) {
			uprintf("Drive has a FAT16 DOS partition boot record\n");
		} else if (entire_fat_16_fd_br_matches(&fake_fd)) {
			uprintf("Drive has a FAT16 FreeDOS partition boot record\n");
		} else if (entire_fat_32_br_matches(&fake_fd)) {
			uprintf("Drive has a FAT32 DOS partition boot record\n");
		} else if (entire_fat_32_nt_br_matches(&fake_fd)) {
			uprintf("Drive has a FAT32 NT partition boot record\n");
		} else if (entire_fat_32_fd_br_matches(&fake_fd)) {
			uprintf("Drive has a FAT32 FreeDOS partition boot record\n");
		} else {
			uprintf("Drive has a unknown FAT16 or FAT32 partition boot record\n");
		}
	}
	return TRUE;
}

static BOOL ClearMBR(HANDLE hPhysicalDrive)
{
	FILE fake_fd = { 0 };

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	return clear_mbr(&fake_fd);
}

/*
 * Our own MBR, not in ms-sys
 */
BOOL WriteRufusMBR(FILE *fp)
{
	HGLOBAL res_handle;
	HRSRC res;
	unsigned char aucRef[] = {0x55, 0xAA};
	unsigned char* rufus_mbr;

	// TODO: Will we need to edit the disk ID according to UI selection in the MBR as well?
	res = FindResource(hMainInstance, MAKEINTRESOURCE(IDR_BR_MBR_BIN), RT_RCDATA);
	if (res == NULL) {
		uprintf("Unable to locate mbr.bin resource: %s\n", WindowsErrorString());
		return FALSE;
	}
	res_handle = LoadResource(NULL, res);
	if (res_handle == NULL) {
		uprintf("Unable to load mbr.bin resource: %s\n", WindowsErrorString());
		return FALSE;
	}
	rufus_mbr = (unsigned char*)LockResource(res_handle);

	return
		write_data(fp, 0x0, rufus_mbr, 0x1b8) &&
		write_data(fp, 0x1fe, aucRef, sizeof(aucRef));
}

/*
 * Process the Master Boot Record
 */
static BOOL WriteMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	int dt, fs;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x200 + SecSize -1) / SecSize;
	FILE fake_fd = { 0 };

	if (!AnalyzeMBR(hPhysicalDrive)) return FALSE;

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	// TODO: Something else for bootable GPT
	buf = (unsigned char*)malloc(SecSize * nSecs);
	if (buf == NULL) {
		uprintf("Could not allocate memory for MBR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hPhysicalDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf)) {
		uprintf("Could not read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (buf[0x1c2] == 0x0e) {
			uprintf("Partition is already FAT16 LBA...\n");
		} else if ((buf[0x1c2] != 0x04) && (buf[0x1c2] != 0x06)) {
			uprintf("Warning: converting a non FAT16 partition to FAT16 LBA: FS type=0x%02x\n", buf[0x1c2]);
		}
		buf[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		if (buf[0x1c2] == 0x0c) {
			uprintf("Partition is already FAT32 LBA...\n");
		} else if (buf[0x1c2] != 0x0b) {
			uprintf("Warning: converting a non FAT32 partition to FAT32 LBA: FS type=0x%02x\n", buf[0x1c2]);
		}
		buf[0x1c2] = 0x0c;
		break;
	}
	if (IsChecked(IDC_DOS)) {
		// Set first partition bootable - masquerade as per the DiskID selected
		buf[0x1be] = (IsChecked(IDC_RUFUS_MBR))?(BYTE)ComboBox_GetItemData(hDiskID, ComboBox_GetCurSel(hDiskID)):0x80;
		uprintf("Set bootable USB partition as 0x%02X\n", buf[0x1be]);
	}

	if (!write_sectors(hPhysicalDrive, SecSize, 0, nSecs, buf)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	dt = (int)ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
	if ( (dt == DT_SYSLINUX) || ((dt == DT_ISO) && ((fs == FS_FAT16) || (fs == FS_FAT32))) ) {
		r = write_syslinux_mbr(&fake_fd);
	} else {
		if ((IS_WINPE(iso_report.winpe) && !iso_report.uses_minint) || (IsChecked(IDC_RUFUS_MBR))) {
			uprintf("Using Rufus bootable USB selection MBR\n");
			r = WriteRufusMBR(&fake_fd);
		} else {
			uprintf("Using Windows 7 MBR\n");
			r = write_win7_mbr(&fake_fd);
		}
	}

out:
	safe_free(buf);
	return r;
}

/*
 * Process the Partition Boot Record
 */
static BOOL WritePBR(HANDLE hLogicalVolume)
{
	int i;
	FILE fake_fd = { 0 };
	BOOL bFreeDOS = (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_FREEDOS);

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (!is_fat_16_fs(&fake_fd)) {
			uprintf("New volume does not have a FAT16 boot sector\n");
			break;
		}
		uprintf("Confirmed new volume has a FAT16 boot sector\n");
		if (bFreeDOS) {
			if (!write_fat_16_fd_br(&fake_fd, 0)) break;
		} else {
			if (!write_fat_16_br(&fake_fd, 0)) break;
		}
		// Disk Drive ID needs to be corrected on XP
		if (!write_partition_physical_disk_drive_id_fat16(&fake_fd))
			break;
		return TRUE;
	case FS_FAT32:
		for (i=0; i<2; i++) {
			if (!is_fat_32_fs(&fake_fd)) {
				uprintf("New volume does not have a %s FAT32 boot sector\n", i?"secondary":"primary");
				break;
			}
			uprintf("Confirmed new volume has a %s FAT32 boot sector\n", i?"secondary":"primary");
			uprintf("Setting %s FAT32 boot sector for DOS boot...\n", i?"secondary":"primary");
			if (bFreeDOS) {
				if (!write_fat_32_fd_br(&fake_fd, 0)) break;
			} else if (!write_fat_32_br(&fake_fd, 0)) break;
			// Disk Drive ID needs to be corrected on XP
			if (!write_partition_physical_disk_drive_id_fat32(&fake_fd))
				break;
			fake_fd._cnt += 6 * (int)SelectedDrive.Geometry.BytesPerSector;
		}
		return TRUE;
	case FS_NTFS:
		if (!is_ntfs_fs(&fake_fd)) {
			uprintf("New volume does not have an NTFS boot sector\n");
			break;
		}
		uprintf("Confirmed new volume has an NTFS boot sector\n");
		if (!write_ntfs_br(&fake_fd)) break;
		// Note: NTFS requires a full remount after writing the PBR. We dismount when we lock
		// and also go through a forced remount, so that shouldn't be an issue.
		// But with NTFS, if you don't remount, you don't boot!
		return TRUE;
	default:
		uprintf("unsupported FS for FS BR processing\n");
		break;
	}
	FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
	return FALSE;
}

/*
 * Setup WinPE for bootable USB
 */
static BOOL SetupWinPE(char drive_letter)
{
	char src[64], dst[32];
	const char* basedir[] = { "i386", "minint" };
	const char* patch_str_org[] = { "\\minint\\txtsetup.sif", "\\minint\\system32\\" };
	const char* patch_str_rep[] = { "\\i386\\txtsetup.sif", "\\i386\\system32\\" };
	const char *win_nt_bt_org = "$win_nt$.~bt", *win_nt_bt_rep = "i386";
	const char *rdisk_zero = "rdisk(0)";
	char setupsrcdev[64];
	HANDLE handle = INVALID_HANDLE_VALUE;
	DWORD i, j, size, rw_size, index = 0;
	BOOL r = FALSE;
	char* buf = NULL;

	index = ((iso_report.winpe&WINPE_I386) == WINPE_I386)?0:1;
	// Allow other values than harddisk 1, as per user choice for disk ID
	safe_sprintf(setupsrcdev, sizeof(setupsrcdev),
		"SetupSourceDevice = \"\\device\\harddisk%d\\partition1\"", ComboBox_GetCurSel(hDiskID));
	// Copy of ntdetect.com in root
	safe_sprintf(src, sizeof(src), "%c:\\%s\\ntdetect.com", drive_letter, basedir[index]);
	safe_sprintf(dst, sizeof(dst), "%c:\\ntdetect.com", drive_letter);
	CopyFileA(src, dst, TRUE);
	if (!iso_report.uses_minint) {
		// Create a copy of txtsetup.sif, as we want to keep the i386 files unmodified
		safe_sprintf(src, sizeof(src), "%c:\\%s\\txtsetup.sif", drive_letter, basedir[index]);
		safe_sprintf(dst, sizeof(dst), "%c:\\txtsetup.sif", drive_letter);
		if (!CopyFileA(src, dst, TRUE)) {
			uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
		}
		if (insert_section_data(dst, "[SetupData]", setupsrcdev, FALSE) == NULL) {
			uprintf("Failed to add SetupSourceDevice in %s\n", dst);
			goto out;
		}
		uprintf("Succesfully added '%s' to %s\n", setupsrcdev, dst);
	}

	safe_sprintf(src, sizeof(src), "%c:\\%s\\setupldr.bin", drive_letter,  basedir[index]);
	safe_sprintf(dst, sizeof(dst), "%c:\\BOOTMGR", drive_letter);
	if (!CopyFileA(src, dst, TRUE)) {
		uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
	}

	// \minint with /minint option doesn't require further processing => return true
	// \minint and no \i386 without /minint is unclear => return error
	if (iso_report.winpe&WINPE_MININT) {
		if (iso_report.uses_minint) {
			uprintf("Detected \\minint directory with /minint option: nothing to patch\n");
			r = TRUE;
		} else if (!(iso_report.winpe&WINPE_I386)) {
			uprintf("Detected \\minint directory only but no /minint option: not sure what to do\n");
		}
		goto out;
	}

	// At this stage we only handle \i386
	handle = CreateFileA(dst, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open %s for patching: %s\n", dst, WindowsErrorString());
		goto out;
	}
	size = GetFileSize(handle, NULL);
	if (size == INVALID_FILE_SIZE) {
		uprintf("Could not get size for file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	buf = (char*)malloc(size);
	if (buf == NULL)
		goto out;
	if ((!ReadFile(handle, buf, size, &rw_size, NULL)) || (size != rw_size)) {
		uprintf("Could not read file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	SetFilePointer(handle, 0, NULL, FILE_BEGIN);

	// Patch setupldr.bin
	uprintf("Patching file %s\n", dst);
	// Remove CRC check for 32 bit part of setupldr.bin from Win2k3
	if ((size > 0x2061) && (buf[0x2060] == 0x74) && (buf[0x2061] == 0x03)) {
		buf[0x2060] = 0xeb;
		buf[0x2061] = 0x1a;
		uprintf("  0x00002060: 0x74 0x03 -> 0xEB 0x1A (disable Win2k3 CRC check)\n");
	}
	for (i=1; i<size-32; i++) {
		for (j=0; j<ARRAYSIZE(patch_str_org); j++) {
			if (safe_strnicmp(&buf[i], patch_str_org[j], strlen(patch_str_org[j])-1) == 0) {
				uprintf("  0x%08X: '%s' -> '%s'\n", i, &buf[i], patch_str_rep[j]);
				strcpy(&buf[i], patch_str_rep[j]);
				i += (DWORD)max(strlen(patch_str_org[j]), strlen(patch_str_rep[j]));	// in case org is a substring of rep
			}
		}
	}

	if (!iso_report.uses_minint) {
		// Additional setupldr.bin/bootmgr patching
		for (i=0; i<size-32; i++) {
			// rdisk(0) -> rdisk(#) disk masquerading
			// NB: only the first one seems to be needed
			if (safe_strnicmp(&buf[i], rdisk_zero, strlen(rdisk_zero)-1) == 0) {
				buf[i+6] = 0x20 + ComboBox_GetCurSel(hDiskID);
				uprintf("  0x%08X: '%s' -> 'rdisk(%c)'\n", i, rdisk_zero, buf[i+6]);
			}
			// $WIN_NT$_~BT -> i386
			if (safe_strnicmp(&buf[i], win_nt_bt_org, strlen(win_nt_bt_org)-1) == 0) {
				uprintf("  0x%08X: '%s' -> '%s%s'\n", i, &buf[i], win_nt_bt_rep, &buf[i+strlen(win_nt_bt_org)]);
				strcpy(&buf[i], win_nt_bt_rep);
				// This ensures that we keep the terminator backslash
				buf[i+strlen(win_nt_bt_rep)] = buf[i+strlen(win_nt_bt_org)];
				buf[i+strlen(win_nt_bt_rep)+1] = 0;
			}
		}
	}

	if ((!WriteFile(handle, buf, size, &rw_size, NULL)) || (size != rw_size)) {
		uprintf("Could not write patched file: %s\n", WindowsErrorString());
		goto out;
	}
	safe_free(buf);
	safe_closehandle(handle);

	r = TRUE;

out:
	safe_closehandle(handle);
	safe_free(buf);
	return r;
}

/*
 * Issue a complete remount of the volume
 */
static BOOL RemountVolume(char drive_letter)
{
	char drive_guid[50];
	char drive_name[] = "?:\\";

	drive_name[0] = drive_letter;
	if (GetVolumeNameForVolumeMountPointA(drive_name, drive_guid, sizeof(drive_guid))) {
		if (DeleteVolumeMountPointA(drive_name)) {
			Sleep(200);
			if (SetVolumeMountPointA(drive_name, drive_guid)) {
				uprintf("Successfully remounted %s on %s\n", drive_guid, drive_name);
			} else {
				uprintf("Failed to remount %s on %s\n", drive_guid, drive_name);
				// This will leave the drive unaccessible and must be flagged as an error
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_REMOUNT_VOLUME);
				return FALSE;
			}
		} else {
			uprintf("Could not remount %s %s\n", drive_name, WindowsErrorString());
			// Try to continue regardless
		}
	}
	return TRUE;
}

/*
 * Standalone thread for the formatting operation
 */
DWORD WINAPI FormatThread(LPVOID param)
{
	DWORD num = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	char drive_name[] = "?:\\";
	char bb_msg[512];
	char logfile[MAX_PATH], *userdir;
	FILE* log_fd;
	int r, fs, dt;

	hPhysicalDrive = GetDriveHandle(num, NULL, TRUE, TRUE);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	// At this stage with have both a handle and a lock to the physical drive...

	// ... but we can't write sectors that are part of a volume, even if we have 
	// access to physical, unless we have a lock (which doesn't have to be write)
	// Also, having a volume handle allows us to unmount the volume
	hLogicalVolume = GetDriveHandle(num, drive_name, FALSE, TRUE);
	if (hLogicalVolume == INVALID_HANDLE_VALUE) {
		uprintf("Could not lock volume\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}
	UnmountDrive(hLogicalVolume);

	AnalyzeMBR(hPhysicalDrive);
	AnalyzePBR(hLogicalVolume);

	if (IsChecked(IDC_BADBLOCKS)) {
		do {
			// create a log file for bad blocks report. Since %USERPROFILE% may
			// have localised characters, we use the UTF-8 API.
			userdir = getenvU("USERPROFILE");
			safe_strcpy(logfile, MAX_PATH, userdir);
			safe_free(userdir);
			GetLocalTime(&lt);
			safe_sprintf(&logfile[strlen(logfile)], sizeof(logfile)-strlen(logfile)-1,
				"\\rufus_%04d%02d%02d_%02d%02d%02d.log",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
			log_fd = fopenU(logfile, "w+");
			if (log_fd == NULL) {
				uprintf("Could not create log file for bad blocks check\n");
			} else {
				fprintf(log_fd, "Rufus bad blocks check started on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fflush(log_fd);
			}

			if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize,
				SelectedDrive.Geometry.BytesPerSector, ComboBox_GetCurSel(hNBPasses)+1, &report, log_fd)) {
				uprintf("Bad blocks: Check failed.\n");
				if (!FormatStatus)
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|
						APPERR(ERROR_BADBLOCKS_FAILURE);
				ClearMBR(hPhysicalDrive);
				fclose(log_fd);
				_unlink(logfile);
				goto out;
			}
			uprintf("Bad Blocks: Check completed, %u bad block%s found. (%d/%d/%d errors)\n",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
			r = IDOK;
			if (report.bb_count) {
				safe_sprintf(bb_msg, sizeof(bb_msg), "Check completed: %u bad block%s found.\n"
					"  %d read errors\n  %d write errors\n  %d corruption errors\n",
					report.bb_count, (report.bb_count==1)?"":"s",
					report.num_read_errors, report.num_write_errors, 
					report.num_corruption_errors);
				fprintf(log_fd, "%s", bb_msg);
				GetLocalTime(&lt);
				fprintf(log_fd, "Rufus bad blocks check ended on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fclose(log_fd);
				safe_sprintf(&bb_msg[strlen(bb_msg)], sizeof(bb_msg)-strlen(bb_msg)-1,
					"\nA more detailed report can be found in:\n%s\n", logfile);
				r = MessageBoxU(hMainDialog, bb_msg, "Bad blocks found", MB_ABORTRETRYIGNORE|MB_ICONWARNING);
			} else {
				// We didn't get any errors => delete the log file
				fclose(log_fd);
				_unlink(logfile);
			}
		} while (r == IDRETRY);
		if (r == IDABORT) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			goto out;
		}
	}
	// Close the (unmounted) volume before formatting, but keep the lock
	safe_closehandle(hLogicalVolume);

	// Especially after destructive badblocks test, you must zero the MBR completely
	// before repartitioning. Else, all kind of bad things happen
	if (!ClearMBR(hPhysicalDrive)) {
		uprintf("unable to zero MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	} 
	UpdateProgress(OP_ZERO_MBR, -1.0f);

	if (!CreatePartition(hPhysicalDrive)) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Add a small delay after partitioning to be safe
	Sleep(200);

	if (!FormatDrive(drive_name[0])) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: %s\n", StrError(FormatStatus));
		goto out;
	}

	PrintStatus(0, TRUE, "Writing master boot record...");
	if (!WriteMBR(hPhysicalDrive)) {
		if (!FormatStatus)
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}
	UpdateProgress(OP_FIX_MBR, -1.0f);

	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	dt = (int)ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
	if (IsChecked(IDC_DOS)) {
		if ((dt == DT_WINME) || (dt == DT_FREEDOS) || ((dt == DT_ISO) && (fs == FS_NTFS))) {
			// We still have a lock, which we need to modify the volume boot record 
			// => no need to reacquire the lock...
			hLogicalVolume = GetDriveHandle(num, drive_name, TRUE, FALSE);
			if (hLogicalVolume == INVALID_HANDLE_VALUE) {
				uprintf("Could not re-mount volume for partition boot record access\n");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
				goto out;
			}
			// NB: if you unmount the logical volume here, XP will report error:
			// [0x00000456] The media in the drive may have changed
			PrintStatus(0, TRUE, "Writing partition boot record...");
			if (!WritePBR(hLogicalVolume)) {
				if (!FormatStatus)
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
				goto out;
			}
			// We must close and unlock the volume to write files to it
			safe_unlockclose(hLogicalVolume);
		} else if ( (dt == DT_SYSLINUX) || ((dt == DT_ISO) && ((fs == FS_FAT16) || (fs == FS_FAT32))) ) {
			PrintStatus(0, TRUE, "Installing Syslinux...");
			if (!InstallSyslinux(num, drive_name)) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
			}
		}
	} else {
		if (IsChecked(IDC_SET_ICON))
			SetAutorun(drive_name);
	}

	// We issue a complete remount of the filesystem at on account of:
	// - Ensuring the file explorer properly detects that the volume was updated
	// - Ensuring that an NTFS system will be reparsed so that it becomes bootable
	if (!RemountVolume(drive_name[0]))
		goto out;

	if (IsChecked(IDC_DOS)) {
		if ((dt == DT_WINME) || (dt == DT_FREEDOS)) {
			UpdateProgress(OP_DOS, -1.0f);
			PrintStatus(0, TRUE, "Copying DOS files...");
			if (!ExtractDOS(drive_name)) {
				if (!FormatStatus)
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
				goto out;
			}
		} else if (dt == DT_ISO) {
			if (iso_path != NULL) {
				UpdateProgress(OP_DOS, 0.0f);
				PrintStatus(0, TRUE, "Copying ISO files...");
				drive_name[2] = 0;
				if (!ExtractISO(iso_path, drive_name, FALSE)) {
					if (!FormatStatus)
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
					goto out;
				}
			}
			if (IS_WINPE(iso_report.winpe)) {
				// Apply WinPe fixup
				if (!SetupWinPE(drive_name[0]))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
			}
		}
		UpdateProgress(OP_FINALIZE, -1.0f);
		PrintStatus(0, TRUE, "Finalizing...");
		if (IsChecked(IDC_SET_ICON))
			SetAutorun(drive_name);
		// Issue another complete remount before we exit, to ensure we're clean
		RemountVolume(drive_name[0]);
		// NTFS fixup (WinPE/AIK images don't seem to boot without an extra checkdisk)
		if ((dt == DT_ISO) && (fs == FS_NTFS)) {
			CheckDisk(drive_name[0]);
			UpdateProgress(OP_FINALIZE, -1.0f);
		}
	}

out:
	SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	ExitThread(0);
}
