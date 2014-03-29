/*
 * Rufus: The Reliable USB Formatting Utility
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
#include <shlobj.h>
#include <process.h>
#include <dbt.h>
#include <io.h>
#include <getopt.h>

#include "msapi_utf8.h"
#include "resource.h"
#include "rufus.h"
#include "drive.h"
#include "registry.h"
#include "localization.h"

/* Redefinitions for WDK and MinGW */
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
#ifndef DBT_CUSTOMEVENT
#define DBT_CUSTOMEVENT 0x8006
#endif

// MinGW fails to link those...
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
} bi_iso = {0}, bi_up = {0}, bi_down = {0}, bi_lang = {0};	// BUTTON_IMAGELIST

// ...and MinGW doesn't know these.
typedef struct
{
	LPCITEMIDLIST pidl;
	BOOL   fRecursive;
} MY_SHChangeNotifyEntry;

typedef BOOL (WINAPI *SHChangeNotifyDeregister_t)(
	ULONG ulID
);
typedef ULONG (WINAPI *SHChangeNotifyRegister_t)(
	HWND hwnd,
	int fSources,
	LONG fEvents,
	UINT wMsg,
	int cEntries,
	const MY_SHChangeNotifyEntry *pshcne
);

const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS" };
// Number of steps for each FS for FCC_STRUCTURE_PROGRESS
const int nb_steps[FS_MAX] = { 5, 5, 12, 1, 10 };
static const char* PartitionTypeLabel[2] = { "MBR", "GPT" };
static BOOL existing_key = FALSE;	// For LGP set/restore
static BOOL size_check = TRUE;
static BOOL log_displayed = FALSE;
static BOOL iso_provided = FALSE;
static BOOL user_notified = FALSE;
static BOOL relaunch = FALSE;
extern BOOL force_large_fat32, enable_joliet, enable_rockridge, enable_ntfs_compression;
extern const char* old_c32_name[NB_OLD_C32];
static int selection_default;
static loc_cmd* selected_locale = NULL;
static UINT_PTR UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
static RECT relaunch_rc = { -65536, -65536, 0, 0};
static UINT uBootChecked = BST_CHECKED, uQFChecked = BST_CHECKED, uMBRChecked = BST_UNCHECKED;
char ClusterSizeLabel[MAX_CLUSTER_SIZES][64];
char msgbox[1024], msgbox_title[32];

/*
 * Globals
 */
HINSTANCE hMainInstance;
HWND hMainDialog;
char szFolderPath[MAX_PATH], app_dir[MAX_PATH];
char* iso_path = NULL;
float fScale = 1.0f;
int default_fs;
uint32_t dur_mins, dur_secs;
HWND hDeviceList, hPartitionScheme, hFileSystem, hClusterSize, hLabel, hBootType, hNBPasses, hLog = NULL;
HWND hISOProgressDlg = NULL, hLogDlg = NULL, hISOProgressBar, hISOFileName, hDiskID;
BOOL use_own_c32[NB_OLD_C32] = {FALSE, FALSE}, detect_fakes = TRUE, mbr_selected_by_user = FALSE;
BOOL iso_op_in_progress = FALSE, format_op_in_progress = FALSE, right_to_left_mode = FALSE;
BOOL enable_HDDs = FALSE, advanced_mode = TRUE, force_update = FALSE;
int dialog_showing = 0;
uint16_t rufus_version[4], embedded_sl_version[2];
char embedded_sl_version_str[2][12] = { "?.??", "?.??" };
RUFUS_UPDATE update = { {0,0,0,0}, {0,0}, NULL, NULL};
extern char szStatusMessage[256];

static HANDLE format_thid = NULL;
static HWND hProgress = NULL, hBoot = NULL, hSelectISO = NULL;
static HICON hIconDisc, hIconDown, hIconUp, hIconLang;
static StrArray DriveID, DriveLabel;
static char szTimer[12] = "00:00:00";
static unsigned int timer;
static int64_t last_iso_blocking_status;

/*
 * The following is used to allocate slots within the progress bar
 * 0 means unused (no operation or no progress allocated to it)
 * +n means allocate exactly n bars (n percent of the progress bar)
 * -n means allocate a weighted slot of n from all remaining
 *    bars. Eg if 80 slots remain and the sum of all negative entries
 *    is 10, -4 will allocate 4/10*80 = 32 bars (32%) for OP progress
 */
static int nb_slots[OP_MAX];
static float slot_end[OP_MAX+1];	// shifted +1 so that we can substract 1 to OP indexes
static float previous_end;

// TODO: Remember to update copyright year in both license.h and the RC when the year changes!
// Also localization_data.sh

#define KB          1024LL
#define MB       1048576LL
#define GB    1073741824LL
#define TB 1099511627776LL

/*
 * Fill in the cluster size names
 */
static void SetClusterSizeLabels(void)
{
	unsigned int i, j, k;
	safe_sprintf(ClusterSizeLabel[0], 64, lmprintf(MSG_029));
	for (i=512, j=1, k=MSG_026; j<MAX_CLUSTER_SIZES; i<<=1, j++) {
		if (i > 8192) {
			i /= 1024;
			k++;
		}
		safe_sprintf(ClusterSizeLabel[j], 64, "%d %s", i, lmprintf(k));
	}
}

/*
 * Set cluster size values according to http://support.microsoft.com/kb/140365
 * this call will return FALSE if we can't find a supportable FS for the drive
 */
static BOOL DefineClusterSizes(void)
{
	LONGLONG i;
	int fs;
	BOOL r = FALSE;
	char tmp[128] = "", *entry;

	default_fs = FS_UNKNOWN;
	memset(&SelectedDrive.ClusterSize, 0, sizeof(SelectedDrive.ClusterSize));
	if (SelectedDrive.DiskSize < 8*MB) {
		uprintf("Device was eliminated because it is smaller than 8 MB\n");
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
	// > 32GB FAT32 is not supported by MS and FormatEx but is achieved using fat32format
	// See: http://www.ridgecrop.demon.co.uk/index.htm?fat32format.htm
	// < 32 MB FAT32 is not allowed by FormatEx, so we don't bother
	if ((SelectedDrive.DiskSize >= 32*MB) && (1.0f*SelectedDrive.DiskSize < 1.0f*MAX_FAT32_SIZE*TB)) {
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

	if (SelectedDrive.DiskSize < 256*TB) {
		// NTFS
		SelectedDrive.ClusterSize[FS_NTFS].Allowed = 0x0001FE00;
		for (i=16; i<=256; i<<=1) {				// 7 MB -> 256 TB
			if (SelectedDrive.DiskSize < i*TB) {
				SelectedDrive.ClusterSize[FS_NTFS].Default = ((ULONG)i/4)*1024;
				break;
			}
		}

		// exFAT (requires KB955704 installed on XP => don't bother)
		if (nWindowsVersion > WINDOWS_XP) {
			SelectedDrive.ClusterSize[FS_EXFAT].Allowed = 0x03FFFE00;
			if (SelectedDrive.DiskSize < 256*MB)	// < 256 MB
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 4*1024;
			else if (SelectedDrive.DiskSize < 32*GB)	// < 32 GB
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 32*1024;
			else
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 28*1024;
		}

		// UDF (only supported for Vista and later)
		if (nWindowsVersion >= WINDOWS_VISTA) {
			SelectedDrive.ClusterSize[FS_UDF].Allowed = 0x00000100;
			SelectedDrive.ClusterSize[FS_UDF].Default = 1;
		}

		// ReFS (only supported for Windows 8.1 and later and for fixed disks)
		if ((nWindowsVersion >= WINDOWS_8_1_OR_LATER) && (SelectedDrive.Geometry.MediaType == FixedMedia)) {
			SelectedDrive.ClusterSize[FS_REFS].Allowed = 0x00000100;
			SelectedDrive.ClusterSize[FS_REFS].Default = 1;
		}
	}

out:
	// Only add the filesystems we can service
	for (fs=0; fs<FS_MAX; fs++) {
		if (SelectedDrive.ClusterSize[fs].Allowed != 0) {
			tmp[0] = 0;
			// Tell the user if we're going to use Large FAT32 or regular
			if ((fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32)))
				safe_strcat(tmp, sizeof(tmp), "Large ");
			safe_strcat(tmp, sizeof(tmp), FileSystemLabel[fs]);
			if (default_fs == FS_UNKNOWN) {
				entry = lmprintf(MSG_030, tmp);
				default_fs = fs;
			} else {
				entry = tmp;
			}
			IGNORE_RETVAL(ComboBox_SetItemData(hFileSystem,
				ComboBox_AddStringU(hFileSystem, entry), fs));
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
	char* szClustSize;
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

	for(i=0,j=0x100,k=0;j<0x10000000;i++,j<<=1) {
		if (j & SelectedDrive.ClusterSize[FSType].Allowed) {
			if (j == SelectedDrive.ClusterSize[FSType].Default) {
				szClustSize = lmprintf(MSG_030, ClusterSizeLabel[i]);
				default_index = k;
			} else {
				szClustSize = ClusterSizeLabel[i];
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
static BOOL SetDriveInfo(int ComboIndex)
{
	DWORD i;
	int pt, bt;
	char fs_type[32];

	memset(&SelectedDrive, 0, sizeof(SelectedDrive));
	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, ComboIndex);

	SelectedDrive.nPartitions = GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_type, sizeof(fs_type));

	if (!DefineClusterSizes()) {
		uprintf("No file system is selectable for this drive\n");
		return FALSE;
	}

	// re-select existing FS if it's one we know
	SelectedDrive.FSType = FS_UNKNOWN;
	if (safe_strlen(fs_type) != 0) {
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

	for (i=0; i<3; i++) {
		// Populate MBR/BIOS, MBR/UEFI and GPT/UEFI targets, with an exception
		// for XP, as it doesn't support GPT at all
		if ((i == 2) && (nWindowsVersion <= WINDOWS_XP))
			continue;
		bt = (i==0)?BT_BIOS:BT_UEFI;
		pt = (i==2)?PARTITION_STYLE_GPT:PARTITION_STYLE_MBR;
		IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme, ComboBox_AddStringU(hPartitionScheme,
			lmprintf((i==0)?MSG_031:MSG_033, PartitionTypeLabel[pt])), (bt<<16)|pt));
	}

	// At least one filesystem is go => enable formatting
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), TRUE);

	return SetClusterSizes((int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)));
}

static void SetFSFromISO(void)
{
	int i, fs, selected_fs = FS_UNKNOWN;
	uint32_t fs_mask = 0;
	int bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));

	if (iso_path == NULL)
		return;

	// Create a mask of all the FS's available
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs = (int)ComboBox_GetItemData(hFileSystem, i);
		fs_mask |= 1<<fs;
	}

	// Syslinux and EFI have precedence over bootmgr (unless the user selected BIOS as target type)
	if ((HAS_SYSLINUX(iso_report)) || (IS_REACTOS(iso_report)) || ( (IS_EFI(iso_report)) && (bt == BT_UEFI))) {
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

static void SetMBRProps(void)
{
	int fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	int dt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	BOOL needs_masquerading = (IS_WINPE(iso_report.winpe) && (!iso_report.uses_minint));

	if ((!mbr_selected_by_user) && ((iso_path == NULL) || (dt != DT_ISO) || (fs != FS_NTFS))) {
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
		IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
		return;
	}

	uMBRChecked = (needs_masquerading || iso_report.has_bootmgr || mbr_selected_by_user)?BST_CHECKED:BST_UNCHECKED;
	if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)))
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, needs_masquerading?1:0));
}

