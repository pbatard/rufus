/*
 * Rufus: The Resourceful USB Formatting Utility
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
 * 
 * Device enumeration based in part on TestUSBDriveEject.cpp by ahmd:
 * http://www.codeguru.com/forum/showpost.php?p=1951973&postcount=7
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
#include <windowsx.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <commctrl.h>
#include <setupapi.h>
#include <winioctl.h>
#include <process.h>
#include <dbt.h>
#include <io.h>

// http://git.kernel.org/?p=fs/ext2/e2fsprogs.git;a=blob;f=misc/badblocks.c
// http://ms-sys.sourceforge.net/
// http://thestarman.pcministry.com/asm/mbr/MSWIN41.htm
// http://www.c-jump.com/CIS24/Slides/FAT/lecture.html#F01_0130_sector_assignments

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"
#include "sys_types.h"
#include "br.h"
#include "fat32.h"
#include "file.h"

#if !defined(GUID_DEVINTERFACE_DISK)
const GUID GUID_DEVINTERFACE_DISK = { 0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b} };
#endif

// For MinGW
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif
#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH];
HWND hStatus;
float fScale = 1.0f;

BOOL bBootable;
BOOL bQuickFormat;

struct {
	DWORD DeviceNumber;
	LONGLONG DiskSize;
	LONGLONG PartitionSize;
	DISK_GEOMETRY Geometry;
	DWORD FirstSector;
	enum _FSType FSType;
} SelectedDrive;

static HWND hDeviceList, hCapacity, hFileSystem, hClusterSize, hLabel;
static HWND hDeviceTooltip = NULL, hFSTooltip = NULL;
static StrArray DriveID, DriveLabel;
static DWORD FormatStatus;

#ifdef RUFUS_DEBUG
void _uprintf(const char *format, ...)
{
	char buf[4096], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspace(p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	OutputDebugStringA(buf);
}
#endif

void DumpBufferHex(unsigned char *buffer, size_t size)
{
	size_t i, j, k;
	char line[80] = "";

	for (i=0; i<size; i+=16) {
		uprintf("%s\n", line);
		line[0] = 0;
		sprintf(&line[strlen(line)], "  %08x  ", (unsigned int)i);
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				sprintf(&line[strlen(line)], "%02x", buffer[i+j]);
			} else {
				sprintf(&line[strlen(line)], "  ");
			}
			sprintf(&line[strlen(line)], " ");
		}
		sprintf(&line[strlen(line)], " ");
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				if ((buffer[i+j] < 32) || (buffer[i+j] > 126)) {
					sprintf(&line[strlen(line)], ".");
				} else {
					sprintf(&line[strlen(line)], "%c", buffer[i+j]);
				}
			}
		}
	}
	uprintf("%s\n", line);
}

void PrintStatus(const char *format, ...)
{
	char buf[256], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-1, format, args); // room for NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-1:n;

	while((p>buf) && (isspace(p[-1])))
		*--p = '\0';

	*p   = '\0';

	SetDlgItemTextU(hMainDialog, IDC_STATUS, buf);
}

/*
 * Convert a partition type to its human readable form using
 * (slightly modified) entries from GNU fdisk
 */
static const char* GetPartitionType(BYTE Type)
{
	int i;
	for (i=0; i<ARRAYSIZE(msdos_systypes); i++) {
		if (msdos_systypes[i].type == Type)
			return msdos_systypes[i].name;
	}
	return "Unknown";
}

/*
 * Open a drive with optional write access - returns a drive HANDLE and the drive letter
 * or INVALID_HANDLE_VALUE (/!\ which is != NULL /!\) on failure
 * This call is quite risky (left unchecked, inadvertently passing 0 as index would
 * return a handle to C:, which we might then proceed to unknowingly repartition!),
 * so we apply the following mitigation factors:
 * - Valid indexes must belong to a specific range [DRIVE_INDEX_MIN; DRIVE_INDEX_MAX]
 * - When opening for write access, we lock the volume. If that fails, which would
 *   typically be the case on C:\ or any other drive in use, we report failure
 * - We report the full path of any drive that was successfully opened for write acces
 */
