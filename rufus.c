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

// http://git.kernel.org/?p=fs/ext2/e2fsprogs.git;a=blob;f=misc/badblocks.c
// http://ms-sys.sourceforge.net/
// http://thestarman.pcministry.com/asm/mbr/MSWIN41.htm
// http://www.c-jump.com/CIS24/Slides/FAT/lecture.html#F01_0130_sector_assignments

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"
#include "sys_types.h"

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

static HWND hDeviceList, hCapacity, hFileSystem, hLabel;
static HWND hDeviceTooltip = NULL, hFSTooltip = NULL;
static StrArray DriveID, DriveLabel;
static DWORD FormatErr;

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


void StatusPrintf(const char *format, ...)
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
 * Open a drive with optional write access - return a drive HANDLE and the drive letter
 * This call is quite risky (left unchecked, inadvertently passing 0 as index would
 * return a handle to C:, which we might then proceed to unknowingly repartition!),
 * so we apply the following mitigation factors:
 * - Valid indexes must belong to a specific range [DRIVE_INDEX_MIN; DRIVE_INDEX_MAX]
 * - When opening for write access, we lock the volume. If that fails, which would
 *   typically be the case on C:\ or any other drive in use, we fail the call
 * - We report the full path of any drive that was successfully opened for write acces
 */
