/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright © 2007-2009 Tom Thornhill/Ridgecrop
 * Copyright © 2011-2019 Pete Batard <pete@akeo.ie>
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
#include <assert.h>
#include <vds.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "settings.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "partition_info.h"
#include "file.h"
#include "drive.h"
#include "format.h"
#include "badblocks.h"
#include "bled/bled.h"
#include "../res/grub/grub_version.h"

/*
 * Globals
 */
const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS", "ext2", "ext3", "ext4" };
DWORD FormatStatus = 0, LastWriteError = 0;
badblocks_report report = { 0 };
static float format_percent = 0.0f;
static int task_number = 0;
extern const int nb_steps[FS_MAX];
extern uint32_t dur_mins, dur_secs;
extern uint32_t wim_nb_files, wim_proc_files, wim_extra_files;
static int actual_fs_type, wintogo_index = -1, wininst_index = 0;
extern BOOL force_large_fat32, enable_ntfs_compression, lock_drive, zero_drive, fast_zeroing, enable_file_indexing, write_as_image;
extern BOOL use_vds;
uint8_t *grub2_buf = NULL;
long grub2_len;
static BOOL WritePBR(HANDLE hLogicalDrive);

/*
 * Convert the fmifs outputs messages (that use an OEM code page) to UTF-8
 */
static void OutputUTF8Message(const char* src)
{
	int len;
	wchar_t* wdst = NULL;

	if (src == NULL)
		goto out;
	len = (int)safe_strlen(src);
	while ((len > 0) && ((src[len-1] == 0x0A) || (src[len-1] == 0x0D) || (src[len-1] == ' ')))
		len--;
	if (len == 0)
		goto out;

	len = MultiByteToWideChar(CP_OEMCP, 0, src, len, NULL, 0);
	if (len == 0)
		goto out;
	wdst = (wchar_t*)calloc(len+1, sizeof(wchar_t));
	if ((wdst == NULL) || (MultiByteToWideChar(CP_OEMCP, 0, src, len, wdst, len+1) == 0))
		goto out;
	uprintf("%S", wdst);

out:
	safe_free(wdst);
}

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID pData)
{
	DWORD* percent;
	if (IS_ERROR(FormatStatus))
		return FALSE;

	assert((actual_fs_type >= 0) && (actual_fs_type < FS_MAX));

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)pData;
		PrintInfo(0, MSG_217, 1.0f * (*percent));
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		if (task_number < nb_steps[actual_fs_type] - 1) {
			if (task_number == 0)
				uprintf("Creating file system...");
			PrintInfo(0, MSG_218, ++task_number, nb_steps[actual_fs_type]);
			format_percent += 100.0f / (1.0f * nb_steps[actual_fs_type]);
			UpdateProgress(OP_CREATE_FS, format_percent);
		}
		break;
	case FCC_DONE:
		PrintInfo(0, MSG_218, nb_steps[actual_fs_type], nb_steps[actual_fs_type]);
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INCOMPATIBLE_FS);
		break;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
		break;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_PROTECT;
		break;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_DEVICE_IN_USE;
		break;
	case FCC_DEVICE_NOT_READY:
		uprintf("The device is not ready");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_READY;
		break;
	case FCC_CANT_QUICK_FORMAT:
		uprintf("Cannot quick format this volume");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_QUICK_FORMAT);
		break;
	case FCC_BAD_LABEL:
		uprintf("Bad label");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_LABEL_TOO_LONG;
		break;
	case FCC_OUTPUT:
		OutputUTF8Message(((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_CLUSTER_SIZE_TOO_BIG:
	case FCC_CLUSTER_SIZE_TOO_SMALL:
		uprintf("Unsupported cluster size");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_CLUSTER_SIZE);
		break;
	case FCC_VOLUME_TOO_BIG:
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too %s", (Command == FCC_VOLUME_TOO_BIG)?"big":"small");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_INVALID_VOLUME_SIZE);
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_MEDIA_IN_DRIVE;
		break;
	default:
		uprintf("FormatExCallback: Received unhandled command 0x02%X - aborting", Command);
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

	switch (Command) {
	case FCC_PROGRESS:
	case FCC_CHECKDISK_PROGRESS:
		percent = (DWORD*)pData;
		PrintInfo(0, MSG_219, *percent);
		break;
	case FCC_DONE:
		if (*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while checking disk");
			return FALSE;
		}
		break;
	case FCC_UNKNOWN1A:
	case FCC_DONE_WITH_STRUCTURE:
		// Silence these specific calls
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System");
		return FALSE;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied");
		return FALSE;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected");
		return FALSE;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use");
		return FALSE;
	case FCC_OUTPUT:
		OutputUTF8Message(((PTEXTOUTPUT)pData)->Output);
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media in drive");
		return FALSE;
	case FCC_READ_ONLY_MODE:
		uprintf("Media has been switched to read-only - Leaving checkdisk");
		break;
	default:
		uprintf("ChkdskExCallback: received unhandled command %X", Command);
		// Assume the command isn't an error
		break;
	}
	return TRUE;
}

/*
 * Converts an UTF-8 label to a valid FAT/NTFS one
 * TODO: Use IVdsService::QueryFileSystemTypes -> VDS_FILE_SYSTEM_TYPE_PROP
 * to get the list of unauthorised and max length for each FS.
 */
static void ToValidLabel(char* Label, BOOL bFAT)
{
	size_t i, j, k;
	BOOL found;
	WCHAR unauthorized[] = L"*?,;:/\\|+=<>[]\"";
	WCHAR to_underscore[] = L"\t.";
	WCHAR *wLabel = utf8_to_wchar(Label);

	if (wLabel == NULL)
		return;

	for (i = 0, k = 0; i < wcslen(wLabel); i++) {
		if (bFAT) {	// NTFS does allows all the FAT unauthorized above
			found = FALSE;
			for (j = 0; j < wcslen(unauthorized); j++) {
				if (wLabel[i] == unauthorized[j]) {
					found = TRUE;
					break;
				}
			}
			// A FAT label that contains extended chars will be rejected
			if (wLabel[i] >= 0x80) {
				wLabel[k++] = L'_';
				found = TRUE;
			}
			if (found)
				continue;
		}
		found = FALSE;
		for (j = 0; j < wcslen(to_underscore); j++) {
			if (wLabel[i] == to_underscore[j]) {
				wLabel[k++] = '_';
				found = TRUE;
				break;
			}
		}
		if (found)
			continue;
		wLabel[k++] = bFAT ? toupper(wLabel[i]) : wLabel[i];
	}
	wLabel[k] = 0;

	if (bFAT) {
		if (wcslen(wLabel) > 11)
			wLabel[11] = 0;
		for (i = 0, j = 0; wLabel[i] != 0 ; i++)
			if (wLabel[i] == '_')
				j++;
		if (i < 2*j) {
			// If the final label is mostly underscore, use the proposed label
			uprintf("FAT label is mostly underscores. Using '%s' label instead.", SelectedDrive.proposed_label);
			for(i = 0; SelectedDrive.proposed_label[i] != 0; i++)
				wLabel[i] = SelectedDrive.proposed_label[i];
			wLabel[i] = 0;
		}
	} else if (wcslen(wLabel) > 32) {
		wLabel[32] = 0;
	}

	// Needed for disk by label isolinux.cfg workaround
	wchar_to_utf8_no_alloc(wLabel, img_report.usb_label, sizeof(img_report.usb_label));
	safe_strcpy(Label, strlen(Label) + 1, img_report.usb_label);
	free(wLabel);
}

/*
 * 28.2  CALCULATING THE VOLUME SERIAL NUMBER
 *
 * For example, say a disk was formatted on 26 Dec 95 at 9:55 PM and 41.94
 * seconds.  DOS takes the date and time just before it writes it to the
 * disk.
 *
 * Low order word is calculated:               Volume Serial Number is:
 * Month & Day         12/26   0c1ah
 * Sec & Hundredths    41:94   295eh               3578:1d02
 * -----
 * 3578h
 *
 * High order word is calculated:
 * Hours & Minutes     21:55   1537h
 * Year                1995    07cbh
 * -----
 * 1d02h
 */
static DWORD GetVolumeID(void)
{
	SYSTEMTIME s;
	DWORD d;
	WORD lo,hi,tmp;

	GetLocalTime(&s);

	lo = s.wDay + (s.wMonth << 8);
	tmp = (s.wMilliseconds/10) + (s.wSecond << 8);
	lo += tmp;

	hi = s.wMinute + (s.wHour << 8);
	hi += s.wYear;

	d = lo + (hi << 16);
	return d;
}

/*
 * Proper computation of FAT size
 * See: http://www.syslinux.org/archives/2016-February/024850.html
 * and subsequent replies.
 */
static DWORD GetFATSizeSectors(DWORD DskSize, DWORD ReservedSecCnt, DWORD SecPerClus, DWORD NumFATs, DWORD BytesPerSect)
{
	ULONGLONG Numerator, Denominator;
	ULONGLONG FatElementSize = 4;
	ULONGLONG ReservedClusCnt = 2;
	ULONGLONG FatSz;

	Numerator = DskSize - ReservedSecCnt + ReservedClusCnt * SecPerClus;
	Denominator = SecPerClus * BytesPerSect / FatElementSize + NumFATs;
	FatSz = Numerator / Denominator + 1;	// +1 to ensure we are rounded up

	return (DWORD)FatSz;
}

/*
 * Large FAT32 volume formatting from fat32format by Tom Thornhill
 * http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm
 */
static BOOL FormatLargeFAT32(DWORD DriveIndex, uint64_t PartitionOffset, DWORD ClusterSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	BOOL r = FALSE;
	DWORD i;
	HANDLE hLogicalVolume = NULL;
	DWORD cbRet;
	DISK_GEOMETRY dgDrive;
	BYTE geometry_ex[256]; // DISK_GEOMETRY_EX is variable size
	PDISK_GEOMETRY_EX xdgDrive = (PDISK_GEOMETRY_EX)(void*)geometry_ex;
	PARTITION_INFORMATION piDrive;
	PARTITION_INFORMATION_EX xpiDrive;
	// Recommended values
	DWORD ReservedSectCount = 32;
	DWORD NumFATs = 2;
	DWORD BackupBootSect = 6;
	DWORD VolumeId = 0; // calculated before format
	char* VolumeName = NULL;
	DWORD BurstSize = 128; // Zero in blocks of 64K typically

	// Calculated later
	DWORD FatSize = 0;
	DWORD BytesPerSect = 0;
	DWORD SectorsPerCluster = 0;
	DWORD TotalSectors = 0;
	DWORD SystemAreaSize = 0;
	DWORD UserAreaSize = 0;
	ULONGLONG qTotalSectors = 0;

	// Structures to be written to the disk
	FAT_BOOTSECTOR32 *pFAT32BootSect = NULL;
	FAT_FSINFO *pFAT32FsInfo = NULL;
	DWORD *pFirstSectOfFat = NULL;
	BYTE* pZeroSect = NULL;
	char VolId[12] = "NO NAME    ";

	// Debug temp vars
	ULONGLONG FatNeeded, ClusterCount;

	if (safe_strncmp(FSName, "FAT", 3) != 0) {
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_INVALID_PARAMETER;
		goto out;
	}
	PrintInfoDebug(0, MSG_222, "Large FAT32");
	UpdateProgressWithInfoInit(NULL, TRUE);
	VolumeId = GetVolumeID();

	// Open the drive and lock it
	hLogicalVolume = GetLogicalHandle(DriveIndex, PartitionOffset, TRUE, TRUE, FALSE);
	if (IS_ERROR(FormatStatus))
		goto out;
	if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL))
		die("Invalid logical volume handle", ERROR_INVALID_HANDLE);

	// Try to disappear the volume while we're formatting it
	UnmountVolume(hLogicalVolume);

	// Work out drive params
	if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dgDrive,
		sizeof(dgDrive), &cbRet, NULL)) {
		if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, xdgDrive,
			sizeof(geometry_ex), &cbRet, NULL)) {
			uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY error: %s", WindowsErrorString());
			die("Failed to get device geometry (both regular and _ex)", ERROR_NOT_SUPPORTED);
		}
		memcpy(&dgDrive, &xdgDrive->Geometry, sizeof(dgDrive));
	}
	if (dgDrive.BytesPerSector < 512)
		dgDrive.BytesPerSector = 512;
	if (IS_ERROR(FormatStatus)) goto out;
	if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0, &piDrive,
		sizeof(piDrive), &cbRet, NULL)) {
		if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0, &xpiDrive,
			sizeof(xpiDrive), &cbRet, NULL)) {
			uprintf("IOCTL_DISK_GET_PARTITION_INFO error: %s", WindowsErrorString());
			die("Failed to get partition info (both regular and _ex)", ERROR_NOT_SUPPORTED);
		}

		memset(&piDrive, 0, sizeof(piDrive));
		piDrive.StartingOffset.QuadPart = xpiDrive.StartingOffset.QuadPart;
		piDrive.PartitionLength.QuadPart = xpiDrive.PartitionLength.QuadPart;
		piDrive.HiddenSectors = (DWORD) (xpiDrive.StartingOffset.QuadPart / dgDrive.BytesPerSector);
	}
	if (IS_ERROR(FormatStatus)) goto out;

	BytesPerSect = dgDrive.BytesPerSector;

	// Checks on Disk Size
	qTotalSectors = piDrive.PartitionLength.QuadPart/dgDrive.BytesPerSector;
	// Low end limit - 65536 sectors
	if (qTotalSectors < 65536) {
		// Most FAT32 implementations would probably mount this volume just fine,
		// but the spec says that we shouldn't do this, so we won't
		die("This drive is too small for FAT32 - there must be at least 64K clusters", APPERR(ERROR_INVALID_CLUSTER_SIZE));
	}

	if (qTotalSectors >= 0xffffffff) {
		// This is a more fundamental limitation on FAT32 - the total sector count in the root dir
		// is 32bit. With a bit of creativity, FAT32 could be extended to handle at least 2^28 clusters
		// There would need to be an extra field in the FSInfo sector, and the old sector count could
		// be set to 0xffffffff. This is non standard though, the Windows FAT driver FASTFAT.SYS won't
		// understand this. Perhaps a future version of FAT32 and FASTFAT will handle this.
		die("This drive is too big for FAT32 - max 2TB supported", APPERR(ERROR_INVALID_VOLUME_SIZE));
	}

	// coverity[tainted_data]
	pFAT32BootSect = (FAT_BOOTSECTOR32*) calloc(BytesPerSect, 1);
	pFAT32FsInfo = (FAT_FSINFO*) calloc(BytesPerSect, 1);
	pFirstSectOfFat = (DWORD*) calloc(BytesPerSect, 1);
	if (!pFAT32BootSect || !pFAT32FsInfo || !pFirstSectOfFat) {
		die("Failed to allocate memory", ERROR_NOT_ENOUGH_MEMORY);
	}

	// fill out the boot sector and fs info
	pFAT32BootSect->sJmpBoot[0]=0xEB;
	pFAT32BootSect->sJmpBoot[1]=0x58; // jmp.s $+0x5a is 0xeb 0x58, not 0xeb 0x5a. Thanks Marco!
	pFAT32BootSect->sJmpBoot[2]=0x90;
	memcpy(pFAT32BootSect->sOEMName, "MSWIN4.1", 8);
	pFAT32BootSect->wBytsPerSec = (WORD) BytesPerSect;
	SectorsPerCluster = ClusterSize / BytesPerSect;

	pFAT32BootSect->bSecPerClus = (BYTE) SectorsPerCluster ;
	pFAT32BootSect->wRsvdSecCnt = (WORD) ReservedSectCount;
	pFAT32BootSect->bNumFATs = (BYTE) NumFATs;
	pFAT32BootSect->wRootEntCnt = 0;
	pFAT32BootSect->wTotSec16 = 0;
	pFAT32BootSect->bMedia = 0xF8;
	pFAT32BootSect->wFATSz16 = 0;
	pFAT32BootSect->wSecPerTrk = (WORD) dgDrive.SectorsPerTrack;
	pFAT32BootSect->wNumHeads = (WORD) dgDrive.TracksPerCylinder;
	pFAT32BootSect->dHiddSec = (DWORD) piDrive.HiddenSectors;
	TotalSectors = (DWORD)  (piDrive.PartitionLength.QuadPart/dgDrive.BytesPerSector);
	pFAT32BootSect->dTotSec32 = TotalSectors;

	FatSize = GetFATSizeSectors(pFAT32BootSect->dTotSec32, pFAT32BootSect->wRsvdSecCnt,
		pFAT32BootSect->bSecPerClus, pFAT32BootSect->bNumFATs, BytesPerSect);

	pFAT32BootSect->dFATSz32 = FatSize;
	pFAT32BootSect->wExtFlags = 0;
	pFAT32BootSect->wFSVer = 0;
	pFAT32BootSect->dRootClus = 2;
	pFAT32BootSect->wFSInfo = 1;
	pFAT32BootSect->wBkBootSec = (WORD) BackupBootSect;
	pFAT32BootSect->bDrvNum = 0x80;
	pFAT32BootSect->Reserved1 = 0;
	pFAT32BootSect->bBootSig = 0x29;

	pFAT32BootSect->dBS_VolID = VolumeId;
	memcpy(pFAT32BootSect->sVolLab, VolId, 11);
	memcpy(pFAT32BootSect->sBS_FilSysType, "FAT32   ", 8);
	((BYTE*)pFAT32BootSect)[510] = 0x55;
	((BYTE*)pFAT32BootSect)[511] = 0xaa;

	// FATGEN103.DOC says "NOTE: Many FAT documents mistakenly say that this 0xAA55 signature occupies the "last 2 bytes of
	// the boot sector". This statement is correct if - and only if - BPB_BytsPerSec is 512. If BPB_BytsPerSec is greater than
	// 512, the offsets of these signature bytes do not change (although it is perfectly OK for the last two bytes at the end
	// of the boot sector to also contain this signature)."
	//
	// Windows seems to only check the bytes at offsets 510 and 511. Other OSs might check the ones at the end of the sector,
	// so we'll put them there too.
	if (BytesPerSect != 512) {
		((BYTE*)pFAT32BootSect)[BytesPerSect-2] = 0x55;
		((BYTE*)pFAT32BootSect)[BytesPerSect-1] = 0xaa;
	}

	// FSInfo sect
	pFAT32FsInfo->dLeadSig = 0x41615252;
	pFAT32FsInfo->dStrucSig = 0x61417272;
	pFAT32FsInfo->dFree_Count = (DWORD) -1;
	pFAT32FsInfo->dNxt_Free = (DWORD) -1;
	pFAT32FsInfo->dTrailSig = 0xaa550000;

	// First FAT Sector
	pFirstSectOfFat[0] = 0x0ffffff8;  // Reserved cluster 1 media id in low byte
	pFirstSectOfFat[1] = 0x0fffffff;  // Reserved cluster 2 EOC
	pFirstSectOfFat[2] = 0x0fffffff;  // end of cluster chain for root dir

	// Write boot sector, fats
	// Sector 0 Boot Sector
	// Sector 1 FSInfo
	// Sector 2 More boot code - we write zeros here
	// Sector 3 unused
	// Sector 4 unused
	// Sector 5 unused
	// Sector 6 Backup boot sector
	// Sector 7 Backup FSInfo sector
	// Sector 8 Backup 'more boot code'
	// zeroed sectors upto ReservedSectCount
	// FAT1  ReservedSectCount to ReservedSectCount + FatSize
	// ...
	// FATn  ReservedSectCount to ReservedSectCount + FatSize
	// RootDir - allocated to cluster2

	UserAreaSize = TotalSectors - ReservedSectCount - (NumFATs*FatSize);
	ClusterCount = UserAreaSize / SectorsPerCluster;

	// Sanity check for a cluster count of >2^28, since the upper 4 bits of the cluster values in
	// the FAT are reserved.
	if (ClusterCount > 0x0FFFFFFF) {
		die("This drive has more than 2^28 clusters, try to specify a larger cluster size or use the default",
			ERROR_INVALID_CLUSTER_SIZE);
	}

	// Sanity check - < 64K clusters means that the volume will be misdetected as FAT16
	if (ClusterCount < 65536) {
		die("FAT32 must have at least 65536 clusters, try to specify a smaller cluster size or use the default",
			ERROR_INVALID_CLUSTER_SIZE);
	}

	// Sanity check, make sure the fat is big enough
	// Convert the cluster count into a Fat sector count, and check the fat size value we calculated
	// earlier is OK.
	FatNeeded = ClusterCount * 4;
	FatNeeded += (BytesPerSect-1);
	FatNeeded /= BytesPerSect;
	if (FatNeeded > FatSize) {
		die("This drive is too big for large FAT32 format", APPERR(ERROR_INVALID_VOLUME_SIZE));
	}

	// Now we're committed - print some info first
	uprintf("Size : %s %u sectors", SizeToHumanReadable(piDrive.PartitionLength.QuadPart, TRUE, FALSE), TotalSectors);
	uprintf("Cluster size %d bytes, %d Bytes Per Sector", SectorsPerCluster*BytesPerSect, BytesPerSect);
	uprintf("Volume ID is %x:%x", VolumeId>>16, VolumeId&0xffff);
	uprintf("%d Reserved Sectors, %d Sectors per FAT, %d FATs", ReservedSectCount, FatSize, NumFATs);
	uprintf("%d Total clusters", ClusterCount);

	// Fix up the FSInfo sector
	pFAT32FsInfo->dFree_Count = (UserAreaSize/SectorsPerCluster) - 1;
	pFAT32FsInfo->dNxt_Free = 3; // clusters 0-1 reserved, we used cluster 2 for the root dir

	uprintf("%d Free Clusters", pFAT32FsInfo->dFree_Count);
	// Work out the Cluster count

	// First zero out ReservedSect + FatSize * NumFats + SectorsPerCluster
	SystemAreaSize = ReservedSectCount + (NumFATs*FatSize) + SectorsPerCluster;
	uprintf("Clearing out %d sectors for reserved sectors, FATs and root cluster...", SystemAreaSize);

	// Not the most effective, but easy on RAM
	pZeroSect = (BYTE*)calloc(BytesPerSect, BurstSize);
	if (!pZeroSect) {
		die("Failed to allocate memory", ERROR_NOT_ENOUGH_MEMORY);
	}

	for (i=0; i<(SystemAreaSize+BurstSize-1); i+=BurstSize) {
		UpdateProgressWithInfo(OP_FORMAT, MSG_217, (uint64_t)i, (uint64_t)(SystemAreaSize + BurstSize));
		CHECK_FOR_USER_CANCEL;
		if (write_sectors(hLogicalVolume, BytesPerSect, i, BurstSize, pZeroSect) != (BytesPerSect*BurstSize)) {
			die("Error clearing reserved sectors", ERROR_WRITE_FAULT);
		}
	}

	uprintf ("Initializing reserved sectors and FATs...");
	// Now we should write the boot sector and fsinfo twice, once at 0 and once at the backup boot sect position
	for (i=0; i<2; i++) {
		int SectorStart = (i==0) ? 0 : BackupBootSect;
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart, 1, pFAT32BootSect);
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart+1, 1, pFAT32FsInfo);
	}

	// Write the first fat sector in the right places
	for ( i=0; i<NumFATs; i++ ) {
		int SectorStart = ReservedSectCount + (i * FatSize );
		uprintf("FAT #%d sector at address: %d", i, SectorStart);
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart, 1, pFirstSectOfFat);
	}

	if (!(Flags & FP_NO_BOOT)) {
		// Must do it here, as have issues when trying to write the PBR after a remount
		PrintInfoDebug(0, MSG_229);
		if (!WritePBR(hLogicalVolume)) {
			// Non fatal error, but the drive probably won't boot
			uprintf("Could not write partition boot record - drive may not boot...");
		}
	}

	// Set the FAT32 volume label
	PrintInfoDebug(0, MSG_221);
	// Handle must be closed for SetVolumeLabel to work
	safe_closehandle(hLogicalVolume);
	VolumeName = GetLogicalName(DriveIndex, PartitionOffset, TRUE, TRUE);
	if ((VolumeName == NULL) || (!SetVolumeLabelA(VolumeName, Label))) {
		uprintf("Could not set label: %s", WindowsErrorString());
		// Non fatal error
	}

	uprintf("Format completed.");
	r = TRUE;