static HANDLE GetDriveHandle(DWORD DriveIndex, char* DriveLetter, BOOL bWriteAccess, BOOL bLockDrive)
{
	BOOL r;
	DWORD size;
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	STORAGE_DEVICE_NUMBER_REDEF device_number = {0};
	char drives[26*4];	/* "D:\", "E:\", etc. */
	char *drive = drives;
	char logical_drive[] = "\\\\.\\#:";
	char physical_drive[24];

	if ((DriveIndex < DRIVE_INDEX_MIN) || (DriveIndex > DRIVE_INDEX_MAX)) {
		uprintf("WARNING: Bad index value. Please check the code!\n");
	}
	DriveIndex -= DRIVE_INDEX_MIN;

	// If no drive letter is requested, open a phyical drive
	if (DriveLetter == NULL) {
		safe_sprintf(physical_drive, sizeof(physical_drive), "\\\\.\\PHYSICALDRIVE%d", DriveIndex);
		hDrive = CreateFileA(physical_drive, GENERIC_READ|(bWriteAccess?GENERIC_WRITE:0),
			FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
		if (hDrive == INVALID_HANDLE_VALUE) {
			uprintf("Could not open drive %s: %s\n", physical_drive, WindowsErrorString());
			goto out;
		}
		if (bWriteAccess) {
			uprintf("Caution: Opened %s drive for write access\n", physical_drive);
		}
	} else {
		*DriveLetter = ' ';
		size = GetLogicalDriveStringsA(sizeof(drives), drives);
		if (size == 0) {
			uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString());
			goto out;
		}
		if (size > sizeof(drives)) {
			uprintf("GetLogicalDriveStrings: buffer too small (required %d vs %d)\n", size, sizeof(drives));
			goto out;
		}

		hDrive = INVALID_HANDLE_VALUE;
		for ( ;*drive; drive += safe_strlen(drive)+1) {
			if (!isalpha(*drive))
				continue;
			*drive = (char)toupper((int)*drive);
			if (*drive < 'C') {
				continue;
			}
			safe_sprintf(logical_drive, sizeof(logical_drive), "\\\\.\\%c:", drive[0]);
			hDrive = CreateFileA(logical_drive, GENERIC_READ|(bWriteAccess?GENERIC_WRITE:0),
				FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
			if (hDrive == INVALID_HANDLE_VALUE) {
				uprintf("Warning: could not open drive %c: %s\n", drive[0], WindowsErrorString());
				continue;
			}

			r = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL,
				0, &device_number, sizeof(device_number), &size, NULL);
			if ((!r) || (size <= 0)) {
				uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER failed for device %s: %s\n", logical_drive, WindowsErrorString());
			} else if (device_number.DeviceNumber == DriveIndex) {
				break;
			}
			safe_closehandle(hDrive);
		}
		if (hDrive == INVALID_HANDLE_VALUE) {
			goto out;
		}
		if (bWriteAccess) {
			uprintf("Caution: Opened %s drive for write access\n", logical_drive);
		}
		*DriveLetter = *drive?*drive:' ';
	}

	if ((bLockDrive) && (!DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL))) {
		uprintf("Could not get exclusive access to %s: %s\n", logical_drive, WindowsErrorString());
		safe_closehandle(hDrive);
		goto out;
	}

out:
	return hDrive;
}

static __inline BOOL UnlockDrive(HANDLE hDrive)
{
	DWORD size;
	return DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL);
}

/*
 * Return the drive letter and volume label
 */
static BOOL GetDriveLabel(DWORD DriveIndex, char* letter, char** label)
{
	HANDLE hDrive;
	char DrivePath[] = "#:\\";
	static char volume_label[MAX_PATH+1];

	*label = STR_NO_LABEL;

	hDrive = GetDriveHandle(DriveIndex, DrivePath, FALSE, FALSE);
	if (hDrive == INVALID_HANDLE_VALUE)
		return FALSE;
	safe_closehandle(hDrive);
	*letter = DrivePath[0];

	if (GetVolumeInformationA(DrivePath, volume_label, sizeof(volume_label), NULL, NULL, NULL, NULL, 0) && *volume_label) {
		*label = volume_label;
	}

	return TRUE;
}

