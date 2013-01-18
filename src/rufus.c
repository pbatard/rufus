/*
 * Rufus: The Reliable USB Formatting Utility
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
#include "registry.h"

/* Redefinitions for the WDK */
#ifndef PBM_SETSTATE
#define PBM_SETSTATE (WM_USER+16)
#endif
#ifndef PBST_NORMAL
#define PBST_NORMAL 1
#endif
#ifndef PBST_ERROR
#define PBST_ERROR 2
#endif
#ifndef PBST_PAUSED
#define PBST_PAUSED 3
#endif
#ifndef BUTTON_IMAGELIST_ALIGN_CENTER
#define BUTTON_IMAGELIST_ALIGN_CENTER 4
#endif
#ifndef BCM_SETIMAGELIST
#define BCM_SETIMAGELIST 0x1602
#endif

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
} bi_iso = {0}, bi_up = {0}, bi_down = {0};	// BUTTON_IMAGELIST

static const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "exFAT" };
// Don't ask me - just following the MS "standard" here
static const char* ClusterSizeLabel[] = { "512 bytes", "1024 bytes","2048 bytes","4096 bytes","8192 bytes",
	"16 kilobytes", "32 kilobytes", "64 kilobytes", "128 kilobytes", "256 kilobytes", "512 kilobytes",
	"1024 kilobytes","2048 kilobytes","4096 kilobytes","8192 kilobytes","16 megabytes","32 megabytes" };
static const char* PartitionSchemeName[PT_MAX] = { "MBR", "GPT" };
static BOOL existing_key = FALSE;	// For LGP set/restore
static BOOL iso_size_check = TRUE;
static BOOL log_displayed = FALSE;
static int selection_default;
BOOL enable_fixed_disks = FALSE, advanced_mode = TRUE;

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH], app_dir[MAX_PATH];
char* iso_path = NULL;
float fScale = 1.0f;
int default_fs;
HWND hDeviceList, hPartitionScheme, hFileSystem, hClusterSize, hLabel, hDOSType, hNBPasses, hLog = NULL;
HWND hISOProgressDlg = NULL, hLogDlg = NULL, hISOProgressBar, hISOFileName, hDiskID;
BOOL use_own_c32[NB_OLD_C32] = {FALSE, FALSE}, detect_fakes = TRUE, mbr_selected_by_user = FALSE;
BOOL iso_op_in_progress = FALSE, format_op_in_progress = FALSE;
int dialog_showing = 0;
uint16_t rufus_version[4];
RUFUS_UPDATE update = { {0,0,0,0}, {0,0}, NULL, NULL};
extern char szStatusMessage[256];

static HANDLE format_thid = NULL;
static HWND hProgress = NULL, hDOS = NULL, hSelectISO = NULL;
static HICON hIconDisc, hIconDown, hIconUp;
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
static float previous_end;

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
		uprintf("This application does not support volumes smaller than 8 MB\n");
		goto out;
	}

/*
 * The following are MS's allowed cluster sizes for FAT16 and FAT32:
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
 * 31GB : 8192 - 64k This is as far as Microsoft's FormatEx goes...
 * 63GB :  16k - 64k ...but we can go higher using fat32format from RidgeCrop.
 * 2TB+ : N/A
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
	// > 32GB FAT32 is not supported by MS and FormatEx but is achieved using fat32fomat
	// See: http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm
	// < 32 MB FAT32 is not allowed by FormatEx, so we don't bother

	if ((SelectedDrive.DiskSize >= 32*MB) && (SelectedDrive.DiskSize < 2*TB)) {
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
		if ((SelectedDrive.DiskSize >= 256*MB) && (SelectedDrive.DiskSize < 32*GB)) {
			for (i=8; i<=32; i<<=1) {				// 256 MB -> 32 GB
				if (SelectedDrive.DiskSize < i*GB) {
					SelectedDrive.ClusterSize[FS_FAT32].Default = ((ULONG)i/2)*1024;
					break;
				}
			}
		}
		// More adjustments for large drives
		if (SelectedDrive.DiskSize >= 32*GB) {
			SelectedDrive.ClusterSize[FS_FAT32].Allowed &= 0x0001C000;
			SelectedDrive.ClusterSize[FS_FAT32].Default = 0x00008000;
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
			tmp[0] = 0;
			// Tell the user if we're going to use Large FAT32 or regular
			if ((fs == FS_FAT32) && (SelectedDrive.DiskSize > LARGE_FAT32_SIZE))
				safe_strcat(tmp, sizeof(tmp), "Large ");
			safe_strcat(tmp, sizeof(tmp), FileSystemLabel[fs]);
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

// Convert a file size to human readable
static __inline char* size_to_hr(LARGE_INTEGER size)
{
	int suffix = 0;
	static char str_size[24];
	const char* sizes[] = { "", "KB", "MB", "GB", "TB" };
	double hr_size = (double)size.QuadPart;
	while ((suffix < ARRAYSIZE(sizes)) && (hr_size >= 1024.0)) {
		hr_size /= 1024.0;
		suffix++;
	}
	if (suffix == 0) {
		safe_sprintf(str_size, sizeof(str_size), "%d bytes", (int)hr_size);
	} else {
		safe_sprintf(str_size, sizeof(str_size), "%0.1f %s", hr_size, sizes[suffix]);
	}
	return str_size;
}

/*
 * Fill the drive properties (size, FS, etc)
 */