out:
	safe_free(VolumeName);
	safe_closehandle(hLogicalVolume);
	safe_free(pFAT32BootSect);
	safe_free(pFAT32FsInfo);
	safe_free(pFirstSectOfFat);
	safe_free(pZeroSect);
	return r;
}

// Error messages for ext2fs
const char* error_message(errcode_t error_code)
{
	static char error_string[256];

	switch (error_code) {
	case EXT2_ET_MAGIC_EXT2FS_FILSYS:
	case EXT2_ET_MAGIC_BADBLOCKS_LIST:
	case EXT2_ET_MAGIC_BADBLOCKS_ITERATE:
	case EXT2_ET_MAGIC_INODE_SCAN:
	case EXT2_ET_MAGIC_IO_CHANNEL:
	case EXT2_ET_MAGIC_IO_MANAGER:
	case EXT2_ET_MAGIC_BLOCK_BITMAP:
	case EXT2_ET_MAGIC_INODE_BITMAP:
	case EXT2_ET_MAGIC_GENERIC_BITMAP:
	case EXT2_ET_MAGIC_ICOUNT:
	case EXT2_ET_MAGIC_EXTENT_HANDLE:
	case EXT2_ET_BAD_MAGIC:
		return "Bad magic";
	case EXT2_ET_RO_FILSYS:
		return "Read-only file system";
	case EXT2_ET_GDESC_BAD_BLOCK_MAP:
	case EXT2_ET_GDESC_BAD_INODE_MAP:
	case EXT2_ET_GDESC_BAD_INODE_TABLE:
		return "Bad map or table";
	case EXT2_ET_UNEXPECTED_BLOCK_SIZE:
		return "Unexpected block size";
	case EXT2_ET_DIR_CORRUPTED:
		return "Corrupted entry";
	case EXT2_ET_GDESC_READ:
	case EXT2_ET_GDESC_WRITE:
	case EXT2_ET_INODE_BITMAP_WRITE:
	case EXT2_ET_INODE_BITMAP_READ:
	case EXT2_ET_BLOCK_BITMAP_WRITE:
	case EXT2_ET_BLOCK_BITMAP_READ:
	case EXT2_ET_INODE_TABLE_WRITE:
	case EXT2_ET_INODE_TABLE_READ:
	case EXT2_ET_NEXT_INODE_READ:
	case EXT2_ET_SHORT_READ:
	case EXT2_ET_SHORT_WRITE:
		return "read/write error";
	case EXT2_ET_DIR_NO_SPACE:
		return "no space left";
	case EXT2_ET_TOOSMALL:
		return "Too small";
	case EXT2_ET_BAD_DEVICE_NAME:
		return "Bad device name";
	case EXT2_ET_MISSING_INODE_TABLE:
		return "Missing inode table";
	case EXT2_ET_CORRUPT_SUPERBLOCK:
		return "Superblock is corrupted";
	case EXT2_ET_CALLBACK_NOTHANDLED:
		return "Unhandled callback";
	case EXT2_ET_BAD_BLOCK_IN_INODE_TABLE:
		return "Bad block in inode table";
	case EXT2_ET_UNSUPP_FEATURE:
	case EXT2_ET_RO_UNSUPP_FEATURE:
	case EXT2_ET_UNIMPLEMENTED:
		return "Unsupported feature";
	case EXT2_ET_LLSEEK_FAILED:
		return "Seek failed";
	case EXT2_ET_NO_MEMORY:
	case EXT2_ET_BLOCK_ALLOC_FAIL:
	case EXT2_ET_INODE_ALLOC_FAIL:
		return "Out of memory";
	case EXT2_ET_INVALID_ARGUMENT:
		return "Invalid argument";
	case EXT2_ET_NO_DIRECTORY:
		return "No directory";
	case EXT2_ET_FILE_NOT_FOUND:
		return "File not found";
	case EXT2_ET_FILE_RO:
		return "File is read-only";
	case EXT2_ET_DIR_EXISTS:
		return "Directory already exists";
	case EXT2_ET_CANCEL_REQUESTED:
		return "Cancel requested";
	case EXT2_ET_FILE_TOO_BIG:
		return "File too big";
	case EXT2_ET_JOURNAL_NOT_BLOCK:
	case EXT2_ET_NO_JOURNAL_SB:
		return "No journal superblock";
	case EXT2_ET_JOURNAL_TOO_SMALL:
		return "Journal too small";
	case EXT2_ET_NO_JOURNAL:
		return "No journal";
	case EXT2_ET_TOO_MANY_INODES:
		return "Too many inodes";
	case EXT2_ET_NO_CURRENT_NODE:
		return "No current node";
	case EXT2_ET_OP_NOT_SUPPORTED:
		return "Operation not supported";
	case EXT2_ET_IO_CHANNEL_NO_SUPPORT_64:
		return "I/O Channel does not support 64-bit operation";
	case EXT2_ET_BAD_DESC_SIZE:
		return "Bad descriptor size";
	case EXT2_ET_INODE_CSUM_INVALID:
	case EXT2_ET_INODE_BITMAP_CSUM_INVALID:
	case EXT2_ET_EXTENT_CSUM_INVALID:
	case EXT2_ET_DIR_CSUM_INVALID:
	case EXT2_ET_EXT_ATTR_CSUM_INVALID:
	case EXT2_ET_SB_CSUM_INVALID:
	case EXT2_ET_BLOCK_BITMAP_CSUM_INVALID:
	case EXT2_ET_MMP_CSUM_INVALID:
		return "Invalid checksum";
	case EXT2_ET_UNKNOWN_CSUM:
		return "Unknown checksum";
	case EXT2_ET_FILE_EXISTS:
		return "File exists";
	case EXT2_ET_INODE_IS_GARBAGE:
		return "Inode is garbage";
	case EXT2_ET_JOURNAL_FLAGS_WRONG:
		return "Wrong journal flags";
	case EXT2_ET_FILESYSTEM_CORRUPTED:
		return "File system is corrupted";
	case EXT2_ET_BAD_CRC:
		return "Bad CRC";
	case EXT2_ET_CORRUPT_JOURNAL_SB:
		return "Journal Superblock is corrupted";
	case EXT2_ET_INODE_CORRUPTED:
	case EXT2_ET_EA_INODE_CORRUPTED:
		return "Inode is corrupted";
	default:
		if ((error_code > EXT2_ET_BASE) && error_code < (EXT2_ET_BASE + 1000)) {
			static_sprintf(error_string, "Unknown ext2fs error %ld (EXT2_ET_BASE + %ld)", error_code, error_code - EXT2_ET_BASE);
		} else {
			SetLastError((FormatStatus == 0) ? (ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | (error_code & 0xFFFF)) : FormatStatus);
			static_sprintf(error_string, WindowsErrorString());
		}
		return error_string;
	}
}