static void SetClusterSizes(enum _FSType FSType)
{
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	// Don't ask me - just following the MS standard here
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "Default allocation size"), 0x1000));
	// TODO set value above according to FS selected and default cluster sizes from
	// http://support.microsoft.com/kb/140365
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "512 bytes"), 0x200));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "1024 bytes"), 0x400));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "2048 bytes"), 0x800));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "4096 bytes"), 0x1000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "8192 bytes"), 0x2000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "16 kilobytes"), 0x4000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "32 kilobytes"), 0x8000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "64 kilobytes"), 0x10000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "128 kilobytes"), 0x20000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "256 kilobytes"), 0x40000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "512 kilobytes"), 0x80000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "1024 kilobytes"), 0x100000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "2048 kilobytes"), 0x200000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "4096 kilobytes"), 0x400000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "8192 kilobytes"), 0x800000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "16 megabytes"), 0x1000000));
	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, "32 megabytes"), 0x2000000));
	// TODO set value according to disk props
	IGNORE_RETVAL(ComboBox_SetCurSel(hClusterSize, 0));
}

/*
 * Fill the drive properties (size, FS, etc)
 */
static BOOL GetDriveInfo(void)
{
	BOOL r;
	HANDLE hDrive;
	DWORD size;
	BYTE geometry[128], layout[1024];
	void* disk_geometry = (void*)geometry;
	void* drive_layout = (void*)layout;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)disk_geometry;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)drive_layout;
	char DrivePath[] = "#:\\", tmp[128], fs_type[32];
	DWORD i, nb_partitions = 0;

	SelectedDrive.DiskSize = 0;

	hDrive = GetDriveHandle(SelectedDrive.DeviceNumber, DrivePath, FALSE, FALSE);
	if (hDrive == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 
			NULL, 0, geometry, sizeof(geometry), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}
	SelectedDrive.DiskSize = DiskGeometry->DiskSize.QuadPart;
	memcpy(&SelectedDrive.Geometry, &DiskGeometry->Geometry, sizeof(DISK_GEOMETRY));
	uprintf("Cylinders: %lld, TracksPerCylinder: %d, SectorsPerTrack: %d, BytesPerSector: %d\n",
		DiskGeometry->Geometry.Cylinders, DiskGeometry->Geometry.TracksPerCylinder,
		DiskGeometry->Geometry.SectorsPerTrack, DiskGeometry->Geometry.BytesPerSector);

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 
			NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed: %s\n", WindowsErrorString());
	} else {
		DestroyTooltip(hFSTooltip);
		hFSTooltip = NULL;
		switch (DriveLayout->PartitionStyle) {
		case PARTITION_STYLE_MBR:
			for (i=0; i<DriveLayout->PartitionCount; i++) {
				if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
					uprintf("Partition #%d:\n", ++nb_partitions);
					if (hFSTooltip == NULL) {
						// TODO: provide all partitions FS on tooltip, not just the one
						safe_sprintf(tmp, sizeof(tmp), "Current file system: %s (0x%02x)",
							GetPartitionType(DriveLayout->PartitionEntry[i].Mbr.PartitionType),
							DriveLayout->PartitionEntry[i].Mbr.PartitionType);
						hFSTooltip = CreateTooltip(hFileSystem, tmp, -1);
					}
					uprintf("  Type: %s (0x%02x)\n  Boot: %s\n  Recognized: %s\n  Hidden Sectors: %d\n",
						GetPartitionType(DriveLayout->PartitionEntry[i].Mbr.PartitionType),
						DriveLayout->PartitionEntry[i].Mbr.PartitionType,
						DriveLayout->PartitionEntry[i].Mbr.BootIndicator?"Yes":"No",
						DriveLayout->PartitionEntry[i].Mbr.RecognizedPartition?"Yes":"No",
						DriveLayout->PartitionEntry[i].Mbr.HiddenSectors);
				}
			}
			uprintf("Partition type: MBR, NB Partitions: %d\n", nb_partitions);
			break;
		case PARTITION_STYLE_GPT:
			uprintf("Partition type: GPT, NB Partitions: %d\n", DriveLayout->PartitionCount);
			break;
		default:
			uprintf("Partition type: RAW\n");
			break;
		}
	}

	safe_closehandle(hDrive);

	SelectedDrive.FSType = FS_DEFAULT;

	if (GetVolumeInformationA(DrivePath, NULL, 0, NULL, NULL, NULL,
		fs_type, sizeof(fs_type))) {
		// re-select existing FS if it's one we know
		for (SelectedDrive.FSType=FS_FAT16; SelectedDrive.FSType<FS_MAX; SelectedDrive.FSType++) {
			tmp[0] = 0;
			IGNORE_RETVAL(ComboBox_GetLBTextU(hFileSystem, SelectedDrive.FSType, tmp));
			if (safe_strcmp(fs_type, tmp) == 0) {
				IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, SelectedDrive.FSType));
				break;
			}
		}
		if (SelectedDrive.FSType == FS_MAX)
			SelectedDrive.FSType = FS_DEFAULT;
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, SelectedDrive.FSType));
	SetClusterSizes(SelectedDrive.FSType);

	return TRUE;
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	double HumanReadableSize;
	char capacity[64];
	char *suffix[] = { "KB", "MB", "GB", "TB", "PB"};
	char proposed_label[16], no_label[] = STR_NO_LABEL;
	int i;

	IGNORE_RETVAL(ComboBox_ResetContent(hCapacity));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	SetWindowTextA(hLabel, "");
	DestroyTooltip(hDeviceTooltip);
	DestroyTooltip(hFSTooltip);
	hDeviceTooltip = NULL;
	hFSTooltip = NULL;
	memset(&SelectedDrive, 0, sizeof(SelectedDrive));

	if (ComboIndex < 0) {
		return TRUE;
	}

	// Populate the FileSystem values
	IGNORE_RETVAL(ComboBox_AddStringU(hFileSystem, "FAT"));
	IGNORE_RETVAL(ComboBox_AddStringU(hFileSystem, "FAT32"));
	IGNORE_RETVAL(ComboBox_AddStringU(hFileSystem, "NTFS"));

	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, ComboIndex);
	if (!GetDriveInfo())
		return FALSE;

	HumanReadableSize = (double)SelectedDrive.DiskSize;
	for (i=0; i<ARRAYSIZE(suffix); i++) {
		HumanReadableSize /= 1024.0;
		if (HumanReadableSize < 512.0) {
			safe_sprintf(capacity, sizeof(capacity), "%0.2f %s", HumanReadableSize, suffix[i]);
			break;
		}
	}
	IGNORE_RETVAL(ComboBox_AddStringU(hCapacity, capacity));
	IGNORE_RETVAL(ComboBox_SetCurSel(hCapacity, 0));
	hDeviceTooltip = CreateTooltip(hDeviceList, DriveID.Table[ComboIndex], -1);

	// If no existing label is available, propose one according to the size (eg: "256MB", "8GB")
	if (safe_strcmp(no_label, DriveLabel.Table[ComboIndex]) == 0) {
		if (HumanReadableSize < 1.0) {
			HumanReadableSize *= 1024.0;
			i--;
		}
		// If we're beneath the tolerance, round proposed label to an integer, if not, show one decimal point
		if (fabs(HumanReadableSize / ceil(HumanReadableSize) - 1.0) < PROPOSEDLABEL_TOLERANCE) {
			safe_sprintf(proposed_label, sizeof(proposed_label), "%0.0f%s", ceil(HumanReadableSize), suffix[i]);
		} else {
			safe_sprintf(proposed_label, sizeof(proposed_label), "%0.1f%s", HumanReadableSize, suffix[i]);
		}
		SetWindowTextA(hLabel, proposed_label);
	} else {
		SetWindowTextA(hLabel, DriveLabel.Table[ComboIndex]);
	}

	return TRUE;
}