static BOOL GetDriveInfo(void)
{
	BOOL r;
	HANDLE hDrive;
	DWORD size;
	BYTE geometry[128], layout[1024], part_type;
	void* disk_geometry = (void*)geometry;
	void* drive_layout = (void*)layout;
	PDISK_GEOMETRY_EX DiskGeometry = (PDISK_GEOMETRY_EX)disk_geometry;
	PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = (PDRIVE_LAYOUT_INFORMATION_EX)drive_layout;
	char DrivePath[] = "#:\\", tmp[256], fs_type[32];
	DWORD i, nb_partitions = 0;

	SelectedDrive.DiskSize = 0;

	hDrive = GetDriveHandle(SelectedDrive.DeviceNumber, DrivePath, FALSE, FALSE);
	if (hDrive == INVALID_HANDLE_VALUE)
		return FALSE;

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, 
			NULL, 0, geometry, sizeof(geometry), &size, NULL);
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_GEOMETRY_EX failed for drive %c: %s\n", DrivePath[0], WindowsErrorString());
		safe_closehandle(hDrive);
		return FALSE;
	}
	SelectedDrive.DiskSize = DiskGeometry->DiskSize.QuadPart;
	memcpy(&SelectedDrive.Geometry, &DiskGeometry->Geometry, sizeof(DISK_GEOMETRY));
	uprintf("Sector Size: %d bytes\n", DiskGeometry->Geometry.BytesPerSector);
	uprintf("Cylinders: %lld, TracksPerCylinder: %d, SectorsPerTrack: %d\n",
		DiskGeometry->Geometry.Cylinders, DiskGeometry->Geometry.TracksPerCylinder, DiskGeometry->Geometry.SectorsPerTrack);

	r = DeviceIoControl(hDrive, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, 
			NULL, 0, layout, sizeof(layout), &size, NULL );
	if (!r || size <= 0) {
		uprintf("IOCTL_DISK_GET_DRIVE_LAYOUT_EX failed for drive %c: %s\n", DrivePath[0], WindowsErrorString());
	} else {
		switch (DriveLayout->PartitionStyle) {
		case PARTITION_STYLE_MBR:
			SelectedDrive.PartitionType = PT_MBR;
			for (i=0; i<DriveLayout->PartitionCount; i++) {
				if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
					nb_partitions++;
				}
			}
			uprintf("Partition type: MBR, NB Partitions: %d\n", nb_partitions);
			uprintf("Disk ID: 0x%08X\n", DriveLayout->Mbr.Signature);
			for (i=0; i<DriveLayout->PartitionCount; i++) {
				if (DriveLayout->PartitionEntry[i].Mbr.PartitionType != PARTITION_ENTRY_UNUSED) {
					uprintf("Partition %d:\n", DriveLayout->PartitionEntry[i].PartitionNumber);
					part_type = DriveLayout->PartitionEntry[i].Mbr.PartitionType;
					uprintf("  Type: %s (0x%02x)\r\n  Size: %s (%lld bytes)\r\n  Start Sector: %d, Boot: %s, Recognized: %s\n",
						GetPartitionType(part_type), part_type, size_to_hr(DriveLayout->PartitionEntry[i].PartitionLength),
						DriveLayout->PartitionEntry[i].PartitionLength, DriveLayout->PartitionEntry[i].Mbr.HiddenSectors,
						DriveLayout->PartitionEntry[i].Mbr.BootIndicator?"Yes":"No",
						DriveLayout->PartitionEntry[i].Mbr.RecognizedPartition?"Yes":"No");
				}
			}
			break;
		case PARTITION_STYLE_GPT:
			SelectedDrive.PartitionType = PT_GPT;
			uprintf("Partition type: GPT, NB Partitions: %d\n", DriveLayout->PartitionCount);
			uprintf("Disk GUID: %s\n", GuidToString(&DriveLayout->Gpt.DiskId));
			uprintf("Max parts: %d, Start Offset: %lld, Usable = %lld bytes\n",
				DriveLayout->Gpt.MaxPartitionCount, DriveLayout->Gpt.StartingUsableOffset.QuadPart, DriveLayout->Gpt.UsableLength.QuadPart);
			for (i=0; i<DriveLayout->PartitionCount; i++) {
				nb_partitions++;
				tmp[0] = 0;
				wchar_to_utf8_no_alloc(DriveLayout->PartitionEntry[i].Gpt.Name, tmp, sizeof(tmp));
				uprintf("Partition %d:\r\n  Type: %s\r\n  Name: '%s'\n", DriveLayout->PartitionEntry[i].PartitionNumber,
					GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionType), tmp);
				uprintf("  ID: %s\r\n  Size: %s (%lld bytes)\r\n  Start Sector: %lld, Attributes: 0x%016llX\n",
					GuidToString(&DriveLayout->PartitionEntry[i].Gpt.PartitionId), size_to_hr(DriveLayout->PartitionEntry[i].PartitionLength),
					DriveLayout->PartitionEntry[i].PartitionLength, DriveLayout->PartitionEntry[i].StartingOffset.QuadPart / DiskGeometry->Geometry.BytesPerSector,
					DriveLayout->PartitionEntry[i].Gpt.Attributes);
			}
			break;
		default:
			SelectedDrive.PartitionType = PT_MBR;
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

	// Syslinux and EFI have precedence over bootmgr
	if ((iso_report.has_isolinux) || (iso_report.has_efi)) {
		if (fs_mask & (1<<FS_FAT32)) {
			selected_fs = FS_FAT32;
		} else if (fs_mask & (1<<FS_FAT16)) {
			selected_fs = FS_FAT16;
		}
	} else if ((iso_report.has_bootmgr) || (IS_WINPE(iso_report.winpe))) {
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

void SetMBRProps(void)
{
	int fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	int dt = (int)ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
	BOOL needs_masquerading = (IS_WINPE(iso_report.winpe) && (!iso_report.uses_minint));

	if ((!mbr_selected_by_user) && ((iso_path == NULL) || (dt != DT_ISO) || (fs != FS_NTFS))) {
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
		IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
		return;
	}

	CheckDlgButton(hMainDialog, IDC_RUFUS_MBR,
		(needs_masquerading || iso_report.has_bootmgr || mbr_selected_by_user)?BST_CHECKED:BST_UNCHECKED);
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, needs_masquerading?1:0));
}

