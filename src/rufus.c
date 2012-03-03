/*
 * Rufus: The Reliable USB Formatting Utility
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

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"
#include "sys_types.h"

static const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "exFAT" };
// Don't ask me - just following the MS standard here
static const char* ClusterSizeLabel[] = { "512 bytes", "1024 bytes","2048 bytes","4096 bytes","8192 bytes",
	"16 kilobytes", "32 kilobytes", "64 kilobytes", "128 kilobytes", "256 kilobytes", "512 kilobytes",
	"1024 kilobytes","2048 kilobytes","4096 kilobytes","8192 kilobytes","16 megabytes","32 megabytes" };
static BOOL existing_key = FALSE;	// For LGP set/restore
static BOOL iso_size_check = TRUE;
static int selection_default;
BOOL enable_fixed_disks = FALSE;

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH];
char* iso_path = NULL;
float fScale = 1.0f;
int default_fs;
HWND hDeviceList, hCapacity, hFileSystem, hClusterSize, hLabel, hDOSType, hNBPasses;
HWND hISOProgressDlg = NULL, hISOProgressBar, hISOFileName;
BOOL bWithFreeDOS;
extern char szStatusMessage[256];

static HANDLE format_thid = NULL;
static HWND hDeviceTooltip = NULL, hFSTooltip = NULL, hProgress = NULL;
static HWND hDOS = NULL, hSelectISO = NULL, hISOToolTip = NULL, hPassesToolTip = NULL;
static HICON hIconDisc;
static StrArray DriveID, DriveLabel;
static char szTimer[12] = "00:00:00";
static unsigned int timer;
static int64_t last_iso_blocking_status;

/*
 * The following is used to allocate slots within the progress bar
 * 0 means unused (no operation or no progress allocated to it)
 * +n means allocate exactly n bars (n percents of the progress bar)
 * -n means allocate a weighted slot of n from all remaining
 *    bars. Eg if 80 slots remain and the sum of all negative entries
 *    is 10, -4 will allocate 4/10*80 = 32 bars (32%) for OP progress
 */
static int nb_slots[OP_MAX];
static float slot_end[OP_MAX+1];	// shifted +1 so that we can substract 1 to OP indexes
static float previous_end = 0.0f;

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
		// TODO: muck with FAT12 and Small FAT16 like Microsoft does to support small drives?
		uprintf("This application does not support volumes smaller than 8 MB\n");
		goto out;
	}

