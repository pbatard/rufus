/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
 * Copyright © 2011-2014 Pete Batard <pete@akeo.ie>
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
#include "drive.h"
#include "resource.h"
#include "sys_types.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "localization.h"

#if !defined(PARTITION_BASIC_DATA_GUID)
const GUID PARTITION_BASIC_DATA_GUID = 
	{ 0xebd0a0a2, 0xb9e5, 0x4433, {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7} };
#endif

/*
 * Globals
 */
RUFUS_DRIVE_INFO SelectedDrive;

/*
 * Working with drive indexes quite risky (left unchecked,inadvertently passing 0 as
 * index would return a handle to C:, which we might then proceed to unknowingly
 * clear the MBR of!), so we mitigate the risk by forcing our indexes to belong to
 * the specific range [DRIVE_INDEX_MIN; DRIVE_INDEX_MAX].
 */
#define CheckDriveIndex(DriveIndex) do { \
	if ((DriveIndex < DRIVE_INDEX_MIN) || (DriveIndex > DRIVE_INDEX_MAX)) { \
		uprintf("WARNING: Bad index value. Please check the code!\n"); \
		goto out; \
	} \
	DriveIndex -= DRIVE_INDEX_MIN; } while (0)

/*
 * Open a drive or volume with optional write and lock access
 * Return INVALID_HANDLE_VALUE (/!\ which is DIFFERENT from NULL /!\) on failure.
 */
static HANDLE GetHandle(char* Path, BOOL bWriteAccess, BOOL bLockDrive)
{
	int i;
	DWORD size;
	HANDLE hDrive = INVALID_HANDLE_VALUE;

	if (Path == NULL)
		goto out;
	hDrive = CreateFileA(Path, GENERIC_READ|(bWriteAccess?GENERIC_WRITE:0),
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hDrive == INVALID_HANDLE_VALUE) {
		uprintf("Could not open drive %s: %s\n", Path, WindowsErrorString());
		goto out;
	}

	if (bWriteAccess) {
		uprintf("Opened drive %s for write access\n", Path);
	}

	if (bLockDrive) {
		if (DeviceIoControl(hDrive, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &size, NULL)) {
			uprintf("I/O boundary checks disabled\n");
		}

		for (i = 0; i < DRIVE_ACCESS_RETRIES; i++) {
			if (DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL))
				goto out;
			if (IS_ERROR(FormatStatus))	// User cancel
				break;
			Sleep(DRIVE_ACCESS_TIMEOUT/DRIVE_ACCESS_RETRIES);
		}
		// If we reached this section, either we didn't manage to get a lock or the user cancelled
		uprintf("Could not get exclusive access to device %s: %s\n", Path, WindowsErrorString());
		safe_closehandle(hDrive);
	}

out:
	return hDrive;
}

/* 
 * Return the path to access the physical drive, or NULL on error.
 * The string is allocated and must be freed (to ensure concurrent access)
 */
char* GetPhysicalName(DWORD DriveIndex)
{
	BOOL success = FALSE;
	char physical_name[24];

	CheckDriveIndex(DriveIndex);
	safe_sprintf(physical_name, sizeof(physical_name), "\\\\.\\PHYSICALDRIVE%d", DriveIndex);
	success = TRUE;
out:
	return (success)?safe_strdup(physical_name):NULL;
}

/* 
 * Return a handle to the physical drive identified by DriveIndex
 */
HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bWriteAccess, BOOL bLockDrive)
{
	HANDLE hPhysical = INVALID_HANDLE_VALUE;
	char* PhysicalPath = GetPhysicalName(DriveIndex);
	hPhysical = GetHandle(PhysicalPath, bWriteAccess, bLockDrive);
	safe_free(PhysicalPath);
	return hPhysical;
}

/*
 * Return the first GUID volume name for the associated drive or NULL if not found
 * See http://msdn.microsoft.com/en-us/library/cc542456.aspx
 * The returned string is allocated and must be freed
 * TODO: a drive may have multiple volumes - should we handle those?
 */