static void EnableAdvancedBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	int bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	BOOL actual_enable = ((bt==BT_UEFI)||(selection_default>=DT_IMG)||!IsChecked(IDC_BOOT))?FALSE:enable;
	static UINT uXPartChecked = BST_UNCHECKED;

	if (remove_checkboxes) {
		// Store/Restore the checkbox states
		if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && !actual_enable) {
			uMBRChecked = IsChecked(IDC_RUFUS_MBR);
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
			uXPartChecked = IsChecked(IDC_EXTRA_PARTITION);
			CheckDlgButton(hMainDialog, IDC_EXTRA_PARTITION, BST_UNCHECKED);
		} else if (!IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && actual_enable) {
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
			CheckDlgButton(hMainDialog, IDC_EXTRA_PARTITION, uXPartChecked);
		}
	}

	EnableWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), actual_enable);
	EnableWindow(hDiskID, actual_enable);
}

static void EnableBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	int fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	BOOL actual_enable = ((fs != FS_FAT16) && (fs != FS_FAT32) && (fs != FS_NTFS) && (selection_default == DT_IMG))?FALSE:enable;

	EnableWindow(hBoot, actual_enable);
	EnableWindow(hBootType, actual_enable);
	EnableWindow(hSelectISO, actual_enable);
	EnableAdvancedBootOptions(actual_enable, remove_checkboxes);
}

static void SetPartitionSchemeTooltip(void)
{
	int bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	int pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	if (bt == BT_BIOS) {
		CreateTooltip(hPartitionScheme, lmprintf(MSG_150), 15000);
	} else {
		if (pt == PARTITION_STYLE_MBR) {
			CreateTooltip(hPartitionScheme, lmprintf(MSG_151), 15000);
		} else {
			CreateTooltip(hPartitionScheme, lmprintf(MSG_152), 15000);
		}
	}
}

static void SetTargetSystem(void)
{
	int ts;

	if (SelectedDrive.PartitionType == PARTITION_STYLE_GPT) {
		ts = 2;	// GPT/UEFI
	} else if (SelectedDrive.has_protective_mbr || SelectedDrive.has_mbr_uefi_marker || (IS_EFI(iso_report) &&
		(!HAS_SYSLINUX(iso_report)) && (!iso_report.has_bootmgr) && (!IS_WINPE(iso_report.winpe))) ) {
		ts = 1;	// MBR/UEFI
	} else {
		ts = 0;	// MBR/BIOS|UEFI
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hPartitionScheme, ts));
	SetPartitionSchemeTooltip();
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	const char no_label[] = STR_NO_LABEL;
	char* device_tooltip;

	IGNORE_RETVAL(ComboBox_ResetContent(hPartitionScheme));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), FALSE);
	SetWindowTextA(hLabel, "");
	memset(&SelectedDrive, 0, sizeof(SelectedDrive));

	if (ComboIndex < 0)
		return TRUE;

	if (!SetDriveInfo(ComboIndex))	// This also populates FS
		return FALSE;
	SetTargetSystem();
	SetFSFromISO();
	EnableBootOptions(TRUE, TRUE);

	// Set a proposed label according to the size (eg: "256MB", "8GB")
	safe_sprintf(SelectedDrive.proposed_label, sizeof(SelectedDrive.proposed_label),
		SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, TRUE));

	// Add a tooltip (with the size of the device in parenthesis)
	device_tooltip = (char*) malloc(safe_strlen(DriveID.String[ComboIndex]) + 16);
	if (device_tooltip != NULL) {
		safe_sprintf(device_tooltip, safe_strlen(DriveID.String[ComboIndex]) + 16, "%s (%s)",
			DriveID.String[ComboIndex], SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, FALSE));
		CreateTooltip(hDeviceList, device_tooltip, -1);
		free(device_tooltip);
	}

	// If no existing label is available and no ISO is selected, propose one according to the size (eg: "256MB", "8GB")
	if ((iso_path == NULL) || (iso_report.label[0] == 0)) {
		if ( (safe_stricmp(no_label, DriveLabel.String[ComboIndex]) == 0)
		  || (safe_stricmp(lmprintf(MSG_207), DriveLabel.String[ComboIndex]) == 0) ) {
			SetWindowTextU(hLabel, SelectedDrive.proposed_label);
		} else {
			SetWindowTextU(hLabel, DriveLabel.String[ComboIndex]);
		}
	} else {
		SetWindowTextU(hLabel, iso_report.label);
	}

	return TRUE;
}

/*
 * Refresh the list of USB devices
 */