/*
 * The following is MS's allowed cluster sizes for FAT16 and FAT32:
 *
 * FAT16
 * 31M  :  512 - 4096
 * 63M  : 1024 - 8192
 * 127M : 2048 - 16k
 * 255M : 4096 - 32k
 * 511M : 8192 - 64k
 * 1023M:  16k - 64k
 * 2047M:  32k - 64k
 * 4095M:  64k
 * 4GB+ : N/A
 *
 * FAT32
 * 31M  : N/A
 * 63M  : N/A			(NB unlike MS, we're allowing 512-512 here - UNTESTED)
 * 127M :  512 - 1024
 * 255M :  512 - 2048
 * 511M :  512 - 4096
 * 1023M:  512 - 8192
 * 2047M:  512 - 16k
 * 4095M: 1024 - 32k
 * 7GB  : 2048 - 64k
 * 15GB : 4096 - 64k
 * 31GB : 8192 - 64k
 * 32GB+: possible but N/A from Microsoft (see below)
 */

	// FAT 16
	if (SelectedDrive.DiskSize < 4*GB) {
		SelectedDrive.ClusterSize[FS_FAT16].Allowed = 0x00001E00;
		for (i=32; i<=4096; i<<=1) {			// 8 MB -> 4 GB
			if (SelectedDrive.DiskSize < i*MB) {
				SelectedDrive.ClusterSize[FS_FAT16].Default = 16*(ULONG)i;
				break;
			}
			SelectedDrive.ClusterSize[FS_FAT16].Allowed <<= 1;
		}
		SelectedDrive.ClusterSize[FS_FAT16].Allowed &= 0x0001FE00;
	}

	// FAT 32
	// > 32GB FAT32 is not supported by MS (and likely FormatEx) but is feasible
	// See: http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm
	// < 32 MB FAT32 is not allowed by FormatEx
	if ((SelectedDrive.DiskSize >= 32*MB) && (SelectedDrive.DiskSize < 32*GB)) {
		SelectedDrive.ClusterSize[FS_FAT32].Allowed = 0x000001F8;
		for (i=32; i<=(32*1024); i<<=1) {			// 32 MB -> 32 GB
			if (SelectedDrive.DiskSize < i*MB) {
				SelectedDrive.ClusterSize[FS_FAT32].Default = 8*(ULONG)i;
				break;
			}
			SelectedDrive.ClusterSize[FS_FAT32].Allowed <<= 1;
		}
		SelectedDrive.ClusterSize[FS_FAT32].Allowed &= 0x0001FE00;

		// Default cluster sizes in the 256MB to 32 GB range do not follow the rule above
		if (SelectedDrive.DiskSize >= 256*MB) {
			for (i=8; i<=32; i<<=1) {				// 256 MB -> 32 GB
				if (SelectedDrive.DiskSize < i*GB) {
					SelectedDrive.ClusterSize[FS_FAT32].Default = ((ULONG)i/2)*1024;
					break;
				}
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
	char szClustSize[64];
	int i, k, default_index = 0;
	ULONG j;

	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));

	if ((FSType < 0) || (FSType >= FS_MAX)) {
		return FALSE;
	}

	if ( (SelectedDrive.ClusterSize[FSType].Allowed == 0)
	  || (SelectedDrive.ClusterSize[FSType].Default == 0) ) {
		uprintf("The drive is incompatible with FS type #%d\n", FSType);
		return FALSE;
	}

	for(i=0,j=0x200,k=0;j<0x10000000;i++,j<<=1) {
		if (j & SelectedDrive.ClusterSize[FSType].Allowed) {
			safe_sprintf(szClustSize, sizeof(szClustSize), "%s", ClusterSizeLabel[i]);
			if (j == SelectedDrive.ClusterSize[FSType].Default) {
				safe_strcat(szClustSize, sizeof(szClustSize), " (Default)");
				default_index = k;
			}
			IGNORE_RETVAL(ComboBox_SetItemData(hClusterSize, ComboBox_AddStringU(hClusterSize, szClustSize), j));
			k++;
		}
	}

	IGNORE_RETVAL(ComboBox_SetCurSel(hClusterSize, default_index));
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

static void SetFSFromISO(void)
{
	int i, fs, selected_fs = FS_UNKNOWN;
	uint32_t fs_mask = 0;

	if (iso_path == NULL)
		return;

	// Create a mask of all the FS's available
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs = (int)ComboBox_GetItemData(hFileSystem, i);
		fs_mask |= 1<<fs;
	}

	// Syslinux has precedence over bootmgr
	if (iso_report.has_isolinux) {
		if (fs_mask & (1<<FS_FAT32)) {
			selected_fs = FS_FAT32;
		} else if (fs_mask & (1<<FS_FAT16)) {
			selected_fs = FS_FAT16;
		}
	} else if (iso_report.has_bootmgr) {
		if (fs_mask & (1<<FS_NTFS)) {
			selected_fs = FS_NTFS;
		}
	}

	// Try to select the FS
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs = (int)ComboBox_GetItemData(hFileSystem, i);
		if (fs == selected_fs)
			IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
	}

	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
		ComboBox_GetCurSel(hFileSystem));
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	double HumanReadableSize;
	char capacity[64];
	static char *suffix[] = { "KB", "MB", "GB", "TB", "PB"};
	char no_label[] = STR_NO_LABEL;
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
	if (!GetDriveInfo())	// This also populates FS
		return FALSE;
	SetFSFromISO();

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

	// Set a proposed label according to the size (eg: "256MB", "8GB")
	if (HumanReadableSize < 1.0) {
		HumanReadableSize *= 1024.0;
		i--;
	}
	// If we're beneath the tolerance, round proposed label to an integer, if not, show one decimal point
	if (fabs(HumanReadableSize / ceil(HumanReadableSize) - 1.0) < PROPOSEDLABEL_TOLERANCE) {
		safe_sprintf(SelectedDrive.proposed_label, sizeof(SelectedDrive.proposed_label),
			"%0.0f%s", ceil(HumanReadableSize), suffix[i]);
	} else {
		safe_sprintf(SelectedDrive.proposed_label, sizeof(SelectedDrive.proposed_label),
			"%0.1f%s", HumanReadableSize, suffix[i]);
	}

	// If no existing label is available and no ISO is selected, propose one according to the size (eg: "256MB", "8GB")
	if ((iso_path == NULL) || (iso_report.label[0] == 0)) {
		if (safe_strcmp(no_label, DriveLabel.Table[ComboIndex]) == 0) {
			SetWindowTextU(hLabel, SelectedDrive.proposed_label);
		} else {
			SetWindowTextU(hLabel, DriveLabel.Table[ComboIndex]);
		}
	} else {
		SetWindowTextU(hLabel, iso_report.label);
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

	PrintStatus(0, TRUE, "Partitioning...");
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
	switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
	case FS_FAT16:
		DriveLayoutEx->PartitionEntry[0].Mbr.PartitionType = 0x0e;	// FAT16 LBA
		break;
	case FS_NTFS:
	case FS_EXFAT:
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
static BOOL GetUSBDevices(DWORD devnum)
{
	BOOL r, found = FALSE;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	STORAGE_DEVICE_NUMBER_REDEF device_number;
	DWORD size, i, j, datatype;
	HANDLE hDrive;
	LONG maxwidth = 0;
	RECT rect;
	char drive_letter;
	char *label, entry[MAX_PATH], buffer[MAX_PATH];
	const char* usbstor_name = "USBSTOR";
	GUID _GUID_DEVINTERFACE_DISK =			// only known to some...
		{ 0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b} };

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));
	StrArrayClear(&DriveID);
	StrArrayClear(&DriveLabel);
	GetClientRect(hDeviceList, &rect);

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
				maxwidth = max(maxwidth, GetEntryWidth(hDeviceList, entry));
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}
		}
	}
	SetupDiDestroyDeviceInfoList(dev_info);

	// Adjust the Dropdown width to the maximum text size
	SendMessage(hDeviceList, CB_SETDROPPEDWIDTH, (WPARAM)maxwidth, 0);

	if (devnum >= DRIVE_INDEX_MIN) {
		for (i=0; i<ComboBox_GetCount(hDeviceList); i++) {
			if ((DWORD)ComboBox_GetItemData(hDeviceList, i) == devnum) {
				found = TRUE;
				break;
			}
		}
	}
	if (!found)
		i = 0;
	IGNORE_RETVAL(ComboBox_SetCurSel(hDeviceList, i));
	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_DEVICE, 0);
	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
		ComboBox_GetCurSel(hFileSystem));
	return TRUE;
}

/*
 * Set up progress bar real estate allocation
 */