static float ext2_percent_start = 0.0f, ext2_percent_share = 0.5f;
const float ext2_max_marker = 80.0f;
errcode_t ext2fs_print_progress(int64_t cur_value, int64_t max_value)
{
	static int64_t last_value = -1;
	if (max_value == 0)
		return 0;
	// TODO: Need to use OP_CREATE_FS here for standalone format
	UpdateProgressWithInfo(OP_FORMAT, MSG_217, (uint64_t)((ext2_percent_start * max_value) + (ext2_percent_share * cur_value)), max_value);
	cur_value = (int64_t)(((float)cur_value / (float)max_value) * min(ext2_max_marker, (float)max_value));
	if ((cur_value < last_value) || (cur_value > last_value)) {
		last_value = cur_value;
		uprintfs("+");
	}
	return IS_ERROR(FormatStatus) ? EXT2_ET_CANCEL_REQUESTED : 0;
}

const char* GetExtFsLabel(DWORD DriveIndex, uint64_t PartitionOffset)
{
	static char label[EXT2_LABEL_LEN + 1];
	errcode_t r;
	ext2_filsys ext2fs = NULL;
	io_manager manager = nt_io_manager();
	char* volume_name = AltMountVolume(DriveIndex, PartitionOffset, TRUE);

	if (volume_name == NULL)
		return NULL;
	r = ext2fs_open(volume_name, EXT2_FLAG_SKIP_MMP, 0, 0, manager, &ext2fs);
	if (r == 0) {
		strncpy(label, ext2fs->super->s_volume_name, EXT2_LABEL_LEN);
		label[EXT2_LABEL_LEN] = 0;
	}
	if (ext2fs != NULL)
		ext2fs_close(ext2fs);
	AltUnmountVolume(volume_name, TRUE);
	return (r == 0) ? label : NULL;
}

BOOL FormatExtFs(DWORD DriveIndex, uint64_t PartitionOffset, DWORD BlockSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	// Mostly taken from mke2fs.conf
	const float reserve_ratio = 0.05f;
	const ext2fs_default_t ext2fs_default[5] = {
		{ 3*MB, 1024, 128, 3},		// "floppy"
		{ 512*MB, 1024, 128, 2},	// "small"
		{ 4*GB, 4096, 256, 2},		// "default"
		{ 16*GB, 4096, 256, 3},		// "big"
		{ 1024*TB, 4096, 256, 4}	// "huge"
	};

	BOOL ret = FALSE;
	char *volume_name = NULL;
	int i, count;
	struct ext2_super_block features = { 0 };
	io_manager manager = nt_io_manager();
	blk_t journal_size;
	blk64_t size = 0, cur;
	ext2_filsys ext2fs = NULL;
	errcode_t r;
	uint8_t* buf = NULL;

#if defined(RUFUS_TEST)
	// Create a 32 MB disk image file to test
	uint8_t zb[1024];
	HANDLE h;
	DWORD dwSize;
	volume_name = strdup("\\??\\C:\\tmp\\disk.img");
	memset(zb, 0xFF, sizeof(zb));	// Set to nonzero so we can detect init issues
	h = CreateFileU(volume_name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	for (i = 0; i < 32 * 1024; i++) {
		if (!WriteFile(h, zb, sizeof(zb), &dwSize, NULL) || (dwSize != sizeof(zb))) {
			uprintf("Write error: %s", WindowsErrorString());
			break;
		}
	}
	CloseHandle(h);
#else
	volume_name = AltMountVolume(DriveIndex, PartitionOffset, FALSE);
#endif
	if ((volume_name == NULL) | (strlen(FSName) != 4) || (strncmp(FSName, "ext", 3) != 0)) {
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_INVALID_PARAMETER;
		goto out;
	}

	if ((strcmp(FSName, FileSystemLabel[FS_EXT2]) != 0) && (strcmp(FSName, FileSystemLabel[FS_EXT3]) != 0)) {
		if (strcmp(FSName, FileSystemLabel[FS_EXT4]) == 0)
			uprintf("ext4 file system is not supported, will use ext3 instead");
		else
			uprintf("invalid ext file system version requested, will use ext3");
	}

	if ((strcmp(FSName, FileSystemLabel[FS_EXT2]) != 0) && (strcmp(FSName, FileSystemLabel[FS_EXT3]) != 0))
		FSName = FileSystemLabel[FS_EXT3];

	PrintInfoDebug(0, MSG_222, FSName);
	UpdateProgressWithInfoInit(NULL, TRUE);

	// Figure out the volume size and block size
	r = ext2fs_get_device_size2(volume_name, KB, &size);
	if ((r != 0) || (size == 0)) {
		FormatStatus = ext2_last_winerror(ERROR_READ_FAULT);
		uprintf("Could not read device size: %s", error_message(r));
		goto out;
	}
	size *= KB;
	for (i = 0; i < ARRAYSIZE(ext2fs_default); i++) {
		if (size < ext2fs_default[i].max_size)
			break;
	}
	assert(i < ARRAYSIZE(ext2fs_default));
	// NB: We validated that BlockSize is a power of two in FormatPartition()
	if (BlockSize == 0)
		BlockSize = ext2fs_default[i].block_size;
	size /= BlockSize;
	for (features.s_log_block_size = 0; EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE; features.s_log_block_size++) {
		if (EXT2_BLOCK_SIZE(&features) == BlockSize)
			break;
	}
	assert(EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE);

	// Set the blocks, reserved blocks and inodes
	ext2fs_blocks_count_set(&features, size);
	ext2fs_r_blocks_count_set(&features, (blk64_t)(reserve_ratio * size));
	features.s_rev_level = 1;
	features.s_inode_size = ext2fs_default[i].inode_size;
	features.s_inodes_count = ((ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio) > UINT32_MAX) ?
		UINT32_MAX : (uint32_t)(ext2fs_blocks_count(&features) >> ext2fs_default[i].inode_ratio);
	uprintf("%d possible inodes out of %lld blocks (block size = %d)", features.s_inodes_count, size, EXT2_BLOCK_SIZE(&features));
	uprintf("%lld blocks (%0.1f%%) reserved for the super user", ext2fs_r_blocks_count(&features), reserve_ratio * 100.0f);

	// Set features
	ext2fs_set_feature_xattr(&features);
	ext2fs_set_feature_resize_inode(&features);
	ext2fs_set_feature_dir_index(&features);
	ext2fs_set_feature_filetype(&features);
	ext2fs_set_feature_sparse_super(&features);
	ext2fs_set_feature_large_file(&features);
	if (FSName[3] != '2')
		ext2fs_set_feature_journal(&features);
	features.s_backup_bgs[0] = ~0;
	features.s_default_mount_opts = EXT2_DEFM_XATTR_USER | EXT2_DEFM_ACL;

	// Now that we have set our base features, initialize a virtual superblock
	r = ext2fs_initialize(volume_name, EXT2_FLAG_EXCLUSIVE | EXT2_FLAG_64BITS, &features, manager, &ext2fs);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_INVALID_DATA);
		uprintf("Could not initialize %s features: %s", FSName, error_message(r));
		goto out;
	}

	// Zero 16 blocks of data from the start of our volume
	buf = calloc(16, ext2fs->io->block_size);
	assert(buf != NULL);
	r = io_channel_write_blk64(ext2fs->io, 0, 16, buf);
	safe_free(buf);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
		uprintf("Could not zero %s superblock area: %s", FSName, error_message(r));
		goto out;
	}

	// Finish setting up the file system
	IGNORE_RETVAL(CoCreateGuid((GUID*)ext2fs->super->s_uuid));
	ext2fs_init_csum_seed(ext2fs);
	ext2fs->super->s_def_hash_version = EXT2_HASH_HALF_MD4;
	IGNORE_RETVAL(CoCreateGuid((GUID*)ext2fs->super->s_hash_seed));
	ext2fs->super->s_max_mnt_count = -1;
	ext2fs->super->s_creator_os = EXT2_OS_WINDOWS;
	ext2fs->super->s_errors = EXT2_ERRORS_CONTINUE;
	if (Label != NULL)
		static_strcpy(ext2fs->super->s_volume_name, Label);

	r = ext2fs_allocate_tables(ext2fs);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_INVALID_DATA);
		uprintf("Could not allocate %s tables: %s", FSName, error_message(r));
		goto out;
	}
	r = ext2fs_convert_subcluster_bitmap(ext2fs, &ext2fs->block_map);
	if (r != 0) {
		uprintf("Could set %s cluster bitmap: %s", FSName, error_message(r));
		goto out;
	}

	ext2_percent_start = 0.0f;
	ext2_percent_share = (FSName[3] == '2') ? 1.0f : 0.5f;
	uprintf("Creating %d inode sets: [1 marker = %0.1f set(s)]", ext2fs->group_desc_count,
		max((float)ext2fs->group_desc_count / ext2_max_marker, 1.0f));
	for (i = 0; i < (int)ext2fs->group_desc_count; i++) {
		if (ext2fs_print_progress((int64_t)i, (int64_t)ext2fs->group_desc_count))
			goto out;
		cur = ext2fs_inode_table_loc(ext2fs, i);
		count = ext2fs_div_ceil((ext2fs->super->s_inodes_per_group - ext2fs_bg_itable_unused(ext2fs, i))
			* EXT2_BLOCK_SIZE(ext2fs->super), EXT2_BLOCK_SIZE(ext2fs->super));
		r = ext2fs_zero_blocks2(ext2fs, cur, count, &cur, &count);
		if (r != 0) {
			FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
			uprintf("\r\nCould not zero inode set at position %llu (%d blocks): %s", cur, count, error_message(r));
			goto out;
		}
	}
	uprintfs("\r\n");

	// Create root and lost+found dirs
	r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_DIR_NOT_ROOT);
		uprintf("Failed to create %s root dir: %s", FSName, error_message(r));
		goto out;
	}
	ext2fs->umask = 077;
	r = ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, 0, "lost+found");
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_DIR_NOT_ROOT);
		uprintf("Failed to create %s 'lost+found' dir: %s", FSName, error_message(r));
		goto out;
	}

	// Create bitmaps
	for (i = EXT2_ROOT_INO + 1; i < (int)EXT2_FIRST_INODE(ext2fs->super); i++)
		ext2fs_inode_alloc_stats(ext2fs, i, 1);
	ext2fs_mark_ib_dirty(ext2fs);

	r = ext2fs_mark_inode_bitmap2(ext2fs->inode_map, EXT2_BAD_INO);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
		uprintf("Could not set inode bitmaps: %s", error_message(r));
		goto out;
	}
	ext2fs_inode_alloc_stats(ext2fs, EXT2_BAD_INO, 1);
	r = ext2fs_update_bb_inode(ext2fs, NULL);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
		uprintf("Could not set inode stats: %s", error_message(r));
		goto out;
	}

	if (FSName[3] != '2') {
		// Create the journal
		ext2_percent_start = 0.5f;
		journal_size = ext2fs_default_journal_size(ext2fs_blocks_count(ext2fs->super));
		journal_size /= 2;	// That journal init is really killing us!
		uprintf("Creating %d journal blocks: [1 marker = %0.1f block(s)]", journal_size,
			max((float)journal_size / ext2_max_marker, 1.0f));
		// Even with EXT2_MKJOURNAL_LAZYINIT, this call is absolutely dreadful in terms of speed...
		r = ext2fs_add_journal_inode(ext2fs, journal_size, EXT2_MKJOURNAL_NO_MNT_CHECK | ((Flags & FP_QUICK) ? EXT2_MKJOURNAL_LAZYINIT : 0));
		uprintfs("\r\n");
		if (r != 0) {
			FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
			uprintf("Could not create %s journal: %s", FSName, error_message(r));
			goto out;
		}
	}

	// Create a 'persistence.conf' file if required
	if (Flags & FP_CREATE_PERSISTENCE_CONF) {
		// You *do* want the LF at the end of the "/ union" line, else Debian Live bails out...
		const char *name = "persistence.conf", data[] = "/ union\n";
		int written = 0, fsize = sizeof(data) - 1;
		ext2_file_t ext2fd;
		ext2_ino_t inode_id;
		uint32_t ctime = (uint32_t)time(0);
		struct ext2_inode inode = { 0 };
		inode.i_mode = 0100644;
		inode.i_links_count = 1;
		inode.i_atime = ctime;
		inode.i_ctime = ctime;
		inode.i_mtime = ctime;
		inode.i_size = fsize;

		ext2fs_namei(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, name, &inode_id);
		ext2fs_new_inode(ext2fs, EXT2_ROOT_INO, 010755, 0, &inode_id);
		ext2fs_link(ext2fs, EXT2_ROOT_INO, name, inode_id, EXT2_FT_REG_FILE);
		ext2fs_inode_alloc_stats(ext2fs, inode_id, 1);
		ext2fs_write_new_inode(ext2fs, inode_id, &inode);
		ext2fs_file_open(ext2fs, inode_id, EXT2_FILE_WRITE, &ext2fd);
		if ((ext2fs_file_write(ext2fd, data, fsize, &written) != 0) || (written != fsize))
			uprintf("Error: Could not create '%s' file", name);
		else
			uprintf("Created '%s' file", name);
		ext2fs_file_close(ext2fd);
	}

	// Finally we can call close() to get the file system gets created
	r = ext2fs_close(ext2fs);
	if (r != 0) {
		FormatStatus = ext2_last_winerror(ERROR_WRITE_FAULT);
		uprintf("Could not create %s volume: %s", FSName, error_message(r));
		goto out;
	}
	UpdateProgressWithInfo(OP_FORMAT, MSG_217, 100, 100);
	uprintf("Done");
	ret = TRUE;

out:
	ext2fs_free(ext2fs);
	free(buf);
	AltUnmountVolume(volume_name, FALSE);
	return ret;
}

/*
 * Call on VDS to format a partition
 */
