/*
 * Rufus: The Reliable USB Formatting Utility
 * Drive access function calls
 * Copyright © 2011-2024 Pete Batard <pete@akeo.ie>
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
#include <ctype.h>
#include <assert.h>
#if !defined(__MINGW32__)
#include <initguid.h>
#include <vds.h>
#endif

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "settings.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "file.h"
#include "drive.h"
#include "mbr_types.h"
#include "gpt_types.h"
#include "br.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"

#define GLOBALROOT_NAME "\\\\?\\GLOBALROOT"
const char* sfd_name = "Super Floppy Disk";
const char* groot_name = GLOBALROOT_NAME;
const size_t groot_len = sizeof(GLOBALROOT_NAME) - 1;


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
extern BOOL write_as_esp;
extern windows_version_t WindowsVersion;
int partition_index[PI_MAX];
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
	BOOL ret = FALSE;

	hMountMgr = CreateFileA(MOUNTMGR_DOS_DEVICE_NAME, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hMountMgr == INVALID_HANDLE_VALUE)
		return FALSE;
	ret = DeviceIoControl(hMountMgr, IOCTL_MOUNTMGR_SET_AUTO_MOUNT, &enable, sizeof(enable), NULL, 0, NULL, NULL);
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
	hMountMgr = CreateFileA(MOUNTMGR_DOS_DEVICE_NAME, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hMountMgr == INVALID_HANDLE_VALUE)
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
			uprintf("Notice: Volume Device Path is %s", DevPath);
			uprintf("Waiting for access on %s...", Path);
		} else if (!bWriteShare && (i > DRIVE_ACCESS_RETRIES/3)) {
			// If we can't seem to get a hold of the drive for some time, try to enable FILE_SHARE_WRITE...
			uprintf("Warning: Could not obtain exclusive rights. Retrying with write sharing enabled...");
			bWriteShare = TRUE;
			// Try to report the process that is locking the drive
			access_mask = GetProcessSearch(SEARCH_PROCESS_TIMEOUT, 0x07, FALSE);
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
		if (DeviceIoControl(hDrive, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, NULL, NULL)) {
			uprintf("I/O boundary checks disabled");
		}

		EndTime = GetTickCount64() + DRIVE_ACCESS_TIMEOUT;
		do {
			if (DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, NULL, NULL))
				goto out;
			if (IS_ERROR(ErrorStatus))	// User cancel
				break;
			Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
		} while (GetTickCount64() < EndTime);
		// If we reached this section, either we didn't manage to get a lock or the user cancelled
		uprintf("Could not lock access to %s: %s", Path, WindowsErrorString());
		// See if we can report the processes are accessing the drive
		if (!IS_ERROR(ErrorStatus) && (access_mask == 0))
			access_mask = GetProcessSearch(SEARCH_PROCESS_TIMEOUT, 0x07, FALSE);
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
	BOOL r, bPrintHeader = TRUE;
	HANDLE hDrive = INVALID_HANDLE_VALUE, hVolume = INVALID_HANDLE_VALUE;
	VOLUME_DISK_EXTENTS_REDEF DiskExtents;
	DWORD size = 0;
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

		hDrive = CreateFileWithTimeout(volume_name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL, 3000);
		if (hDrive == INVALID_HANDLE_VALUE) {
			suprintf("Could not open GUID volume '%s': %s", volume_name, WindowsErrorString());
			continue;
		}

		r = DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &DiskExtents, sizeof(DiskExtents), &size, NULL);
		if ((!r) || (size == 0)) {
			suprintf("Could not get Disk Extents: %s", r ? "(empty data)" : WindowsErrorString());
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

	if (i < found_name.Index) {
		ret = safe_strdup(found_name.String[i]);
	} else {
		// NB: We need to re-add DRIVE_INDEX_MIN for this call since CheckDriveIndex() subtracted it
		ret = AltGetLogicalName(DriveIndex + DRIVE_INDEX_MIN, PartitionOffset, bKeepTrailingBackslash, bSilent);
		if ((ret != NULL) && (strchr(ret, ' ') != NULL))
			uprintf("Warning: Using physical device to access partition data");
	}

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
	BOOL matching_drive = (DriveIndex == SelectedDrive.DeviceNumber);
	DWORD i;
	char *ret = NULL, volume_name[MAX_PATH], path[64];

	CheckDriveIndex(DriveIndex);

	// Match the offset to a partition index
	if (PartitionOffset == 0) {
		i = 0;
	} else if (matching_drive) {
		for (i = 0; (i < MAX_PARTITIONS) && (PartitionOffset != SelectedDrive.Partition[i].Offset); i++);
		if (i >= MAX_PARTITIONS) {
			suprintf("Error: Could not find a partition at offset %lld on this disk", PartitionOffset);
			goto out;
		}
	} else {
		suprintf("Error: Searching for a partition on a non matching disk");
		goto out;
	}
	static_sprintf(path, "Harddisk%luPartition%lu", DriveIndex, i + 1);
	static_strcpy(volume_name, groot_name);
	if (!QueryDosDeviceA(path, &volume_name[groot_len], (DWORD)(MAX_PATH - groot_len)) || (strlen(volume_name) < 20)) {
		suprintf("Could not find a DOS volume name for '%s': %s", path, WindowsErrorString());
		goto out;
	} else if (bKeepTrailingBackslash) {
		static_strcat(volume_name, "\\");
	}
	ret = safe_strdup(volume_name);

out:
	return ret;
}

/*
 * Custom volume name for extfs formatting (that includes partition offset and partition size)
 * so that these can be created and accessed on pre 1703 versions of Windows.
 */
char* GetExtPartitionName(DWORD DriveIndex, uint64_t PartitionOffset)
{
	DWORD i;
	char* ret = NULL, volume_name[MAX_PATH];

	// Can't operate if we're not on the selected drive
	if (DriveIndex != SelectedDrive.DeviceNumber)
		goto out;
	CheckDriveIndex(DriveIndex);
	for (i = 0; (i < MAX_PARTITIONS) && (PartitionOffset != SelectedDrive.Partition[i].Offset); i++);
	if (i >= MAX_PARTITIONS)
		goto out;
	static_sprintf(volume_name, "\\\\.\\PhysicalDrive%lu %I64u %I64u", DriveIndex,
		SelectedDrive.Partition[i].Offset, SelectedDrive.Partition[i].Size);
	ret = safe_strdup(volume_name);
out:
	return ret;
}

static const char* VdsErrorString(HRESULT hr) {
	SetLastError(hr);
	return WindowsErrorString();
}

/*
 * Per https://docs.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateinstance
 * and even though we aren't a UWP app, Windows Store prevents the ability to use of VDS when the
 * Store version of Rufus is running (the call to IVdsServiceLoader_LoadService() will return
 * E_ACCESSDENIED).
 */
BOOL IsVdsAvailable(BOOL bSilent)
{
	HRESULT hr = S_FALSE;
	IVdsService* pService = NULL;
	IVdsServiceLoader* pLoader = NULL;

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void**)&pLoader);
	if (hr != S_OK) {
		suprintf("Notice: Disabling VDS (Could not create VDS Loader Instance: %s)", VdsErrorString(hr));
		goto out;
	}

	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	if (hr != S_OK) {
		suprintf("Notice: Disabling VDS (Could not load VDS Service: %s)", VdsErrorString(hr));
		goto out;
	}

out:
	if (pService != NULL)
		IVdsService_Release(pService);
	if (pLoader != NULL)
		IVdsServiceLoader_Release(pLoader);
	VDS_SET_ERROR(hr);
	return (hr == S_OK);
}

/*
 * Call on VDS to refresh the drive layout
 */
BOOL RefreshLayout(DWORD DriveIndex)
{
	HRESULT hr = S_FALSE;
	wchar_t wPhysicalName[24];
	IVdsServiceLoader* pLoader = NULL;
	IVdsService* pService = NULL;
	IEnumVdsObject *pEnum;

	CheckDriveIndex(DriveIndex);
	wnsprintf(wPhysicalName, ARRAYSIZE(wPhysicalName), L"\\\\?\\PhysicalDrive%lu", DriveIndex);

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void **)&pLoader);
	if (hr != S_OK) {
		uprintf("Could not create VDS Loader Instance: %s", VdsErrorString(hr));
		goto out;
	}

	// Load the VDS Service
	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	if (hr != S_OK) {
		uprintf("Could not load VDS Service: %s", VdsErrorString(hr));
		goto out;
	}

	// Wait for the Service to become ready if needed
	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		uprintf("VDS Service is not ready: %s", VdsErrorString(hr));
		goto out;
	}

	// Query the VDS Service Providers
	hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
	if (hr != S_OK) {
		uprintf("Could not query VDS Service Providers: %s", VdsErrorString(hr));
		goto out;
	}

	// Remove mountpoints
	hr = IVdsService_CleanupObsoleteMountPoints(pService);
	if (hr != S_OK) {
		uprintf("Could not clean up VDS mountpoints: %s", VdsErrorString(hr));
		goto out;
	}

	// Invoke layout refresh
	hr = IVdsService_Refresh(pService);
	if (hr != S_OK) {
		uprintf("Could not refresh VDS layout: %s", VdsErrorString(hr));
		goto out;
	}

	// Force re-enum
	hr = IVdsService_Reenumerate(pService);
	if (hr != S_OK) {
		uprintf("Could not refresh VDS layout: %s", VdsErrorString(hr));
		goto out;
	}

out:
	if (pService != NULL)
		IVdsService_Release(pService);
	if (pLoader != NULL)
		IVdsServiceLoader_Release(pLoader);
	VDS_SET_ERROR(hr);
	return (hr == S_OK);
}

/*
 * Generic call to instantiate a VDS Disk Interface. Mostly copied from:
 * https://social.msdn.microsoft.com/Forums/vstudio/en-US/b90482ae-4e44-4b08-8731-81915030b32a/createpartition-using-vds-interface-throw-error-enointerface-dcom?forum=vcgeneral
 * See also: https://docs.microsoft.com/en-us/windows/win32/vds/working-with-enumeration-objects
 */
