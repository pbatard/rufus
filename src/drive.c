/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
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
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#if !defined(__MINGW32__)
#include <initguid.h>
#endif
#include <vds.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "file.h"
#include "drive.h"
#include "sys_types.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"

#define GLOBALROOT_NAME "\\\\?\\GLOBALROOT"
const char* sfd_name = "Super Floppy Disk";
const char* groot_name = GLOBALROOT_NAME;
const size_t groot_len = sizeof(GLOBALROOT_NAME) - 1;

#if !defined(PARTITION_BASIC_DATA_GUID)
const GUID PARTITION_BASIC_DATA_GUID =
	{ 0xebd0a0a2L, 0xb9e5, 0x4433, {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7} };
#endif
#if !defined(PARTITION_MSFT_RESERVED_GUID)
const GUID PARTITION_MSFT_RESERVED_GUID =
	{ 0xe3c9e316L, 0x0b5c, 0x4db8, {0x81, 0x7d, 0xf9, 0x2d, 0xf0, 0x02, 0x15, 0xae} };
#endif
#if !defined(PARTITION_SYSTEM_GUID)
const GUID PARTITION_SYSTEM_GUID =
	{ 0xc12a7328L, 0xf81f, 0x11d2, {0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b} };
#endif
#if !defined(PARTITION_LINUX_HOME_GUID)
const GUID PARTITION_LINUX_HOME_GUID =
	{ 0x933ac7e1l, 0x2eb4, 0x4f13, {0xb8, 0x44, 0x0e, 0x14, 0xe2, 0xae, 0xf9, 0x15 } };
#endif

#if defined(__MINGW32__)
const IID CLSID_VdsLoader = { 0x9c38ed61, 0xd565, 0x4728, { 0xae, 0xee, 0xc8, 0x09, 0x52, 0xf0, 0xec, 0xde } };
const IID IID_IVdsServiceLoader = { 0xe0393303, 0x90d4, 0x4a97, { 0xab, 0x71, 0xe9, 0xb6, 0x71, 0xee, 0x27, 0x29 } };
const IID IID_IVdsProvider = { 0x10c5e575, 0x7984, 0x4e81, { 0xa5, 0x6b, 0x43, 0x1f, 0x5f, 0x92, 0xae, 0x42 } };
const IID IID_IVdsSwProvider = { 0x9aa58360, 0xce33, 0x4f92, { 0xb6, 0x58, 0xed, 0x24, 0xb1, 0x44, 0x25, 0xb8 } };
const IID IID_IVdsPack = { 0x3b69d7f5, 0x9d94, 0x4648, { 0x91, 0xca, 0x79, 0x93, 0x9b, 0xa2, 0x63, 0xbf } };
const IID IID_IVdsDisk = { 0x07e5c822, 0xf00c, 0x47a1, { 0x8f, 0xce, 0xb2, 0x44, 0xda, 0x56, 0xfd, 0x06 } };
const IID IID_IVdsAdvancedDisk = { 0x6e6f6b40, 0x977c, 0x4069, { 0xbd, 0xdd, 0xac, 0x71, 0x00, 0x59, 0xf8, 0xc0 } };
const IID IID_IVdsVolume = { 0x88306BB2, 0xE71F, 0x478C, { 0x86, 0xA2, 0x79, 0xDA, 0x20, 0x0A, 0x0F, 0x11} };
const IID IID_IVdsVolumeMF3 = { 0x6788FAF9, 0x214E, 0x4B85, { 0xBA, 0x59, 0x26, 0x69, 0x53, 0x61, 0x6E, 0x09 } };
#endif

PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryVolumeInformationFile, (HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FS_INFORMATION_CLASS));

/*
 * Globals
 */
RUFUS_DRIVE_INFO SelectedDrive;
BOOL installed_uefi_ntfs;
uint64_t partition_offset[3];
uint64_t persistence_size = 0;

/*
 * The following methods get or set the AutoMount setting (which is different from AutoRun)
 * Rufus needs AutoMount to be set as the format process may fail for fixed drives otherwise.
 * See https://github.com/pbatard/rufus/issues/386.
 *
 * Reverse engineering diskpart and mountvol indicates that the former uses the IVdsService
 * ClearFlags()/SetFlags() to set VDS_SVF_AUTO_MOUNT_OFF whereas mountvol on uses
 * IOCTL_MOUNTMGR_SET_AUTO_MOUNT on "\\\\.\\MountPointManager".
 * As the latter is MUCH simpler this is what we'll use too
 */
BOOL SetAutoMount(BOOL enable)
{
	HANDLE hMountMgr;
	DWORD size;
	BOOL ret = FALSE;

	hMountMgr = CreateFileA(MOUNTMGR_DOS_DEVICE_NAME, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hMountMgr == NULL)
		return FALSE;
	ret = DeviceIoControl(hMountMgr, IOCTL_MOUNTMGR_SET_AUTO_MOUNT, &enable, sizeof(enable), NULL, 0, &size, NULL);
	CloseHandle(hMountMgr);
	return ret;
}

BOOL GetAutoMount(BOOL* enabled)
{
	HANDLE hMountMgr;
	DWORD size;
	BOOL ret = FALSE;

	if (enabled == NULL)
		return FALSE;
	hMountMgr = CreateFileA(MOUNTMGR_DOS_DEVICE_NAME, GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hMountMgr == NULL)
		return FALSE;
	ret = DeviceIoControl(hMountMgr, IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT, NULL, 0, enabled, sizeof(*enabled), &size, NULL);
	CloseHandle(hMountMgr);
	return ret;
}

/*
 * Working with drive indexes quite risky (left unchecked,inadvertently passing 0 as
 * index would return a handle to C:, which we might then proceed to unknowingly
 * clear the MBR of!), so we mitigate the risk by forcing our indexes to belong to
 * the specific range [DRIVE_INDEX_MIN; DRIVE_INDEX_MAX].
 */
#define CheckDriveIndex(DriveIndex) do {                                            \
	if ((int)DriveIndex < 0) goto out;                                              \
	assert((DriveIndex >= DRIVE_INDEX_MIN) && (DriveIndex <= DRIVE_INDEX_MAX));     \
	if ((DriveIndex < DRIVE_INDEX_MIN) || (DriveIndex > DRIVE_INDEX_MAX)) goto out; \
	DriveIndex -= DRIVE_INDEX_MIN; } while (0)

/*
 * Open a drive or volume with optional write and lock access
 * Return INVALID_HANDLE_VALUE (/!\ which is DIFFERENT from NULL /!\) on failure.
 */
