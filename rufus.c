/*
 * Rufus: The Resourceful USB Formatting Utility
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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
#include <math.h>
#include <commctrl.h>
#include <setupapi.h>
#include <winioctl.h>
#include <process.h>
#include <dbt.h>

// http://git.kernel.org/?p=fs/ext2/e2fsprogs.git;a=blob;f=misc/badblocks.c
// http://thestarman.pcministry.com/asm/mbr/MSWIN41.htm
// http://sourceforge.net/projects/grub4dos/ (bootable NTFS?)

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"
#include "sys_types.h"

static const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "exFAT" };
// Don't ask me - just following the MS standard here
static const char* ClusterSizeLabel[] = { "512 bytes", "1024 bytes","2048 bytes","4096 bytes","8192 bytes",
	"16 kilobytes", "32 kilobytes", "64 kilobytes", "128 kilobytes", "256 kilobytes", "512 kilobytes",
	"1024 kilobytes","2048 kilobytes","4096 kilobytes","8192 kilobytes","16 megabytes","32 megabytes" };

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH];
float fScale = 1.0f;
int default_fs;
HWND hDeviceList, hCapacity, hFileSystem, hClusterSize, hLabel;

static HWND hDeviceTooltip = NULL, hFSTooltip = NULL;
static StrArray DriveID, DriveLabel;

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


#define KB          1024LL
#define MB       1048576LL
#define GB    1073741824LL
#define TB 1099511627776LL
/* 
 * Set cluster size values according to http://support.microsoft.com/kb/140365
 * this call will return FALSE if we can't find a supportable FS for the drive
 */
static BOOL DefineClusterSizes(void)
{
	LONGLONG i;
	int fs;
	BOOL r = FALSE;
	char tmp[64] = "";

	default_fs = FS_UNKNOWN;
	memset(&SelectedDrive.ClusterSize, 0, sizeof(SelectedDrive.ClusterSize));
	if (SelectedDrive.DiskSize < 8*MB) {
		// TODO: muck with FAT12 and Small FAT16 like Microsoft does
		uprintf("This application does not support volumes smaller than 8 MB yet\n");
		goto out;
	}

	// FAT 16
	if (SelectedDrive.DiskSize < 4*GB) {
		// TODO: Refine the following according to size
		SelectedDrive.ClusterSize[FS_FAT16].Allowed = 0x0001FE00;
		for (i=32; i<=4096; i<<=1) {			// 8 MB -> 4 GB
			if (SelectedDrive.DiskSize < i*MB) {
				SelectedDrive.ClusterSize[FS_FAT16].Default = 16*(ULONG)i;
				break;
			}
		}
	}

	// FAT 32
	if (SelectedDrive.DiskSize < 256*MB) {
		// TODO: Refine the following according to size
		SelectedDrive.ClusterSize[FS_FAT32].Allowed = 0x0001FE00;
		for (i=64; i<=256; i<<=1) {				// 8 MB -> 256 MB
			if (SelectedDrive.DiskSize < i*MB) {
				SelectedDrive.ClusterSize[FS_FAT32].Default = 8*(ULONG)i;
				break;
			}
		}
	} else if (SelectedDrive.DiskSize < 32*GB) {
		SelectedDrive.ClusterSize[FS_FAT32].Allowed = 0x0001FE00;
		for (i=8; i<=32; i<<=1) {				// 256 MB -> 32 GB
			if (SelectedDrive.DiskSize < i*GB) {
				SelectedDrive.ClusterSize[FS_FAT32].Default = ((ULONG)i/2)*1024;
				break;
			}
		}
	}

	// NTFS
	if (SelectedDrive.DiskSize < 256*TB) {
		SelectedDrive.ClusterSize[FS_NTFS].Allowed = 0x0001FE00;
		for (i=16; i<=256; i<<=1) {				// 7 MB -> 256 TB
			if (SelectedDrive.DiskSize < i*TB) {
				SelectedDrive.ClusterSize[FS_NTFS].Default = ((ULONG)i/4)*1024;
				break;
			}
		}
	}

	// exFAT
	if (SelectedDrive.DiskSize < 256*TB) {
		SelectedDrive.ClusterSize[FS_EXFAT].Allowed = 0x03FFFE00;
		if (SelectedDrive.DiskSize < 256*MB)	// < 256 MB
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 4*1024;
		else if (SelectedDrive.DiskSize < 32*GB)	// < 32 GB
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 32*1024;
		else
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 28*1024;
	}

out:
	// Only add the filesystems we can service
	for (fs=0; fs<FS_MAX; fs++) {
		if (SelectedDrive.ClusterSize[fs].Allowed != 0) {
			safe_sprintf(tmp, sizeof(tmp), FileSystemLabel[fs]);
			if (default_fs == FS_UNKNOWN) {
				safe_strcat(tmp, sizeof(tmp), " (Default)");
				default_fs = fs;
			}
			IGNORE_RETVAL(ComboBox_SetItemData(hFileSystem, 
				ComboBox_AddStringU(hFileSystem, tmp), fs));
			r = TRUE;
		}
	}

	return r;
}
#undef KB
#undef MB
#undef GB
#undef TB