static BOOL GetVdsDiskInterface(DWORD DriveIndex, const IID* InterfaceIID, void** pInterfaceInstance, BOOL bSilent)
{
	HRESULT hr = S_FALSE;
	ULONG ulFetched;
	wchar_t wPhysicalName[24];
	IVdsServiceLoader* pLoader;
	IVdsService* pService;
	IEnumVdsObject* pEnum;
	IUnknown* pUnk;

	*pInterfaceInstance = NULL;
	CheckDriveIndex(DriveIndex);
	wnsprintf(wPhysicalName, ARRAYSIZE(wPhysicalName), L"\\\\?\\PhysicalDrive%lu", DriveIndex);

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void**)&pLoader);
	if (hr != S_OK) {
		suprintf("Could not create VDS Loader Instance: %s", VdsErrorString(hr));
		goto out;
	}

	// Load the VDS Service
	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	IVdsServiceLoader_Release(pLoader);
	if (hr != S_OK) {
		suprintf("Could not load VDS Service: %s", VdsErrorString(hr));
		goto out;
	}

	// Wait for the Service to become ready if needed
	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		suprintf("VDS Service is not ready: %s", VdsErrorString(hr));
		goto out;
	}

	// Query the VDS Service Providers
	hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
	IVdsService_Release(pService);
	if (hr != S_OK) {
		suprintf("Could not query VDS Service Providers: %s", VdsErrorString(hr));
		goto out;
	}

	while (IEnumVdsObject_Next(pEnum, 1, &pUnk, &ulFetched) == S_OK) {
		IVdsProvider* pProvider;
		IVdsSwProvider* pSwProvider;
		IEnumVdsObject* pEnumPack;
		IUnknown* pPackUnk;

		// Get VDS Provider
		hr = IUnknown_QueryInterface(pUnk, &IID_IVdsProvider, (void**)&pProvider);
		IUnknown_Release(pUnk);
		if (hr != S_OK) {
			suprintf("Could not get VDS Provider: %s", VdsErrorString(hr));
			break;
		}

		// Get VDS Software Provider
		hr = IVdsSwProvider_QueryInterface(pProvider, &IID_IVdsSwProvider, (void**)&pSwProvider);
		IVdsProvider_Release(pProvider);
		if (hr != S_OK) {
			suprintf("Could not get VDS Software Provider: %s", VdsErrorString(hr));
			break;
		}

		// Get VDS Software Provider Packs
		hr = IVdsSwProvider_QueryPacks(pSwProvider, &pEnumPack);
		IVdsSwProvider_Release(pSwProvider);
		if (hr != S_OK) {
			suprintf("Could not get VDS Software Provider Packs: %s", VdsErrorString(hr));
			break;
		}

		// Enumerate Provider Packs
		while (IEnumVdsObject_Next(pEnumPack, 1, &pPackUnk, &ulFetched) == S_OK) {
			IVdsPack* pPack;
			IEnumVdsObject* pEnumDisk;
			IUnknown* pDiskUnk;

			hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void**)&pPack);
			IUnknown_Release(pPackUnk);
			if (hr != S_OK) {
				suprintf("Could not query VDS Software Provider Pack: %s", VdsErrorString(hr));
				break;
			}

			// Use the pack interface to access the disks
			hr = IVdsPack_QueryDisks(pPack, &pEnumDisk);
			IVdsPack_Release(pPack);
			if (hr != S_OK) {
				suprintf("Could not query VDS disks: %s", VdsErrorString(hr));
				break;
			}

			// List disks
			while (IEnumVdsObject_Next(pEnumDisk, 1, &pDiskUnk, &ulFetched) == S_OK) {
				VDS_DISK_PROP prop;
				IVdsDisk* pDisk;

				// Get the disk interface.
				hr = IUnknown_QueryInterface(pDiskUnk, &IID_IVdsDisk, (void**)&pDisk);
				IUnknown_Release(pDiskUnk);
				if (hr != S_OK) {
					suprintf("Could not query VDS Disk Interface: %s", VdsErrorString(hr));
					break;
				}

				// Get the disk properties
				hr = IVdsDisk_GetProperties(pDisk, &prop);
				if ((hr != S_OK) && (hr != VDS_S_PROPERTIES_INCOMPLETE)) {
					IVdsDisk_Release(pDisk);
					suprintf("Could not query VDS Disk Properties: %s", VdsErrorString(hr));
					break;
				}

				// Check if we are on the target disk
				// uprintf("GetVdsDiskInterface: Seeking %S found %S", wPhysicalName, prop.pwszName);
				hr = (HRESULT)_wcsicmp(wPhysicalName, prop.pwszName);
				CoTaskMemFree(prop.pwszName);
				if (hr != S_OK) {
					hr = S_OK;
					continue;
				}

				// Instantiate the requested VDS disk interface
				hr = IVdsDisk_QueryInterface(pDisk, InterfaceIID, pInterfaceInstance);
				IVdsDisk_Release(pDisk);
				if (hr != S_OK)
					suprintf("Could not access the requested Disk interface: %s", VdsErrorString(hr));

				// With the interface found, we should be able to return
				break;
			}
			IEnumVdsObject_Release(pEnumDisk);
		}
		IEnumVdsObject_Release(pEnumPack);
	}
	IEnumVdsObject_Release(pEnum);

out:
	VDS_SET_ERROR(hr);
	return (hr == S_OK);
}

/*
 * Invoke IVdsService::Refresh() and/or IVdsService::Reenumerate() to force a
 * rescan of the VDS disks. This can become necessary after writing an image
 * such as Ubuntu 20.10, as Windows may "lose" the active disk otherwise...
 */
BOOL VdsRescan(DWORD dwRescanType, DWORD dwSleepTime, BOOL bSilent)
{
	BOOL ret = TRUE;
	HRESULT hr = S_FALSE;
	IVdsServiceLoader* pLoader;
	IVdsService* pService;

	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void**)&pLoader);
	if (hr != S_OK) {
		suprintf("Could not create VDS Loader Instance: %s", VdsErrorString(hr));
		return FALSE;
	}

	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	IVdsServiceLoader_Release(pLoader);
	if (hr != S_OK) {
		suprintf("Could not load VDS Service: %s", VdsErrorString(hr));
		return FALSE;
	}

	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		suprintf("VDS Service is not ready: %s", VdsErrorString(hr));
		return FALSE;
	}

	// https://docs.microsoft.com/en-us/windows/win32/api/vds/nf-vds-ivdsservice-refresh
	// This method synchronizes the disk layout to the layout known to the disk driver.
	// It does not force the driver to read the layout from the disk.
	// Additionally, this method refreshes the view of all objects in the VDS cache.
	if (dwRescanType & VDS_RESCAN_REFRESH) {
		hr = IVdsService_Refresh(pService);
		if (hr != S_OK) {
			suprintf("VDS Refresh failed: %s", VdsErrorString(hr));
			ret = FALSE;
		}
	}

	// https://docs.microsoft.com/en-us/windows/win32/api/vds/nf-vds-ivdsservice-reenumerate
	// This method returns immediately after a bus rescan request is issued.
	// The operation might be incomplete when the method returns.
	if (dwRescanType & VDS_RESCAN_REENUMERATE) {
		hr = IVdsService_Reenumerate(pService);
		if (hr != S_OK) {
			suprintf("VDS Re-enumeration failed: %s", VdsErrorString(hr));
			ret = FALSE;
		}
	}

	if (dwSleepTime != 0)
		Sleep(dwSleepTime);

	return ret;
}

/*
 * Delete one partition at offset PartitionOffset, or all partitions if the offset is 0.
 */
BOOL DeletePartition(DWORD DriveIndex, ULONGLONG PartitionOffset, BOOL bSilent)
{
	HRESULT hr = S_FALSE;
	VDS_PARTITION_PROP* prop_array = NULL;
	LONG i, prop_array_size;
	IVdsAdvancedDisk *pAdvancedDisk = NULL;

	if (!GetVdsDiskInterface(DriveIndex, &IID_IVdsAdvancedDisk, (void**)&pAdvancedDisk, bSilent))
		return FALSE;
	if (pAdvancedDisk == NULL) {
		suprintf("Looks like Windows has \"lost\" our disk - Forcing a VDS rescan...");
		VdsRescan(VDS_RESCAN_REFRESH | VDS_RESCAN_REENUMERATE, 1000, bSilent);
		if (!GetVdsDiskInterface(DriveIndex, &IID_IVdsAdvancedDisk, (void**)&pAdvancedDisk, bSilent) ||
			(pAdvancedDisk == NULL)) {
			suprintf("Could not locate disk - Aborting.");
			return FALSE;
		}
	}

	// Query the partition data, so we can get the start offset, which we need for deletion
	hr = IVdsAdvancedDisk_QueryPartitions(pAdvancedDisk, &prop_array, &prop_array_size);
	if (hr == S_OK) {
		suprintf("Deleting partition%s:", (PartitionOffset == 0) ? "s" : "");
		// Now go through each partition
		for (i = 0; i < prop_array_size; i++) {
			if ((PartitionOffset != 0) && (prop_array[i].ullOffset != PartitionOffset))
				continue;
			suprintf("● Partition %d (offset: %lld, size: %s)", prop_array[i].ulPartitionNumber,
				prop_array[i].ullOffset, SizeToHumanReadable(prop_array[i].ullSize, FALSE, FALSE));
			hr = IVdsAdvancedDisk_DeletePartition(pAdvancedDisk, prop_array[i].ullOffset, TRUE, TRUE);
			if (hr != S_OK)
				suprintf("Could not delete partition: %s", VdsErrorString(hr));
		}
	} else {
		suprintf("No partition to delete on disk");
		hr = S_OK;
	}
	CoTaskMemFree(prop_array);
	IVdsAdvancedDisk_Release(pAdvancedDisk);
	VDS_SET_ERROR(hr);
	return (hr == S_OK);
}

/*
 * Count on Microsoft for *COMPLETELY CRIPPLING* an API when allegedly upgrading it...
 * As illustrated when you do so with diskpart (which uses VDS behind the scenes), VDS
 * simply *DOES NOT* list all the volumes that the system can see, especially compared
 * to what mountvol (which uses FindFirstVolume()/FindNextVolume()) and other APIs do.
 * Also for reference, if you want to list volumes through WMI in PowerShell:
 * Get-WmiObject win32_volume | Format-Table -Property DeviceID,Name,Label,Capacity
 */