static HANDLE GetHandle(char* Path, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	int i;
	BYTE access_mask = 0;
	DWORD size;
	uint64_t EndTime;
	HANDLE hDrive = INVALID_HANDLE_VALUE;
	char DevPath[MAX_PATH];

	if ((safe_strlen(Path) < 5) || (Path[0] != '\\') || (Path[1] != '\\') || (Path[3] != '\\'))
		goto out;

	// Resolve a device path, so that we can look for that handle in case of access issues.
	if (safe_strncmp(Path, groot_name, groot_len) == 0)
		static_strcpy(DevPath, &Path[groot_len]);
	else if (QueryDosDeviceA(&Path[4], DevPath, sizeof(DevPath)) == 0)
		strcpy(DevPath, "???");

	for (i = 0; i < DRIVE_ACCESS_RETRIES; i++) {
		// Try without FILE_SHARE_WRITE (unless specifically requested) so that
		// we won't be bothered by the OS or other apps when we set up our data.
		// However this means we might have to wait for an access gap...
		// We keep FILE_SHARE_READ though, as this shouldn't hurt us any, and is
		// required for enumeration.
		hDrive = CreateFileA(Path, GENERIC_READ|(bWriteAccess?GENERIC_WRITE:0),
			FILE_SHARE_READ|(bWriteShare?FILE_SHARE_WRITE:0),
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDrive != INVALID_HANDLE_VALUE)
			break;
		if ((GetLastError() != ERROR_SHARING_VIOLATION) && (GetLastError() != ERROR_ACCESS_DENIED))
			break;
		if (i == 0) {
			uprintf("Waiting for access on %s [%s]...", Path, DevPath);
		} else if (!bWriteShare && (i > DRIVE_ACCESS_RETRIES/3)) {
			// If we can't seem to get a hold of the drive for some time, try to enable FILE_SHARE_WRITE...
			uprintf("Warning: Could not obtain exclusive rights. Retrying with write sharing enabled...");
			bWriteShare = TRUE;
			// Try to report the process that is locking the drive
			// We also use bit 6 as a flag to indicate that SearchProcess was called.
			access_mask = SearchProcess(DevPath, SEARCH_PROCESS_TIMEOUT, TRUE, TRUE, FALSE) | 0x40;
		}
		Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
	}
	if (hDrive == INVALID_HANDLE_VALUE) {
		uprintf("Could not open %s: %s", Path, WindowsErrorString());
		goto out;
	}

	if (bWriteAccess) {
		uprintf("Opened %s for %s write access", Path, bWriteShare?"shared":"exclusive");
	}

	if (bLockDrive) {
		if (DeviceIoControl(hDrive, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &size, NULL)) {
			uprintf("I/O boundary checks disabled");
		}

		EndTime = GetTickCount64() + DRIVE_ACCESS_TIMEOUT;
		do {
			if (DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL))
				goto out;
			if (IS_ERROR(FormatStatus))	// User cancel
				break;
			Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
		} while (GetTickCount64() < EndTime);
		// If we reached this section, either we didn't manage to get a lock or the user cancelled
		uprintf("Could not lock access to %s: %s", Path, WindowsErrorString());
		// See if we can report the processes are accessing the drive
		if (!IS_ERROR(FormatStatus) && (access_mask == 0))
			access_mask = SearchProcess(DevPath, SEARCH_PROCESS_TIMEOUT, TRUE, TRUE, FALSE);
		// Try to continue if the only access rights we saw were for read-only
		if ((access_mask & 0x07) != 0x01)
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
	static_sprintf(physical_name, "\\\\.\\PhysicalDrive%lu", DriveIndex);
	success = TRUE;
out:
	return (success)?safe_strdup(physical_name):NULL;
}

/*
 * Return the path to access a partition on a specific disk, or NULL on error.
 * The string is allocated and must be freed (to ensure concurrent access)
 * If PartitionOffset is 0, the offset is ignored and the first partition found is returned.
 */
char* GetPartitionName(DWORD DriveIndex, uint64_t PartitionOffset)
{
	char partition_name[32];
	DWORD i = MAX_PARTITIONS + 1;

	CheckDriveIndex(DriveIndex);
	for (i = 1; (i <= MAX_PARTITIONS) && (PartitionOffset != 0) && (SelectedDrive.PartitionOffset[i - 1] != PartitionOffset); i++);
	static_sprintf(partition_name, "\\Device\\Harddisk%lu\\Partition%lu", DriveIndex, i);
out:
	return (i <= MAX_PARTITIONS) ? safe_strdup(partition_name) : NULL;
}

/*
 * Return a handle to the physical drive identified by DriveIndex
 */
HANDLE GetPhysicalHandle(DWORD DriveIndex, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	HANDLE hPhysical = INVALID_HANDLE_VALUE;
	char* PhysicalPath = GetPhysicalName(DriveIndex);
	hPhysical = GetHandle(PhysicalPath, bLockDrive, bWriteAccess, bWriteShare);
	safe_free(PhysicalPath);
	return hPhysical;
}

/*
 * Return the GUID volume name for the disk and partition specified, or NULL if not found.
 * See http://msdn.microsoft.com/en-us/library/cc542456.aspx
 * If PartitionOffset is 0, the offset is ignored and the first partition found is returned.
 * The returned string is allocated and must be freed.
 */
char* GetLogicalName(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bKeepTrailingBackslash, BOOL bSilent)
{
	static const char* ignore_device[] = { "\\Device\\CdRom", "\\Device\\Floppy" };
	static const char* volume_start = "\\\\?\\";
	char *ret = NULL, volume_name[MAX_PATH], path[MAX_PATH];
	BOOL bPrintHeader = TRUE;
	HANDLE hDrive = INVALID_HANDLE_VALUE, hVolume = INVALID_HANDLE_VALUE;
	VOLUME_DISK_EXTENTS_REDEF DiskExtents;
	DWORD size;
	UINT drive_type;
	StrArray found_name;
	uint64_t found_offset[MAX_PARTITIONS] = { 0 };
	uint32_t i, j;
	size_t len;

	StrArrayCreate(&found_name, MAX_PARTITIONS);
	CheckDriveIndex(DriveIndex);

	for (i = 0; hDrive == INVALID_HANDLE_VALUE; i++) {
		if (i == 0) {
			hVolume = FindFirstVolumeA(volume_name, sizeof(volume_name));
			if (hVolume == INVALID_HANDLE_VALUE) {
				suprintf("Could not access first GUID volume: %s", WindowsErrorString());
				goto out;
			}
		} else {
			if (!FindNextVolumeA(hVolume, volume_name, sizeof(volume_name))) {
				if (GetLastError() != ERROR_NO_MORE_FILES) {
					suprintf("Could not access next GUID volume: %s", WindowsErrorString());
				}
				break;
			}
		}

		// Sanity checks
		len = safe_strlen(volume_name);
		assert(len > 4);
		assert(safe_strnicmp(volume_name, volume_start, 4) == 0);
		assert(volume_name[len - 1] == '\\');

		drive_type = GetDriveTypeA(volume_name);
		if ((drive_type != DRIVE_REMOVABLE) && (drive_type != DRIVE_FIXED))
			continue;

		volume_name[len-1] = 0;

		if (QueryDosDeviceA(&volume_name[4], path, sizeof(path)) == 0) {
			suprintf("Failed to get device path for GUID volume '%s': %s", volume_name, WindowsErrorString());
			continue;
		}

		for (j=0; (j<ARRAYSIZE(ignore_device)) &&
			(_strnicmp(path, ignore_device[j], safe_strlen(ignore_device[j])) != 0); j++);
		if (j < ARRAYSIZE(ignore_device)) {
			suprintf("Skipping GUID volume for '%s'", path);
			continue;
		}

		hDrive = CreateFileA(volume_name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDrive == INVALID_HANDLE_VALUE) {
			suprintf("Could not open GUID volume '%s': %s", volume_name, WindowsErrorString());
			continue;
		}

		if ((!DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0,
			&DiskExtents, sizeof(DiskExtents), &size, NULL)) || (size <= 0)) {
			suprintf("Could not get Disk Extents: %s", WindowsErrorString());
			safe_closehandle(hDrive);
			continue;
		}
		safe_closehandle(hDrive);
		if (DiskExtents.NumberOfDiskExtents == 0) {
			suprintf("Ignoring volume '%s' because it has no extents...", volume_name);
			continue;
		}
		if (DiskExtents.NumberOfDiskExtents != 1) {
			// If we have more than one extent for a volume, it means that someone
			// is using RAID-1 or something => Stay well away from such a volume!
			suprintf("Ignoring volume '%s' because it has more than one extent (RAID?)...", volume_name);
			continue;
		}
		if (DiskExtents.Extents[0].DiskNumber != DriveIndex)
			// Not on our disk
			continue;

		if (found_name.Index == MAX_PARTITIONS) {
			uprintf("Error: Trying to process a disk with more than %d partitions!", MAX_PARTITIONS);
			goto out;
		}

		if (bKeepTrailingBackslash)
			volume_name[len - 1] = '\\';
		found_offset[found_name.Index] = DiskExtents.Extents[0].StartingOffset.QuadPart;
		StrArrayAdd(&found_name, volume_name, TRUE);
		if (!bSilent) {
			if (bPrintHeader) {
				bPrintHeader = FALSE;
				uuprintf("Windows volumes from this device:");
			}
			uuprintf("● %s @%lld", volume_name, DiskExtents.Extents[0].StartingOffset.QuadPart);
		}
	}

	if (found_name.Index == 0)
		goto out;

	// Now process all the volumes we found, and try to match one with our partition offset
	for (i = 0; (i < found_name.Index) && (PartitionOffset != 0) && (PartitionOffset != found_offset[i]); i++);

	if (i < found_name.Index)
		ret = safe_strdup(found_name.String[i]);
	else
		// NB: We need to re-add DRIVE_INDEX_MIN for this call since CheckDriveIndex() substracted it
		ret = AltGetLogicalName(DriveIndex + DRIVE_INDEX_MIN, PartitionOffset, bKeepTrailingBackslash, bSilent);