char* GetLogicalName(DWORD DriveIndex, BOOL bKeepTrailingBackslash, BOOL bSilent)
{
	BOOL success = FALSE;
	char volume_name[MAX_PATH];
	HANDLE hDrive = INVALID_HANDLE_VALUE, hVolume = INVALID_HANDLE_VALUE;
	size_t len;
	char path[MAX_PATH];
	VOLUME_DISK_EXTENTS DiskExtents;
	DWORD size;
	UINT drive_type;
	int i, j;
	static const char* ignore_device[] = { "\\Device\\CdRom", "\\Device\\Floppy" };
	static const char* volume_start = "\\\\?\\";

	CheckDriveIndex(DriveIndex);

	for (i=0; hDrive == INVALID_HANDLE_VALUE; i++) {
		if (i == 0) {
			hVolume = FindFirstVolumeA(volume_name, sizeof(volume_name));
			if (hVolume == INVALID_HANDLE_VALUE) {
				suprintf("Could not access first GUID volume: %s\n", WindowsErrorString());
				goto out;
			}
		} else {
			if (!FindNextVolumeA(hVolume, volume_name, sizeof(volume_name))) {
				if (GetLastError() != ERROR_NO_MORE_FILES) {
					suprintf("Could not access next GUID volume: %s\n", WindowsErrorString());
				}
				goto out;
			}
		}

		// Sanity checks
		len = safe_strlen(volume_name);
		if ((len <= 1) || (safe_strnicmp(volume_name, volume_start, 4) != 0) || (volume_name[len-1] != '\\')) {
			suprintf("'%s' is not a GUID volume name\n", volume_name);
			continue;
		}

		drive_type = GetDriveTypeA(volume_name);
		if ((drive_type != DRIVE_REMOVABLE) && (drive_type != DRIVE_FIXED))
			continue;

		volume_name[len-1] = 0;

		if (QueryDosDeviceA(&volume_name[4], path, sizeof(path)) == 0) {
			suprintf("Failed to get device path for GUID volume '%s': %s\n", volume_name, WindowsErrorString());
			continue;
		}

		for (j=0; (j<ARRAYSIZE(ignore_device)) &&
			(_strnicmp(path, ignore_device[j], safe_strlen(ignore_device[j])) != 0); j++);
		if (j < ARRAYSIZE(ignore_device)) {
			suprintf("Skipping GUID volume for '%s'\n", path);
			continue;
		}

		// If we can't have FILE_SHARE_WRITE, forget it
		hDrive = CreateFileA(volume_name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDrive == INVALID_HANDLE_VALUE) {
			suprintf("Could not open GUID volume '%s': %s\n", volume_name, WindowsErrorString());
			continue;
		}

		if ((!DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
			&DiskExtents, sizeof(DiskExtents), &size, NULL)) || (size <= 0)) {
			suprintf("Could not get Disk Extents: %s\n", WindowsErrorString());
			safe_closehandle(hDrive);
			continue;
		}
		safe_closehandle(hDrive);
		if ((DiskExtents.NumberOfDiskExtents >= 1) && (DiskExtents.Extents[0].DiskNumber == DriveIndex)) {
			if (bKeepTrailingBackslash)
				volume_name[len-1] = '\\';
			success = TRUE;
			break;
		}
	}

out:
	if (hVolume != INVALID_HANDLE_VALUE)
		FindVolumeClose(hVolume);
	return (success)?safe_strdup(volume_name):NULL;
}

/* Wait for a logical drive to reappear - Used when a drive has just been repartitioned */
BOOL WaitForLogical(DWORD DriveIndex)
{
	DWORD i;
	char* LogicalPath = NULL;

	for (i = 0; i < DRIVE_ACCESS_RETRIES; i++) {
		LogicalPath = GetLogicalName(DriveIndex, FALSE, TRUE);
		if (LogicalPath != NULL) {
			free(LogicalPath);
			return TRUE;
		}
		if (IS_ERROR(FormatStatus))	// User cancel
			return FALSE;
		Sleep(DRIVE_ACCESS_TIMEOUT/DRIVE_ACCESS_RETRIES);
	}
	uprintf("Timeout while waiting for logical drive\n");
	return FALSE;
}

/* 
 * Obtain a handle to the first logical volume on the disk identified by DriveIndex
 * Returns INVALID_HANDLE_VALUE on error or NULL if no logical path exists (typical
 * of unpartitioned drives)
 */
HANDLE GetLogicalHandle(DWORD DriveIndex, BOOL bWriteAccess, BOOL bLockDrive)
{
	HANDLE hLogical = INVALID_HANDLE_VALUE;
	char* LogicalPath = GetLogicalName(DriveIndex, FALSE, FALSE);

	if (LogicalPath == NULL) {
		uprintf("No logical drive found (unpartitioned?)\n");
		return NULL;
	}

	hLogical = GetHandle(LogicalPath, bWriteAccess, bLockDrive);
	free(LogicalPath);
	return hLogical;
}