BOOL ListVdsVolumes(BOOL bSilent)
{
	HRESULT hr = S_FALSE;
	ULONG ulFetched;
	IVdsServiceLoader* pLoader;
	IVdsService* pService;
	IEnumVdsObject* pEnum;
	IUnknown* pUnk;

	// Initialize COM
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
	IGNORE_RETVAL(CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
		RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL));

	// Create a VDS Loader Instance
	hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
		&IID_IVdsServiceLoader, (void**)&pLoader);
	if (hr != S_OK) {
		suprintf("Could not create VDS Loader Instance: %s", VdsErrorString(hr));
		goto out;
	}

	// Load the VDS Service
	hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
	IVdsServiceLoader_Release(pLoader);
	if (hr != S_OK) {
		suprintf("Could not load VDS Service: %s", VdsErrorString(hr));
		goto out;
	}

	// Wait for the Service to become ready if needed
	hr = IVdsService_WaitForServiceReady(pService);
	if (hr != S_OK) {
		suprintf("VDS Service is not ready: %s", VdsErrorString(hr));
		goto out;
	}

	// Query the VDS Service Providers
	hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
	IVdsService_Release(pService);
	if (hr != S_OK) {
		suprintf("Could not query VDS Service Providers: %s", VdsErrorString(hr));
		goto out;
	}

	while (IEnumVdsObject_Next(pEnum, 1, &pUnk, &ulFetched) == S_OK) {
		IVdsProvider* pProvider;
		IVdsSwProvider* pSwProvider;
		IEnumVdsObject* pEnumPack;
		IUnknown* pPackUnk;

		// Get VDS Provider
		hr = IUnknown_QueryInterface(pUnk, &IID_IVdsProvider, (void**)&pProvider);
		IUnknown_Release(pUnk);
		if (hr != S_OK) {
			suprintf("Could not get VDS Provider: %s", VdsErrorString(hr));
			break;
		}

		// Get VDS Software Provider
		hr = IVdsSwProvider_QueryInterface(pProvider, &IID_IVdsSwProvider, (void**)&pSwProvider);
		IVdsProvider_Release(pProvider);
		if (hr != S_OK) {
			suprintf("Could not get VDS Software Provider: %s", VdsErrorString(hr));
			break;
		}

		// Get VDS Software Provider Packs
		hr = IVdsSwProvider_QueryPacks(pSwProvider, &pEnumPack);
		IVdsSwProvider_Release(pSwProvider);
		if (hr != S_OK) {
			suprintf("Could not get VDS Software Provider Packs: %s", VdsErrorString(hr));
			break;
		}

		// Enumerate Provider Packs
		while (IEnumVdsObject_Next(pEnumPack, 1, &pPackUnk, &ulFetched) == S_OK) {
			IVdsPack* pPack;
			IEnumVdsObject* pEnumVolume;
			IUnknown* pVolumeUnk;

			hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void**)&pPack);
			IUnknown_Release(pPackUnk);
			if (hr != S_OK) {
				suprintf("Could not query VDS Software Provider Pack: %s", VdsErrorString(hr));
				break;
			}

			// Use the pack interface to access the disks
			hr = IVdsPack_QueryVolumes(pPack, &pEnumVolume);
			if (hr != S_OK) {
				suprintf("Could not query VDS volumes: %s", VdsErrorString(hr));
				break;
			}

			// List volumes
			while (IEnumVdsObject_Next(pEnumVolume, 1, &pVolumeUnk, &ulFetched) == S_OK) {
				IVdsVolume* pVolume;
				IVdsVolumeMF3* pVolumeMF3;
				VDS_VOLUME_PROP prop;
				LPWSTR* wszPathArray;
				ULONG i, ulNumberOfPaths;

				// Get the volume interface.
				hr = IUnknown_QueryInterface(pVolumeUnk, &IID_IVdsVolume, (void**)&pVolume);
				if (hr != S_OK) {
					suprintf("Could not query VDS Volume Interface: %s", VdsErrorString(hr));
					break;
				}

				// Get the volume properties
				hr = IVdsVolume_GetProperties(pVolume, &prop);
				if ((hr != S_OK) && (hr != VDS_S_PROPERTIES_INCOMPLETE)) {
					suprintf("Could not query VDS Volume Properties: %s", VdsErrorString(hr));
					break;
				}

				uprintf("FOUND VOLUME: '%S'", prop.pwszName);
				CoTaskMemFree(prop.pwszName);
				IVdsVolume_Release(pVolume);

				// Get the volume MF3 interface.
				hr = IUnknown_QueryInterface(pVolumeUnk, &IID_IVdsVolumeMF3, (void**)&pVolumeMF3);
				if (hr != S_OK) {
					suprintf("Could not query VDS VolumeMF3 Interface: %s", VdsErrorString(hr));
					break;
				}

				// Get the volume properties
				hr = IVdsVolumeMF3_QueryVolumeGuidPathnames(pVolumeMF3, &wszPathArray, &ulNumberOfPaths);
				if ((hr != S_OK) && (hr != VDS_S_PROPERTIES_INCOMPLETE)) {
					suprintf("Could not query VDS VolumeMF3 GUID PathNames: %s", VdsErrorString(hr));
					break;
				}
				hr = S_OK;

				for (i = 0; i < ulNumberOfPaths; i++)
					uprintf("  VOL GUID: '%S'", wszPathArray[i]);
				CoTaskMemFree(wszPathArray);
				IVdsVolume_Release(pVolumeMF3);
				IUnknown_Release(pVolumeUnk);
			}
			IEnumVdsObject_Release(pEnumVolume);
		}
		IEnumVdsObject_Release(pEnumPack);
	}
	IEnumVdsObject_Release(pEnum);

out:
	VDS_SET_ERROR(hr);
	return (hr == S_OK);
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
		if (IS_ERROR(ErrorStatus))	// User cancel
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

/* Alternate version of the above, for ESPs */
HANDLE AltGetLogicalHandle(DWORD DriveIndex, uint64_t PartitionOffset, BOOL bLockDrive, BOOL bWriteAccess, BOOL bWriteShare)
{
	HANDLE hLogical = INVALID_HANDLE_VALUE;
	char* LogicalPath = AltGetLogicalName(DriveIndex, PartitionOffset, FALSE, FALSE);

	if (LogicalPath == NULL) {
		uprintf("No logical drive found");
		return NULL;
	}

	hLogical = GetHandle(LogicalPath, bLockDrive, bWriteAccess, bWriteShare);
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
	DWORD size = 0;
	BOOL s;
	int r = -1;

	if (!DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &DiskExtents, sizeof(DiskExtents), &size, NULL) ||
		(size <= 0) || (DiskExtents.NumberOfDiskExtents < 1)) {
		// DiskExtents are NO_GO (which is the case for external USB HDDs...)
		s = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &DeviceNumber, sizeof(DeviceNumber), &size, NULL);
		if ((!s) || (size == 0)) {
			uuprintf("Could not get device number for device %s %s", path, s ? "(empty data)" : WindowsErrorString());
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
		uprintf("NOTE: This may be due to an excess of Virtual Drives, such as hidden ones created by the XBox PC app");
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
	HANDLE hDrive = INVALID_HANDLE_VALUE, hPhysical = INVALID_HANDLE_VALUE;
	UINT _drive_type;
	IO_STATUS_BLOCK io_status_block;
	FILE_FS_DEVICE_INFORMATION file_fs_device_info;
	BYTE geometry[256] = { 0 };
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)(void*)geometry;
	int i = 0, drives_found = 0, drive_number;
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
		uprintf("GetLogicalDriveStrings: Buffer too small (required %lu vs. %zu)", size, sizeof(drives));
		goto out;
	}

	r = TRUE;	// Required to detect drives that don't have volumes assigned
	for (drive = drives ;*drive; drive += safe_strlen(drive) + 1) {
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

		static_sprintf(logical_drive, "\\\\.\\%c:", toupper(drive[0]));
		// This call appears to freeze on some systems and we don't want to spend more
		// time than needed waiting for unresponsive drives, so use a 3 seconds timeout.
		hDrive = CreateFileWithTimeout(logical_drive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL, 3000);
		if (hDrive == INVALID_HANDLE_VALUE) {
			if (GetLastError() == WAIT_TIMEOUT)
				uprintf("Warning: Time-out while trying to query drive %c", toupper(drive[0]));
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
			drives_found++;
			if (drive_letters != NULL)
				drive_letters[i++] = *drive;
			// The drive type should be the same for all volumes, so we can overwrite
			if (drive_type != NULL)
				*drive_type = _drive_type;
		}
	}

	// Devices that don't have mounted partitions require special
	// handling to determine if they are fixed or removable.
	if ((drives_found == 0) && (drive_type != NULL)) {
		hPhysical = GetPhysicalHandle(DriveIndex + DRIVE_INDEX_MIN, FALSE, FALSE, FALSE);
		r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, geometry, sizeof(geometry), &size, NULL);
		safe_closehandle(hPhysical);
		if (r && size > 0) {
			if (DiskGeometry->Geometry.MediaType == FixedMedia)
				*drive_type = DRIVE_FIXED;
			else if (DiskGeometry->Geometry.MediaType == RemovableMedia)
				*drive_type = DRIVE_REMOVABLE;
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

// Removes all drive letters associated with the specific drive, and return
// either the first or last letter that was removed, according to bReturnLast.
char RemoveDriveLetters(DWORD DriveIndex, BOOL bReturnLast, BOOL bSilent)
{
	int i, len;
	char drive_letters[27] = { 0 }, drive_name[4] = "#:\\";

	if (!GetDriveLetters(DriveIndex, drive_letters)) {
		suprintf("Failed to get a drive letter");
		return 0;
	}
	if (drive_letters[0] == 0) {
		suprintf("No drive letter was assigned...");
		return GetUnusedDriveLetter();
	}
	len = (int)strlen(drive_letters);
	if (len == 0)
		return 0;

	// Unmount all mounted volumes that belong to this drive
	for (i = 0; i < len; i++) {
		// Check that the current image isn't located on a drive we are trying to dismount
		if ((boot_type == BT_IMAGE) && (drive_letters[i] == (PathGetDriveNumberU(image_path) + 'A'))) {
			if ((PathGetDriveNumberU(image_path) + 'A') == drive_letters[i]) {
				suprintf("ABORTED: Cannot use an image that is located on the target drive!");
				return 0;
			}
		}
		drive_name[0] = drive_letters[i];
		// DefineDosDevice() cannot have a trailing backslash...
		drive_name[2] = 0;
		DefineDosDeviceA(DDD_REMOVE_DEFINITION, drive_name, NULL);
		// ... but DeleteVolumeMountPoint() requires one. Go figure...
		drive_name[2] = '\\';
		if (!DeleteVolumeMountPointA(drive_name))
			suprintf("Failed to delete mountpoint %s: %s", drive_name, WindowsErrorString());
	}
	return drive_letters[bReturnLast ? (len - 1) : 0];
}

/*
 * Return the next unused drive letter from the system or NUL on error.
 */
char GetUnusedDriveLetter(void)
{
	DWORD size;
	char drive_letter, *drive, drives[26*4 + 1];	/* "D:\", "E:\", etc., plus one NUL */

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s", WindowsErrorString());
		return 0;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: Buffer too small (required %lu vs. %zu)", size, sizeof(drives));
		return 0;
	}

	for (drive_letter = 'C'; drive_letter <= 'Z'; drive_letter++) {
		for (drive = drives ; *drive; drive += safe_strlen(drive) + 1) {
			if (!isalpha(*drive))
				continue;
			if (drive_letter == (char)toupper((int)*drive))
				break;
		}
		if (!*drive)
			break;
	}

	return (drive_letter > 'Z') ? 0 : drive_letter;
}