out:
	if (hVolume != INVALID_HANDLE_VALUE)
		FindVolumeClose(hVolume);
	StrArrayDestroy(&found_name);
	return ret;
}

/*
 * Alternative version of the above, needed because some volumes, such as ESPs, are not listed
 * by Windows, be it with VDS or other APIs.
 * For these, we return the "\\?\GLOBALROOT\Device\HarddiskVolume#" identifier that matches
 * our "Harddisk#Partition#", as reported by QueryDosDevice().
 * The returned string is allocated and must be freed.
*/
char* AltGetLogicalName(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bKeepTrailingBackslash, BOOL bSilent)
{
	DWORD i;
	char *ret = NULL, volume_name[MAX_PATH], path[MAX_PATH];

	CheckDriveIndex(DriveIndex);

	// Match the offset to a partition index
	for (i = 0; (i < MAX_PARTITIONS) && (PartitionOffset != 0) && (PartitionOffset != SelectedDrive.PartitionOffset[i]); i++);
	if (i >= MAX_PARTITIONS) {
		suprintf("Error: Could not find a partition at offset %lld on this disk", PartitionOffset);
		goto out;
	}
	static_sprintf(path, "Harddisk%luPartition%lu", DriveIndex, i + 1);
	static_strcpy(volume_name, groot_name);
	if (!QueryDosDeviceA(path, &volume_name[groot_len], (DWORD)(MAX_PATH - groot_len)) || (strlen(volume_name) < 20)) {
		suprintf("Could not find the DOS volume name for '%s': %s", path, WindowsErrorString());
	} else {
		if (bKeepTrailingBackslash)
			static_strcat(volume_name, "\\");
		ret = safe_strdup(volume_name);
	}

out:
	return ret;
}

/*
 * Call on VDS to refresh the drive layout
 */
BOOL RefreshLayout(DWORD DriveIndex)
{
	BOOL r = FALSE;
	HRESULT hr;
	wchar_t wPhysicalName[24];
	IVdsServiceLoader *pLoader;
	IVdsService *pService;
	IEnumVdsObject *pEnum;

	CheckDriveIndex(DriveIndex);
	wnsprintf(wPhysicalName, ARRAYSIZE(wPhysicalName), L"\\\\?\\PhysicalDrive%lu", DriveIndex);

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

	// Remove mountpoints
	hr = IVdsService_CleanupObsoleteMountPoints(pService);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not clean up VDS mountpoints: %s", WindowsErrorString());
		goto out;
	}

	// Invoke layout refresh
	hr = IVdsService_Refresh(pService);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not refresh VDS layout: %s", WindowsErrorString());
		goto out;
	}

	// Force re-enum
	hr = IVdsService_Reenumerate(pService);
	if (hr != S_OK) {
		VDS_SET_ERROR(hr);
		uprintf("Could not refresh VDS layout: %s", WindowsErrorString());
		goto out;
	}
	r = TRUE;

	out:
		return r;
}

/*
 * Delete all the partitions from a disk, using VDS
 * Mostly copied from https://social.msdn.microsoft.com/Forums/vstudio/en-US/b90482ae-4e44-4b08-8731-81915030b32a/createpartition-using-vds-interface-throw-error-enointerface-dcom?forum=vcgeneral
 */
BOOL DeletePartitions(DWORD DriveIndex)
{
	BOOL r = FALSE;
	HRESULT hr;
	ULONG ulFetched;
	wchar_t wPhysicalName[24];
	IVdsServiceLoader *pLoader;
	IVdsService *pService;
	IEnumVdsObject *pEnum;
	IUnknown *pUnk;

	CheckDriveIndex(DriveIndex);
	wnsprintf(wPhysicalName, ARRAYSIZE(wPhysicalName), L"\\\\?\\PhysicalDrive%lu", DriveIndex);

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
			IEnumVdsObject *pEnumDisk;
			IUnknown *pDiskUnk;

			hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void **)&pPack);
			IUnknown_Release(pPackUnk);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS Software Provider Pack: %s", WindowsErrorString());
				goto out;
			}

			// Use the pack interface to access the disks
			hr = IVdsPack_QueryDisks(pPack, &pEnumDisk);
			if (hr != S_OK) {
				VDS_SET_ERROR(hr);
				uprintf("Could not query VDS disks: %s", WindowsErrorString());
				goto out;
			}

			// List disks
			while (IEnumVdsObject_Next(pEnumDisk, 1, &pDiskUnk, &ulFetched) == S_OK) {
				VDS_DISK_PROP diskprop;
				VDS_PARTITION_PROP* prop_array;
				LONG i, prop_array_size;
				IVdsDisk *pDisk;
				IVdsAdvancedDisk *pAdvancedDisk;

				// Get the disk interface.
				hr = IUnknown_QueryInterface(pDiskUnk, &IID_IVdsDisk, (void **)&pDisk);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS Disk Interface: %s", WindowsErrorString());
					goto out;
				}

				// Get the disk properties
				hr = IVdsDisk_GetProperties(pDisk, &diskprop);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not query VDS Disk Properties: %s", WindowsErrorString());
					goto out;
				}

				// Isolate the disk we want
				if (_wcsicmp(wPhysicalName, diskprop.pwszName) != 0) {
					IVdsDisk_Release(pDisk);
					continue;
				}

				// Instantiate the AdvanceDisk interface for our disk.
				hr = IVdsDisk_QueryInterface(pDisk, &IID_IVdsAdvancedDisk, (void **)&pAdvancedDisk);
				IVdsDisk_Release(pDisk);
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not access VDS Advanced Disk interface: %s", WindowsErrorString());
					goto out;
				}

				// Query the partition data, so we can get the start offset, which we need for deletion
				hr = IVdsAdvancedDisk_QueryPartitions(pAdvancedDisk, &prop_array, &prop_array_size);
				if (hr == S_OK) {
					uprintf("Deleting ALL partition(s) from disk '%S':", diskprop.pwszName);
					// Now go through each partition
					for (i = 0; i < prop_array_size; i++) {
						uprintf("● Partition %d (offset: %lld, size: %s)", prop_array[i].ulPartitionNumber,
							prop_array[i].ullOffset, SizeToHumanReadable(prop_array[i].ullSize, FALSE, FALSE));
						hr = IVdsAdvancedDisk_DeletePartition(pAdvancedDisk, prop_array[i].ullOffset, TRUE, TRUE);
						if (hr != S_OK) {
							r = FALSE;
							VDS_SET_ERROR(hr);
							uprintf("Could not delete partitions: %s", WindowsErrorString());
						}
					}
					r = TRUE;
				} else {
					uprintf("No partition to delete on disk '%S'", diskprop.pwszName);
					r = TRUE;
				}
				CoTaskMemFree(prop_array);

#if 0
				// Issue a Clean while we're at it
				HRESULT hr2 = E_FAIL;
				ULONG completed;
				IVdsAsync* pAsync;
				hr = IVdsAdvancedDisk_Clean(pAdvancedDisk, TRUE, FALSE, FALSE, &pAsync);
				while (SUCCEEDED(hr)) {
					if (IS_ERROR(FormatStatus)) {
						IVdsAsync_Cancel(pAsync);
						break;
					}
					hr = IVdsAsync_QueryStatus(pAsync, &hr2, &completed);
					if (SUCCEEDED(hr)) {
						hr = hr2;
						if (hr == S_OK)
							break;
						if (hr == VDS_E_OPERATION_PENDING)
							hr = S_OK;
					}
					Sleep(500);
				}
				if (hr != S_OK) {
					VDS_SET_ERROR(hr);
					uprintf("Could not clean disk: %s", WindowsErrorString());
				}
#endif
				IVdsAdvancedDisk_Release(pAdvancedDisk);
				goto out;
			}
		}
	}

out:
	return r;
}


