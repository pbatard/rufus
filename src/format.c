/*
 * Rufus: The Reliable USB Formatting Utility
 * Formatting function calls
 * Copyright © 2007-2009 Tom Thornhill/Ridgecrop
 * Copyright © 2011-2013 Pete Batard <pete@akeo.ie>
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
#include "localization.h"

/*
 * Globals
 */
DWORD FormatStatus;
badblocks_report report;
static float format_percent = 0.0f;
static int task_number = 0;
extern const int nb_steps[FS_MAX];
static int fs_index = 0;
BOOL force_large_fat32 = FALSE;
static BOOL WritePBR(HANDLE hLogicalDrive);

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
		PrintStatus(0, FALSE, lmprintf(MSG_217, *percent));
		UpdateProgress(OP_FORMAT, 1.0f * (*percent));
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		PrintStatus(0, TRUE, lmprintf(MSG_218, ++task_number, nb_steps[fs_index]));
		format_percent += 100.0f / (1.0f * nb_steps[fs_index]);
		UpdateProgress(OP_CREATE_FS, format_percent);
		break;
	case FCC_DONE:
		PrintStatus(0, TRUE, lmprintf(MSG_218, nb_steps[fs_index], nb_steps[fs_index]));
		UpdateProgress(OP_CREATE_FS, 100.0f);
		if(*(BOOLEAN*)pData == FALSE) {
			uprintf("Error while formatting.\n");
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
		PrintStatus(0, FALSE, lmprintf(MSG_219, *percent));
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
	case FCC_READ_ONLY_MODE:
		uprintf("Media has been switched to read-only - Leaving checkdisk\n");
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
	WCHAR unauthorized[] = L"*?,;:/\\|+=<>[]";
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
	wchar_to_utf8_no_alloc(name, iso_report.usb_label, sizeof(iso_report.usb_label));
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
 * This is the Microsoft calculation from FATGEN
 * 
 * DWORD RootDirSectors = 0;
 * DWORD TmpVal1, TmpVal2, FATSz;
 *
 * TmpVal1 = DskSize - (ReservedSecCnt + RootDirSectors);
 * TmpVal2 = (256 * SecPerClus) + NumFATs;
 * TmpVal2 = TmpVal2 / 2;
 * FATSz = (TmpVal1 + (TmpVal2 - 1)) / TmpVal2;
 *
 * return( FatSz );
 */
static DWORD GetFATSizeSectors(DWORD DskSize, DWORD ReservedSecCnt, DWORD SecPerClus, DWORD NumFATs, DWORD BytesPerSect)
{
	ULONGLONG Numerator, Denominator;
	ULONGLONG FatElementSize = 4;
	ULONGLONG FatSz;

	// This is based on 
	// http://hjem.get2net.dk/rune_moeller_barnkob/filesystems/fat.html
	Numerator = FatElementSize * (DskSize - ReservedSecCnt);
	Denominator = (SecPerClus * BytesPerSect) + (FatElementSize * NumFATs);
	FatSz = Numerator / Denominator;
	// round up
	FatSz += 1;

	return (DWORD)FatSz;
}

/*
 * Large FAT32 volume formatting from fat32format by Tom Thornhill
 * http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm
 */
static BOOL FormatFAT32(DWORD DriveIndex)
{
	BOOL r = FALSE;
	DWORD i, LastRefresh = 0;
	HANDLE hLogicalVolume;
	DWORD cbRet;
	DISK_GEOMETRY dgDrive;
	PARTITION_INFORMATION piDrive;
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

	// TODO: use another lmsg for Large FAT32
	PrintStatus(0, TRUE, lmprintf(MSG_222, "Large FAT32"));
	VolumeId = GetVolumeID();

	// Open the drive and lock it
	hLogicalVolume = GetLogicalHandle(DriveIndex, TRUE, TRUE);
	if (IS_ERROR(FormatStatus)) goto out;
	if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL))
		die("Invalid logical volume handle\n", ERROR_INVALID_HANDLE);

	// Try to disappear the volume while we're formatting it
	UnmountVolume(hLogicalVolume);

	// Work out drive params
	if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &dgDrive,
		sizeof(dgDrive), &cbRet, NULL)) {
		die("Failed to get device geometry\n", ERROR_NOT_SUPPORTED);
	}
	if (IS_ERROR(FormatStatus)) goto out;
	if (!DeviceIoControl (hLogicalVolume, IOCTL_DISK_GET_PARTITION_INFO, NULL, 0, &piDrive,
		sizeof(piDrive), &cbRet, NULL)) {
		die("Failed to get partition info\n", ERROR_NOT_SUPPORTED);
	}

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
		// ís 32bit. With a bit of creativity, FAT32 could be extended to handle at least 2^28 clusters
		// There would need to be an extra field in the FSInfo sector, and the old sector count could
		// be set to 0xffffffff. This is non standard though, the Windows FAT driver FASTFAT.SYS won't
		// understand this. Perhaps a future version of FAT32 and FASTFAT will handle this.
		die ("This drive is too big for FAT32 - max 2TB supported\n", APPERR(ERROR_INVALID_VOLUME_SIZE));
	}

	pFAT32BootSect = (FAT_BOOTSECTOR32*) calloc(BytesPerSect, 1);
	pFAT32FsInfo = (FAT_FSINFO*) calloc(BytesPerSect, 1);
	pFirstSectOfFat = (DWORD*) calloc(BytesPerSect, 1);
	if (!pFAT32BootSect || !pFAT32FsInfo || !pFirstSectOfFat) {
		die("Failed to allocate memory\n", ERROR_NOT_ENOUGH_MEMORY);
	}

	// fill out the boot sector and fs info
	pFAT32BootSect->sJmpBoot[0]=0xEB;
	pFAT32BootSect->sJmpBoot[1]=0x5A;
	pFAT32BootSect->sJmpBoot[2]=0x90;
	strncpy((char*)pFAT32BootSect->sOEMName, "MSWIN4.1", 8);
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
	uprintf("Size : %s %u sectors\n", SizeToHumanReadable(piDrive.PartitionLength), TotalSectors);
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
		if (GetTickCount() > LastRefresh + 25) {
			LastRefresh = GetTickCount();
			format_percent = (100.0f*i)/(1.0f*(SystemAreaSize+BurstSize));
			PrintStatus(0, FALSE, lmprintf(MSG_217, format_percent));
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
	PrintStatus(0, TRUE, lmprintf(MSG_229));
	if (!WritePBR(hLogicalVolume)) {
		// Non fatal error, but the drive probably won't boot
		uprintf("Could not write partition boot record - drive may not boot...\n");
	}

	// Set the FAT32 volume label
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	ToValidLabel(wLabel, TRUE);
	PrintStatus(0, TRUE, lmprintf(MSG_221));
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
	char FSType[32];
	char *locale, *VolumeName = NULL;
	WCHAR* wVolumeName = NULL;
	WCHAR wFSType[64];
	WCHAR wLabel[64];
	ULONG ulClusterSize;
	size_t i;

	GetWindowTextU(hFileSystem, FSType, ARRAYSIZE(FSType));
	PrintStatus(0, TRUE, lmprintf(MSG_222, FSType));
	VolumeName = GetLogicalName(DriveIndex, FALSE, TRUE);
	wVolumeName = utf8_to_wchar(VolumeName);
	if (wVolumeName == NULL) {
		uprintf("Could not read volume name\n");
		goto out;
	}

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
	pfFormatEx(wVolumeName, SelectedDrive.Geometry.MediaType, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), ulClusterSize, FormatExCallback);
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
	PrintStatus(0, TRUE, lmprintf(MSG_223));

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
		uprintf("Drive has a DOS/NT/95A master boot record\n");
	} else if (is_dos_f2_mbr(&fake_fd)) {
		uprintf("Drive has a DOS/NT/95A master boot record "
			"with the undocumented F2 instruction\n");
	} else if (is_95b_mbr(&fake_fd)) {
		uprintf("Drive has a Windows 95B/98/98SE/ME master boot record\n");
	} else if (is_2000_mbr(&fake_fd)) {
		uprintf("Drive has a Windows 2000/XP/2003 master boot record\n");
	} else if (is_vista_mbr(&fake_fd)) {
		uprintf("Drive has a Windows Vista master boot record\n");
	} else if (is_win7_mbr(&fake_fd)) {
		uprintf("Drive has a Windows 7 master boot record\n");
	} else if (is_zero_mbr(&fake_fd)) {
		uprintf("Drive has a zeroed non-bootable master boot record\n");
	} else {
		uprintf("Drive has an unknown master boot record\n");
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
			uprintf("Drive has an unknown FAT16 or FAT32 partition boot record\n");
		}
	} else {
		uprintf("Drive has an unknown partition boot record\n");
	}
	return TRUE;
}