void EnableBootOptions(BOOL enable)
{
	EnableWindow(hDOS, enable);
	EnableWindow(hDOSType, enable);
	EnableWindow(hSelectISO, enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), enable);
	EnableWindow(hDiskID, enable);
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	double HumanReadableSize;
	char capacity[64];
	static char* suffix[] = { "B", "KB", "MB", "GB", "TB", "PB"};
	char no_label[] = STR_NO_LABEL;
	int i, j, fs;

	IGNORE_RETVAL(ComboBox_ResetContent(hPartitionScheme));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), FALSE);
	SetWindowTextA(hLabel, "");
	memset(&SelectedDrive, 0, sizeof(SelectedDrive));

	if (ComboIndex < 0)
		return TRUE;

	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, ComboIndex);
	if (!GetDriveInfo())	// This also populates FS
		return FALSE;
	SetFSFromISO();
	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	EnableBootOptions((fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));

	HumanReadableSize = (double)SelectedDrive.DiskSize;
	for (i=1; i<ARRAYSIZE(suffix); i++) {
		HumanReadableSize /= 1024.0;
		if (HumanReadableSize < 512.0) {
			for (j=0; j<PT_MAX; j++) {
				safe_sprintf(capacity, sizeof(capacity), "%s (1 Partition of %0.2f %s)",
					PartitionSchemeName[j], HumanReadableSize, suffix[i]);
				IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme, ComboBox_AddStringU(hPartitionScheme, capacity), j));
			}
			break;
		}
	}
	if (i >= ARRAYSIZE(suffix))
		uprintf("Could not populate partition scheme data\n");
	IGNORE_RETVAL(ComboBox_SetCurSel(hPartitionScheme, SelectedDrive.PartitionType));
	CreateTooltip(hDeviceList, DriveID.Table[ComboIndex], -1);

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
BOOL CreatePartition(HANDLE hDrive)
{
	CREATE_DISK CreateDisk = {PARTITION_STYLE_RAW, {0}};
	DRIVE_LAYOUT_INFORMATION_EX4 DriveLayoutEx = {0};
	BOOL r;
	DWORD size;
	LONGLONG size_in_sectors;
	int pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));

	PrintStatus(0, TRUE, "Partitioning (%s)...",  PartitionSchemeName[pt]);

	if ((pt == PT_GPT) || (!IsChecked(IDC_EXTRA_PARTITION))) {
		// Go with the MS 1 MB wastage at the beginning...
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 1024*1024;
	} else {
		// Align on Cylinder
		DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart = 
			SelectedDrive.Geometry.BytesPerSector * SelectedDrive.Geometry.SectorsPerTrack;
	}
	size_in_sectors = (SelectedDrive.DiskSize - DriveLayoutEx.PartitionEntry[0].StartingOffset.QuadPart) / SelectedDrive.Geometry.BytesPerSector;

	switch (pt) {
	case PT_MBR:
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
	case PT_GPT:
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

	switch (pt) {
	case PT_MBR:
		DriveLayoutEx.PartitionEntry[0].Mbr.HiddenSectors = SelectedDrive.Geometry.SectorsPerTrack;
		switch (ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))) {
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
	case PT_GPT:
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

	size = sizeof(DriveLayoutEx) - ((pt == PT_GPT)?(3*sizeof(PARTITION_INFORMATION_EX)):0);
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
	const char* generic_friendly_name = "USB Storage Device (Generic)";
	GUID _GUID_DEVINTERFACE_DISK =			// only known to some...
		{ 0x53f56307L, 0xb6bf, 0x11d0, {0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b} };

	IGNORE_RETVAL(ComboBox_ResetContent(hDeviceList));
	StrArrayClear(&DriveID);
	StrArrayClear(&DriveLabel);
	GetClientRect(hDeviceList, &rect);

	dev_info = SetupDiGetClassDevsA(&_GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		uprintf("SetupDiGetClassDevs (Interface) failed: %s\n", WindowsErrorString());
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %s\n", WindowsErrorString());
			continue;
		}

		if (safe_strcmp(buffer, usbstor_name) != 0)
			continue;
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %s\n", WindowsErrorString());
			// We can afford a failure on this call - just replace the name
			safe_strcpy(buffer, sizeof(buffer), generic_friendly_name);
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
			if (devint_detail_data == NULL) {
				uprintf("SetupDiGetDeviceInterfaceDetail (dummy) - no data was allocated\n");
				continue;
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
	int i, dt, fs;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	dt = (int)ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
	memset(&nb_slots, 0, sizeof(nb_slots));
	memset(&slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	memset(nb_slots, 0, sizeof(nb_slots));
	memset(slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	nb_slots[OP_ZERO_MBR] = 1;
	if (IsChecked(IDC_BADBLOCKS)) {
		nb_slots[OP_BADBLOCKS] = -1;
	}
	if (IsChecked(IDC_DOS)) {
		// 1 extra slot for PBR writing
		switch (dt) {
		case DT_WINME:
			nb_slots[OP_DOS] = 3+1;
			break;
		case DT_FREEDOS:
			nb_slots[OP_DOS] = 5+1;
			break;
		case DT_ISO:
			nb_slots[OP_DOS] = -1;
			break;
		default:
			nb_slots[OP_DOS] = 2+1;
			break;
		}
	}
	nb_slots[OP_PARTITION] = 1;
	nb_slots[OP_FIX_MBR] = 1;
	nb_slots[OP_CREATE_FS] = 
		nb_steps[ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))];
	if ( (!IsChecked(IDC_QUICKFORMAT))
	  || ((fs == FS_FAT32) && (SelectedDrive.DiskSize >= LARGE_FAT32_SIZE)) ) {
		nb_slots[OP_FORMAT] = -1;
	}
	nb_slots[OP_FINALIZE] = ((dt == DT_ISO) && (fs == FS_NTFS))?2:1;

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
	SetTaskbarProgressValue(pos, MAX_PROGRESS);
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
	EnableWindow(GetDlgItem(hMainDialog, IDC_PARTITION_SCHEME), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), bEnable);
	if (bEnable) {
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		EnableWindow(hDOS, (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
		EnableWindow(hDOSType, (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
		EnableWindow(hDiskID, (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
		EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
		EnableWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), (fs == FS_FAT16) || (fs == FS_FAT32) || (fs == FS_NTFS));
	} else {
		EnableWindow(hDOS, FALSE);
		EnableWindow(hDOSType, FALSE);
		EnableWindow(hDiskID, FALSE);
		EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), FALSE);
		EnableWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), FALSE);
	}
	EnableWindow(GetDlgItem(hMainDialog, IDC_BADBLOCKS), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), bEnable);
	EnableWindow(hSelectISO, bEnable);
	EnableWindow(hNBPasses, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ADVANCED), bEnable);
	SetDlgItemTextA(hMainDialog, IDCANCEL, bEnable?"Close":"Cancel");
}