static BOOL GetUSBDevices(DWORD devnum)
{
	// The first two are standard Microsoft drivers (including the Windows 8 UASP one).
	// The rest are the vendor UASP drivers I know of so far - list may be incomplete!
	const char* storage_name[] = { "USBSTOR", "UASPSTOR", "VUSBSTOR", "ETRONSTOR" };
	const char* scsi_name = "SCSI";
	const char* vhd_name = "Virtual Disk";
	char letter_name[] = " (?:)";
	BOOL found = FALSE, is_SCSI, is_UASP, is_VHD;
	HDEVINFO dev_info = NULL;
	SP_DEVINFO_DATA dev_info_data;
	SP_DEVICE_INTERFACE_DATA devint_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA_A devint_detail_data;
	DEVINST parent_inst, device_inst;
	DWORD size, i, j, k, datatype, drive_index;
	ULONG list_size[ARRAYSIZE(storage_name)], full_list_size;
	HANDLE hDrive;
	LONG maxwidth = 0;
	RECT rect;
	int s, score, drive_number;
	char drive_letters[27], *devid, *devid_list = NULL, entry_msg[128];
	char *label, *entry, buffer[MAX_PATH], str[sizeof("0000:0000")+1];
	uint16_t vid, pid;
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

	full_list_size = 0;
	for (s=0; s<ARRAYSIZE(storage_name); s++) {
		// Get a list of hardware IDs for all USB storage devices
		// This will be used to retrieve the VID:PID of our devices
		CM_Get_Device_ID_List_SizeA(&list_size[s], storage_name[s], CM_GETIDLIST_FILTER_SERVICE);
		if (list_size[s] != 0)
			full_list_size += list_size[s]-1;	// remove extra NUL terminator
	}
	full_list_size += 1;	// add extra NUL terminator
	if (full_list_size < 2)
		return FALSE;
	devid_list = (char*)malloc(full_list_size);
	if (devid_list == NULL) {
		uprintf("Could not allocate Dev ID list\n");
		return FALSE;
	}

	// Build a single list from all the storage enumerators we know of
	for (s=0, i=0; s<ARRAYSIZE(storage_name); s++) {
		if (list_size[s] > 1) {
			CM_Get_Device_ID_ListA(storage_name[s], &devid_list[i], list_size[s], CM_GETIDLIST_FILTER_SERVICE);
			// list_size is sometimes larger than required thus we need to find the real end
			for (i += list_size[s]; i > 2; i--) {
				if ((devid_list[i-2] != '\0') && (devid_list[i-1] == '\0') && (devid_list[i] == '\0'))
					break;
			}
		}
	}

	dev_info_data.cbSize = sizeof(dev_info_data);
	for (i=0; SetupDiEnumDeviceInfo(dev_info, i, &dev_info_data); i++) {
		memset(buffer, 0, sizeof(buffer));
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_ENUMERATOR_NAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Enumerator Name) failed: %s\n", WindowsErrorString());
			continue;
		}
		// UASP drives are listed under SCSI (along with regular SYSTEM drives => "DANGER, WILL ROBINSON!!!")
		is_SCSI = (safe_stricmp(buffer, scsi_name) == 0);
		if ((safe_stricmp(buffer, storage_name[0]) != 0) && (!is_SCSI))
			continue;
		memset(buffer, 0, sizeof(buffer));
		vid = 0; pid = 0;
		is_UASP = FALSE, is_VHD = FALSE;
		if (!SetupDiGetDeviceRegistryPropertyA(dev_info, &dev_info_data, SPDRP_FRIENDLYNAME,
				&datatype, (LPBYTE)buffer, sizeof(buffer), &size)) {
			uprintf("SetupDiGetDeviceRegistryProperty (Friendly Name) failed: %s\n", WindowsErrorString());
			// We can afford a failure on this call - just replace the name with "USB Storage Device (Generic)"
			safe_strcpy(buffer, sizeof(buffer), lmprintf(MSG_045));
		} else if (safe_strstr(buffer, vhd_name) != NULL) {
			is_VHD = TRUE;
		} else {
			// Get the VID:PID of the device. We could avoid doing this lookup every time by keeping
			// a lookup table, but there shouldn't be that many USB storage devices connected...
			for (devid = devid_list; *devid; devid += strlen(devid) + 1) {
				if ( (CM_Locate_DevNodeA(&parent_inst, devid, 0) == 0)
				  && (CM_Get_Child(&device_inst, parent_inst, 0) == 0)
				  && (device_inst == dev_info_data.DevInst) ) {
					BOOL post_backslash = FALSE;
					// If we're not dealing with the USBSTOR part of our list, then this is an UASP device
					is_UASP = ((((uintptr_t)devid)+2) >= ((uintptr_t)devid_list)+list_size[0]);
					for (j=0, k=0; (j<strlen(devid))&&(k<2); j++) {
						// The ID is in the form USB_VENDOR_BUSID\VID_xxxx&PID_xxxx\...
						if (devid[j] == '\\')
							post_backslash = TRUE;
						if (!post_backslash)
							continue;
						if (devid[j] == '_') {
							pid = (uint16_t)strtoul(&devid[j+1], NULL, 16);
							// We could have used a vid_pid[] table, but keeping vid/pid separate is clearer
							if (k++==0) vid = pid;
						}
					}
				}
			}
		}
		if (is_VHD) {
			uprintf("Found VHD device '%s'\n", buffer);
		} else {
			if ((vid == 0) && (pid == 0)) {
				if (is_SCSI) {
					// If we have an SCSI drive and couldn't get a VID:PID, we are most likely
					// dealing with a system drive => eliminate it!
					continue;
				}
				safe_strcpy(str, sizeof(str), "????:????");	// Couldn't figure VID:PID
			} else {
				safe_sprintf(str, sizeof(str), "%04X:%04X", vid, pid);
			}
			uprintf("Found %s device '%s' (%s)\n", is_UASP?"UAS":"USB", buffer, str);
		}
		devint_data.cbSize = sizeof(devint_data);
		hDrive = INVALID_HANDLE_VALUE;
		devint_detail_data = NULL;
		for (j=0; ;j++) {
			safe_closehandle(hDrive);
			safe_free(devint_detail_data);

			if (!SetupDiEnumDeviceInterfaces(dev_info, &dev_info_data, &_GUID_DEVINTERFACE_DISK, j, &devint_data)) {
				if(GetLastError() != ERROR_NO_MORE_ITEMS) {
					uprintf("SetupDiEnumDeviceInterfaces failed: %s\n", WindowsErrorString());
				} else {
					uprintf("A device was eliminated because it didn't report itself as a disk\n");
				}
				break;
			}

			if (!SetupDiGetDeviceInterfaceDetailA(dev_info, &devint_data, NULL, 0, &size, NULL)) {
				if(GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
					devint_detail_data = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)calloc(1, size);
					if (devint_detail_data == NULL) {
						uprintf("Unable to allocate data for SP_DEVICE_INTERFACE_DETAIL_DATA\n");
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
				uprintf("Could not open '%s': %s\n", devint_detail_data->DevicePath, WindowsErrorString());
				continue;
			}

			drive_number = GetDriveNumber(hDrive, devint_detail_data->DevicePath);
			if (drive_number < 0)
				continue;

			drive_index = drive_number + DRIVE_INDEX_MIN;
			if (!IsMediaPresent(drive_index)) {
				uprintf("Device eliminated because it appears to contain no media\n");
				safe_closehandle(hDrive);
				safe_free(devint_detail_data);
				break;
			}

			if (GetDriveLabel(drive_index, drive_letters, &label)) {
				if ((!enable_HDDs) && ((score = IsHDD(drive_index, vid, pid, buffer)) > 0)) {
					uprintf("Device eliminated because it was detected as an USB Hard Drive (score %d > 0)\n", score);
					uprintf("If this device is not an USB Hard Drive, please e-mail the author of this application\n");
					uprintf("NOTE: You can enable the listing of USB Hard Drives in 'Advanced Options' (after clicking the white triangle)");
					safe_closehandle(hDrive);
					safe_free(devint_detail_data);
					break;
				}

				// The empty string is returned for drives that don't have any volumes assigned
				if (drive_letters[0] == 0) {
					entry = lmprintf(MSG_046, label, drive_number, SizeToHumanReadable(GetDriveSize(drive_index), FALSE, TRUE));
				} else {
					// We have multiple volumes assigned to the same device (multiple partitions)
					// If that is the case, use "Multiple Volumes" instead of the label
					safe_strcpy(entry_msg, sizeof(entry_msg), ((drive_letters[0] != 0) && (drive_letters[1] != 0))?
						lmprintf(MSG_047):label);
					for (k=0; drive_letters[k]; k++) {
						// Append all the drive letters we detected
						letter_name[2] = drive_letters[k];
						if (right_to_left_mode)
							safe_strcat(entry_msg, sizeof(entry_msg), RIGHT_TO_LEFT_MARK);
						safe_strcat(entry_msg, sizeof(entry_msg), letter_name);
						if (drive_letters[k] == app_dir[0]) break;
					}
					// Repeat as we need to break the outside loop
					if (drive_letters[k] == app_dir[0]) {
						uprintf("Removing %c: from the list: This is the disk from which " APPLICATION_NAME " is running!\n", app_dir[0]);
						safe_closehandle(hDrive);
						safe_free(devint_detail_data);
						break;
					}
					safe_sprintf(&entry_msg[strlen(entry_msg)], sizeof(entry_msg) - strlen(entry_msg),
						" [%s]", SizeToHumanReadable(GetDriveSize(drive_index), FALSE, TRUE));
					entry = entry_msg;
				}

				// Must ensure that the combo box is UNSORTED for indexes to be the same
				StrArrayAdd(&DriveID, buffer);
				StrArrayAdd(&DriveLabel, label);

				IGNORE_RETVAL(ComboBox_SetItemData(hDeviceList, ComboBox_AddStringU(hDeviceList, entry), drive_index));
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
	safe_free(devid_list);
	return TRUE;
}

/*
 * Set up progress bar real estate allocation
 */
static void InitProgress(void)
{
	int i, fs;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	memset(&nb_slots, 0, sizeof(nb_slots));
	memset(&slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	memset(nb_slots, 0, sizeof(nb_slots));
	memset(slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	nb_slots[OP_ANALYZE_MBR] = 1;
	if (IsChecked(IDC_BADBLOCKS)) {
		nb_slots[OP_BADBLOCKS] = -1;
	}
	if (IsChecked(IDC_BOOT)) {
		// 1 extra slot for PBR writing
		switch (selection_default) {
		case DT_WINME:
			nb_slots[OP_DOS] = 3+1;
			break;
		case DT_FREEDOS:
			nb_slots[OP_DOS] = 5+1;
			break;
		case DT_IMG:
			nb_slots[OP_DOS] = 0;
			break;
		case DT_ISO:
			nb_slots[OP_DOS] = -1;
			break;
		default:
			nb_slots[OP_DOS] = 2+1;
			break;
		}
	}
	if (selection_default == DT_IMG) {
		nb_slots[OP_FORMAT] = -1;
	} else {
		nb_slots[OP_ZERO_MBR] = 1;
		nb_slots[OP_PARTITION] = 1;
		nb_slots[OP_FIX_MBR] = 1;
		nb_slots[OP_CREATE_FS] =
			nb_steps[ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))];
		if ( (!IsChecked(IDC_QUICKFORMAT))
		  || ((fs == FS_FAT32) && ((SelectedDrive.DiskSize >= LARGE_FAT32_SIZE) || (force_large_fat32))) ) {
			nb_slots[OP_FORMAT] = -1;
		}
		nb_slots[OP_FINALIZE] = ((selection_default == DT_ISO) && (fs == FS_NTFS))?3:2;
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
		duprintf("UpdateProgress: invalid op %d\n", op);
		return;
	}
	if (percent > 100.1f) {
		duprintf("UpdateProgress(%d): invalid percentage %0.2f\n", op, percent);
		return;
	}
	if ((percent < 0.0f) && (nb_slots[op] <= 0)) {
		duprintf("UpdateProgress(%d): error negative percentage sent for negative slot value\n", op);
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
		duprintf("UpdateProgress(%d): rounding error - pos %d is greater than %d\n", op, pos, MAX_PROGRESS);
		pos = MAX_PROGRESS;
	}

	SendMessage(hProgress, PBM_SETPOS, (WPARAM)pos, 0);
	SetTaskbarProgressValue(pos, MAX_PROGRESS);
}

/*
 * Toggle controls according to operation
 */
static void EnableControls(BOOL bEnable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_DEVICE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_START), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_BADBLOCKS), bEnable);
	EnableBootOptions(bEnable, FALSE);
	EnableWindow(hSelectISO, bEnable);
	EnableWindow(hNBPasses, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ADVANCED), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LANG), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ENABLE_FIXED_DISKS), bEnable);
	SetDlgItemTextU(hMainDialog, IDCANCEL, lmprintf(bEnable?MSG_006:MSG_007));
	if (selection_default == DT_IMG)
		return;
	EnableWindow(GetDlgItem(hMainDialog, IDC_PARTITION_TYPE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), bEnable);
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
		apply_localization(IDD_LOG, hDlg);
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
			if (log_size <= 0)
				break;
			log_buffer = (char*)malloc(log_size);
			if (log_buffer != NULL) {
				log_size = GetDlgItemTextU(hDlg, IDC_LOG_EDIT, log_buffer, log_size);
				if (log_size != 0) {
					log_size--;	// remove NUL terminator
					filepath =  FileDialog(TRUE, app_dir, "rufus.log", "log", lmprintf(MSG_108), 0);
					if (filepath != NULL) {
						FileIO(TRUE, filepath, &log_buffer, &log_size);
					}
					safe_free(filepath);
				}
				safe_free(log_buffer);
			}
			break;
		}
		break;
	case WM_CLOSE:
		ShowWindow(hDlg, SW_HIDE);
		reset_localization(IDD_LOG);
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
 * Device Refresh Timer
 */
static void CALLBACK RefreshTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	// DO NOT USE WM_DEVICECHANGE - IT MAY BE FILTERED OUT BY WINDOWS!
	SendMessage(hWnd, UM_MEDIA_CHANGE, 0, 0);
}

/*
 * Detect and notify about a blocking operation during ISO extraction cancellation
 */
static void CALLBACK BlockingTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	if (iso_blocking_status < 0) {
		KillTimer(hMainDialog, TID_BLOCKING_TIMER);
		user_notified = FALSE;
		uprintf("Killed blocking I/O timer\n");
	} else if(!user_notified) {
		if (last_iso_blocking_status == iso_blocking_status) {
			// A write or close operation hasn't made any progress since our last check
			user_notified = TRUE;
			uprintf("Blocking I/O operation detected\n");
			MessageBoxU(hMainDialog, lmprintf(MSG_080), lmprintf(MSG_048), MB_OK|MB_ICONINFORMATION|MB_IS_RTL);
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
		apply_localization(IDD_ISO_EXTRACT, hDlg);
		hISOProgressBar = GetDlgItem(hDlg, IDC_PROGRESS);
		hISOFileName = GetDlgItem(hDlg, IDC_ISO_FILENAME);
		// Use maximum granularity for the progress bar
		SendMessage(hISOProgressBar, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
		return TRUE;
	case UM_ISO_INIT:
		iso_op_in_progress = TRUE;
		EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), TRUE);
		CenterDialog(hDlg);
		ShowWindow(hDlg, SW_SHOW);
		UpdateWindow(hDlg);
		return TRUE;
	case UM_ISO_EXIT:
		// Just hide and recenter the dialog
		ShowWindow(hDlg, SW_HIDE);
		iso_op_in_progress = FALSE;
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_ISO_ABORT:
			FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
			PrintStatus(0, FALSE, MSG_201);
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

BOOL IsImage(const char* src_img)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	LARGE_INTEGER liImageSize;

	handle = CreateFileU(src_img, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not open image '%s'", src_img);
		goto out;
	}
	iso_report.is_bootable_img = AnalyzeMBR(handle, "Image");

	if (!GetFileSizeEx(handle, &liImageSize)) {
		uprintf("Could not get image size: %s", WindowsErrorString());
		goto out;
	}
	iso_report.projected_size = (uint64_t)liImageSize.QuadPart;

	if (iso_report.is_bootable_img) {
		uprintf("Using bootable disk image: '%s'", src_img);
		selection_default = DT_IMG;
	}

out:
	safe_closehandle(handle);
	return iso_report.is_bootable_img;
}