static BOOL ClearMBRGPT(HANDLE hPhysicalDrive, LONGLONG DiskSize, DWORD SectorSize)
{
	BOOL r = FALSE;
	uint64_t i, last_sector = DiskSize/SectorSize;
	unsigned char* pBuf = (unsigned char*) calloc(SectorSize, 1);

	PrintStatus(0, TRUE, lmprintf(MSG_224));
	if (pBuf == NULL) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}
	// http://en.wikipedia.org/wiki/GUID_Partition_Table tells us we should clear 34 sectors at the
	// beginning and 33 at the end. We bump these values to MAX_SECTORS_TO_CLEAR each end to help
	// with reluctant access to large drive.

	// Must clear at least 1MB + the PBR for large FAT32 format to work on a large drive
	for (i=0; i<(2048+MAX_SECTORS_TO_CLEAR); i++) {
		if ((IS_ERROR(FormatStatus)) || (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize)) {
			goto out;
		}
	}
	for (i=last_sector-MAX_SECTORS_TO_CLEAR; i<last_sector; i++) {
		if ((IS_ERROR(FormatStatus)) || (write_sectors(hPhysicalDrive, SectorSize, i, 1, pBuf) != SectorSize)) {
			goto out;
		}
	}
	r = TRUE;

out:
	safe_free(pBuf);
	return r;
}