static void InitProgress(void)
{
	int i;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	memset(&nb_slots, 0, sizeof(nb_slots));
	memset(&slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	nb_slots[OP_ZERO_MBR] = 1;
	if (IsChecked(IDC_BADBLOCKS)) {
		nb_slots[OP_BADBLOCKS] = -1;
	}
	if (IsChecked(IDC_DOS)) {
		// 1 extra slot for PBR writing
		switch (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType))) {
		case DT_WINME:
			nb_slots[OP_DOS] = 4+1;
			break;
		case DT_FREEDOS:
			nb_slots[OP_DOS] = 6+1;
			break;
		default:
			nb_slots[OP_DOS] = 3+1;
			break;
		}
	}
	nb_slots[OP_PARTITION] = 1;
	nb_slots[OP_FIX_MBR] = 1;
	nb_slots[OP_CREATE_FS] = 
		nb_steps[ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))];
	if (!IsChecked(IDC_QUICKFORMAT)) {
		nb_slots[OP_FORMAT] = -1;
	}

	for (i=0; i<OP_MAX; i++) {
		if (nb_slots[i] > 0) {
			slots_discrete += nb_slots[i]*1.0f;
		}
		if (nb_slots[i] < 0) {
			slots_analog += nb_slots[i]*1.0f;
		}
	}

	for (i=0; i<OP_MAX; i++) {
		if (nb_slots[i] == 0) {
			slot_end[i+1] = last_end;
		} else if (nb_slots[i] > 0) {
			slot_end[i+1] = last_end + (1.0f * nb_slots[i]);
		} else if (nb_slots[i] < 0) {
			slot_end[i+1] = last_end + (( (100.0f-slots_discrete) * nb_slots[i]) / slots_analog);
		}
		last_end = slot_end[i+1];
	}

	/* Is there's no analog, adjust our discrete ends to fill the whole bar */
	if (slots_analog == 0.0f) {
		for (i=0; i<OP_MAX; i++) {
			slot_end[i+1] *= 100.0f / slots_discrete;
		}
	}
}

/*
 * Position the progress bar within each operation range
 */
void UpdateProgress(int op, float percent)
{
	int pos;

	if ((op < 0) || (op > OP_MAX)) {
		uprintf("UpdateProgress: invalid op %d\n", op);
		return;
	}
	if (percent > 100.1f) {
		uprintf("UpdateProgress(%d): invalid percentage %0.2f\n", op, percent);
		return;
	}
	if ((percent < 0.0f) && (nb_slots[op] <= 0)) {
		uprintf("UpdateProgress(%d): error negative percentage sent for negative slot value\n", op);
		return;
	}
	if (nb_slots[op] == 0)
		return;
	if (previous_end < slot_end[op]) {
		previous_end = slot_end[op];
	}

	if (percent < 0.0f) {
		// Negative means advance one slot (1.0%) - requires a positive slot allocation
		previous_end += (slot_end[op+1] - slot_end[op]) / (1.0f * nb_slots[op]);
		pos = (int)(previous_end / 100.0f * MAX_PROGRESS);
	} else {
		pos = (int)((previous_end + ((slot_end[op+1] - previous_end) * (percent / 100.0f))) / 100.0f * MAX_PROGRESS);
	}
	if (pos > MAX_PROGRESS) {
		uprintf("UpdateProgress(%d): rounding error - pos %d is greater than %d\n", op, pos, MAX_PROGRESS);
		pos = MAX_PROGRESS;
	}

	SendMessage(hProgress, PBM_SETPOS, (WPARAM)pos, 0);
}

/*
 * Set or restore a Local Group Policy DWORD key indexed by szPath/SzPolicy
 */
