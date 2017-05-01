/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
 * Copyright © 2011-2016 Pete Batard <pete@akeo.ie>
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

#include <windows.h>
#include <stdint.h>
#include <winioctl.h>				// for DISK_GEOMETRY

#pragma once

#define RUFUS_EXTRA_PARTITION_TYPE          0xea
#define MOUNTMGRCONTROLTYPE                 ((ULONG)'m')
#define MOUNTMGR_DOS_DEVICE_NAME            "\\\\.\\MountPointManager"
#define IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT     \
	CTL_CODE(MOUNTMGRCONTROLTYPE, 15, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUNTMGR_SET_AUTO_MOUNT       \
	CTL_CODE(MOUNTMGRCONTROLTYPE, 16, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define XP_MSR       0x01
#define XP_EFI       0x02
#define XP_UEFI_NTFS 0x04
#define XP_COMPAT    0x08

/* We need a redef of these MS structure */
typedef struct {
	DWORD DeviceType;
	ULONG DeviceNumber;
	ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_REDEF;

typedef struct {
	DWORD NumberOfDiskExtents;
	// The one from MS uses ANYSIZE_ARRAY, which can lead to all kind of problems
	DISK_EXTENT Extents[8];
} VOLUME_DISK_EXTENTS_REDEF;

static __inline BOOL UnlockDrive(HANDLE hDrive) {
	DWORD size;
	return DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL);
}
#define safe_unlockclose(h) do {if ((h != INVALID_HANDLE_VALUE) && (h != NULL)) {UnlockDrive(h); CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)

/* Current drive info */
typedef struct {
	LONGLONG DiskSize;
	DWORD DeviceNumber;
	DWORD SectorsPerTrack;
	DWORD SectorSize;
	DWORD FirstDataSector;
	MEDIA_TYPE MediaType;
	int PartitionType;
	int nPartitions;	// number of partitions we actually care about
	int FSType;
	char proposed_label[16];
	BOOL has_protective_mbr;
	BOOL has_mbr_uefi_marker;
	struct {
		ULONG Allowed;
		ULONG Default;
	} ClusterSize[FS_MAX];
} RUFUS_DRIVE_INFO;
extern RUFUS_DRIVE_INFO SelectedDrive;

BOOL SetAutoMount(BOOL enable);
BOOL GetAutoMount(BOOL* enabled);
char* GetPhysicalName(DWORD DriveIndex);
HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare);
char* GetLogicalName(DWORD DriveIndex, BOOL bKeepTrailingBackslash, BOOL bSilent);
BOOL WaitForLogical(DWORD DriveIndex);
HANDLE GetLogicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare);
int GetDriveNumber(HANDLE hDrive, char* path);
BOOL GetDriveLetters(DWORD DriveIndex, char* drive_letters);
UINT GetDriveTypeFromIndex(DWORD DriveIndex);
char GetUnusedDriveLetter(void);
BOOL GetDriveLabel(DWORD DriveIndex, char* letter, char** label);
uint64_t GetDriveSize(DWORD DriveIndex);
BOOL IsMediaPresent(DWORD DriveIndex);
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName);
BOOL AnalyzePBR(HANDLE hLogicalVolume);
BOOL GetDrivePartitionData(DWORD DriveIndex, char* FileSystemName, DWORD FileSystemNameSize, BOOL bSilent);
BOOL UnmountVolume(HANDLE hDrive);
BOOL MountVolume(char* drive_name, char *drive_guid);
BOOL AltUnmountVolume(const char* drive_name);
char* AltMountVolume(const char* drive_name, uint8_t part_nr);
BOOL RemountVolume(char* drive_name);
BOOL CreatePartition(HANDLE hDrive, int partition_style, int file_system, BOOL mbr_uefi_marker, uint8_t extra_partitions);
BOOL InitializeDisk(HANDLE hDrive);
BOOL RefreshDriveLayout(HANDLE hDrive);
const char* GetPartitionType(BYTE Type);