/* Wait for a logical drive to reappear - Used when a drive has just been repartitioned */
BOOL WaitForLogical(DWORD DriveIndex, uint64_t PartitionOffset)
{
	uint64_t EndTime;
	char* LogicalPath = NULL;

	// GetLogicalName() calls may be slow, so use the system time to
	// make sure we don't spend more than DRIVE_ACCESS_TIMEOUT in wait.
	EndTime = GetTickCount64() + DRIVE_ACCESS_TIMEOUT;
	do {
		LogicalPath = GetLogicalName(DriveIndex, PartitionOffset, FALSE, TRUE);
		// Need to filter out GlobalRoot devices as we don't want to wait on those
		if ((LogicalPath != NULL) && (strncmp(LogicalPath, groot_name, groot_len) != 0)) {
			free(LogicalPath);
			return TRUE;
		}
		free(LogicalPath);
		if (IS_ERROR(FormatStatus))	// User cancel
			return FALSE;
		Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
	} while (GetTickCount64() < EndTime);
	uprintf("Timeout while waiting for logical drive");
	return FALSE;
}

/*
 * Obtain a handle to the volume identified by DriveIndex + PartitionIndex
 * Returns INVALID_HANDLE_VALUE on error or NULL if no logical path exists (typical
 * of unpartitioned drives)
 */
HANDLE GetLogicalHandle(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	HANDLE hLogical = INVALID_HANDLE_VALUE;
	char* LogicalPath = GetLogicalName(DriveIndex, PartitionOffset, FALSE, FALSE);

	if (LogicalPath == NULL) {
		uprintf("No logical drive found (unpartitioned?)");
		return NULL;
	}

	hLogical = GetHandle(LogicalPath, bLockDrive, bWriteAccess, bWriteShare);
	free(LogicalPath);
	return hLogical;
}

/*
 * Similar to the above, but use the partition name instead
 */
HANDLE GetPartitionHandle(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	char* volume_name = GetPartitionName(DriveIndex, PartitionOffset);

	if (volume_name == NULL) {
		uprintf("Could not get partition volume name");
		return NULL;
	}

	handle = GetHandle(volume_name, bLockDrive, bWriteAccess, bWriteShare);
	free(volume_name);
	return handle;
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
	IO_STATUS_BLOCK io_status_block;
	FILE_FS_DEVICE_INFORMATION file_fs_device_info;
	int i = 0, drive_number;
	char *drive, drives[26*4 + 1];	/* "D:\", "E:\", etc., plus one NUL */
	char logical_drive[] = "\\\\.\\#:";

	PF_INIT(NtQueryVolumeInformationFile, Ntdll);

	if (drive_letters != NULL)
		drive_letters[0] = 0;
	if (drive_type != NULL)
		*drive_type = DRIVE_UNKNOWN;
	CheckDriveIndex(DriveIndex);

	// This call is weird... The buffer needs to have an extra NUL, but you're
	// supposed to provide the size without the extra NUL. And the returned size
	// does not include the NUL either *EXCEPT* if your buffer is too small...
	// But then again, this doesn't hold true if you have a 105 byte buffer and
	// pass a 4*26=104 size, as the the call will return 105 (i.e. *FAILURE*)
	// instead of 104 as it should => screw Microsoft: We'll include the NUL
	// always, as each drive string is at least 4 chars long anyway.
	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s", WindowsErrorString());
		goto out;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: Buffer too small (required %d vs. %d)", size, sizeof(drives));
		goto out;
	}

	r = TRUE;	// Required to detect drives that don't have volumes assigned
	for (drive = drives ;*drive; drive += safe_strlen(drive)+1) {
		if (!isalpha(*drive))
			continue;
		*drive = (char)toupper((int)*drive);

		// IOCTL_STORAGE_GET_DEVICE_NUMBER's STORAGE_DEVICE_NUMBER.DeviceNumber is
		// not unique! An HDD, a DVD and probably other drives can have the same
		// value there => Use GetDriveType() to filter out unwanted devices.
		// See https://github.com/pbatard/rufus/issues/32#issuecomment-3785956
		_drive_type = GetDriveTypeA(drive);

		if ((_drive_type != DRIVE_REMOVABLE) && (_drive_type != DRIVE_FIXED))
			continue;

		static_sprintf(logical_drive, "\\\\.\\%c:", drive[0]);
		hDrive = CreateFileA(logical_drive, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hDrive == INVALID_HANDLE_VALUE) {
//			uprintf("Warning: could not open drive %c: %s", drive[0], WindowsErrorString());
			continue;
		}

		// Eliminate floppy drives
		if ((pfNtQueryVolumeInformationFile != NULL) &&
			(pfNtQueryVolumeInformationFile(hDrive, &io_status_block, &file_fs_device_info,
				sizeof(file_fs_device_info), FileFsDeviceInformation) == NO_ERROR) &&
			(file_fs_device_info.Characteristics & FILE_FLOPPY_DISKETTE) ) {
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
	char drive_letter = 'Z'+1, *drive, drives[26*4 + 1];	/* "D:\", "E:\", etc., plus one NUL */

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s", WindowsErrorString());
		goto out;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: Buffer too small (required %d vs. %d)", size, sizeof(drives));
		goto out;
	}

	for (drive_letter = 'C'; drive_letter <= 'Z'; drive_letter++) {
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
	DWORD size, error;
	static char VolumeLabel[MAX_PATH + 1] = { 0 };
	char DrivePath[] = "#:\\", AutorunPath[] = "#:\\autorun.inf", *AutorunLabel = NULL;
	WCHAR VolumeName[MAX_PATH + 1] = { 0 }, FileSystemName[64];
	DWORD VolumeSerialNumber, MaximumComponentLength, FileSystemFlags;

	*label = STR_NO_LABEL;

	if (!GetDriveLetters(DriveIndex, letters))
		return FALSE;
	if (letters[0] == 0) {
		// Even if we don't have a letter, try to obtain the label of the first partition
		HANDLE h = GetLogicalHandle(DriveIndex, 0, FALSE, FALSE, FALSE);
		if (GetVolumeInformationByHandleW(h, VolumeName, 64, &VolumeSerialNumber,
			&MaximumComponentLength, &FileSystemFlags, FileSystemName, 64)) {
			wchar_to_utf8_no_alloc(VolumeName, VolumeLabel, sizeof(VolumeLabel));
			*label = VolumeLabel;
		}
		// Drive without volume assigned - always enabled
		return TRUE;
	}
	// We only care about an autorun.inf if we have a single volume
	AutorunPath[0] = letters[0];
	DrivePath[0] = letters[0];

	// Try to read an extended label from autorun first. Fallback to regular label if not found.
	// In the case of card readers with no card, users can get an annoying popup asking them
	// to insert media. Use IOCTL_STORAGE_CHECK_VERIFY to prevent this
	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
	if (DeviceIoControl(hPhysical, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, &size, NULL))
		AutorunLabel = get_token_data_file("label", AutorunPath);
	else if (GetLastError() == ERROR_NOT_READY)
		uprintf("Ignoring autorun.inf label for drive %c: %s", letters[0],
		(HRESULT_CODE(GetLastError()) == ERROR_NOT_READY)?"No media":WindowsErrorString());
	safe_closehandle(hPhysical);
	if (AutorunLabel != NULL) {
		uprintf("Using autorun.inf label for drive %c: '%s'", letters[0], AutorunLabel);
		static_strcpy(VolumeLabel, AutorunLabel);
		safe_free(AutorunLabel);
		*label = VolumeLabel;
	} else if (GetVolumeInformationU(DrivePath, VolumeLabel, ARRAYSIZE(VolumeLabel),
		NULL, NULL, NULL, NULL, 0) && (VolumeLabel[0] != 0)) {
		*label = VolumeLabel;
	} else {
		// Might be an extfs label
		error = GetLastError();
		*label = (char*)GetExtFsLabel(DriveIndex, 0);
		if (*label == NULL) {
			SetLastError(error);
			duprintf("Failed to read label: %s", WindowsErrorString());
			*label = STR_NO_LABEL;
		}
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

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
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

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, geometry, sizeof(geometry), &size, NULL) && (size > 0);
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
	{ is_kolibrios_mbr, "KolibriOS" },
	{ is_grub4dos_mbr, "Grub4DOS" },
	{ is_grub2_mbr, "Grub 2.0" },
	{ is_zero_mbr_not_including_disk_signature_or_copy_protect, "Zeroed" },
};

// Returns TRUE if the drive seems bootable, FALSE otherwise
BOOL AnalyzeMBR(HANDLE hPhysicalDrive, const char* TargetName, BOOL bSilent)
{
	const char* mbr_name = "Master Boot Record";
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	int i;

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	if (!is_br(fp)) {
		suprintf("%s does not have an x86 %s", TargetName, mbr_name);
		return FALSE;
	}
	for (i=0; i<ARRAYSIZE(known_mbr); i++) {
		if (known_mbr[i].fn(fp)) {
			suprintf("%s has a %s %s", TargetName, known_mbr[i].str, mbr_name);
			return TRUE;
		}
	}

	suprintf("%s has an unknown %s", TargetName, mbr_name);
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
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	int i;

	fake_fd._handle = (char*)hLogicalVolume;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	if (!is_br(fp)) {
		uprintf("Volume does not have an x86 %s", pbr_name);
		return FALSE;
	}

	if (is_fat_16_br(fp) || is_fat_32_br(fp)) {
		for (i=0; i<ARRAYSIZE(known_pbr); i++) {
			if (known_pbr[i].fn(fp)) {
				uprintf("Drive has a %s %s", known_pbr[i].str, pbr_name);
				return TRUE;
			}
		}
		uprintf("Volume has an unknown FAT16 or FAT32 %s", pbr_name);
	} else {
		uprintf("Volume has an unknown %s", pbr_name);
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
	BOOL r, ret = FALSE, isUefiNtfs;
	HANDLE hPhysical;
	DWORD size, i, j, super_floppy_disk = FALSE;
	BYTE geometry[256] = {0}, layout[4096] = {0}, part_type;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)(void*)geometry;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)(void*)layout;
	char *volume_name, *buf;

	if (FileSystemName == NULL)
		return FALSE;

	SelectedDrive.nPartitions = 0;
	memset(SelectedDrive.PartitionOffset, 0, sizeof(SelectedDrive.PartitionOffset));
	// Populate the filesystem data
	FileSystemName[0] = 0;
	volume_name = GetLogicalName(DriveIndex, 0, TRUE, FALSE);
	if ((volume_name == NULL) || (!GetVolumeInformationA(volume_name, NULL, 0, NULL, NULL, NULL, FileSystemName, FileSystemNameSize))) {
		suprintf("No volume information for drive 0x%02x", DriveIndex);
	}
	safe_free(volume_name);

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return 0;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
			NULL, 0, geometry, sizeof(geometry), &size, NULL);
	if (!r || size <= 0) {
		suprintf("Could not get geometry for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return 0;
	}
	SelectedDrive.DiskSize = DiskGeometry->DiskSize.QuadPart;
	SelectedDrive.SectorSize = DiskGeometry->Geometry.BytesPerSector;
	SelectedDrive.FirstDataSector = MAXDWORD;
	if (SelectedDrive.SectorSize < 512) {
		suprintf("Warning: Drive 0x%02x reports a sector size of %d - Correcting to 512 bytes.",
			DriveIndex, SelectedDrive.SectorSize);
		SelectedDrive.SectorSize = 512;
	}
	SelectedDrive.SectorsPerTrack = DiskGeometry->Geometry.SectorsPerTrack;
	SelectedDrive.MediaType = DiskGeometry->Geometry.MediaType;

	suprintf("Disk type: %s, Disk size: %s, Sector size: %d bytes", (SelectedDrive.MediaType == FixedMedia)?"FIXED":"Removable",
		SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, TRUE), SelectedDrive.SectorSize);
	suprintf("Cylinders: %" PRIi64 ", Tracks per cylinder: %d, Sectors per track: %d",
		DiskGeometry->Geometry.Cylinders, DiskGeometry->Geometry.TracksPerCylinder, DiskGeometry->Geometry.SectorsPerTrack);

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
			NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		suprintf("Could not get layout for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return 0;
	}