/*
 * Who would have thought that Microsoft would make it so unbelievably hard to
 * get the frickin' device number for a drive? You have to use TWO different
 * methods to have a chance to get it!
 */
int GetDriveNumber(HANDLE hDrive, char* path)
{
	STORAGE_DEVICE_NUMBER_REDEF DeviceNumber;
	VOLUME_DISK_EXTENTS_REDEF DiskExtents;
	DWORD size;
	int r = -1;

	if (!DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
		&DiskExtents, sizeof(DiskExtents), &size, NULL) || (size <= 0) || (DiskExtents.NumberOfDiskExtents < 1) ) {
		// DiskExtents are NO_GO (which is the case for external USB HDDs...)
		if(!DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
			&DeviceNumber, sizeof(DeviceNumber), &size, NULL ) || (size <= 0)) {
			uprintf("Could not get device number for device %s: %s", path, WindowsErrorString());
			return -1;
		}
		r = (int)DeviceNumber.DeviceNumber;
	} else if (DiskExtents.NumberOfDiskExtents >= 2) {
		uprintf("Ignoring drive '%s' as it spans multiple disks (RAID?)", path);
		return -1;
	} else {
		r = (int)DiskExtents.Extents[0].DiskNumber;
	}
	if (r >= MAX_DRIVES) {
		uprintf("Device Number for device %s is too big (%d) - ignoring device", path, r);
		return -1;
	}
	return r;
}

/*
 * Returns the drive letters for all volumes located on the drive identified by DriveIndex,
 * as well as the drive type. This is used as base for the 2 function calls that follow.
 */
static BOOL _GetDriveLettersAndType(DWORD DriveIndex, char* drive_letters, UINT* drive_type)
{
	DWORD size;
	BOOL r = FALSE;
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	UINT _drive_type;
	int i = 0, drive_number;
	char *drive, drives[26*4];	/* "D:\", "E:\", etc. */
	char logical_drive[] = "\\\\.\\#:";

	if (drive_letters != NULL)
		drive_letters[0] = 0;
	if (drive_type != NULL)
		*drive_type = DRIVE_UNKNOWN;
	CheckDriveIndex(DriveIndex);

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString());
		goto out;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: buffer too small (required %d vs %d)\n", size, sizeof(drives));
		goto out;
	}

	r = TRUE;	// Required to detect drives that don't have volumes assigned
	for (drive = drives ;*drive; drive += safe_strlen(drive)+1) {
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
		_drive_type = GetDriveTypeA(drive);

		if ((_drive_type != DRIVE_REMOVABLE) && (_drive_type != DRIVE_FIXED))
			continue;

		safe_sprintf(logical_drive, sizeof(logical_drive), "\\\\.\\%c:", drive[0]);
		hDrive = CreateFileA(logical_drive, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
		if (hDrive == INVALID_HANDLE_VALUE) {
			uprintf("Warning: could not open drive %c: %s\n", drive[0], WindowsErrorString());
			continue;
		}

		drive_number = GetDriveNumber(hDrive, logical_drive);
		safe_closehandle(hDrive);
		if (drive_number == DriveIndex) {
			r = TRUE;
			if (drive_letters != NULL)
				drive_letters[i++] = *drive;
			// The drive type should be the same for all volumes, so we can overwrite
			if (drive_type != NULL)
				*drive_type = _drive_type;
		}
	}

out:
	if (drive_letters != NULL)
		drive_letters[i] = 0;
	return r;
}

// Could have used a #define, but this is clearer
BOOL GetDriveLetters(DWORD DriveIndex, char* drive_letters)
{
	return _GetDriveLettersAndType(DriveIndex, drive_letters, NULL);
}

// There's already a GetDriveType in the Windows API
UINT GetDriveTypeFromIndex(DWORD DriveIndex)
{
	UINT drive_type;
	_GetDriveLettersAndType(DriveIndex, NULL, &drive_type);
	return drive_type;
}

/*
 * Return the next unused drive letter from the system
 */
char GetUnusedDriveLetter(void)
{
	DWORD size;
	char drive_letter = 'Z'+1, *drive, drives[26*4];	/* "D:\", "E:\", etc. */

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString());
		goto out;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: buffer too small (required %d vs %d)\n", size, sizeof(drives));
		goto out;
	}

	for (drive_letter = 'C'; drive_letter < 'Z'; drive_letter++) {
		for (drive = drives ;*drive; drive += safe_strlen(drive)+1) {
			if (!isalpha(*drive))
				continue;
			if (drive_letter == (char)toupper((int)*drive))
				break;
		}
		if (!*drive)
			break;
	}