#pragma push_macro("INTERFACE")
#undef  INTERFACE
#define INTERFACE IGroupPolicyObject
#define REGISTRY_EXTENSION_GUID { 0x35378EAC, 0x683F, 0x11D2, {0xA8, 0x9A, 0x00, 0xC0, 0x4F, 0xBB, 0xCF, 0xA2} }
#define GPO_OPEN_LOAD_REGISTRY  1
#define GPO_SECTION_MACHINE     2
typedef enum _GROUP_POLICY_OBJECT_TYPE {
	GPOTypeLocal = 0, GPOTypeRemote, GPOTypeDS
} GROUP_POLICY_OBJECT_TYPE, *PGROUP_POLICY_OBJECT_TYPE;
DECLARE_INTERFACE_(IGroupPolicyObject, IUnknown) {
	STDMETHOD(QueryInterface) (THIS_ REFIID riid, LPVOID *ppvObj) PURE;
	STDMETHOD_(ULONG, AddRef) (THIS) PURE;
	STDMETHOD_(ULONG, Release) (THIS) PURE;
	STDMETHOD(New) (THIS_ LPOLESTR pszDomainName, LPOLESTR pszDisplayName, DWORD dwFlags) PURE;
	STDMETHOD(OpenDSGPO) (THIS_ LPOLESTR pszPath, DWORD dwFlags) PURE;
	STDMETHOD(OpenLocalMachineGPO) (THIS_ DWORD dwFlags) PURE;
	STDMETHOD(OpenRemoteMachineGPO) (THIS_ LPOLESTR pszComputerName, DWORD dwFlags) PURE;
	STDMETHOD(Save) (THIS_ BOOL bMachine, BOOL bAdd,GUID *pGuidExtension, GUID *pGuid) PURE;
	STDMETHOD(Delete) (THIS) PURE;
	STDMETHOD(GetName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetDisplayName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(SetDisplayName) (THIS_ LPOLESTR pszName) PURE;
	STDMETHOD(GetPath) (THIS_ LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetDSPath) (THIS_ DWORD dwSection, LPOLESTR pszPath ,int cchMaxPath) PURE;
	STDMETHOD(GetFileSysPath) (THIS_ DWORD dwSection, LPOLESTR pszPath, int cchMaxPath) PURE;
	STDMETHOD(GetRegistryKey) (THIS_ DWORD dwSection, HKEY *hKey) PURE;
	STDMETHOD(GetOptions) (THIS_ DWORD *dwOptions) PURE;
	STDMETHOD(SetOptions) (THIS_ DWORD dwOptions, DWORD dwMask) PURE;
	STDMETHOD(GetType) (THIS_ GROUP_POLICY_OBJECT_TYPE *gpoType) PURE;
	STDMETHOD(GetMachineName) (THIS_ LPOLESTR pszName, int cchMaxLength) PURE;
	STDMETHOD(GetPropertySheetPages) (THIS_ HPROPSHEETPAGE **hPages, UINT *uPageCount) PURE;
};
typedef IGroupPolicyObject *LPGROUPPOLICYOBJECT;

BOOL SetLGP(BOOL bRestore, const char* szPath, const char* szPolicy, DWORD dwValue)
{
	LONG r;
	DWORD disp, regtype, val=0, val_size=sizeof(DWORD);
	HRESULT hr;
	IGroupPolicyObject* pLGPO;
	// Along with global 'existing_key', this static value is used to restore initial state
	static DWORD original_val;
	HKEY path_key = NULL, policy_key = NULL;
	// MSVC is finicky about these ones => redefine them
	const IID my_IID_IGroupPolicyObject = 
		{ 0xea502723, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	const IID my_CLSID_GroupPolicyObject = 
		{ 0xea502722, 0xa23d, 0x11d1, { 0xa7, 0xd3, 0x0, 0x0, 0xf8, 0x75, 0x71, 0xe3 } };
	GUID ext_guid = REGISTRY_EXTENSION_GUID;
	// Can be anything really
	GUID snap_guid = { 0x3D271CFC, 0x2BC6, 0x4AC2, {0xB6, 0x33, 0x3B, 0xDF, 0xF5, 0xBD, 0xAB, 0x2A} };

	// We need an IGroupPolicyObject instance to set a Local Group Policy
	hr = CoCreateInstance(&my_CLSID_GroupPolicyObject, NULL, CLSCTX_INPROC_SERVER, &my_IID_IGroupPolicyObject, (LPVOID*)&pLGPO);
	if (FAILED(hr)) {
		uprintf("SetLGP: CoCreateInstance failed; hr = %x\n", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->OpenLocalMachineGPO(pLGPO, GPO_OPEN_LOAD_REGISTRY);
	if (FAILED(hr)) {
		uprintf("SetLGP: OpenLocalMachineGPO failed - error %x\n", hr);
		goto error;
	}

	hr = pLGPO->lpVtbl->GetRegistryKey(pLGPO, GPO_SECTION_MACHINE, &path_key);
	if (FAILED(hr)) {
		uprintf("SetLGP: GetRegistryKey failed - error %x\n", hr);
		goto error;
	}

	// The DisableSystemRestore is set in Software\Policies\Microsoft\Windows\DeviceInstall\Settings
	r = RegCreateKeyExA(path_key, szPath, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE,
		NULL, &policy_key, &disp);
	if (r != ERROR_SUCCESS) {
		uprintf("SetLGP: Failed to open LGPO path %s - error %x\n", szPath, hr);
		goto error;
	}

	if ((disp == REG_OPENED_EXISTING_KEY) && (!bRestore) && (!existing_key)) {
		// backup existing value for restore
		existing_key = TRUE;
		regtype = REG_DWORD;
		r = RegQueryValueExA(policy_key, szPolicy, NULL, &regtype, (LPBYTE)&original_val, &val_size);
		if (r == ERROR_FILE_NOT_FOUND) {
			// The Key exists but not its value, which is OK
			existing_key = FALSE;
		} else if (r != ERROR_SUCCESS) {
			uprintf("SetLGP: Failed to read original %s policy value - error %x\n", szPolicy, r);
		}
	}

	if ((!bRestore) || (existing_key)) {
		val = (bRestore)?original_val:dwValue;
		r = RegSetValueExA(policy_key, szPolicy, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
	} else {
		r = RegDeleteValueA(policy_key, szPolicy);
	}
	if (r != ERROR_SUCCESS) {
		uprintf("SetLGP: RegSetValueEx / RegDeleteValue failed - error %x\n", r);
	}
	RegCloseKey(policy_key);
	policy_key = NULL;

	// Apply policy
	hr = pLGPO->lpVtbl->Save(pLGPO, TRUE, (bRestore)?FALSE:TRUE, &ext_guid, &snap_guid);
	if (r != S_OK) {
		uprintf("SetLGP: Unable to apply %s policy - error %x\n", szPolicy, hr);
		goto error;
	} else {
		if ((!bRestore) || (existing_key)) {
			uprintf("SetLGP: Successfully %s %s policy to 0x%08X\n", (bRestore)?"restored":"set", szPolicy, val);
		} else {
			uprintf("SetLGP: Successfully removed %s policy key\n", szPolicy);
		}
	}

	RegCloseKey(path_key);
	pLGPO->lpVtbl->Release(pLGPO);
	return TRUE;

error:
	if (path_key != NULL) RegCloseKey(path_key);
	if (policy_key != NULL) RegCloseKey(policy_key);
	if (pLGPO != NULL) pLGPO->lpVtbl->Release(pLGPO);
	return FALSE;
}
#pragma pop_macro("INTERFACE")

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
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOS), (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOSTYPE), (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
	} else {
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOS), FALSE);
		EnableWindow(GetDlgItem(hMainDialog, IDC_DOSTYPE), FALSE);
	}
	EnableWindow(GetDlgItem(hMainDialog, IDC_BADBLOCKS), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SELECT_ISO), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_NBPASSES), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), bEnable);
	SetDlgItemTextA(hMainDialog, IDCANCEL, bEnable?"Close":"Cancel");
}

/*
 * Timer in the right part of the status area
 */
static void CALLBACK ClockTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	timer++;
	safe_sprintf(szTimer, sizeof(szTimer), "%02d:%02d:%02d",
		timer/3600, (timer%3600)/60, timer%60);
	SendMessageA(GetDlgItem(hWnd, IDC_STATUS), SB_SETTEXTA, SBT_OWNERDRAW | 1, (LPARAM)szTimer);
}