#if defined(__GNUC__)
// GCC 4.9 bugs us about the fact that MS defined an expandable array as array[1]
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
	switch (DriveLayout->PartitionStyle) {
	case PARTITION_STYLE_MBR:
		SelectedDrive.PartitionStyle = PARTITION_STYLE_MBR;
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				SelectedDrive.nPartitions++;
			}
		}
		// Detect drives that are using the whole disk as a single partition
		if ((DriveLayout->PartitionEntry[0].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) &&
			(DriveLayout->PartitionEntry[0].StartingOffset.QuadPart == 0LL)) {
			suprintf("Partition type: SFD (%s) or Unpartitioned", sfd_name);
			super_floppy_disk = TRUE;
		} else {
			suprintf("Partition type: MBR, NB Partitions: %d", SelectedDrive.nPartitions);
			SelectedDrive.has_mbr_uefi_marker = (DriveLayout->Mbr.Signature == MBR_UEFI_MARKER);
			suprintf("Disk ID: 0x%08X %s", DriveLayout->Mbr.Signature, SelectedDrive.has_mbr_uefi_marker?"(UEFI target)":"");
			AnalyzeMBR(hPhysical, "Drive", bSilent);
		}
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			isUefiNtfs = FALSE;
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				part_type = DriveLayout->PartitionEntry[i].Mbr.PartitionType;
				if (part_type == 0xef) {
					// Check the FAT label to see if we're dealing with an UEFI_NTFS partition
					buf = calloc(SelectedDrive.SectorSize, 1);
					if (buf != NULL) {
						if (SetFilePointerEx(hPhysical, DriveLayout->PartitionEntry[i].StartingOffset, NULL, FILE_BEGIN) &&
							ReadFile(hPhysical, buf, SelectedDrive.SectorSize, &size, NULL)) {
							isUefiNtfs = (strncmp(&buf[0x2B], "UEFI_NTFS", 9) == 0);
						}
						free(buf);
					}
				}
				suprintf("Partition %d%s:", i+(super_floppy_disk?0:1), isUefiNtfs?" (UEFI:NTFS)":"");
				for (j=0; j<ARRAYSIZE(mbr_mountable); j++) {
					if (part_type == mbr_mountable[j]) {
						ret = TRUE;
						break;
					}
				}
				if (i < MAX_PARTITIONS)
					SelectedDrive.PartitionOffset[i] = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
				// NB: MinGW's gcc 4.9.2 broke "%lld" printout on XP so we use the inttypes.h "PRI##" qualifiers
				suprintf("  Type: %s (0x%02x)\r\n  Size: %s (%" PRIi64 " bytes)\r\n  Start Sector: %" PRIi64 ", Boot: %s",
					((part_type==0x07||super_floppy_disk)&&(FileSystemName[0]!=0))?FileSystemName:GetPartitionType(part_type), super_floppy_disk?0:part_type,
					SizeToHumanReadable(DriveLayout->PartitionEntry[i].PartitionLength.QuadPart, TRUE, FALSE),
					DriveLayout->PartitionEntry[i].PartitionLength.QuadPart,
					DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize,
					DriveLayout->PartitionEntry[i].Mbr.BootIndicator?"Yes":"No");
				// suprintf("  GUID: %s", GuidToString(&DriveLayout->PartitionEntry[i].Mbr.PartitionId));
				SelectedDrive.FirstDataSector = min(SelectedDrive.FirstDataSector,
					(DWORD)(DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize));
				if ((part_type == RUFUS_EXTRA_PARTITION_TYPE) || (isUefiNtfs))
					// This is a partition Rufus created => we can safely ignore it
					--SelectedDrive.nPartitions;
				if (part_type == 0xee)	// Flag a protective MBR for non GPT platforms (XP)
					SelectedDrive.has_protective_mbr = TRUE;
			}
		}
		break;
	case PARTITION_STYLE_GPT:
		SelectedDrive.PartitionStyle = PARTITION_STYLE_GPT;
		suprintf("Partition type: GPT, NB Partitions: %d", DriveLayout->PartitionCount);
		suprintf("Disk GUID: %s", GuidToString(&DriveLayout->Gpt.DiskId));
		suprintf("Max parts: %d, Start Offset: %" PRIi64 ", Usable = %" PRIi64 " bytes",
			DriveLayout->Gpt.MaxPartitionCount, DriveLayout->Gpt.StartingUsableOffset.QuadPart, DriveLayout->Gpt.UsableLength.QuadPart);
		for (i=0; i<DriveLayout->PartitionCount; i++) {
			if (i < MAX_PARTITIONS)
				SelectedDrive.PartitionOffset[i] = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
			SelectedDrive.nPartitions++;
			isUefiNtfs = (wcscmp(DriveLayout->PartitionEntry[i].Gpt.Name, L"UEFI:NTFS") == 0);
			suprintf("Partition %d%s:\r\n  Type: %s\r\n  Name: '%S'", i+1, isUefiNtfs ? " (UEFI:NTFS)" : "",
				GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionType), DriveLayout->PartitionEntry[i].Gpt.Name);
			suprintf("  ID: %s\r\n  Size: %s (%" PRIi64 " bytes)\r\n  Start Sector: %" PRIi64 ", Attributes: 0x%016" PRIX64,
				GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionId),
				SizeToHumanReadable(DriveLayout->PartitionEntry[i].PartitionLength.QuadPart, TRUE, FALSE),
				DriveLayout->PartitionEntry[i].PartitionLength,
				DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize,
				DriveLayout->PartitionEntry[i].Gpt.Attributes);
			SelectedDrive.FirstDataSector = min(SelectedDrive.FirstDataSector,
				(DWORD)(DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize));
			// Don't register the partitions that we don't care about destroying
			if ( isUefiNtfs ||
				 (CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_MSFT_RESERVED_GUID)) ||
				 (CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_SYSTEM_GUID)) )
				--SelectedDrive.nPartitions;
			if (memcmp(&PARTITION_BASIC_DATA_GUID, &DriveLayout->PartitionEntry[i].Gpt.PartitionType, sizeof(GUID)) == 0)
				ret = TRUE;
		}
		break;
	default:
		SelectedDrive.PartitionStyle = PARTITION_STYLE_MBR;
		suprintf("Partition type: RAW");
		break;
	}