out:
	return (drive_letter>'Z')?0:drive_letter;
}

/*
 * Return the drive letter and volume label
 * If the drive doesn't have a volume assigned, space is returned for the letter
 */
BOOL GetDriveLabel(DWORD DriveIndex, char* letters, char** label)
{
	HANDLE hPhysical;
	DWORD size;
	char AutorunPath[] = "#:\\autorun.inf", *AutorunLabel = NULL;
	wchar_t wDrivePath[] = L"#:\\";
	wchar_t wVolumeLabel[MAX_PATH+1];
	static char VolumeLabel[MAX_PATH+1];

	*label = STR_NO_LABEL;

	if (!GetDriveLetters(DriveIndex, letters))
		return FALSE;
	if (letters[0] == 0) {
		// Drive without volume assigned - always enabled
		return TRUE;
	}
	// We only care about an autorun.inf if we have a single volume
	AutorunPath[0] = letters[0];
	wDrivePath[0] = letters[0];

	// Try to read an extended label from autorun first. Fallback to regular label if not found.
	// In the case of card readers with no card, users can get an annoying popup asking them
	// to insert media. Use IOCTL_STORAGE_CHECK_VERIFY to prevent this
	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE);
	if (DeviceIoControl(hPhysical, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &size, NULL))
		AutorunLabel = get_token_data_file("label", AutorunPath);
	else if (GetLastError() == ERROR_NOT_READY)
		uprintf("Ignoring autorun.inf label for drive %c: %s\n", letters[0],
		(HRESULT_CODE(GetLastError()) == ERROR_NOT_READY)?"No media":WindowsErrorString());
	safe_closehandle(hPhysical);
	if (AutorunLabel != NULL) {
		uprintf("Using autorun.inf label for drive %c: '%s'\n", letters[0], AutorunLabel);
		safe_strcpy(VolumeLabel, sizeof(VolumeLabel), AutorunLabel);
		safe_free(AutorunLabel);
		*label = VolumeLabel;
	} else if (GetVolumeInformationW(wDrivePath, wVolumeLabel, ARRAYSIZE(wVolumeLabel),
		NULL, NULL, NULL, NULL, 0) && *wVolumeLabel) {
		wchar_to_utf8_no_alloc(wVolumeLabel, VolumeLabel, sizeof(VolumeLabel));
		*label = VolumeLabel;
	}

	return TRUE;
}

/*
 * Return the drive size
 */
uint64_t GetDriveSize(DWORD DriveIndex)
{
	BOOL r;
	HANDLE hPhysical;
	DWORD size;
	BYTE geometry[256];
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)(void*)geometry;

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
		NULL, 0, geometry, sizeof(geometry), &size, NULL);
	safe_closehandle(hPhysical);
	if (!r || size <= 0)
		return 0;
	return DiskGeometry->DiskSize.QuadPart;
}

/*
 * GET_DRIVE_GEOMETRY is used to tell if there is an actual media
 */
BOOL IsMediaPresent(DWORD DriveIndex)
{
	BOOL r;
	HANDLE hPhysical;
	DWORD size;
	BYTE geometry[128];

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE);
	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, geometry, sizeof(geometry), &size, NULL) || (size <= 0);
	safe_closehandle(hPhysical);
	return r;
}

const struct {int (*fn)(FILE *fp); char* str;} known_mbr[] = {
	{ is_dos_mbr, "DOS/NT/95A" },
	{ is_dos_f2_mbr, "DOS/NT/95A (F2)" },
	{ is_95b_mbr, "Windows 95B/98/98SE/ME" },
	{ is_2000_mbr, "Windows 2000/XP/2003" },
	{ is_vista_mbr, "Windows Vista" },
	{ is_win7_mbr, "Windows 7" },
	{ is_rufus_mbr, "Rufus" },
	{ is_syslinux_mbr, "Syslinux" },
	{ is_reactos_mbr, "ReactOS" },
	{ is_kolibri_mbr, "KolibriOS" },
	{ is_zero_mbr, "Zeroed" },
};