/*
 * Detect and notify about a blocking operation during ISO extraction cancellation
 */
static void CALLBACK BlockingTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	static BOOL user_notified = FALSE;
	if (iso_blocking_status < 0) {
		KillTimer(hMainDialog, TID_BLOCKING_TIMER);
		user_notified = FALSE;
		uprintf("Killed blocking I/O timer\n");
	} else if(!user_notified) {
		if (last_iso_blocking_status == iso_blocking_status) {
		// A write or close operation hasn't made any progress since our last check
		user_notified = TRUE;
		uprintf("Blocking I/O operation detected\n");
		MessageBoxU(hMainDialog,
			"Rufus detected that Windows is still flushing its internal buffers\n"
			"onto the USB device.\n\n"
			"Depending on the speed of your USB device, this operation may\n"
			"take a long time to complete, especially for large files.\n\n"
			"We recommend that you let Windows finish, to avoid corruption.\n"
			"But if you grow tired of waiting, you can just unplug the device...",
			RUFUS_BLOCKING_IO_TITLE, MB_OK|MB_ICONINFORMATION);
		} else {
			last_iso_blocking_status = iso_blocking_status;
		}
	}
}

/* Callback for the modeless ISO extraction progress */
BOOL CALLBACK ISOProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) 
{
	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		hISOProgressBar = GetDlgItem(hDlg, IDC_ISO_PROGRESS);
		hISOFileName = GetDlgItem(hDlg, IDC_ISO_FILENAME);
		// Use maximum granularity for the progress bar
		SendMessage(hISOProgressBar, PBM_SETRANGE, 0, MAX_PROGRESS<<16);
		return TRUE;
	case UM_ISO_EXIT:
		DestroyWindow(hDlg);
		hISOProgressDlg = NULL;
		return TRUE;
	case WM_COMMAND: 
		switch (LOWORD(wParam)) {
		case IDC_ISO_ABORT:
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			PrintStatus(0, FALSE, "Cancelling - Please wait...");
			uprintf("Cancelling (from ISO proc.)\n");
			EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), FALSE);
			EnableWindow(GetDlgItem(hMainDialog, IDCANCEL), FALSE);
			//  Start a timer to detect blocking operations during ISO file extraction
			if (iso_blocking_status >= 0) {
				last_iso_blocking_status = iso_blocking_status;
				SetTimer(hMainDialog, TID_BLOCKING_TIMER, 5000, BlockingTimer);
			}
			return TRUE;
		}
	case WM_CLOSE:		// prevent closure using Alt-F4
		return TRUE;
	}
	return FALSE; 
} 

// The scanning process can be blocking for message processing => use a thread
DWORD WINAPI ISOScanThread(LPVOID param)
{
	int i;

	if (iso_path == NULL)
		goto out;
	PrintStatus(0, TRUE, "Scanning ISO image...\n");
	if (!ExtractISO(iso_path, "", TRUE)) {
		PrintStatus(0, TRUE, "Failed to scan ISO image.");
		safe_free(iso_path);
		goto out;
	}
	uprintf("ISO label: '%s'\n size: %lld bytes, 4GB:%c, bootmgr:%c, isolinux:%c\n",
		iso_report.label, iso_report.projected_size,
		iso_report.has_4GB_file?'Y':'N', iso_report.has_bootmgr?'Y':'N', iso_report.has_isolinux?'Y':'N');
	if ((!iso_report.has_bootmgr) && (!iso_report.has_isolinux)) {
		MessageBoxU(hMainDialog, "This version of Rufus only supports bootable ISOs\n"
			"based on 'bootmgr' or 'isolinux'.\n"
			"This ISO image doesn't appear to use either...", "Unsupported ISO", MB_OK|MB_ICONINFORMATION);
		safe_free(iso_path);
	} else {
		// Enable DOS, set DOS Type to ISO (last item) and set FS accordingly
		CheckDlgButton(hMainDialog, IDC_DOS, BST_CHECKED);
		SetFSFromISO();
		for (i=(int)safe_strlen(iso_path); (i>0)&&(iso_path[i]!='\\'); i--);
		PrintStatus(0, TRUE, "Using ISO: %s\n", &iso_path[i+1]);
		// Some Linux distros, such as Arch Linux, require the USB drive to have
		// a specific label => copy the one we got from the ISO image
		if (iso_report.label[0] != 0) {
			SetWindowTextU(hLabel, iso_report.label);
		}
		// Lose the focus on the select ISO (but place it on Close)
		SendMessage(hMainDialog, WM_NEXTDLGCTL,  (WPARAM)FALSE, 0);
		// Lose the focus from Close and set it back to Start
		SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
	}

out:
	ExitThread(0);
}

// Helper function to obtain a handle to a DLL
static __inline HMODULE GetDLLHandle(char* szDLLName)
{
	HMODULE h = NULL;
	if ((h = GetModuleHandleA(szDLLName)) == NULL)
		h = LoadLibraryA(szDLLName);
	return h;
}