/*
 * Create a partition table
 */
static BOOL CreatePartition(HANDLE hDrive)
{
	BYTE layout[sizeof(DRIVE_LAYOUT_INFORMATION_EX) + 3*sizeof(PARTITION_INFORMATION_EX)] = {0};
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayoutEx = (PDRIVE_LAYOUT_INFORMATION_EX)layout;
	BOOL r;
	DWORD size;

	PrintStatus("Partitioning...");
	DriveLayoutEx->PartitionStyle = PARTITION_STYLE_MBR;
	DriveLayoutEx->PartitionCount = 4;	// Must be multiple of 4 for MBR
	DriveLayoutEx->Mbr.Signature = GetTickCount();
	DriveLayoutEx->PartitionEntry[0].PartitionStyle = PARTITION_STYLE_MBR;
	DriveLayoutEx->PartitionEntry[0].StartingOffset.QuadPart = 
		SelectedDrive.Geometry.BytesPerSector * SelectedDrive.Geometry.SectorsPerTrack;
	DriveLayoutEx->PartitionEntry[0].PartitionLength.QuadPart = SelectedDrive.DiskSize -
		DriveLayoutEx->PartitionEntry[0].StartingOffset.QuadPart;
	DriveLayoutEx->PartitionEntry[0].PartitionNumber = 1;
	DriveLayoutEx->PartitionEntry[0].RewritePartition = TRUE;
	DriveLayoutEx->PartitionEntry[0].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack;
	switch (ComboBox_GetCurSel(hFileSystem)) {
	case FS_FAT16:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x0e;	// FAT16 LBA
		break;
	case FS_NTFS:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x07;	// NTFS
		break;
	default:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x0c;	// FAT32 LBA
		break;
	}
	// For the remaining partitions, PartitionStyle & PartitionType have already
	// been zeroed => set to MBR/unused

	r = DeviceIoControl(hDrive, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, 
			layout, sizeof(layout), NULL, 0, &size, NULL );
	if (!r) {
		uprintf("IOCTL_DISK_SET_DRIVE_LAYOUT_EX failed: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	return TRUE;
}

/*
 * FormatEx callback. Return FALSE to halt operations
 */
static BOOLEAN __stdcall FormatExCallback(FILE_SYSTEM_CALLBACK_COMMAND Command, DWORD Action, PVOID Data)
{
	DWORD* percent;
	int task_number = 0;

	if (IS_ERROR(FormatStatus))
		return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)Data;
		PostMessage(hMainDialog, UM_FORMAT_PROGRESS, (WPARAM)*percent, (LPARAM)0);
		uprintf("%d percent completed.\n", *percent);
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		uprintf("Format task %d/? completed.\n", ++task_number);
		break;
	case FCC_DONE:
		if(*(BOOLEAN*)Data == FALSE) {
			uprintf("Error while formatting.\n");
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_GEN_FAILURE;
		}
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
		uprintf("%s\n", ((PTEXTOUTPUT)Data)->Output);
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

	wDriveRoot[0] = (WCHAR)DriveLetter;
	PrintStatus("Formatting...");
	PF_INIT_OR_OUT(FormatEx, fmifs);

	// TODO: properly set MediaType
	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	uprintf("Using cluster size: %d bytes\n", ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)));
	pfFormatEx(wDriveRoot, RemovableMedia, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)), FormatExCallback);
	if (!IS_ERROR(FormatStatus)) {
		uprintf("Format completed.\n");
		r = TRUE;
	}