// Returns TRUE if the drive seems bootable, FALSE otherwise
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName)
{
	const char* mbr_name = "Master Boot Record";
	FILE fake_fd = { 0 };
	int i;

	fake_fd._ptr = (char*)hPhysicalDrive;
	fake_fd._bufsiz = 512;

	if (!is_br(&fake_fd)) {
		uprintf("%s does not have an x86 %s\n", TargetName, mbr_name);
		return FALSE;
	}
	for (i=0; i<ARRAYSIZE(known_mbr); i++) {
		if (known_mbr[i].fn(&fake_fd)) {
			uprintf("%s has a %s %s\n", TargetName, known_mbr[i].str, mbr_name);
			return TRUE;
		}
	}

	uprintf("%s has an unknown %s\n", TargetName, mbr_name);
	return TRUE;
}

const struct {int (*fn)(FILE *fp); char* str;} known_pbr[] = {
	{ entire_fat_16_br_matches, "FAT16 DOS" },
	{ entire_fat_16_fd_br_matches, "FAT16 FreeDOS" },
	{ entire_fat_16_ros_br_matches, "FAT16 ReactOS" },
	{ entire_fat_32_br_matches, "FAT32 DOS" },
	{ entire_fat_32_nt_br_matches, "FAT32 NT" },
	{ entire_fat_32_fd_br_matches, "FAT32 FreeDOS" },
	{ entire_fat_32_ros_br_matches, "FAT32 ReactOS" },
	{ entire_fat_32_kos_br_matches, "FAT32 KolibriOS" },
};

BOOL AnalyzePBR(HANDLE hLogicalVolume)
{
	const char* pbr_name = "Partition Boot Record";
	FILE fake_fd = { 0 };
	int i;

	fake_fd._ptr = (char*)hLogicalVolume;
	fake_fd._bufsiz = 512;

	if (!is_br(&fake_fd)) {
		uprintf("Volume does not have an x86 %s\n", pbr_name);
		return FALSE;
	}

	if (is_fat_16_br(&fake_fd) || is_fat_32_br(&fake_fd)) {
		for (i=0; i<ARRAYSIZE(known_pbr); i++) {
			if (known_pbr[i].fn(&fake_fd)) {
				uprintf("Drive has a %s %s\n", known_pbr[i].str, pbr_name);
				return TRUE;
			}
		}
		uprintf("Volume has an unknown FAT16 or FAT32 %s\n", pbr_name);
	} else {
		uprintf("Volume has an unknown %s\n", pbr_name);
	}
	return TRUE;
}

/*
 * Fill the drive properties (size, FS, etc)
 * Returns TRUE if the drive has a partition that can be mounted in Windows, FALSE otherwise
 */
