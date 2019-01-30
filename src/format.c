/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright © 2007-2009 Tom Thornhill/Ridgecrop
 * Copyright © 2011-2018 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"
#include "missing.h"
#include "resource.h"
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
DWORD FormatStatus = 0;
badblocks_report report = { 0 };
static uint64_t LastRefresh = 0;
static float format_percent = 0.0f;
static int task_number = 0;
extern const int nb_steps[FS_MAX];
extern uint32_t dur_mins, dur_secs;
static int fs_index = 0, wintogo_index = -1, wininst_index = 0;
extern BOOL force_large_fat32, enable_ntfs_compression, lock_drive, zero_drive, fast_zeroing, enable_file_indexing, write_as_image;
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

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)pData;
		PrintInfo(0, MSG_217, 1.0f * (*percent));
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		if (task_number < nb_steps[fs_index] - 1) {
			if (task_number == 0)
				uprintf("Creating file system...");
			PrintInfo(0, MSG_218, ++task_number, nb_steps[fs_index]);
			format_percent += 100.0f / (1.0f * nb_steps[fs_index]);
			UpdateProgress(OP_CREATE_FS, format_percent);
		}
		break;
	case FCC_DONE:
		PrintInfo(0, MSG_218, nb_steps[fs_index], nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
		break;
	case FCC_DONE_WITH_STRUCTURE:	// We get this message when formatting Small FAT16
		// pData Seems to be a struct with at least one (32 BIT!!!) string pointer to the size in MB
		// uprintf("Done with that sort of thing: Action=%d pData=%0p\n", Action, pData);
		// /!\ THE FOLLOWING ONLY WORKS ON VISTA OR LATER - DO NOT ENABLE ON XP!
		// DumpBufferHex(pData, 8);
		// uprintf("Volume size: %s MB\n", (char*)(LONG_PTR)(*(ULONG32*)pData));
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
 * Converts an UTF-16 label to a valid FAT/NTFS one
 */
static void ToValidLabel(WCHAR* name, BOOL bFAT)
{
	size_t i, j, k;
	BOOL found;
	WCHAR unauthorized[] = L"*?,;:/\\|+=<>[]\"";
	WCHAR to_underscore[] = L"\t.";

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
			uprintf("FAT label is mostly underscores. Using '%s' label instead.\n", SelectedDrive.proposed_label);
			for(i=0; SelectedDrive.proposed_label[i]!=0; i++)
				name[i] = SelectedDrive.proposed_label[i];
			name[i] = 0;
		}
	} else {
		name[32] = 0;
	}

	// Needed for disk by label isolinux.cfg workaround
	wchar_to_utf8_no_alloc(name, img_report.usb_label, sizeof(img_report.usb_label));
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
static BOOL FormatFAT32(DWORD DriveIndex)
{
	BOOL r = FALSE;
	DWORD i;
	HANDLE hLogicalVolume;
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
	WCHAR wLabel[64], *wVolumeName = NULL;
	DWORD BurstSize = 128; // Zero in blocks of 64K typically

	// Calculated later
	DWORD FatSize = 0;
	DWORD BytesPerSect = 0;
	DWORD ClusterSize = 0;
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

	PrintInfoDebug(0, MSG_222, "Large FAT32");
	LastRefresh = 0;
	VolumeId = GetVolumeID();

	// Open the drive and lock it
	hLogicalVolume = GetLogicalHandle(DriveIndex, TRUE, TRUE, FALSE);
	if (IS_ERROR(FormatStatus)) goto out;
	if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL))
		die("Invalid logical volume handle\n", ERROR_INVALID_HANDLE);

	// Try to disappear the volume while we're formatting it
	UnmountVolume(hLogicalVolume);

	// Work out drive params
	if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dgDrive,
		sizeof(dgDrive), &cbRet, NULL)) {
		if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, xdgDrive,
			sizeof(geometry_ex), &cbRet, NULL)) {
			uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY error: %s\n", WindowsErrorString());
			die("Failed to get device geometry (both regular and _ex)\n", ERROR_NOT_SUPPORTED);
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
			uprintf("IOCTL_DISK_GET_PARTITION_INFO error: %s\n", WindowsErrorString());
			die("Failed to get partition info (both regular and _ex)\n", ERROR_NOT_SUPPORTED);
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
		die("This drive is too small for FAT32 - there must be at least 64K clusters\n", APPERR(ERROR_INVALID_CLUSTER_SIZE));
	}

	if (qTotalSectors >= 0xffffffff) {
		// This is a more fundamental limitation on FAT32 - the total sector count in the root dir
		// is 32bit. With a bit of creativity, FAT32 could be extended to handle at least 2^28 clusters
		// There would need to be an extra field in the FSInfo sector, and the old sector count could
		// be set to 0xffffffff. This is non standard though, the Windows FAT driver FASTFAT.SYS won't
		// understand this. Perhaps a future version of FAT32 and FASTFAT will handle this.
		die("This drive is too big for FAT32 - max 2TB supported\n", APPERR(ERROR_INVALID_VOLUME_SIZE));
	}

	// coverity[tainted_data]
	pFAT32BootSect = (FAT_BOOTSECTOR32*) calloc(BytesPerSect, 1);
	pFAT32FsInfo = (FAT_FSINFO*) calloc(BytesPerSect, 1);
	pFirstSectOfFat = (DWORD*) calloc(BytesPerSect, 1);
	if (!pFAT32BootSect || !pFAT32FsInfo || !pFirstSectOfFat) {
		die("Failed to allocate memory\n", ERROR_NOT_ENOUGH_MEMORY);
	}

	// fill out the boot sector and fs info
	pFAT32BootSect->sJmpBoot[0]=0xEB;
	pFAT32BootSect->sJmpBoot[1]=0x58; // jmp.s $+0x5a is 0xeb 0x58, not 0xeb 0x5a. Thanks Marco!
	pFAT32BootSect->sJmpBoot[2]=0x90;
	memcpy(pFAT32BootSect->sOEMName, "MSWIN4.1", 8);
	pFAT32BootSect->wBytsPerSec = (WORD) BytesPerSect;

	ClusterSize = (DWORD)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize));
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
		die("This drive has more than 2^28 clusters, try to specify a larger cluster size or use the default\n",
			ERROR_INVALID_CLUSTER_SIZE);
	}

	// Sanity check - < 64K clusters means that the volume will be misdetected as FAT16
	if (ClusterCount < 65536) {
		die("FAT32 must have at least 65536 clusters, try to specify a smaller cluster size or use the default\n",
			ERROR_INVALID_CLUSTER_SIZE);
	}

	// Sanity check, make sure the fat is big enough
	// Convert the cluster count into a Fat sector count, and check the fat size value we calculated
	// earlier is OK.
	FatNeeded = ClusterCount * 4;
	FatNeeded += (BytesPerSect-1);
	FatNeeded /= BytesPerSect;
	if (FatNeeded > FatSize) {
		die("This drive is too big for large FAT32 format\n", APPERR(ERROR_INVALID_VOLUME_SIZE));
	}

	// Now we're committed - print some info first
	uprintf("Size : %s %u sectors\n", SizeToHumanReadable(piDrive.PartitionLength.QuadPart, TRUE, FALSE), TotalSectors);
	uprintf("Cluster size %d bytes, %d Bytes Per Sector\n", SectorsPerCluster*BytesPerSect, BytesPerSect);
	uprintf("Volume ID is %x:%x\n", VolumeId>>16, VolumeId&0xffff);
	uprintf("%d Reserved Sectors, %d Sectors per FAT, %d FATs\n", ReservedSectCount, FatSize, NumFATs);
	uprintf("%d Total clusters\n", ClusterCount);

	// Fix up the FSInfo sector
	pFAT32FsInfo->dFree_Count = (UserAreaSize/SectorsPerCluster) - 1;
	pFAT32FsInfo->dNxt_Free = 3; // clusters 0-1 reserved, we used cluster 2 for the root dir

	uprintf("%d Free Clusters\n", pFAT32FsInfo->dFree_Count);
	// Work out the Cluster count

	// First zero out ReservedSect + FatSize * NumFats + SectorsPerCluster
	SystemAreaSize = ReservedSectCount + (NumFATs*FatSize) + SectorsPerCluster;
	uprintf("Clearing out %d sectors for reserved sectors, FATs and root cluster...\n", SystemAreaSize);

	// Not the most effective, but easy on RAM
	pZeroSect = (BYTE*)calloc(BytesPerSect, BurstSize);
	if (!pZeroSect) {
		die("Failed to allocate memory\n", ERROR_NOT_ENOUGH_MEMORY);
	}

	format_percent = 0.0f;
	for (i=0; i<(SystemAreaSize+BurstSize-1); i+=BurstSize) {
		if (GetTickCount64() > LastRefresh + MAX_REFRESH) {
			LastRefresh = GetTickCount64();
			format_percent = (100.0f*i)/(1.0f*(SystemAreaSize+BurstSize));
			PrintInfo(0, MSG_217, format_percent);
			UpdateProgress(OP_FORMAT, format_percent);
		}
		if (IS_ERROR(FormatStatus)) goto out;	// For cancellation
		if (write_sectors(hLogicalVolume, BytesPerSect, i, BurstSize, pZeroSect) != (BytesPerSect*BurstSize)) {
			die("Error clearing reserved sectors\n", ERROR_WRITE_FAULT);
		}
	}

	uprintf ("Initializing reserved sectors and FATs...\n");
	// Now we should write the boot sector and fsinfo twice, once at 0 and once at the backup boot sect position
	for (i=0; i<2; i++) {
		int SectorStart = (i==0) ? 0 : BackupBootSect;
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart, 1, pFAT32BootSect);
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart+1, 1, pFAT32FsInfo);
	}

	// Write the first fat sector in the right places
	for ( i=0; i<NumFATs; i++ ) {
		int SectorStart = ReservedSectCount + (i * FatSize );
		uprintf("FAT #%d sector at address: %d\n", i, SectorStart);
		write_sectors(hLogicalVolume, BytesPerSect, SectorStart, 1, pFirstSectOfFat);
	}

	// Must do it here, as have issues when trying to write the PBR after a remount
	PrintInfoDebug(0, MSG_229);
	if (!WritePBR(hLogicalVolume)) {
		// Non fatal error, but the drive probably won't boot
		uprintf("Could not write partition boot record - drive may not boot...\n");
	}

	// Set the FAT32 volume label
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	ToValidLabel(wLabel, TRUE);
	PrintInfoDebug(0, MSG_221);
	// Handle must be closed for SetVolumeLabel to work
	safe_closehandle(hLogicalVolume);
	VolumeName = GetLogicalName(DriveIndex, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if ((wVolumeName == NULL) || (!SetVolumeLabelW(wVolumeName, wLabel))) {
		uprintf("Could not set label: %s\n", WindowsErrorString());
		// Non fatal error
	}

	uprintf("Format completed.\n");
	r = TRUE;

out:
	safe_free(VolumeName);
	safe_free(wVolumeName);
	safe_closehandle(hLogicalVolume);
	safe_free(pFAT32BootSect);
	safe_free(pFAT32FsInfo);
	safe_free(pFirstSectOfFat);
	safe_free(pZeroSect);
	return r;
}