out:
	return r;
}

/*
 * Process the MBR
 */
static BOOL ProcessMBR(HANDLE hPhysicalDrive)
{
	BOOL r = FALSE;
	HANDLE hDrive = hPhysicalDrive;
	FILE fake_fd;
	unsigned char* buf = NULL;
	size_t SecSize = SelectedDrive.Geometry.BytesPerSector;
	size_t nSecs = 0x200/min(0x200, SelectedDrive.Geometry.BytesPerSector);

	if (SecSize * nSecs != 0x200) {
		uprintf("Seriously? A drive where sector size is not a power of 2?!?\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_SUPPORTED;
		goto out;
	}

	PrintStatus("Processing MBR...\n");

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = SelectedDrive.Geometry.BytesPerSector;
	uprintf("I'm %sa boot record\n", is_br(&fake_fd)?"":"NOT ");

	// FormatEx rewrites the MBR and removes the LBA attribute of FAT16
	// and FAT32 partitions - we need to correct this in the MBR
	// TODO: something else for bootable GPT
	buf = (unsigned char*)malloc(SecSize * nSecs);
	if (buf == NULL) {
		uprintf("Could not allocate memory for MBR");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_NOT_ENOUGH_MEMORY;
		goto out;
	}

	if (!read_sectors(hDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize)) {
		uprintf("Could not read MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_READ_FAULT;
		goto out;
	}
	DumpBufferHex(buf, 0x200);
	switch (ComboBox_GetCurSel(hFileSystem)) {
	// TODO: check for 0x06 & 0x0b?
	case FS_FAT16:
		buf[0x1c2] = 0x0e;
		break;
	case FS_FAT32:
		buf[0x1c2] = 0x0c;
		break;
	}

	if (!write_sectors(hDrive, SelectedDrive.Geometry.BytesPerSector, 0, nSecs, buf, SecSize)) {
		uprintf("Could not write MBR\n");
		FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT;
		goto out;
	}

	r = TRUE;

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
static void __cdecl FormatThread(void* param)
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
	// TODO: disable indexing on NTFS

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
	if (!ProcessMBR(hPhysicalDrive)) {
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
		// TODO: check return value & set bootblock
		ExtractMSDOS(drive_name);
	}

out:
	safe_unlockclose(hLogicalVolume);
	safe_unlockclose(hPhysicalDrive);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
	_endthread();
}

/*
 * Refresh the list of USB devices
 */
static BOOL GetUSBDevices(void)
{
	BOOL r;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	STORAGE_DEVICE_NUMBER_REDEF device_number;
	DWORD size, i, j, datatype;
	HANDLE hDrive;
	char drive_letter;
	char *label, entry[MAX_PATH], buffer[MAX_PATH];
	const char* usbstor_name = "USBSTOR";

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));
	StrArrayClear(&DriveID);
	StrArrayClear(&DriveLabel);

	dev_info = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		uprintf("SetupDiGetClassDevs (Interface) failed: %d\n", WindowsErrorString());
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %d\n", WindowsErrorString());
			continue;
		}

		if (safe_strcmp(buffer, usbstor_name) != 0)
			continue;
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %d\n", WindowsErrorString());
			continue;
		}
		uprintf("Found drive '%s'\n", buffer);

		devint_data.cbSize = sizeof(devint_data);
		hDrive = INVALID_HANDLE_VALUE;
		devint_detail_data = NULL;
		for (j=0; ;j++) {
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &GUID_DEVINTERFACE_DISK, j, &devint_data)) {
				if(GetLastError() != ERROR_NO_MORE_ITEMS) {
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString());
				}
				break;
			}

			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL)) {
				if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size);
					if (devint_detail_data == NULL) {
						uprintf("unable to allocate data for SP_DEVICE_INTERFACE_DETAIL_DATA\n");
						return FALSE;
					}
					devint_detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
				} else {
					uprintf("SetupDiGetDeviceInterfaceDetail (dummy) failed: %s\n", WindowsErrorString());
					continue;
				}
			}
			if(!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, devint_detail_data, size, &size, NULL)) {
				uprintf("SetupDiGetDeviceInterfaceDetail (actual) failed: %s\n", WindowsErrorString());
				continue;
			}

			hDrive = CreateFileA(devint_detail_data->DevicePath, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if(hDrive == INVALID_HANDLE_VALUE) {
				uprintf("could not open '%s': %s\n", devint_detail_data->DevicePath, WindowsErrorString()); 
				continue;
			}

			memset(&device_number, 0, sizeof(device_number));
			r = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, 
						NULL, 0, &device_number, sizeof(device_number), &size, NULL );
			if (!r || size <= 0) {
				uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER (GetUSBDevices) failed: %s\n", WindowsErrorString());
				continue;
			}

			if (GetDriveLabel(device_number.DeviceNumber + DRIVE_INDEX_MIN, &drive_letter, &label)) {
				// Must ensure that the combo box is UNSORTED for indexes to be the same
				StrArrayAdd(&DriveID, buffer);
				StrArrayAdd(&DriveLabel, label);
				safe_sprintf(entry, sizeof(entry), "%s (%c:)", label, drive_letter);
				IGNORE_RETVAL(ComboBox_SetItemData(hDeviceList, ComboBox_AddStringU(hDeviceList, entry),
					device_number.DeviceNumber + DRIVE_INDEX_MIN));
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}
		}
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDeviceList, 0));
	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);

	return TRUE;
}