BOOL GetDrivePartitionData(DWORD DriveIndex, char* FileSystemName, DWORD FileSystemNameSize, BOOL bSilent)
{
	// MBR partition types that can be mounted in Windows
	const uint8_t mbr_mountable[] = { 0x01, 0x04, 0x06, 0x07, 0x0b, 0x0c, 0x0e, 0xef };
	BOOL r, hasRufusExtra = FALSE, ret = FALSE;
	HANDLE hPhysical;
	DWORD size;
	BYTE geometry[256], layout[4096], part_type;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)(void*)geometry;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)(void*)layout;
	char* volume_name;
	char tmp[256];
	DWORD i, j;

	SelectedDrive.nPartitions = 0;
	// Populate the filesystem data
	FileSystemName[0] = 0;
	volume_name = GetLogicalName(DriveIndex, TRUE, FALSE);
	if ((volume_name == NULL) || (!GetVolumeInformationA(volume_name, NULL, 0, NULL, NULL, NULL, FileSystemName, FileSystemNameSize))) {
		suprintf("No volume information for drive 0x%02x\n", DriveIndex);
	}
	safe_free(volume_name);

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return 0;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, geometry, sizeof(geometry), &size, NULL);
	if (!r || size <= 0) {
		suprintf("Could not get geometry for drive 0x%02x: %s\n", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return 0;
	}
	if (DiskGeometry->Geometry.BytesPerSector < 512) {
		suprintf("WARNING: Drive 0x%02x reports a sector size of %d - Correcting to 512 bytes.\n",
			DriveIndex, DiskGeometry->Geometry.BytesPerSector);
		DiskGeometry->Geometry.BytesPerSector = 512;
	}
	SelectedDrive.DiskSize = DiskGeometry->DiskSize.QuadPart;
	memcpy(&SelectedDrive.Geometry, &DiskGeometry->Geometry, sizeof(DISK_GEOMETRY));
	suprintf("Disk type: %s, Sector Size: %d bytes\n", (DiskGeometry->Geometry.MediaType == FixedMedia)?"Fixed":"Removable",
		DiskGeometry->Geometry.BytesPerSector);
	suprintf("Cylinders: %lld, TracksPerCylinder: %d, SectorsPerTrack: %d\n",
		DiskGeometry->Geometry.Cylinders, DiskGeometry->Geometry.TracksPerCylinder, DiskGeometry->Geometry.SectorsPerTrack);

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 
			NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		suprintf("Could not get layout for drive 0x%02x: %s\n", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return 0;
	}

	switch (DriveLayout->PartitionStyle) {
	case PARTITION_STYLE_MBR:
		SelectedDrive.PartitionType = PARTITION_STYLE_MBR;
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				SelectedDrive.nPartitions++;
			}
		}
		suprintf("Partition type: MBR, NB Partitions: %d\n", SelectedDrive.nPartitions);
		SelectedDrive.has_mbr_uefi_marker = (DriveLayout->Mbr.Signature == MBR_UEFI_MARKER);
		suprintf("Disk ID: 0x%08X %s\n", DriveLayout->Mbr.Signature, SelectedDrive.has_mbr_uefi_marker?"(UEFI target)":"");
		AnalyzeMBR(hPhysical, "Drive");
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				suprintf("Partition %d:\n", i+1);
				part_type = DriveLayout->PartitionEntry[i].Mbr.PartitionType;
				for (j=0; j<ARRAYSIZE(mbr_mountable); j++) {
					if (part_type == mbr_mountable[j]) {
						ret = TRUE;
						break;
					}
				}
				suprintf("  Type: %s (0x%02x)\r\n  Size: %s (%lld bytes)\r\n  Start Sector: %d, Boot: %s, Recognized: %s\n",
					((part_type==0x07)&&(FileSystemName[0]!=0))?FileSystemName:GetPartitionType(part_type), part_type,
					SizeToHumanReadable(DriveLayout->PartitionEntry[i].PartitionLength.QuadPart, TRUE, FALSE),
					DriveLayout->PartitionEntry[i].PartitionLength, DriveLayout->PartitionEntry[i].Mbr.HiddenSectors,
					DriveLayout->PartitionEntry[i].Mbr.BootIndicator?"Yes":"No",
					DriveLayout->PartitionEntry[i].Mbr.RecognizedPartition?"Yes":"No");
				if (part_type == RUFUS_EXTRA_PARTITION_TYPE)	// This is a partition Rufus created => we can safely ignore it
					hasRufusExtra = TRUE;
				if (part_type == 0xee)	// Flag a protective MBR for non GPT platforms (XP)
					SelectedDrive.has_protective_mbr = TRUE;
			}
		}
		break;
	case PARTITION_STYLE_GPT:
		SelectedDrive.PartitionType = PARTITION_STYLE_GPT;
		suprintf("Partition type: GPT, NB Partitions: %d\n", DriveLayout->PartitionCount);
		suprintf("Disk GUID: %s\n", GuidToString(&DriveLayout->Gpt.DiskId));
		suprintf("Max parts: %d, Start Offset: %lld, Usable = %lld bytes\n",
			DriveLayout->Gpt.MaxPartitionCount, DriveLayout->Gpt.StartingUsableOffset.QuadPart, DriveLayout->Gpt.UsableLength.QuadPart);
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			SelectedDrive.nPartitions++;
			tmp[0] = 0;
			wchar_to_utf8_no_alloc(DriveLayout->PartitionEntry[i].Gpt.Name, tmp, sizeof(tmp));
			suprintf("Partition %d:\r\n  Type: %s\r\n  Name: '%s'\n", DriveLayout->PartitionEntry[i].PartitionNumber,
				GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionType), tmp);
			suprintf("  ID: %s\r\n  Size: %s (%lld bytes)\r\n  Start Sector: %lld, Attributes: 0x%016llX\n",
				GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionId), SizeToHumanReadable(DriveLayout->PartitionEntry[i].PartitionLength.QuadPart, TRUE, FALSE),
				DriveLayout->PartitionEntry[i].PartitionLength, DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / DiskGeometry->Geometry.BytesPerSector,
				DriveLayout->PartitionEntry[i].Gpt.Attributes);
			if ( (memcmp(&PARTITION_BASIC_DATA_GUID, &DriveLayout->PartitionEntry[i].Gpt.PartitionType, sizeof(GUID)) == 0) &&
				 (nWindowsVersion >= WINDOWS_VISTA) )
				ret = TRUE;
		}
		break;
	default:
		SelectedDrive.PartitionType = PARTITION_STYLE_MBR;
		suprintf("Partition type: RAW\n");
		break;
	}
	safe_closehandle(hPhysical);

	if (hasRufusExtra)
		SelectedDrive.nPartitions--;
	return ret;
}