#if defined(__GNUC__)
#pragma GCC diagnostic warning "-Warray-bounds"
#endif
	safe_closehandle(hPhysical);

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
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDrive == INVALID_HANDLE_VALUE) {
		uprintf("Failed to open %c: for flushing: %s", drive_letter, WindowsErrorString());
		goto out;
	}
	r = FlushFileBuffers(hDrive);
	if (r == FALSE)
		uprintf("Failed to flush %c: %s", drive_letter, WindowsErrorString());

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
		uprintf("Could not unmount drive: %s", WindowsErrorString());
		return FALSE;
	}
	return TRUE;
}

/*
 * Mount the volume identified by drive_guid to mountpoint drive_name.
 * If volume_name is already mounted, but with a different letter than the
 * one requested then drive_name is updated to use that letter.
 */
BOOL MountVolume(char* drive_name, char *volume_name)
{
	char mounted_guid[52];
	char mounted_letter[27] = { 0 };
	DWORD size;

	if ((drive_name == NULL) || (volume_name == NULL) || (drive_name[0] == '?') ||
		(strncmp(volume_name, groot_name, groot_len) == 0))
		return FALSE;

	// Windows may already have the volume mounted but under a different letter.
	// If that is the case, update drive_name to that letter.
	if ( (GetVolumePathNamesForVolumeNameA(volume_name, mounted_letter, sizeof(mounted_letter), &size))
	  && (size > 1) && (mounted_letter[0] != drive_name[0]) ) {
		uprintf("%s is already mounted as %C: instead of %C: - Will now use this target instead...",
			volume_name, mounted_letter[0], drive_name[0]);
		drive_name[0] = mounted_letter[0];
		return TRUE;
	}

	if (!SetVolumeMountPointA(drive_name, volume_name)) {
		if (GetLastError() == ERROR_DIR_NOT_EMPTY) {
			if (!GetVolumeNameForVolumeMountPointA(drive_name, mounted_guid, sizeof(mounted_guid))) {
				uprintf("%s is already mounted, but volume GUID could not be checked: %s",
					drive_name, WindowsErrorString());
			} else if (safe_strcmp(volume_name, mounted_guid) != 0) {
				uprintf("%s is mounted, but volume GUID doesn't match:\r\n  expected %s, got %s",
					drive_name, volume_name, mounted_guid);
			} else {
				uprintf("%s is already mounted as %C:", volume_name, drive_name[0]);
				return TRUE;
			}
			uprintf("Retrying after dismount...");
			if (!DeleteVolumeMountPointA(drive_name))
				uprintf("Warning: Could not delete volume mountpoint: %s", WindowsErrorString());
			if (SetVolumeMountPointA(drive_name, volume_name))
				return TRUE;
			if ((GetLastError() == ERROR_DIR_NOT_EMPTY) &&
				GetVolumeNameForVolumeMountPointA(drive_name, mounted_guid, sizeof(mounted_guid)) &&
				(safe_strcmp(volume_name, mounted_guid) == 0)) {
				uprintf("%s was remounted as %C: (second time lucky!)", volume_name, drive_name[0]);
				return TRUE;
			}
		}
		return FALSE;
	}
	return TRUE;
}

/*
 * Alternate version of MountVolume required for ESP's, since Windows (including VDS) does
 * *NOT* provide any means of mounting these volume but through DefineDosDevice(). Also
 * note that bcdboot is very finicky about what it may or may not handle, even if the
 * mount was successful (e.g. '\Device\HarddiskVolume###' vs 'Device\HarddiskVolume###').
 * Returned string is static (no concurrency) and must not be freed.
 */
char* AltMountVolume(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bSilent)
{
	char* ret = NULL, *volume_name = NULL;
	static char mounted_drive[] = "?:";

	mounted_drive[0] = GetUnusedDriveLetter();
	if (mounted_drive[0] == 0) {
		suprintf("Could not find an unused drive letter");
		goto out;
	}
	// Can't use a regular volume GUID for ESPs...
	volume_name = AltGetLogicalName(DriveIndex, PartitionOffset, FALSE, TRUE);
	if ((volume_name == NULL) || (strncmp(volume_name, groot_name, groot_len) != 0)) {
		suprintf("Unexpected volume name: '%s'", volume_name);
		goto out;
	}

	suprintf("Mounting '%s' as '%s'", &volume_name[14], mounted_drive);
	// bcdboot doesn't like it if you forget the starting '\'
	if (!DefineDosDeviceA(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, mounted_drive, &volume_name[14])) {
		suprintf("Mount operation failed: %s", WindowsErrorString());
		goto out;
	}
	ret = mounted_drive;

out:
	free(volume_name);
	return ret;
}

/*
 * Unmount a volume that was mounted by AltmountVolume()
 */
BOOL AltUnmountVolume(const char* drive_name, BOOL bSilent)
{
	if (drive_name == NULL)
		return FALSE;
	if (!DefineDosDeviceA(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive_name, NULL)) {
		suprintf("Could not unmount '%s': %s", drive_name, WindowsErrorString());
		return FALSE;
	}
	suprintf("Successfully unmounted '%s'", drive_name);
	return TRUE;
}

/*
 * Issue a complete remount of the volume.
 * Note that drive_name *may* be altered when the volume gets remounted.
 */
BOOL RemountVolume(char* drive_name)
{
	char volume_name[51];

	// UDF requires a sync/flush, and it's also a good idea for other FS's
	FlushDrive(drive_name[0]);
	if (GetVolumeNameForVolumeMountPointA(drive_name, volume_name, sizeof(volume_name))) {
		if (DeleteVolumeMountPointA(drive_name)) {
			Sleep(200);
			if (MountVolume(drive_name, volume_name)) {
				uprintf("Successfully remounted %s as %C:", volume_name, drive_name[0]);
			} else {
				uprintf("Failed to remount %s as %C:", volume_name, drive_name[0]);
				// This will leave the drive inaccessible and must be flagged as an error
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_REMOUNT_VOLUME);
				return FALSE;
			}
		} else {
			uprintf("Could not remount %s as %C: %s", volume_name, drive_name[0], WindowsErrorString());
			// Try to continue regardless
		}
	}
	return TRUE;
}

/*
 * Zero the first 'size' bytes of a partition. This is needed because we haven't found a way to
 * properly reset Windows's cached view of a drive partitioning short of cycling the USB port
 * (especially IOCTL_DISK_UPDATE_PROPERTIES is *USELESS*), and therefore the OS will try to
 * read the file system data at an old location, even if the partition has just been deleted.
 * TODO: We should do something like this is DeletePartitions() too.
 */