BOOL IsDriveLetterInUse(const char drive_letter)
{
	DWORD size;
	char *drive, drives[26 * 4 + 1];

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s", WindowsErrorString());
		return TRUE;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: Buffer too small (required %lu vs. %zu)", size, sizeof(drives));
		return TRUE;
	}

	for (drive = drives; *drive; drive += safe_strlen(drive) + 1) {
		if (drive_letter == (char)toupper((int)*drive))
			return TRUE;
	}

	return FALSE;
}

/*
 * Return the drive letter and volume label
 * If the drive doesn't have a volume assigned, space is returned for the letter
 */
BOOL GetDriveLabel(DWORD DriveIndex, char* letters, char** label, BOOL bSilent)
{
	HANDLE hPhysical;
	DWORD error;
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
			*label = (VolumeLabel[0] != 0) ? VolumeLabel : STR_NO_LABEL;
		}
		safe_closehandle(h);
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
	if (DeviceIoControl(hPhysical, IOCTL_STORAGE_CHECK_VERIFY, NULL, 0, NULL, 0, NULL, NULL))
		AutorunLabel = get_token_data_file("label", AutorunPath);
	else if (GetLastError() == ERROR_NOT_READY)
		suprintf("Ignoring 'autorun.inf' label for drive %c: No media", toupper(letters[0]));
	safe_closehandle(hPhysical);
	if (AutorunLabel != NULL) {
		suprintf("Using 'autorun.inf' label for drive %c: '%s'", toupper(letters[0]), AutorunLabel);
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
			if (error != ERROR_UNRECOGNIZED_VOLUME)
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

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, geometry, sizeof(geometry), &size, NULL);
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
	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, geometry, sizeof(geometry), &size, NULL) && (size > 0);
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
	FAKE_FD fake_fd = { 0 };
	FILE* fp = (FILE*)&fake_fd;
	int i;

	fake_fd._handle = (char*)hPhysicalDrive;
	set_bytes_per_sector(SelectedDrive.SectorSize);

	if (!is_br(fp)) {
		suprintf("%s does not have a Boot Marker", TargetName);
		return FALSE;
	}
	for (i=0; i<ARRAYSIZE(known_mbr); i++) {
		if (known_mbr[i].fn(fp)) {
			suprintf("%s has a %s Master Boot Record", TargetName, known_mbr[i].str);
			return TRUE;
		}
	}

	suprintf("%s has an unknown Master Boot Record", TargetName);
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
 * This call returns the offset of the first ESP partition found
 * on the relevant drive, or 0ULL if no ESP was found.
 */
uint64_t GetEspOffset(DWORD DriveIndex)
{
	uint64_t ret = 0ULL;
	BOOL r;
	HANDLE hPhysical;
	DWORD size, i;
	BYTE layout[4096] = { 0 };
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)(void*)layout;

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, TRUE, TRUE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, sizeof(layout), &size, NULL);
	if (!r || size <= 0) {
		uprintf("Could not get layout for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		goto out;
	}

	for (i = 0; i < DriveLayout->PartitionCount; i++) {
		if (((DriveLayout->PartitionStyle == PARTITION_STYLE_MBR) && (DriveLayout->PartitionEntry[i].Mbr.PartitionType == 0xef)) ||
			((DriveLayout->PartitionStyle == PARTITION_STYLE_GPT) && CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_GENERIC_ESP))) {
			ret = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
			break;
		}
	}

out:
	safe_closehandle(hPhysical);
	return ret;
}

static BOOL StoreEspInfo(GUID* guid)
{
	uint8_t j;
	char key_name[2][16], *str;
	// Look for an empty slot and use that if available
	for (j = 1; j <= MAX_ESP_TOGGLE; j++) {
		static_sprintf(key_name[0], "ToggleEsp%02u", j);
		str = ReadSettingStr(key_name[0]);
		if ((str == NULL) || (str[0] == 0))
			return WriteSettingStr(key_name[0], GuidToString(guid, TRUE));
	}
	// All slots are used => Move every key down and add to last slot
	// NB: No, we don't care that the slot we remove may not be the oldest.
	for (j = 1; j < MAX_ESP_TOGGLE; j++) {
		static_sprintf(key_name[0], "ToggleEsp%02u", j);
		static_sprintf(key_name[1], "ToggleEsp%02u", j + 1);
		WriteSettingStr(key_name[0], ReadSettingStr(key_name[1]));
	}
	return WriteSettingStr(key_name[1], GuidToString(guid, TRUE));
}

static GUID* GetEspGuid(uint8_t index)
{
	char key_name[16];

	static_sprintf(key_name, "ToggleEsp%02u", index);
	return StringToGuid(ReadSettingStr(key_name));
}

static BOOL ClearEspInfo(uint8_t index)
{
	char key_name[16];
	static_sprintf(key_name, "ToggleEsp%02u", index);
	return WriteSettingStr(key_name, "");
}

/*
 * This calls changes the type of a GPT ESP back and forth to Basic Data.
 * Needed because Windows 10 doesn't mount ESPs by default, and also
 * doesn't let usermode apps (such as File Explorer) access mounted ESPs.
 */
BOOL ToggleEsp(DWORD DriveIndex, uint64_t PartitionOffset)
{
	char *volume_name, mount_point[] = DEFAULT_ESP_MOUNT_POINT;
	int i, j, esp_index = -1;
	BOOL r, ret = FALSE, delete_data = FALSE;
	HANDLE hPhysical;
	DWORD dl_size, size, offset;
	BYTE layout[4096] = { 0 }, buf[512];
	GUID *guid = NULL, *stored_guid = NULL, mbr_guid;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)(void*)layout;
	typedef struct {
		const uint8_t mbr_type;
		const uint8_t magic[8];
	} fat_mbr_type;
	const fat_mbr_type fat_mbr_types[] = {
		{ 0x0b, { 'F', 'A', 'T', ' ', ' ', ' ', ' ', ' ' } },
		{ 0x01, { 'F', 'A', 'T', '1', '2', ' ', ' ', ' ' } },
		{ 0x0e, { 'F', 'A', 'T', '1', '6', ' ', ' ', ' ' } },
		{ 0x0c, { 'F', 'A', 'T', '3', '2', ' ', ' ', ' ' } },
	};

	if ((PartitionOffset == 0) && (WindowsVersion.Version < WINDOWS_10)) {
		uprintf("ESP toggling is only available for Windows 10 or later");
		return FALSE;
	}

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, TRUE, TRUE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, sizeof(layout), &dl_size, NULL);
	if (!r || dl_size <= 0) {
		uprintf("Could not get layout for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		goto out;
	}

	if (PartitionOffset == 0) {
		// See if the current drive contains an ESP
		for (i = 0; i < (int)DriveLayout->PartitionCount; i++) {
			if (((DriveLayout->PartitionStyle == PARTITION_STYLE_MBR) && (DriveLayout->PartitionEntry[i].Mbr.PartitionType == 0xef)) ||
				((DriveLayout->PartitionStyle == PARTITION_STYLE_GPT) && CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_GENERIC_ESP))) {
				esp_index = i;
				break;
			}
		}

		if (esp_index >= 0) {
			// ESP -> Basic Data
			if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT) {
				uprintf("ESP name: '%S'", DriveLayout->PartitionEntry[esp_index].Gpt.Name);
				guid = &DriveLayout->PartitionEntry[esp_index].Gpt.PartitionId;
			} else {
				// For MBR we create a GUID from the disk signature and the offset
				mbr_guid.Data1 = DriveLayout->Mbr.Signature;
				mbr_guid.Data2 = 0; mbr_guid.Data3 = 0;
				*((uint64_t*)&mbr_guid.Data4) = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
				guid = &mbr_guid;
			}
			if (!StoreEspInfo(guid)) {
				uprintf("ESP toggling data could not be stored");
				goto out;
			}
			if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT) {
				DriveLayout->PartitionEntry[esp_index].Gpt.PartitionType = PARTITION_MICROSOFT_DATA;
			} else if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR) {
				// Default to FAT32 (non LBA) if we can't determine anything better
				DriveLayout->PartitionEntry[esp_index].Mbr.PartitionType = 0x0b;
				// Now detect if we're dealing with FAT12/16/32
				if (SetFilePointerEx(hPhysical, DriveLayout->PartitionEntry[esp_index].StartingOffset, NULL, FILE_BEGIN) &&
					ReadFile(hPhysical, buf, 512, &size, NULL) && size == 512) {
					for (offset = 0x36; offset <= 0x52; offset += 0x1c) {
						for (i = 0; i < ARRAYSIZE(fat_mbr_types); i++) {
							if (memcmp(&buf[offset], fat_mbr_types[i].magic, 8) == 0) {
								DriveLayout->PartitionEntry[esp_index].Mbr.PartitionType = fat_mbr_types[i].mbr_type;
								break;
							}
						}
					}
				}
			}
		} else {
			// Basic Data -> ESP
			for (i = 1; i <= MAX_ESP_TOGGLE && esp_index < 0; i++) {
				stored_guid = GetEspGuid((uint8_t)i);
				if (stored_guid != NULL) {
					for (j = 0; j < (int)DriveLayout->PartitionCount && esp_index < 0; j++) {
						if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT) {
							guid = &DriveLayout->PartitionEntry[j].Gpt.PartitionId;
						} else if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR) {
							mbr_guid.Data1 = DriveLayout->Mbr.Signature;
							mbr_guid.Data2 = 0; mbr_guid.Data3 = 0;
							*((uint64_t*)&mbr_guid.Data4) = DriveLayout->PartitionEntry[j].StartingOffset.QuadPart;
							guid = &mbr_guid;
						}
						if (CompareGUID(stored_guid, guid)) {
							esp_index = j;
							delete_data = TRUE;
							if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT)
								DriveLayout->PartitionEntry[esp_index].Gpt.PartitionType = PARTITION_GENERIC_ESP;
							else if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)
								DriveLayout->PartitionEntry[esp_index].Mbr.PartitionType = 0xef;
						}
					}
				}
			}
		}
	} else {
		for (i = 0; i < (int)DriveLayout->PartitionCount; i++) {
			if (DriveLayout->PartitionEntry[i].StartingOffset.QuadPart == PartitionOffset) {
				esp_index = i;
				if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT)
					DriveLayout->PartitionEntry[esp_index].Gpt.PartitionType = PARTITION_GENERIC_ESP;
				else if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)
					DriveLayout->PartitionEntry[esp_index].Mbr.PartitionType = 0xef;
				break;
			}
		}
	}
	if (esp_index < 0) {
		uprintf("No partition to toggle");
		goto out;
	}

	DriveLayout->PartitionEntry[esp_index].RewritePartition = TRUE;	// Just in case
	r = DeviceIoControl(hPhysical, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, (BYTE*)DriveLayout, dl_size, NULL, 0, NULL, NULL);
	if (!r) {
		uprintf("Could not set drive layout: %s", WindowsErrorString());
		goto out;
	}
	RefreshDriveLayout(hPhysical);
	if (PartitionOffset == 0) {
		if (delete_data) {
			// We successfully reverted ESP from Basic Data -> Delete stored ESP info
			ClearEspInfo((uint8_t)j);
		} else if (!IsDriveLetterInUse(*mount_point)) {
			// We successfully switched ESP to Basic Data -> Try to mount it
			volume_name = GetLogicalName(DriveIndex, DriveLayout->PartitionEntry[esp_index].StartingOffset.QuadPart, TRUE, FALSE);
			IGNORE_RETVAL(MountVolume(mount_point, volume_name));
			free(volume_name);
		}
	}
	ret = TRUE;