// The scanning process can be blocking for message processing => use a thread
DWORD WINAPI ISOScanThread(LPVOID param)
{
	int i;
	BOOL r;
	char isolinux_str[16] = "No";

	if (iso_path == NULL)
		goto out;
	PrintStatus(0, TRUE, MSG_202);
	user_notified = FALSE;
	EnableControls(FALSE);
	r = ExtractISO(iso_path, "", TRUE) || IsImage(iso_path);
	EnableControls(TRUE);
	if (!r) {
		SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
		PrintStatus(0, TRUE, MSG_203);
		safe_free(iso_path);
		goto out;
	}

	if (!iso_report.is_bootable_img) {
		if (HAS_SYSLINUX(iso_report)) {
			safe_sprintf(isolinux_str, sizeof(isolinux_str), "Yes (%s)", iso_report.sl_version_str);
		}
		uprintf("ISO label: '%s'\r\n  Size: %lld bytes\r\n  Has a >64 chars filename: %s\r\n  Has Symlinks: %s\r\n  Has a >4GB file: %s\r\n"
			"  ReactOS: %s\r\n  Uses EFI: %s%s\r\n  Uses Bootmgr: %s\r\n  Uses WinPE: %s%s\r\n  Uses isolinux: %s\r\n",
			iso_report.label, iso_report.projected_size, iso_report.has_long_filename?"Yes":"No", iso_report.has_symlinks?"Yes":"No",
			iso_report.has_4GB_file?"Yes":"No", IS_REACTOS(iso_report)?"Yes":"No", (iso_report.has_efi || iso_report.has_win7_efi)?"Yes":"No",
			(iso_report.has_win7_efi && (!iso_report.has_efi))?" (win7_x64)":"", iso_report.has_bootmgr?"Yes":"No",
			IS_WINPE(iso_report.winpe)?"Yes":"No", (iso_report.uses_minint)?" (with /minint)":"", isolinux_str);
		if (HAS_SYSLINUX(iso_report) && (SL_MAJOR(iso_report.sl_version) < 5)) {
			for (i=0; i<NB_OLD_C32; i++) {
				uprintf("    With an old %s: %s\n", old_c32_name[i], iso_report.has_old_c32[i]?"Yes":"No");
			}
		}
	}
	if ( (!iso_report.has_bootmgr) && (!HAS_SYSLINUX(iso_report)) && (!IS_WINPE(iso_report.winpe)) 
		&& (!iso_report.has_efi) && (!IS_REACTOS(iso_report) && (!iso_report.is_bootable_img)) ) {
		MessageBoxU(hMainDialog, lmprintf(MSG_082), lmprintf(MSG_081), MB_OK|MB_ICONINFORMATION|MB_IS_RTL);
		safe_free(iso_path);
		SetMBRProps();
	} else {
		// Enable bootable and set Target System and FS accordingly
		CheckDlgButton(hMainDialog, IDC_BOOT, BST_CHECKED);
		if (!iso_report.is_bootable_img) {
			SetTargetSystem();
			SetFSFromISO();
			SetMBRProps();
			// Some Linux distros, such as Arch Linux, require the USB drive to have
			// a specific label => copy the one we got from the ISO image
			if (iso_report.label[0] != 0) {
				SetWindowTextU(hLabel, iso_report.label);
			}
		} else {
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
		}
		for (i=(int)safe_strlen(iso_path); (i>0)&&(iso_path[i]!='\\'); i--);
		PrintStatus(0, TRUE, MSG_205, &iso_path[i+1]);
		// Lose the focus on the select ISO (but place it on Close)
		SendMessage(hMainDialog, WM_NEXTDLGCTL,  (WPARAM)FALSE, 0);
		// Lose the focus from Close and set it back to Start
		SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
	}

out:
	SendMessage(hISOProgressDlg, UM_ISO_EXIT, 0, 0);
	ExitThread(0);
}

// Move a control along the Y axis according to the advanced mode setting
void MoveCtrlY(HWND hDlg, int nID, float vertical_shift) {
	ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, nID), 0,
		(int)(advanced_mode?vertical_shift:-vertical_shift), 0, 0);
}

void SetPassesTooltip(void)
{
	const unsigned char pattern[] = BADBLOCK_PATTERNS;
	CreateTooltip(hNBPasses, lmprintf(MSG_153 + ComboBox_GetCurSel(hNBPasses),
		pattern[0], pattern[1], pattern[2], pattern[3]), -1);
}

// Toggle "advanced" mode
void ToggleAdvanced(void)
{
	float dialog_shift = 80.0f;
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
	MoveCtrlY(hMainDialog, IDC_STATUS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_START, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_PROGRESS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_ABOUT, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_LOG, dialog_shift);
	MoveCtrlY(hMainDialog, IDCANCEL, dialog_shift);
#ifdef RUFUS_TEST
	MoveCtrlY(hMainDialog, IDC_TEST, dialog_shift);
#endif

	// And do the same for the log dialog while we're at it
	GetWindowRect(hLogDlg, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hLogDlg, rect.left, rect.top, point.x,
		point.y + (int)(fScale*(advanced_mode?dialog_shift:-dialog_shift)), TRUE);
	MoveCtrlY(hLogDlg, IDC_LOG_CLEAR, dialog_shift);
	MoveCtrlY(hLogDlg, IDC_LOG_SAVE, dialog_shift);
	MoveCtrlY(hLogDlg, IDCANCEL, dialog_shift);
	GetWindowRect(hLog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top) + (int)(fScale*(advanced_mode?dialog_shift:-dialog_shift));
	SetWindowPos(hLog, 0, 0, 0, point.x, point.y, 0);
	// Don't forget to scroll the edit to the bottom after resize
	SendMessage(hLog, EM_LINESCROLL, 0, SendMessage(hLog, EM_GETLINECOUNT, 0, 0));

	// Hide or show the various advanced options
	toggle = advanced_mode?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMainDialog, IDC_ENABLE_FIXED_DISKS), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_DISK_ID), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDS_ADVANCED_OPTIONS_GRP), toggle);

	// Toggle the up/down icon
	SendMessage(GetDlgItem(hMainDialog, IDC_ADVANCED), BCM_SETIMAGELIST, 0, (LPARAM)(advanced_mode?&bi_up:&bi_down));
}

// Toggle DD Image mode
void ToggleImage(BOOL enable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_PARTITION_TYPE), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), enable);
}

static BOOL BootCheck(void)
{
	int i, fs, bt, dt, r;
	FILE* fd;
	DWORD len;
	BOOL in_files_dir = FALSE;
	const char* ldlinux = "ldlinux";
	const char* syslinux = "syslinux";
	const char* ldlinux_ext[3] = { "sys", "bss", "c32" };
	char tmp[MAX_PATH];

	syslinux_ldlinux_len[0] = 0; syslinux_ldlinux_len[1] = 0;
	dt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	if ((dt == DT_ISO) || (dt == DT_IMG)) {
		if (iso_path == NULL) {
			// Please click on the disc button to select a bootable ISO
			MessageBoxU(hMainDialog, lmprintf(MSG_087), lmprintf(MSG_086), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return FALSE;
		}
		if ((size_check) && (iso_report.projected_size > (uint64_t)SelectedDrive.DiskSize)) {
			// This ISO image is too big for the selected target
			MessageBoxU(hMainDialog, lmprintf(MSG_089), lmprintf(MSG_088), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return FALSE;
		}
		if (dt == DT_IMG) {
			if (!iso_report.is_bootable_img)
			// The selected image doesn't match the boot option selected.
				MessageBoxU(hMainDialog, lmprintf(MSG_188), lmprintf(MSG_187), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return (iso_report.is_bootable_img);
		}
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
		if (bt == BT_UEFI) {
			if (!IS_EFI(iso_report)) {
				// Unsupported ISO
				MessageBoxU(hMainDialog, lmprintf(MSG_091), lmprintf(MSG_090), MB_OK|MB_ICONERROR|MB_IS_RTL);
				return FALSE;
			}
			if ((iso_report.has_win7_efi) && (!WimExtractCheck())) {
				// Your platform cannot extract files from WIM archives => download 7-zip?
				if (MessageBoxU(hMainDialog, lmprintf(MSG_102), lmprintf(MSG_101), MB_YESNO|MB_ICONERROR|MB_IS_RTL) == IDYES)
					ShellExecuteA(hMainDialog, "open", SEVENZIP_URL, NULL, NULL, SW_SHOWNORMAL);
				return FALSE;
			}
		} else if ((fs == FS_NTFS) && (!iso_report.has_bootmgr) && (!IS_WINPE(iso_report.winpe))) {
			if (HAS_SYSLINUX(iso_report)) {
				// Only FAT/FAT32 is supported for this type of ISO
				MessageBoxU(hMainDialog, lmprintf(MSG_096), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL);
			} else {
				// Only 'bootmgr' or 'WinPE' based ISO images can currently be used with NTFS
				MessageBoxU(hMainDialog, lmprintf(MSG_097), lmprintf(MSG_090), MB_OK|MB_ICONERROR|MB_IS_RTL);
			}
			return FALSE;
		} else if (((fs == FS_FAT16)||(fs == FS_FAT32)) && (!HAS_SYSLINUX(iso_report)) && (!IS_REACTOS(iso_report))) {
			// FAT/FAT32 can only be used for isolinux based ISO images or when the Target Type is UEFI
			MessageBoxU(hMainDialog, lmprintf(MSG_098), lmprintf(MSG_090), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return FALSE;
		}
		if (((fs == FS_FAT16)||(fs == FS_FAT32)) && (iso_report.has_4GB_file)) {
			// This ISO image contains a file larger than 4GB file (FAT32)
			MessageBoxU(hMainDialog, lmprintf(MSG_100), lmprintf(MSG_099), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return FALSE;
		}

		if (HAS_SYSLINUX(iso_report)) {
			if (SL_MAJOR(iso_report.sl_version) < 5) {
				IGNORE_RETVAL(_chdirU(app_dir));
				for (i=0; i<NB_OLD_C32; i++) {
					if (iso_report.has_old_c32[i]) {
						if (!in_files_dir) {
							IGNORE_RETVAL(_mkdir(FILES_DIR));
							IGNORE_RETVAL(_chdir(FILES_DIR));
							in_files_dir = TRUE;
						}
						static_sprintf(tmp, "%s-%s/%s", syslinux, embedded_sl_version_str[0], old_c32_name[i]);
						fd = fopen(tmp, "rb");
						if (fd != NULL) {
							// If a file already exists in the current directory, use that one
							uprintf("Will replace obsolete '%s' from ISO with the one found in './" FILES_DIR "/%s'\n", old_c32_name[i], tmp);
							fclose(fd);
							use_own_c32[i] = TRUE;
						} else {
							PrintStatus(0, FALSE, MSG_204, old_c32_name[i]);
							if (MessageBoxU(hMainDialog, lmprintf(MSG_084, old_c32_name[i], old_c32_name[i]),
									lmprintf(MSG_083, old_c32_name[i]), MB_YESNO|MB_ICONWARNING|MB_IS_RTL) == IDYES) {
								static_sprintf(tmp, "%s-%s", syslinux, embedded_sl_version_str[0]);
								IGNORE_RETVAL(_mkdir(tmp));
								SetWindowTextU(hISOProgressDlg, lmprintf(MSG_085, old_c32_name[i]));
								static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, syslinux, embedded_sl_version_str[0], old_c32_name[i]);
								SetWindowTextU(hISOFileName, tmp);
								len = DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hISOProgressDlg);
								if (len == 0) {
									uprintf("Couldn't download the files - cancelling\n");
									return FALSE;
								}
								use_own_c32[i] = TRUE;
							}
						}
					}
				}
			} else if (iso_report.sl_version != embedded_sl_version[1]) {
				// Unlike what was the case for v4 and earlier, Syslinux v5+ versions are INCOMPATIBLE with one another!
				IGNORE_RETVAL(_chdirU(app_dir));
				IGNORE_RETVAL(_mkdir(FILES_DIR));
				IGNORE_RETVAL(_chdir(FILES_DIR));
				for (i=0; i<2; i++) {
					// Check if we already have the relevant ldlinux_v#.##.sys & ldlinux_v#.##.bss files
					static_sprintf(tmp, "%s-%s/%s.%s", syslinux, iso_report.sl_version_str, ldlinux, ldlinux_ext[i]);
					fd = fopen(tmp, "rb");
					if (fd != NULL) {
						fseek(fd, 0, SEEK_END);
						syslinux_ldlinux_len[i] = (DWORD)ftell(fd);
						fclose(fd);
					}
				}
				if ((syslinux_ldlinux_len[0] != 0) && (syslinux_ldlinux_len[1] != 0)) {
					uprintf("Will reuse '%s.%s' and '%s.%s' from './" FILES_DIR "/%s/%s-%s/' for Syslinux installation\n",
						ldlinux, ldlinux_ext[0], ldlinux, ldlinux_ext[1], FILES_DIR, syslinux, iso_report.sl_version_str);
				} else {
					r = MessageBoxU(hMainDialog, lmprintf(MSG_114, iso_report.sl_version_str, embedded_sl_version_str[1]),
						lmprintf(MSG_115), MB_YESNO|MB_ICONWARNING|MB_IS_RTL);
					if (r != IDYES)
						return FALSE;
					for (i=0; i<2; i++) {
						static_sprintf(tmp, "%s-%s", syslinux, iso_report.sl_version_str);
						IGNORE_RETVAL(_mkdir(tmp));
						static_sprintf(tmp, "%s.%s %s", ldlinux, ldlinux_ext[i], iso_report.sl_version_str);
						SetWindowTextU(hISOProgressDlg, lmprintf(MSG_085, tmp));
						static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, iso_report.sl_version_str, ldlinux, ldlinux_ext[i]);
						SetWindowTextU(hISOFileName, tmp);
						syslinux_ldlinux_len[i] = DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hISOProgressDlg);
						if (syslinux_ldlinux_len[i] == 0) {
							uprintf("Couldn't download the files - cancelling\n");
							return FALSE;
						}
					}
				}
			}
		}
	} else if (dt == DT_SYSLINUX_V5) {
		IGNORE_RETVAL(_chdirU(app_dir));
		IGNORE_RETVAL(_mkdir(FILES_DIR));
		IGNORE_RETVAL(_chdir(FILES_DIR));
		static_sprintf(tmp, "%s-%s/%s.%s", syslinux, embedded_sl_version_str[1], ldlinux, ldlinux_ext[2]);
		fd = fopenU(tmp, "rb");
		if (fd != NULL) {
			uprintf("Will reuse './%s/%s' for Syslinux installation\n", FILES_DIR, tmp);
			fclose(fd);
		} else {
			static_sprintf(tmp, "%s.%s", ldlinux, ldlinux_ext[2]);
			PrintStatus(0, FALSE, MSG_206, tmp);
			// MSG_104: "Syslinux v5.0 or later requires a '%s' file to be installed"
			r = MessageBoxU(hMainDialog, lmprintf(MSG_104, tmp, tmp),
				lmprintf(MSG_103, tmp), MB_YESNOCANCEL|MB_ICONWARNING|MB_IS_RTL);
			if (r == IDCANCEL)
				return FALSE;
			if (r == IDYES) {
				static_sprintf(tmp, "%s-%s", syslinux, embedded_sl_version_str[1]);
				IGNORE_RETVAL(_mkdir(tmp));
				static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, embedded_sl_version_str[1], ldlinux, ldlinux_ext[2]);
				SetWindowTextU(hISOProgressDlg, lmprintf(MSG_085, tmp));
				SetWindowTextU(hISOFileName, tmp);
				DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hISOProgressDlg);
			}
		}
	} else if (dt == DT_WINME) {
		if ((size_check) && (ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)) >= 65536)) {
			// MS-DOS cannot boot from a drive using a 64 kilobytes Cluster size
			MessageBoxU(hMainDialog, lmprintf(MSG_110), lmprintf(MSG_111), MB_OK|MB_ICONERROR|MB_IS_RTL);
			return FALSE;
		}
	}
	return TRUE;
}