static BOOL FormatNativeVds(DWORD DriveIndex, uint64_t PartitionOffset, DWORD ClusterSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	BOOL r = FALSE, bFoundVolume = FALSE;
	HRESULT hr;
	ULONG ulFetched;
	IVdsServiceLoader *pLoader;
	IVdsService *pService;
	IEnumVdsObject *pEnum;
	IUnknown *pUnk;
	char* VolumeName = NULL;
	WCHAR *wVolumeName = NULL, *wLabel = utf8_to_wchar(Label), *wFSName = utf8_to_wchar(FSName);

	if ((strcmp(FSName, FileSystemLabel[FS_EXFAT]) == 0) && !((dur_mins == 0) && (dur_secs == 0))) {
		PrintInfoDebug(0, MSG_220, FSName, dur_mins, dur_secs);
	} else {
		PrintInfoDebug(0, MSG_222, FSName);
	}
	UpdateProgressWithInfoInit(NULL, TRUE);
	VolumeName = GetLogicalName(DriveIndex, PartitionOffset, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name");
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_GEN_FAILURE;
		goto out;
	}

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void **)&pLoader);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not create VDS Loader Instance: %s", WindowsErrorString());
		goto out;
	}

	// Load the VDS Service
	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	IVdsServiceLoader_Release(pLoader);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not load VDS Service: %s", WindowsErrorString());
		goto out;
	}

	// Wait for the Service to become ready if needed
	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("VDS Service is not ready: %s", WindowsErrorString());
		goto out;
	}

	// Query the VDS Service Providers
	hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not query VDS Service Providers: %s", WindowsErrorString());
		goto out;
	}

	while (IEnumVdsObject_Next(pEnum, 1, &pUnk, &ulFetched) == S_OK) {
		IVdsProvider *pProvider;
		IVdsSwProvider *pSwProvider;
		IEnumVdsObject *pEnumPack;
		IUnknown *pPackUnk;
		CHECK_FOR_USER_CANCEL;

		// Get VDS Provider
		hr = IUnknown_QueryInterface(pUnk, &IID_IVdsProvider, (void **)&pProvider);
		IUnknown_Release(pUnk);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Provider: %s", WindowsErrorString());
			goto out;
		}

		// Get VDS Software Provider
		hr = IVdsSwProvider_QueryInterface(pProvider, &IID_IVdsSwProvider, (void **)&pSwProvider);
		IVdsProvider_Release(pProvider);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Software Provider: %s", WindowsErrorString());
			goto out;
		}

		// Get VDS Software Provider Packs
		hr = IVdsSwProvider_QueryPacks(pSwProvider, &pEnumPack);
		IVdsSwProvider_Release(pSwProvider);
		if (hr != S_OK) {
			VDS_SET_ERROR(hr);
			uprintf("Could not get VDS Software Provider Packs: %s", WindowsErrorString());
			goto out;
		}

		// Enumerate Provider Packs
		while (IEnumVdsObject_Next(pEnumPack, 1, &pPackUnk, &ulFetched) == S_OK) {
			IVdsPack *pPack;
			IEnumVdsObject *pEnumVolume;
			IUnknown *pVolumeUnk;
			CHECK_FOR_USER_CANCEL;

			hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void **)&pPack);
			IUnknown_Release(pPackUnk);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS Software Provider Pack: %s", WindowsErrorString());
				goto out;
			}

			// Use the pack interface to access the volumes
			hr = IVdsPack_QueryVolumes(pPack, &pEnumVolume);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS volumes: %s", WindowsErrorString());
				goto out;
			}

			// List volumes
			while (IEnumVdsObject_Next(pEnumVolume, 1, &pVolumeUnk, &ulFetched) == S_OK) {
				BOOL match;
				HRESULT hr2 = E_FAIL;
				VDS_VOLUME_PROP VolumeProps;
				LPWSTR *wszPathArray;
				ULONG ulPercentCompleted, ulNumberOfPaths;
				USHORT usFsVersion = 0;
				IVdsVolume *pVolume;
				IVdsAsync* pAsync;
				IVdsVolumeMF3 *pVolumeMF3;
				CHECK_FOR_USER_CANCEL;

				// Get the volume interface.
				hr = IUnknown_QueryInterface(pVolumeUnk, &IID_IVdsVolume, (void **)&pVolume);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS Volume Interface: %s", WindowsErrorString());
					goto out;
				}

				hr = IVdsVolume_GetProperties(pVolume, &VolumeProps);
				if ((hr != S_OK) && (hr != VDS_S_PROPERTIES_INCOMPLETE)) {
					VDS_SET_ERROR(hr);
					IVdsVolume_Release(pVolume);
					uprintf("Could not query VDS Volume Properties: %s", WindowsErrorString());
					continue;
				}
				CoTaskMemFree(VolumeProps.pwszName);

				// Instantiate the IVdsVolumeMF3 interface for our volume.
				hr = IVdsVolume_QueryInterface(pVolume, &IID_IVdsVolumeMF3, (void **)&pVolumeMF3);
				IVdsVolume_Release(pVolume);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not access VDS VolumeMF3 interface: %s", WindowsErrorString());
					continue;
				}

				// Query the volume GUID
				hr = IVdsVolumeMF3_QueryVolumeGuidPathnames(pVolumeMF3, &wszPathArray, &ulNumberOfPaths);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS VolumeGuidPathnames: %s", WindowsErrorString());
					continue;
				}

				if (ulNumberOfPaths > 1)
					uprintf("Notice: Volume %S has more than one GUID...", wszPathArray[0]);

				match = (wcscmp(wVolumeName, wszPathArray[0]) == 0);
				CoTaskMemFree(wszPathArray);
				if (!match)
					continue;

				bFoundVolume = TRUE;
				if (strcmp(Label, FileSystemLabel[FS_UDF]) == 0)
					usFsVersion = ReadSetting32(SETTING_USE_UDF_VERSION);
				if (ClusterSize < 0x200) {
					ClusterSize = 0;
					uprintf("Using default cluster size");
				} else {
					uprintf("Using cluster size: %d bytes", ClusterSize);
				}
				format_percent = 0.0f;
				uprintf("%s format was selected", (Flags & FP_QUICK) ? "Quick" : "Slow");
				if (Flags & FP_COMPRESSION)
					uprintf("NTFS compression is enabled");

				hr = IVdsVolumeMF3_FormatEx2(pVolumeMF3, wFSName, usFsVersion, ClusterSize, wLabel, Flags, &pAsync);
				while (SUCCEEDED(hr)) {
					if (IS_ERROR(FormatStatus)) {
						IVdsAsync_Cancel(pAsync);
						break;
					}
					hr = IVdsAsync_QueryStatus(pAsync, &hr2, &ulPercentCompleted);
					if (SUCCEEDED(hr)) {
						if (Flags & FP_QUICK) {
							// Progress report on quick format is useless, so we'll just pretend we have 2 tasks
							PrintInfo(0, MSG_218, (ulPercentCompleted < 100) ? 1 : 2, 2);
							UpdateProgress(OP_CREATE_FS, (float)ulPercentCompleted);
						} else {
							UpdateProgressWithInfo(OP_FORMAT, MSG_217, ulPercentCompleted, 100);
						}
						hr = hr2;
						if (hr == S_OK)
							break;
						if (hr == VDS_E_OPERATION_PENDING)
							hr = S_OK;
					}
					Sleep(500);
				}
				if (!SUCCEEDED(hr)) {
					VDS_SET_ERROR(hr);
					uprintf("Could not format drive: %s", WindowsErrorString());
					goto out;
				}

				IVdsAsync_Release(pAsync);
				IVdsVolumeMF3_Release(pVolumeMF3);

				if (!IS_ERROR(FormatStatus)) {
					uprintf("Format completed.");
					r = TRUE;
				}
				goto out;
			}
		}
	}

out:
	if ((!bFoundVolume) && (FormatStatus == 0))
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_PATH_NOT_FOUND;
	safe_free(VolumeName);
	safe_free(wVolumeName);
	safe_free(wLabel);
	safe_free(wFSName);
	return r;
}

/*
 * Call on fmifs.dll's FormatEx() to format the drive
 */
static BOOL FormatNative(DWORD DriveIndex, uint64_t PartitionOffset, DWORD ClusterSize, LPCSTR FSName, LPCSTR Label, DWORD Flags)
{
	BOOL r = FALSE;
	PF_DECL(FormatEx);
	PF_DECL(EnableVolumeCompression);
	char *locale, *VolumeName = NULL;
	WCHAR* wVolumeName = NULL, *wLabel = utf8_to_wchar(Label), *wFSName = utf8_to_wchar(FSName);
	size_t i;

	if ((strcmp(FSName, FileSystemLabel[FS_EXFAT]) == 0) && !((dur_mins == 0) && (dur_secs == 0))) {
		PrintInfoDebug(0, MSG_220, FSName, dur_mins, dur_secs);
	} else {
		PrintInfoDebug(0, MSG_222, FSName);
	}
	VolumeName = GetLogicalName(DriveIndex, PartitionOffset, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		goto out;
	}
	// Hey, nice consistency here, Microsoft! -  FormatEx() fails if wVolumeName has
	// a trailing backslash, but EnableCompression() fails without...
	wVolumeName[wcslen(wVolumeName)-1] = 0;		// Remove trailing backslash

	// LoadLibrary("fmifs.dll") appears to changes the locale, which can lead to
	// problems with tolower(). Make sure we restore the locale. For more details,
	// see http://comments.gmane.org/gmane.comp.gnu.mingw.user/39300
	locale = setlocale(LC_ALL, NULL);
	PF_INIT_OR_OUT(FormatEx, fmifs);
	PF_INIT(EnableVolumeCompression, fmifs);
	setlocale(LC_ALL, locale);

	if (ClusterSize < 0x200) {
		// 0 is FormatEx's value for default, which we need to use for UDF
		ClusterSize = 0;
		uprintf("Using default cluster size");
	} else {
		uprintf("Using cluster size: %d bytes", ClusterSize);
	}
	format_percent = 0.0f;
	task_number = 0;

	uprintf("%s format was selected", (Flags & FP_QUICK) ? "Quick" : "Slow");
	for (i = 0; i < WRITE_RETRIES; i++) {
		pfFormatEx(wVolumeName, SelectedDrive.MediaType, wFSName, wLabel,
			(Flags & FP_QUICK), ClusterSize, FormatExCallback);
		if (!IS_ERROR(FormatStatus) || (HRESULT_CODE(FormatStatus) == ERROR_CANCELLED))
			break;
		uprintf("%s - Retrying...", WindowsErrorString());
		Sleep(WRITE_TIMEOUT);
	}
	if (IS_ERROR(FormatStatus))
		goto out;

	if (Flags & FP_COMPRESSION) {
		wVolumeName[wcslen(wVolumeName)] = '\\';	// Add trailing backslash back again
		if (pfEnableVolumeCompression(wVolumeName, FPF_COMPRESSED)) {
			uprintf("Enabled NTFS compression");
		} else {
			uprintf("Could not enable NTFS compression: %s", WindowsErrorString());
		}
	}

	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.");
		r = TRUE;
	}

out:
	if (!r && !IS_ERROR(FormatStatus))
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|SCODE_CODE(GetLastError());
	safe_free(VolumeName);
	safe_free(wVolumeName);
	safe_free(wLabel);
	safe_free(wFSName);
	return r;
}