out:
	safe_closehandle(hPhysical);
	return ret;
}

// This is a crude attempt at detecting file systems through their superblock magic.
// Note that we only attempt to detect the file systems that Rufus can format as
// well as a couple other maintsream ones.
const char* GetFsName(HANDLE hPhysical, LARGE_INTEGER StartingOffset)
{
	typedef struct {
		const char* name;
		const uint8_t magic[8];
	} win_fs_type;
	const win_fs_type win_fs_types[] = {
		{ "exFAT", { 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' } },
		{ "NTFS", { 'N', 'T', 'F', 'S', ' ', ' ', ' ', ' ' } },
		{ "ReFS", { 'R', 'e', 'F', 'S', 0, 0, 0, 0 } }
	};
	const win_fs_type fat_fs_types[] = {
		{ "FAT", { 'F', 'A', 'T', ' ', ' ', ' ', ' ', ' ' } },
		{ "FAT12", { 'F', 'A', 'T', '1', '2', ' ', ' ', ' ' } },
		{ "FAT16", { 'F', 'A', 'T', '1', '6', ' ', ' ', ' ' } },
		{ "FAT32", { 'F', 'A', 'T', '3', '2', ' ', ' ', ' ' } },
	};
	const uint32_t ext_feature[3][3] = {
		// feature_compat
		{ 0x0000017B, 0x00000004, 0x00000E00 },
		// feature_ro_compat
		{ 0x00000003, 0x00000000, 0x00008FF8 },
		// feature_incompat
		{ 0x00000013, 0x0000004C, 0x0003F780 }
	};
	const char* ext_names[] = { "ext", "ext2", "ext3", "ext4" };
	const char* ret = "(Unrecognized)";
	DWORD i, j, offset, size, sector_size = 512;
	uint8_t* buf = calloc(sector_size, 1);
	if (buf == NULL)
		goto out;

	// 1. Try to detect ISO9660/FAT/exFAT/NTFS/ReFS through the 512 bytes superblock at offset 0
	if (!SetFilePointerEx(hPhysical, StartingOffset, NULL, FILE_BEGIN))
		goto out;
	if (!ReadFile(hPhysical, buf, sector_size, &size, NULL) || size != sector_size)
		goto out;
	if (strncmp("CD001", &buf[0x01], 5) == 0) {
		ret = "ISO9660";
		goto out;
	}

	// The beginning of a superblock for FAT/exFAT/NTFS/ReFS is pretty much always the same:
	// There are 3 bytes potentially used for a jump instruction, and then are 8 bytes of
	// OEM Name which, even if *not* technically correct, we are going to assume hold an
	// immutable file system magic for exFAT/NTFS/ReFS (but not for FAT, see below).
	for (i = 0; i < ARRAYSIZE(win_fs_types); i++)
		if (memcmp(&buf[0x03], win_fs_types[i].magic, 8) == 0)
			break;
	if (i < ARRAYSIZE(win_fs_types)) {
		ret = win_fs_types[i].name;
		goto out;
	}

	// For FAT, because the OEM Name may actually be set to something else than what we
	// expect, we poke the FAT12/16 Extended BIOS Parameter Block:
	// https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#Extended_BIOS_Parameter_Block
	// or FAT32 Extended BIOS Parameter Block:
	// https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#FAT32_Extended_BIOS_Parameter_Block
	for (offset = 0x36; offset <= 0x52; offset += 0x1C) {
		for (i = 0; i < ARRAYSIZE(fat_fs_types); i++)
			if (memcmp(&buf[offset], fat_fs_types[i].magic, 8) == 0)
				break;
		if (i < ARRAYSIZE(fat_fs_types)) {
			ret = fat_fs_types[i].name;
			goto out;
		}
	}

	// 2. Try to detect Apple AFS/HFS/HFS+ through the 512 bytes superblock at either offset 0 or 1024
	// "NXSB" at offset 0x20 => APFS
	if (strncmp("NXSB", &buf[0x20], 4) == 0) {
		ret = "APFS";
		goto out;
	}
	// Switch to offset 1024
	memset(buf, 0, sector_size);
	StartingOffset.QuadPart += 0x0400ULL;
	if (!SetFilePointerEx(hPhysical, StartingOffset, NULL, FILE_BEGIN))
		goto out;
	if (!ReadFile(hPhysical, buf, sector_size, &size, NULL) || size != sector_size)
		goto out;
	// "HX" or "H+" at offset 0x00 => HFS/HFS+
	if (buf[0] == 'H' && (buf[1] == 'X' || buf[1] == '+')) {
		ret = "HFS/HFS+";
		goto out;
	}

	// 3. Try to detect ext2/ext3/ext4 through the 512 bytes superblock at offset 1024
	// We're already at the right offset
	if (!SetFilePointerEx(hPhysical, StartingOffset, NULL, FILE_BEGIN))
		goto out;
	if (!ReadFile(hPhysical, buf, sector_size, &size, NULL) || size != sector_size)
		goto out;
	if (buf[0x38] == 0x53 && buf[0x39] == 0xEF) {
		uint32_t rev = 0;
		for (i = 0; i < 3; i++) {
			uint32_t feature = *((uint32_t*)&buf[0x5C + 4 * i]);
			for (j = 0; j < 3; j++) {
				if (feature & ext_feature[i][j] && rev <= j)
					rev = j + 1;
			}
		}
		assert(rev < ARRAYSIZE(ext_names));
		ret = ext_names[rev];
		goto out;
	}

	// 4. Try to detect UDF through by looking for a "BEA01\0" string at offset 0xC001
	// NB: This is not thorough UDF detection but good enough for our purpose.
	// For the full specs see: http://www.osta.org/specs/pdf/udf260.pdf
	memset(buf, 0, sector_size);
	StartingOffset.QuadPart += 0x8000ULL - 0x0400ULL;
	if (!SetFilePointerEx(hPhysical, StartingOffset, NULL, FILE_BEGIN))
		goto out;
	if (!ReadFile(hPhysical, buf, sector_size, &size, NULL) || size != sector_size)
		goto out;
	if (strncmp("BEA01", &buf[1], 5) == 0) {
		ret = "UDF";
		goto out;
	}

out:
	free(buf);
	return ret;
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
	memset(SelectedDrive.Partition, 0, sizeof(SelectedDrive.Partition));
	// Populate the filesystem data
	FileSystemName[0] = 0;
	volume_name = GetLogicalName(DriveIndex, 0, TRUE, FALSE);
	if ((volume_name == NULL) || (!GetVolumeInformationA(volume_name, NULL, 0, NULL, NULL, NULL, FileSystemName, FileSystemNameSize))) {
		suprintf("No volume information for drive 0x%02x", DriveIndex);
	}
	safe_free(volume_name);

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, geometry, sizeof(geometry), &size, NULL);
	if (!r || size <= 0) {
		suprintf("Could not get geometry for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return FALSE;
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

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		suprintf("Could not get layout for drive 0x%02x: %s", DriveIndex, WindowsErrorString());
		safe_closehandle(hPhysical);
		return FALSE;
	}

	switch (DriveLayout->PartitionStyle) {
	case PARTITION_STYLE_MBR:
		SelectedDrive.PartitionStyle = PARTITION_STYLE_MBR;
		for (i = 0; i < DriveLayout->PartitionCount; i++) {
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				SelectedDrive.nPartitions++;
			}
		}
		// Detect drives that are using the whole disk as a single partition
		if ((DriveLayout->PartitionEntry[0].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) &&
			(DriveLayout->PartitionEntry[0].StartingOffset.QuadPart == 0LL)) {
			suprintf("Partition type: SFD (%s) or unpartitioned", sfd_name);
			super_floppy_disk = TRUE;
		} else {
			suprintf("Partition type: MBR, NB Partitions: %d", SelectedDrive.nPartitions);
			SelectedDrive.has_mbr_uefi_marker = (DriveLayout->Mbr.Signature == MBR_UEFI_MARKER);
			suprintf("Disk ID: 0x%08X %s", DriveLayout->Mbr.Signature, SelectedDrive.has_mbr_uefi_marker?"(UEFI target)":"");
			AnalyzeMBR(hPhysical, "Drive", bSilent);
		}
		for (i = 0; i < DriveLayout->PartitionCount; i++) {
			isUefiNtfs = FALSE;
			if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
				part_type = DriveLayout->PartitionEntry[i].Mbr.PartitionType;
				// Microsoft will have to explain why they completely ignore the actual MBR partition
				// type for zeroed drive (which *IS* 0x00) and fill in Small FAT16 instead (0x04).
				// This means that if we detect a Small FAT16 "partition", that "starts" at offset 0
				// and that is larger than 16 MB, our drive is actually unpartitioned. 
				if (part_type == 0x04 && super_floppy_disk && SelectedDrive.DiskSize > 16 * MB)
					break;
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
				suprintf("Partition %d%s:", i + (super_floppy_disk ? 0 : 1), isUefiNtfs ? " (UEFI:NTFS)" : "");
				for (j = 0; j < ARRAYSIZE(mbr_mountable); j++) {
					if (part_type == mbr_mountable[j]) {
						ret = TRUE;
						break;
					}
				}
				if (i < MAX_PARTITIONS) {
					SelectedDrive.Partition[i].Offset = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
					SelectedDrive.Partition[i].Size = DriveLayout->PartitionEntry[i].PartitionLength.QuadPart;
				}
				suprintf("  Type: %s (0x%02x)\r\n  Detected File System: %s\r\n"
					"  Size: %s (%lld bytes)\r\n  Start Sector: %lld, Boot: %s",
					((part_type == 0x07 || super_floppy_disk) && (FileSystemName[0] != 0)) ?
					FileSystemName : GetMBRPartitionType(part_type), super_floppy_disk ? 0: part_type,
					GetFsName(hPhysical, DriveLayout->PartitionEntry[i].StartingOffset),
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
		suprintf("Disk GUID: %s", GuidToString(&DriveLayout->Gpt.DiskId, TRUE));
		suprintf("Max parts: %d, Start Offset: %" PRIi64 ", Usable = %" PRIi64 " bytes",
			DriveLayout->Gpt.MaxPartitionCount, DriveLayout->Gpt.StartingUsableOffset.QuadPart, DriveLayout->Gpt.UsableLength.QuadPart);
		for (i = 0; i < DriveLayout->PartitionCount; i++) {
			if (i < MAX_PARTITIONS) {
				SelectedDrive.Partition[i].Offset = DriveLayout->PartitionEntry[i].StartingOffset.QuadPart;
				SelectedDrive.Partition[i].Size = DriveLayout->PartitionEntry[i].PartitionLength.QuadPart;
				wcscpy(SelectedDrive.Partition[i].Name, DriveLayout->PartitionEntry[i].Gpt.Name);
			}
			SelectedDrive.nPartitions++;
			isUefiNtfs = (wcscmp(DriveLayout->PartitionEntry[i].Gpt.Name, L"UEFI:NTFS") == 0);
			suprintf("Partition %d%s:\r\n  Type: %s", i + 1, isUefiNtfs ? " (UEFI:NTFS)" : "",
				GetGPTPartitionType(&DriveLayout->PartitionEntry[i].Gpt.PartitionType));
			if (DriveLayout->PartitionEntry[i].Gpt.Name[0] != 0)
				suprintf("  Name: '%S'", DriveLayout->PartitionEntry[i].Gpt.Name);
			suprintf("  Detected File System: %s", GetFsName(hPhysical, DriveLayout->PartitionEntry[i].StartingOffset));
			suprintf("  ID: %s\r\n  Size: %s (%" PRIi64 " bytes)\r\n  Start Sector: %" PRIi64 ", Attributes: 0x%016" PRIX64,
				GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionId, TRUE),
				SizeToHumanReadable(DriveLayout->PartitionEntry[i].PartitionLength.QuadPart, TRUE, FALSE),
				DriveLayout->PartitionEntry[i].PartitionLength,
				DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize,
				DriveLayout->PartitionEntry[i].Gpt.Attributes);
			SelectedDrive.FirstDataSector = min(SelectedDrive.FirstDataSector,
				(DWORD)(DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / SelectedDrive.SectorSize));
			// Don't register the partitions that we don't care about destroying
			if ( isUefiNtfs ||
				 (CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_MICROSOFT_RESERVED)) ||
				 (CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_GENERIC_ESP)) )
				--SelectedDrive.nPartitions;
			if (CompareGUID(&DriveLayout->PartitionEntry[i].Gpt.PartitionType, &PARTITION_MICROSOFT_DATA))
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
		uprintf("Failed to open %c: for flushing: %s", toupper(drive_letter), WindowsErrorString());
		goto out;
	}
	r = FlushFileBuffers(hDrive);
	if (r == FALSE)
		uprintf("Failed to flush %c: %s", toupper(drive_letter), WindowsErrorString());

out:
	safe_closehandle(hDrive);
	return r;
}

/*
 * Unmount of volume using the DISMOUNT_VOLUME ioctl
 */
BOOL UnmountVolume(HANDLE hDrive)
{
	if (!DeviceIoControl(hDrive, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, NULL, NULL)) {
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
	char mounted_guid[52], dos_name[] = "?:";
#if defined(WINDOWS_IS_NOT_BUGGY)
	char mounted_letter[27] = { 0 };
	DWORD size;
#endif

	if ((drive_name == NULL) || (volume_name == NULL) || (drive_name[0] == '?')) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	// If we are working with a "\\?\GLOBALROOT" device, SetVolumeMountPoint()
	// is useless, so try with DefineDosDevice() instead.
	if (_strnicmp(volume_name, groot_name, groot_len) == 0) {
		dos_name[0] = drive_name[0];
		// Microsoft will also have to explain why "In no case is a trailing backslash allowed" [1] in
		// DefineDosDevice(), instead of just checking if the driver parameter is "X:\" and remove the
		// backslash from a copy of the parameter in the bloody API call. *THIS* really tells a lot
		// about the level of thought and foresight that actually goes into the Windows APIs...
		// [1] https://docs.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-definedosdevicew
		if (!DefineDosDeviceA(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, dos_name, &volume_name[14])) {
			uprintf("Could not mount %s as %c:", volume_name, toupper(drive_name[0]));
			return FALSE;
		}
		uprintf("%s was successfully mounted as %c:", volume_name, toupper(drive_name[0]));
		return TRUE;
	}

	// Great: Windows has a *MAJOR BUG* whereas, in some circumstances, GetVolumePathNamesForVolumeName()
	// can return the *WRONG* drive letter. And yes, we validated that this is *NOT* an issue like stack
	// or buffer corruption and whatnot. It *IS* a Windows bug. So just drop the idea of updating the
	// drive letter if already mounted and use the passed target always.
#if defined(WINDOWS_IS_NOT_BUGGY)
	// Windows may already have the volume mounted but under a different letter.
	// If that is the case, update drive_name to that letter.
	if ( (GetVolumePathNamesForVolumeNameA(volume_name, mounted_letter, sizeof(mounted_letter), &size))
	  && (size > 1) && (mounted_letter[0] != drive_name[0]) ) {
		uprintf("%s is already mounted as %c: instead of %c: - Will now use this target instead...",
			volume_name, toupper(mounted_letter[0]), toupper(drive_name[0]));
		drive_name[0] = mounted_letter[0];
		return TRUE;
	}
#endif

	if (!SetVolumeMountPointA(drive_name, volume_name)) {
		if (GetLastError() == ERROR_DIR_NOT_EMPTY) {
			if (!GetVolumeNameForVolumeMountPointA(drive_name, mounted_guid, sizeof(mounted_guid))) {
				uprintf("%s is already mounted, but volume GUID could not be checked: %s",
					drive_name, WindowsErrorString());
			} else if (safe_strcmp(volume_name, mounted_guid) != 0) {
				uprintf("%s is mounted, but volume GUID doesn't match:\r\n  expected %s, got %s",
					drive_name, volume_name, mounted_guid);
			} else {
				duprintf("%s is already mounted as %c:", volume_name, toupper(drive_name[0]));
				return TRUE;
			}
			uprintf("Retrying after dismount...");
			if (!DeleteVolumeMountPointA(drive_name))
				uprintf("Warning: Could not delete volume mountpoint '%s': %s", drive_name, WindowsErrorString());
			if (SetVolumeMountPointA(drive_name, volume_name))
				return TRUE;
			if ((GetLastError() == ERROR_DIR_NOT_EMPTY) &&
				GetVolumeNameForVolumeMountPointA(drive_name, mounted_guid, sizeof(mounted_guid)) &&
				(safe_strcmp(volume_name, mounted_guid) == 0)) {
				uprintf("%s was remounted as %c: (second time lucky!)", volume_name, toupper(drive_name[0]));
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
	volume_name = AltGetLogicalName(DriveIndex, PartitionOffset, FALSE, FALSE);
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
BOOL RemountVolume(char* drive_name, BOOL bSilent)
{
	char volume_name[51];

	// UDF requires a sync/flush, and it's also a good idea for other FS's
	FlushDrive(drive_name[0]);
	if (GetVolumeNameForVolumeMountPointA(drive_name, volume_name, sizeof(volume_name))) {
		if (MountVolume(drive_name, volume_name)) {
			suprintf("Successfully remounted %s as %c:", volume_name, toupper(drive_name[0]));
		} else {
			suprintf("Could not remount %s as %c: %s", volume_name, toupper(drive_name[0]), WindowsErrorString());
			// This will leave the drive inaccessible and must be flagged as an error
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_REMOUNT_VOLUME));
			return FALSE;
		}
	}
	return TRUE;
}

/*
 * Zero the first 'size' bytes of a partition. This is needed because we haven't found a way to
 * properly reset Windows's cached view of a drive partitioning short of cycling the USB port
 * (especially IOCTL_DISK_UPDATE_PROPERTIES is *USELESS*), and therefore the OS will try to
 * read the file system data at an old location, even if the partition has just been deleted.
 */
static BOOL ClearPartition(HANDLE hDrive, uint64_t offset, DWORD size)
{
	BOOL r = FALSE;
	uint8_t* buffer = calloc(size, 1);
	LARGE_INTEGER li_offset;

	if (buffer == NULL)
		return FALSE;

	li_offset.QuadPart = offset;
	if (!SetFilePointerEx(hDrive, li_offset, NULL, FILE_BEGIN)) {
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
	const LONGLONG bytes_per_track = ((LONGLONG)SelectedDrive.SectorsPerTrack) * SelectedDrive.SectorSize;
	const DWORD size_to_clear = MAX_SECTORS_TO_CLEAR * SelectedDrive.SectorSize;
	uint8_t* buffer;
	uint64_t last_offset = SelectedDrive.DiskSize;
	size_t uefi_ntfs_size = 0;
	DWORD pi = 0, mi, i, size, bufsize;
	CREATE_DISK CreateDisk = { PARTITION_STYLE_RAW, { { 0 } } };
	DRIVE_LAYOUT_INFORMATION_EX4 DriveLayoutEx = { 0 };
	// Go for a 260 MB sized ESP by default to keep everyone happy, including 4K sector users:
	// https://docs.microsoft.com/en-us/windows-hardware/manufacture/desktop/configure-uefigpt-based-hard-drive-partitions
	// and folks using MacOS: https://github.com/pbatard/rufus/issues/979
	LONGLONG esp_size = 260 * MB;
	LONGLONG ClusterSize = (LONGLONG)ComboBox_GetCurItemData(hClusterSize);

	PrintInfoDebug(0, MSG_238, PartitionTypeName[partition_style]);

	if (ClusterSize == 0)
		ClusterSize = 0x200;

	if (partition_style == PARTITION_STYLE_SFD)
		// Nothing to do
		return TRUE;

	if (extra_partitions & XP_UEFI_NTFS) {
		uefi_ntfs_size = GetResourceSize(hMainInstance, MAKEINTRESOURCEA(IDR_UEFI_NTFS), _RT_RCDATA, "uefi-ntfs.img");
		if (uefi_ntfs_size == 0) {
			uprintf("Could not access embedded 'uefi-ntfs.img'");
			return FALSE;
		}
	}

	memset(partition_index, 0, sizeof(partition_index));
	memset(SelectedDrive.Partition, 0, sizeof(SelectedDrive.Partition));

	// Compute the starting offset of the first partition
	if ((partition_style == PARTITION_STYLE_GPT) || (!IsChecked(IDC_OLD_BIOS_FIXES))) {
		// Go with the MS 1 MB wastage at the beginning...
		SelectedDrive.Partition[pi].Offset = 1 * MB;
	} else {
		// Some folks appear to think that 'Fixes for old BIOSes' is some kind of magic
		// wand and are adamant to try to apply them when creating *MODERN* VHD drives.
		// This, however, wrecks havok on MS' internal format calls because, as opposed
		// to what is the case for regular drives, VHDs require each cluster block to
		// be aligned to the cluster size, and that may not be the case with the stupid
		// CHS sizes that IBM imparted upon us. Long story short, we now align to a
		// cylinder size that is itself aligned to the cluster size.
		// If this actually breaks old systems, please send your complaints to IBM.
		SelectedDrive.Partition[pi].Offset = HI_ALIGN_X_TO_Y(bytes_per_track, ClusterSize);
		// GRUB2 no longer fits in the usual 31½ KB that the above computation provides
		// so just unconditionally double that size and get on with it.
		SelectedDrive.Partition[pi].Offset *= 2;
	}

	// Having the ESP up front may help (and is the Microsoft recommended way) but this
	// is only achievable if we can mount more than one partition at once, which means
	// either fixed drive or Windows 10 1703 or later.
	if (((SelectedDrive.MediaType == FixedMedia) || (WindowsVersion.BuildNumber > 15000)) &&
		(extra_partitions & XP_ESP)) {
		assert(partition_style == PARTITION_STYLE_GPT);
		partition_index[PI_ESP] = pi;
		wcscpy(SelectedDrive.Partition[pi].Name, L"EFI System Partition");
		SelectedDrive.Partition[pi].Size = esp_size;
		SelectedDrive.Partition[pi + 1].Offset = SelectedDrive.Partition[pi].Offset + SelectedDrive.Partition[pi].Size;
		// Align next partition to track and cluster
		SelectedDrive.Partition[pi + 1].Offset = HI_ALIGN_X_TO_Y(SelectedDrive.Partition[pi + 1].Offset, bytes_per_track);
		if (ClusterSize % SelectedDrive.SectorSize == 0)
			SelectedDrive.Partition[pi + 1].Offset = LO_ALIGN_X_TO_Y(SelectedDrive.Partition[pi + 1].Offset, ClusterSize);
		assert(SelectedDrive.Partition[pi + 1].Offset >= SelectedDrive.Partition[pi].Offset + SelectedDrive.Partition[pi].Size);
		pi++;
		// Clear the extra partition we processed
		extra_partitions &= ~(XP_ESP);
	}

	// If required, set the MSR partition (GPT only - must be created before the data part)
	if (extra_partitions & XP_MSR) {
		assert(partition_style == PARTITION_STYLE_GPT);
		wcscpy(SelectedDrive.Partition[pi].Name, L"Microsoft Reserved Partition");
		SelectedDrive.Partition[pi].Size = 128 * MB;
		SelectedDrive.Partition[pi + 1].Offset = SelectedDrive.Partition[pi].Offset + SelectedDrive.Partition[pi].Size;
		SelectedDrive.Partition[pi + 1].Offset = HI_ALIGN_X_TO_Y(SelectedDrive.Partition[pi + 1].Offset, bytes_per_track);
		if (ClusterSize % SelectedDrive.SectorSize == 0)
			SelectedDrive.Partition[pi + 1].Offset = LO_ALIGN_X_TO_Y(SelectedDrive.Partition[pi + 1].Offset, ClusterSize);
		assert(SelectedDrive.Partition[pi + 1].Offset >= SelectedDrive.Partition[pi].Offset + SelectedDrive.Partition[pi].Size);
		pi++;
		extra_partitions &= ~(XP_MSR);
	}

	// Reserve an entry for the main partition
	partition_index[PI_MAIN] = pi++;
	// Shorthand for the main index.
	mi = partition_index[PI_MAIN];
	wcscpy(SelectedDrive.Partition[mi].Name, write_as_esp ? L"EFI System Partition" : L"Main Data Partition");

	if (extra_partitions) {
		// Adjust the size according to extra partitions (which we always align to a track)
		// TODO: Should we align these to cluster as well?
		if (extra_partitions & XP_PERSISTENCE) {
			assert(persistence_size != 0);
			partition_index[PI_CASPER] = pi;
			wcscpy(SelectedDrive.Partition[pi].Name, L"Linux Persistence");
			SelectedDrive.Partition[pi++].Size = HI_ALIGN_X_TO_Y(persistence_size, bytes_per_track);
		}
		if (extra_partitions & XP_ESP) {
			partition_index[PI_ESP] = pi;
			wcscpy(SelectedDrive.Partition[pi].Name, L"EFI System Partition");
			SelectedDrive.Partition[pi++].Size = HI_ALIGN_X_TO_Y(esp_size, bytes_per_track);
		} else if (extra_partitions & XP_UEFI_NTFS) {
			partition_index[PI_UEFI_NTFS] = pi;
			wcscpy(SelectedDrive.Partition[pi].Name, L"UEFI:NTFS");
			SelectedDrive.Partition[pi++].Size = HI_ALIGN_X_TO_Y(uefi_ntfs_size, bytes_per_track);
		} else if (extra_partitions & XP_COMPAT) {
			wcscpy(SelectedDrive.Partition[pi].Name, L"BIOS Compatibility");
			SelectedDrive.Partition[pi++].Size = bytes_per_track;	// One track for the extra partition
		}
	}
	assert(pi <= MAX_PARTITIONS);
	if (pi > MAX_PARTITIONS)
		return FALSE;

	// Compute the offsets of the extra partitions (which we always align to a track)
	last_offset = SelectedDrive.DiskSize;
	if (partition_style == PARTITION_STYLE_GPT)
		last_offset -= 33ULL * SelectedDrive.SectorSize;
	for (i = pi - 1; i > mi; i--) {
		assert(SelectedDrive.Partition[i].Size < last_offset);
		SelectedDrive.Partition[i].Offset = LO_ALIGN_X_TO_Y(last_offset - SelectedDrive.Partition[i].Size, bytes_per_track);
		last_offset = SelectedDrive.Partition[i].Offset;
	}

	// With the above, Compute the main partition size (which we align to a track)
	assert(last_offset > SelectedDrive.Partition[mi].Offset);
	SelectedDrive.Partition[mi].Size = LO_ALIGN_X_TO_Y(last_offset - SelectedDrive.Partition[mi].Offset, bytes_per_track);
	// Try to make sure that the main partition size is a multiple of the cluster size
	// This can be especially important when trying to capture an NTFS partition as FFU, as, when
	// the NTFS partition is aligned to cluster size, the FFU capture parses the NTFS allocated
	// map to only record clusters that are in use, whereas, if not aligned, the FFU capture uses
	// a full sector by sector scan of the NTFS partition and records any non-zero garbage, which
	// may include garbage leftover data from a previous reformat...
	if (ClusterSize % SelectedDrive.SectorSize == 0)
		SelectedDrive.Partition[mi].Size = LO_ALIGN_X_TO_Y(SelectedDrive.Partition[mi].Size, ClusterSize);
	if (SelectedDrive.Partition[mi].Size <= 0) {
		uprintf("Error: Invalid %S size", SelectedDrive.Partition[mi].Name);
		return FALSE;
	}

	// Build the DriveLayoutEx table
	for (i = 0; i < pi; i++) {
		uprintf("● Creating %S%s (offset: %lld, size: %s)", SelectedDrive.Partition[i].Name,
			(wcsstr(SelectedDrive.Partition[i].Name, L"Partition") == NULL) ? " Partition" : "",
			SelectedDrive.Partition[i].Offset,
			SizeToHumanReadable(SelectedDrive.Partition[i].Size, TRUE, FALSE));
		// Zero the first sectors of the partition to avoid file system caching issues
		if (!ClearPartition(hDrive, SelectedDrive.Partition[i].Offset,
			(DWORD)MIN(size_to_clear, SelectedDrive.Partition[i].Size)))
			uprintf("Could not zero %S: %s", SelectedDrive.Partition[i].Name, WindowsErrorString());
		DriveLayoutEx.PartitionEntry[i].PartitionStyle = partition_style;
		DriveLayoutEx.PartitionEntry[i].StartingOffset.QuadPart = SelectedDrive.Partition[i].Offset;
		DriveLayoutEx.PartitionEntry[i].PartitionLength.QuadPart = SelectedDrive.Partition[i].Size;
		DriveLayoutEx.PartitionEntry[i].PartitionNumber = i + 1;
		DriveLayoutEx.PartitionEntry[i].RewritePartition = TRUE;
		if (partition_style == PARTITION_STYLE_MBR) {
			if (i == mi) {
				DriveLayoutEx.PartitionEntry[i].Mbr.BootIndicator = (boot_type != BT_NON_BOOTABLE);
				switch (file_system) {
				case FS_FAT16:
					DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0x0e;	// FAT16 LBA
					break;
				case FS_NTFS:
				case FS_EXFAT:
				case FS_UDF:
				case FS_REFS:
					DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0x07;
					break;
				case FS_EXT2:
				case FS_EXT3:
				case FS_EXT4:
					DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0x83;
					break;
				case FS_FAT32:
					DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0x0c;	// FAT32 LBA
					break;
				default:
					uprintf("Unsupported file system");
					return FALSE;
				}
			}
			// May override the the type of main partition if write_as_esp is active
			if ((wcscmp(SelectedDrive.Partition[i].Name, L"EFI System Partition") == 0) ||
				(wcscmp(SelectedDrive.Partition[i].Name, L"UEFI:NTFS") == 0))
				DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0xef;
			else if (wcscmp(SelectedDrive.Partition[i].Name, L"Linux Persistence") == 0)
				DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = 0x83;
			else if (wcscmp(SelectedDrive.Partition[i].Name, L"BIOS Compatibility") == 0)
				DriveLayoutEx.PartitionEntry[i].Mbr.PartitionType = RUFUS_EXTRA_PARTITION_TYPE;
		} else {
			assert(partition_style == PARTITION_STYLE_GPT);
			if (wcscmp(SelectedDrive.Partition[i].Name, L"UEFI:NTFS") == 0) {
				DriveLayoutEx.PartitionEntry[i].Gpt.PartitionType = PARTITION_GENERIC_ESP;
				// Prevent a drive letter from being assigned to the UEFI:NTFS partition
				DriveLayoutEx.PartitionEntry[i].Gpt.Attributes = GPT_BASIC_DATA_ATTRIBUTE_NO_DRIVE_LETTER;
#if !defined(_DEBUG)
				// Also make the partition read-only for release versions
				DriveLayoutEx.PartitionEntry[i].Gpt.Attributes += GPT_BASIC_DATA_ATTRIBUTE_READ_ONLY;
#endif
			} else if (wcscmp(SelectedDrive.Partition[i].Name, L"EFI System Partition") == 0)
				DriveLayoutEx.PartitionEntry[i].Gpt.PartitionType = PARTITION_GENERIC_ESP;
			else if (wcscmp(SelectedDrive.Partition[i].Name, L"Linux Persistence") == 0)
				DriveLayoutEx.PartitionEntry[i].Gpt.PartitionType = PARTITION_LINUX_DATA;
			else if (wcscmp(SelectedDrive.Partition[i].Name, L"Microsoft Reserved Partition") == 0)
				DriveLayoutEx.PartitionEntry[i].Gpt.PartitionType = PARTITION_MICROSOFT_RESERVED;
			else
				DriveLayoutEx.PartitionEntry[i].Gpt.PartitionType = PARTITION_MICROSOFT_DATA;
			IGNORE_RETVAL(CoCreateGuid(&DriveLayoutEx.PartitionEntry[i].Gpt.PartitionId));
			wcscpy(DriveLayoutEx.PartitionEntry[i].Gpt.Name, SelectedDrive.Partition[i].Name);
		}
	}

	// We need to write the UEFI:NTFS partition before we refresh the disk
	if (extra_partitions & XP_UEFI_NTFS) {
		LARGE_INTEGER li;
		uprintf("Writing UEFI:NTFS data...", SelectedDrive.Partition[partition_index[PI_UEFI_NTFS]].Name);
		li.QuadPart = SelectedDrive.Partition[partition_index[PI_UEFI_NTFS]].Offset;
		if (!SetFilePointerEx(hDrive, li, NULL, FILE_BEGIN)) {
			uprintf("  Could not set position");
			return FALSE;
		}
		buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_UEFI_NTFS), _RT_RCDATA,
			"uefi-ntfs.img", &bufsize, FALSE);
		if (buffer == NULL) {
			uprintf("  Could not access source image");
			return FALSE;
		}
		if(!WriteFileWithRetry(hDrive, buffer, bufsize, NULL, WRITE_RETRIES)) {
			uprintf("  Write error: %s", WindowsErrorString());
			return FALSE;
		}
	}

	switch (partition_style) {
	case PARTITION_STYLE_MBR:
		CreateDisk.PartitionStyle = PARTITION_STYLE_MBR;
		// If MBR+UEFI is selected, write an UEFI marker in lieu of the regular MBR signature.
		// This helps us reselect the partition scheme option that was used when creating the
		// drive in Rufus. As far as I can tell, Windows doesn't care much if this signature
		// isn't unique for USB drives.
		CreateDisk.Mbr.Signature = mbr_uefi_marker ? MBR_UEFI_MARKER : (DWORD)GetTickCount64();

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
		DriveLayoutEx.PartitionCount = pi;
		// At the very least, a GPT disk has 34 reserved sectors at the beginning and 33 at the end.
		DriveLayoutEx.Type.Gpt.StartingUsableOffset.QuadPart = 34 * SelectedDrive.SectorSize;
		DriveLayoutEx.Type.Gpt.UsableLength.QuadPart = SelectedDrive.DiskSize - (34 + 33) * SelectedDrive.SectorSize;
		DriveLayoutEx.Type.Gpt.MaxPartitionCount = MAX_PARTITIONS;
		DriveLayoutEx.Type.Gpt.DiskId = CreateDisk.Gpt.DiskId;
		break;
	}

	// If you don't call IOCTL_DISK_CREATE_DISK, the IOCTL_DISK_SET_DRIVE_LAYOUT_EX call will fail
	size = sizeof(CreateDisk);
	if (!DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK, (BYTE*)&CreateDisk, size, NULL, 0, NULL, NULL)) {
		uprintf("Could not reset disk: %s", WindowsErrorString());
		return FALSE;
	}

	// "The goggles, they do nothing!"
	RefreshDriveLayout(hDrive);

	size = sizeof(DriveLayoutEx) - ((partition_style == PARTITION_STYLE_GPT) ?
		((MAX_PARTITIONS - pi) * sizeof(PARTITION_INFORMATION_EX)) : 0);
	// The DRIVE_LAYOUT_INFORMATION_EX used by Microsoft, with its 1-sized array, is designed to overrun...
	// coverity[overrun-buffer-arg]
	if (!DeviceIoControl(hDrive, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, (BYTE*)&DriveLayoutEx, size, NULL, 0, NULL, NULL)) {
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

	// Diskpart does call the following IOCTL this after updating the partition table, so we do too
	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, NULL, NULL);
	if (!r)
		uprintf("Could not refresh drive layout: %s", WindowsErrorString());
	return r;
}

/* Initialize disk for partitioning */
BOOL InitializeDisk(HANDLE hDrive)
{
	BOOL r;
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {{0}}};

	uprintf("Initializing disk...");

	r = DeviceIoControl(hDrive, IOCTL_DISK_CREATE_DISK, (BYTE*)&CreateDisk, sizeof(CreateDisk), NULL, 0, NULL, NULL);
	if (!r) {
		uprintf("Could not delete drive layout: %s", WindowsErrorString());
		return FALSE;
	}

	r = DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, NULL, NULL);
	if (!r) {
		uprintf("Could not refresh drive layout: %s", WindowsErrorString());
		return FALSE;
	}

	return TRUE;
}