/* Callback for the log window */
BOOL CALLBACK LogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) 
{
	HDC hdc;
	HFONT hf;
	long lfHeight;
	DWORD log_size;
	char *log_buffer = NULL, *filepath;

	switch (message) {
	case WM_INITDIALOG:
		hLog = GetDlgItem(hDlg, IDC_LOG_EDIT);
		// Increase the size of our log textbox to MAX_LOG_SIZE (unsigned word)
		PostMessage(hLog, EM_LIMITTEXT, MAX_LOG_SIZE , 0);
		// Set the font to Unicode so that we can display anything
		hdc = GetDC(NULL);
		lfHeight = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
		ReleaseDC(NULL, hdc);
		hf = CreateFontA(lfHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, "Arial Unicode MS");
		SendDlgItemMessageA(hDlg, IDC_LOG_EDIT, WM_SETFONT, (WPARAM)hf, TRUE);
		return TRUE;
	case WM_COMMAND: 
		switch (LOWORD(wParam)) {
		case IDCANCEL:
			ShowWindow(hDlg, SW_HIDE);
			log_displayed = FALSE;
			return TRUE;
		case IDC_LOG_CLEAR:
			SetWindowTextA(hLog, "");
			return TRUE;
		case IDC_LOG_SAVE:
			log_size = GetWindowTextLengthU(hLog);
			if (log_size > 0)
				log_buffer = (char*)malloc(log_size);
			if (log_buffer != NULL) {
				log_size = GetDlgItemTextU(hDlg, IDC_LOG_EDIT, log_buffer, log_size);
				if (log_size == 0) {
					uprintf("Nothing to save.\n");
				} else {
					log_size--;	// remove NULL terminator
					filepath =  FileDialog(TRUE, app_dir, "rufus.log", "log", "Rufus log");
					if (filepath != NULL) {
						FileIO(TRUE, filepath, &log_buffer, &log_size);
					}
					safe_free(filepath);
				}
				safe_free(log_buffer);
			} else {
				uprintf("Could not allocate buffer to save log\n");
			}
			break;
		}
		break;
//	case WM_SHOWWINDOW:
//		if (wParam)
//			SendMessage(hLog, EM_LINESCROLL, 0, SendMessage(hLog, EM_GETLINECOUNT, 0, 0));
//		return FALSE;
	case WM_CLOSE:
		ShowWindow(hDlg, SW_HIDE);
		log_displayed = FALSE;
		return TRUE;
	}
	return FALSE; 
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

/* Callback for the modeless ISO extraction progress, and other progress dialogs */
BOOL CALLBACK ISOProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) 
{
	switch (message) {
	case WM_INITDIALOG:
		hISOProgressBar = GetDlgItem(hDlg, IDC_PROGRESS);
		hISOFileName = GetDlgItem(hDlg, IDC_ISO_FILENAME);
		// Use maximum granularity for the progress bar
		SendMessage(hISOProgressBar, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
		return TRUE;
	case UM_ISO_INIT:
		iso_op_in_progress = TRUE;
		CenterDialog(hDlg);
		ShowWindow(hDlg, SW_SHOW);
		UpdateWindow(hDlg);
		return TRUE;
	case UM_ISO_EXIT:
		// Just hide and recentrer the dialog
		ShowWindow(hDlg, SW_HIDE);
		iso_op_in_progress = FALSE;
		return TRUE;
	case WM_COMMAND: 
		switch (LOWORD(wParam)) {
		case IDC_ISO_ABORT:
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			PrintStatus(0, FALSE, "Cancelling - Please wait...");
			uprintf("Cancelling (from ISO proc.)\n");
			EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), FALSE);
			if (format_thid != NULL)
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
	FILE* fd;
	const char* old_c32_name[NB_OLD_C32] = OLD_C32_NAMES;
	const char* new_c32_url[NB_OLD_C32] = NEW_C32_URL;
	char msg[1024], msg_title[32];

	if (iso_path == NULL)
		goto out;
	PrintStatus(0, TRUE, "Scanning ISO image...\n");
	if (!ExtractISO(iso_path, "", TRUE)) {
		SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
		PrintStatus(0, TRUE, "Failed to scan ISO image.");
		safe_free(iso_path);
		goto out;
	}
	uprintf("ISO label: '%s'\r\n  Size: %lld bytes\r\n  Has a >4GB file: %s\r\n  Uses EFI: %s\r\n  Uses Bootmgr: %s\r\n  Uses WinPE: %s%s\r\n  Uses isolinux: %s\n",
		iso_report.label, iso_report.projected_size, iso_report.has_4GB_file?"Yes":"No",  iso_report.has_efi?"Yes":"No", iso_report.has_bootmgr?"Yes":"No",
		IS_WINPE(iso_report.winpe)?"Yes":"No", (iso_report.uses_minint)?" (with /minint)":"", iso_report.has_isolinux?"Yes":"No");
	if (iso_report.has_isolinux) {
		for (i=0; i<NB_OLD_C32; i++) {
			uprintf("    With an old %s: %s\n", old_c32_name[i], iso_report.has_old_c32[i]?"Yes":"No");
		}
	}
	if ((!iso_report.has_bootmgr) && (!iso_report.has_isolinux) && (!IS_WINPE(iso_report.winpe))) {
		MessageBoxU(hMainDialog, "This version of Rufus only supports bootable ISOs\n"
			"based on 'bootmgr/WinPE' or 'isolinux'.\n"
			"This ISO image doesn't appear to use either...", "Unsupported ISO", MB_OK|MB_ICONINFORMATION);
		safe_free(iso_path);
		SetMBRProps();
	} else {
		for(i=0; i<NB_OLD_C32; i++) {
			if (iso_report.has_old_c32[i]) {
				fd = fopen(old_c32_name[i], "rb");
				if (fd != NULL) {
					// If a file already exists in the current directory, use that one
					uprintf("Will replace obsolete '%s' from ISO with the one found in current directory\n", old_c32_name[i]);
					fclose(fd);
					use_own_c32[i] = TRUE;
				} else {
					PrintStatus(0, FALSE, "Obsolete %s detected", old_c32_name[i]);
					safe_sprintf(msg, sizeof(msg), "This ISO image seems to use an obsolete version of %s\n"
						"that may prevent boot menus from displaying properly...\n\n"
						"Rufus can fix this issue by downloading a newer version for you:\n"
						"- Select 'Yes' to connect to the internet and replace the file.\n"
						"- Select 'No' to leave the existing ISO file unmodified.\n"
						"If you don't know what to do, you should select 'Yes'.\n\n"
						"Note: the file will be downloaded in the current directory. Once a\n"
						"%s exists there, it will always be used as replacement.\n", old_c32_name[i], old_c32_name[i]);
					safe_sprintf(msg_title, sizeof(msg_title), "Replace %s?", old_c32_name[i]);
					if (MessageBoxA(hMainDialog, msg, msg_title, MB_YESNO|MB_ICONWARNING) == IDYES) {
						SetWindowTextU(hISOProgressDlg, "Downloading file...");
						SetWindowTextU(hISOFileName, new_c32_url[i]);
						if (DownloadFile(new_c32_url[i], old_c32_name[i], hISOProgressDlg))
							use_own_c32[i] = TRUE;
					}
				}
			}
		}

		// Enable DOS, set DOS Type to ISO (last item) and set FS accordingly
		CheckDlgButton(hMainDialog, IDC_DOS, BST_CHECKED);
		SetFSFromISO();
		SetMBRProps();
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
	SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
	ExitThread(0);
}


void MoveControl(int nID, float vertical_shift)
{
	RECT rect;
	POINT point;
	HWND hControl;

	hControl = GetDlgItem(hMainDialog, nID);
	GetWindowRect(hControl, &rect);
	point.x = rect.left;
	point.y = rect.top;
	ScreenToClient(hMainDialog, &point);
	GetClientRect(hControl, &rect);
	MoveWindow(hControl, point.x, point.y + (int)(fScale*(advanced_mode?vertical_shift:-vertical_shift)),
		(rect.right - rect.left), (rect.bottom - rect.top), TRUE);
}

// Toggle "advanced" mode
void ToggleAdvanced(void)
{
	float dialog_shift = 59.0f;
	RECT rect;
	POINT point;
	int toggle;

	advanced_mode = !advanced_mode;

	// Increase or decrease the Window size
	GetWindowRect(hMainDialog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hMainDialog, rect.left, rect.top, point.x,
		point.y + (int)(fScale*(advanced_mode?dialog_shift:-dialog_shift)), TRUE);

	// Move the status bar up or down
	MoveControl(IDC_STATUS, dialog_shift);
	MoveControl(IDC_START, dialog_shift);
	MoveControl(IDC_PROGRESS, dialog_shift);
	MoveControl(IDC_ABOUT, dialog_shift);
	MoveControl(IDC_LOG, dialog_shift);
	MoveControl(IDCANCEL, dialog_shift);
#ifdef RUFUS_TEST
	MoveControl(IDC_TEST, dialog_shift);
#endif

	// Hide or show the various advanced options
	toggle = advanced_mode?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_DISK_ID), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_ADVANCED_GROUP), toggle);

	// Toggle the up/down icon
	SendMessage(GetDlgItem(hMainDialog, IDC_ADVANCED), BCM_SETIMAGELIST, 0, (LPARAM)(advanced_mode?&bi_up:&bi_down));
}