void InitDialog(HWND hDlg)
{
	HINSTANCE hDllInst;
	DWORD len;
	HDC hDC;
	int i, i16, s16;
	char tmp[128], *token, *buf;
	static char* resource[2] = { MAKEINTRESOURCEA(IDR_SL_LDLINUX_V4_SYS), MAKEINTRESOURCEA(IDR_SL_LDLINUX_V5_SYS) };

#ifdef RUFUS_TEST
	ShowWindow(GetDlgItem(hDlg, IDC_TEST), SW_SHOW);
#endif

	// Quite a burden to carry around as parameters
	hMainDialog = hDlg;
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
	hPartitionScheme = GetDlgItem(hDlg, IDC_PARTITION_TYPE);
	hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
	hClusterSize = GetDlgItem(hDlg, IDC_CLUSTERSIZE);
	hLabel = GetDlgItem(hDlg, IDC_LABEL);
	hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
	hBoot = GetDlgItem(hDlg, IDC_BOOT);
	hBootType = GetDlgItem(hDlg, IDC_BOOTTYPE);
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
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;

	// Create the title bar icon
	SetTitleBarIcon(hDlg);
	GetWindowTextA(hDlg, tmp, sizeof(tmp));
	// Count of Microsoft for making it more attractive to read a
	// version using strtok() than using GetFileVersionInfo()
	token = strtok(tmp, " ");
	for (i=0; (i<4) && ((token = strtok(NULL, ".")) != NULL); i++)
		rufus_version[i] = (uint16_t)atoi(token);
	if (right_to_left_mode) {
		static_sprintf(tmp, "%d.%d.%d.%d " APPLICATION_NAME, rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]);
		SetWindowTextU(hDlg, tmp);
	}
	uprintf(APPLICATION_NAME " version: %d.%d.%d.%d\n", rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]);
	for (i=0; i<ARRAYSIZE(resource); i++) {
		buf = (char*)GetResource(hMainInstance, resource[i], _RT_RCDATA, "ldlinux_sys", &len, FALSE);
		if ((buf == NULL) || (len < 16)) {
			uprintf("Warning: could not read embedded Syslinux v%d version", i+4);
		} else {
			embedded_sl_version[i] = (((uint8_t)strtoul(&buf[0xb], &token, 10))<<8) + (uint8_t)strtoul(&token[1], NULL, 10);
			static_sprintf(embedded_sl_version_str[i], "%d.%02d", SL_MAJOR(embedded_sl_version[i]), SL_MINOR(embedded_sl_version[i]));
		}
	}
	uprintf("Syslinux versions: %s, %s", embedded_sl_version_str[0], embedded_sl_version_str[1]);
	uprintf("Windows version: %s\n", WindowsVersionStr);
	uprintf("Locale ID: 0x%04X\n", GetUserDefaultUILanguage());

	SetClusterSizeLabels();

	// Prefer FreeDOS to MS-DOS
	selection_default = DT_FREEDOS;
	// Create the status line and initialize the taskbar icon for progress overlay
	CreateStatusBar();
	CreateTaskbarList();
	SetTaskbarProgressState(TASKBAR_NORMAL);

	// Use maximum granularity for the progress bar
	SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
	// Fill up the passes
	for (i=0; i<4; i++) {
		IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, lmprintf((i==0)?MSG_034:MSG_035, i+1)));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hNBPasses, 1));
	SetPassesTooltip();
	// Fill up the DOS type dropdown
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "MS-DOS"), DT_WINME));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "FreeDOS"), DT_FREEDOS));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_036)), DT_ISO));
	IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, selection_default));
	// Fill up the MBR masqueraded disk IDs ("8 disks should be enough for anybody")
	IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_030, "0x80")), 0x80));
	for (i=1; i<=7; i++) {
		IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_109, 0x80+i, i+1)), 0x80+i));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));

	// Create the string array
	StrArrayCreate(&DriveID, MAX_DRIVES);
	StrArrayCreate(&DriveLabel, MAX_DRIVES);
	// Set various checkboxes
	CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_BOOT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_SET_ICON, BST_CHECKED);

	// Load system icons (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hDllInst = LoadLibraryA("shell32.dll");
	hIconDisc = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(12), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR|LR_SHARED);
	hIconLang = (HICON)LoadImage(hDllInst, MAKEINTRESOURCE(244), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR|LR_SHARED);
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
	bi_lang.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_lang.himl, -1, hIconLang);
	SetRect(&bi_lang.margin, 0, 1, 0, 0);
	bi_lang.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_down.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_down.himl, -1, hIconDown);
	SetRect(&bi_down.margin, 0, 0, 0, 0);
	bi_down.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_up.himl = pImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	pImageList_ReplaceIcon(bi_up.himl, -1, hIconUp);
	SetRect(&bi_up.margin, 0, 0, 0, 0);
	bi_up.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;

	SendMessage(hSelectISO, BCM_SETIMAGELIST, 0, (LPARAM)&bi_iso);
	SendMessage(GetDlgItem(hDlg, IDC_LANG), BCM_SETIMAGELIST, 0, (LPARAM)&bi_lang);
	SendMessage(GetDlgItem(hDlg, IDC_ADVANCED), BCM_SETIMAGELIST, 0, (LPARAM)&bi_down);

	// Set the various tooltips
	CreateTooltip(hFileSystem, lmprintf(MSG_157), -1);
	CreateTooltip(hClusterSize, lmprintf(MSG_158), -1);
	CreateTooltip(hLabel, lmprintf(MSG_159), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ADVANCED), lmprintf(MSG_160), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_BADBLOCKS), lmprintf(MSG_161), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_QUICKFORMAT), lmprintf(MSG_162), -1);
	CreateTooltip(hBoot, lmprintf(MSG_163), -1);
	CreateTooltip(hBootType, lmprintf(MSG_164), -1);
	CreateTooltip(hSelectISO, lmprintf(MSG_165), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_SET_ICON), lmprintf(MSG_166), 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_RUFUS_MBR), lmprintf(MSG_167), 10000);
	CreateTooltip(hDiskID, lmprintf(MSG_168), 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_EXTRA_PARTITION), lmprintf(MSG_169), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ENABLE_FIXED_DISKS), lmprintf(MSG_170), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_START), lmprintf(MSG_171), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ABOUT), lmprintf(MSG_172), -1);
	// TODO: add new tooltips

	ToggleAdvanced();	// We start in advanced mode => go to basic mode

	// Process commandline parameters
	if (iso_provided) {
		// Simulate a button click for ISO selection
		PostMessage(hDlg, WM_COMMAND, IDC_SELECT_ISO, 0);
	}
}

