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

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
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
		PrintStatus(0, "Formatting: %d%% completed.\n", *percent);
//		uprintf("%d percent completed.\n", *percent);
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		PrintStatus(0, "Creating file system: Task %d/%d completed.\n", ++task_number, nb_steps[fs_index]);
		uprintf("Create FS: Task %d/%d completed.\n", task_number, nb_steps[fs_index]);
		format_percent += 100.0f / (1.0f * nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, format_percent);
		break;
	case FCC_DONE:
		PrintStatus(0, "Creating file system: Task %d/%d completed.\n", nb_steps[fs_index], nb_steps[fs_index]);
		uprintf("Create FS: Task %d/%d completed.\n", nb_steps[fs_index], nb_steps[fs_index]);
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
	PrintStatus(0, "Formatting...");
	PF_INIT_OR_OUT(FormatEx, fmifs);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
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

	// TODO: Add/Eliminate FAT12?
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
 * Process the Master Boot Record
 */
static BOOL WriteMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x200 + SecSize -1) / SecSize;
	FILE fake_fd = { 0 };

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
		buf[0x1be] = 0x80;		// Set first partition bootable
	}

	if (!write_sectors(hPhysicalDrive, SecSize, 0, nSecs, buf)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	if (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_SYSLINUX) {
		r = write_syslinux_mbr(&fake_fd);
	} else {
		r = write_95b_mbr(&fake_fd);
	}

out:
	safe_free(buf);
	return r;
}

/*
 * Process the Partition Boot Record
 */
static BOOL WritePBR(HANDLE hLogicalVolume, BOOL bFreeDOS)
{
	int i;
	FILE fake_fd = { 0 };

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;

	// TODO: call write_partition_number_of_heads() and write_partition_start_sector_number()?
	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (!is_fat_16_fs(&fake_fd)) {
			uprintf("New volume does not have a FAT16 boot sector\n");
			break;
		}
		uprintf("Confirmed new volume has a FAT16 boot sector\n");
		if (bFreeDOS) {
			if (!write_fat_16_fd_br(&fake_fd, 0)) break;
		} else if (!write_fat_16_br(&fake_fd, 0)) break;
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
	default:
		uprintf("unsupported FS for FS BR processing\n");
		break;
	}
	FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
	return FALSE;
}

/*
 * Standalone thread for the formatting operation
 */
void __cdecl FormatThread(void* param)
{
	DWORD num = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	char drive_name[] = "?:";
	char bb_msg[512];
	char logfile[MAX_PATH], *userdir;
	FILE* log_fd;
	int r;

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
				uprintf("Bad blocks check failed.\n");
				if (!FormatStatus)
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|
						APPERR(ERROR_BADBLOCKS_FAILURE);
				ClearMBR(hPhysicalDrive);
				goto out;
			}
			uprintf("Check completed, %u bad block%s found. (%d/%d/%d errors)\n",
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
				fclose(log_fd);
				safe_sprintf(&bb_msg[strlen(bb_msg)], sizeof(bb_msg)-strlen(bb_msg)-1,
					"\nA more detailed report can be found in:\n%s\n", logfile);
				r = MessageBoxU(hMainDialog, bb_msg, "Bad blocks found", MB_ABORTRETRYIGNORE|MB_ICONWARNING);
			} else {
				// We didn't get any errors => delete the log file
				// NB: the log doesn't get deleted on abort
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
	// TODO: do we have to sleep here for unmount to be effective?

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
	Sleep(500);

	if (!FormatDrive(drive_name[0])) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: %s\n", StrError(FormatStatus));
		goto out;
	}

	PrintStatus(0, "Writing master boot record...\n");
	if (!WriteMBR(hPhysicalDrive)) {
		// Errorcode has already been set
		goto out;
	}
	UpdateProgress(OP_FIX_MBR, -1.0f);

	if (IsChecked(IDC_DOS)) {
		switch (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType))) {
		case DT_FREEDOS:
		case DT_WINME:
			// We still have a lock, which we need to modify the volume boot record 
			// => no need to reacquire the lock...
			hLogicalVolume = GetDriveHandle(num, drive_name, TRUE, FALSE);
			if (hLogicalVolume == INVALID_HANDLE_VALUE) {
				uprintf("Could not re-mount volume for partition boot record access\n");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
				goto out;
			}
			PrintStatus(0, "Writing partition boot record...\n");
			if (!WritePBR(hLogicalVolume, ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_FREEDOS)) {
				// Errorcode has already been set
				goto out;
			}
			// ...but we must have relinquished that lock to write the MS-DOS files 
			safe_unlockclose(hLogicalVolume);
			UpdateProgress(OP_DOS, -1.0f);
			PrintStatus(0, "Copying DOS files...\n");
			if (!ExtractDOS(drive_name)) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
				goto out;
			}
			break;
		// Syslinux requires patching of the PBR after the files have been extracted
		case DT_SYSLINUX:
			if (!InstallSyslinux(num, drive_name)) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
			break;
		}
	}

out:
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	_endthread();
}