void InitDialog(HWND hDlg)
{
	HINSTANCE hDllInst;
	HDC hDC;
	int i, i16, s16, s32;
	char tmp[128], *token;

#ifdef RUFUS_TEST
	ShowWindow(GetDlgItem(hDlg, IDC_TEST), SW_SHOW);
#endif

	// Quite a burden to carry around as parameters
	hMainDialog = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
	hPartitionScheme = GetDlgItem(hDlg, IDC_PARTITION_SCHEME);
	hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
	hClusterSize = GetDlgItem(hDlg, IDC_CLUSTERSIZE);
	hLabel = GetDlgItem(hDlg, IDC_LABEL);
	hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
	hDOS = GetDlgItem(hDlg, IDC_DOS);
	hDOSType = GetDlgItem(hDlg, IDC_DOSTYPE);
	hSelectISO = GetDlgItem(hDlg, IDC_SELECT_ISO);
	hNBPasses = GetDlgItem(hDlg, IDC_NBPASSES);
	hDiskID = GetDlgItem(hDlg, IDC_DISK_ID);

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hDC = GetDC(hDlg);
	fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
	ReleaseDC(hDlg, hDC);
	// Adjust icon size lookup
	s16 = i16;
	s32 = (int)(32.0f*fScale);
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;
	if (s32 >= 54)
		s32 = 64;
	else if (s32 >= 40)
		s32 = 48;
	else if (s32 >= 28)
		s32 = 32;
	else if (s32 >= 20)
		s32 = 24;

	// Create the title bar icon
	SetTitleBarIcon(hDlg);
	GetWindowTextA(hDlg, tmp, sizeof(tmp));
	// Count of Microsoft for making it more attractive to read a
	// version using strtok() than using GetFileVersionInfo()
	token = strtok(tmp, "v");
	for (i=0; (i<4) && ((token = strtok(NULL, ".")) != NULL); i++)
		rufus_version[i] = (uint16_t)atoi(token);
	uprintf("Rufus version %d.%d.%d.%d\n", rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]);

	// Prefer FreeDOS to MS-DOS
	selection_default = DT_FREEDOS;
	// Create the status line and initialize the taskbar icon for progress overlay
	CreateStatusBar();
	CreateTaskbarList();
	SetTaskbarProgressState(TASKBAR_NORMAL);

	// Use maximum granularity for the progress bar
	SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
	// Fill up the passes
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "1 Pass"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "2 Passes"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "3 Passes"));
	IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, "4 Passes"));
	IGNORE_RETVAL(ComboBox_SetCurSel(hNBPasses, 1));
	CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA", -1);
	// Fill up the DOS type dropdown
	IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "MS-DOS"), DT_WINME));
	IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "FreeDOS"), DT_FREEDOS));
	IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "ISO Image"), DT_ISO));
	IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, selection_default));
	// Fill up the MBR masqueraded disk IDs ("8 disks should be enough for anybody")
	IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, "0x80 (default)"), 0x80));
	for (i=1; i<=7; i++) {
		sprintf(tmp, "0x%02x (%d%s disk)", 0x80+i, i+1, (i==1)?"nd":((i==2)?"rd":"th"));
		IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, tmp), 0x80+i));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));

	// Create the string array
	StrArrayCreate(&DriveID, MAX_DRIVES);
	StrArrayCreate(&DriveLabel, MAX_DRIVES);
	// Set various checkboxes
	CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_DOS, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_SET_ICON, BST_CHECKED);

	// Load system icons (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hDllInst = LoadLibraryA("shell32.dll");
	hIconDisc = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(12), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR|LR_SHARED);
	if (nWindowsVersion >= WINDOWS_VISTA) {
		hIconDown = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(16750), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR|LR_SHARED);
		hIconUp = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(16749), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR|LR_SHARED);
	} else {
		hIconDown = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_DOWN), IMAGE_ICON, 16, 16, 0);
		hIconUp = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_UP), IMAGE_ICON, 16, 16, 0);
	}

	// Set the icons on the the buttons
	pImageList_Create = (ImageList_Create_t) GetProcAddress(GetDLLHandle("Comctl32.dll"), "ImageList_Create");
	pImageList_ReplaceIcon = (ImageList_ReplaceIcon_t) GetProcAddress(GetDLLHandle("Comctl32.dll"), "ImageList_ReplaceIcon");

	bi_iso.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_iso.himl, -1, hIconDisc);
	SetRect(&bi_iso.margin, 0, 1, 0, 0);
	bi_iso.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_down.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_down.himl, -1, hIconDown);
	SetRect(&bi_down.margin, 0, 0, 0, 0);
	bi_down.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_up.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_up.himl, -1, hIconUp);
	SetRect(&bi_up.margin, 0, 0, 0, 0);
	bi_up.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;

	SendMessage(hSelectISO, BCM_SETIMAGELIST, 0, (LPARAM)&bi_iso);
	SendMessage(GetDlgItem(hDlg, IDC_ADVANCED), BCM_SETIMAGELIST, 0, (LPARAM)&bi_down);

	// Set the various tooltips
	CreateTooltip(hFileSystem, "Sets the target filesystem", -1);
	CreateTooltip(hClusterSize, "Minimum size that each data block occupies", -1);
	CreateTooltip(hLabel, "Use this field to set the drive label\nInternational characters are accepted", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ADVANCED), "Toggle advanced options", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_BADBLOCKS), "Test the device for bad blocks using a byte pattern", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_QUICKFORMAT), "Unchek this box to use the \"slow\" format method", -1);
	CreateTooltip(hDOS, "Check this box to make the USB drive bootable", -1);
	CreateTooltip(hDOSType, "Boot method", -1);
	CreateTooltip(hSelectISO, "Click to select an ISO...", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_SET_ICON), "Check this box to allow the display of international labels "
		"and set a device icon (creates an autorun.inf)", 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_RUFUS_MBR), "Install an MBR that allows boot selection and can masquerade the BIOS USB drive ID", 10000);
	CreateTooltip(hDiskID, "Try to masquerade first bootable USB drive (usually 0x80) as a different disk.\n"
		"This should only be necessary for XP installation" , 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_EXTRA_PARTITION), "Create an extra hidden partition and try to align partitions boundaries.\n"
		"This can improve boot detection for older BIOSes", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_START), "Start the formatting operation.\nThis will DESTROY any data on the target!", -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ABOUT), "Licensing information and credits", -1);

	ToggleAdvanced();	// We start in advanced mode => go to basic mode
}

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	DRAWITEMSTRUCT* pDI;
	POINT Point;
	RECT DialogRect, DesktopRect;
	int nDeviceIndex, fs, pt, i, nWidth, nHeight;
	static DWORD DeviceNum = 0;
	wchar_t wtmp[128], wstr[MAX_PATH];
	static UINT uDOSChecked = BST_CHECKED, uQFChecked;
	static BOOL first_log_display = TRUE, user_changed_label = FALSE;

	switch (message) {

	case WM_DEVICECHANGE:
		if ( (format_thid == NULL) &&
			 ((wParam == DBT_DEVICEARRIVAL) || (wParam == DBT_DEVICEREMOVECOMPLETE)) ) {
			GetUSBDevices((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
			user_changed_label = FALSE;
			return (INT_PTR)TRUE;
		}
		break;

	case WM_INITDIALOG:
		SetUpdateCheck();
		// Create the log window (hidden)
		hLogDlg = CreateDialogA(hMainInstance, MAKEINTRESOURCEA(IDD_LOG), hDlg, (DLGPROC)LogProc); 
		InitDialog(hDlg);
		GetUSBDevices(0);
		CheckForUpdates(FALSE);
		PostMessage(hMainDialog, UM_ISO_CREATE, 0, 0);
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
					DT_LEFT|DT_END_ELLIPSIS|DT_PATH_ELLIPSIS, NULL);
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
		case IDC_LOG:
			// Place the log Window to the right of our dialog on first display
			if (first_log_display) {
				GetClientRect(GetDesktopWindow(), &DesktopRect);
				GetWindowRect(hLogDlg, &DialogRect);
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				GetWindowRect(hDlg, &DialogRect);
				Point.x = min(DialogRect.right + GetSystemMetrics(SM_CXSIZEFRAME)+(int)(2.0f * fScale), DesktopRect.right - nWidth);
				Point.y = max(DialogRect.top, DesktopRect.top - nHeight);
				MoveWindow(hLogDlg, Point.x, Point.y, nWidth, nHeight, FALSE);
				first_log_display = FALSE;
			}
			// Display the log Window
			log_displayed = !log_displayed;
			ShowWindow(hLogDlg, log_displayed?SW_SHOW:SW_HIDE);
			if (IsShown(hISOProgressDlg))
				SetFocus(hISOProgressDlg);
			// Set focus on the start button
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
			break;
#ifdef RUFUS_TEST
		case IDC_TEST:
			break;
#endif
		case IDC_ADVANCED:
			ToggleAdvanced();
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_LABEL:
			if (HIWORD(wParam) == EN_CHANGE)
				user_changed_label = TRUE;
			break;
		case IDC_DEVICE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			PrintStatus(0, TRUE, "%d device%s found.", ComboBox_GetCount(hDeviceList),
				(ComboBox_GetCount(hDeviceList)!=1)?"s":"");
			PopulateProperties(ComboBox_GetCurSel(hDeviceList));
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_NBPASSES:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			switch(ComboBox_GetCurSel(hNBPasses)) {
			case 0:
				CreateTooltip(hNBPasses, "Pattern: 0x55", -1);
				break;
			case 1:
				CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA", -1);
				break;
			case 2:
				CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA, 0xFF", -1);
				break;
			case 3:
				CreateTooltip(hNBPasses, "Pattern: 0x55, 0xAA, 0xFF, 0x00", -1);
				break;
			}
			break;
		case IDC_PARTITION_SCHEME:
		case IDC_FILESYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
			SetClusterSizes(fs);
			// Disable/restore the quick format control depending on large FAT32
			if ((fs == FS_FAT32) && (SelectedDrive.DiskSize > LARGE_FAT32_SIZE)) {
				if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_QUICKFORMAT))) {
					uQFChecked = IsDlgButtonChecked(hMainDialog, IDC_QUICKFORMAT);
					CheckDlgButton(hMainDialog, IDC_QUICKFORMAT, BST_CHECKED);
					EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), FALSE);
				}
			} else {
				if (!IsWindowEnabled(GetDlgItem(hMainDialog, IDC_QUICKFORMAT))) {
					CheckDlgButton(hMainDialog, IDC_QUICKFORMAT, uQFChecked);
					EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), TRUE);
				}
			}
			if (fs < 0) {
				EnableBootOptions(TRUE);
				SetMBRProps();
				// Remove the SysLinux option if exists
				if (ComboBox_GetItemData(hDOSType, ComboBox_GetCount(hDOSType)-1) == DT_SYSLINUX) {
					IGNORE_RETVAL(ComboBox_DeleteString(hDOSType,  ComboBox_GetCount(hDOSType)-1));
					IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, 1));
				}
				break;
			}
			if ((fs == FS_EXFAT) || ((pt == PT_GPT) && (fs == FS_NTFS)))  {
				if (IsWindowEnabled(hDOS)) {
					// unlikely to be supported by BIOSes => don't bother
					IGNORE_RETVAL(ComboBox_SetCurSel(hDOSType, 0));
					uDOSChecked = IsDlgButtonChecked(hMainDialog, IDC_DOS);
					CheckDlgButton(hDlg, IDC_DOS, BST_UNCHECKED);
					EnableBootOptions(FALSE);
				}
				SetMBRProps();
				break;
			}
			IGNORE_RETVAL(ComboBox_ResetContent(hDOSType));
			if ((pt == PT_MBR) && ((fs == FS_FAT16) || (fs == FS_FAT32))) {
				IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "MS-DOS"), DT_WINME));
				IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "FreeDOS"), DT_FREEDOS));
			}
			IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "ISO Image"), DT_ISO));
			// If needed (advanced mode) also append a Syslinux option
			if ( (pt == PT_MBR) && (((fs == FS_FAT16) || (fs == FS_FAT32)) && (advanced_mode)) )
				IGNORE_RETVAL(ComboBox_SetItemData(hDOSType, ComboBox_AddStringU(hDOSType, "SysLinux"), DT_SYSLINUX));
			if ( ((!advanced_mode) && (selection_default == DT_SYSLINUX)) ) {
				selection_default = DT_FREEDOS;
				CheckDlgButton(hDlg, IDC_DISK_ID, BST_UNCHECKED);
			}
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
			SetMBRProps();
			break;
		case IDC_DOSTYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			selection_default = (int) ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType));
			if (ComboBox_GetItemData(hDOSType, ComboBox_GetCurSel(hDOSType)) == DT_ISO) {
				if ((iso_path == NULL) || (iso_report.label[0] == 0)) {
					// Set focus to the Select ISO button
					SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
					SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)hSelectISO, TRUE);
				} else {
					// Some distros (eg. Arch Linux) want to see a specific label => ignore user one
					SetWindowTextU(hLabel, iso_report.label);
				}
			} else {
				// Set focus on the start button
				SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
				SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
				// For non ISO, if the user manually set a label, try to preserve it
				if (!user_changed_label)
					SetWindowTextU(hLabel, SelectedDrive.proposed_label);
				// Reset disk ID to 0x80 if Rufus MBR is used
				IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
			}
			return (INT_PTR)TRUE;
		case IDC_SELECT_ISO:
			safe_free(iso_path);
			iso_path = FileDialog(FALSE, NULL, "*.iso", "iso", "ISO Image");
			if (iso_path == NULL) {
				CreateTooltip(hSelectISO, "Click to select...", -1);
				break;
			}
			selection_default = DT_ISO;
			CreateTooltip(hSelectISO, iso_path, -1);
			FormatStatus = 0;
			if (CreateThread(NULL, 0, ISOScanThread, NULL, 0, NULL) == NULL) {
				uprintf("Unable to start ISO scanning thread");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
			}
			break;
		case IDC_RUFUS_MBR:
			if ((HIWORD(wParam)) == BN_CLICKED)
				mbr_selected_by_user = IsChecked(IDC_RUFUS_MBR);
			break;
		case IDC_START:
			if (format_thid != NULL) {
				return (INT_PTR)TRUE;
			}
			FormatStatus = 0;
			format_op_in_progress = TRUE;
			// Reset all progress bars
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
			SetTaskbarProgressState(TASKBAR_NORMAL);
			SetTaskbarProgressValue(0, MAX_PROGRESS);
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
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
						pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
						if ((pt == PT_GPT) && ((!iso_report.has_efi) || ((fs > FS_FAT32)))) {
							MessageBoxA(hMainDialog, "When using GPT, only EFI bootable ISOs are supported. "
								"Please select an EFI bootable ISO or change the Partition Scheme to MBR.", "Unsupported GPT ISO...", MB_OK|MB_ICONERROR);
							break;
						}
						else if ((fs == FS_NTFS) && (!iso_report.has_bootmgr) && (!IS_WINPE(iso_report.winpe))) {
							if (iso_report.has_isolinux) {
								MessageBoxA(hMainDialog, "Only FAT32 is supported for this type of ISO. "
									"Please revert the filesystem back from NTFS to FAT32.", "Unsupported filesystem...", MB_OK|MB_ICONERROR);
							} else {
								MessageBoxA(hMainDialog, "Only 'bootmgr' or 'WinPE' based ISO "
									"images can currently be used with NTFS.", "Unsupported ISO...", MB_OK|MB_ICONERROR);
							}
							break;
						} else if (((fs == FS_FAT16)||(fs == FS_FAT32)) && ((!iso_report.has_isolinux) && (pt != PT_GPT))) {
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
			if (format_thid == NULL)
				format_op_in_progress = FALSE;
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

	case UM_ISO_CREATE:
		// You'd think that Windows would let you instantiate a modeless dialog wherever
		// but you'd be wrong. It must be done in the main callback, hence the custom message.
		if (!IsWindow(hISOProgressDlg)) { 
			hISOProgressDlg = CreateDialogA(hMainInstance, MAKEINTRESOURCEA(IDD_ISO_EXTRACT),
				hDlg, (DLGPROC)ISOProc); 
			// The window is not visible by default but takes focus => restore it
			SetFocus(hDlg);
		} 
		return (INT_PTR)TRUE;

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
			// This is the only way to achieve instantenous progress transition to 100%
			SendMessage(hProgress, PBM_SETRANGE, 0, ((MAX_PROGRESS+1)<<16) & 0xFFFF0000);
			SendMessage(hProgress, PBM_SETPOS, (MAX_PROGRESS+1), 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
			SetTaskbarProgressState(TASKBAR_NOPROGRESS);
			PrintStatus(0, FALSE, "DONE");
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_PAUSED, 0);
			SetTaskbarProgressState(TASKBAR_PAUSED);
			PrintStatus(0, FALSE, "Cancelled");
			Notification(MSG_INFO, NULL, "Cancelled", "Operation cancelled by the user.");
		} else {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			PrintStatus(0, FALSE, "FAILED");
			Notification(MSG_ERROR, NULL, "Error", "Error: %s.%s", StrError(FormatStatus), 
				(strchr(StrError(FormatStatus), '\n') != NULL)?"":"\nFor more information, please check the log.");
		}
		FormatStatus = 0;
		format_op_in_progress = FALSE;
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