/* Toggle controls according to operation */
static void EnableControls(BOOL bEnable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_DEVICE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CAPACITY), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_DOSSTARTUP), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), bEnable);
	SetDlgItemTextA(hMainDialog, IDCANCEL, bEnable?"Close":"Cancel");
}

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	DRAWITEMSTRUCT* pDI;
	int nDeviceIndex;
	DWORD DeviceNum;
	char str[MAX_PATH], tmp[128];
	static uintptr_t format_thid = -1L;
	static HWND hProgress;
	static LONG ProgressStyle = 0;

	switch (message) {

	case WM_DEVICECHANGE:
		if ( (format_thid == -1L) &&
			 ((wParam == DBT_DEVICEARRIVAL) || (wParam == DBT_DEVICEREMOVECOMPLETE)) ) {
			GetUSBDevices();
			return (INT_PTR)TRUE;
		}
		break;

	case WM_INITDIALOG:
		hMainDialog = hDlg;
		hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
		hCapacity = GetDlgItem(hDlg, IDC_CAPACITY);
		hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
		hClusterSize = GetDlgItem(hDlg, IDC_CLUSTERSIZE);
		hLabel = GetDlgItem(hDlg, IDC_LABEL);
		hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
		// High DPI scaling
		hDC = GetDC(hDlg);
		fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
		ReleaseDC(hDlg, hDC);
		// Create the status line
		CreateStatusBar();
		// Display the version in the right area of the status bar
		SendMessageA(GetDlgItem(hDlg, IDC_STATUS), SB_SETTEXTA, SBT_OWNERDRAW | 1, (LPARAM)APP_VERSION);
		// We'll switch the progressbar to marquee and back => keep a copy of current style
		ProgressStyle = GetWindowLong(hProgress, GWL_STYLE);
		// Create the string array
		StrArrayCreate(&DriveID, MAX_DRIVES);
		StrArrayCreate(&DriveLabel, MAX_DRIVES);
		// Set the quick format checkbox
		CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
		GetUSBDevices();
		return (INT_PTR)TRUE;

	// Change the colour of the version text in the status bar
	case WM_DRAWITEM:
		if (wParam == IDC_STATUS) {
			pDI = (DRAWITEMSTRUCT*)lParam;
			SetBkMode(pDI->hDC, TRANSPARENT);
			SetTextColor(pDI->hDC, GetSysColor(COLOR_3DSHADOW));
			pDI->rcItem.top += (int)(2.0f * fScale);
			pDI->rcItem.left += (int)(4.0f * fScale);
			DrawTextExA(pDI->hDC, APP_VERSION, -1, &pDI->rcItem, DT_LEFT, NULL);
			return (INT_PTR)TRUE;
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDOK:			// close application
		case IDCANCEL:
			if (format_thid != -1L) {
				if (MessageBoxA(hMainDialog, "Cancelling may leave the device in an UNUSABLE state.\r\n"
					"If you are sure you want to cancel, click YES. Otherwise, click NO.",
					RUFUS_CANCELBOX_TITLE, MB_YESNO|MB_ICONWARNING) == IDYES) {
					// Operation may have completed in the meantime
					if (format_thid != -1L) {
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
						PrintStatus("Cancelling - please wait...");
					}
				}
				return (INT_PTR)TRUE;
			}
			PostQuitMessage(0);
			StrArrayDestroy(&DriveID);
			StrArrayDestroy(&DriveLabel);
			DestroyAllTooltips();
			EndDialog(hDlg, 0);
			break;
		case IDC_ABOUT:
			CreateAboutBox();
			break;
		case IDC_DEVICE:
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				PrintStatus("%d device%s found.", ComboBox_GetCount(hDeviceList),
					(ComboBox_GetCount(hDeviceList)!=1)?"s":"");
				PopulateProperties(ComboBox_GetCurSel(hDeviceList));
				break;
			}
			break;
		case IDC_START:
			if (format_thid != -1L) {
				return (INT_PTR)TRUE;
			}
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			if (nDeviceIndex != CB_ERR) {
				GetWindowTextA(hDeviceList, tmp, sizeof(tmp));
				safe_sprintf(str, sizeof(str), "WARNING: ALL DATA ON DEVICE %s\r\nWILL BE DESTROYED.\r\n"
					"To continue with this operation, click OK. To quit click CANCEL.", tmp);
				if (MessageBoxA(hMainDialog, str, "Rufus", MB_OKCANCEL|MB_ICONWARNING) == IDOK) {
					// Disable all controls except cancel
					EnableControls(FALSE);
					// Handle marquee progress bar on quickformat
					SetWindowLongPtr(hProgress, GWL_STYLE, ProgressStyle | (IsChecked(IDC_QUICKFORMAT)?PBS_MARQUEE:0));
					if (IsChecked(IDC_QUICKFORMAT)) {
						SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 0);
					}
					DeviceNum =  (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
					FormatStatus = 0;
					format_thid = _beginthread(FormatThread, 0, (void*)(uintptr_t)DeviceNum);
					if (format_thid == -1L) {
						uprintf("Unable to start formatting thread");
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANT_START_THREAD;
						PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
					}
				}
			}
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	case WM_CLOSE:
		if (format_thid != -1L) {
			return (INT_PTR)TRUE;
		}
		PostQuitMessage(0);
		break;

	case UM_FORMAT_PROGRESS:
		SendMessage(hProgress, PBM_SETPOS, wParam, lParam);
		return (INT_PTR)TRUE;

	case UM_FORMAT_COMPLETED:
		format_thid = -1L;
		// Close the cancel MessageBox if active
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), RUFUS_CANCELBOX_TITLE), WM_COMMAND, IDNO, 0);
		if (IsChecked(IDC_QUICKFORMAT)) {
			SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
			SetWindowLongPtr(hProgress, GWL_STYLE, ProgressStyle);
			// This is the only way to achieve instantenous progress transition
			SendMessage(hProgress, PBM_SETRANGE, 0, 101<<16);
			SendMessage(hProgress, PBM_SETPOS, 101, 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, 100<<16);
		}
		SendMessage(hProgress, PBM_SETPOS, FormatStatus?0:100, 0);
		EnableControls(TRUE);
		GetUSBDevices();
		PrintStatus(!IS_ERROR(FormatStatus)?"DONE":
			((SCODE_CODE(FormatStatus)==ERROR_CANCELLED)?"Cancelled":"FAILED"));
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

/*
 * Application Entrypoint
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	HANDLE mutex = NULL;
	HWND hDlg = NULL;
	MSG msg;

	uprintf("*** RUFUS INIT ***\n");

	// Prevent 2 applications from running at the same time
	mutex = CreateMutexA(NULL, TRUE, "Global/RUFUS");
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS))
	{
		MessageBoxA(NULL, "Another Rufus application is running.\n"
			"Please close the first application before running another one.",
			"Other instance detected", MB_ICONSTOP);
		return 0;
	}

	// Save instance of the application for further reference
	hMainInstance = hInstance;

	// Initialize COM for folder selection
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

	// Create the main Window
	if ( (hDlg = CreateDialogA(hInstance, MAKEINTRESOURCEA(IDD_DIALOG), NULL, MainCallback)) == NULL ) {
		MessageBoxA(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
		goto out;
	}
	CenterDialog(hDlg);
	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing
	while(GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

out:
	CloseHandle(mutex);
	uprintf("*** RUFUS EXIT ***\n");

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