static BOOL FormatPartition(DWORD DriveIndex, uint64_t PartitionOffset, DWORD UnitAllocationSize, USHORT FSType, LPCSTR Label, DWORD Flags)
{
	if ((DriveIndex < 0x80) || (DriveIndex > 0x100) || (FSType >= FS_MAX) ||
		// The following validates that UnitAllocationSize is a power of 2
		((UnitAllocationSize != 0) && (UnitAllocationSize & (UnitAllocationSize - 1)))) {
		ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_INVALID_PARAMETER;
		return FALSE;
	}
	actual_fs_type = FSType;
	if ((FSType == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32) || (Flags & FP_LARGE_FAT32)))
		return FormatLargeFAT32(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else if (FSType >= FS_EXT2)
		return FormatExtFs(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else if (use_vds)
		return FormatNativeVds(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
	else
		return FormatNative(DriveIndex, PartitionOffset, UnitAllocationSize, FileSystemLabel[FSType], Label, Flags);
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
	PrintInfoDebug(0, MSG_223);

	PF_INIT_OR_OUT(Chkdsk, Fmifs);

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

static BOOL ClearMBRGPT(HANDLE hPhysicalDrive, LONGLONG DiskSize, DWORD SectorSize, BOOL add1MB)
{
	BOOL r = FALSE;
	uint64_t i, j, last_sector = DiskSize/SectorSize, num_sectors_to_clear;
	unsigned char* pBuf = (unsigned char*) calloc(SectorSize, 1);

	PrintInfoDebug(0, MSG_224);
	if (pBuf == NULL) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}
	// http://en.wikipedia.org/wiki/GUID_Partition_Table tells us we should clear 34 sectors at the
	// beginning and 33 at the end. We bump these values to MAX_SECTORS_TO_CLEAR each end to help
	// with reluctant access to large drive.

	// We try to clear at least 1MB + the PBR when Large FAT32 is selected (add1MB), but
	// don't do it otherwise, as it seems unnecessary and may take time for slow drives.
	// Also, for various reasons (one of which being that Windows seems to have issues
	// with GPT drives that contain a lot of small partitions) we try not not to clear
	// sectors further than the lowest partition already residing on the disk.
	num_sectors_to_clear = min(SelectedDrive.FirstDataSector, (DWORD)((add1MB ? 2048 : 0) + MAX_SECTORS_TO_CLEAR));
	// Special case for big floppy disks (FirstDataSector = 0)
	if (num_sectors_to_clear < 4)
		num_sectors_to_clear = (DWORD)((add1MB ? 2048 : 0) + MAX_SECTORS_TO_CLEAR);

	uprintf("Erasing %d sectors", num_sectors_to_clear);
	for (i=0; i<num_sectors_to_clear; i++) {
		for (j = 1; j <= WRITE_RETRIES; j++) {
			CHECK_FOR_USER_CANCEL;
			if (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize) {
				if (j >= WRITE_RETRIES)
					goto out;
				uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
				// Don't sit idly but use the downtime to check for conflicting processes...
				Sleep(CheckDriveAccess(WRITE_TIMEOUT, FALSE));
			}
		}
	}
	for (i = last_sector - MAX_SECTORS_TO_CLEAR; i < last_sector; i++) {
		for (j = 1; j <= WRITE_RETRIES; j++) {
			CHECK_FOR_USER_CANCEL;
			if (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize) {
				if (j < WRITE_RETRIES) {
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(CheckDriveAccess(WRITE_TIMEOUT, FALSE));
				} else {
					// Windows seems to be an ass about keeping a lock on a backup GPT,
					// so we try to be lenient about not being able to clear it.
					uprintf("Warning: Failed to clear backup GPT...");
					r = TRUE;
					goto out;
				}
			}
		}
	}
	r = TRUE;

out:
	safe_free(pBuf);
	return r;
}

/*
 * Process the Master Boot Record
 */
static BOOL WriteMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	DWORD size;
	unsigned char* buffer = NULL;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	const char* using_msg = "Using %s MBR";

//	AnalyzeMBR(hPhysicalDrive, "Drive", FALSE);

	if (SelectedDrive.SectorSize < 512)
		goto out;

	if (partition_type == PARTITION_STYLE_GPT) {
		// Add a notice in the protective MBR
		fake_fd._handle = (char*)hPhysicalDrive;
		set_bytes_per_sector(SelectedDrive.SectorSize);
		uprintf(using_msg, "Rufus protective");
		r = write_rufus_gpt_mbr(fp);
		goto notify;
	}

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	buffer = (unsigned char*)_mm_malloc(SelectedDrive.SectorSize, 16);
	if (buffer == NULL) {
		uprintf("Could not allocate memory for MBR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hPhysicalDrive, SelectedDrive.SectorSize, 0, 1, buffer)) {
		uprintf("Could not read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		if (buffer[0x1c2] == 0x0e) {
			uprintf("Partition is already FAT16 LBA...\n");
		} else if ((buffer[0x1c2] != 0x04) && (buffer[0x1c2] != 0x06)) {
			uprintf("Warning: converting a non FAT16 partition to FAT16 LBA: FS type=0x%02x\n", buffer[0x1c2]);
		}
		buffer[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		if (buffer[0x1c2] == 0x0c) {
			uprintf("Partition is already FAT32 LBA...\n");
		} else if (buffer[0x1c2] != 0x0b) {
			uprintf("Warning: converting a non FAT32 partition to FAT32 LBA: FS type=0x%02x\n", buffer[0x1c2]);
		}
		buffer[0x1c2] = 0x0c;
		break;
	}
	if ((boot_type != BT_NON_BOOTABLE) && (target_type == TT_BIOS)) {
		// Set first partition bootable - masquerade as per the DiskID selected
		buffer[0x1be] = IsChecked(IDC_RUFUS_MBR) ?
			(BYTE)ComboBox_GetItemData(hDiskID, ComboBox_GetCurSel(hDiskID)):0x80;
		uprintf("Set bootable USB partition as 0x%02X\n", buffer[0x1be]);
	}

	if (!write_sectors(hPhysicalDrive, SelectedDrive.SectorSize, 0, 1, buffer)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	// What follows is really a case statement with complex conditions listed
	// by order of preference
	if ((boot_type == BT_IMAGE) && HAS_WINDOWS(img_report) && (allow_dual_uefi_bios) && (target_type == TT_BIOS))
		goto windows_mbr;

	// Non bootable or forced UEFI (zeroed MBR)
	if ((boot_type == BT_NON_BOOTABLE) || (target_type == TT_UEFI)) {
		uprintf(using_msg, "Zeroed");
		r = write_zero_mbr(fp);
		goto notify;
	}

	// Syslinux
	if ( (boot_type == BT_SYSLINUX_V4) || (boot_type == BT_SYSLINUX_V6) ||
		 ((boot_type == BT_IMAGE) && HAS_SYSLINUX(img_report)) ) {
		uprintf(using_msg, "Syslinux");
		r = write_syslinux_mbr(fp);
		goto notify;
	}

	// Grub 2.0
	if ( ((boot_type == BT_IMAGE) && (img_report.has_grub2)) || (boot_type == BT_GRUB2) ) {
		uprintf(using_msg, "Grub 2.0");
		r = write_grub2_mbr(fp);
		goto notify;
	}

	// Grub4DOS
	if ( ((boot_type == BT_IMAGE) && (img_report.has_grub4dos)) || (boot_type == BT_GRUB4DOS) ) {
		uprintf(using_msg, "Grub4DOS");
		r = write_grub4dos_mbr(fp);
		goto notify;
	}

	// ReactOS
	if (boot_type == BT_REACTOS) {
		uprintf(using_msg, "ReactOS");
		r = write_reactos_mbr(fp);
		goto notify;
	}

	// KolibriOS
	if ( (boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report) && (IS_FAT(fs_type))) {
		uprintf(using_msg, "KolibriOS");
		r = write_kolibrios_mbr(fp);
		goto notify;
	}

	// If everything else failed, fall back to a conventional Windows/Rufus MBR
windows_mbr:
	if ((HAS_WINPE(img_report) && !img_report.uses_minint) || (IsChecked(IDC_RUFUS_MBR))) {
		uprintf(using_msg, APPLICATION_NAME);
		r = write_rufus_mbr(fp);
	} else {
		uprintf(using_msg, "Windows 7");
		r = write_win7_mbr(fp);
	}

notify:
	// Tell the system we've updated the disk properties
	if (!DeviceIoControl(hPhysicalDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL))
		uprintf("Failed to notify system about disk properties update: %s\n", WindowsErrorString());

out:
	safe_mm_free(buffer);
	return r;
}

/*
 * Write Secondary Boot Record (usually right after the MBR)
 */
static BOOL WriteSBR(HANDLE hPhysicalDrive)
{
	// TODO: Do we need anything special for 4K sectors?
	DWORD size, max_size, mbr_size = 0x200;
	int r, sub_type = boot_type;
	unsigned char* buf = NULL;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;

	if (partition_type == PARTITION_STYLE_GPT)
		return TRUE;

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);
	// Ensure that we have sufficient space for the SBR
	max_size = IsChecked(IDC_OLD_BIOS_FIXES) ?
		(DWORD)(SelectedDrive.SectorsPerTrack * SelectedDrive.SectorSize) : 1*MB;
	max_size -= mbr_size;
	// Syslinux has precedence over Grub
	if ((boot_type == BT_IMAGE) && (!HAS_SYSLINUX(img_report))) {
		if (img_report.has_grub4dos)
			sub_type = BT_GRUB4DOS;
		if (img_report.has_grub2)
			sub_type = BT_GRUB2;
	}

	switch (sub_type) {
	case BT_GRUB4DOS:
		uprintf("Writing Grub4Dos SBR");
		buf = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_GR_GRUB_GRLDR_MBR), _RT_RCDATA, "grldr.mbr", &size, FALSE);
		if ((buf == NULL) || (size <= mbr_size)) {
			uprintf("grldr.mbr is either not present or too small");
			return FALSE;
		}
		buf = &buf[mbr_size];
		size -= mbr_size;
		break;
	case BT_GRUB2:
		if (grub2_buf != NULL) {
			uprintf("Writing Grub 2.0 SBR (from download) %s",
				IsBufferInDB(grub2_buf, grub2_len)?"✓":"✗");
			buf = grub2_buf;
			size = (DWORD)grub2_len;
		} else {
			uprintf("Writing Grub 2.0 SBR (from embedded)");
			buf = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_GR_GRUB2_CORE_IMG), _RT_RCDATA, "core.img", &size, FALSE);
			if (buf == NULL) {
				uprintf("Could not access core.img");
				return FALSE;
			}
		}
		break;
	default:
		// No need to write secondary block
		return TRUE;
	}

	if (size > max_size) {
		uprintf("  SBR size is too large - You may need to uncheck 'Add fixes for old BIOSes'.");
		return FALSE;
	}
	r = write_data(fp, mbr_size, buf, (uint64_t)size);
	safe_free(grub2_buf);
	return (r != 0);
}

/*
 * Process the Partition Boot Record
 */