static void PrintStatus2000(const char* str, BOOL val)
{
	PrintStatus(2000, FALSE, (val)?MSG_250:MSG_251, str);
}

void ShowLanguageMenu(HWND hDlg)
{
	POINT pt;
	HMENU menu;
	loc_cmd* lcmd = NULL;
	char lang[256];
	char *search = "()";
	char *l, *r, *dup;

	UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
	menu = CreatePopupMenu();
	list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
		// The appearance of LTR languages must be fixed for RTL menus
		if ((right_to_left_mode) && (!(lcmd->ctrl_id & LOC_RIGHT_TO_LEFT)))  {
			dup = safe_strdup(lcmd->txt[1]);
			l = strtok(dup, search);
			r = strtok(NULL, search);
			static_sprintf(lang, LEFT_TO_RIGHT_MARK "(%s) " LEFT_TO_RIGHT_MARK "%s", r, l);
			safe_free(dup);
		} else {
			safe_strcpy(lang, sizeof(lang), lcmd->txt[1]);
		}
		InsertMenuU(menu, -1, MF_BYPOSITION|((selected_locale == lcmd)?MF_CHECKED:0), UM_LANGUAGE_MENU_MAX++, lang);
	}

	SetForegroundWindow(hDlg);
	GetCursorPos(&pt);
	TrackPopupMenu(menu, TPM_TOPALIGN|TPM_RIGHTALIGN, pt.x, pt.y, 0, hMainDialog, NULL);
	DestroyMenu(menu);
}

void SetBoot(int fs, int bt)
{
	int i;

	IGNORE_RETVAL(ComboBox_ResetContent(hBootType));
	if ((bt == BT_BIOS) && ((fs == FS_FAT16) || (fs == FS_FAT32))) {
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "MS-DOS"), DT_WINME));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "FreeDOS"), DT_FREEDOS));
	}
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_036)), DT_ISO));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_095)), DT_IMG));
	// If needed (advanced mode) also append a Syslinux option
	if ( (bt == BT_BIOS) && (((fs == FS_FAT16) || (fs == FS_FAT32)) && (advanced_mode)) ) {
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "Syslinux 4"), DT_SYSLINUX_V4));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "Syslinux 5"), DT_SYSLINUX_V5));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "ReactOS"), DT_REACTOS));
	}
	if ((!advanced_mode) && (selection_default >= DT_SYSLINUX_V4)) {
		selection_default = DT_FREEDOS;
		CheckDlgButton(hMainDialog, IDC_DISK_ID, BST_UNCHECKED);
	}
	for (i=0; i<ComboBox_GetCount(hBootType); i++) {
		if (ComboBox_GetItemData(hBootType, i) == selection_default) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, i));
			break;
		}
	}
	if (i == ComboBox_GetCount(hBootType))
		IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, 0));

	if (!IsWindowEnabled(hBoot)) {
		EnableWindow(hBoot, TRUE);
		EnableWindow(hBootType, TRUE);
		EnableWindow(hSelectISO, TRUE);
		CheckDlgButton(hMainDialog, IDC_BOOT, uBootChecked);
	}
}

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	DRAWITEMSTRUCT* pDI;
	POINT Point;
	RECT DialogRect, DesktopRect;
	int nDeviceIndex, fs, bt, i, nWidth, nHeight, nb_devices, selected_language;
	static DWORD DeviceNum = 0, LastRefresh = 0;
	char tmp[128];
	static BOOL first_log_display = TRUE, user_changed_label = FALSE;
	static ULONG ulRegister = 0;
	static LPITEMIDLIST pidlDesktop = NULL;
	static MY_SHChangeNotifyEntry NotifyEntry;
	loc_cmd* lcmd = NULL;
	PF_DECL(SHChangeNotifyRegister);
	PF_DECL(SHChangeNotifyDeregister);

	switch (message) {

	case UM_MEDIA_CHANGE:
		wParam = DBT_CUSTOMEVENT;
		// Fall through
	case WM_DEVICECHANGE:
		// The Windows hotplug subsystem sucks. Among other things, if you insert a GPT partitioned
		// USB drive with zero partitions, the only device messages you will get are a stream of
		// DBT_DEVNODES_CHANGED and that's it. But those messages are also issued when you get a
		// DBT_DEVICEARRIVAL and DBT_DEVICEREMOVECOMPLETE, and there's a whole slew of them so we
		// can't really issue a refresh for each one we receive
		// What we do then is arm a timer on DBT_DEVNODES_CHANGED, if it's been more than 1 second
		// since last refresh/arm timer, and have that timer send DBT_CUSTOMEVENT when it expires.
		// DO *NOT* USE WM_DEVICECHANGE AS THE MESSAGE FROM THE TIMER PROC, as it may be filtered!
		// For instance filtering will occur when (un)plugging in a FreeBSD UFD on Windows 8.
		// Intead, use a custom user message, such as UM_MEDIA_CHANGE, to set DBT_CUSTOMEVENT.
		if (format_thid == NULL) {
			switch (wParam) {
			case DBT_DEVICEARRIVAL:
			case DBT_DEVICEREMOVECOMPLETE:
			case DBT_CUSTOMEVENT:	// Sent by our timer refresh function or for card reader media change
				LastRefresh = GetTickCount();	// Don't care about 49.7 days rollback of GetTickCount()
				KillTimer(hMainDialog, TID_REFRESH_TIMER);
				GetUSBDevices((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
				user_changed_label = FALSE;
				return (INT_PTR)TRUE;
			case DBT_DEVNODES_CHANGED:
				// If it's been more than a second since last device refresh, arm a refresh timer
				if (GetTickCount() > LastRefresh + 1000) {
					LastRefresh = GetTickCount();
					SetTimer(hMainDialog, TID_REFRESH_TIMER, 1000, RefreshTimer);
				}
				break;
			default:
				break;
			}
		}
		break;

	case WM_INITDIALOG:
		PF_INIT(SHChangeNotifyRegister, shell32);
		apply_localization(IDD_DIALOG, hDlg);
		SetUpdateCheck();
		advanced_mode = TRUE;
		// Create the log window (hidden)
		first_log_display = TRUE;
		log_displayed = FALSE;
		hLogDlg = CreateDialogW(hMainInstance, MAKEINTRESOURCEW(IDD_LOG + IDD_IS_RTL), hDlg, (DLGPROC)LogProc);
		InitDialog(hDlg);
		GetUSBDevices(0);
		CheckForUpdates(FALSE);
		// Register MEDIA_INSERTED/MEDIA_REMOVED notifications for card readers
		if ((pfSHChangeNotifyRegister != NULL) && (SUCCEEDED(SHGetSpecialFolderLocation(0, CSIDL_DESKTOP, &pidlDesktop)))) {
			NotifyEntry.pidl = pidlDesktop;
			NotifyEntry.fRecursive = TRUE;
			// NB: The following only works if the media is already formatted.
			// If you insert a blank card, notifications will not be sent... :(
			ulRegister = pfSHChangeNotifyRegister(hDlg, 0x0001|0x0002|0x8000,
				SHCNE_MEDIAINSERTED|SHCNE_MEDIAREMOVED, UM_MEDIA_CHANGE, 1, &NotifyEntry);
		}
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
		if ((LOWORD(wParam) >= UM_LANGUAGE_MENU) && (LOWORD(wParam) < UM_LANGUAGE_MENU_MAX)) {
			selected_language = LOWORD(wParam) - UM_LANGUAGE_MENU;
			i = 0;
			list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
				if (i++ == selected_language) {
					if (selected_locale != lcmd) {
						selected_locale = lcmd;
						relaunch = TRUE;
						PostMessage(hDlg, WM_COMMAND, IDCANCEL, 0);
					}
					break;
				}
			}
		}
		switch(LOWORD(wParam)) {
		case IDOK:			// close application
		case IDCANCEL:
			PF_INIT(SHChangeNotifyDeregister, shell32);
			EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), FALSE);
			EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);
			if (format_thid != NULL) {
				if (MessageBoxU(hMainDialog, lmprintf(MSG_105), lmprintf(MSG_049),
					MB_YESNO|MB_ICONWARNING|MB_IS_RTL) == IDYES) {
					// Operation may have completed in the meantime
					if (format_thid != NULL) {
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
						PrintStatus(0, FALSE, MSG_201);
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
			if ((pfSHChangeNotifyDeregister != NULL) && (ulRegister != 0))
				pfSHChangeNotifyDeregister(ulRegister);
			PostQuitMessage(0);
			StrArrayDestroy(&DriveID);
			StrArrayDestroy(&DriveLabel);
			DestroyAllTooltips();
			DestroyWindow(hLogDlg);
			GetWindowRect(hDlg, &relaunch_rc);
			EndDialog(hDlg, 0);
			break;
		case IDC_ABOUT:
			CreateAboutBox();
			break;
		case IDC_LOG:
			// Place the log Window to the right (or left for RTL) of our dialog on first display
			if (first_log_display) {
				GetClientRect(GetDesktopWindow(), &DesktopRect);
				GetWindowRect(hLogDlg, &DialogRect);
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				GetWindowRect(hDlg, &DialogRect);
				if (right_to_left_mode)
					Point.x = max(DialogRect.left - GetSystemMetrics(SM_CXSIZEFRAME)-(int)(2.0f * fScale) - nWidth, 0);
				else
					Point.x = min(DialogRect.right + GetSystemMetrics(SM_CXSIZEFRAME)+(int)(2.0f * fScale), DesktopRect.right - nWidth);
				
				Point.y = max(DialogRect.top, DesktopRect.top - nHeight);
				MoveWindow(hLogDlg, Point.x, Point.y, nWidth, nHeight, FALSE);
				// The log may have been recentered to fit the screen, in which case, try to shift our main dialog left (or right for RTL)
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				if (right_to_left_mode) {
					Point.x = DialogRect.left;
					GetWindowRect(hLogDlg, &DialogRect);
					Point.x = max(Point.x, DialogRect.right - DialogRect.left + GetSystemMetrics(SM_CXSIZEFRAME) + (int)(2.0f * fScale));
				} else {
					Point.x = max((DialogRect.left<0)?DialogRect.left:0,
						Point.x - nWidth - GetSystemMetrics(SM_CXSIZEFRAME) - (int)(2.0f * fScale));
				}
				MoveWindow(hDlg, Point.x, Point.y, nWidth, nHeight, TRUE);
				first_log_display = FALSE;
			}
			// Display the log Window
			log_displayed = !log_displayed;
			if (IsShown(hISOProgressDlg))
				SetFocus(hISOProgressDlg);
			// Set focus on the start button
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDC_START), TRUE);
			// Must come last for the log window to get focus
			ShowWindow(hLogDlg, log_displayed?SW_SHOW:SW_HIDE);
			break;
#ifdef RUFUS_TEST
		case IDC_TEST:
			break;
#endif
		case IDC_LANG:
			ShowLanguageMenu(GetDlgItem(hDlg, IDC_LANG));
			break;
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
			nb_devices = ComboBox_GetCount(hDeviceList);
			PrintStatus(0, TRUE, (nb_devices==1)?MSG_208:MSG_209, nb_devices);
			PopulateProperties(ComboBox_GetCurSel(hDeviceList));
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_NBPASSES:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetPassesTooltip();
			break;
		case IDC_PARTITION_TYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetPartitionSchemeTooltip();
			SetFSFromISO();
			// fall-through
		case IDC_FILESYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			bt = GETBIOSTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
			if ((selection_default == DT_IMG) && IsChecked(IDC_BOOT)) {
				ToggleImage(FALSE);
				EnableAdvancedBootOptions(FALSE, TRUE);
				SetBoot(fs, bt);
				break;
			}
			SetClusterSizes(fs);
			// Disable/restore the quick format control depending on large FAT32 or ReFS
			if ( ((fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32))) || (fs == FS_REFS) ) {
				if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_QUICKFORMAT))) {
					uQFChecked = IsChecked(IDC_QUICKFORMAT);
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
				EnableBootOptions(TRUE, TRUE);
				SetMBRProps();
				// Remove the SysLinux and ReactOS options if they exists
				if (ComboBox_GetItemData(hBootType, ComboBox_GetCount(hBootType)-1) == (DT_MAX-1)) {
					for (i=DT_SYSLINUX_V4; i<DT_MAX; i++)
						IGNORE_RETVAL(ComboBox_DeleteString(hBootType,  ComboBox_GetCount(hBootType)-1));
					IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, 1));
				}
				break;
			}
			if ((fs == FS_EXFAT) || (fs == FS_UDF) || (fs == FS_REFS)) {
				if (IsWindowEnabled(hBoot)) {
					// unlikely to be supported by BIOSes => don't bother
					IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, 0));
					uBootChecked = IsChecked(IDC_BOOT);
					CheckDlgButton(hDlg, IDC_BOOT, BST_UNCHECKED);
					EnableBootOptions(FALSE, TRUE);
				} else if (IsChecked(IDC_BOOT)) {
					uBootChecked = TRUE;
					CheckDlgButton(hDlg, IDC_BOOT, BST_UNCHECKED);
				}
				SetMBRProps();
				break;
			}
			EnableAdvancedBootOptions(TRUE, TRUE);
			SetBoot(fs, bt);
			SetMBRProps();
			break;
		case IDC_BOOT:
			EnableAdvancedBootOptions(TRUE, TRUE);
			if (selection_default == DT_IMG)
				ToggleImage(!IsChecked(IDC_BOOT));
			break;
		case IDC_BOOTTYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			selection_default = (int) ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			EnableAdvancedBootOptions(TRUE, TRUE);
			ToggleImage(!IsChecked(IDC_BOOT) || (selection_default != DT_IMG));
			if ((selection_default == DT_ISO) || (selection_default == DT_IMG)) {
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
			if (iso_provided) {
				uprintf("Commandline Image provided: '%s'\n", iso_path);
				iso_provided = FALSE;	// One off thing...
			} else {
				safe_free(iso_path);
				if (selection_default == DT_IMG)
					iso_path = FileDialog(FALSE, NULL, "*.img", "img", "DD Image", 0);
				else
					iso_path = FileDialog(FALSE, NULL, "*.iso", "iso", lmprintf(MSG_036), 0);
				if (iso_path == NULL) {
					CreateTooltip(hSelectISO, lmprintf(MSG_173), -1);
					break;
				}
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
		case IDC_ENABLE_FIXED_DISKS:
			if ((HIWORD(wParam)) == BN_CLICKED) {
				enable_HDDs = !enable_HDDs;
				PrintStatus2000(lmprintf(MSG_253), enable_HDDs);
				GetUSBDevices(0);
			}
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
			selection_default = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			if (nDeviceIndex != CB_ERR) {
				if ((IsChecked(IDC_BOOT)) && (!BootCheck())) {
					format_op_in_progress = FALSE;
					break;
				}

				// Display a warning about UDF formatting times
				fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
				if (fs == FS_UDF) {
					dur_secs = (uint32_t)(((double)SelectedDrive.DiskSize)/1073741824.0f/UDF_FORMAT_SPEED);
					if (dur_secs > UDF_FORMAT_WARN) {
						dur_mins = dur_secs/60;
						dur_secs -= dur_mins*60;
						MessageBoxU(hMainDialog, lmprintf(MSG_112, dur_mins, dur_secs), lmprintf(MSG_113), MB_OK|MB_ICONASTERISK|MB_IS_RTL);
					} else {
						dur_secs = 0;
						dur_mins = 0;
					}
				}

				GetWindowTextU(hDeviceList, tmp, ARRAYSIZE(tmp));
				if (MessageBoxU(hMainDialog, lmprintf(MSG_003, tmp),
					APPLICATION_NAME, MB_OKCANCEL|MB_ICONWARNING|MB_IS_RTL) == IDCANCEL) {
					format_op_in_progress = FALSE;
					break;
				}
				if ((SelectedDrive.nPartitions > 1) && (MessageBoxU(hMainDialog, lmprintf(MSG_093),
					lmprintf(MSG_094), MB_OKCANCEL|MB_ICONWARNING|MB_IS_RTL) == IDCANCEL)) {
					format_op_in_progress = FALSE;
					break;
				}

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
				uprintf("\r\nFormat operation started");
				PrintStatus(0, FALSE, -1);
				timer = 0;
				safe_sprintf(szTimer, sizeof(szTimer), "00:00:00");
				SendMessageA(GetDlgItem(hMainDialog, IDC_STATUS), SB_SETTEXTA,
					SBT_OWNERDRAW | 1, (LPARAM)szTimer);
				SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
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
			hISOProgressDlg = CreateDialogW(hMainInstance, MAKEINTRESOURCEW(IDD_ISO_EXTRACT + IDD_IS_RTL),
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
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), lmprintf(MSG_049)), WM_COMMAND, IDNO, 0);
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), lmprintf(MSG_049)), WM_COMMAND, IDYES, 0);
		EnableWindow(GetDlgItem(hISOProgressDlg, IDC_ISO_ABORT), TRUE);
		EnableWindow(GetDlgItem(hMainDialog, IDCANCEL), TRUE);
		EnableControls(TRUE);
		uprintf("\r\n");
		GetUSBDevices(DeviceNum);
		if (!IS_ERROR(FormatStatus)) {
			// This is the only way to achieve instantaneous progress transition to 100%
			SendMessage(hProgress, PBM_SETRANGE, 0, ((MAX_PROGRESS+1)<<16) & 0xFFFF0000);
			SendMessage(hProgress, PBM_SETPOS, (MAX_PROGRESS+1), 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
			SetTaskbarProgressState(TASKBAR_NOPROGRESS);
			PrintStatus(0, FALSE, MSG_210);
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_PAUSED, 0);
			SetTaskbarProgressState(TASKBAR_PAUSED);
			PrintStatus(0, FALSE, MSG_211);
			Notification(MSG_INFO, NULL, lmprintf(MSG_211), lmprintf(MSG_041));
		} else {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			PrintStatus(0, FALSE, MSG_212);
			Notification(MSG_ERROR, NULL, lmprintf(MSG_042), lmprintf(MSG_043, StrError(FormatStatus, FALSE)), StrError(FormatStatus, FALSE));
		}
		FormatStatus = 0;
		format_op_in_progress = FALSE;
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