/*
 * Our own MBR, not in ms-sys
 */
BOOL WriteRufusMBR(FILE *fp)
{
	DWORD size;
	unsigned char aucRef[] = {0x55, 0xAA};
	unsigned char* rufus_mbr;

	// TODO: Will we need to edit the disk ID according to UI selection in the MBR as well?
	rufus_mbr = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_BR_MBR_BIN), _RT_RCDATA, "mbr.bin", &size, FALSE);

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
	DWORD size;
	int dt, fs;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = (0x200 + SecSize -1) / SecSize;
	FILE fake_fd = { 0 };

	if (!AnalyzeMBR(hPhysicalDrive)) return FALSE;

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
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
	if (IsChecked(IDC_BOOT)) {
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
	dt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	if ( (dt == DT_SYSLINUX_V4) || (dt == DT_SYSLINUX_V5) || ((dt == DT_ISO) && ((fs == FS_FAT16) || (fs == FS_FAT32))) ) {
		r = write_syslinux_mbr(&fake_fd);
	} else {
		if ((IS_WINPE(iso_report.winpe) && !iso_report.uses_minint) || (IsChecked(IDC_RUFUS_MBR))) {
			uprintf("Using " APPLICATION_NAME " bootable USB selection MBR\n");
			r = WriteRufusMBR(&fake_fd);
		} else {
			uprintf("Using Windows 7 MBR\n");
			r = write_win7_mbr(&fake_fd);
		}
	}

	// Tell the system we've updated the disk properties
	DeviceIoControl(hPhysicalDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL );

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
	BOOL bFreeDOS = (ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType)) == DT_FREEDOS);

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
		uprintf("Successfully added '%s' to %s\n", setupsrcdev, dst);
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
				buf[i+6] = 0x30 + ComboBox_GetCurSel(hDiskID);
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
 * Detect if a Windows Format prompt is active, by enumerating the
 * whole Windows tree and looking for the relevant popup
 */
static BOOL CALLBACK FormatPromptCallback(HWND hWnd, LPARAM lParam)
{
	char str_buf[MAX_PATH];
	HWND *hFound = (HWND*)lParam;
	static const char* security_string = "Microsoft Windows";

	// The format prompt has the popup window style
	if (GetWindowLong(hWnd, GWL_STYLE) & WS_POPUPWINDOW) {
		str_buf[0] = 0;
		GetWindowTextA(hWnd, str_buf, MAX_PATH);
		str_buf[MAX_PATH-1] = 0;
		if (safe_strcmp(str_buf, security_string) == 0) {
			*hFound = hWnd;
			return TRUE;
		}
	}
	return TRUE;
}