/*
 * Application Entrypoint
 */
// If we ever need to process more than one commandline arguments, uncomment the following parts
// typedef int (CDECL *__wgetmainargs_t)(int*, wchar_t***, wchar_t***, int, int*);
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
//	int i, argc = 0, si = 0;
//	char** argv = NULL;
//	wchar_t **wenv, **wargv;
//	PF_DECL(__wgetmainargs);
	HANDLE mutex = NULL;
	HWND hDlg = NULL;
	MSG msg;
	int wait_for_mutex = 0;

	uprintf("*** RUFUS INIT ***\n");

//	PF_INIT(__wgetmainargs, msvcrt);
//	if (pf__wgetmainargs != NULL) {
//		pf__wgetmainargs(&argc, &wargv, &wenv, 1, &si);
//		argv = (char**)calloc(argc, sizeof(char*));
//		for (i=0; i<argc; i++) {
//			argv[i] = wchar_to_utf8(wargv[i]);
//		}
//	} else {
//		uprintf("unable to access UTF-16 args");
//	}

	// Prevent 2 applications from running at the same time, unless "/W" is passed as an option
	// in which case we wait for the mutex to be relinquished
	if ((safe_strlen(lpCmdLine)==2) && (lpCmdLine[0] == '/') && (lpCmdLine[1] == 'W'))
		wait_for_mutex = 150;		// Try to acquire the mutex for 15 seconds
	mutex = CreateMutexA(NULL, TRUE, "Global/RUFUS");
	for (;(wait_for_mutex>0) && (mutex != NULL) && (GetLastError() == ERROR_ALREADY_EXISTS); wait_for_mutex--) {
		CloseHandle(mutex);
		Sleep(100);
		mutex = CreateMutexA(NULL, TRUE, "Global/RUFUS");
	}
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) {
		MessageBoxA(NULL, "Another Rufus application is running.\n"
			"Please close the first application before running another one.",
			"Other instance detected", MB_ICONSTOP);
		return 0;
	}

	// Save instance of the application for further reference
	hMainInstance = hInstance;

	// Initialize COM for folder selection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

	// Some dialogs have Rich Edit controls and won't display without this
	if (LoadLibraryA("Riched20.dll") == NULL) {
		uprintf("Could not load RichEdit library - some dialogs may not display: %s\n", WindowsErrorString());
	}

	// Retrieve the current application directory
	GetCurrentDirectoryU(MAX_PATH, app_dir);

	// Set the Windows version
	nWindowsVersion = DetectWindowsVersion();

	// We use local group policies rather than direct registry manipulation
	// 0x9e disables removable and fixed drive notifications
	SetLGP(FALSE, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0x9e);

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
			// Alt-S => Disable size limit for ISOs
			// By default, Rufus will not copy ISOs that are larger than in size than 
			// the target USB drive. If this is enabled, the size check is disabled.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'S')) {
				iso_size_check = !iso_size_check;
				PrintStatus(2000, FALSE, "ISO size check %s.", iso_size_check?"enabled":"disabled");
				continue;
			}
			// Alt-F => Toggle detection of fixed disks
			// By default Rufus does not allow formatting USB fixed disk drives, such as USB HDDs
			// This is a safety feature, to avoid someone unintentionally formatting a backup 
			// drive instead of an USB key. If this is enabled, Rufus will allow fixed disk formatting.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'F')) {
				enable_fixed_disks = !enable_fixed_disks;
				PrintStatus(2000, FALSE, "Fixed disks detection %s.", enable_fixed_disks?"enabled":"disabled");
				GetUSBDevices(0);
				continue;
			}
			// Alt-D => Delete the NoDriveTypeAutorun key on exit (useful if the app crashed)
			// This key is used to disable Windows popup messages when an USB drive is plugged in.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'D')) {
				PrintStatus(2000, FALSE, "NoDriveTypeAutorun will be deleted on exit.");
				existing_key = FALSE;
				continue;
			}
			// Alt K => Toggle fake drive detection during bad blocks check
			// By default, Rufus will check for fake USB flash drives that mistakenly present 
			// more capacity than they already have by looping over the flash. This check which
			// is enabled by default is performed by writing the block number sequence and reading
			// it back during the bad block check.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'K')) {
				detect_fakes = !detect_fakes;
				PrintStatus(2000, FALSE, "Fake drive detection %s.", detect_fakes?"enabled":"disabled");
				continue;
			}
			// Alt-R => Remove all the registry keys created by Rufus
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'R')) {
				PrintStatus(2000, FALSE, "Application registry key %s deleted.",
					DeleteRegistryKey(COMPANY_NAME "\\" APPLICATION_NAME)?"successfully":"could not be");
				// Also try to delete the upper key (company name) if it's empty (don't care about the result)
				DeleteRegistryKey(COMPANY_NAME);
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

out:
	DestroyAllTooltips();
	safe_free(iso_path);
	safe_free(update.download_url);
	safe_free(update.release_notes);
	SetLGP(TRUE, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0);
	CloseHandle(mutex);
	uprintf("*** RUFUS EXIT ***\n");

#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