static void PrintUsage(char* appname)
{
	char fname[_MAX_FNAME];

	_splitpath(appname, NULL, NULL, fname, NULL);
	printf("\nUsage: %s [-h] [-i PATH] [-w TIMEOUT]\n", fname);
	printf("  -i PATH, --iso=PATH\n");
	printf("     Select the ISO image pointed by PATH to be used on startup\n");
	printf("  -w TIMEOUT, --wait=TIMEOUT\n");
	printf("     Wait TIMEOUT tens of a second for the global application mutex to be released.\n");
	printf("     Used when launching a newer version of " APPLICATION_NAME " from a running application.\n");
	printf("  -h, --help\n");
	printf("     This usage guide.\n");
}

/* There's a massive annoyance when taking over the console in a win32 app
 * in that it doesn't return the prompt on app exit. So we must handle that
 * manually, but the *ONLY* frigging way to achieve it is by simulating a
 * keypress... which means we first need to bring our console back on top.
 * And people wonder why developing elegant Win32 apps takes forever...
 */
static void DetachConsole(void)
{
	INPUT input;
	HWND hWnd;

	hWnd = GetConsoleWindow();
	SetWindowPos(hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
	FreeConsole();
	memset(&input, 0, sizeof(input));
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = VK_RETURN;
	SendInput(1, &input, sizeof(input));
	input.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &input, sizeof(input));
}

/*
 * Application Entrypoint
 */