/*
 * Call on fmifs.dll's FormatEx() to format the drive
 */
static BOOL FormatDrive(DWORD DriveIndex)
{
	BOOL r = FALSE;
	PF_DECL(FormatEx);
	PF_DECL(EnableVolumeCompression);
	char FSType[64], path[MAX_PATH];
	char *locale, *VolumeName = NULL;
	WCHAR* wVolumeName = NULL;
	WCHAR wFSType[64];
	WCHAR wLabel[64];
	ULONG ulClusterSize;
	size_t i, index;

	GetWindowTextU(hFileSystem, FSType, ARRAYSIZE(FSType));
	// Skip the RIGHT_TO_LEFT_EMBEDDING mark from LTR languages
	index = (strncmp(FSType, RIGHT_TO_LEFT_EMBEDDING, sizeof(RIGHT_TO_LEFT_EMBEDDING) - 1) == 0) ? (sizeof(RIGHT_TO_LEFT_EMBEDDING) - 1) : 0;
	// Might have a (Default) suffix => remove it
	for (i=strlen(FSType); i>2; i--) {
		if (FSType[i] == '(') {
			FSType[i-1] = 0;
			break;
		}
	}
	if ((fs == FS_UDF) && !((dur_mins == 0) && (dur_secs == 0))) {
		PrintInfoDebug(0, MSG_220, &FSType[index], dur_mins, dur_secs);
	} else {
		PrintInfoDebug(0, MSG_222, &FSType[index]);
	}
	VolumeName = GetLogicalName(DriveIndex, TRUE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		goto out;
	}
	// Hey, nice consistency here, Microsoft! -  FormatEx() fails if wVolumeName has
	// a trailing backslash, but EnableCompression() fails without...
	wVolumeName[wcslen(wVolumeName)-1] = 0;		// Remove trailing backslash

	// Check if Windows picked the UEFI:NTFS partition
	// NB: No need to do this for Large FAT32, as this only applies to NTFS
	static_strcpy(path, VolumeName);
	static_strcat(path, "EFI\\Rufus\\ntfs_x64.efi");
	if (PathFileExistsA(path)) {
		uprintf("Windows selected the UEFI:NTFS partition for formatting - Retry needed", VolumeName);
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_RETRY;
		goto out;
	}

	// LoadLibrary("fmifs.dll") appears to changes the locale, which can lead to
	// problems with tolower(). Make sure we restore the locale. For more details,
	// see http://comments.gmane.org/gmane.comp.gnu.mingw.user/39300
	locale = setlocale(LC_ALL, NULL);
	PF_INIT_OR_OUT(FormatEx, Fmifs);
	PF_INIT(EnableVolumeCompression, Fmifs);
	setlocale(LC_ALL, locale);

	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	// Again, skip the RIGHT_TO_LEFT_EMBEDDING mark if present
	index = (wFSType[0] == 0x202b) ? 1 : 0;
	// We may have a " (Default)" trail
	for (i=0; i<wcslen(wFSType); i++) {
		if (wFSType[i] == ' ') {
			wFSType[i] = 0;
			break;
		}
	}
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	// Make sure the label is valid
	ToValidLabel(wLabel, (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_EXFAT));
	ulClusterSize = (ULONG)ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize));
	if (ulClusterSize < 0x200) {
		// 0 is FormatEx's value for default, which we need to use for UDF
		ulClusterSize = 0;
		uprintf("Using default cluster size\n");
	} else {
		uprintf("Using cluster size: %d bytes\n", ulClusterSize);
	}
	format_percent = 0.0f;
	task_number = 0;
	fs_index = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));

	uprintf("%s format was selected\n", IsChecked(IDC_QUICK_FORMAT)?"Quick":"Slow");
	pfFormatEx(wVolumeName, SelectedDrive.MediaType, &wFSType[index], wLabel,
		IsChecked(IDC_QUICK_FORMAT), ulClusterSize, FormatExCallback);

	if ((fs == FS_NTFS) && (enable_ntfs_compression) && (pfEnableVolumeCompression != NULL)) {
		wVolumeName[wcslen(wVolumeName)] = '\\';	// Add trailing backslash back again
		if (pfEnableVolumeCompression(wVolumeName, FPF_COMPRESSED)) {
			uprintf("Enabled NTFS compression\n");
		} else {
			uprintf("Could not enable NTFS compression: %s\n", WindowsErrorString());
		}
	}

	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.\n");
		r = TRUE;
	}