/*
 * Flush file data
 */
static BOOL FlushDrive(char drive_letter)
{
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	BOOL r = FALSE;
	char logical_drive[] = "\\\\.\\#:";

	logical_drive[4] = drive_letter;
	hDrive = CreateFileA(logical_drive, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	if (hDrive == INVALID_HANDLE_VALUE) {
		uprintf("Failed to open %c: for flushing: %s\n", drive_letter, WindowsErrorString());
		goto out;
	}
	r = FlushFileBuffers(hDrive);
	if (r == FALSE)
		uprintf("Failed to flush %c: %s\n", drive_letter, WindowsErrorString());

out:
	safe_closehandle(hDrive);
	return r;
}

/*
 * Unmount of volume using the DISMOUNT_VOLUME ioctl
 */
BOOL UnmountVolume(HANDLE hDrive)
{
	DWORD size;

	if (!DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &size, NULL)) {
		uprintf("Could not unmount drive: %s\n", WindowsErrorString());
		return FALSE;
	}
	return TRUE;
}

/*
 * Mount the volume identified by drive_guid to mountpoint drive_name
 */
BOOL MountVolume(char* drive_name, char *drive_guid)
{
	char mounted_guid[52];	// You need at least 51 characters on XP
	char mounted_letter[16] = {0};
	DWORD size;

	// For fixed disks, Windows may already have remounted the volume, but with a different letter
	// than the one we want. If that's the case, we need to unmount first.
	if ( (GetVolumePathNamesForVolumeNameA(drive_guid, mounted_letter, sizeof(mounted_letter), &size))
	  && (size > 1) && (mounted_letter[0] != drive_name[0]) ) {
		uprintf("Volume is already mounted, but as %c: instead of %c: - Unmounting...\n", mounted_letter[0], drive_name[0]);
		if (!DeleteVolumeMountPointA(mounted_letter))
			uprintf("Failed to unmount volume: %s", WindowsErrorString());
		Sleep(200);
	}

	if (!SetVolumeMountPointA(drive_name, drive_guid)) {
		// If the OS was faster than us at remounting the drive, this operation can fail
		// with ERROR_DIR_NOT_EMPTY. If that's the case, just check that mountpoints match
		if (GetLastError() == ERROR_DIR_NOT_EMPTY) {
			if (!GetVolumeNameForVolumeMountPointA(drive_name, mounted_guid, sizeof(mounted_guid))) {
				uprintf("%s already mounted, but volume GUID could not be checked: %s\n",
					drive_name, WindowsErrorString());
				return FALSE;
			}
			if (safe_strcmp(drive_guid, mounted_guid) != 0) {
				uprintf("%s already mounted, but volume GUID doesn't match:\r\n  expected %s, got %s\n",
					drive_name, drive_guid, mounted_guid);
				return FALSE;
			}
			uprintf("%s was already mounted as %s\n", drive_guid, drive_name);
		} else {
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Issue a complete remount of the volume
 */
BOOL RemountVolume(char* drive_name)
{
	char drive_guid[51];

	// UDF requires a sync/flush, and it's also a good idea for other FS's
	FlushDrive(drive_name[0]);
	if (GetVolumeNameForVolumeMountPointA(drive_name, drive_guid, sizeof(drive_guid))) {
		if (DeleteVolumeMountPointA(drive_name)) {
			Sleep(200);
			if (MountVolume(drive_name, drive_guid)) {
				uprintf("Successfully remounted %s on %s\n", &drive_guid[4], drive_name);
			} else {
				uprintf("Failed to remount %s on %s\n", &drive_guid[4], drive_name);
				// This will leave the drive inaccessible and must be flagged as an error
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
 * See http://technet.microsoft.com/en-us/library/cc739412.aspx for some background info
 * NB: if you modify the MBR outside of using the Windows API, Windows still uses the cached
 * copy it got from the last IOCTL, and ignores your changes until you replug the drive
 * or issue an IOCTL_DISK_UPDATE_PROPERTIES.
 */
BOOL CreatePartition(HANDLE hDrive, int partition_style, int file_system, BOOL mbr_uefi_marker)
{
	const char* PartitionTypeName[2] = { "MBR", "GPT" };
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};
	DRIVE_LAYOUT_INFORMATION_EX4 DriveLayoutEx = {0};
	BOOL r;
	DWORD size;
	LONGLONG size_in_sectors;

	PrintStatus(0, TRUE, MSG_238, PartitionTypeName[partition_style]);

	if ((partition_style == PARTITION_STYLE_GPT) || (!IsChecked(IDC_EXTRA_PARTITION))) {
		// Go with the MS 1 MB wastage at the beginning...
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 1024*1024;
	} else {
		// Align on Cylinder
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 
			SelectedDrive.Geometry.BytesPerSector * SelectedDrive.Geometry.SectorsPerTrack;
	}
	// TODO: should we try to align the following to the cluster size as well?
	size_in_sectors = (SelectedDrive.DiskSize - DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart) / SelectedDrive.Geometry.BytesPerSector;

	switch (partition_style) {
	case PARTITION_STYLE_MBR:
		CreateDisk.PartitionStyle = PARTITION_STYLE_MBR;
		// If MBR+UEFI is selected, write an UEFI marker in lieu of the regular MBR signature.
		// This helps us reselect the partition scheme option that was used when creating the
		// drive in Rufus. As far as I can tell, Windows doesn't care much if this signature
		// isn't unique for USB drives.
		CreateDisk.Mbr.Signature = mbr_uefi_marker?MBR_UEFI_MARKER:GetTickCount();

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
		// TODO: (?) As per MSDN: "When specifying a GUID partition table (GPT) as the PARTITION_STYLE of the CREATE_DISK
		// structure, an application should wait for the MSR partition arrival before sending the IOCTL_DISK_SET_DRIVE_LAYOUT_EX
		// control code. For more information about device notification, see RegisterDeviceNotification."

		CreateDisk.PartitionStyle = PARTITION_STYLE_GPT;
		IGNORE_RETVAL(CoCreateGuid(&CreateDisk.Gpt.DiskId));
		CreateDisk.Gpt.MaxPartitionCount = MAX_GPT_PARTITIONS;

		DriveLayoutEx.PartitionStyle = PARTITION_STYLE_GPT;
		DriveLayoutEx.PartitionCount = 1;
		// At the very least, a GPT disk has 34 reserved blocks (512 bytes) at the beginning and 33 at the end.
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
		DriveLayoutEx.PartitionEntry[0].Mbr.BootIndicator = IsChecked(IDC_BOOT);
		DriveLayoutEx.PartitionEntry[0].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack;
		switch (file_system) {
		case FS_FAT16:
			DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType = 0x0e;	// FAT16 LBA
			break;
		case FS_NTFS:
		case FS_EXFAT:
		case FS_UDF:
		case FS_REFS:
			DriveLayoutEx.PartitionEntry[0].Mbr.PartitionType = 0x07;
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
			DriveLayoutEx.PartitionEntry[1].Mbr.BootIndicator = FALSE;
			DriveLayoutEx.PartitionEntry[1].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack*SelectedDrive.Geometry.BytesPerSector;
			DriveLayoutEx.PartitionEntry[1].Mbr.PartitionType = RUFUS_EXTRA_PARTITION_TYPE;
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
		uprintf("Could not reset disk: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	size = sizeof(DriveLayoutEx) - ((partition_style == PARTITION_STYLE_GPT)?(3*sizeof(PARTITION_INFORMATION_EX)):0);
	r = DeviceIoControl(hDrive, IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
			(BYTE*)&DriveLayoutEx, size, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("Could not set drive layout: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	if (!RefreshDriveLayout(hDrive)) {
		safe_closehandle(hDrive);
		return FALSE;
	}

	return TRUE;
}

BOOL RefreshDriveLayout(HANDLE hDrive)
{
	BOOL r;
	DWORD size;

	// Diskpart does call the following IOCTL this after updating the partition table, so we do too
	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL );
	if (!r)
		uprintf("Could not refresh drive layout: %s\n", WindowsErrorString());
	return r;
}

/* Delete the disk partition table */
BOOL DeletePartitions(HANDLE hDrive)
{
	BOOL r;
	DWORD size;
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};

	PrintStatus(0, TRUE, MSG_239);

	size = sizeof(CreateDisk);
	r = DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK,
			(BYTE*)&CreateDisk, size, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("Could not delete drive layout: %s\n", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("Could not refresh drive layout: %s\n", WindowsErrorString());
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