void InitDialog(HWND hDlg)
{
	// MinGW fails to link those
	typedef HIMAGELIST (WINAPI *ImageList_Create_t)(
		int cx,
		int cy,
		UINT flags,
		int cInitial,
		int cGrow
	);
	ImageList_Create_t pImageList_Create = NULL;
	typedef int (WINAPI *ImageList_ReplaceIcon_t)(
		HIMAGELIST himl,
		int i,
		HICON hicon
	);
	ImageList_ReplaceIcon_t pImageList_ReplaceIcon = NULL;
	struct {
		HIMAGELIST himl;
		RECT margin;
		UINT uAlign;
	} bi = {0};	// BUTTON_IMAGELIST
	HINSTANCE hDllInst;
	HDC hDC;
	int i16;
	HICON hSmallIcon, hBigIcon;
	char tmp[128];

#ifdef RUFUS_TEST
	ShowWindow(GetDlgItem(hDlg, IDC_TEST), SW_SHOW);
#endif

	// Quite a burden to carry around as parameters
	hMainDialog = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
	hCapacity = GetDlgItem(hDlg, IDC_CAPACITY);
	hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
	hClusterSize = GetDlgItem(hDlg, IDC_CLUSTERSIZE);
	hLabel = GetDlgItem(hDlg, IDC_LABEL);
	hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
	hDOS = GetDlgItem(hDlg, IDC_DOS);
	hDOSType = GetDlgItem(hDlg, IDC_DOSTYPE);
	hSelectISO = GetDlgItem(hDlg, IDC_SELECT_ISO);
	hNBPasses = GetDlgItem(hDlg, IDC_NBPASSES);

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hDC = GetDC(hDlg);
	fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
	ReleaseDC(hDlg, hDC);

	// Create the title bar icon
	hSmallIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 16, 16, 0);
	SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
	hBigIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, 32, 32, 0);
	SendMessage (hDlg, WM_SETICON, ICON_BIG, (LPARAM)hBigIcon);
	// Update the title if we have FreeDOS support
	if (bWithFreeDOS) {
		GetWindowTextA(hDlg, &tmp[15], sizeof(tmp)-15);
		safe_sprintf(tmp, sizeof(tmp), "Rufus (with FreeDOS)");
		tmp[20] = ' ';
		SetWindowTextA(hDlg, tmp);
		selection_default = DT_FREEDOS;
	} else {
		selection_default = DT_WINME;
	}

	// Create the status line
	CreateStatusBar();
	// Use maximum granularity for the progress bar
	SendMessage(hProgress, PBM_SETRANGE, 0, MAX_PROGRESS<<16);
	// Fill up the passes
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "1 Pass"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "2 Passes"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "3 Passes"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "4 Passes"));
	IGNORE_RETVAL(ComboBox_SetCurSel(hNBPasses, 1));
	hPassesToolTip = CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA", -1);
	// Fill up the DOS type dropdown
	IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "MS-DOS"), DT_WINME));
	IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, DT_WINME));
	// Create the string array
	StrArrayCreate(&DriveID, MAX_DRIVES);
	StrArrayCreate(&DriveLabel, MAX_DRIVES);
	// Set the Quick format, Create bootable and Set icon checkboxes
	CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_DOS, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_SET_ICON, BST_CHECKED);

	// Load system icons (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hDllInst = LoadLibraryA("shell32.dll");
	hIconDisc = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(12), IMAGE_ICON, i16, i16, LR_DEFAULTCOLOR|LR_SHARED);

	// Set a disc icon on the select ISO button
	pImageList_Create = (ImageList_Create_t) GetProcAddress(GetDLLHandle("Comctl32.dll"), "ImageList_Create");
	pImageList_ReplaceIcon = (ImageList_ReplaceIcon_t) GetProcAddress(GetDLLHandle("Comctl32.dll"), "ImageList_ReplaceIcon");

	bi.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi.himl, -1, hIconDisc);
	SetRect(&bi.margin, 0, 1, 0, 0);
	bi.uAlign = 4;	// BUTTON_IMAGELIST_ALIGN_CENTER
	SendMessage(GetDlgItem(hDlg, IDC_SELECT_ISO), 0x1602, 0, (LPARAM)&bi);	// BCM_SETIMAGELIST 

	hISOToolTip = CreateTooltip(hSelectISO, "Click to select...", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_SET_ICON), "Create an autorun.inf on the target drive, to set the icon. "
		"Also allow the display of non-English labels.", 10000);
}

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	DRAWITEMSTRUCT* pDI;
	int nDeviceIndex, fs, i;
	static DWORD DeviceNum = 0;
	wchar_t wtmp[128], wstr[MAX_PATH];
	static UINT uDOSChecked = BST_CHECKED;

	switch (message) {

	case WM_DEVICECHANGE:
		if ( (format_thid == NULL) &&
			 ((wParam == DBT_DEVICEARRIVAL) || (wParam == DBT_DEVICEREMOVECOMPLETE)) ) {
			GetUSBDevices((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
			return (INT_PTR)TRUE;
		}
		break;

	case WM_INITDIALOG:
		InitDialog(hDlg);
		GetUSBDevices(0);
		return (INT_PTR)TRUE;

	// The things one must do to get an ellipsis on the status bar...
	case WM_DRAWITEM:
		if (wParam == IDC_STATUS) {
			pDI = (DRAWITEMSTRUCT*)lParam;
			pDI->rcItem.top += (int)(2.0f * fScale);
			pDI->rcItem.left += (int)(4.0f * fScale);
			SetBkMode(pDI->hDC, TRANSPARENT);
			switch(pDI->itemID) {
			case 0:	// left part
				DrawTextExU(pDI->hDC, szStatusMessage, -1, &pDI->rcItem,
					DT_LEFT|DT_END_ELLIPSIS, NULL);
				return (INT_PTR)TRUE;
			case 1:	// right part
				SetTextColor(pDI->hDC, GetSysColor(COLOR_3DSHADOW));
				DrawTextExA(pDI->hDC, szTimer, -1, &pDI->rcItem, DT_LEFT, NULL);
				return (INT_PTR)TRUE;
			}
		}
		break;

	case WM_COMMAND:
		switch(LOWORD(wParam)) {
		case IDOK:			// close application
		case IDCANCEL:
			EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);
			if (format_thid != NULL) {
				if (MessageBoxA(hMainDialog, "Cancelling may leave the device in an UNUSABLE state.\r\n"
					"If you are sure you want to cancel, click YES. Otherwise, click NO.",
					RUFUS_CANCELBOX_TITLE, MB_YESNO|MB_ICONWARNING) == IDYES) {
					// Operation may have completed in the meantime
					if (format_thid != NULL) {
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
						PrintStatus(0, FALSE, "Cancelling - Please wait...");
						uprintf("Cancelling (from main app)\n");
						//  Start a timer to detect blocking operations during ISO file extraction
						if (iso_blocking_status >= 0) {
							last_iso_blocking_status = iso_blocking_status;
							SetTimer(hMainDialog, TID_BLOCKING_TIMER, 3000, BlockingTimer);
						}
					}
				} else {
					EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), TRUE);
					EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
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
#ifdef RUFUS_TEST
		case IDC_TEST:
			break;
#endif
		case IDC_DEVICE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			PrintStatus(0, TRUE, "%d device%s found.", ComboBox_GetCount(hDeviceList),
				(ComboBox_GetCount(hDeviceList)!=1)?"s":"");
			PopulateProperties(ComboBox_GetCurSel(hDeviceList));
			break;
		case IDC_NBPASSES:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			DestroyTooltip(hPassesToolTip);
			switch(ComboBox_GetCurSel(hNBPasses)) {
			case 0:
				hPassesToolTip = CreateTooltip(hNBPasses, "Pattern: 0x55", -1);
				break;
			case 1:
				hPassesToolTip = CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA", -1);
				break;
			case 2:
				hPassesToolTip = CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA, 0xFF", -1);
				break;
			case 3:
				hPassesToolTip = CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA, 0xFF, 0x00", -1);
				break;
			}
			break;
		case IDC_FILESYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			SetClusterSizes(fs);
			if ((fs == FS_EXFAT) || (fs <0)) {
				if (IsWindowEnabled(hDOS)) {
					// unlikely to be supported by BIOSes => don't bother
					IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, 0));
					uDOSChecked = IsDlgButtonChecked(hMainDialog, IDC_DOS);
					CheckDlgButton(hDlg, IDC_DOS, BST_UNCHECKED);
					EnableWindow(hDOS, FALSE);
					EnableWindow(hDOSType, FALSE);
					EnableWindow(hSelectISO, FALSE);
				}
				break;
			}
			IGNORE_RETVAL(ComboBox_ResetContent(hDOSType));
			if ((fs == FS_FAT16) || (fs == FS_FAT32)) {
				IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "MS-DOS"), DT_WINME));
				if (bWithFreeDOS)
					IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "FreeDOS"), DT_FREEDOS));
			}
			IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "ISO Image"), DT_ISO));
			if ((selection_default == DT_ISO) && (iso_path == NULL))
				selection_default = (bWithFreeDOS)?DT_FREEDOS:DT_WINME;
			for (i=0; i<ComboBox_GetCount(hDOSType); i++) {
				if (ComboBox_GetItemData(hDOSType, i) == selection_default) {
					IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, i));
					break;
				}
			}
			if (i == ComboBox_GetCount(hDOSType))
				IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, 0));
			if (!IsWindowEnabled(hDOS)) {
				EnableWindow(hDOS, TRUE);
				EnableWindow(hDOSType, TRUE);
				EnableWindow(hSelectISO, TRUE);
				CheckDlgButton(hDlg, IDC_DOS, uDOSChecked);
			}
			break;
		case IDC_DOSTYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			if (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_ISO) {
				if ((iso_path == NULL) || (iso_report.label[0] == 0)) {
					// Set focus to the Select ISO button
					SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
					SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)hSelectISO, TRUE);
				} else {
					SetWindowTextU(hLabel, iso_report.label);
				}
			} else {
				// Set focus on the start button
				SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
				SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
				SetWindowTextU(hLabel, SelectedDrive.proposed_label);
			}
			return (INT_PTR)TRUE;
		case IDC_SELECT_ISO:
			DestroyTooltip(hISOToolTip);
			safe_free(iso_path);
			iso_path = FileDialog(FALSE, NULL, "*.iso", "iso", "ISO Image");
			if (iso_path == NULL) {
				hISOToolTip = CreateTooltip(hSelectISO, "Click to select...", -1);
				break;
			}
			selection_default = DT_ISO;
			hISOToolTip = CreateTooltip(hSelectISO, iso_path, -1);
			FormatStatus = 0;
			// You'd think that Windows would let you instantiate a modeless dialog wherever
			// but you'd be wrong. It must be done in the main callback!
			if (!IsWindow(hISOProgressDlg)) { 
				hISOProgressDlg = CreateDialogA(hMainInstance, MAKEINTRESOURCEA(IDD_ISO_EXTRACT),
					hDlg, (DLGPROC)ISOProc); 
				// The window is not visible by default but takes focus => restore it
				SetFocus(hDlg);
			} 
			if (CreateThread(NULL, 0, ISOScanThread, NULL, 0, 0) == NULL) {
				uprintf("Unable to start ISO scanning thread");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
			}
			break;
		case IDC_START:
			if (format_thid != NULL) {
				return (INT_PTR)TRUE;
			}
			FormatStatus = 0;
			selection_default =  (int)ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			if (nDeviceIndex != CB_ERR) {
				if (IsChecked(IDC_DOS)) {
					if (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_ISO) {
						if (iso_path == NULL) {
							MessageBoxA(hMainDialog, "Please click on the disc button to select a bootable ISO,\n"
								"or uncheck the \"Create a bootable disk...\" checkbox.",
								"No ISO image selected...", MB_OK|MB_ICONERROR);
							break;
						}
						if ((iso_size_check) && (iso_report.projected_size > (uint64_t)SelectedDrive.DiskSize)) {
							MessageBoxA(hMainDialog, "This ISO image is too big "
								"for the selected target.", "ISO image too big...", MB_OK|MB_ICONERROR);
							break;
						}
						fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
						if ((fs == FS_NTFS) && (!iso_report.has_bootmgr)) {
							MessageBoxA(hMainDialog, "Only 'bootmgr' based ISO "
								"images can currently be used with NTFS.", "Unsupported ISO...", MB_OK|MB_ICONERROR);
							break;
						} else if (((fs == FS_FAT16)||(fs == FS_FAT32)) && (!iso_report.has_isolinux)) {
							MessageBoxA(hMainDialog, "Only 'isolinux' based ISO "
								"images can currently be used with FAT.", "Unsupported ISO...", MB_OK|MB_ICONERROR);
							break;
						}
					}
				}
				GetWindowTextW(hDeviceList, wtmp, ARRAYSIZE(wtmp));
				_snwprintf(wstr, ARRAYSIZE(wstr), L"WARNING: ALL DATA ON DEVICE %s\r\nWILL BE DESTROYED.\r\n"
					L"To continue with this operation, click OK. To quit click CANCEL.", wtmp);
				if (MessageBoxW(hMainDialog, wstr, L"Rufus", MB_OKCANCEL|MB_ICONWARNING) == IDOK) {
					// Disable all controls except cancel
					EnableControls(FALSE);
					DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
					FormatStatus = 0;
					InitProgress();
					if (!IsWindow(hISOProgressDlg)) { 
						hISOProgressDlg = CreateDialogA(hMainInstance, MAKEINTRESOURCEA(IDD_ISO_EXTRACT),
							hDlg, (DLGPROC)ISOProc); 
						// The window is not visible by default but takes focus => restore it
						SetFocus(hDlg);
					} 
					format_thid = CreateThread(NULL, 0, FormatThread, (LPVOID)(uintptr_t)DeviceNum, 0, NULL);
					if (format_thid == NULL) {
						uprintf("Unable to start formatting thread");
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
						PostMessage(hMainDialog, UM_FORMAT_COMPLETED, 0, 0);
					}
					timer = 0;
					safe_sprintf(szTimer, sizeof(szTimer), "00:00:00");
					SendMessageA(GetDlgItem(hMainDialog, IDC_STATUS), SB_SETTEXTA,
						SBT_OWNERDRAW | 1, (LPARAM)szTimer);
					SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
				}
			}
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	case WM_CLOSE:
		if (format_thid != NULL) {
			return (INT_PTR)TRUE;
		}
		PostQuitMessage(0);
		break;

	case UM_FORMAT_COMPLETED:
		format_thid = NULL;
		// Stop the timer
		KillTimer(hMainDialog, TID_APP_TIMER);
		// Close the cancel MessageBox and Blocking notification if active
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), RUFUS_CANCELBOX_TITLE), WM_COMMAND, IDNO, 0);
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), RUFUS_BLOCKING_IO_TITLE), WM_COMMAND, IDYES, 0);
		EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), TRUE);
		EnableWindow(GetDlgItem(hMainDialog, IDCANCEL), TRUE);
		EnableControls(TRUE);
		GetUSBDevices(DeviceNum);
		if (!IS_ERROR(FormatStatus)) {
			PrintStatus(0, FALSE, "DONE");
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			PrintStatus(0, FALSE, "Cancelled");
			Notification(MSG_INFO, "Cancelled", "Operation cancelled by the user.");
		} else {
			PrintStatus(0, FALSE, "FAILED");
			Notification(MSG_ERROR, "Error", "Error: %s", StrError(FormatStatus));
		}
		if (FormatStatus) {
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
		} else {
			// This is the only way to achieve instantenous progress transition
			SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS+1)<<16);
			SendMessage(hProgress, PBM_SETPOS, (MAX_PROGRESS+1), 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, MAX_PROGRESS<<16);
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