out:
	safe_free(VolumeName);
	safe_free(wVolumeName);
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
			if (IS_ERROR(FormatStatus))
				goto out;
			if (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize) {
				if (j < WRITE_RETRIES) {
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(WRITE_TIMEOUT);
				} else
					goto out;
			}
		}
	}
	for (i = last_sector - MAX_SECTORS_TO_CLEAR; i < last_sector; i++) {
		for (j = 1; j <= WRITE_RETRIES; j++) {
			if (IS_ERROR(FormatStatus))
				goto out;
			if (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize) {
				if (j < WRITE_RETRIES) {
					uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
					Sleep(WRITE_TIMEOUT);
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
	const char* using_msg = "Using %s MBR\n";

	AnalyzeMBR(hPhysicalDrive, "Drive", FALSE);

	if (SelectedDrive.SectorSize < 512)
		goto out;

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
	if ((bt != BT_NON_BOOTABLE) && (tt == TT_BIOS)) {
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
	if (HAS_WINDOWS(img_report) && (allow_dual_uefi_bios) && (tt == TT_BIOS))
		goto windows_mbr;

	// Forced UEFI (by zeroing the MBR)
	if (tt == TT_UEFI) {
		uprintf(using_msg, "zeroed");
		r = write_zero_mbr(fp);
		goto notify;
	}

	// Syslinux
	if ( (bt == BT_SYSLINUX_V4) || (bt == BT_SYSLINUX_V6) ||
		 ((bt == BT_IMAGE) && HAS_SYSLINUX(img_report)) ) {
		uprintf(using_msg, "Syslinux");
		r = write_syslinux_mbr(fp);
		goto notify;
	}

	// Grub 2.0
	if ( ((bt == BT_IMAGE) && (img_report.has_grub2)) || (bt == BT_GRUB2) ) {
		uprintf(using_msg, "Grub 2.0");
		r = write_grub2_mbr(fp);
		goto notify;
	}

	// Grub4DOS
	if ( ((bt == BT_IMAGE) && (img_report.has_grub4dos)) || (bt == BT_GRUB4DOS) ) {
		uprintf(using_msg, "Grub4DOS");
		r = write_grub4dos_mbr(fp);
		goto notify;
	}

	// ReactOS
	if (bt == BT_REACTOS) {
		uprintf(using_msg, "ReactOS");
		r = write_reactos_mbr(fp);
		goto notify;
	}

	// KolibriOS
	if ( (bt == BT_IMAGE) && HAS_KOLIBRIOS(img_report) && (IS_FAT(fs))) {
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
	int r, sub_type = bt;
	unsigned char* buf = NULL;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);
	// Ensure that we have sufficient space for the SBR
	max_size = IsChecked(IDC_OLD_BIOS_FIXES) ?
		(DWORD)(SelectedDrive.SectorsPerTrack * SelectedDrive.SectorSize) : 1*MB;
	max_size -= mbr_size;
	// Syslinux has precedence over Grub
	if ((bt == BT_IMAGE) && (!HAS_SYSLINUX(img_report))) {
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
	switch (bt) {
	case BT_FREEDOS: return "FreeDOS";
	case BT_REACTOS: return "ReactOS";
	default:
		return ((bt==BT_IMAGE) && HAS_KOLIBRIOS(img_report)) ? "KolibriOS" : "Standard";
	}
}
static BOOL WritePBR(HANDLE hLogicalVolume)
{
	int i;
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	const char* using_msg = "Using %s %s partition boot record\n";

	fake_fd._handle = (char*)hLogicalVolume;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		uprintf(using_msg, bt_to_name(), "FAT16");
		if (!is_fat_16_fs(fp)) {
			uprintf("New volume does not have a FAT16 boot sector - aborting\n");
			break;
		}
		uprintf("Confirmed new volume has a FAT16 boot sector\n");
		if (bt == BT_FREEDOS) {
			if (!write_fat_16_fd_br(fp, 0)) break;
		} else if (bt == BT_REACTOS) {
			if (!write_fat_16_ros_br(fp, 0)) break;
		} else if ((bt == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
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
			if (bt == BT_FREEDOS) {
				if (!write_fat_32_fd_br(fp, 0)) break;
			} else if (bt == BT_REACTOS) {
				if (!write_fat_32_ros_br(fp, 0)) break;
			} else if ((bt == BT_IMAGE) && HAS_KOLIBRIOS(img_report)) {
				if (!write_fat_32_kos_br(fp, 0)) break;
			} else if ((bt == BT_IMAGE) && HAS_BOOTMGR(img_report)) {
				if (!write_fat_32_pe_br(fp, 0)) break;
			} else if ((bt == BT_IMAGE) && HAS_WINPE(img_report)) {
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
// due to people wondering why they can't see them by default, and also due to dism
// incompatibilities from one version of Windows to the next.
// Maybe when we use wimlib we'll review this, but for now just turn it off.
//#define SET_INTERNAL_DRIVES_OFFLINE
static BOOL SetupWinToGo(const char* drive_name, BOOL use_ms_efi)
{
#ifdef SET_INTERNAL_DRIVES_OFFLINE
	static char san_policy_path[] = "?:\\san_policy.xml";
#endif
	static char unattend_path[] = "?:\\Windows\\System32\\sysprep\\unattend.xml";
	char *mounted_iso, *ms_efi = NULL, image[128], cmd[MAX_PATH];
	unsigned char *buffer;
	wchar_t wVolumeName[] = L"?:";
	DWORD bufsize;
	ULONG cluster_size;
	FILE* fd;
	PF_DECL(FormatEx);
	PF_INIT(FormatEx, Fmifs);

	uprintf("Windows To Go mode selected");
	// Additional sanity checks
	if ( (use_ms_efi) && (SelectedDrive.MediaType != FixedMedia) && (nWindowsBuildNumber < 15000)) {
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

	if (use_ms_efi) {
		uprintf("Setting up MS EFI system partition");
		if (pfFormatEx == NULL)
			return FALSE;
		ms_efi = AltMountVolume(drive_name, 3);	// MSR, main, EFI
		if (ms_efi == NULL) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
			return FALSE;
		}
		uprintf("Formatting EFI system partition %s", ms_efi);
		// According to Ubuntu (https://bugs.launchpad.net/ubuntu/+source/partman-efi/+bug/811485) you want to use FAT32.
		// However, you have to be careful that the cluster size needs to be greater or equal to the sector size, which
		// in turn has an impact on the minimum EFI partition size we can create (see ms_efi_size_MB in drive.c)
		if (SelectedDrive.SectorSize <= 1024)
			cluster_size = 1024;
		else if (SelectedDrive.SectorSize <= 4096)
			cluster_size = 4096;
		else	// Go for broke
			cluster_size = (ULONG)SelectedDrive.SectorSize;
		fs_index = 1;	// FAT32
		task_number = 0;
		wVolumeName[0] = ms_efi[0];

		// Boy do you *NOT* want to specify a label here, and spend HOURS figuring out why your EFI partition cannot boot...
		pfFormatEx(wVolumeName, SelectedDrive.MediaType, L"FAT32", L"", TRUE, cluster_size, FormatExCallback);
		if (IS_ERROR(FormatStatus)) {
			uprintf("Failed to format EFI partition");
			AltUnmountVolume(ms_efi);
			return FALSE;
		}
		Sleep(200);
	}

	// We invoke the 'bcdboot' command from the host, as the one from the drive produces problems (#558)
	// Also, since Rufus should (usually) be running as a 32 bit app, on 64 bit systems, we need to use
	// 'C:\Windows\Sysnative' and not 'C:\Windows\System32' to invoke bcdboot, as 'C:\Windows\System32'
	// will get converted to 'C:\Windows\SysWOW64' behind the scenes, and there is no bcdboot.exe there.
	static_sprintf(cmd, "%s\\bcdboot.exe %s\\Windows /v /f %s /s %s", sysnative_dir, drive_name,
		HAS_BOOTMGR_BIOS(img_report) ? (HAS_BOOTMGR_EFI(img_report) ? "ALL" : "BIOS") : "UEFI",
		(use_ms_efi)?ms_efi:drive_name);
	uprintf("Enabling boot using command '%s'", cmd);
	if (RunCommand(cmd, sysnative_dir, usb_debug) != 0) {
		// Try to continue... but report a failure
		uprintf("Failed to enable boot");
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_ISO_EXTRACT);
	}

	if (use_ms_efi) {
		Sleep(200);
		AltUnmountVolume(ms_efi);
	}
	PrintInfo(0, MSG_267, 99.9f);
	UpdateProgress(OP_DOS, 99.9f);

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
	if ((fd == NULL) || (fwrite(buffer, 1, bufsize, fd) != bufsize)) {
		uprintf("Could not write '%s'\n", unattend_path);
	}
	fclose(fd);
	PrintInfo(0, MSG_267, 100.0f);
	UpdateProgress(OP_DOS, 100.0f);

	return TRUE;
}

static void update_progress(const uint64_t processed_bytes)
{
	if (GetTickCount64() > LastRefresh + MAX_REFRESH) {
		LastRefresh = GetTickCount64();
		format_percent = (100.0f*processed_bytes)/(1.0f*img_report.image_size);
		PrintInfo(0, MSG_261, format_percent);
		UpdateProgress(OP_FORMAT, format_percent);
	}
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
	LastRefresh = 0;

	if (img_report.compression_type != BLED_COMPRESSION_NONE) {
		uprintf("Writing compressed image...");
		bled_init(_uprintf, update_progress, &FormatStatus);
		bled_ret = bled_uncompress_with_handles(hSourceImage, hPhysicalDrive, img_report.compression_type);
		bled_exit();
		if ((bled_ret < 0) && (SCODE_CODE(FormatStatus) != ERROR_CANCELLED)) {
			// Unfortunately, different compression backends return different negative error codes
			uprintf("Could not write compressed image: %" PRIi64, bled_ret);
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
			if (GetTickCount64() > LastRefresh + MAX_REFRESH) {
				LastRefresh = GetTickCount64();
				format_percent = (100.0f*wb) / (1.0f*target_size);
				PrintInfo(0, hSourceImage?MSG_261:fast_zeroing?MSG_306:MSG_286, format_percent);
				UpdateProgress(OP_FORMAT, format_percent);
			}

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
					uprintf("Write error at sector %" PRIi64 ": %s", wb / SelectedDrive.SectorSize, WindowsErrorString());
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
	DWORD DriveIndex = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	HANDLE hSourceImage = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	FILE* log_fd;
	uint8_t *buffer = NULL, extra_partitions = 0;
	char *bb_msg, *guid_volume = NULL;
	char drive_name[] = "?:\\";
	char drive_letters[27], fs_type[32];
	char logfile[MAX_PATH], *userdir;
	char efi_dst[] = "?:\\efi\\boot\\bootx64.efi";
	char kolibri_dst[] = "?:\\MTLD_F32";
	char grub4dos_dst[] = "?:\\grldr";

	use_large_fat32 = (fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32));
	windows_to_go = (image_options & IMOP_WINTOGO) && (bt == BT_IMAGE) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1);
	large_drive = (SelectedDrive.DiskSize > (1*TB));
	if (large_drive)
		uprintf("Notice: Large drive detected (may produce short writes)");
	// Find out if we need to add any extra partitions
	if ((windows_to_go) && (tt == TT_UEFI) && (pt == PARTITION_STYLE_GPT))
		// According to Microsoft, every GPT disk (we RUN Windows from) must have an MSR due to not having hidden sectors
		// http://msdn.microsoft.com/en-us/library/windows/hardware/dn640535.aspx#gpt_faq_what_disk_require_msr
		extra_partitions = XP_MSR | XP_EFI;
	else if ( (fs == FS_NTFS) && ((bt == BT_UEFI_NTFS) ||
			  ((bt == BT_IMAGE) && IS_EFI_BOOTABLE(img_report) && ((tt == TT_UEFI) || (windows_to_go) || (allow_dual_uefi_bios)))) )
		extra_partitions = XP_UEFI_NTFS;
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
		uprintf("Failed to get a drive letter\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
		goto out;
	}
	if (drive_letters[0] == 0) {
		uprintf("No drive letter was assigned...\n");
		drive_name[0] =  GetUnusedDriveLetter();
		if (drive_name[0] == 0) {
			uprintf("Could not find a suitable drive letter\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
			goto out;
		}
	} else {
		// Unmount all mounted volumes that belong to this drive
		// Do it in reverse so that we always end on the first volume letter
		for (i = (int)safe_strlen(drive_letters); i > 0; i--) {
			drive_name[0] = drive_letters[i-1];
			if (bt == BT_IMAGE) {
				// If we are using an image, check that it isn't located on the drive we are trying to format
				if ((PathGetDriveNumberU(image_path) + 'A') == drive_letters[i-1]) {
					uprintf("ABORTED: Cannot use an image that is located on the target drive!\n");
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
					goto out;
				}
			}
			if (!DeleteVolumeMountPointA(drive_name)) {
				uprintf("Failed to delete mountpoint %s: %s\n", drive_name, WindowsErrorString());
				// Try to continue. We will bail out if this causes an issue.
			}
		}
	}
	uprintf("Will use '%c:' as volume mountpoint\n", drive_name[0]);

	// It kind of blows, but we have to relinquish access to the physical drive
	// for VDS to be able to delete the partitions that reside on it...
	safe_unlockclose(hPhysicalDrive);
	PrintInfoDebug(0, MSG_239);
	DeletePartitions(DriveIndex);

	// Now get RW access to the physical drive...
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, lock_drive, TRUE, !lock_drive);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_OPEN_FAILED;
		goto out;
	}

	// ...and get a lock to the logical drive so that we can actually write something
	hLogicalVolume = GetLogicalHandle(DriveIndex, TRUE, FALSE, !lock_drive);
	if (hLogicalVolume == INVALID_HANDLE_VALUE) {
		uprintf("Could not lock volume\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	} else if (hLogicalVolume == NULL) {
		// NULL is returned for cases where the drive is not yet partitioned
		uprintf("Drive does not appear to be partitioned\n");
	} else if (!UnmountVolume(hLogicalVolume)) {
		uprintf("Trying to continue regardless...\n");
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
	if ((bt != BT_IMAGE) || (img_report.is_iso && !write_as_image)) {
		if ((!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) ||
			(!InitializeDisk(hPhysicalDrive))) {
			uprintf("Could not reset partitions\n");
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_PARTITION_FAILURE;
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
				uprintf("Could not create log file for bad blocks check\n");
			} else {
				fprintf(log_fd, APPLICATION_NAME " bad blocks check started on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fflush(log_fd);
			}

			if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize, (sel >= 2) ? 4 : sel +1,
				(sel < 2) ? 0 : sel - 2, &report, log_fd)) {
				uprintf("Bad blocks: Check failed.\n");
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_BADBLOCKS_FAILURE);
				ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, FALSE);
				fclose(log_fd);
				_unlinkU(logfile);
				goto out;
			}
			uprintf("Bad Blocks: Check completed, %d bad block%s found. (%d/%d/%d errors)\n",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
			r = IDOK;
			if (report.bb_count) {
				bb_msg = lmprintf(MSG_011, report.bb_count, report.num_read_errors, report.num_write_errors,
					report.num_corruption_errors);
				fprintf(log_fd, bb_msg);
				GetLocalTime(&lt);
				fprintf(log_fd, APPLICATION_NAME " bad blocks check ended on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fclose(log_fd);
				r = MessageBoxExU(hMainDialog, lmprintf(MSG_012, bb_msg, logfile),
					lmprintf(MSG_010), MB_ABORTRETRYIGNORE|MB_ICONWARNING|MB_IS_RTL, selected_langid);
			} else {
				// We didn't get any errors => delete the log file
				fclose(log_fd);
				_unlinkU(logfile);
			}
		} while (r == IDRETRY);
		if (r == IDABORT) {
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			goto out;
		}

		// Especially after destructive badblocks test, you must zero the MBR/GPT completely
		// before repartitioning. Else, all kind of bad things happen.
		if (!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.SectorSize, use_large_fat32)) {
			uprintf("unable to zero MBR/GPT\n");
			if (!IS_ERROR(FormatStatus))
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
	}

	// Write an image file
	if ((bt == BT_IMAGE) && write_as_image) {
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
		WaitForLogical(DriveIndex);
		if (GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_type, sizeof(fs_type), TRUE)) {
			guid_volume = GetLogicalName(DriveIndex, TRUE, TRUE);
			if ((guid_volume != NULL) && (MountVolume(drive_name, guid_volume)))
				uprintf("Remounted %s on %s\n", guid_volume, drive_name);
		}
		goto out;
	}

	UpdateProgress(OP_ZERO_MBR, -1.0f);
	CHECK_FOR_USER_CANCEL;

	if (!CreatePartition(hPhysicalDrive, pt, fs, (pt==PARTITION_STYLE_MBR) && (tt==TT_UEFI), extra_partitions)) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Close the (unmounted) volume before formatting
	if ((hLogicalVolume != NULL) && (hLogicalVolume != INVALID_HANDLE_VALUE)) {
		PrintInfoDebug(0, MSG_227);
		if (!CloseHandle(hLogicalVolume)) {
			uprintf("Could not close volume: %s\n", WindowsErrorString());
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
			goto out;
		}
	}
	hLogicalVolume = INVALID_HANDLE_VALUE;

	// Wait for the logical drive we just created to appear
	uprintf("Waiting for logical drive to reappear...\n");
	Sleep(200);
	if (!WaitForLogical(DriveIndex))
		uprintf("Logical drive was not found!");	// We try to continue even if this fails, just in case
	CHECK_FOR_USER_CANCEL;

	// If FAT32 is requested and we have a large drive (>32 GB) use
	// large FAT32 format, else use MS's FormatEx.
	ret = use_large_fat32?FormatFAT32(DriveIndex):FormatDrive(DriveIndex);
	if (!ret) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: %s\n", StrError(FormatStatus, TRUE));
		goto out;
	}

	// Thanks to Microsoft, we must fix the MBR AFTER the drive has been formatted
	if (pt == PARTITION_STYLE_MBR) {
		PrintInfoDebug(0, MSG_228);	// "Writing master boot record..."
		if ((!WriteMBR(hPhysicalDrive)) || (!WriteSBR(hPhysicalDrive))) {
			if (!IS_ERROR(FormatStatus))
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
			goto out;
		}
		UpdateProgress(OP_FIX_MBR, -1.0f);
	}
	Sleep(200);
	WaitForLogical(DriveIndex);
	// Try to continue
	CHECK_FOR_USER_CANCEL;

	guid_volume = GetLogicalName(DriveIndex, TRUE, TRUE);
	if (guid_volume == NULL) {
		uprintf("Could not get GUID volume name\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NO_VOLUME_ID;
		goto out;
	}
	uprintf("Found volume GUID %s\n", guid_volume);

	if (!MountVolume(drive_name, guid_volume)) {
		uprintf("Could not remount %s on %s: %s\n", guid_volume, drive_name, WindowsErrorString());
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_MOUNT_VOLUME);
		goto out;
	}
	CHECK_FOR_USER_CANCEL;

	// Disable file indexing, unless it was force-enabled by the user
	if ((!enable_file_indexing) && ((fs == FS_NTFS) || (fs == FS_UDF) || (fs == FS_REFS))) {
		uprintf("Disabling file indexing...");
		if (!SetFileAttributesA(guid_volume, FILE_ATTRIBUTE_NOT_CONTENT_INDEXED))
			uprintf("Could not disable file indexing: %s", WindowsErrorString());
	}

	// Refresh the drive label - This is needed as Windows may have altered it from
	// the name we proposed, and we require an exact label, to patch config files.
	if (!GetVolumeInformationU(drive_name, img_report.usb_label, ARRAYSIZE(img_report.usb_label),
		NULL, NULL, NULL, NULL, 0)) {
		uprintf("Warning: Failed to refresh label: %s", WindowsErrorString());
	}

	if (bt != BT_NON_BOOTABLE) {
		if (bt == BT_UEFI_NTFS) {
			// All good
		} else if (tt == TT_UEFI) {
			// For once, no need to do anything - just check our sanity
			assert((bt == BT_IMAGE) && IS_EFI_BOOTABLE(img_report) && (fs <= FS_NTFS));
			if ( (bt != BT_IMAGE) || !IS_EFI_BOOTABLE(img_report) || (fs > FS_NTFS) ) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
		} else if ( (bt == BT_SYSLINUX_V4) || (bt == BT_SYSLINUX_V6) ||
			((bt == BT_IMAGE) && (HAS_SYSLINUX(img_report) || HAS_REACTOS(img_report)) &&
				(!HAS_WINDOWS(img_report) || !allow_dual_uefi_bios)) ) {
			if (!InstallSyslinux(DriveIndex, drive_name[0], fs)) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
		} else {
			// We still have a lock, which we need to modify the volume boot record
			// => no need to reacquire the lock...
			hLogicalVolume = GetLogicalHandle(DriveIndex, FALSE, TRUE, FALSE);
			if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL)) {
				uprintf("Could not re-mount volume for partition boot record access\n");
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

	if (bt != BT_NON_BOOTABLE) {
		if ((bt == BT_MSDOS) || (bt == BT_FREEDOS)) {
			UpdateProgress(OP_DOS, -1.0f);
			PrintInfoDebug(0, MSG_230);
			if (!ExtractDOS(drive_name)) {
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
				goto out;
			}
		} else if (bt == BT_GRUB4DOS) {
			grub4dos_dst[0] = drive_name[0];
			IGNORE_RETVAL(_chdirU(app_dir));
			uprintf("Installing: %s (Grub4DOS loader) %s\n", grub4dos_dst,
				IsFileInDB(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr")?"✓":"✗");
			if (!CopyFileU(FILES_DIR "\\grub4dos-" GRUB4DOS_VERSION "\\grldr", grub4dos_dst, FALSE))
				uprintf("Failed to copy file: %s", WindowsErrorString());
		} else if ((bt == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso)) {
			UpdateProgress(OP_DOS, 0.0f);
			drive_name[2] = 0;	// Ensure our drive is something like 'D:'
			if (windows_to_go) {
				PrintInfoDebug(0, MSG_268);
				if (!SetupWinToGo(drive_name, (extra_partitions & XP_EFI))) {
					if (!IS_ERROR(FormatStatus))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
					goto out;
				}
			} else {
				PrintInfoDebug(0, MSG_231);
				if (!ExtractISO(image_path, drive_name, FALSE)) {
					if (!IS_ERROR(FormatStatus))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_ISO_EXTRACT);
					goto out;
				}
				if (HAS_KOLIBRIOS(img_report)) {
					kolibri_dst[0] = drive_name[0];
					uprintf("Installing: %s (KolibriOS loader)\n", kolibri_dst);
					if (ExtractISOFile(image_path, "HD_Load/USB_Boot/MTLD_F32", kolibri_dst,
						FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM) == 0) {
						uprintf("Warning: loader installation failed - KolibriOS will not boot!\n");
					}
				}
				// EFI mode selected, with no 'boot###.efi' but Windows 7 x64's 'bootmgr.efi' (bit #0)
				if ((tt == TT_UEFI) && HAS_WIN7_EFI(img_report)) {
					PrintInfoDebug(0, MSG_232);
					img_report.wininst_path[0][0] = drive_name[0];
					efi_dst[0] = drive_name[0];
					efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = 0;
					if (!CreateDirectoryA(efi_dst, 0)) {
						uprintf("Could not create directory '%s': %s\n", efi_dst, WindowsErrorString());
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
					} else {
						efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = '\\';
						if (!WimExtractFile(img_report.wininst_path[0], 1, "Windows\\Boot\\EFI\\bootmgfw.efi", efi_dst)) {
							uprintf("Failed to setup Win7 EFI boot\n");
							FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
						}
					}
				}
				if ( (tt == TT_BIOS) && HAS_WINPE(img_report) ) {
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
		if ((bt == BT_IMAGE) && (img_report.is_iso) && (fs == FS_NTFS)) {
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
	safe_free(guid_volume);
	safe_free(buffer);
	safe_closehandle(hSourceImage);
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);	// This can take a while
	if (IS_ERROR(FormatStatus)) {
		guid_volume = GetLogicalName(DriveIndex, TRUE, FALSE);
		if (guid_volume != NULL) {
			if (MountVolume(drive_name, guid_volume))
				uprintf("Re-mounted volume as '%c:' after error\n", drive_name[0]);
			free(guid_volume);
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
	LastRefresh = 0;
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
		if (GetTickCount64() > LastRefresh + MAX_REFRESH) {
			LastRefresh = GetTickCount64();
			format_percent = (100.0f*wb)/(1.0f*img_save->DeviceSize);
			PrintInfo(0, MSG_261, format_percent);
			UpdateProgress(OP_FORMAT, format_percent);
		}
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