static BOOL ClearPartition(HANDLE hDrive, LARGE_INTEGER offset, DWORD size)
{
	BOOL r = FALSE;
	uint8_t* buffer = calloc(size, 1);

	if (buffer == NULL)
		return FALSE;

	if (!SetFilePointerEx(hDrive, offset, NULL, FILE_BEGIN)) {
		free(buffer);
		return FALSE;
	}

	r = WriteFileWithRetry(hDrive, buffer, size, &size, WRITE_RETRIES);
	free(buffer);
	return r;
}

/*
 * Create a partition table
 * See http://technet.microsoft.com/en-us/library/cc739412.aspx for some background info
 * NB: if you modify the MBR outside of using the Windows API, Windows still uses the cached
 * copy it got from the last IOCTL, and ignores your changes until you replug the drive
 * or issue an IOCTL_DISK_UPDATE_PROPERTIES.
 */
BOOL CreatePartition(HANDLE hDrive, int partition_style, int file_system, BOOL mbr_uefi_marker, uint8_t extra_partitions)
{
	const char* PartitionTypeName[] = { "MBR", "GPT", "SFD" };
	const wchar_t *extra_part_name = L"", *main_part_name = L"Main Data Partition";
	const LONGLONG bytes_per_track = ((LONGLONG)SelectedDrive.SectorsPerTrack) * SelectedDrive.SectorSize;
	const DWORD size_to_clear = MAX_SECTORS_TO_CLEAR * SelectedDrive.SectorSize;
	uint8_t* buffer;
	size_t uefi_ntfs_size = 0;
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};
	DRIVE_LAYOUT_INFORMATION_EX4 DriveLayoutEx = {0};
	BOOL r;
	DWORD i, size, bufsize, pn = 0;
	LONGLONG main_part_size_in_sectors, extra_part_size_in_tracks = 0, ms_esp_size;

	PrintInfoDebug(0, MSG_238, PartitionTypeName[partition_style]);

	if (partition_style == PARTITION_STYLE_SFD)
		// Nothing to do
		return TRUE;

	if (extra_partitions & XP_UEFI_NTFS) {
		uefi_ntfs_size = GetResourceSize(hMainInstance, MAKEINTRESOURCEA(IDR_UEFI_NTFS), _RT_RCDATA, "uefi-ntfs.img");
		if (uefi_ntfs_size == 0)
			return FALSE;
	}
	memset(partition_offset, 0, sizeof(partition_offset));
	memset(SelectedDrive.PartitionOffset, 0, sizeof(SelectedDrive.PartitionOffset));

	// Compute the start offset of our first partition
	if ((partition_style == PARTITION_STYLE_GPT) || (!IsChecked(IDC_OLD_BIOS_FIXES))) {
		// Go with the MS 1 MB wastage at the beginning...
		DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart = MB;
	} else {
		// Align on Cylinder
		DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart = bytes_per_track;
	}

	// If required, set the MSR partition (GPT only - must be created before the data part)
	if (extra_partitions & XP_MSR) {
		assert (partition_style == PARTITION_STYLE_GPT);
		extra_part_name = L"Microsoft Reserved Partition";
		DriveLayoutEx.PartitionEntry[pn].PartitionLength.QuadPart = 128*MB;
		DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionType = PARTITION_MSFT_RESERVED_GUID;
		uprintf("● Creating %S (offset: %lld, size: %s)", extra_part_name, DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart,
			SizeToHumanReadable(DriveLayoutEx.PartitionEntry[pn].PartitionLength.QuadPart, TRUE, FALSE));
		IGNORE_RETVAL(CoCreateGuid(&DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionId));
		wcsncpy(DriveLayoutEx.PartitionEntry[pn].Gpt.Name, extra_part_name, ARRAYSIZE(DriveLayoutEx.PartitionEntry[pn].Gpt.Name));
		// Zero the first sectors from this partition to avoid file system caching issues
		if (!ClearPartition(hDrive, DriveLayoutEx.PartitionEntry[pn].StartingOffset, size_to_clear))
			uprintf("Could not zero %S: %s", extra_part_name, WindowsErrorString());
		SelectedDrive.PartitionOffset[pn] = DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart;
		pn++;
		DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart = DriveLayoutEx.PartitionEntry[pn-1].StartingOffset.QuadPart +
				DriveLayoutEx.PartitionEntry[pn-1].PartitionLength.QuadPart;
		// Clear the extra partition we processed
		extra_partitions &= ~(XP_MSR);
	}

	// Set our main data partition
	main_part_size_in_sectors = (SelectedDrive.DiskSize - DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart) /
		// Need 33 sectors at the end for secondary GPT
		SelectedDrive.SectorSize - ((partition_style == PARTITION_STYLE_GPT)?33:0);
	if (extra_partitions) {
		// Adjust the size according to extra partitions (which we always align to a track)
		if (extra_partitions & XP_ESP) {
			extra_part_name = L"EFI System";
			// The size of the ESP depends on the minimum size we're able to format in FAT32, which
			// in turn depends on the cluster size used, which in turn depends on the disk sector size.
			// Plus some people are complaining that the *OFFICIAL MINIMUM SIZE* as documented by Microsoft at
			// https://docs.microsoft.com/en-us/windows-hardware/manufacture/desktop/configure-uefigpt-based-hard-drive-partitions
			// is too small. See: https://github.com/pbatard/rufus/issues/979
			if (SelectedDrive.SectorSize <= 4096)
				ms_esp_size = 300 * MB;
			else
				ms_esp_size = 1200 * MB;	// That'll teach you to have a nonstandard disk!
			extra_part_size_in_tracks = (ms_esp_size + bytes_per_track - 1) / bytes_per_track;
		} else if (extra_partitions & XP_UEFI_NTFS) {
			extra_part_name = L"UEFI:NTFS";
			extra_part_size_in_tracks = (max(MIN_EXTRA_PART_SIZE, uefi_ntfs_size) + bytes_per_track - 1) / bytes_per_track;
		} else if ((extra_partitions & XP_CASPER)) {
			assert(persistence_size != 0);
			extra_part_name = L"Linux Persistence";
			extra_part_size_in_tracks = persistence_size / bytes_per_track;
		} else if (extra_partitions & XP_COMPAT) {
			extra_part_name = L"BIOS Compatibility";
			extra_part_size_in_tracks = 1;	// One track for the extra partition
		} else {
			assert(FALSE);
		}
		main_part_size_in_sectors = ((main_part_size_in_sectors / SelectedDrive.SectorsPerTrack) -
			extra_part_size_in_tracks) * SelectedDrive.SectorsPerTrack;
	}
	if (main_part_size_in_sectors <= 0) {
		uprintf("Error: Invalid %S size", main_part_name);
		return FALSE;
	}
	uprintf("● Creating %S (offset: %lld, size: %s)", main_part_name, DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart,
		SizeToHumanReadable(main_part_size_in_sectors * SelectedDrive.SectorSize, TRUE, FALSE));
	// Zero the beginning of this partition to avoid conflicting leftovers
	if (!ClearPartition(hDrive, DriveLayoutEx.PartitionEntry[pn].StartingOffset, size_to_clear))
		uprintf("Could not zero %S: %s", main_part_name, WindowsErrorString());

	DriveLayoutEx.PartitionEntry[pn].PartitionLength.QuadPart = main_part_size_in_sectors * SelectedDrive.SectorSize;
	if (partition_style == PARTITION_STYLE_MBR) {
		DriveLayoutEx.PartitionEntry[pn].Mbr.BootIndicator = (boot_type != BT_NON_BOOTABLE);
		switch (file_system) {
		case FS_FAT16:
			DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0x0e;	// FAT16 LBA
			break;
		case FS_NTFS:
		case FS_EXFAT:
		case FS_UDF:
		case FS_REFS:
			DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0x07;
			break;
		case FS_EXT2:
		case FS_EXT3:
		case FS_EXT4:
			DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0x83;
			break;
		case FS_FAT32:
			DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0x0c;	// FAT32 LBA
			break;
		default:
			uprintf("Unsupported file system");
			return FALSE;
		}
	} else {
		DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionType = PARTITION_BASIC_DATA_GUID;
		IGNORE_RETVAL(CoCreateGuid(&DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionId));
		wcsncpy(DriveLayoutEx.PartitionEntry[pn].Gpt.Name, main_part_name, ARRAYSIZE(DriveLayoutEx.PartitionEntry[pn].Gpt.Name));
	}
	SelectedDrive.PartitionOffset[pn] = DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart;
	partition_offset[PI_MAIN] = SelectedDrive.PartitionOffset[pn];
	pn++;

	// Set the optional extra partition
	if (extra_partitions) {
		// Should end on a track boundary
		DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart = DriveLayoutEx.PartitionEntry[pn-1].StartingOffset.QuadPart +
			DriveLayoutEx.PartitionEntry[pn-1].PartitionLength.QuadPart;
		DriveLayoutEx.PartitionEntry[pn].PartitionLength.QuadPart = (extra_partitions & XP_UEFI_NTFS)?uefi_ntfs_size:
			extra_part_size_in_tracks * bytes_per_track;
		uprintf("● Creating %S Partition (offset: %lld, size: %s)", extra_part_name, DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart,
			SizeToHumanReadable(DriveLayoutEx.PartitionEntry[pn].PartitionLength.QuadPart, TRUE, FALSE));
		SelectedDrive.PartitionOffset[pn] = DriveLayoutEx.PartitionEntry[pn].StartingOffset.QuadPart;
		if (extra_partitions & XP_CASPER)
			partition_offset[PI_CASPER] = SelectedDrive.PartitionOffset[pn];
		else if (extra_partitions & XP_ESP)
			partition_offset[PI_ESP] = SelectedDrive.PartitionOffset[pn];

		if (partition_style == PARTITION_STYLE_GPT) {
			DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionType = (extra_partitions & XP_ESP) ? PARTITION_SYSTEM_GUID : PARTITION_BASIC_DATA_GUID;
			IGNORE_RETVAL(CoCreateGuid(&DriveLayoutEx.PartitionEntry[pn].Gpt.PartitionId));
			wcsncpy(DriveLayoutEx.PartitionEntry[pn].Gpt.Name, (extra_partitions & XP_ESP) ? L"EFI System Partition" : extra_part_name,
				ARRAYSIZE(DriveLayoutEx.PartitionEntry[pn].Gpt.Name));
		} else {
			if (extra_partitions & (XP_UEFI_NTFS | XP_ESP)) {
				DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0xef;
			} else if (extra_partitions & XP_CASPER) {
				DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = 0x83;
			}  else if (extra_partitions & XP_COMPAT) {
				DriveLayoutEx.PartitionEntry[pn].Mbr.PartitionType = RUFUS_EXTRA_PARTITION_TYPE;
				// Set the one track compatibility partition to be all hidden sectors
				DriveLayoutEx.PartitionEntry[pn].Mbr.HiddenSectors = SelectedDrive.SectorsPerTrack;
			} else {
				assert(FALSE);
			}
		}

		// We need to write the UEFI:NTFS partition before we refresh the disk
		if (extra_partitions & XP_UEFI_NTFS) {
			uprintf("Writing %S data...", extra_part_name);
			if (!SetFilePointerEx(hDrive, DriveLayoutEx.PartitionEntry[pn].StartingOffset, NULL, FILE_BEGIN)) {
				uprintf("Could not set position");
				return FALSE;
			}
			buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_UEFI_NTFS), _RT_RCDATA, "uefi-ntfs.img", &bufsize, FALSE);
			if (buffer == NULL) {
				uprintf("Could not access source image");
				return FALSE;
			}
			if(!WriteFileWithRetry(hDrive, buffer, bufsize, &size, WRITE_RETRIES)) {
				uprintf("Write error: %s", WindowsErrorString());
				return FALSE;
			}
			installed_uefi_ntfs = TRUE;
		}
		pn++;
	}

	// Initialize the remaining partition data
	for (i = 0; i < pn; i++) {
		DriveLayoutEx.PartitionEntry[i].PartitionNumber = i + 1;
		DriveLayoutEx.PartitionEntry[i].PartitionStyle = partition_style;
		DriveLayoutEx.PartitionEntry[i].RewritePartition = TRUE;
	}

	switch (partition_style) {
	case PARTITION_STYLE_MBR:
		CreateDisk.PartitionStyle = PARTITION_STYLE_MBR;
		// If MBR+UEFI is selected, write an UEFI marker in lieu of the regular MBR signature.
		// This helps us reselect the partition scheme option that was used when creating the
		// drive in Rufus. As far as I can tell, Windows doesn't care much if this signature
		// isn't unique for USB drives.
		CreateDisk.Mbr.Signature = mbr_uefi_marker?MBR_UEFI_MARKER:(DWORD)GetTickCount64();

		DriveLayoutEx.PartitionStyle = PARTITION_STYLE_MBR;
		DriveLayoutEx.PartitionCount = 4;	// Must be multiple of 4 for MBR
		DriveLayoutEx.Type.Mbr.Signature = CreateDisk.Mbr.Signature;
		// TODO: CHS fixup (32 sectors/track) through a cheat mode, if requested
		// NB: disk geometry is computed by BIOS & co. by finding a match between LBA and CHS value of first partition
		//     ms-sys's write_partition_number_of_heads() and write_partition_start_sector_number() can be used if needed
		break;
	case PARTITION_STYLE_GPT:
		// TODO: (HOW?!?!?) As per MSDN: "When specifying a GUID partition table (GPT) as the PARTITION_STYLE of the CREATE_DISK
		// structure, an application should wait for the MSR partition arrival before sending the IOCTL_DISK_SET_DRIVE_LAYOUT_EX
		// control code. For more information about device notification, see RegisterDeviceNotification."

		CreateDisk.PartitionStyle = PARTITION_STYLE_GPT;
		IGNORE_RETVAL(CoCreateGuid(&CreateDisk.Gpt.DiskId));
		CreateDisk.Gpt.MaxPartitionCount = MAX_PARTITIONS;

		DriveLayoutEx.PartitionStyle = PARTITION_STYLE_GPT;
		DriveLayoutEx.PartitionCount = pn;
		// At the very least, a GPT disk has 34 reserved sectors at the beginning and 33 at the end.
		DriveLayoutEx.Type.Gpt.StartingUsableOffset.QuadPart = 34 * SelectedDrive.SectorSize;
		DriveLayoutEx.Type.Gpt.UsableLength.QuadPart = SelectedDrive.DiskSize - (34+33) * SelectedDrive.SectorSize;
		DriveLayoutEx.Type.Gpt.MaxPartitionCount = MAX_PARTITIONS;
		DriveLayoutEx.Type.Gpt.DiskId = CreateDisk.Gpt.DiskId;
		break;
	}

	// If you don't call IOCTL_DISK_CREATE_DISK, the IOCTL_DISK_SET_DRIVE_LAYOUT_EX call will fail
	size = sizeof(CreateDisk);
	r = DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK, (BYTE*)&CreateDisk, size, NULL, 0, &size, NULL);
	if (!r) {
		uprintf("Could not reset disk: %s", WindowsErrorString());
		return FALSE;
	}

	// "The goggles, they do nothing!"
	RefreshDriveLayout(hDrive);

	size = sizeof(DriveLayoutEx) - ((partition_style == PARTITION_STYLE_GPT)?((4-pn)*sizeof(PARTITION_INFORMATION_EX)):0);
	r = DeviceIoControl(hDrive, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, (BYTE*)&DriveLayoutEx, size, NULL, 0, &size, NULL);
	if (!r) {
		uprintf("Could not set drive layout: %s", WindowsErrorString());
		return FALSE;
	}

	if (!RefreshDriveLayout(hDrive))
		return FALSE;

	return TRUE;
}

BOOL RefreshDriveLayout(HANDLE hDrive)
{
	BOOL r;
	DWORD size;

	// Diskpart does call the following IOCTL this after updating the partition table, so we do too
	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL );
	if (!r)
		uprintf("Could not refresh drive layout: %s", WindowsErrorString());
	return r;
}

/* Initialize disk for partitioning */
BOOL InitializeDisk(HANDLE hDrive)
{
	BOOL r;
	DWORD size;
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};

	PrintInfoDebug(0, MSG_239);

	size = sizeof(CreateDisk);
	r = DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK,
			(BYTE*)&CreateDisk, size, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("Could not delete drive layout: %s", WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}

	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &size, NULL );
	if (!r) {
		uprintf("Could not refresh drive layout: %s", WindowsErrorString());
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