/*
 * Populate the Allocation unit size field
 */
static BOOL SetClusterSizes(int FSType)
{
	char szDefault[64];
	int i;
	ULONG j;

	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));

	if ((FSType < 0) || (FSType >= FS_MAX)) {
		uprintf("Invalid FS value passed to SetClusterSizes\n");
		return FALSE;
	}

	if ( (SelectedDrive.ClusterSize[FSType].Allowed == 0)
	  || (SelectedDrive.ClusterSize[FSType].Default == 0) ) {
		uprintf("The drive is incompatible with FS type #%d\n", FSType);
		return FALSE;
	}

	// Yes, I know there exist more efficient ways...
	for(i=0,j=SelectedDrive.ClusterSize[FSType].Default>>10;j;i++,j>>=1);
	safe_sprintf(szDefault, sizeof(szDefault), "Default allocation size (%s)", ClusterSizeLabel[i]);

	IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, szDefault),
		SelectedDrive.ClusterSize[FSType].Default));

	for(i=0,j=0x200;j<0x10000000;i++,j<<=1) {
		if (j & SelectedDrive.ClusterSize[FSType].Allowed) {
			IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, ClusterSizeLabel[i]), j));
		}
	}

	IGNORE_RETVAL(ComboBox_SetCurSel(hClusterSize, 0));
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

	if (!DefineClusterSizes()) {
		uprintf("no file system is selectable for this drive\n");
		return FALSE;
	}

	// re-select existing FS if it's one we know
	if (GetVolumeInformationA(DrivePath, NULL, 0, NULL, NULL, NULL,
		fs_type, sizeof(fs_type))) {
		for (SelectedDrive.FSType=FS_MAX-1; SelectedDrive.FSType>=0; SelectedDrive.FSType--) {
			if (safe_strcmp(fs_type, FileSystemLabel[SelectedDrive.FSType]) == 0) {
				break;
			}
		}
	} else {
		SelectedDrive.FSType = FS_UNKNOWN;
	}

	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		if (ComboBox_GetItemData(hFileSystem, i) == SelectedDrive.FSType) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
			break;
		}
	}

	if (i == ComboBox_GetCount(hFileSystem)) {
		// failed to reselect => pick default
		for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
			if (ComboBox_GetItemData(hFileSystem, i) == default_fs) {
				IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
				break;
			}
		}
	}

	// At least one filesystem is go => enable formatting
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), TRUE);

	return SetClusterSizes((int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)));
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	double HumanReadableSize;
	char capacity[64];
	static char *suffix[] = { "KB", "MB", "GB", "TB", "PB"};
	char proposed_label[16], no_label[] = STR_NO_LABEL;
	int i;

	IGNORE_RETVAL(ComboBox_ResetContent(hCapacity));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), FALSE);
	SetWindowTextA(hLabel, "");
	DestroyTooltip(hDeviceTooltip);
	DestroyTooltip(hFSTooltip);
	hDeviceTooltip = NULL;
	hFSTooltip = NULL;
	memset(&SelectedDrive, 0, sizeof(SelectedDrive));

	if (ComboIndex < 0) {
		return TRUE;
	}

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
BOOL CreatePartition(HANDLE hDrive)
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
	case FS_EXFAT:
		// TODO: but how do we set this thing up afterwards?
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
	GUID _GUID_DEVINTERFACE_DISK =			// only known to some...
		{ 0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b} };

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));
	StrArrayClear(&DriveID);
	StrArrayClear(&DriveLabel);

	dev_info = SetupDiGetClassDevsA(&_GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
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

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &_GUID_DEVINTERFACE_DISK, j, &devint_data)) {
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

/* 
 * Toggle controls according to operation
 */
static void EnableControls(BOOL bEnable)
{
	int fs;

	EnableWindow(GetDlgItem(hMainDialog, IDC_DEVICE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CAPACITY), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), bEnable);
	if (bEnable) {
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOS), (fs == FS_FAT16) || (fs == FS_FAT32));
	} else {
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOS), FALSE);
	}
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), bEnable);
	SetDlgItemTextA(hMainDialog, IDCANCEL, bEnable?"Close":"Cancel");
}