#ifdef DISABLE_AUTORUN
	// We use local group policies rather than direct registry manipulation
	// 0x9e disables removable and fixed drive notifications
	SetLGP(FALSE, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0x9e);
#endif

	// Find out if the FreeDOS resources are embedded in the app
	bWithFreeDOS = (FindResource(hMainInstance, MAKEINTRESOURCE(IDR_FD_COMMAND_COM), RT_RCDATA) != NULL) &&
		(FindResource(hMainInstance, MAKEINTRESOURCE(IDR_FD_KERNEL_SYS), RT_RCDATA) != NULL);
	uprintf("FreeDOS resources are %sembedded with this app\n", bWithFreeDOS?"":"NOT ");

	// Create the main Window
	if ( (hDlg = CreateDialogA(hInstance, MAKEINTRESOURCEA(IDD_DIALOG), NULL, MainCallback)) == NULL ) {
		MessageBoxA(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP);
		goto out;
	}
	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing and process "magic" commands
	while(GetMessage(&msg, NULL, 0, 0)) {
		// The following ensures the processing of the ISO progress window messages
		if (!IsWindow(hISOProgressDlg) || !IsDialogMessage(hISOProgressDlg, &msg)) {
			// Alt-S => Disable size limit
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'S')) {
				iso_size_check = !iso_size_check;
				PrintStatus(0, FALSE, "ISO size check %s", iso_size_check?"enabled":"disabled");
				continue;
			}
			// Alt-F => toggle detection of fixed disks
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'F')) {
				enable_fixed_disks = !enable_fixed_disks;
				PrintStatus(0, FALSE, "Fixed disks detection %s", enable_fixed_disks?"enabled":"disabled");
				GetUSBDevices(0);
				continue;
			}
#ifdef DISABLE_AUTORUN
			// Alt-D => Delete the NoDriveTypeAutorun key on exit (useful if the app crashed)
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'D')) {
				PrintStatus(0, FALSE, "NoDriveTypeAutorun will be deleted on exit.");
				existing_key = FALSE;
				continue;
			}
#endif
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

out:
	DestroyAllTooltips();
	safe_free(iso_path);
#ifdef DISABLE_AUTORUN
	SetLGP(TRUE, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0);
#endif
	CloseHandle(mutex);
	uprintf("*** RUFUS EXIT ***\n");

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
