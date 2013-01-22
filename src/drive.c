/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
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
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"
#include "sys_types.h"

/*
 * Globals
 */
RUFUS_DRIVE_INFO SelectedDrive;
extern BOOL enable_fixed_disks;

/*
 * Open a drive or volume with optional write and lock access
 * Returns INVALID_HANDLE_VALUE (/!\ which is DIFFERENT from NULL /!\) on failure.
 * This call is quite risky (left unchecked, inadvertently passing 0 as index would
 * return a handle to C:, which we might then proceed to unknowingly repartition!),
 * so we apply the following mitigation factors:
 * - Valid indexes must belong to a specific range [DRIVE_INDEX_MIN; DRIVE_INDEX_MAX]
 * - When opening for write access, we lock the volume. If that fails, which would
 *   typically be the case on C:\ or any other drive in use, we report failure
 * - We report the full path of any drive that was successfully opened for write acces
 */
HANDLE GetDriveHandle(DWORD DriveIndex, char* DriveLetter, BOOL bWriteAccess, BOOL bLockDrive)
{
	BOOL r;
	DWORD size;
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	STORAGE_DEVICE_NUMBER_REDEF device_number = {0};
	UINT drive_type;
	char drives[26*4];	/* "D:\", "E:\", etc. */
	char *drive = drives;
	char logical_drive[] = "\\\\.\\#:";
	char physical_drive[24];

	if ((DriveIndex < DRIVE_INDEX_MIN) || (DriveIndex > DRIVE_INDEX_MAX)) {
		uprintf("WARNING: Bad index value. Please check the code!\n");
	}
	DriveIndex -= DRIVE_INDEX_MIN;

	// If no drive letter is requested, open a physical drive
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

			/* IOCTL_STORAGE_GET_DEVICE_NUMBER's STORAGE_DEVICE_NUMBER.DeviceNumber is
			   not unique! An HDD, a DVD and probably other drives can have the same
			   value there => Use GetDriveType() to filter out unwanted devices.
			   See https://github.com/pbatard/rufus/issues/32 for details. */
			drive_type = GetDriveTypeA(drive);
			// NB: the HP utility allows drive_type == DRIVE_FIXED, which we don't allow by default
			// Using Alt-F in Rufus does enable listing, but this mode is unsupported.
			if ((drive_type != DRIVE_REMOVABLE) && ((!enable_fixed_disks) || (drive_type != DRIVE_FIXED)))
				continue;

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
				uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER failed for device %s: %s\n",
					logical_drive, WindowsErrorString());
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
		uprintf("Could not get exclusive access to %s %s\n", logical_drive, WindowsErrorString());
		safe_closehandle(hDrive);
		goto out;
	}

out:
	return hDrive;
}

/*
 * Return the drive letter and volume label
 */
BOOL GetDriveLabel(DWORD DriveIndex, char* letter, char** label)
{
	HANDLE hDrive, hPhysical;
	DWORD size;
	char AutorunPath[] = "#:\\autorun.inf", *AutorunLabel = NULL;
	wchar_t wDrivePath[] = L"#:\\";
	wchar_t wVolumeLabel[MAX_PATH+1];
	static char VolumeLabel[MAX_PATH+1];

	*label = STR_NO_LABEL;

	hDrive = GetDriveHandle(DriveIndex, letter, FALSE, FALSE);
	if (hDrive == INVALID_HANDLE_VALUE)
		return FALSE;
	safe_closehandle(hDrive);
	AutorunPath[0] = *letter;
	wDrivePath[0] = *letter;

	// Try to read an extended label from autorun first. Fallback to regular label if not found.
	// In the case of card readers with no card, users can get an annoying popup asking them
	// to insert media. Use IOCTL_STORAGE_CHECK_VERIFY to prevent this
	hPhysical = GetDriveHandle(DriveIndex, NULL, FALSE, FALSE);
	if (DeviceIoControl(hPhysical, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &size, NULL))
		AutorunLabel = get_token_data_file("label", AutorunPath);
	else if (GetLastError() == ERROR_NOT_READY)
		uprintf("Ignoring autorun.inf label for drive %c: %s\n", *letter,
		(HRESULT_CODE(GetLastError()) == ERROR_NOT_READY)?"No media":WindowsErrorString());
	safe_closehandle(hPhysical);
	if (AutorunLabel != NULL) {
		uprintf("Using autorun.inf label for drive %c: '%s'\n", *letter, AutorunLabel);
		strncpy(VolumeLabel, AutorunLabel, sizeof(VolumeLabel));
		safe_free(AutorunLabel);
		*label = VolumeLabel;
	} else if (GetVolumeInformationW(wDrivePath, wVolumeLabel, ARRAYSIZE(wVolumeLabel),
		NULL, NULL, NULL, NULL, 0) && *wVolumeLabel) {
		wchar_to_utf8_no_alloc(wVolumeLabel, VolumeLabel, sizeof(VolumeLabel));
		*label = VolumeLabel;
	}

	return TRUE;
}