static __inline const char* bt_to_name(void) {
	switch (boot_type) {
	case BT_FREEDOS: return "FreeDOS";
	case BT_REACTOS: return "ReactOS";
	default:
		return ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) ? "KolibriOS" : "Standard";
	}
}
static BOOL WritePBR(HANDLE hLogicalVolume)
{
	int i;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	const char* using_msg = "Using %s %s partition boot record";

	fake_fd._handle = (char*)hLogicalVolume;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	switch (actual_fs_type) {
	case FS_FAT16:
		uprintf(using_msg, bt_to_name(), "FAT16");
		if (!is_fat_16_fs(fp)) {
			uprintf("New volume does not have a FAT16 boot sector - aborting");
			break;
		}
		uprintf("Confirmed new volume has a FAT16 boot sector");
		if (boot_type == BT_FREEDOS) {
			if (!write_fat_16_fd_br(fp, 0)) break;
		} else if (boot_type == BT_REACTOS) {
			if (!write_fat_16_ros_br(fp, 0)) break;
		} else if ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
			uprintf("FAT16 is not supported for KolibriOS\n"); break;
		} else {
			if (!write_fat_16_br(fp, 0)) break;
		}
		// Disk Drive ID needs to be corrected on XP
		if (!write_partition_physical_disk_drive_id_fat16(fp))
			break;
		return TRUE;
	case FS_FAT32:
		uprintf(using_msg, bt_to_name(), "FAT32");
		for (i=0; i<2; i++) {
			if (!is_fat_32_fs(fp)) {
				uprintf("New volume does not have a %s FAT32 boot sector - aborting\n", i?"secondary":"primary");
				break;
			}
			uprintf("Confirmed new volume has a %s FAT32 boot sector\n", i?"secondary":"primary");
			uprintf("Setting %s FAT32 boot sector for boot...\n", i?"secondary":"primary");
			if (boot_type == BT_FREEDOS) {
				if (!write_fat_32_fd_br(fp, 0)) break;
			} else if (boot_type == BT_REACTOS) {
				if (!write_fat_32_ros_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
				if (!write_fat_32_kos_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_BOOTMGR(img_report)) {
				if (!write_fat_32_pe_br(fp, 0)) break;
			} else if ((boot_type == BT_IMAGE) && HAS_WINPE(img_report)) {
				if (!write_fat_32_nt_br(fp, 0)) break;
			} else {
				if (!write_fat_32_br(fp, 0)) break;
			}
			// Disk Drive ID needs to be corrected on XP
			if (!write_partition_physical_disk_drive_id_fat32(fp))
				break;
			fake_fd._offset += 6 * SelectedDrive.SectorSize;
		}
		return TRUE;
	case FS_NTFS:
		uprintf(using_msg, bt_to_name(), "NTFS");
		if (!is_ntfs_fs(fp)) {
			uprintf("New volume does not have an NTFS boot sector - aborting\n");
			break;
		}
		uprintf("Confirmed new volume has an NTFS boot sector\n");
		if (!write_ntfs_br(fp)) break;
		// Note: NTFS requires a full remount after writing the PBR. We dismount when we lock
		// and also go through a forced remount, so that shouldn't be an issue.
		// But with NTFS, if you don't remount, you don't boot!
		return TRUE;
	default:
		uprintf("Unsupported FS for FS BR processing - aborting\n");
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
	const char* basedir[3] = { "i386", "amd64", "minint" };
	const char* patch_str_org[2] = { "\\minint\\txtsetup.sif", "\\minint\\system32\\" };
	const char* patch_str_rep[2][2] = { { "\\i386\\txtsetup.sif", "\\i386\\system32\\" } ,
										{ "\\amd64\\txtsetup.sif", "\\amd64\\system32\\" } };
	const char *win_nt_bt_org = "$win_nt$.~bt";
	const char *rdisk_zero = "rdisk(0)";
	const LARGE_INTEGER liZero = { {0, 0} };
	char setupsrcdev[64];
	HANDLE handle = INVALID_HANDLE_VALUE;
	DWORD i, j, size, rw_size, index = 0;
	BOOL r = FALSE;
	char* buffer = NULL;

	if ((img_report.winpe & WINPE_AMD64) == WINPE_AMD64)
		index = 1;
	else if ((img_report.winpe & WINPE_MININT) == WINPE_MININT)
		index = 2;
	// Allow other values than harddisk 1, as per user choice for disk ID
	static_sprintf(setupsrcdev, "SetupSourceDevice = \"\\device\\harddisk%d\\partition1\"",
		ComboBox_GetCurSel(hDiskID));
	// Copy of ntdetect.com in root
	static_sprintf(src, "%c:\\%s\\ntdetect.com", drive_letter, basedir[2*(index/2)]);
	static_sprintf(dst, "%c:\\ntdetect.com", drive_letter);
	CopyFileA(src, dst, TRUE);
	if (!img_report.uses_minint) {
		// Create a copy of txtsetup.sif, as we want to keep the i386/amd64 files unmodified
		static_sprintf(src, "%c:\\%s\\txtsetup.sif", drive_letter, basedir[index]);
		static_sprintf(dst, "%c:\\txtsetup.sif", drive_letter);
		if (!CopyFileA(src, dst, TRUE)) {
			uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
		}
		if (insert_section_data(dst, "[SetupData]", setupsrcdev, FALSE) == NULL) {
			uprintf("Failed to add SetupSourceDevice in %s\n", dst);
			goto out;
		}
		uprintf("Successfully added '%s' to %s\n", setupsrcdev, dst);
	}

	static_sprintf(src, "%c:\\%s\\setupldr.bin", drive_letter,  basedir[2*(index/2)]);
	static_sprintf(dst, "%c:\\BOOTMGR", drive_letter);
	if (!CopyFileA(src, dst, TRUE)) {
		uprintf("Did not copy %s as %s: %s\n", src, dst, WindowsErrorString());
	}

	// \minint with /minint option doesn't require further processing => return true
	// \minint and no \i386 without /minint is unclear => return error
	if (img_report.winpe&WINPE_MININT) {
		if (img_report.uses_minint) {
			uprintf("Detected \\minint directory with /minint option: nothing to patch\n");
			r = TRUE;
		} else if (!(img_report.winpe&(WINPE_I386|WINPE_AMD64))) {
			uprintf("Detected \\minint directory only but no /minint option: not sure what to do\n");
		}
		goto out;
	}

	// At this stage we only handle \i386
	handle = CreateFileA(dst, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open %s for patching: %s\n", dst, WindowsErrorString());
		goto out;
	}
	size = GetFileSize(handle, NULL);
	if (size == INVALID_FILE_SIZE) {
		uprintf("Could not get size for file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	buffer = (char*)malloc(size);
	if (buffer == NULL)
		goto out;
	if ((!ReadFile(handle, buffer, size, &rw_size, NULL)) || (size != rw_size)) {
		uprintf("Could not read file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}
	if (!SetFilePointerEx(handle, liZero, NULL, FILE_BEGIN)) {
		uprintf("Could not rewind file %s: %s\n", dst, WindowsErrorString());
		goto out;
	}

	// Patch setupldr.bin
	uprintf("Patching file %s\n", dst);
	// Remove CRC check for 32 bit part of setupldr.bin from Win2k3
	if ((size > 0x2061) && (buffer[0x2060] == 0x74) && (buffer[0x2061] == 0x03)) {
		buffer[0x2060] = 0xeb;
		buffer[0x2061] = 0x1a;
		uprintf("  0x00002060: 0x74 0x03 -> 0xEB 0x1A (disable Win2k3 CRC check)\n");
	}
	for (i=1; i<size-32; i++) {
		for (j=0; j<ARRAYSIZE(patch_str_org); j++) {
			if (safe_strnicmp(&buffer[i], patch_str_org[j], strlen(patch_str_org[j])-1) == 0) {
				uprintf("  0x%08X: '%s' -> '%s'\n", i, &buffer[i], patch_str_rep[index][j]);
				strcpy(&buffer[i], patch_str_rep[index][j]);
				i += (DWORD)max(strlen(patch_str_org[j]), strlen(patch_str_rep[index][j]));	// in case org is a substring of rep
			}
		}
	}

	if (!img_report.uses_minint) {
		// Additional setupldr.bin/bootmgr patching
		for (i=0; i<size-32; i++) {
			// rdisk(0) -> rdisk(#) disk masquerading
			// NB: only the first one seems to be needed
			if (safe_strnicmp(&buffer[i], rdisk_zero, strlen(rdisk_zero)-1) == 0) {
				buffer[i+6] = 0x30 + ComboBox_GetCurSel(hDiskID);
				uprintf("  0x%08X: '%s' -> 'rdisk(%c)'\n", i, rdisk_zero, buffer[i+6]);
			}
			// $WIN_NT$_~BT -> i386/amd64
			if (safe_strnicmp(&buffer[i], win_nt_bt_org, strlen(win_nt_bt_org)-1) == 0) {
				uprintf("  0x%08X: '%s' -> '%s%s'\n", i, &buffer[i], basedir[index], &buffer[i+strlen(win_nt_bt_org)]);
				strcpy(&buffer[i], basedir[index]);
				// This ensures that we keep the terminator backslash
				buffer[i+strlen(basedir[index])] = buffer[i+strlen(win_nt_bt_org)];
				buffer[i+strlen(basedir[index])+1] = 0;
			}
		}
	}

	if (!WriteFileWithRetry(handle, buffer, size, &rw_size, WRITE_RETRIES)) {
		uprintf("Could not write patched file: %s\n", WindowsErrorString());
		goto out;
	}
	r = TRUE;

out:
	safe_closehandle(handle);
	safe_free(buffer);
	return r;
}

// Checks which versions of Windows are available in an install image
// to set our extraction index. Asks the user to select one if needed.
// Returns -2 on user cancel, -1 on other error, >=0 on success.
int SetWinToGoIndex(void)
{
	char *mounted_iso, *build, image[128];
	char tmp_path[MAX_PATH] = "", xml_file[MAX_PATH] = "";
	char *install_names[MAX_WININST];
	StrArray version_name, version_index;
	int i, build_nr = 0;
	BOOL bNonStandard = FALSE;

	// Sanity checks
	wintogo_index = -1;
	wininst_index = 0;
	if ((nWindowsVersion < WINDOWS_8) || ((WimExtractCheck() & 4) == 0) ||
		(ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)) != FS_NTFS)) {
		return -1;
	}

	// If we have multiple windows install images, ask the user the one to use
	if (img_report.wininst_index > 1) {
		for (i = 0; i < img_report.wininst_index; i++)
			install_names[i] = &img_report.wininst_path[i][2];
		wininst_index = SelectionDialog(lmprintf(MSG_130), lmprintf(MSG_131), install_names, img_report.wininst_index);
		if (wininst_index < 0)
			return -2;
		wininst_index--;
		if ((wininst_index < 0) || (wininst_index >= MAX_WININST))
			wininst_index = 0;
	}

	// Mount the install.wim image, that resides on the ISO
	mounted_iso = MountISO(image_path);
	if (mounted_iso == NULL) {
		uprintf("Could not mount ISO for Windows To Go selection");
		return FALSE;
	}
	static_sprintf(image, "%s%s", mounted_iso, &img_report.wininst_path[wininst_index][2]);

	// Now take a look at the XML file in install.wim to list our versions
	if ((GetTempPathU(sizeof(tmp_path), tmp_path) == 0)
		|| (GetTempFileNameU(tmp_path, APPLICATION_NAME, 0, xml_file) == 0)
		|| (xml_file[0] == 0)) {
		// Last ditch effort to get a tmp file - just extract it to the current directory
		static_strcpy(xml_file, ".\\RufVXml.tmp");
	}
	// GetTempFileName() may leave a file behind
	DeleteFileU(xml_file);

	// Must use the Windows WIM API as 7z messes up the XML
	if (!WimExtractFile_API(image, 0, "[1].xml", xml_file)) {
		uprintf("Could not acquire WIM index");
		goto out;
	}

	StrArrayCreate(&version_name, 16);
	StrArrayCreate(&version_index, 16);
	for (i = 0; StrArrayAdd(&version_index, get_token_data_file_indexed("IMAGE INDEX", xml_file, i + 1), FALSE) >= 0; i++) {
		// Some people are apparently creating *unofficial* Windows ISOs that don't have DISPLAYNAME elements.
		// If we are parsing such an ISO, try to fall back to using DESCRIPTION. Of course, since we don't use
		// a formal XML parser, if an ISO mixes entries with both DISPLAYNAME and DESCRIPTION and others with
		// only DESCRIPTION, the version names we report will be wrong.
		// But hey, there's only so far I'm willing to go to help people who, not content to have demonstrated
		// their utter ignorance on development matters, are also trying to lecture experienced developers
		// about specific "noob mistakes"... that don't exist in the code they are trying to criticize.
		if (StrArrayAdd(&version_name, get_token_data_file_indexed("DISPLAYNAME", xml_file, i + 1), FALSE) < 0) {
			bNonStandard = TRUE;
			if (StrArrayAdd(&version_name, get_token_data_file_indexed("DESCRIPTION", xml_file, i + 1), FALSE) < 0) {
				uprintf("Warning: Could not find a description for image index %d", i + 1);
				StrArrayAdd(&version_name, "Unknown Windows Version", TRUE);
			}
		}
	}
	if (bNonStandard)
		uprintf("Warning: Nonstandard Windows image (missing <DISPLAYNAME> entries)");

	if (i > 1)
		i = SelectionDialog(lmprintf(MSG_291), lmprintf(MSG_292), version_name.String, i);
	if (i < 0) {
		wintogo_index = -2;	// Cancelled by the user
	} else if (i == 0) {
		wintogo_index = 1;
	} else {
		wintogo_index = atoi(version_index.String[i - 1]);
	}
	if (i > 0) {
		// Get the build version
		build = get_token_data_file_indexed("BUILD", xml_file, i);
		if (build != NULL)
			build_nr = atoi(build);
		free(build);
		uprintf("Will use '%s' (Build: %d, Index %s) for Windows To Go",
			version_name.String[i - 1], build_nr, version_index.String[i - 1]);
		// Need Windows 10 Creator Update or later for boot on REMOVABLE to work
		if ((build_nr < 15000) && (SelectedDrive.MediaType != FixedMedia)) {
			if (MessageBoxExU(hMainDialog, lmprintf(MSG_098), lmprintf(MSG_190),
				MB_YESNO | MB_ICONWARNING | MB_IS_RTL, selected_langid) != IDYES)
				wintogo_index = -2;
		}
		// Display a notice about WppRecorder.sys for 1809 ISOs
		if (build_nr == 17763) {
			notification_info more_info;
			more_info.id = MORE_INFO_URL;
			more_info.url = WPPRECORDER_MORE_INFO_URL;
			Notification(MSG_INFO, NULL, &more_info, lmprintf(MSG_128, "Windows To Go"), lmprintf(MSG_133));
		}
	}
	StrArrayDestroy(&version_name);
	StrArrayDestroy(&version_index);

out:
	DeleteFileU(xml_file);
	UnMountISO();
	return wintogo_index;
}

// http://technet.microsoft.com/en-ie/library/jj721578.aspx
// As opposed to the technet guide above, we no longer set internal drives offline,
// due to people wondering why they can't see them by default.
//#define SET_INTERNAL_DRIVES_OFFLINE
static BOOL SetupWinToGo(DWORD DriveIndex, const char* drive_name, BOOL use_esp)
{
#ifdef SET_INTERNAL_DRIVES_OFFLINE
	static char san_policy_path[] = "?:\\san_policy.xml";
#endif
	static char unattend_path[] = "?:\\Windows\\System32\\sysprep\\unattend.xml";
	char *mounted_iso, *ms_efi = NULL, image[128], cmd[MAX_PATH];
	unsigned char *buffer;
	DWORD bufsize;
	ULONG cluster_size;
	FILE* fd;

	uprintf("Windows To Go mode selected");
	// Additional sanity checks
	if ((use_esp) && (SelectedDrive.MediaType != FixedMedia) && (nWindowsBuildNumber < 15000)) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_SUPPORTED;
		return FALSE;
	}

	// First, we need to access the install.wim image, that resides on the ISO
	mounted_iso = MountISO(image_path);
	if (mounted_iso == NULL) {
		uprintf("Could not mount ISO for Windows To Go installation");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
		return FALSE;
	}
	static_sprintf(image, "%s%s", mounted_iso, &img_report.wininst_path[wininst_index][2]);
	uprintf("Mounted ISO as '%s'", mounted_iso);

	// Now we use the WIM API to apply that image
	if (!WimApplyImage(image, wintogo_index, drive_name)) {
		uprintf("Failed to apply Windows To Go image");
		if (!IS_ERROR(FormatStatus))
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
		UnMountISO();
		return FALSE;
	}
	UnMountISO();

	if (use_esp) {
		uprintf("Setting up EFI System Partition");
		// According to Ubuntu (https://bugs.launchpad.net/ubuntu/+source/partman-efi/+bug/811485) you want to use FAT32.
		// However, you have to be careful that the cluster size needs to be greater or equal to the sector size, which
		// in turn has an impact on the minimum EFI partition size we can create (see ms_efi_size_MB in drive.c)
		if (SelectedDrive.SectorSize <= 1024)
			cluster_size = 1024;
		else if (SelectedDrive.SectorSize <= 4096)
			cluster_size = 4096;
		else	// Go for broke
			cluster_size = (ULONG)SelectedDrive.SectorSize;
		// Boy do you *NOT* want to specify a label here, and spend HOURS figuring out why your EFI partition cannot boot...
		// Also, we use the Large FAT32 facility Microsoft APIs are *UTTERLY USELESS* for achieving what we want:
		// VDS cannot list ESP volumes (talk about allegedly improving on the old disk and volume APIs, only to
		// completely neuter it) and IVdsDiskPartitionMF::FormatPartitionEx(), which is what you are supposed to
		// use for ESPs, explicitly states: "This method cannot be used to format removable media."
		if (!FormatPartition(DriveIndex, partition_offset[PI_ESP], cluster_size, FS_FAT32, "",
			FP_QUICK | FP_FORCE | FP_LARGE_FAT32 | FP_NO_BOOT)) {
			uprintf("Could not format EFI System Partition");
			return FALSE;
		}
		Sleep(200);
	}

	if (use_esp) {
		// Need to have the ESP mounted to invoke bcdboot
		ms_efi = AltMountVolume(DriveIndex, partition_offset[PI_ESP], FALSE);
		if (ms_efi == NULL) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_ASSIGN_LETTER);
			return FALSE;
		}
	}

	// We invoke the 'bcdboot' command from the host, as the one from the drive produces problems (#558)
	// Also, since Rufus should (usually) be running as a 32 bit app, on 64 bit systems, we need to use
	// 'C:\Windows\Sysnative' and not 'C:\Windows\System32' to invoke bcdboot, as 'C:\Windows\System32'
	// will get converted to 'C:\Windows\SysWOW64' behind the scenes, and there is no bcdboot.exe there.
	static_sprintf(cmd, "%s\\bcdboot.exe %s\\Windows /v /f %s /s %s", sysnative_dir, drive_name,
		HAS_BOOTMGR_BIOS(img_report) ? (HAS_BOOTMGR_EFI(img_report) ? "ALL" : "BIOS") : "UEFI",
		(use_esp)?ms_efi:drive_name);
	uprintf("Enabling boot using command '%s'", cmd);
	if (RunCommand(cmd, sysnative_dir, usb_debug) != 0) {
		// Try to continue... but report a failure
		uprintf("Failed to enable boot");
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_ISO_EXTRACT);
	}

	if (use_esp) {
		Sleep(200);
		AltUnmountVolume(ms_efi, FALSE);
	}
	UpdateProgressWithInfo(OP_FILE_COPY, MSG_267, wim_proc_files + 2 * wim_extra_files, wim_nb_files);

	// The following are non fatal if they fail

#ifdef SET_INTERNAL_DRIVES_OFFLINE
	uprintf("Applying 'san_policy.xml', to set the target's internal drives offline...");
	buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_TOGO_SAN_POLICY_XML),
		_RT_RCDATA, "san_policy.xml", &bufsize, FALSE);
	san_policy_path[0] = drive_name[0];
	fd = fopenU(san_policy_path, "wb");
	if ((fd == NULL) || (fwrite(buffer, 1, bufsize, fd) != bufsize)) {
		uprintf("Could not write '%s'\n", san_policy_path);
		if (fd)
			fclose(fd);
	} else {
		fclose(fd);
		// Can't use the one from the USB (at least for Windows 10 preview), as you'll get
		// "Error: 0x800401f0  An error occurred while initializing COM security".
		// On the other hand, using Windows 8.1 dism against Windows 10 doesn't work either
		// (you get a message about needing to upgrade to latest AIK)...
		static_sprintf(cmd, "dism /Image:%s\\ /Apply-Unattend:%s", drive_name, san_policy_path);
		if (RunCommand(cmd, NULL, TRUE) != 0)
			uprintf("Command '%s' failed to run", cmd);
	}
#endif

	uprintf("Copying 'unattend.xml', to disable the use of the Windows Recovery Environment...");
	buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_TOGO_UNATTEND_XML),
		_RT_RCDATA, "unattend.xml", &bufsize, FALSE);
	unattend_path[0] = drive_name[0];
	fd = fopenU(unattend_path, "wb");
	if ((fd == NULL) || (fwrite(buffer, 1, bufsize, fd) != bufsize))
		uprintf("Could not write '%s'", unattend_path);
	if (fd != NULL)
		fclose(fd);
	UpdateProgressWithInfo(OP_FILE_COPY, MSG_267, wim_nb_files, wim_nb_files);

	return TRUE;
}

static void update_progress(const uint64_t processed_bytes)
{
	UpdateProgressWithInfo(OP_FORMAT, MSG_261, processed_bytes, img_report.image_size);
}