/*
 * When we format a drive that doesn't have any existing partitions, we can't lock it
 * prior to partitioning, which means that Windows will display a "You need to format the
 * disk in drive X: before you can use it'. We have to close that popup manually.
 */
DWORD WINAPI CloseFormatPromptThread(LPVOID param) {
	HWND hFormatPrompt;

	while(format_op_in_progress) {
		hFormatPrompt = NULL;
		EnumChildWindows(GetDesktopWindow(), FormatPromptCallback, (LPARAM)&hFormatPrompt);
		if (hFormatPrompt != NULL) {
			SendMessage(hFormatPrompt, WM_COMMAND, (WPARAM)IDCANCEL, (LPARAM)0);
			uprintf("Closed Windows format prompt\n");
		}
		Sleep(50);
	}
	ExitThread(0);
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
#define CHECK_FOR_USER_CANCEL 	if (IS_ERROR(FormatStatus)) goto out
DWORD WINAPI FormatThread(LPVOID param)
{
	int r, pt, bt, fs, dt;
	BOOL ret, use_large_fat32;
	DWORD DriveIndex = (DWORD)(uintptr_t)param;
	HANDLE hPhysicalDrive = INVALID_HANDLE_VALUE;
	HANDLE hLogicalVolume = INVALID_HANDLE_VALUE;
	SYSTEMTIME lt;
	char *bb_msg, *guid_volume = NULL;
	char drive_name[] = "?:\\";
	char logfile[MAX_PATH], *userdir;
	char wim_image[] = "?:\\sources\\install.wim";
	char efi_dst[] = "?:\\efi\\boot\\bootx64.efi";
	FILE* log_fd;

	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	dt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	use_large_fat32 = (fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32));

	PrintStatus(0, TRUE, lmprintf(MSG_225));
	hPhysicalDrive = GetPhysicalHandle(DriveIndex, TRUE, TRUE);
	if (hPhysicalDrive == INVALID_HANDLE_VALUE) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
		goto out;
	}

	// At this stage with have both a handle and a lock to the physical drive...
	drive_name[0] = GetDriveLetter(DriveIndex);
	if (drive_name[0] == ' ') {
		uprintf("No drive letter was assigned...\n");
		drive_name[0] =  GetUnusedDriveLetter();
		if (drive_name[0] == ' ') {
			uprintf("Could not find a suitable drive letter\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_ASSIGN_LETTER);
			goto out;
		}
	} else if (!DeleteVolumeMountPointA(drive_name)) {
		uprintf("Failed to delete mountpoint %s: %s\n", drive_name, WindowsErrorString());
		// Try to continue. We will bail out if this causes an issue.
	}
	uprintf("Will use '%c:' as volume mountpoint\n", drive_name[0]);

	// ...but we need a lock to the logical drive to be able to write anything to it
	hLogicalVolume = GetLogicalHandle(DriveIndex, FALSE, TRUE);
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

	PrintStatus(0, TRUE, lmprintf(MSG_226));
	AnalyzeMBR(hPhysicalDrive);
	if ((hLogicalVolume != NULL) && (hLogicalVolume != INVALID_HANDLE_VALUE)) {
		AnalyzePBR(hLogicalVolume);
	}
	UpdateProgress(OP_ANALYZE_MBR, -1.0f);

	// Zap any existing partitions. This helps prevent access errors.
	// As this creates issues with FAT16 formatted MS drives, only do this for other filesystems
	if ( (fs != FS_FAT16) && (!DeletePartitions(hPhysicalDrive)) ) {
		uprintf("Could not reset partitions\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}

	if (IsChecked(IDC_BADBLOCKS)) {
		do {
			// create a log file for bad blocks report. Since %USERPROFILE% may
			// have localized characters, we use the UTF-8 API.
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
				fprintf(log_fd, APPLICATION_NAME " bad blocks check started on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fflush(log_fd);
			}

			if (!BadBlocks(hPhysicalDrive, SelectedDrive.DiskSize,
				SelectedDrive.Geometry.BytesPerSector, ComboBox_GetCurSel(hNBPasses)+1, &report, log_fd)) {
				uprintf("Bad blocks: Check failed.\n");
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_BADBLOCKS_FAILURE);
				ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.Geometry.BytesPerSector);
				fclose(log_fd);
				_unlink(logfile);
				goto out;
			}
			uprintf("Bad Blocks: Check completed, %u bad block%s found. (%d/%d/%d errors)\n",
				report.bb_count, (report.bb_count==1)?"":"s",
				report.num_read_errors, report.num_write_errors, report.num_corruption_errors);
			r = IDOK;
			if (report.bb_count) {
				bb_msg = lmprintf(MSG_011, report.num_read_errors, report.num_write_errors,
					report.num_corruption_errors);
				fprintf(log_fd, bb_msg);
				GetLocalTime(&lt);
				fprintf(log_fd, APPLICATION_NAME " bad blocks check ended on: %04d.%02d.%02d %02d:%02d:%02d\n",
				lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
				fclose(log_fd);
				r = MessageBoxU(hMainDialog, lmprintf(MSG_012, bb_msg, logfile),
					lmprintf(MSG_010), MB_ABORTRETRYIGNORE|MB_ICONWARNING);
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
	// Close the (unmounted) volume before formatting
	if ((hLogicalVolume != NULL) && (hLogicalVolume != INVALID_HANDLE_VALUE)) {
		PrintStatus(0, TRUE, lmprintf(MSG_227));
		if (!CloseHandle(hLogicalVolume)) {
			uprintf("Could not close volume: %s\n", WindowsErrorString());
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_ACCESS_DENIED;
			goto out;
		}
	}
	hLogicalVolume = INVALID_HANDLE_VALUE;

	// TODO: (v1.4) Our start button should become cancel instead of close

	// Especially after destructive badblocks test, you must zero the MBR/GPT completely
	// before repartitioning. Else, all kind of bad things happen.
	if (!ClearMBRGPT(hPhysicalDrive, SelectedDrive.DiskSize, SelectedDrive.Geometry.BytesPerSector)) {
		uprintf("unable to zero MBR/GPT\n");
		if (!IS_ERROR(FormatStatus))
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}
	UpdateProgress(OP_ZERO_MBR, -1.0f);
	CHECK_FOR_USER_CANCEL;

	CreateThread(NULL, 0, CloseFormatPromptThread, NULL, 0, NULL);
	if (!CreatePartition(hPhysicalDrive, pt, fs, (pt==PARTITION_STYLE_MBR)&&(bt==BT_UEFI))) {
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_PARTITION_FAILURE;
		goto out;
	}
	UpdateProgress(OP_PARTITION, -1.0f);

	// Wait for the logical drive we just created to appear
	uprintf("Waiting for logical drive to reappear...\n");
	Sleep(200);
	WaitForLogical(DriveIndex);	// We try to continue even if this fails, just in case
	CHECK_FOR_USER_CANCEL;

	// If FAT32 is requested and we have a large drive (>32 GB) use 
	// large FAT32 format, else use MS's FormatEx.
	ret = use_large_fat32?FormatFAT32(DriveIndex):FormatDrive(DriveIndex);
	if (!ret) {
		// Error will be set by FormatDrive() in FormatStatus
		uprintf("Format error: %s\n", StrError(FormatStatus));
		goto out;
	}

	// Thanks to Microsoft, we must fix the MBR AFTER the drive has been formatted
	if (pt == PARTITION_STYLE_MBR) {
		PrintStatus(0, TRUE, lmprintf(MSG_228));
		if (!WriteMBR(hPhysicalDrive)) {
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

	if (IsChecked(IDC_BOOT)) {
		if (bt == BT_UEFI) {
			// For once, no need to do anything - just check our sanity
			if ( (dt != DT_ISO) || (!IS_EFI(iso_report)) || (fs > FS_FAT32) ) {
				uprintf("Spock gone crazy error!\n");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
				goto out;
			}
		} else if ((((dt == DT_WINME) || (dt == DT_FREEDOS)) && (!use_large_fat32)) || ((dt == DT_ISO) && (fs == FS_NTFS))) {
			// We still have a lock, which we need to modify the volume boot record 
			// => no need to reacquire the lock...
			hLogicalVolume = GetLogicalHandle(DriveIndex, TRUE, FALSE);
			if ((hLogicalVolume == INVALID_HANDLE_VALUE) || (hLogicalVolume == NULL)) {
				uprintf("Could not re-mount volume for partition boot record access\n");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_OPEN_FAILED;
				goto out;
			}
			// NB: if you unmount the logical volume here, XP will report error:
			// [0x00000456] The media in the drive may have changed
			PrintStatus(0, TRUE, lmprintf(MSG_229));
			if (!WritePBR(hLogicalVolume)) {
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
				goto out;
			}
			// We must close and unlock the volume to write files to it
			safe_unlockclose(hLogicalVolume);
		} else if ( (dt == DT_SYSLINUX_V4) || (dt == DT_SYSLINUX_V5) || ((dt == DT_ISO) && ((fs == FS_FAT16) || (fs == FS_FAT32))) ) {
			if (!InstallSyslinux(DriveIndex, drive_name[0])) {
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_INSTALL_FAILURE;
			}
		}
	} else {
		if (IsChecked(IDC_SET_ICON))
			SetAutorun(drive_name);
	}
	CHECK_FOR_USER_CANCEL;

	// We issue a complete remount of the filesystem at on account of:
	// - Ensuring the file explorer properly detects that the volume was updated
	// - Ensuring that an NTFS system will be reparsed so that it becomes bootable
	if (!RemountVolume(drive_name))
		goto out;
	CHECK_FOR_USER_CANCEL;

	if (IsChecked(IDC_BOOT)) {
		if ((dt == DT_WINME) || (dt == DT_FREEDOS)) {
			UpdateProgress(OP_DOS, -1.0f);
			PrintStatus(0, TRUE, lmprintf(MSG_230));
			if (!ExtractDOS(drive_name)) {
				if (!IS_ERROR(FormatStatus))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
				goto out;
			}
		} else if (dt == DT_ISO) {
			if (iso_path != NULL) {
				UpdateProgress(OP_DOS, 0.0f);
				PrintStatus(0, TRUE, lmprintf(MSG_231));
				drive_name[2] = 0;
				if (!ExtractISO(iso_path, drive_name, FALSE)) {
					if (!IS_ERROR(FormatStatus))
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANNOT_COPY;
					goto out;
				}
				if ((bt == BT_UEFI) && (!iso_report.has_efi) && (iso_report.has_win7_efi)) {
					// TODO: (v1.4.0) check ISO with EFI only
					PrintStatus(0, TRUE, lmprintf(MSG_232));
					wim_image[0] = drive_name[0];
					efi_dst[0] = drive_name[0];
					efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = 0;
					if (!CreateDirectoryA(efi_dst, 0)) {
						uprintf("Could not create directory '%s': %s\n", WindowsErrorString());
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
					} else {
						efi_dst[sizeof(efi_dst) - sizeof("\\bootx64.efi")] = '\\';
						if (!WimExtractFile(wim_image, 1, "Windows\\Boot\\EFI\\bootmgfw.efi", efi_dst)) {
							uprintf("Failed to setup Win7 EFI boot\n");
							FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
						}
					}
				}
			}
			if ( (bt == BT_BIOS) && (IS_WINPE(iso_report.winpe)) ) {
				// Apply WinPe fixup
				if (!SetupWinPE(drive_name[0]))
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_PATCH);
			}
		}
		UpdateProgress(OP_FINALIZE, -1.0f);
		PrintStatus(0, TRUE, lmprintf(MSG_233));
		if (IsChecked(IDC_SET_ICON))
			SetAutorun(drive_name);
		// Issue another complete remount before we exit, to ensure we're clean
		RemountVolume(drive_name);
		// NTFS fixup (WinPE/AIK images don't seem to boot without an extra checkdisk)
		if ((dt == DT_ISO) && (fs == FS_NTFS)) {
			CheckDisk(drive_name[0]);
			UpdateProgress(OP_FINALIZE, -1.0f);
		}
	}

out:
	safe_free(guid_volume);
	SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);	// This can take a while
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	ExitThread(0);
}