BOOL UnmountDrive(HANDLE hDrive)
{
	DWORD size;

	if (!DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &size, NULL)) {
		uprintf("Could not ummount drive: %s\n", WindowsErrorString());
		return FALSE;
	}
	return TRUE;
}

/* MinGW is unhappy about accessing partitions beside the first unless we redef */
typedef struct _DRIVE_LAYOUT_INFORMATION_EX4 {
	DWORD PartitionStyle;
	DWORD PartitionCount;
	union {
		DRIVE_LAYOUT_INFORMATION_MBR Mbr;
		DRIVE_LAYOUT_INFORMATION_GPT Gpt;
	} Type;
	PARTITION_INFORMATION_EX PartitionEntry[4];
} DRIVE_LAYOUT_INFORMATION_EX4,*PDRIVE_LAYOUT_INFORMATION_EX4;

/*
 * Create a partition table
 */
// See http://technet.microsoft.com/en-us/library/cc739412.aspx for some background info
#if !defined(PARTITION_BASIC_DATA_GUID)
const GUID PARTITION_BASIC_DATA_GUID = { 0xebd0a0a2, 0xb9e5, 0x4433, {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7} };
#endif
BOOL CreatePartition(HANDLE hDrive, int partition_style, int file_system)
{
	const char* PartitionTypeName[2] = { "MBR", "GPT" };
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};
	DRIVE_LAYOUT_INFORMATION_EX4 DriveLayoutEx = {0};
	BOOL r;
	DWORD size;
	LONGLONG size_in_sectors;

	PrintStatus(0, TRUE, "Partitioning (%s)...",  PartitionTypeName[partition_style]);

	if ((partition_style == PARTITION_STYLE_GPT) || (!IsChecked(IDC_EXTRA_PARTITION))) {
		// Go with the MS 1 MB wastage at the beginning...
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 1024*1024;
	} else {
		// Align on Cylinder
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 
			SelectedDrive.Geometry.BytesPerSector * SelectedDrive.Geometry.SectorsPerTrack;
	}
	size_in_sectors = (SelectedDrive.DiskSize - DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart) / SelectedDrive.Geometry.BytesPerSector;

	switch (partition_style) {
	case PARTITION_STYLE_MBR:
		CreateDisk.PartitionStyle = PARTITION_STYLE_MBR;
		CreateDisk.Mbr.Signature = GetTickCount();

		DriveLayoutEx.PartitionStyle = PARTITION_STYLE_MBR;
		DriveLayoutEx.PartitionCount = 4;	// Must be multiple of 4 for MBR
		DriveLayoutEx.Type.Mbr.Signature = CreateDisk.Mbr.Signature;
		DriveLayoutEx.PartitionEntry[0].PartitionStyle = PARTITION_STYLE_MBR;
		// TODO: CHS fixup (32 sectors/track) through a cheat mode, if requested
		// NB: disk geometry is computed by BIOS & co. by finding a match between LBA and CHS value of first partition
		//     ms-sys's write_partition_number_of_heads() and write_partition_start_sector_number() can be used if needed

		// Align on sector boundary if the extra part option is checked
		if (IsChecked(IDC_EXTRA_PARTITION)) {
			size_in_sectors = ((size_in_sectors / SelectedDrive.Geometry.SectorsPerTrack)-1) * SelectedDrive.Geometry.SectorsPerTrack;
			if (size_in_sectors <= 0)
				return FALSE;
		}
		break;
	case PARTITION_STYLE_GPT:
		CreateDisk.PartitionStyle = PARTITION_STYLE_GPT;
		IGNORE_RETVAL(CoCreateGuid(&CreateDisk.Gpt.DiskId));
		CreateDisk.Gpt.MaxPartitionCount = MAX_GPT_PARTITIONS;

		DriveLayoutEx.PartitionStyle = PARTITION_STYLE_GPT;
		DriveLayoutEx.PartitionCount = 1;
		// At the very least, a GPT disk has atv least 34 reserved (512 bytes) blocks at the beginning
		// and 33 at the end.
		DriveLayoutEx.Type.Gpt.StartingUsableOffset.QuadPart = 34*512;
		DriveLayoutEx.Type.Gpt.UsableLength.QuadPart = SelectedDrive.DiskSize - (34+33)*512;
		DriveLayoutEx.Type.Gpt.MaxPartitionCount = MAX_GPT_PARTITIONS;
		DriveLayoutEx.Type.Gpt.DiskId = CreateDisk.Gpt.DiskId;
		DriveLayoutEx.PartitionEntry[0].PartitionStyle = PARTITION_STYLE_GPT;

		size_in_sectors -= 33;	// Need 33 sectors at the end for secondary GPT
		break;
	default:
		break;
	}

	DriveLayoutEx.PartitionEntry[0].PartitionLength.QuadPart = size_in_sectors * SelectedDrive.Geometry.BytesPerSector;
	DriveLayoutEx.PartitionEntry[0].PartitionNumber = 1;
	DriveLayoutEx.PartitionEntry[0].RewritePartition = TRUE;

	switch (partition_style) {
	case PARTITION_STYLE_MBR:
		DriveLayoutEx.PartitionEntry[0].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack;
		switch (file_system) {
		case FS_FAT16:
			DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType = 0x0e;	// FAT16 LBA
			break;
		case FS_NTFS:
		case FS_EXFAT:
			DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType = 0x07;	// NTFS
			break;
		case FS_FAT32:
			DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType = 0x0c;	// FAT32 LBA
			break;
		default:
			uprintf("Unsupported file system\n");
			return FALSE;
		}
		// Create an extra partition on request - can improve BIOS detection as HDD for older BIOSes
		if (IsChecked(IDC_EXTRA_PARTITION)) {
			DriveLayoutEx.PartitionEntry[1].PartitionStyle = PARTITION_STYLE_MBR;
			// Should end on a sector boundary
			DriveLayoutEx.PartitionEntry[1].StartingOffset.QuadPart = DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart +
				DriveLayoutEx.PartitionEntry[0].PartitionLength.QuadPart;
			DriveLayoutEx.PartitionEntry[1].PartitionLength.QuadPart = SelectedDrive.Geometry.SectorsPerTrack*SelectedDrive.Geometry.BytesPerSector;
			DriveLayoutEx.PartitionEntry[1].PartitionNumber = 2;
			DriveLayoutEx.PartitionEntry[1].RewritePartition = TRUE;
			DriveLayoutEx.PartitionEntry[1].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack*SelectedDrive.Geometry.BytesPerSector;
			DriveLayoutEx.PartitionEntry[1].Mbr.PartitionType = DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType + 0x10;	// Hidden whatever
		}
		// For the remaining partitions, PartitionStyle & PartitionType have already
		// been zeroed => already set to MBR/unused
		break;
	case PARTITION_STYLE_GPT:
		DriveLayoutEx.PartitionEntry[0].Gpt.PartitionType = PARTITION_BASIC_DATA_GUID;
		wcscpy(DriveLayoutEx.PartitionEntry[0].Gpt.Name, L"Microsoft Basic Data");
		IGNORE_RETVAL(CoCreateGuid(&DriveLayoutEx.PartitionEntry[0].Gpt.PartitionId));
		break;
	default:
		break;
	}

	// If you don't call IOCTL_DISK_CREATE_DISK, the next call will fail
	size = sizeof(CreateDisk);
	r = DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK,
			(BYTE*)&CreateDisk, size, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("IOCTL_DISK_CREATE_DISK failed: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	size = sizeof(DriveLayoutEx) - ((partition_style == PARTITION_STYLE_GPT)?(3*sizeof(PARTITION_INFORMATION_EX)):0);
	r = DeviceIoControl(hDrive, IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
			(BYTE*)&DriveLayoutEx, size, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("IOCTL_DISK_SET_DRIVE_LAYOUT_EX failed: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	return TRUE;
}

/*
 * Convert a partition type to its human readable form using
 * (slightly modified) entries from GNU fdisk
 */
const char* GetPartitionType(BYTE Type)
{
	int i;
	for (i=0; i<ARRAYSIZE(msdos_systypes); i++) {
		if (msdos_systypes[i].type == Type)
			return msdos_systypes[i].name;
	}
	return "Unknown";
}