/* Write an image file or zero a drive */
static BOOL WriteDrive(HANDLE hPhysicalDrive, HANDLE hSourceImage)
{
	BOOL s, ret = FALSE;
	LARGE_INTEGER li;
	DWORD rSize, wSize, xSize, BufSize;
	uint64_t wb, target_size = hSourceImage?img_report.image_size:SelectedDrive.DiskSize;
	int64_t bled_ret;
	uint8_t *buffer = NULL;
	uint8_t *cmp_buffer = NULL;
	int i, *ptr, zero_data, throttle_fast_zeroing = 0;

	// We poked the MBR and other stuff, so we need to rewind
	li.QuadPart = 0;
	if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN))
		uprintf("Warning: Unable to rewind image position - wrong data might be copied!");
	UpdateProgressWithInfoInit(NULL, FALSE);

	if (img_report.compression_type != BLED_COMPRESSION_NONE) {
		uprintf("Writing compressed image...");
		bled_init(_uprintf, update_progress, &FormatStatus);
		bled_ret = bled_uncompress_with_handles(hSourceImage, hPhysicalDrive, img_report.compression_type);
		bled_exit();
		if ((bled_ret < 0) && (SCODE_CODE(FormatStatus) != ERROR_CANCELLED)) {
			// Unfortunately, different compression backends return different negative error codes
			uprintf("Could not write compressed image: %lld", bled_ret);
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_WRITE_FAULT;
			goto out;
		}
	} else {
		uprintf(hSourceImage?"Writing Image...":fast_zeroing?"Fast-zeroing drive...":"Zeroing drive...");
		// Our buffer size must be a multiple of the sector size and *ALIGNED* to the sector size
		BufSize = ((DD_BUFFER_SIZE + SelectedDrive.SectorSize - 1) / SelectedDrive.SectorSize) * SelectedDrive.SectorSize;
		buffer = (uint8_t*)_mm_malloc(BufSize, SelectedDrive.SectorSize);
		if (buffer == NULL) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_NOT_ENOUGH_MEMORY;
			uprintf("Could not allocate disk write buffer");
			goto out;
		}
		assert((uintptr_t)buffer % SelectedDrive.SectorSize == 0);

		// Clear buffer
		memset(buffer, fast_zeroing ? 0xff : 0x00, BufSize);

		cmp_buffer = (uint8_t*)_mm_malloc(BufSize, SelectedDrive.SectorSize);
		if (cmp_buffer == NULL) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_NOT_ENOUGH_MEMORY;
			uprintf("Could not allocate disk comparison buffer");
			goto out;
		}
		assert((uintptr_t)cmp_buffer % SelectedDrive.SectorSize == 0);

		// Don't bother trying for something clever, using double buffering overlapped and whatnot:
		// With Windows' default optimizations, sync read + sync write for sequential operations
		// will be as fast, if not faster, than whatever async scheme you can come up with.
		rSize = BufSize;
		for (wb = 0, wSize = 0; wb < (uint64_t)SelectedDrive.DiskSize; wb += wSize) {
			UpdateProgressWithInfo(OP_FORMAT, hSourceImage ? MSG_261 : fast_zeroing ? MSG_306 : MSG_286, wb, target_size);
			if (hSourceImage != NULL) {
				s = ReadFile(hSourceImage, buffer, BufSize, &rSize, NULL);
				if (!s) {
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_READ_FAULT;
					uprintf("Read error: %s", WindowsErrorString());
					goto out;
				}
				if (rSize == 0)
					break;
			}
			// Don't overflow our projected size (mostly for VHDs)
			if (wb + rSize > target_size) {
				rSize = (DWORD)(target_size - wb);
			}

			// WriteFile fails unless the size is a multiple of sector size
			if (rSize % SelectedDrive.SectorSize != 0)
				rSize = ((rSize + SelectedDrive.SectorSize - 1) / SelectedDrive.SectorSize) * SelectedDrive.SectorSize;

			// Fast-zeroing: Depending on your hardware, reading from flash may be much faster than writing, so
			// we might speed things up by skipping empty blocks, or skipping the write if the data is the same.
			// Notes: A block is declared empty when all bits are either 0 (zeros) or 1 (flash block erased).
			// Also, a back-off strategy is used to limit reading.
			if (throttle_fast_zeroing) {
				throttle_fast_zeroing--;
			} else if (fast_zeroing) {
				assert(hSourceImage == NULL);	// Only enabled for zeroing
				CHECK_FOR_USER_CANCEL;

				// Read block and compare against the block that needs to be written
				s = ReadFile(hPhysicalDrive, cmp_buffer, rSize, &xSize, NULL);
				if ((!s) || (xSize != rSize) ) {
					uprintf("Read error: Could not read data for comparison - %s", WindowsErrorString());
					goto out;
				}

				// Check for an empty block
				ptr = (int*)(cmp_buffer);
				// Get first element
				zero_data = ptr[0];
				// Check all bits are the same
				if ((zero_data == 0) || (zero_data == -1)) {
					// Compare the rest of the block against the first element
					for (i = 1; i < (int)(rSize / sizeof(int)); i++) {
						if (ptr[i] != zero_data)
							break;
					}
					if (i >= (int)(rSize / sizeof(int))) {
						// Block is empty, skip write
						wSize = rSize;
						continue;
					}
				}

				// Move the file pointer position back for writing
				li.QuadPart = wb;
				if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN)) {
					uprintf("Error: Could not reset position - %s", WindowsErrorString());
					goto out;
				}
				// Throttle read operations
				throttle_fast_zeroing = 15;
			}

			for (i = 1; i <= WRITE_RETRIES; i++) {
				CHECK_FOR_USER_CANCEL;
				s = WriteFile(hPhysicalDrive, buffer, rSize, &wSize, NULL);
				if ((s) && (wSize == rSize))
					break;
				if (s)
					uprintf("Write error: Wrote %d bytes, expected %d bytes", wSize, rSize);
				else
					uprintf("Write error at sector %lld: %s", wb / SelectedDrive.SectorSize, WindowsErrorString());
				if (i < WRITE_RETRIES) {
					li.QuadPart = wb;
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(WRITE_TIMEOUT);
					if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN)) {
						uprintf("Write error: Could not reset position - %s", WindowsErrorString());
						goto out;
					}
				} else {
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_WRITE_FAULT;
					goto out;
				}
				Sleep(200);
			}
			if (i > WRITE_RETRIES)
				goto out;
		}
	}
	RefreshDriveLayout(hPhysicalDrive);
	ret = TRUE;
out:
	safe_mm_free(buffer);
	safe_mm_free(cmp_buffer);
	return ret;
}

/*
 * Standalone thread for the formatting operation
 * According to http://msdn.microsoft.com/en-us/library/windows/desktop/aa364562.aspx
 * To change a volume file system
 *   Open a volume.
 *   Lock the volume.
 *   Format the volume.
 *   Dismount the volume.
 *   Unlock the volume.
 *   Close the volume handle.
 */