static BOOL GetDriveHandle(DWORD DriveIndex, HANDLE* hDrive, char* DriveLetter, BOOL bWriteAccess)
{
	BOOL r;
	DWORD size;
	STORAGE_DEVICE_NUMBER_REDEF device_number = {0};
	char drives[26*4];	/* "D:\", "E:\", etc. */
	char *drive = drives;
	char drive_name[] = "\\\\.\\#:";

	if ((DriveIndex < DRIVE_INDEX_MIN) || (DriveIndex > DRIVE_INDEX_MAX)) {
		uprintf("WARNING: Bad index value. Please check your code!\n");
	}
	DriveIndex -= DRIVE_INDEX_MIN;

	size = GetLogicalDriveStringsA(sizeof(drives), drives);
	if (size == 0) {
		uprintf("GetLogicalDriveStrings failed: %s\n", WindowsErrorString());
		return FALSE;
	}
	if (size > sizeof(drives)) {
		uprintf("GetLogicalDriveStrings: buffer too small (required %d vs %d)\n", size, sizeof(drives));
		return FALSE;
	}

	*hDrive = INVALID_HANDLE_VALUE;
	for ( ;*drive; drive += safe_strlen(drive)+1) {
		if (!isalpha(*drive))
			continue;
		*drive = (char)toupper((int)*drive);
		if (*drive < 'C') {
			continue;
		}
		safe_sprintf(drive_name, sizeof(drive_name), "\\\\.\\%c:", drive[0]);
		*hDrive = CreateFileA(drive_name, GENERIC_READ|(bWriteAccess?GENERIC_WRITE:0),
			FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
		if (hDrive == INVALID_HANDLE_VALUE) {
			uprintf("Could not open drive %c: %s\n", drive[0], WindowsErrorString());
			continue;
		}

		r = DeviceIoControl(*hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL,
			0, &device_number, sizeof(device_number), &size, NULL);
		if ((!r) || (size <= 0)) {
			uprintf("IOCTL_STORAGE_GET_DEVICE_NUMBER (GetDriveHandle) failed: %s\n", WindowsErrorString());
			safe_closehandle(*hDrive);
			break;
		}
		if (device_number.DeviceNumber == DriveIndex) {
			if (bWriteAccess) {
				if (!DeviceIoControl(*hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL)) {
					uprintf("Could not get exclusive access to %s: %s\n", drive_name, WindowsErrorString());
					safe_closehandle(*hDrive);
					return FALSE;
				}
				uprintf("Warning: Opening %s drive for write access\n", drive_name);
			}
			break;
		}
		safe_closehandle(*hDrive);
	}

	if (DriveLetter != NULL) {
		*DriveLetter = *drive?*drive:' ';	// TODO: handle NUL char upstream
	}

	return (*hDrive != INVALID_HANDLE_VALUE);
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

	if (!GetDriveHandle(DriveIndex, &hDrive, DrivePath, FALSE))
		return FALSE;
	safe_closehandle(hDrive);
	*letter = DrivePath[0];

	if (GetVolumeInformationA(DrivePath, volume_label, sizeof(volume_label), NULL, NULL, NULL, NULL, 0) && *volume_label) {
		*label = volume_label;
	}

	return TRUE;
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

	if (!GetDriveHandle(SelectedDrive.DeviceNumber, &hDrive, DrivePath, FALSE))
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

static BOOL WriteSectors(HANDLE hDrive, size_t SectorSize, size_t StartSector, size_t nSectors, void* Buf, size_t BufSize)
{
	LARGE_INTEGER ptr;
	DWORD Size;

	if (SectorSize * nSectors > BufSize) {
		uprintf("WriteSectors: Buffer is too small\n");
		return FALSE;
	}

	ptr.QuadPart = StartSector*SectorSize;
	if (!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN)) {
		uprintf("WriteSectors: Could not access sector %lld - %s\n", StartSector, WindowsErrorString());
		return FALSE;
	}

	if ((!WriteFile(hDrive, Buf, (DWORD)BufSize, &Size, NULL)) || (Size != BufSize)) {
		uprintf("WriteSectors: Write error - %s\n", WindowsErrorString());
		return FALSE;
	}

	return TRUE;
}

static BOOL ReadSectors(HANDLE hDrive, size_t SectorSize, size_t StartSector, size_t nSectors, void* Buf, size_t BufSize)
{
	LARGE_INTEGER ptr;
	DWORD size;

	if (SectorSize * nSectors > BufSize) {
		uprintf("ReadSectors: Buffer is too small\n");
		return FALSE;
	}

	ptr.QuadPart = StartSector*SectorSize;
	if (!SetFilePointerEx(hDrive, ptr, NULL, FILE_BEGIN)) {
		uprintf("ReadSectors: Could not access sector %lld - %s\n", StartSector, WindowsErrorString());
		return FALSE;
	}

	if ((!ReadFile(hDrive, Buf, (DWORD)BufSize, &size, NULL)) || (size != BufSize)) {
		uprintf("ReadSectors: Write error - %s\n", WindowsErrorString());
		return FALSE;
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

	StatusPrintf("Partitioning...");
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
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x0e;
		break;
	case FS_NTFS:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x07;
		break;
	default:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x0c;
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

	if (FormatErr != 0) return FALSE;

	switch(Command) {
	case FCC_PROGRESS:
		percent = (DWORD*)Data;
		PostMessage(hMainDialog, UM_FORMAT_PROGRESS, (WPARAM)*percent, (LPARAM)0);
		uprintf("%d percent completed.\n", *percent);
		break;
	case FCC_STRUCTURE_PROGRESS:	// No progress on quick format
		uprintf("format task %d/n completed.\n", ++task_number);
		break;
	case FCC_DONE:
		if(*(BOOLEAN*)Data == FALSE) {
			uprintf("Error while formatting.\n");
			FormatErr = FCC_DONE;
		}
		break;
	case FCC_INCOMPATIBLE_FILE_SYSTEM:
		uprintf("Incompatible File System\n");
		FormatErr = Command;
		break;
	case FCC_ACCESS_DENIED:
		uprintf("Access denied\n");
		FormatErr = Command;
		break;
	case FCC_MEDIA_WRITE_PROTECTED:
		uprintf("Media is write protected\n");
		FormatErr = Command;
		break;
	case FCC_VOLUME_IN_USE:
		uprintf("Volume is in use\n");
		FormatErr = Command;
		break;
	case FCC_CANT_QUICK_FORMAT:
		uprintf("Cannot quick format this volume\n");
		FormatErr = Command;
		break;
	case FCC_BAD_LABEL:
		uprintf("Bad label\n");
		break;
	case FCC_OUTPUT:
		uprintf("%s\n", ((PTEXTOUTPUT)Data)->Output);
		break;
	case FCC_CLUSTER_SIZE_TOO_SMALL:
		uprintf("Allocation unit size is too small\n");
		FormatErr = Command;
		break;
	case FCC_CLUSTER_SIZE_TOO_BIG:
		uprintf("Allocation unit size is too big\n");
		FormatErr = Command;
		break;
	case FCC_VOLUME_TOO_SMALL:
		uprintf("Volume is too small\n");
		FormatErr = Command;
		break;
	case FCC_VOLUME_TOO_BIG:
		uprintf("Volume is too big\n");
		FormatErr = Command;
		break;
	case FCC_NO_MEDIA_IN_DRIVE:
		uprintf("No media\n");
		FormatErr = Command;
		break;
	default:
		uprintf("FormatExCallback: received unhandled command %X\n", Command);
		break;
	}
	return (FormatErr == 0);
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
	StatusPrintf("Formatting...");
	PF_INIT_OR_OUT(FormatEx, fmifs);

	// TODO: properly set MediaType
	FormatErr = 0;
	GetWindowTextW(hFileSystem, wFSType, ARRAYSIZE(wFSType));
	GetWindowTextW(hLabel, wLabel, ARRAYSIZE(wLabel));
	// TODO set sector size
	pfFormatEx(wDriveRoot, RemovableMedia, wFSType, wLabel,
		IsChecked(IDC_QUICKFORMAT), 4096, FormatExCallback);
	if (FormatErr == 0) {
		uprintf("Format completed.\n");
		r = TRUE;
	} else {
		uprintf("Format error: 0x%02x\n", FormatErr);
	}

out:
	return r;
}

/*
 * Create a separate thread for the formatting operation
 */
static void __cdecl FormatThread(void* param)
{
	DWORD num = (DWORD)(uintptr_t)param;
	HANDLE hDrive;
	BOOL r;
	char drive_name[] = "?:";
	int i;

	if (!GetDriveHandle(num, &hDrive, NULL, TRUE)) {
		// TODO: use FormatErr to report an error
		goto out;
	}

	r = CreatePartition(hDrive);
	safe_closehandle(hDrive);
	if (!r) {
		// TODO: use FormatErr to report an error
		goto out;
	}

	// Make sure we can reopen the drive before trying to format it
	for (i=0; i<10; i++) {
		Sleep(500);
		if (GetDriveHandle(num, &hDrive, drive_name, FALSE)) {
			break;
		}
	}
	if (i >= 10) {
		uprintf("Unable to reopen drive post partitioning\n");
		// TODO: use FormatErr to report an error
		goto out;
	}
	safe_closehandle(hDrive);

	if (!FormatDrive(drive_name[0])) {
		goto out;
	}

	if (IsChecked(IDC_DOSSTARTUP)) {
		// TODO: check return value & set bootblock
		ExtractMSDOS(drive_name);
	}

out:
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
	PostMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);

	return TRUE;
}

/* Toggle controls according to operation */
static void EnableControls(BOOL bEnable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_DEVICE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CAPACITY), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ALLOCSIZE), bEnable);
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
				// TODO: destroy MessageBox if operation completed while waiting
				if (MessageBoxA(hMainDialog, "Cancelling may leave the device in an UNUSABLE state.\r\n"
					"If you are sure you want to cancel, click YES. Otherwise, click NO.",
					"Do you want to cancel?", MB_YESNO|MB_ICONWARNING) == IDYES) {
					// Operation may have completed
					if (format_thid != -1L) {
						FormatErr = -1;
						StatusPrintf("Cancelling - please wait...");
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
				StatusPrintf("%d device%s found.", ComboBox_GetCount(hDeviceList),
					(ComboBox_GetCount(hDeviceList)!=1)?"s":"");
				PopulateProperties(ComboBox_GetCurSel(hDeviceList));
				break;
			}
			break;
		case IDC_START:
			// TODO: disable all controls and replace Close with Cancel
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
					format_thid = _beginthread(FormatThread, 0, (void*)(uintptr_t)DeviceNum);
					if (format_thid == -1L) {
						// TODO: handle error
						uprintf("Unable to start formatting thread");
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
		if (IsChecked(IDC_QUICKFORMAT)) {
			SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
			SetWindowLongPtr(hProgress, GWL_STYLE, ProgressStyle);
			// This is the only way to achieve instantenous progress transition
			SendMessage(hProgress, PBM_SETRANGE, 0, 101<<16);
			SendMessage(hProgress, PBM_SETPOS, 101, 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, 100<<16);
		}
		SendMessage(hProgress, PBM_SETPOS, FormatErr?0:100, 0);
		// TODO: report cancelled status
		StatusPrintf(FormatErr?"FAILED":"DONE");
		EnableControls(TRUE);
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