/*
 * Main dialog callback
 */
#ifndef PBS_MARQUEE				// Some versions of MinGW don't know these
#define PBS_MARQUEE 0x08
#endif
#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	HICON hSmallIcon, hBigIcon;
	int nDeviceIndex, fs;
	DWORD DeviceNum;
	char str[MAX_PATH], tmp[128];
	static uintptr_t format_thid = -1L;
	static HWND hProgress, hDOS;
	static LONG ProgressStyle = 0;
	static UINT uDOSChecked = BST_CHECKED;

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
		hDOS = GetDlgItem(hDlg, IDC_DOS);
		// High DPI scaling
		hDC = GetDC(hDlg);
		fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
		ReleaseDC(hDlg, hDC);
		// Create the title bar icon
		hSmallIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, 0);
		SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
		hBigIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 32, 32, 0);
		SendMessage (hDlg, WM_SETICON, ICON_BIG, (LPARAM)hBigIcon);
		// Create the status line
		hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE,
			0, 0, 0, 0, hMainDialog, (HMENU)IDC_STATUS,  hMainInstance, NULL);
		// We'll switch the progressbar to marquee and back => keep a copy of current style
		ProgressStyle = GetWindowLong(hProgress, GWL_STYLE);
		// Create the string array
		StrArrayCreate(&DriveID, MAX_DRIVES);
		StrArrayCreate(&DriveLabel, MAX_DRIVES);
		// Set the quick format & create DOS disk checkboxes
		CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
		CheckDlgButton(hDlg, IDC_DOS, BST_CHECKED);
		GetUSBDevices();
		return (INT_PTR)TRUE;

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
		case IDC_FILESYSTEM:
			switch (HIWORD(wParam)) {
			case CBN_SELCHANGE:
				fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
				SetClusterSizes(fs);
				// Disable/Restore the DOS checkbox according to FS
				if ((fs == FS_FAT16) || (fs == FS_FAT32)) {
					if (!IsWindowEnabled(hDOS)) {
						EnableWindow(hDOS, TRUE);
						CheckDlgButton(hDlg, IDC_DOS, uDOSChecked);
					}
				} else {
					if (IsWindowEnabled(hDOS)) {
						uDOSChecked = IsDlgButtonChecked(hMainDialog, IDC_DOS);
						CheckDlgButton(hDlg, IDC_DOS, BST_UNCHECKED);
						EnableWindow(hDOS, FALSE);
					}
				}
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
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
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
		if (!IS_ERROR(FormatStatus)) {
			PrintStatus("DONE");
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			PrintStatus("Cancelled");
			Notification(MSG_INFO, "Cancelled", "Operation cancelled by the user.");
		} else {
			PrintStatus("FAILED");
			Notification(MSG_ERROR, "Error", "Error: %s", StrError(FormatStatus));
		}
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