/*
 * Convert MBR or GPT partition types to their human readable forms
 */
const char* GetMBRPartitionType(const uint8_t type)
{
	int i;
	for (i = 0; (i < ARRAYSIZE(mbr_type)) && (mbr_type[i].type != type); i++);
	return (i < ARRAYSIZE(mbr_type)) ? mbr_type[i].name : "Unknown";
}

const char* GetGPTPartitionType(const GUID* guid)
{
	int i;
	for (i = 0; (i < ARRAYSIZE(gpt_type)) && !CompareGUID(guid, gpt_type[i].guid); i++);
	return (i < ARRAYSIZE(gpt_type)) ? gpt_type[i].name : GuidToString(guid, TRUE);
}

/*
 * Detect Microsoft Dev Drives, which are VHDs consisting of a small MSR followed by a large
 * (50 GB or more) ReFS partition. See https://learn.microsoft.com/en-us/windows/dev-drive/.
 * NB: Despite the option being proposed, I have *NOT* been able to create MBR-based Dev Drives.
 */
BOOL IsMsDevDrive(DWORD DriveIndex)
{
	BOOL r, ret = FALSE;
	DWORD size = 0;
	HANDLE hPhysical = INVALID_HANDLE_VALUE;
	BYTE layout[4096] = { 0 };
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)(void*)layout;

	hPhysical = GetPhysicalHandle(DriveIndex, FALSE, FALSE, TRUE);
	if (hPhysical == INVALID_HANDLE_VALUE)
		goto out;

	r = DeviceIoControl(hPhysical, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, layout, sizeof(layout), &size, NULL);
	if (!r || size <= 0)
		goto out;

	if (DriveLayout->PartitionStyle != PARTITION_STYLE_GPT)
		goto out;
	if (DriveLayout->PartitionCount != 2)
		goto out;
	if (!CompareGUID(&DriveLayout->PartitionEntry[0].Gpt.PartitionType, &PARTITION_MICROSOFT_RESERVED))
		goto out;
	if (!CompareGUID(&DriveLayout->PartitionEntry[1].Gpt.PartitionType, &PARTITION_MICROSOFT_DATA))
		goto out;
	if (DriveLayout->PartitionEntry[1].PartitionLength.QuadPart < 20 * GB)
		goto out;
	ret = (strcmp(GetFsName(hPhysical, DriveLayout->PartitionEntry[1].StartingOffset), "ReFS") == 0);

out:
	safe_closehandle(hPhysical);
	return ret;
}