typedef int (CDECL *__wgetmainargs_t)(int*, wchar_t***, wchar_t***, int, int*);
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	const char* old_wait_option = "/W";
	const char* rufus_loc = "rufus.loc";
	int i, opt, option_index = 0, argc = 0, si = 0, lcid = GetUserDefaultUILanguage();
	BOOL attached_console = FALSE, external_loc_file = FALSE, lgp_set = FALSE;
	BYTE* loc_data;
	DWORD loc_size, Size;
	char tmp_path[MAX_PATH] = "", loc_file[MAX_PATH] = "", *tmp, *locale_name = NULL;
	char** argv = NULL;
	wchar_t **wenv, **wargv;
	PF_DECL(__wgetmainargs);
	HANDLE mutex = NULL, hFile = NULL;
	HWND hDlg = NULL;
	MSG msg;
	int wait_for_mutex = 0;
	struct option long_options[] = {
		{"help",    no_argument,       NULL, 'h'},
		{"iso",     required_argument, NULL, 'i'},
		{"wait",    required_argument, NULL, 'w'},
		{0, 0, NULL, 0}
	};

	uprintf("*** " APPLICATION_NAME " init ***\n");

	// Reattach the console, if we were started from commandline
	if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
		attached_console = TRUE;
		IGNORE_RETVAL(freopen("CONIN$", "r", stdin));
		IGNORE_RETVAL(freopen("CONOUT$", "w", stdout));
		IGNORE_RETVAL(freopen("CONOUT$", "w", stderr));
		_flushall();
		printf("\n");
	}

	// Use the Locale specified in the registry, if any
	tmp = ReadRegistryKeyStr(REGKEY_HKCU, REGKEY_LOCALE);
	if (tmp[0] != 0) {
		locale_name = safe_strdup(tmp);
		uprintf("found registry locale '%s'", locale_name);
	}

	// We have to process the arguments before we acquire the lock and process the locale
	PF_INIT(__wgetmainargs, msvcrt);
	if (pf__wgetmainargs != NULL) {
		pf__wgetmainargs(&argc, &wargv, &wenv, 1, &si);
		argv = (char**)calloc(argc, sizeof(char*));
		for (i=0; i<argc; i++) {
			argv[i] = wchar_to_utf8(wargv[i]);
			// Check for "/W" (wait for mutex release for pre 1.3.3 versions)
			if (safe_strcmp(argv[i], old_wait_option) == 0)
				wait_for_mutex = 150;	// Try to acquire the mutex for 15 seconds
		}

		while ((opt = getopt_long(argc, argv, "?fhi:w:l:", long_options, &option_index)) != EOF)
			switch (opt) {
			case 'f':
				enable_HDDs = TRUE;
				break;
			case 'i':
				if (_access(optarg, 0) != -1) {
					iso_path = safe_strdup(optarg);
					iso_provided = TRUE;
				} else {
					printf("Could not find ISO image '%s'\n", optarg);
				}
				break;
			case 'l':
				if (isdigitU(optarg[0])) {
					lcid = (int)strtol(optarg, NULL, 0);
				} else {
					safe_free(locale_name);
					locale_name =safe_strdup(optarg);
				}
				break;
			case 'w':
				wait_for_mutex = atoi(optarg);
				break;
			case '?':
			case 'h':
			default:
				PrintUsage(argv[0]);
				goto out;
		}
	} else {
		uprintf("unable to access UTF-16 args");
	}

	// Retrieve the current application directory
	GetCurrentDirectoryU(MAX_PATH, app_dir);

	// Init localization
	init_localization();
	// Seek for a loc file in the current directory
	if (GetFileAttributesU(rufus_loc) == INVALID_FILE_ATTRIBUTES) {
		uprintf("loc file not found in current directory - embedded one will be used");

		loc_data = (BYTE*)GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_LC_RUFUS_LOC), _RT_RCDATA, "embedded.loc", &loc_size, FALSE);
		if ( (GetTempPathU(sizeof(tmp_path), tmp_path) == 0)
		  || (GetTempFileNameU(tmp_path, APPLICATION_NAME, 0, loc_file) == 0)
		  || (loc_file[0] == 0) ) {
			// Last ditch effort to get a loc file - just extract it to the current directory
			safe_strcpy(loc_file, sizeof(loc_file), rufus_loc);
		}

		hFile = CreateFileU(loc_file, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS, 0, 0);
		if ((hFile == INVALID_HANDLE_VALUE) || (!WriteFile(hFile, loc_data, loc_size, &Size, 0)) || (loc_size != Size)) {
			uprintf("localization: unable to extract '%s': %s.\n", loc_file, WindowsErrorString());
			safe_closehandle(hFile);
			goto out;
		}
		uprintf("localization: extracted data to '%s'\n", loc_file);
		safe_closehandle(hFile);
	} else {
		safe_sprintf(loc_file, sizeof(loc_file), "%s\\%s", app_dir, rufus_loc);
		external_loc_file = TRUE;
		uprintf("using external loc file '%s'", loc_file);
	}

	if ( (!get_supported_locales(loc_file))
	  || ((selected_locale = ((locale_name == NULL)?get_locale_from_lcid(lcid, TRUE):get_locale_from_name(locale_name, TRUE))) == NULL) ) {
		uprintf("FATAL: Could not access locale!\n");
		MessageBoxU(NULL, "The locale data is missing or invalid. This application will now exit.", "Fatal error", MB_ICONSTOP|MB_IS_RTL);
		goto out;
	}

	// Prevent 2 applications from running at the same time, unless "/W" is passed as an option
	// in which case we wait for the mutex to be relinquished
	if ((safe_strlen(lpCmdLine)==2) && (lpCmdLine[0] == '/') && (lpCmdLine[1] == 'W'))
		wait_for_mutex = 150;		// Try to acquire the mutex for 15 seconds
	mutex = CreateMutexA(NULL, TRUE, "Global/" APPLICATION_NAME);
	for (;(wait_for_mutex>0) && (mutex != NULL) && (GetLastError() == ERROR_ALREADY_EXISTS); wait_for_mutex--) {
		CloseHandle(mutex);
		Sleep(100);
		mutex = CreateMutexA(NULL, TRUE, "Global/" APPLICATION_NAME);
	}
	if ((mutex == NULL) || (GetLastError() == ERROR_ALREADY_EXISTS)) {
		// Load the translation before we print the error
		get_loc_data_file(loc_file, selected_locale);
		MessageBoxU(NULL, lmprintf(MSG_002), lmprintf(MSG_001), MB_ICONSTOP|MB_IS_RTL);
		goto out;
	}

	// Save instance of the application for further reference
	hMainInstance = hInstance;

	// Initialize COM for folder selection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

	// Some dialogs have Rich Edit controls and won't display without this
	if (LoadLibraryA("Riched20.dll") == NULL) {
		uprintf("Could not load RichEdit library - some dialogs may not display: %s\n", WindowsErrorString());
	}

	// Set the Windows version
	GetWindowsVersion();

	// We use local group policies rather than direct registry manipulation
	// 0x9e disables removable and fixed drive notifications
	lgp_set = SetLGP(FALSE, &existing_key, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0x9e);

relaunch:
	uprintf("localization: using locale '%s'\n", selected_locale->txt[0]);
	right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT);
	SetProcessDefaultLayout(right_to_left_mode?LAYOUT_RTL:0);
	if (get_loc_data_file(loc_file, selected_locale))
		WriteRegistryKeyStr(REGKEY_HKCU, REGKEY_LOCALE, selected_locale->txt[0]);

	// Destroy the ISO progress window, if it exists
	if (hISOProgressDlg != NULL) {
		DestroyWindow(hISOProgressDlg);
		hISOProgressDlg = NULL;
	}

	/*
	 * Create the main Window
	 *
	 * Oh yeah, thanks to Microsoft limitations for dialog boxes this is SUPER SUCKY:
	 * As per the MSDN [http://msdn.microsoft.com/en-ie/goglobal/bb688119.aspx], "The only way
	 *   to switch between mirrored and nonmirrored dialog resources at run time is to have two
	 *   sets of dialog resources: one mirrored and one nonmirrored."
	 * Unfortunately, this limitation is VERY REAL, so that's what we have to go through, and
	 * furthermore, trying to switch part of the dialogs back to LTR is also a major exercise
	 * in frustration, because it's next to impossible to figure out which combination of
	 * WS_EX_RTLREADING, WS_EX_RIGHT, WS_EX_LAYOUTRTL, WS_EX_LEFTSCROLLBAR and ES_RIGHT will
	 * work... and there's no way to toggle ES_RIGHT at runtime anyway.
	 * So, just like Microsoft advocates, we go through a massive duplication of all our RC
	 * dialogs (our RTL dialogs having their IDD's offset by +100 - see IDD_IS_RTL), just to
	 * add a handful of stupid flags. And of course, we also have to go through a whole other
	 * exercise just so that our RTL and non RTL duplicated dialogs are kept in sync...
	 */
	hDlg = CreateDialogW(hInstance, MAKEINTRESOURCEW(IDD_DIALOG + IDD_IS_RTL), NULL, MainCallback);
	if (hDlg == NULL) {
		MessageBoxU(NULL, "Could not create Window", "DialogBox failure", MB_ICONSTOP|MB_IS_RTL);
		goto out;
	}
	if ((relaunch_rc.left > -65536) && (relaunch_rc.top > -65536))
		SetWindowPos(hDlg, HWND_TOP, relaunch_rc.left, relaunch_rc.top, 0, 0, SWP_NOSIZE);
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
				size_check = !size_check;
				PrintStatus2000(lmprintf(MSG_252), size_check);
				GetUSBDevices(0);
				continue;
			}
			// Alt-F => Toggle detection of USB HDDs
			// By default Rufus does not list USB HDDs. This is a safety feature aimed at avoiding
			// unintentional formatting of backup drives instead of USB keys.
			// When enabled, Rufus will list and allow the formatting of USB HDDs.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'F')) {
				enable_HDDs = !enable_HDDs;
				PrintStatus2000(lmprintf(MSG_253), enable_HDDs);
				GetUSBDevices(0);
				CheckDlgButton(hMainDialog, IDC_ENABLE_FIXED_DISKS, enable_HDDs?BST_CHECKED:BST_UNCHECKED);
				continue;
			}
			// Alt-L => Force Large FAT32 format to be used on < 32 GB drives
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'L')) {
				force_large_fat32 = !force_large_fat32;
				PrintStatus2000(lmprintf(MSG_254), force_large_fat32);
				GetUSBDevices(0);
				continue;
			}
			// Alt-D => Delete the NoDriveTypeAutorun key on exit (useful if the app crashed)
			// This key is used to disable Windows popup messages when an USB drive is plugged in.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'D')) {
				PrintStatus(2000, FALSE, MSG_255);
				existing_key = FALSE;
				continue;
			}
			// Alt J => Toggle Joliet support for ISO9660 images
			// Some ISOs (Ubuntu) have Joliet extensions but expect applications not to use them,
			// due to their reliance on filenames that are > 64 chars (the Joliet max length for
			// a file name). This option allows users to ignore Joliet when using such images.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'J')) {
				enable_joliet = !enable_joliet;
				PrintStatus2000(lmprintf(MSG_257), enable_joliet);
				continue;
			}
			// Alt K => Toggle Rock Ridge support for ISO9660 images
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'K')) {
				enable_rockridge = !enable_rockridge;
				PrintStatus2000(lmprintf(MSG_258), enable_rockridge);
				continue;
			}
			// Alt L => Toggle fake drive detection during bad blocks check
			// By default, Rufus will check for fake USB flash drives that mistakenly present
			// more capacity than they already have by looping over the flash. This check which
			// is enabled by default is performed by writing the block number sequence and reading
			// it back during the bad block check.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'L')) {
				detect_fakes = !detect_fakes;
				PrintStatus2000(lmprintf(MSG_256), detect_fakes);
				continue;
			}
			// Alt N => Enable NTFS compression
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'N')) {
				enable_ntfs_compression = !enable_ntfs_compression;
				PrintStatus2000(lmprintf(MSG_260), enable_ntfs_compression);
				continue;
			}
			// Alt-R => Remove all the registry keys created by Rufus
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'R')) {
				PrintStatus(2000, FALSE, DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME "\\" APPLICATION_NAME)?MSG_248:MSG_249);
				// Also try to delete the upper key (company name) if it's empty (don't care about the result)
				DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME);
				continue;
			}
			// Alt U => Force the update check to be successful
			// This will set the reported current version of Rufus to 0.0.0.0 when performing an update
			// check, so that it always succeeds. This is useful for translators.
			if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'U')) {
				force_update = !force_update;
				PrintStatus2000(lmprintf(MSG_259), force_update);
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	if (relaunch) {
		relaunch = FALSE;
		reinit_localization();
		goto relaunch;
	}

out:
	if ((!external_loc_file) && (loc_file[0] != 0))
		DeleteFileU(loc_file);
	DestroyAllTooltips();
	exit_localization();
	safe_free(iso_path);
	safe_free(locale_name);
	safe_free(update.download_url);
	safe_free(update.release_notes);
	if (argv != NULL) {
		for (i=0; i<argc; i++) safe_free(argv[i]);
		safe_free(argv);
	}
	if (lgp_set)
		SetLGP(TRUE, &existing_key, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoDriveTypeAutorun", 0);
	if (attached_console)
		DetachConsole();
	CloseHandle(mutex);
	uprintf("*** " APPLICATION_NAME " exit ***\n");
#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