DWORD WINAPI FormatThread(void* param)
{
	int i, r;
	BOOL ret, use_large_fat32, windows_to_go;
	DWORD DriveIndex = (DWORD)(uintptr_t)param, ClusterSize, Flags;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	HANDLE hSourceImage = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	FILE* log_fd;
	uint8_t *buffer = NULL, extra_partitions = 0;
	char *bb_msg, *volume_name = NULL;
	char drive_name[] = "?:\\";
	char drive_letters[27], fs_name[32], label[64];
	char logfile[MAX_PATH], *userdir;
	char efi_dst[] = "?:\\efi\\boot\\bootx64.efi";
	char kolibri_dst[] = "?:\\MTLD_F32";
	char grub4dos_dst[] = "?:\\grldr";

	use_large_fat32 = (fs_type == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32));
	windows_to_go = (image_options & IMOP_WINTOGO) && (boot_type == BT_IMAGE) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1);
	large_drive = (SelectedDrive.DiskSize > (1*TB));
	if (large_drive)
		uprintf("Notice: Large drive detected (may produce short writes)");
	// Find out if we need to add any extra partitions
	if ((windows_to_go) && (target_type == TT_UEFI) && (partition_type == PARTITION_STYLE_GPT))
		// According to Microsoft, every GPT disk (we RUN Windows from) must have an MSR due to not having hidden sectors
		// http://msdn.microsoft.com/en-us/library/windows/hardware/dn640535.aspx#gpt_faq_what_disk_require_msr
		extra_partitions = XP_ESP | XP_MSR;
	else if ( (fs_type == FS_NTFS) && ((boot_type == BT_UEFI_NTFS) ||
			  ((boot_type == BT_IMAGE) && IS_EFI_BOOTABLE(img_report) && ((target_type == TT_UEFI) || (windows_to_go) || (allow_dual_uefi_bios)))) )
		extra_partitions = XP_UEFI_NTFS;
	else if ((boot_type == BT_IMAGE) && !write_as_image && HAS_PERSISTENCE(img_report) && persistence_size)
		extra_partitions = XP_CASPER;
	else if (IsChecked(IDC_OLD_BIOS_FIXES))
		extra_partitions = XP_COMPAT;

	PrintInfoDebug(0, MSG_225);
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, lock_drive, FALSE, !lock_drive);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}

	// At this stage we have both a handle and a lock to the physical drive
	if (!GetDriveLetters(DriveIndex, drive_letters)) {
		uprintf("Failed to get a drive letter");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
		goto out;
	}
	if (drive_letters[0] == 0) {
		uprintf("No drive letter was assigned...");
		drive_name[0] =  GetUnusedDriveLetter();
		if (drive_name[0] == 0) {
			uprintf("Could not find a suitable drive letter");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
			goto out;
		}
	} else {
		// Unmount all mounted volumes that belong to this drive
		// Do it in reverse so that we always end on the first volume letter
		for (i = (int)safe_strlen(drive_letters); i > 0; i--) {
			drive_name[0] = drive_letters[i-1];
			if (boot_type == BT_IMAGE) {
				// If we are using an image, check that it isn't located on the drive we are trying to format
				if ((PathGetDriveNumberU(image_path) + 'A') == drive_letters[i-1]) {
					uprintf("ABORTED: Cannot use an image that is located on the target drive!");
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
					goto out;
				}
			}
			if (!DeleteVolumeMountPointA(drive_name)) {
				uprintf("Failed to delete mountpoint %s: %s", drive_name, WindowsErrorString());
				// Try to continue. We will bail out if this causes an issue.
			}
		}
	}
	uprintf("Will use '%C:' as volume mountpoint", drive_name[0]);

	// It kind of blows, but we have to relinquish access to the physical drive
	// for VDS to be able to delete the partitions that reside on it...
	safe_unlockclose(hPhysicalDrive);
	PrintInfoDebug(0, MSG_239);
	if (!DeletePartitions(DriveIndex)) {
		SetLastError(FormatStatus);
		uprintf("Notice: Could not delete partitions: %s", WindowsErrorString());
		FormatStatus = 0;
	}

	// Now get RW access to the physical drive...
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, lock_drive, TRUE, !lock_drive);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
		goto out;
	}
	RefreshDriveLayout(hPhysicalDrive);

	// ...and get a lock to the logical drive so that we can actually write something
	hLogicalVolume = GetLogicalHandle(DriveIndex, 0, TRUE, FALSE, !lock_drive);
	if (hLogicalVolume == INVALID_HANDLE_VALUE) {
		uprintf("Could not lock volume");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	} else if (hLogicalVolume == NULL) {
		// NULL is returned for cases where the drive is not yet partitioned
		uprintf("Drive does not appear to be partitioned");
	} else if (!UnmountVolume(hLogicalVolume)) {
		uprintf("Trying to continue regardless...");
	}
	CHECK_FOR_USER_CANCEL;

	if (!zero_drive && !write_as_image) {
		PrintInfoDebug(0, MSG_226);
		AnalyzeMBR(hPhysicalDrive, "Drive", FALSE);
		UpdateProgress(OP_ANALYZE_MBR, -1.0f);
	}

	if (zero_drive) {
		WriteDrive(hPhysicalDrive, NULL);
		goto out;
	}

	// Zap partition records. This helps prevent access errors.
	// Note, Microsoft's way of cleaning partitions (IOCTL_DISK_CREATE_DISK, which is what we apply
	// in InitializeDisk) is *NOT ENOUGH* to reset a disk and can render it inoperable for partitioning
	// or formatting under Windows. See https://github.com/pbatard/rufus/issues/759 for details.
	if ((boot_type != BT_IMAGE) || (img_report.is_iso && !write_as_image)) {
		if ((!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) ||
			(!InitializeDisk(hPhysicalDrive))) {
			uprintf("Could not reset partitions");
			FormatStatus = (LastWriteError != 0) ? LastWriteError : (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE);
			goto out;
		}
	}

	if (IsChecked(IDC_BAD_BLOCKS)) {
		do {
			int sel = ComboBox_GetCurSel(hNBPasses);
			// create a log file for bad blocks report. Since %USERPROFILE% may
			// have localized characters, we use the UTF-8 API.
			userdir = getenvU("USERPROFILE");
			static_strcpy(logfile, userdir);
			safe_free(userdir);
			GetLocalTime(&lt);
			safe_sprintf(&logfile[strlen(logfile)], sizeof(logfile)-strlen(logfile)-1,
				"\\rufus_%04d%02d%02d_%02d%02d%02d.log",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
			log_fd = fopenU(logfile, "w+");
			if (log_fd == NULL) {
				uprintf("Could not create log file for bad blocks check");
			} else {
				fprintf(log_fd, APPLICATION_NAME " bad blocks check started on: %04d.%02d.%02d %02d:%02d:%02d",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fflush(log_fd);
			}

			if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize, (sel >= 2) ? 4 : sel +1,
				(sel < 2) ? 0 : sel - 2, &report, log_fd)) {
				uprintf("Bad blocks: Check failed.");
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_BADBLOCKS_FAILURE);
				ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, FALSE);
				fclose(log_fd);
				DeleteFileU(logfile);
				goto out;
			}
			uprintf("Bad Blocks: Check completed, %d bad block%s found. (%d/%d/%d errors)",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
			r = IDOK;
			if (report.bb_count) {
				bb_msg = lmprintf(MSG_011, report.bb_count, report.num_read_errors, report.num_write_errors,
					report.num_corruption_errors);
				fprintf(log_fd, bb_msg);
				GetLocalTime(&lt);
				fprintf(log_fd, APPLICATION_NAME " bad blocks check ended on: %04d.%02d.%02d %02d:%02d:%02d",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fclose(log_fd);
				r = MessageBoxExU(hMainDialog, lmprintf(MSG_012, bb_msg, logfile),
					lmprintf(MSG_010), MB_ABORTRETRYIGNORE|MB_ICONWARNING|MB_IS_RTL, selected_langid);
			} else {
				// We didn't get any errors => delete the log file
				fclose(log_fd);
				DeleteFileU(logfile);
			}
		} while (r == IDRETRY);
		if (r == IDABORT) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			goto out;
		}

		// Especially after destructive badblocks test, you must zero the MBR/GPT completely
		// before repartitioning. Else, all kind of bad things happen.
		if (!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) {
			uprintf("unable to zero MBR/GPT");
			if (!IS_ERROR(FormatStatus))
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
	}

	// Write an image file
	if ((boot_type == BT_IMAGE) && write_as_image) {
		hSourceImage = CreateFileU(image_path, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (hSourceImage == INVALID_HANDLE_VALUE) {
			uprintf("Could not open image '%s': %s", image_path, WindowsErrorString());
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
			goto out;
		}

		WriteDrive(hPhysicalDrive, hSourceImage);

		// If the image contains a partition we might be able to access, try to re-mount it
		safe_unlockclose(hPhysicalDrive);
		safe_unlockclose(hLogicalVolume);
		Sleep(200);
		WaitForLogical(DriveIndex, 0);
		if (GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_name, sizeof(fs_name), TRUE)) {
			volume_name = GetLogicalName(DriveIndex, 0, TRUE, TRUE);
			if ((volume_name != NULL) && (MountVolume(drive_name, volume_name)))
				uprintf("Remounted %s as %C:", volume_name, drive_name[0]);
		}
		goto out;
	}

	UpdateProgress(OP_ZERO_MBR, -1.0f);
	CHECK_FOR_USER_CANCEL;

	if (!CreatePartition(hPhysicalDrive, partition_type, fs_type, (partition_type==PARTITION_STYLE_MBR) && (target_type==TT_UEFI), extra_partitions)) {
		FormatStatus = (LastWriteError != 0) ? LastWriteError : (ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE);
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Close the (unmounted) volume before formatting
	if ((hLogicalVolume != NULL) && (hLogicalVolume != INVALID_HANDLE_VALUE)) {
		PrintInfoDebug(0, MSG_227);
		if (!CloseHandle(hLogicalVolume)) {
			uprintf("Could not close volume: %s", WindowsErrorString());
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
			goto out;
		}
	}
	hLogicalVolume = INVALID_HANDLE_VALUE;

	// VDS wants us to unlock the phys
	if (use_vds) {
		safe_unlockclose(hPhysicalDrive);
		uprintf("Refreshing drive layout...");
#if 0
		// **DON'T USE** This may leave the device disabled on re-plug or reboot
		DWORD cr = CycleDevice(ComboBox_GetCurSel(hDeviceList));
		if (cr == ERROR_DEVICE_REINITIALIZATION_NEEDED) {
			uprintf("Zombie device detected, trying again...");
			Sleep(1000);
			cr = CycleDevice(ComboBox_GetCurSel(hDeviceList));
		}
		if (cr == 0)
			uprintf("Successfully cycled device");
		else
			uprintf("Cycling device failed!");
#endif
		RefreshLayout(DriveIndex);
	}

	// Wait for the logical drive we just created to appear
	uprintf("Waiting for logical drive to reappear...");
	Sleep(200);
	if (!WaitForLogical(DriveIndex, partition_offset[PI_MAIN])) {
		uprintf("Logical drive was not found - aborting");
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_NO_VOLUME_ID;
		goto out;
	}
	CHECK_FOR_USER_CANCEL;

	// Format Casper partition if required. Do it before we format anything with
	// a file system that Windows will recognize, to avoid concurrent access.
	if (extra_partitions & XP_CASPER) {
		uint32_t ext_version = ReadSetting32(SETTING_USE_EXT_VERSION);
		if ((ext_version < 2) || (ext_version > 4))
			ext_version = 3;
		uprintf("Using %s-like method to enable persistence", img_report.uses_casper ? "Ubuntu" : "Debian");
		if (!FormatPartition(DriveIndex, partition_offset[PI_CASPER], 0, FS_EXT2 + (ext_version - 2),
			img_report.uses_casper ? "casper-rw" : "persistence",
			(img_report.uses_casper ? 0 : FP_CREATE_PERSISTENCE_CONF) |
			(IsChecked(IDC_QUICK_FORMAT) ? FP_QUICK : 0))) {
			if (!IS_ERROR(FormatStatus))
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
	}

	GetWindowTextU(hLabel, label, sizeof(label));
	if (fs_type < FS_EXT2)
		ToValidLabel(label, (fs_type == FS_FAT16) || (fs_type == FS_FAT32) || (fs_type == FS_EXFAT));
	ClusterSize = (DWORD)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize));
	if (ClusterSize < 0x200)
		ClusterSize = 0;	// 0 = default cluster size

	Flags = FP_FORCE;
	if (IsChecked(IDC_QUICK_FORMAT))
		Flags |= FP_QUICK;
	if ((fs_type == FS_NTFS) && (enable_ntfs_compression))
		Flags |= FP_COMPRESSION;

	ret = FormatPartition(DriveIndex, partition_offset[PI_MAIN], ClusterSize, fs_type, label, Flags);
	if (!ret) {
		// Error will be set by FormatPartition() in FormatStatus
		uprintf("Format error: %s", StrError(FormatStatus, TRUE));
		goto out;
	}

	if (use_vds) {
		// Get RW access back to the physical drive...
		hPhysicalDrive = GetPhysicalHandle(DriveIndex, lock_drive, TRUE, !lock_drive);
		if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
			goto out;
		}
	}

	// Thanks to Microsoft, we must fix the MBR AFTER the drive has been formatted
	if ((partition_type == PARTITION_STYLE_MBR) || ((boot_type != BT_NON_BOOTABLE) && (partition_type == PARTITION_STYLE_GPT))) {
		PrintInfoDebug(0, MSG_228);	// "Writing master boot record..."
		if ((!WriteMBR(hPhysicalDrive)) || (!WriteSBR(hPhysicalDrive))) {
			if (!IS_ERROR(FormatStatus))
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
		UpdateProgress(OP_FIX_MBR, -1.0f);
	}
	Sleep(200);
	WaitForLogical(DriveIndex, 0);
	// Try to continue
	CHECK_FOR_USER_CANCEL;

	volume_name = GetLogicalName(DriveIndex, partition_offset[PI_MAIN], TRUE, TRUE);
	if (volume_name == NULL) {
		uprintf("Could not get volume name");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_VOLUME_ID;
		goto out;
	}
	uprintf("Found volume %s", volume_name);

	if (!MountVolume(drive_name, volume_name)) {
		uprintf("Could not remount %s as %C: %s\n", volume_name, drive_name[0], WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_MOUNT_VOLUME);
		goto out;
	}
	CHECK_FOR_USER_CANCEL;

	// Disable file indexing, unless it was force-enabled by the user
	if ((!enable_file_indexing) && ((fs_type == FS_NTFS) || (fs_type == FS_UDF) || (fs_type == FS_REFS))) {
		uprintf("Disabling file indexing...");
		if (!SetFileAttributesA(volume_name, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED))
			uprintf("Could not disable file indexing: %s", WindowsErrorString());
	}

	// Refresh the drive label - This is needed as Windows may have altered it from
	// the name we proposed, and we require an exact label, to patch config files.
	if ((fs_type < FS_EXT2) && !GetVolumeInformationU(drive_name, img_report.usb_label,
		ARRAYSIZE(img_report.usb_label), NULL, NULL, NULL, NULL, 0)) {
		uprintf("Warning: Failed to refresh label: %s", WindowsErrorString());
	} else if ((fs_type >= FS_EXT2) && (fs_type <= FS_EXT4)) {
		const char* ext_label = GetExtFsLabel(DriveIndex, 0);
		if (ext_label != NULL)
			static_strcpy(img_report.usb_label, label);
	}

	if (boot_type != BT_NON_BOOTABLE) {
		if (boot_type == BT_UEFI_NTFS) {
			// All good
		} else if (target_type == TT_UEFI) {
			// For once, no need to do anything - just check our sanity
			assert((boot_type == BT_IMAGE) && IS_EFI_BOOTABLE(img_report) && (fs_type <= FS_NTFS));
			if ( (boot_type != BT_IMAGE) || !IS_EFI_BOOTABLE(img_report) || (fs_type > FS_NTFS) ) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
		} else if ( (boot_type == BT_SYSLINUX_V4) || (boot_type == BT_SYSLINUX_V6) ||
			((boot_type == BT_IMAGE) && (HAS_SYSLINUX(img_report) || HAS_REACTOS(img_report)) &&
				(!HAS_WINDOWS(img_report) || !allow_dual_uefi_bios)) ) {
			if (!InstallSyslinux(DriveIndex, drive_name[0], fs_type)) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
		} else {
			// We still have a lock, which we need to modify the volume boot record
			// => no need to reacquire the lock...
			hLogicalVolume = GetLogicalHandle(DriveIndex, partition_offset[PI_MAIN], FALSE, TRUE, FALSE);
			if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL)) {
				uprintf("Could not re-mount volume for partition boot record access");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
				goto out;
			}
			// NB: if you unmount the logical volume here, XP will report error:
			// [0x00000456] The media in the drive may have changed
			PrintInfoDebug(0, MSG_229);
			if (!WritePBR(hLogicalVolume)) {
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
				goto out;
			}
			// We must close and unlock the volume to write files to it
			safe_unlockclose(hLogicalVolume);
		}
	} else {
		if (IsChecked(IDC_EXTENDED_LABEL))
			SetAutorun(drive_name);
	}
	CHECK_FOR_USER_CANCEL;

	// We issue a complete remount of the filesystem at on account of:
	// - Ensuring the file explorer properly detects that the volume was updated
	// - Ensuring that an NTFS system will be reparsed so that it becomes bootable
	if (!RemountVolume(drive_name))
		goto out;
	CHECK_FOR_USER_CANCEL;

	if (boot_type != BT_NON_BOOTABLE) {
		if ((boot_type == BT_MSDOS) || (boot_type == BT_FREEDOS)) {
			UpdateProgress(OP_FILE_COPY, -1.0f);
			PrintInfoDebug(0, MSG_230);
			if (!ExtractDOS(drive_name)) {
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
				goto out;
			}
		} else if (boot_type == BT_GRUB4DOS) {
			grub4dos_dst[0] = drive_name[0];
			IGNORE_RETVAL(_chdirU(app_dir));
			uprintf("Installing: %s (Grub4DOS loader) %s", grub4dos_dst,
				IsFileInDB(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr")?"✓":"✗");
			if (!CopyFileU(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr", grub4dos_dst, FALSE))
				uprintf("Failed to copy file: %s", WindowsErrorString());
		} else if ((boot_type == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso)) {
			UpdateProgress(OP_FILE_COPY, 0.0f);
			drive_name[2] = 0;	// Ensure our drive is something like 'D:'
			if (windows_to_go) {
				PrintInfoDebug(0, MSG_268);
				if (!SetupWinToGo(DriveIndex, drive_name, (extra_partitions & XP_ESP))) {
					if (!IS_ERROR(FormatStatus))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
					goto out;
				}
			} else {
				if (!ExtractISO(image_path, drive_name, FALSE)) {
					if (!IS_ERROR(FormatStatus))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
					goto out;
				}
				if (HAS_KOLIBRIOS(img_report)) {
					kolibri_dst[0] = drive_name[0];
					uprintf("Installing: %s (KolibriOS loader)", kolibri_dst);
					if (ExtractISOFile(image_path, "HD_Load/USB_Boot/MTLD_F32", kolibri_dst,
						FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM) == 0) {
						uprintf("Warning: loader installation failed - KolibriOS will not boot!");
					}
				}
				// EFI mode selected, with no 'boot###.efi' but Windows 7 x64's 'bootmgr.efi' (bit #0)
				if (((target_type == TT_UEFI) || allow_dual_uefi_bios) && HAS_WIN7_EFI(img_report)) {
					PrintInfoDebug(0, MSG_232);
					img_report.wininst_path[0][0] = drive_name[0];
					efi_dst[0] = drive_name[0];
					efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = 0;
					if (!CreateDirectoryA(efi_dst, 0)) {
						uprintf("Could not create directory '%s': %s", efi_dst, WindowsErrorString());
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
					} else {
						efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = '\\';
						if (!WimExtractFile(img_report.wininst_path[0], 1, "Windows\\Boot\\EFI\\bootmgfw.efi", efi_dst)) {
							uprintf("Failed to setup Win7 EFI boot");
							FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
						}
					}
				}
				if ( (target_type == TT_BIOS) && HAS_WINPE(img_report) ) {
					// Apply WinPe fixup
					if (!SetupWinPE(drive_name[0]))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
				}
			}
		}
		UpdateProgress(OP_FINALIZE, -1.0f);
		PrintInfoDebug(0, MSG_233);
		if (IsChecked(IDC_EXTENDED_LABEL))
			SetAutorun(drive_name);
		// Issue another complete remount before we exit, to ensure we're clean
		RemountVolume(drive_name);
		// NTFS fixup (WinPE/AIK images don't seem to boot without an extra checkdisk)
		if ((boot_type == BT_IMAGE) && (img_report.is_iso) && (fs_type == FS_NTFS)) {
			// Try to ensure that all messages from Checkdisk will be in English
			if (PRIMARYLANGID(GetThreadUILanguage()) != LANG_ENGLISH) {
				SetThreadUILanguage(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
				if (PRIMARYLANGID(GetThreadUILanguage()) != LANG_ENGLISH)
					uprintf("Note: CheckDisk messages may be localized");
			}
			CheckDisk(drive_name[0]);
			UpdateProgress(OP_FINALIZE, -1.0f);
		}
	}

out:
	zero_drive = FALSE;
	safe_free(volume_name);
	safe_free(buffer);
	safe_closehandle(hSourceImage);
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);	// This can take a while
	if (IS_ERROR(FormatStatus)) {
		volume_name = GetLogicalName(DriveIndex, 0, TRUE, FALSE);
		if (volume_name != NULL) {
			if (MountVolume(drive_name, volume_name))
				uprintf("Re-mounted volume as %C: after error", drive_name[0]);
			free(volume_name);
		}
	}
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	ExitThread(0);
}

DWORD WINAPI SaveImageThread(void* param)
{
	BOOL s;
	DWORD rSize, wSize;
	IMG_SAVE *img_save = (IMG_SAVE*)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hDestImage = INVALID_HANDLE_VALUE;
	LARGE_INTEGER li;
	uint8_t *buffer = NULL;
	uint64_t wb;
	int i;

	PrintInfoDebug(0, MSG_225);
	switch (img_save->Type) {
	case IMG_SAVE_TYPE_VHD:
		hPhysicalDrive = GetPhysicalHandle(img_save->DeviceNum, TRUE, FALSE, FALSE);
		break;
	case IMG_SAVE_TYPE_ISO:
		hPhysicalDrive = CreateFileA(img_save->DevicePath, GENERIC_READ, FILE_SHARE_READ,
			NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		break;
	default:
		uprintf("Invalid image type");
	}
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}

	// Write an image file
	// We may have poked the MBR and other stuff, so need to rewind
	li.QuadPart = 0;
	if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN))
		uprintf("Warning: Unable to rewind device position - wrong data might be copied!");
	hDestImage = CreateFileU(img_save->ImagePath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDestImage == INVALID_HANDLE_VALUE) {
		uprintf("Could not open image '%s': %s", img_save->ImagePath, WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}

	buffer = (uint8_t*)_mm_malloc(img_save->BufSize, 16);
	if (buffer == NULL) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		uprintf("could not allocate buffer");
		goto out;
	}

	uprintf("Will use a buffer size of %s", SizeToHumanReadable(img_save->BufSize, FALSE, FALSE));
	uprintf("Saving to image '%s'...", img_save->ImagePath);

	// Don't bother trying for something clever, using double buffering overlapped and whatnot:
	// With Windows' default optimizations, sync read + sync write for sequential operations
	// will be as fast, if not faster, than whatever async scheme you can come up with.
	UpdateProgressWithInfoInit(NULL, FALSE);
	for (wb = 0; ; wb += wSize) {
		if (img_save->Type == IMG_SAVE_TYPE_ISO) {
			// Optical drives do not appear to increment the sectors to read automatically
			li.QuadPart = wb;
			if (!SetFilePointerEx(hPhysicalDrive, li, NULL, FILE_BEGIN))
				uprintf("Warning: Unable to set device position - wrong data might be copied!");
		}
		s = ReadFile(hPhysicalDrive, buffer,
			(DWORD)MIN(img_save->BufSize, img_save->DeviceSize - wb), &rSize, NULL);
		if (!s) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
			uprintf("Read error: %s", WindowsErrorString());
			goto out;
		}
		if (rSize == 0)
			break;
		UpdateProgressWithInfo(OP_FORMAT, MSG_261, wb, img_save->DeviceSize);
		for (i = 1; i <= WRITE_RETRIES; i++) {
			CHECK_FOR_USER_CANCEL;
			s = WriteFile(hDestImage, buffer, rSize, &wSize, NULL);
			if ((s) && (wSize == rSize))
				break;
			if (s)
				uprintf("Write error: Wrote %d bytes, expected %d bytes", wSize, rSize);
			else
				uprintf("Write error: %s", WindowsErrorString());
			if (i < WRITE_RETRIES) {
				li.QuadPart = wb;
				uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
				Sleep(WRITE_TIMEOUT);
				if (!SetFilePointerEx(hDestImage, li, NULL, FILE_BEGIN)) {
					uprintf("Write error: Could not reset position - %s", WindowsErrorString());
					goto out;
				}
			} else {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
				goto out;
			}
			Sleep(200);
		}
		if (i > WRITE_RETRIES)
			goto out;
	}
	if (wb != img_save->DeviceSize) {
		uprintf("Error: wrote %s, expected %s", SizeToHumanReadable(wb, FALSE, FALSE),
			SizeToHumanReadable(img_save->DeviceSize, FALSE, FALSE));
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}
	if (img_save->Type == IMG_SAVE_TYPE_VHD) {
		uprintf("Appending VHD footer...");
		if (!AppendVHDFooter(img_save->ImagePath)) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
	}
	uprintf("Operation complete (Wrote %s).", SizeToHumanReadable(wb, FALSE, FALSE));

out:
	safe_free(img_save->ImagePath);
	safe_mm_free(buffer);
	safe_closehandle(hDestImage);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	ExitThread(0);
}
