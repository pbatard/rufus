/*
 * Rufus: The Reliable USB Formatting Utility
 * Copyright © 2011-2017 Pete Batard <pete@akeo.ie>
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
#include <winioctl.h>
#include <shlobj.h>
#include <process.h>
#include <dbt.h>
#include <io.h>
#include <getopt.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "drive.h"
#include "settings.h"
#include "bled/bled.h"
#include "../res/grub/grub_version.h"
#include "../res/grub2/grub2_version.h"

// ImageList calls are unavailable on XP
PF_TYPE_DECL(WINAPI, HIMAGELIST, ImageList_Create, (int, int, UINT, int, int));
PF_TYPE_DECL(WINAPI, int, ImageList_AddIcon, (HIMAGELIST, HICON));
PF_TYPE_DECL(WINAPI, int, ImageList_ReplaceIcon, (HIMAGELIST, int, HICON));

PF_TYPE_DECL(WINAPI, BOOL, SHChangeNotifyDeregister, (ULONG));
PF_TYPE_DECL(WINAPI, ULONG, SHChangeNotifyRegister, (HWND, int, LONG, UINT, int, const MY_SHChangeNotifyEntry*));

const char* cmdline_hogger = "rufus.com";
const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS" };
const char* ep_reg = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
const char* vs_reg = "Software\\Microsoft\\VisualStudio";
// Number of steps for each FS for FCC_STRUCTURE_PROGRESS
const int nb_steps[FS_MAX] = { 5, 5, 12, 1, 10 };
static const char* PartitionTypeLabel[2] = { "MBR", "GPT" };
static BOOL existing_key = FALSE;	// For LGP set/restore
static BOOL size_check = TRUE;
static BOOL log_displayed = FALSE;
static BOOL iso_provided = FALSE;
static BOOL user_notified = FALSE;
static BOOL relaunch = FALSE;
static BOOL dont_display_image_name = FALSE;
static BOOL user_changed_label = FALSE;
static BOOL app_changed_label = FALSE;
extern BOOL enable_iso, enable_joliet, enable_rockridge, enable_ntfs_compression;
extern uint8_t* grub2_buf;
extern long grub2_len;
extern const char* old_c32_name[NB_OLD_C32];
extern const char* cert_name[3];
static int selection_default;
static UINT_PTR UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
static RECT relaunch_rc = { -65536, -65536, 0, 0};
static UINT uBootChecked = BST_CHECKED, uQFChecked = BST_CHECKED, uMBRChecked = BST_UNCHECKED;
static HFONT hInfoFont;
static HBRUSH hInfoBrush;
static WNDPROC info_original_proc = NULL;
char ClusterSizeLabel[MAX_CLUSTER_SIZES][64];
char msgbox[1024], msgbox_title[32], *ini_file = NULL;

/*
 * Globals
 */
OPENED_LIBRARIES_VARS;
HINSTANCE hMainInstance;
HWND hMainDialog, hLangToolbar = NULL, hUpdatesDlg = NULL;
MY_BUTTON_IMAGELIST bi_iso = { 0 }, bi_up = { 0 }, bi_down = { 0 };
GetTickCount64_t pfGetTickCount64 = NULL;
char szFolderPath[MAX_PATH], app_dir[MAX_PATH], system_dir[MAX_PATH], temp_dir[MAX_PATH], sysnative_dir[MAX_PATH];
char* image_path = NULL;
float fScale = 1.0f;
int default_fs;
uint32_t dur_mins, dur_secs;
loc_cmd* selected_locale = NULL;
WORD selected_langid = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
DWORD MainThreadId;
HWND hDeviceList, hPartitionScheme, hFileSystem, hClusterSize, hLabel, hBootType, hNBPasses, hLog = NULL;
HWND hLogDlg = NULL, hProgress = NULL, hInfo, hDiskID, hStatusToolbar;
BOOL use_own_c32[NB_OLD_C32] = {FALSE, FALSE}, mbr_selected_by_user = FALSE, togo_mode;
BOOL iso_op_in_progress = FALSE, format_op_in_progress = FALSE, right_to_left_mode = FALSE;
BOOL enable_HDDs = FALSE, force_update = FALSE, enable_ntfs_compression = FALSE, no_confirmation_on_cancel = FALSE, lock_drive = TRUE;
BOOL advanced_mode, allow_dual_uefi_bios, detect_fakes, enable_vmdk, force_large_fat32, usb_debug, use_fake_units, preserve_timestamps;
BOOL zero_drive = FALSE, list_non_usb_removable_drives = FALSE, disable_file_indexing, large_drive = FALSE, prefer_gpt = FALSE;
int dialog_showing = 0, lang_button_id = 0;
uint16_t rufus_version[3], embedded_sl_version[2];
char embedded_sl_version_str[2][12] = { "?.??", "?.??" };
char embedded_sl_version_ext[2][32];
RUFUS_UPDATE update = { {0,0,0}, {0,0}, NULL, NULL};
StrArray DriveID, DriveLabel, BlockingProcess;
extern char* szStatusMessage;

static HANDLE format_thid = NULL, dialog_handle = NULL;
static HWND hBoot = NULL, hSelectISO = NULL, hStart = NULL;
static HICON hIconDisc, hIconDown, hIconUp, hIconLang;
static char szTimer[12] = "00:00:00";
static unsigned int timer;
static int64_t last_iso_blocking_status;
static void ToggleToGo(void);

/*
 * The following is used to allocate slots within the progress bar
 * 0 means unused (no operation or no progress allocated to it)
 * +n means allocate exactly n bars (n percent of the progress bar)
 * -n means allocate a weighted slot of n from all remaining
 *    bars. E.g. if 80 slots remain and the sum of all negative entries
 *    is 10, -4 will allocate 4/10*80 = 32 bars (32%) for OP progress
 */
static int nb_slots[OP_MAX];
static float slot_end[OP_MAX+1];	// shifted +1 so that we can subtract 1 to OP indexes
static float previous_end;

// TODO: Remember to update copyright year in stdlg's AboutCallback() WM_INITDIALOG,
// localization_data.sh and the .rc when the year changes!

// Set the combo selection according to the data
static __inline void SetComboEntry(HWND hDlg, int data) {
	int i;
	for (i = 0; i < ComboBox_GetCount(hDlg); i++) {
		if (ComboBox_GetItemData(hDlg, i) == data)
			IGNORE_RETVAL(ComboBox_SetCurSel(hDlg, i));
	}
}

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
 * 63M  : N/A			(NB unlike MS, we're allowing 512-512 here)
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
 *
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
			if (SelectedDrive.DiskSize*1.0f < i*MB*FAT32_CLUSTER_THRESHOLD) {	// MS
				SelectedDrive.ClusterSize[FS_FAT32].Default = 8*(ULONG)i;
				break;
			}
			SelectedDrive.ClusterSize[FS_FAT32].Allowed <<= 1;
		}
		SelectedDrive.ClusterSize[FS_FAT32].Allowed &= 0x0001FE00;

		// Default cluster sizes in the 256MB to 32 GB range do not follow the rule above
		if ((SelectedDrive.DiskSize >= 256*MB) && (SelectedDrive.DiskSize < 32*GB)) {
			for (i=8; i<=32; i<<=1) {				// 256 MB -> 32 GB
				if (SelectedDrive.DiskSize*1.0f < i*GB*FAT32_CLUSTER_THRESHOLD) {
					SelectedDrive.ClusterSize[FS_FAT32].Default = ((ULONG)i/2)*KB;
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
				SelectedDrive.ClusterSize[FS_NTFS].Default = ((ULONG)i/4)*KB;
				break;
			}
		}

		// exFAT (requires KB955704 installed on XP => don't bother)
		if (nWindowsVersion > WINDOWS_XP) {
			SelectedDrive.ClusterSize[FS_EXFAT].Allowed = 0x03FFFE00;
			if (SelectedDrive.DiskSize < 256*MB)	// < 256 MB
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 4*KB;
			else if (SelectedDrive.DiskSize < 32*GB)	// < 32 GB
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 32*KB;
			else
				SelectedDrive.ClusterSize[FS_EXFAT].Default = 128*KB;
		}

		// UDF (only supported for Vista and later)
		if (nWindowsVersion >= WINDOWS_VISTA) {
			SelectedDrive.ClusterSize[FS_UDF].Allowed = SINGLE_CLUSTERSIZE_DEFAULT;
			SelectedDrive.ClusterSize[FS_UDF].Default = 1;
		}

		// ReFS (only supported for Windows 8.1 and later and for fixed disks)
		if (SelectedDrive.DiskSize >= 512*MB) {
			if ((nWindowsVersion >= WINDOWS_8_1) && (SelectedDrive.MediaType == FixedMedia)) {
				SelectedDrive.ClusterSize[FS_REFS].Allowed = SINGLE_CLUSTERSIZE_DEFAULT;
				SelectedDrive.ClusterSize[FS_REFS].Default = 1;
			}
		}
	}

	// Only add the filesystems we can service
	for (fs=0; fs<FS_MAX; fs++) {
		// Remove all cluster sizes that are below the sector size
		if (SelectedDrive.ClusterSize[fs].Allowed != SINGLE_CLUSTERSIZE_DEFAULT) {
			SelectedDrive.ClusterSize[fs].Allowed &= ~(SelectedDrive.SectorSize - 1);
			if ((SelectedDrive.ClusterSize[fs].Default & SelectedDrive.ClusterSize[fs].Allowed) == 0)
				// We lost our default => Use rightmost bit to select the new one
				SelectedDrive.ClusterSize[fs].Default =
					SelectedDrive.ClusterSize[fs].Allowed & (-(LONG)SelectedDrive.ClusterSize[fs].Allowed);
		}

		if (SelectedDrive.ClusterSize[fs].Allowed != 0) {
			tmp[0] = 0;
			// Tell the user if we're going to use Large FAT32 or regular
			if ((fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32)))
				static_strcat(tmp, "Large ");
			static_strcat(tmp, FileSystemLabel[fs]);
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

// This call sets the first option for the "partition type and target system" field
// according to whether we will be running in UEFI/CSM mode or standard UEFI
// Return value is -1 if the image is pure EFI (non BIOS bootable), 0 otherwise.
static int SetMBRForUEFI(BOOL replace)
{
	static BOOL pure_efi = FALSE;
	BOOL useCSM = FALSE;

	if (ComboBox_GetCurSel(hDeviceList) < 0)
		return 0;

	if (image_path != NULL) {
		if ( !IS_EFI_BOOTABLE(img_report) || (HAS_BOOTMGR(img_report) && (!allow_dual_uefi_bios) &&
			 (Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO)) != BST_CHECKED)) )
			useCSM = TRUE;
	}

	// If we weren't already dealing with pure EFI, we need to remove the first option
	if (replace && !pure_efi)
		ComboBox_DeleteString(hPartitionScheme, 0);

	if ((image_path != NULL) && IS_EFI_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report)) {
		pure_efi = TRUE;
		// Pure EFI -> no need to add the BIOS option
		return -1;
	}

	pure_efi = FALSE;
	IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme, ComboBox_InsertStringU(hPartitionScheme, 0,
		lmprintf(MSG_031, PartitionTypeLabel[PARTITION_STYLE_MBR], useCSM?"UEFI-CSM":"UEFI")), (TT_BIOS<<16)|PARTITION_STYLE_MBR));
	if (replace)
		IGNORE_RETVAL(ComboBox_SetCurSel(hPartitionScheme, max(ComboBox_GetCurSel(hPartitionScheme), 0)));
	return 0;
}

/*
 * Fill the drive properties (size, FS, etc)
 */
static BOOL SetDriveInfo(int ComboIndex)
{
	DWORD i;
	int pt;
	char fs_type[32];

	memset(&SelectedDrive, 0, sizeof(SelectedDrive));
	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, ComboIndex);

	GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_type, sizeof(fs_type), FALSE);

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
		pt = (i==2)?PARTITION_STYLE_GPT:PARTITION_STYLE_MBR;
		if (i==0) {
			SetMBRForUEFI(FALSE);
		} else {
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme, ComboBox_AddStringU(hPartitionScheme,
				lmprintf(MSG_033, PartitionTypeLabel[pt])), (TT_UEFI<<16)|pt));
		}
	}
	if (advanced_mode) {
		IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
			ComboBox_AddStringU(hPartitionScheme, "Super Floppy Disk"), PARTITION_STYLE_SFD));
	}

	// At least one filesystem is go => enable formatting
	EnableWindow(hStart, TRUE);

	return SetClusterSizes((int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)));
}

static void SetFSFromISO(void)
{
	int i, fs, selected_fs = FS_UNKNOWN;
	uint32_t fs_mask = 0;
	int tt = GETTARGETTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	BOOL windows_to_go = (togo_mode) && HAS_WINTOGO(img_report) &&
		(Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO)) == BST_CHECKED);

	if (image_path == NULL)
		return;

	// Create a mask of all the FS's available
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs = (int)ComboBox_GetItemData(hFileSystem, i);
		fs_mask |= 1<<fs;
	}

	// Syslinux and EFI have precedence over bootmgr (unless the user selected BIOS as target type)
	if ((HAS_SYSLINUX(img_report)) || (HAS_REACTOS(img_report)) || HAS_KOLIBRIOS(img_report) ||
		(IS_EFI_BOOTABLE(img_report) && (tt == TT_UEFI) && (!img_report.has_4GB_file) && (!windows_to_go))) {
		if (fs_mask & (1<<FS_FAT32)) {
			selected_fs = FS_FAT32;
		} else if ((fs_mask & (1<<FS_FAT16)) && !HAS_KOLIBRIOS(img_report)) {
			selected_fs = FS_FAT16;
		}
	} else if ((windows_to_go) || HAS_BOOTMGR(img_report) || HAS_WINPE(img_report)) {
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
	int bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	BOOL needs_masquerading = HAS_WINPE(img_report) && (!img_report.uses_minint);

	if ((!mbr_selected_by_user) && ((image_path == NULL) || (bt != BT_ISO) || (fs != FS_NTFS) || HAS_GRUB(img_report) ||
		((togo_mode) && (Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO)) == BST_CHECKED)) )) {
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
		IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
		return;
	}

	uMBRChecked = (needs_masquerading || HAS_BOOTMGR(img_report) || mbr_selected_by_user)?BST_CHECKED:BST_UNCHECKED;
	if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)))
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, needs_masquerading?1:0));
}

static void SetToGo(void)
{
	int bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	if ( ((bt != BT_ISO) && (togo_mode)) || ((bt == BT_ISO) && (HAS_WINTOGO(img_report)) && (!togo_mode)) )
		ToggleToGo();
}

static void EnableAdvancedBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	int tt = GETTARGETTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	BOOL actual_enable_mbr = ((tt==TT_UEFI)||(selection_default>=BT_IMG)||!IsChecked(IDC_BOOT))?FALSE:enable;
	BOOL actual_enable_fix = ((tt==TT_UEFI)||(selection_default==BT_IMG)||!IsChecked(IDC_BOOT))?FALSE:enable;
	static UINT uXPartChecked = BST_UNCHECKED;

	if ((selection_default == BT_ISO) && IS_BIOS_BOOTABLE(img_report) && !HAS_WINPE(img_report) && !HAS_BOOTMGR(img_report)) {
		actual_enable_mbr = FALSE;
		mbr_selected_by_user = FALSE;
	}
	if (remove_checkboxes) {
		// Store/Restore the checkbox states
		if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && !actual_enable_mbr) {
			uMBRChecked = IsChecked(IDC_RUFUS_MBR);
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
			uXPartChecked = IsChecked(IDC_EXTRA_PARTITION);
			CheckDlgButton(hMainDialog, IDC_EXTRA_PARTITION, BST_UNCHECKED);
		} else if (!IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && actual_enable_mbr) {
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
			CheckDlgButton(hMainDialog, IDC_EXTRA_PARTITION, uXPartChecked);
		}
	}

	EnableWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), actual_enable_fix);
	EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), actual_enable_mbr);
	EnableWindow(hDiskID, actual_enable_mbr);
}

static void EnableBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	int fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
	BOOL actual_enable = ((!IS_FAT(fs)) && (fs != FS_NTFS) && (selection_default == BT_IMG))?FALSE:enable;

	EnableWindow(hBoot, actual_enable);
	EnableWindow(hBootType, actual_enable);
	EnableWindow(hSelectISO, actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL), actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO), actual_enable);
	EnableAdvancedBootOptions(actual_enable, remove_checkboxes);
}

static void SetPartitionSchemeTooltip(void)
{
	int tt = GETTARGETTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	int pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	if (tt == TT_BIOS) {
		if (pt != PARTITION_STYLE_SFD)
			CreateTooltip(hPartitionScheme, lmprintf(MSG_150), 15000);
		else
			DestroyTooltip(hPartitionScheme);
	} else {
		if (pt == PARTITION_STYLE_MBR)
			CreateTooltip(hPartitionScheme, lmprintf(MSG_151), 15000);
		else if (pt == PARTITION_STYLE_GPT)
			CreateTooltip(hPartitionScheme, lmprintf(MSG_152), 15000);
		else
			DestroyTooltip(hPartitionScheme);
	}
}

static void SetTargetSystem(void)
{
	int ts = SetMBRForUEFI(TRUE);	// Will be set to -1 for pure UEFI, 0 otherwise
	if ((prefer_gpt && IS_EFI_BOOTABLE(img_report)) || SelectedDrive.PartitionType == PARTITION_STYLE_GPT) {
		ts += 2;	// GPT/UEFI
	} else if (SelectedDrive.has_protective_mbr || SelectedDrive.has_mbr_uefi_marker ||
		(IS_EFI_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report)) ) {
		ts += 1;	// MBR/UEFI
	} else {
		ts += 0;	// MBR/BIOS|UEFI
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hPartitionScheme, ts));
	// Can't call SetPartitionSchemeTooltip() directly, as we may be on a different thread
	SendMessage(hMainDialog, UM_SET_PARTITION_SCHEME_TOOLTIP, 0, 0);
}

static void SetProposedLabel(int ComboIndex)
{
	const char no_label[] = STR_NO_LABEL, empty[] = "";
	int bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));

	app_changed_label = TRUE;
	// If bootable ISO creation is selected, and we have an ISO selected with a valid name, use that
	// Also some distros (eg. Arch) require the USB to have the same label as the ISO
	if (IsChecked(IDC_BOOT) && (bt == BT_ISO) && (image_path != NULL) && (img_report.label[0] != 0)) {
		SetWindowTextU(hLabel, img_report.label);
		// If we force the ISO label, we need to reset the user_changed_label flag
		user_changed_label = FALSE;
		return;
	}

	// If the user manually changed the label, try to preserve it
	if (user_changed_label) {
		app_changed_label = FALSE;
		return;
	}

	// Empty the label if no device is currently selected
	if (ComboIndex < 0) {
		SetWindowTextU(hLabel, "");
		return;
	}

	// Else if no existing label is available, propose one according to the size (eg: "256MB", "8GB")
	if ((_stricmp(no_label, DriveLabel.String[ComboIndex]) == 0) || (_stricmp(no_label, empty) == 0)
		|| (safe_stricmp(lmprintf(MSG_207), DriveLabel.String[ComboIndex]) == 0)) {
		SetWindowTextU(hLabel, SelectedDrive.proposed_label);
	} else {
		SetWindowTextU(hLabel, DriveLabel.String[ComboIndex]);
	}
}

/*
 * Populate the UI properties
 */
static BOOL PopulateProperties(int ComboIndex)
{
	char* device_tooltip;

	IGNORE_RETVAL(ComboBox_ResetContent(hPartitionScheme));
	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	EnableWindow(hStart, FALSE);
	memset(&SelectedDrive, 0, sizeof(SelectedDrive));

	if (ComboIndex < 0)
		goto out;

	if (!SetDriveInfo(ComboIndex)) {	// This also populates FS
		SetProposedLabel(-1);
		return FALSE;
	}
	SetTargetSystem();
	SetFSFromISO();
	EnableBootOptions(TRUE, TRUE);

	// Set a proposed label according to the size (eg: "256MB", "8GB")
	static_sprintf(SelectedDrive.proposed_label,
		SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, use_fake_units));

	// Add a tooltip (with the size of the device in parenthesis)
	device_tooltip = (char*) malloc(safe_strlen(DriveID.String[ComboIndex]) + 16);
	if (device_tooltip != NULL) {
		safe_sprintf(device_tooltip, safe_strlen(DriveID.String[ComboIndex]) + 16, "%s (%s)",
			DriveID.String[ComboIndex], SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, FALSE));
		CreateTooltip(hDeviceList, device_tooltip, -1);
		free(device_tooltip);
	}

out:
	SetProposedLabel(ComboIndex);
	return TRUE;
}

/*
 * Set up progress bar real estate allocation
 */
static void InitProgress(BOOL bOnlyFormat)
{
	int i, fs;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));

	memset(nb_slots, 0, sizeof(nb_slots));
	memset(slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	if (bOnlyFormat) {
		nb_slots[OP_FORMAT] = -1;
	} else {
		nb_slots[OP_ANALYZE_MBR] = 1;
		if (IsChecked(IDC_BADBLOCKS)) {
			nb_slots[OP_BADBLOCKS] = -1;
		}
		if (IsChecked(IDC_BOOT)) {
			// 1 extra slot for PBR writing
			switch (selection_default) {
			case BT_MSDOS:
				nb_slots[OP_DOS] = 3+1;
				break;
			case BT_FREEDOS:
				nb_slots[OP_DOS] = 5+1;
				break;
			case BT_IMG:
				nb_slots[OP_DOS] = 0;
				break;
			case BT_ISO:
				nb_slots[OP_DOS] = -1;
				break;
			default:
				nb_slots[OP_DOS] = 2+1;
				break;
			}
		}
		if (selection_default == BT_IMG) {
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
			nb_slots[OP_FINALIZE] = ((selection_default == BT_ISO) && (fs == FS_NTFS))?3:2;
		}
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
	static uint64_t LastRefresh = 0;

	if ((op < 0) || (op >= OP_MAX)) {
		duprintf("UpdateProgress: invalid op %d\n", op);
		return;
	}
	if (percent > 100.1f) {
//		duprintf("UpdateProgress(%d): invalid percentage %0.2f\n", op, percent);
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

	// Reduce the refresh rate, to avoid weird effects on the sliding part of progress bar
	if (_GetTickCount64() > LastRefresh + (2 * MAX_REFRESH)) {
		LastRefresh = _GetTickCount64();
		SendMessage(hProgress, PBM_SETPOS, (WPARAM)pos, 0);
		SetTaskbarProgressValue(pos, MAX_PROGRESS);
	}
}

/*
 * Toggle controls according to operation
 */
static void EnableControls(BOOL bEnable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_DEVICE), bEnable);
	EnableWindow(hStart, (ComboBox_GetCurSel(hDeviceList)<0)?FALSE:bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ABOUT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_BADBLOCKS), bEnable);
	EnableBootOptions(bEnable, FALSE);
	EnableWindow(hSelectISO, bEnable);
	EnableWindow(hNBPasses, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ADVANCED), bEnable);
	EnableWindow(hLangToolbar, bEnable);
	EnableWindow(hStatusToolbar, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_ENABLE_FIXED_DISKS), bEnable);
	SetDlgItemTextU(hMainDialog, IDCANCEL, lmprintf(bEnable?MSG_006:MSG_007));
	if (selection_default == BT_IMG)
		return;
	EnableWindow(GetDlgItem(hMainDialog, IDC_PARTITION_TYPE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO), bEnable);
}

/* Callback for the log window */
BOOL CALLBACK LogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hdc;
	HFONT hf;
	long lfHeight, style;
	DWORD log_size;
	char *log_buffer = NULL, *filepath;
	EXT_DECL(log_ext, "rufus.log", __VA_GROUP__("*.log"), __VA_GROUP__("Rufus log"));
	switch (message) {
	case WM_INITDIALOG:
		apply_localization(IDD_LOG, hDlg);
		hLog = GetDlgItem(hDlg, IDC_LOG_EDIT);

		// Increase the size of our log textbox to MAX_LOG_SIZE (unsigned word)
		PostMessage(hLog, EM_LIMITTEXT, MAX_LOG_SIZE , 0);
		// Set the font to Unicode so that we can display anything
		hdc = GetDC(NULL);
		lfHeight = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
		if (hdc != NULL)
			ReleaseDC(NULL, hdc);
		hf = CreateFontA(lfHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, "Arial Unicode MS");
		SendDlgItemMessageA(hDlg, IDC_LOG_EDIT, WM_SETFONT, (WPARAM)hf, TRUE);
		// Set 'Close Log' as the selected button
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, IDCANCEL), TRUE);

		// Suppress any inherited RTL flags from our edit control's style. Otherwise
		// the displayed text becomes a mess due to Windows trying to interpret
		// dots, parenthesis, columns and so on in an RTL context...
		// We also take this opportunity to fix the scroll bar and text alignment.
		style = GetWindowLong(hLog, GWL_EXSTYLE);
		style &= ~(WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR);
		SetWindowLong(hLog, GWL_EXSTYLE, style);
		style = GetWindowLong(hLog, GWL_STYLE);
		style &= ~(ES_RIGHT);
		SetWindowLong(hLog, GWL_STYLE, style);

		break;
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
					filepath =  FileDialog(TRUE, app_dir, &log_ext, 0);
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
	static_sprintf(szTimer, "%02d:%02d:%02d", timer/3600, (timer%3600)/60, timer%60);
	SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
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
			MessageBoxExU(hMainDialog, lmprintf(MSG_080), lmprintf(MSG_048),
				MB_OK|MB_ICONINFORMATION|MB_IS_RTL, selected_langid);
		} else {
			last_iso_blocking_status = iso_blocking_status;
		}
	}
}

// Report the features of the selected ISO images
#define PRINT_ISO_PROP(b, ...) do {if (b) uprintf(__VA_ARGS__);} while(0)
static void DisplayISOProps(void)
{
	int i;

	uprintf("ISO label: '%s'", img_report.label);
	uprintf("  Size: %s (Projected)", SizeToHumanReadable(img_report.projected_size, FALSE, FALSE));
	if (img_report.mismatch_size > 0) {
		uprintf("  ERROR: Detected that file on disk has been truncated by %s!",
			SizeToHumanReadable(img_report.mismatch_size, FALSE, FALSE));
		MessageBoxExU(hMainDialog, lmprintf(MSG_298, SizeToHumanReadable(img_report.mismatch_size, FALSE, FALSE)),
			lmprintf(MSG_297), MB_ICONWARNING | MB_IS_RTL, selected_langid);
	} else if (img_report.mismatch_size < 0) {
		// Not an error (ISOHybrid?), but we report it just in case
		uprintf("  Note: File on disk is larger than reported ISO size by %s...",
			SizeToHumanReadable(-img_report.mismatch_size, FALSE, FALSE));
	}

	PRINT_ISO_PROP(img_report.has_4GB_file, "  Has a >4GB file");
	PRINT_ISO_PROP(img_report.has_long_filename, "  Has a >64 chars filename");
	PRINT_ISO_PROP(HAS_SYSLINUX(img_report), "  Uses: Syslinux/Isolinux v%s", img_report.sl_version_str);
	if (HAS_SYSLINUX(img_report) && (SL_MAJOR(img_report.sl_version) < 5)) {
		for (i = 0; i<NB_OLD_C32; i++) {
			PRINT_ISO_PROP(img_report.has_old_c32[i], "    With an old %s", old_c32_name[i]);
		}
	}
	PRINT_ISO_PROP(HAS_KOLIBRIOS(img_report), "  Uses: KolibriOS");
	PRINT_ISO_PROP(HAS_REACTOS(img_report), "  Uses: ReactOS");
	PRINT_ISO_PROP(img_report.has_grub4dos, "  Uses: Grub4DOS");
	PRINT_ISO_PROP(img_report.has_grub2, "  Uses: GRUB2");
	if (img_report.has_efi == 0x80)
		uprintf("  Uses: EFI (through '%s')", img_report.efi_img_path);
	else
		PRINT_ISO_PROP(img_report.has_efi, "  Uses: EFI %s", HAS_WIN7_EFI(img_report) ? "(win7_x64)" : "");
	PRINT_ISO_PROP(HAS_BOOTMGR(img_report), "  Uses: Bootmgr");
	PRINT_ISO_PROP(HAS_WINPE(img_report), "  Uses: WinPE %s", (img_report.uses_minint) ? "(with /minint)" : "");
	if (HAS_INSTALL_WIM(img_report)) {
		uprintf("  Uses: Install.wim (version %d.%d.%d)", (img_report.install_wim_version >> 24) & 0xff,
			(img_report.install_wim_version >> 16) & 0xff, (img_report.install_wim_version >> 8) & 0xff);
		// Microsoft somehow managed to make their ESD WIMs incompatible with their own APIs
		// (yes, EVEN the Windows 10 APIs), so we must filter them out...
		if (img_report.install_wim_version >= MAX_WIM_VERSION)
			uprintf("  Note: This WIM version is NOT compatible with Windows To Go");
	}
	PRINT_ISO_PROP(img_report.has_symlinks, "  Note: This ISO uses symbolic links, which will not be replicated due to file system limitations.");
	PRINT_ISO_PROP(img_report.has_symlinks, "  Because of this, some features from this image may not work...");

	// We don't support ToGo on Windows 7 or earlier, for lack of native ISO mounting capabilities
	if (nWindowsVersion >= WINDOWS_8)
		if ( ((!togo_mode) && (HAS_WINTOGO(img_report))) || ((togo_mode) && (!HAS_WINTOGO(img_report))) )
			ToggleToGo();
}

// The scanning process can be blocking for message processing => use a thread
DWORD WINAPI ISOScanThread(LPVOID param)
{
	int i;

	if (image_path == NULL)
		goto out;
	PrintInfoDebug(0, MSG_202);
	user_notified = FALSE;
	EnableControls(FALSE);
	memset(&img_report, 0, sizeof(img_report));
	img_report.is_iso = (BOOLEAN)ExtractISO(image_path, "", TRUE);
	img_report.is_bootable_img = (BOOLEAN)IsBootableImage(image_path);

	if ((img_report.image_size == 0) || (!img_report.is_iso && !img_report.is_bootable_img)) {
		// Failed to scan image
		SendMessage(hMainDialog, UM_PROGRESS_EXIT, 0, 0);
		PrintInfoDebug(0, MSG_203);
		safe_free(image_path);
		EnableControls(TRUE);
		EnableWindow(hStatusToolbar, FALSE);
		PrintStatus(0, MSG_086);
		SetMBRProps();
		goto out;
	}

	if (img_report.is_bootable_img) {
		uprintf("  Image is a %sbootable %s image",
			(img_report.compression_type != BLED_COMPRESSION_NONE) ? "compressed " : "", img_report.is_vhd ? "VHD" : "disk");
		selection_default = BT_IMG;
	}

	if (img_report.is_iso) {
		DisplayISOProps();
		// If we have an ISOHybrid, but without an ISO method we support, disable ISO support altogether
		if (IS_DD_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report) && !IS_EFI_BOOTABLE(img_report)) {
			uprintf("This ISOHybrid is not compatible with any of the ISO boot methods we support");
			img_report.is_iso = FALSE;
		} else {
			// Will override BT_IMG above for ISOHybrid
			selection_default = BT_ISO;
		}
	}
	// Only enable AFTER we have determined the image type
	EnableControls(TRUE);
	if (!IS_DD_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report) && !IS_EFI_BOOTABLE(img_report)) {
		// No boot method that we support
		PrintInfo(0, MSG_081);
		safe_free(image_path);
		EnableWindow(hStatusToolbar, FALSE);
		MessageBoxExU(hMainDialog, lmprintf(MSG_082), lmprintf(MSG_081), MB_OK|MB_ICONINFORMATION|MB_IS_RTL, selected_langid);
		PrintStatus(0, MSG_086);
		SetMBRProps();
	} else {
		// Enable bootable and set Target System and FS accordingly
		CheckDlgButton(hMainDialog, IDC_BOOT, BST_CHECKED);
		if (img_report.is_iso) {
			SetTargetSystem();
			SetFSFromISO();
			SetMBRProps();
			SetProposedLabel(ComboBox_GetCurSel(hDeviceList));
		} else {
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
		}
		if (!dont_display_image_name) {
			for (i = (int)safe_strlen(image_path); (i > 0) && (image_path[i] != '\\'); i--);
			PrintStatusDebug(0, MSG_205, &image_path[i + 1]);
		}
		// Lose the focus on the select ISO (but place it on Close)
		SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
		// Lose the focus from Close and set it back to Start
		SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)hStart, TRUE);
	}

	// Need to invalidate as we may have changed the UI and may get artifacts if we don't
	// Oh and we need to invoke BOTH RedrawWindow() and InvalidateRect() because UI refresh
	// in the Microsoft worlds SUCKS!!!! (we may lose the disabled "Start" button otherwise)
	RedrawWindow(hMainDialog, NULL, NULL, RDW_ALLCHILDREN | RDW_UPDATENOW);
	InvalidateRect(hMainDialog, NULL, TRUE);

out:
	dont_display_image_name = FALSE;
	PrintInfo(0, MSG_210);
	ExitThread(0);
}

// Move a control along the Y axis according to the advanced mode setting
static __inline void MoveCtrlY(HWND hDlg, int nID, float vertical_shift) {
	ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, nID), 0, (int)vertical_shift, 0, 0, fScale);
}

static void SetPassesTooltip(void)
{
	const unsigned char pattern[] = BADBLOCK_PATTERNS;
	CreateTooltip(hNBPasses, lmprintf(MSG_153 + ComboBox_GetCurSel(hNBPasses),
		pattern[0], pattern[1], pattern[2], pattern[3]), -1);
}

// Toggle "advanced" mode
static void ToggleAdvanced(BOOL enable)
{
	// Compute the shift according to the weird values we measured at different scales:
	// {1.0, 82}, {1.25, 88}, {1.5, 90}, {2.0, 96}, {2.5, 94} (Seriously, WTF is wrong with your scaling Microsoft?!?!)
	// https://www.wolframalpha.com/input/?i=cubic+fit+{1%2C82}%2C{1.25%2C88}%2C{1.5%2C90}%2C{2%2C96}%2C{2.5%2C94}
	float dialog_shift = -3.22807f*fScale*fScale*fScale + 6.69173f*fScale*fScale + 15.8822f*fScale + 62.9737f;
	RECT rect;
	POINT point;
	BOOL needs_resel = FALSE;
	int i, toggle;

	if (!enable)
		dialog_shift = -dialog_shift;

	// Increase or decrease the Window size
	GetWindowRect(hMainDialog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hMainDialog, rect.left, rect.top, point.x,
		point.y + (int)(fScale*dialog_shift), TRUE);

	// Move the controls up or down
	MoveCtrlY(hMainDialog, IDC_STATUS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_STATUS_TOOLBAR, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_START, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_INFO, dialog_shift);
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
		point.y + (int)(fScale*dialog_shift), TRUE);
	MoveCtrlY(hLogDlg, IDC_LOG_CLEAR, dialog_shift);
	MoveCtrlY(hLogDlg, IDC_LOG_SAVE, dialog_shift);
	MoveCtrlY(hLogDlg, IDCANCEL, dialog_shift);
	GetWindowRect(hLog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top) + (int)(fScale*dialog_shift);
	SetWindowPos(hLog, NULL, 0, 0, point.x, point.y, SWP_NOZORDER);
	// Don't forget to scroll the edit to the bottom after resize
	Edit_Scroll(hLog, 0, Edit_GetLineCount(hLog));

	// Hide or show the various advanced options
	toggle = enable?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMainDialog, IDC_ENABLE_FIXED_DISKS), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_EXTRA_PARTITION), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_DISK_ID), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDS_ADVANCED_OPTIONS_GRP), toggle);

	if (enable) {
		IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
			ComboBox_AddStringU(hPartitionScheme, "Super Floppy Disk"), PARTITION_STYLE_SFD));
	} else {
		for (i = 0; i < ComboBox_GetCount(hPartitionScheme); i++) {
			if (ComboBox_GetItemData(hPartitionScheme, i) == PARTITION_STYLE_SFD) {
				if (ComboBox_GetCurSel(hPartitionScheme) == i)
					needs_resel = TRUE;
				ComboBox_DeleteString(hPartitionScheme, i);
			}
		}
		if (needs_resel)
			SetTargetSystem();
	}

	// Toggle the up/down icon
	SendMessage(GetDlgItem(hMainDialog, IDC_ADVANCED), BCM_SETIMAGELIST, 0, (LPARAM)(enable?&bi_up:&bi_down));

	// Never hurts to force Windows' hand
	InvalidateRect(hMainDialog, NULL, TRUE);
}

// Toggle DD Image mode
static void ToggleImage(BOOL enable)
{
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_PARTITION_TYPE), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_FILESYSTEM), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_CLUSTERSIZE), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICKFORMAT), enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SET_ICON), enable);
}

// Toggle the Windows To Go radio choice
static void ToggleToGo(void)
{
	// {1.0, 38}, {1.25, 40}, {1.5, 40}, {2.0, 44}, {2.5, 44}
	float dialog_shift = (fScale >= 1.9f)?44.0f:((fScale >= 1.2f)?40.0f:38.0f);
	RECT rect;
	POINT point;
	int toggle;

	// Windows To Go mode is only available for Windows 8 or later due to the lack
	// of an ISO mounting API on previous versions.
	// But we still need to be able to hide the Windows To Go option on startup.
	if ((nWindowsVersion < WINDOWS_8) && (!togo_mode))
		return;

	togo_mode = !togo_mode;
	if (!togo_mode)
		dialog_shift = -dialog_shift;

	// Increase or decrease the Window size
	GetWindowRect(hMainDialog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hMainDialog, rect.left, rect.top, point.x,
		point.y + (int)(fScale*dialog_shift), TRUE);

	// Move the controls up or down
	MoveCtrlY(hMainDialog, IDC_STATUS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_STATUS_TOOLBAR, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_START, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_INFO, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_PROGRESS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_ABOUT, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_LOG, dialog_shift);
	MoveCtrlY(hMainDialog, IDCANCEL, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_SET_ICON, dialog_shift);
	MoveCtrlY(hMainDialog, IDS_ADVANCED_OPTIONS_GRP, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_ENABLE_FIXED_DISKS, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_EXTRA_PARTITION, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_RUFUS_MBR, dialog_shift);
	MoveCtrlY(hMainDialog, IDC_DISK_ID, dialog_shift);
	ResizeMoveCtrl(hMainDialog, GetDlgItem(hMainDialog, IDS_FORMAT_OPTIONS_GRP), 0, 0, 0, (int)dialog_shift, fScale);

#ifdef RUFUS_TEST
	MoveCtrlY(hMainDialog, IDC_TEST, dialog_shift);
#endif

	// And do the same for the log dialog while we're at it
	GetWindowRect(hLogDlg, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top);
	MoveWindow(hLogDlg, rect.left, rect.top, point.x,
		point.y + (int)(fScale*dialog_shift), TRUE);
	MoveCtrlY(hLogDlg, IDC_LOG_CLEAR, dialog_shift);
	MoveCtrlY(hLogDlg, IDC_LOG_SAVE, dialog_shift);
	MoveCtrlY(hLogDlg, IDCANCEL, dialog_shift);
	GetWindowRect(hLog, &rect);
	point.x = (rect.right - rect.left);
	point.y = (rect.bottom - rect.top) + (int)(fScale*dialog_shift);
	SetWindowPos(hLog, NULL, 0, 0, point.x, point.y, SWP_NOZORDER);
	// Don't forget to scroll the edit to the bottom after resize
	Edit_Scroll(hLog, 0, Edit_GetLineCount(hLog));

	// Hide or show the various advanced options
	toggle = togo_mode?SW_SHOW:SW_HIDE;
	ShowWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL), toggle);
	ShowWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO), toggle);

	// Reset the radio button choice
	Button_SetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL), BST_CHECKED);
	Button_SetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO), BST_UNCHECKED);

	// If you don't force a redraw here, all kind of bad UI artifacts happen...
	InvalidateRect(hMainDialog, NULL, TRUE);
}

static BOOL BootCheck(void)
{
	int i, fs, tt, bt, pt, r;
	FILE *fd;
	DWORD len;
	BOOL in_files_dir = FALSE;
	const char* grub = "grub";
	const char* core_img = "core.img";
	const char* ldlinux = "ldlinux";
	const char* syslinux = "syslinux";
	const char* ldlinux_ext[3] = { "sys", "bss", "c32" };
	char tmp[MAX_PATH], tmp2[MAX_PATH];

	syslinux_ldlinux_len[0] = 0; syslinux_ldlinux_len[1] = 0;
	safe_free(grub2_buf);
	bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
	if ((bt == BT_ISO) || (bt == BT_IMG)) {
		if (image_path == NULL) {
			// Please click on the disc button to select a bootable ISO
			MessageBoxExU(hMainDialog, lmprintf(MSG_087), lmprintf(MSG_086), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
		if ((size_check) && (img_report.projected_size > (uint64_t)SelectedDrive.DiskSize)) {
			// This ISO image is too big for the selected target
			MessageBoxExU(hMainDialog, lmprintf(MSG_089), lmprintf(MSG_088), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
		if (bt == BT_IMG) {
			if (!IS_DD_BOOTABLE(img_report))
			// The selected image doesn't match the boot option selected.
				MessageBoxExU(hMainDialog, lmprintf(MSG_188), lmprintf(MSG_187), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return IS_DD_BOOTABLE(img_report);
		}
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		tt = GETTARGETTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
		if ((togo_mode) && (Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO)) == BST_CHECKED)) {
			if (fs != FS_NTFS) {
				// Windows To Go only works for NTFS
				MessageBoxExU(hMainDialog, lmprintf(MSG_097, "Windows To Go"), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
				return FALSE;
			}
			if (SelectedDrive.MediaType != FixedMedia) {
				if ((tt == TT_UEFI) && (pt == PARTITION_STYLE_GPT) && (nWindowsBuildNumber < 15000)) {
					// Up to Windows 10 Creators Update, we were screwed, since we need access to 2 partitions at the same time.
					// Thankfully, the newer Windows allow mounting multiple partitions on the same REMOVABLE drive.
					MessageBoxExU(hMainDialog, lmprintf(MSG_198), lmprintf(MSG_190), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
					return FALSE;
				}
			}
			// If multiple versions are available, asks the user to select one before we commit to format the drive
			switch(SetWinToGoIndex()) {
			case -1:
				MessageBoxExU(hMainDialog, lmprintf(MSG_073), lmprintf(MSG_291), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
				// fall through
			case -2:
				return FALSE;
			default:
				break;
			}
		} else if (tt == TT_UEFI) {
			if (!IS_EFI_BOOTABLE(img_report)) {
				// Unsupported ISO
				MessageBoxExU(hMainDialog, lmprintf(MSG_091), lmprintf(MSG_090), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
				return FALSE;
			}
			if (HAS_WIN7_EFI(img_report) && (!WimExtractCheck())) {
				// Your platform cannot extract files from WIM archives => download 7-zip?
				if (MessageBoxExU(hMainDialog, lmprintf(MSG_102), lmprintf(MSG_101), MB_YESNO|MB_ICONERROR|MB_IS_RTL, selected_langid) == IDYES)
					ShellExecuteA(hMainDialog, "open", SEVENZIP_URL, NULL, NULL, SW_SHOWNORMAL);
				return FALSE;
			}
		} else if ( ((fs == FS_NTFS) && !HAS_WINDOWS(img_report) && !HAS_GRUB(img_report) && 
					 (!HAS_SYSLINUX(img_report) || (SL_MAJOR(img_report.sl_version) <= 5)))
				 || ((IS_FAT(fs)) && (!HAS_SYSLINUX(img_report)) && (!allow_dual_uefi_bios) && !IS_EFI_BOOTABLE(img_report) &&
					 (!HAS_REACTOS(img_report)) && !HAS_KOLIBRIOS(img_report) && (!HAS_GRUB(img_report)))
				 || ((IS_FAT(fs)) && (HAS_WINDOWS(img_report) || HAS_INSTALL_WIM(img_report)) && (!allow_dual_uefi_bios)) ) {
			// Incompatible FS and ISO
			MessageBoxExU(hMainDialog, lmprintf(MSG_096), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		} else if ((fs == FS_FAT16) && HAS_KOLIBRIOS(img_report)) {
			// KolibriOS doesn't support FAT16
			MessageBoxExU(hMainDialog, lmprintf(MSG_189), lmprintf(MSG_099), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
		if ((IS_FAT(fs)) && (img_report.has_4GB_file)) {
			// This ISO image contains a file larger than 4GB file (FAT32)
			MessageBoxExU(hMainDialog, lmprintf(MSG_100), lmprintf(MSG_099), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}

		// If the selected target doesn't include include BIOS, skip file downloads for GRUB/Syslinux
		if (tt != TT_BIOS)
			goto uefi_target;

		if ((img_report.has_grub2) && (img_report.grub2_version[0] != 0) &&
			(strcmp(img_report.grub2_version, GRUB2_PACKAGE_VERSION) != 0)) {
			// We may have to download a different Grub2 version if we can find one
			IGNORE_RETVAL(_chdirU(app_dir));
			IGNORE_RETVAL(_mkdir(FILES_DIR));
			IGNORE_RETVAL(_chdir(FILES_DIR));
			static_sprintf(tmp, "%s-%s/%s", grub, img_report.grub2_version, core_img);
			fd = fopen(tmp, "rb");
			if (fd != NULL) {
				// If a file already exists in the current directory, use that one
				uprintf("Will reuse '%s' from './" FILES_DIR "/%s-%s/' for Grub 2.x installation",
					core_img, grub, img_report.grub2_version);
				fseek(fd, 0, SEEK_END);
				grub2_len = ftell(fd);
				fseek(fd, 0, SEEK_SET);
				if (grub2_len > 0)
					grub2_buf = malloc(grub2_len);

				// grub2_buf was set to NULL at the beginning of this call
				if ((grub2_buf == NULL) || (fread(grub2_buf, 1, (size_t)grub2_len, fd) != (size_t)grub2_len)) {
					uprintf("Failed to read existing '%s' data - will use embedded version", core_img);
					safe_free(grub2_buf);
				}
				fclose(fd);
			} else {
				r = MessageBoxExU(hMainDialog, lmprintf(MSG_116, img_report.grub2_version, GRUB2_PACKAGE_VERSION),
					lmprintf(MSG_115), MB_YESNOCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid);
				if (r == IDCANCEL)
					return FALSE;
				else if (r == IDYES) {
					static_sprintf(tmp, "%s-%s", grub, img_report.grub2_version);
					IGNORE_RETVAL(_mkdir(tmp));
					IGNORE_RETVAL(_chdir(tmp));
					static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, grub, img_report.grub2_version, core_img);
					PromptOnError = FALSE;
					grub2_len = (long)DownloadFile(tmp, core_img, hMainDialog);
					PromptOnError = TRUE;
					if ((grub2_len == 0) && (DownloadStatus == 404)) {
						// Couldn't locate the file on the server => try to download without the version extra
						uprintf("Extended version was not found, trying main version...");
						static_strcpy(tmp2, img_report.grub2_version);
						// Isolate the #.### part
						for (i = 0; ((tmp2[i] >= '0') && (tmp2[i] <= '9')) || (tmp2[i] == '.'); i++);
						tmp2[i] = 0;
						static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, grub, tmp2, core_img);
						PromptOnError = FALSE;
						grub2_len = (long)DownloadFile(tmp, core_img, hMainDialog);
						PromptOnError = TRUE;
						static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, grub, img_report.grub2_version, core_img);
					}
					if (grub2_len <= 0) {
						PrintInfo(0, MSG_195, "Grub2");
						uprintf("%s was not found - will use embedded version", tmp);
					} else {
						PrintInfo(0, MSG_193, tmp);
						fd = fopen(core_img, "rb");
						grub2_buf = malloc(grub2_len);
						if ((fd == NULL) || (grub2_buf == NULL) || (fread(grub2_buf, 1, (size_t)grub2_len, fd) != (size_t)grub2_len)) {
							uprintf("Failed to read '%s' data - will use embedded version", core_img);
							safe_free(grub2_buf);
						}
						if (fd != NULL)
							fclose(fd);
					}
				}
			}
		}

		if (HAS_SYSLINUX(img_report)) {
			if (SL_MAJOR(img_report.sl_version) < 5) {
				IGNORE_RETVAL(_chdirU(app_dir));
				for (i=0; i<NB_OLD_C32; i++) {
					if (img_report.has_old_c32[i]) {
						if (!in_files_dir) {
							IGNORE_RETVAL(_mkdir(FILES_DIR));
							IGNORE_RETVAL(_chdir(FILES_DIR));
							in_files_dir = TRUE;
						}
						static_sprintf(tmp, "%s-%s/%s", syslinux, embedded_sl_version_str[0], old_c32_name[i]);
						fd = fopen(tmp, "rb");
						if (fd != NULL) {
							// If a file already exists in the current directory, use that one
							uprintf("Will replace obsolete '%s' from ISO with the one found in './" FILES_DIR "/%s'", old_c32_name[i], tmp);
							fclose(fd);
							use_own_c32[i] = TRUE;
						} else {
							PrintInfo(0, MSG_204, old_c32_name[i]);
							if (MessageBoxExU(hMainDialog, lmprintf(MSG_084, old_c32_name[i], old_c32_name[i]),
									lmprintf(MSG_083, old_c32_name[i]), MB_YESNO|MB_ICONWARNING|MB_IS_RTL, selected_langid) == IDYES) {
								static_sprintf(tmp, "%s-%s", syslinux, embedded_sl_version_str[0]);
								IGNORE_RETVAL(_mkdir(tmp));
								static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, syslinux, embedded_sl_version_str[0], old_c32_name[i]);
								len = DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog);
								if (len == 0) {
									uprintf("Could not download file - cancelling");
									return FALSE;
								}
								use_own_c32[i] = TRUE;
							}
						}
					}
				}
			} else if ((img_report.sl_version != embedded_sl_version[1]) ||
				(safe_strcmp(img_report.sl_version_ext, embedded_sl_version_ext[1]) != 0)) {
				// Unlike what was the case for v4 and earlier, Syslinux v5+ versions are INCOMPATIBLE with one another!
				IGNORE_RETVAL(_chdirU(app_dir));
				IGNORE_RETVAL(_mkdir(FILES_DIR));
				IGNORE_RETVAL(_chdir(FILES_DIR));
				for (i=0; i<2; i++) {
					// Check if we already have the relevant ldlinux_v#.##.sys & ldlinux_v#.##.bss files
					static_sprintf(tmp, "%s-%s%s/%s.%s", syslinux, img_report.sl_version_str,
						img_report.sl_version_ext, ldlinux, ldlinux_ext[i]);
					fd = fopen(tmp, "rb");
					if (fd != NULL) {
						fseek(fd, 0, SEEK_END);
						syslinux_ldlinux_len[i] = (DWORD)ftell(fd);
						fclose(fd);
					}
				}
				if ((syslinux_ldlinux_len[0] != 0) && (syslinux_ldlinux_len[1] != 0)) {
					uprintf("Will reuse '%s.%s' and '%s.%s' from './" FILES_DIR "/%s/%s-%s%s/' for Syslinux installation",
						ldlinux, ldlinux_ext[0], ldlinux, ldlinux_ext[1], FILES_DIR, syslinux,
						img_report.sl_version_str, img_report.sl_version_ext);
				} else {
					r = MessageBoxExU(hMainDialog, lmprintf(MSG_114, img_report.sl_version_str, img_report.sl_version_ext,
						embedded_sl_version_str[1], embedded_sl_version_ext[1]),
						lmprintf(MSG_115), MB_YESNO|MB_ICONWARNING|MB_IS_RTL, selected_langid);
					if (r != IDYES)
						return FALSE;
					for (i=0; i<2; i++) {
						static_sprintf(tmp, "%s-%s", syslinux, img_report.sl_version_str);
						IGNORE_RETVAL(_mkdir(tmp));
						if (*img_report.sl_version_ext != 0) {
							IGNORE_RETVAL(_chdir(tmp));
							IGNORE_RETVAL(_mkdir(&img_report.sl_version_ext[1]));
							IGNORE_RETVAL(_chdir(".."));
						}
						static_sprintf(tmp, "%s/%s-%s%s/%s.%s", FILES_URL, syslinux, img_report.sl_version_str,
							img_report.sl_version_ext, ldlinux, ldlinux_ext[i]);
						PromptOnError = (*img_report.sl_version_ext == 0);
						syslinux_ldlinux_len[i] = DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog);
						PromptOnError = TRUE;
						if ((syslinux_ldlinux_len[i] == 0) && (DownloadStatus == 404) && (*img_report.sl_version_ext != 0)) {
							// Couldn't locate the file on the server => try to download without the version extra
							uprintf("Extended version was not found, trying main version...");
							static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, img_report.sl_version_str,
								ldlinux, ldlinux_ext[i]);
							syslinux_ldlinux_len[i] = DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog);
							if (syslinux_ldlinux_len[i] != 0) {
								// Duplicate the file so that the user won't be prompted to download again
								static_sprintf(tmp, "%s-%s\\%s.%s", syslinux, img_report.sl_version_str, ldlinux, ldlinux_ext[i]);
								static_sprintf(tmp2, "%s-%s\\%s\\%s.%s", syslinux, img_report.sl_version_str,
									&img_report.sl_version_ext[1], ldlinux, ldlinux_ext[i]);
								CopyFileA(tmp, tmp2, FALSE);
							}
						}
						if (syslinux_ldlinux_len[i] == 0) {
							// If the version matches our embedded one, try to use that as a last ditch effort
							if (img_report.sl_version == embedded_sl_version[1]) {
								uprintf("Could not download the file - will try to use embedded %s version instead", img_report.sl_version_str);
							} else {
								uprintf("Could not download the file - cancelling");
								return FALSE;
							}
						}
					}
				}
			}
		}
	} else if (bt == BT_SYSLINUX_V6) {
		IGNORE_RETVAL(_chdirU(app_dir));
		IGNORE_RETVAL(_mkdir(FILES_DIR));
		IGNORE_RETVAL(_chdir(FILES_DIR));
		static_sprintf(tmp, "%s-%s/%s.%s", syslinux, embedded_sl_version_str[1], ldlinux, ldlinux_ext[2]);
		fd = fopenU(tmp, "rb");
		if (fd != NULL) {
			uprintf("Will reuse './%s/%s' for Syslinux installation", FILES_DIR, tmp);
			fclose(fd);
		} else {
			static_sprintf(tmp, "%s.%s", ldlinux, ldlinux_ext[2]);
			PrintInfo(0, MSG_206, tmp);
			// MSG_104: "Syslinux v5.0 or later requires a '%s' file to be installed"
			r = MessageBoxExU(hMainDialog, lmprintf(MSG_104, "Syslinux v5.0", tmp, "Syslinux v5+", tmp),
				lmprintf(MSG_103, tmp), MB_YESNOCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid);
			if (r == IDCANCEL)
				return FALSE;
			if (r == IDYES) {
				static_sprintf(tmp, "%s-%s", syslinux, embedded_sl_version_str[1]);
				IGNORE_RETVAL(_mkdir(tmp));
				static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, embedded_sl_version_str[1], ldlinux, ldlinux_ext[2]);
				if (DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog) == 0)
					return FALSE;
			}
		}
	} else if (bt == BT_MSDOS) {
		if ((size_check) && (ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)) >= 65536)) {
			// MS-DOS cannot boot from a drive using a 64 kilobytes Cluster size
			MessageBoxExU(hMainDialog, lmprintf(MSG_110), lmprintf(MSG_111), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
	} else if (bt == BT_GRUB4DOS) {
		IGNORE_RETVAL(_chdirU(app_dir));
		IGNORE_RETVAL(_mkdir(FILES_DIR));
		IGNORE_RETVAL(_chdir(FILES_DIR));
		static_sprintf(tmp, "grub4dos-%s/grldr", GRUB4DOS_VERSION);
		fd = fopenU(tmp, "rb");
		if (fd != NULL) {
			uprintf("Will reuse './%s/%s' for Grub4DOS installation", FILES_DIR, tmp);
			fclose(fd);
		} else {
			static_sprintf(tmp, "grldr");
			PrintInfo(0, MSG_206, tmp);
			r = MessageBoxExU(hMainDialog, lmprintf(MSG_104, "Grub4DOS 0.4", tmp, "Grub4DOS", tmp),
				lmprintf(MSG_103, tmp), MB_YESNOCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid);
			if (r == IDCANCEL)
				return FALSE;
			if (r == IDYES) {
				static_sprintf(tmp, "grub4dos-%s", GRUB4DOS_VERSION);
				IGNORE_RETVAL(_mkdir(tmp));
				static_sprintf(tmp, "%s/grub4dos-%s/grldr", FILES_URL, GRUB4DOS_VERSION);
				if (DownloadFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog) == 0)
					return FALSE;
			}
		}
	}

uefi_target:
	if (bt == BT_UEFI_NTFS) {
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		if (fs != FS_NTFS) {
			MessageBoxExU(hMainDialog, lmprintf(MSG_097, "UEFI:NTFS"), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
	}
	return TRUE;
}

static __inline const char* IsAlphaOrBeta(void)
{
#if defined(ALPHA)
	return " (Alpha) ";
#elif defined(BETA)
	return " (Beta) ";
#elif defined(TEST)
#	define TEST_STR(x) " (Test" STRINGIFY(x) ") "
	return TEST_STR(TEST);
#else
	return " ";
#endif
}

static INT_PTR CALLBACK InfoCallback(HWND hCtrl, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hdc;
	RECT rect;
	PAINTSTRUCT ps;
	wchar_t winfo[128];

	// Prevent the cursor (caret) from appearing within the edit control
	HideCaret(hCtrl);

	switch (message) {

	// Prevent text selection (wich Windows seems keen on doing on its own)
	case EM_SETSEL:
		return (INT_PTR)TRUE;

	// Prevent select (which screws up our display as it redraws the font using different settings)
	case WM_LBUTTONDOWN:
		return (INT_PTR)FALSE;

	// Prevent the text selection pointer from appearing on hover
	case WM_SETCURSOR:
		SetCursor(LoadCursor(NULL, IDC_ARROW));
		return (INT_PTR)TRUE;

	// The things one needs to do to vertically center text in an edit control...
	case WM_PAINT:
		GetWindowTextW(hInfo, winfo, ARRAYSIZE(winfo));
		hdc = BeginPaint(hCtrl , &ps);
		SelectObject(hdc, hInfoFont);
		SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
		SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
		SetTextAlign(hdc , TA_CENTER | TA_BASELINE);
		GetClientRect(hCtrl , &rect);
		// If you don't fill the client area, you get leftover text artifacts
		FillRect(hdc, &rect, hInfoBrush);
		ExtTextOutW(hdc, rect.right/2, rect.bottom/2 + (int)(5.0f * fScale),
			ETO_CLIPPED | ETO_NUMERICSLOCAL | (right_to_left_mode?ETO_RTLREADING:0),
			&rect, winfo, (int)wcslen(winfo), NULL);
		EndPaint(hCtrl, &ps);
		return (INT_PTR)TRUE;
	}

	return CallWindowProc(info_original_proc, hCtrl, message, wParam, lParam);
}

static void InitDialog(HWND hDlg)
{
	HINSTANCE hShell32DllInst, hUserLanguagesCplDllInst, hINetCplDllInst;
	HIMAGELIST hLangToolbarImageList;
	TBBUTTON tbLangToolbarButtons[1];
	RECT rcDeviceList, rcToolbarButton, rcFormatGroup, rcAdvancedOptions, rcBootType, rcSelectImage;
	DWORD len;
	SIZE sz;
	HWND hCtrl;
	HDC hDC;
	int i, i16, s16, lfHeight;
	char tmp[128], *token, *buf, *ext;
	wchar_t wtmp[128] = {0};
	static char* resource[2] = { MAKEINTRESOURCEA(IDR_SL_LDLINUX_V4_SYS), MAKEINTRESOURCEA(IDR_SL_LDLINUX_V6_SYS) };

#ifdef RUFUS_TEST
	ShowWindow(GetDlgItem(hDlg, IDC_TEST), SW_SHOW);
#endif

	PF_INIT(ImageList_Create, Comctl32);
	PF_INIT(ImageList_AddIcon, Comctl32);
	PF_INIT(ImageList_ReplaceIcon, Comctl32);

	// Quite a burden to carry around as parameters
	hMainDialog = hDlg;
	MainThreadId = GetCurrentThreadId();
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
	hPartitionScheme = GetDlgItem(hDlg, IDC_PARTITION_TYPE);
	hFileSystem = GetDlgItem(hDlg, IDC_FILESYSTEM);
	hClusterSize = GetDlgItem(hDlg, IDC_CLUSTERSIZE);
	hLabel = GetDlgItem(hDlg, IDC_LABEL);
	hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
	hInfo = GetDlgItem(hDlg, IDC_INFO);
	hBoot = GetDlgItem(hDlg, IDC_BOOT);
	hBootType = GetDlgItem(hDlg, IDC_BOOTTYPE);
	hSelectISO = GetDlgItem(hDlg, IDC_SELECT_ISO);
	hNBPasses = GetDlgItem(hDlg, IDC_NBPASSES);
	hDiskID = GetDlgItem(hDlg, IDC_DISK_ID);
	hStart = GetDlgItem(hDlg, IDC_START);

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hDC = GetDC(hDlg);
	lfHeight = -MulDiv(9, GetDeviceCaps(hDC, LOGPIXELSY), 72);
	if (hDC != NULL)
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

	// Create the font and brush for the Info edit box
	hInfoFont = CreateFontA(lfHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		0, 0, PROOF_QUALITY, 0, (nWindowsVersion >= WINDOWS_VISTA)?"Segoe UI":"Arial Unicode MS");
	SendDlgItemMessageA(hDlg, IDC_INFO, WM_SETFONT, (WPARAM)hInfoFont, TRUE);
	hInfoBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	HideCaret(hInfo);

	// Create the title bar icon
	SetTitleBarIcon(hDlg);
	GetWindowTextA(hDlg, tmp, sizeof(tmp));
	// Count of Microsoft for making it more attractive to read a
	// version using strtok() than using GetFileVersionInfo()
	token = strtok(tmp, " ");
	for (i=0; (i<3) && ((token = strtok(NULL, ".")) != NULL); i++)
		rufus_version[i] = (uint16_t)atoi(token);

	// Redefine the title to be able to add "Alpha" or "Beta" and get the version in the right order for RTL
	if (!right_to_left_mode) {
		static_sprintf(tmp, APPLICATION_NAME " %d.%d.%d%s%s", rufus_version[0], rufus_version[1], rufus_version[2],
			IsAlphaOrBeta(), (ini_file != NULL)?"(Portable)":"");
	} else {
		static_sprintf(tmp, "%s%s%d.%d.%d " APPLICATION_NAME, (ini_file != NULL)?"(Portable)":"", IsAlphaOrBeta(),
			rufus_version[0], rufus_version[1], rufus_version[2]);
	}
	SetWindowTextU(hDlg, tmp);
	// Now that we have a title, we can find the handle of our Dialog
	dialog_handle = FindWindowA(NULL, tmp);
	uprintf(APPLICATION_NAME " version: %d.%d.%d%s%s", rufus_version[0], rufus_version[1], rufus_version[2],
		IsAlphaOrBeta(), (ini_file != NULL)?"(Portable)":"");
	for (i=0; i<ARRAYSIZE(resource); i++) {
		buf = (char*)GetResource(hMainInstance, resource[i], _RT_RCDATA, "ldlinux_sys", &len, TRUE);
		if (buf == NULL) {
			uprintf("Warning: could not read embedded Syslinux v%d version", i+4);
		} else {
			embedded_sl_version[i] = GetSyslinuxVersion(buf, len, &ext);
			static_sprintf(embedded_sl_version_str[i], "%d.%02d", SL_MAJOR(embedded_sl_version[i]), SL_MINOR(embedded_sl_version[i]));
			static_strcpy(embedded_sl_version_ext[i], ext);
			free(buf);
		}
	}
	uprintf("Windows version: %s", WindowsVersionStr);
	uprintf("Syslinux versions: %s%s, %s%s", embedded_sl_version_str[0], embedded_sl_version_ext[0],
		embedded_sl_version_str[1], embedded_sl_version_ext[1]);
	uprintf("Grub versions: %s, %s", GRUB4DOS_VERSION, GRUB2_PACKAGE_VERSION);
	uprintf("System locale ID: 0x%04X", GetUserDefaultUILanguage());
	ubflush();
	if (selected_locale->ctrl_id & LOC_NEEDS_UPDATE) {
		uprintf("NOTE: The %s translation requires an update, but the current translator hasn't submitted "
			"one. Because of this, some messages will only be displayed in English.", selected_locale->txt[1]);
		uprintf("If you think you can help update this translation, please e-mail the author of this application");
	}

	SetClusterSizeLabels();

	// Prefer FreeDOS to MS-DOS
	selection_default = BT_FREEDOS;
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
	IGNORE_RETVAL(ComboBox_SetCurSel(hNBPasses, 0));
	SetPassesTooltip();
	// Fill up the boot type dropdown
	if (nWindowsVersion < WINDOWS_10)	// The diskcopy.dll with the MS-DOS floppy image was removed in Windows 10
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "MS-DOS"), BT_MSDOS));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "FreeDOS"), BT_FREEDOS));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_036)), BT_ISO));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_095)), BT_IMG));
	SetComboEntry(hBootType, selection_default);
	// Fill up the MBR masqueraded disk IDs ("8 disks should be enough for anybody")
	IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_030, LEFT_TO_RIGHT_EMBEDDING "0x80" POP_DIRECTIONAL_FORMATTING)), 0x80));
	for (i=1; i<=7; i++) {
		IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_109, 0x80+i, i+1)), 0x80+i));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));

	// Create the string array
	StrArrayCreate(&DriveID, MAX_DRIVES);
	StrArrayCreate(&DriveLabel, MAX_DRIVES);
	StrArrayCreate(&BlockingProcess, 16);
	// Set various checkboxes
	CheckDlgButton(hDlg, IDC_QUICKFORMAT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_BOOT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_SET_ICON, BST_CHECKED);

	// Load system icons (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hShell32DllInst = GetLibraryHandle("Shell32");
	hIconDisc = (HICON)LoadImage(hShell32DllInst, MAKEINTRESOURCE(12), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);

	if (nWindowsVersion >= WINDOWS_8) {
		// Use the icon from the Windows 8+ 'Language' Control Panel
		hUserLanguagesCplDllInst = GetLibraryHandle("UserLanguagesCpl");
		hIconLang = (HICON)LoadImage(hUserLanguagesCplDllInst, MAKEINTRESOURCE(1), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	} else {
		// Otherwise use the globe icon, from the Internet Options Control Panel
		hINetCplDllInst = GetLibraryHandle("inetcpl.cpl");
		hIconLang = (HICON)LoadImage(hINetCplDllInst, MAKEINTRESOURCE(1313), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	}

	if (nWindowsVersion >= WINDOWS_VISTA) {
		hIconDown = (HICON)LoadImage(hShell32DllInst, MAKEINTRESOURCE(16750), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
		hIconUp = (HICON)LoadImage(hShell32DllInst, MAKEINTRESOURCE(16749), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	} else {
		hIconDown = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_DOWN), IMAGE_ICON, 16, 16, 0);
		hIconUp = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_UP), IMAGE_ICON, 16, 16, 0);
	}

	// Create the language toolbar
	hLangToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, NULL, WS_CHILD | WS_TABSTOP | TBSTYLE_TRANSPARENT | CCS_NOPARENTALIGN |
		CCS_NORESIZE | CCS_NODIVIDER, 0, 0, 0, 0, hMainDialog, NULL, hMainInstance, NULL);
	if ((pfImageList_Create != NULL) && (pfImageList_AddIcon != NULL)) {
		hLangToolbarImageList = pfImageList_Create(i16, i16, ILC_COLOR32, 1, 0);
		pfImageList_AddIcon(hLangToolbarImageList, hIconLang);
		SendMessage(hLangToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hLangToolbarImageList);
	}
	SendMessage(hLangToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	memset(tbLangToolbarButtons, 0, sizeof(TBBUTTON));
	tbLangToolbarButtons[0].idCommand = lang_button_id;
	tbLangToolbarButtons[0].fsStyle = BTNS_WHOLEDROPDOWN;
	tbLangToolbarButtons[0].fsState = TBSTATE_ENABLED;
	SendMessage(hLangToolbar, TB_ADDBUTTONS, (WPARAM)1, (LPARAM)&tbLangToolbarButtons); // Add just the 1 button
	SendMessage(hLangToolbar, TB_GETRECT, lang_button_id, (LPARAM)&rcToolbarButton);

	// Make the toolbar window just big enough to hold the button
	// Set the top margin to 4 DIPs and the right margin so that it's aligned with the Device List Combobox
	GetWindowRect(hDeviceList, &rcDeviceList);
	MapWindowPoints(NULL, hDlg, (POINT*)&rcDeviceList, 2);
	SetWindowPos(hLangToolbar, HWND_TOP, rcDeviceList.right - rcToolbarButton.right,
		(int)(4.0f * fScale), rcToolbarButton.right, rcToolbarButton.bottom, 0);
	ShowWindow(hLangToolbar, SW_SHOWNORMAL);

	// Add trailing space to the "Format Options" text
	hCtrl = GetDlgItem(hDlg, IDS_FORMAT_OPTIONS_GRP);
	GetWindowTextW(hCtrl, wtmp, ARRAYSIZE(wtmp));
	wtmp[wcslen(wtmp)] = ' ';
	SetWindowTextW(hCtrl, wtmp);

	// Reposition and resize the Advanced button
	GetWindowRect(hCtrl, &rcFormatGroup);
	MapWindowPoints(NULL, hDlg, (POINT*)&rcFormatGroup, 2);
	sz = GetTextSize(hCtrl);
	GetWindowRect(GetDlgItem(hDlg, IDC_ADVANCED), &rcAdvancedOptions);
	// The label of a group box is always 8 pixels to the right, *regardless* of the zoom level
	SetWindowPos(GetDlgItem(hDlg, IDC_ADVANCED), hCtrl, rcFormatGroup.left + 8 + sz.cx,
		rcFormatGroup.top, i16 + (int)(4.0f * fScale), i16/2 + (int)(8.0f * fScale), 0);

	// Reposition and resize the Select Image button
	GetWindowRect(hBootType, &rcBootType);
	MapWindowPoints(NULL, hDlg, (POINT*)&rcBootType, 2);
	GetWindowRect(hSelectISO, &rcSelectImage);
	MapWindowPoints(NULL, hDlg, (POINT*)&rcSelectImage, 2);
	SetWindowPos(hSelectISO, NULL, rcSelectImage.left, rcBootType.top - 1,
		rcSelectImage.right - rcSelectImage.left, rcBootType.bottom - rcBootType.top + 2, SWP_NOZORDER);

	// The things one needs to do to keep things looking good...
	if (fScale > 1.4f) {
		ResizeMoveCtrl(hDlg, GetDlgItem(hMainDialog, IDS_ADVANCED_OPTIONS_GRP), 0, +1, 0, 0, fScale);
	}

	// Subclass the Info box so that we can align its text vertically
	info_original_proc = (WNDPROC)SetWindowLongPtr(hInfo, GWLP_WNDPROC, (LONG_PTR)InfoCallback);

	// Set the icons on the the buttons
	if ((pfImageList_Create != NULL) && (pfImageList_ReplaceIcon != NULL)) {

		bi_iso.himl = pfImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
		pfImageList_ReplaceIcon(bi_iso.himl, -1, hIconDisc);
		SetRect(&bi_iso.margin, 0, 1, 0, 0);
		bi_iso.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
		bi_down.himl = pfImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
		pfImageList_ReplaceIcon(bi_down.himl, -1, hIconDown);
		SetRect(&bi_down.margin, 0, 0, 0, 0);
		bi_down.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
		bi_up.himl = pfImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
		pfImageList_ReplaceIcon(bi_up.himl, -1, hIconUp);
		SetRect(&bi_up.margin, 0, 0, 0, 0);
		bi_up.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;

		SendMessage(hSelectISO, BCM_SETIMAGELIST, 0, (LPARAM)&bi_iso);
		SendMessage(GetDlgItem(hDlg, IDC_ADVANCED), BCM_SETIMAGELIST, 0,
			(LPARAM)(advanced_mode?&bi_up:&bi_down));
	}

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
	CreateTooltip(hStart, lmprintf(MSG_171), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ABOUT), lmprintf(MSG_172), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_WINDOWS_INSTALL), lmprintf(MSG_199), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_WINDOWS_TO_GO), lmprintf(MSG_200), -1);
	CreateTooltip(hStatusToolbar, lmprintf(MSG_272), -1);
	CreateTooltip(hLangToolbar, lmprintf(MSG_273), -1);

	// Set a label for the Advanced Mode and Select Image button for screen readers
	if (nWindowsVersion > WINDOWS_XP) {
		SetWindowTextU(GetDlgItem(hDlg, IDC_ADVANCED), lmprintf(MSG_160));
		SetWindowTextU(hSelectISO, lmprintf(MSG_165));
	}

	if (!advanced_mode)	// Hide as needed, since we display the advanced controls by default
		ToggleAdvanced(FALSE);
	ToggleToGo();

	// Process commandline parameters
	if (iso_provided) {
		// Simulate a button click for ISO selection
		PostMessage(hDlg, WM_COMMAND, IDC_SELECT_ISO, 0);
	}

	PrintInfo(0, MSG_210);
}

static void PrintStatusTimeout(const char* str, BOOL val)
{
	PrintStatus(STATUS_MSG_TIMEOUT, (val)?MSG_250:MSG_251, str);
}

static void ShowLanguageMenu(RECT rcExclude)
{
	TPMPARAMS tpm;
	HMENU menu;
	loc_cmd* lcmd = NULL;
	char lang[256];
	char *search = "()";
	char *l, *r, *str;

	UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
	menu = CreatePopupMenu();
	list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
		// The appearance of LTR languages must be fixed for RTL menus
		if ((right_to_left_mode) && (!(lcmd->ctrl_id & LOC_RIGHT_TO_LEFT)))  {
			str = safe_strdup(lcmd->txt[1]);
			l = strtok(str, search);
			r = strtok(NULL, search);
			static_sprintf(lang, LEFT_TO_RIGHT_EMBEDDING "(%s) " POP_DIRECTIONAL_FORMATTING "%s", r, l);
			safe_free(str);
		} else {
			static_strcpy(lang, lcmd->txt[1]);
		}
		InsertMenuU(menu, -1, MF_BYPOSITION|((selected_locale == lcmd)?MF_CHECKED:0), UM_LANGUAGE_MENU_MAX++, lang);
	}

	// Open the menu such that it doesn't overlap the specified rect
	tpm.cbSize = sizeof(TPMPARAMS);
	tpm.rcExclude = rcExclude;
	TrackPopupMenuEx(menu, 0,
		right_to_left_mode ? rcExclude.right : rcExclude.left, // In RTL languages, the menu should be placed at the bottom-right of the rect
		rcExclude.bottom, hMainDialog, &tpm);

	DestroyMenu(menu);
}

static void SetBoot(int fs, int tt, int pt)
{
	int i;
	char tmp[32];

	IGNORE_RETVAL(ComboBox_ResetContent(hBootType));
	if ((tt == TT_BIOS) && (IS_FAT(fs))) {
		if (nWindowsVersion < WINDOWS_10)	// The diskcopy.dll with the MS-DOS floppy image was removed in Windows 10
			IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "MS-DOS"), BT_MSDOS));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "FreeDOS"), BT_FREEDOS));
	}
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_036)), BT_ISO));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_095)), BT_IMG));
	// If needed (advanced mode) also append "bare" Syslinux and other options
	if ( (tt == TT_BIOS) && ((IS_FAT(fs) || (fs == FS_NTFS)) && (advanced_mode)) ) {
		static_sprintf(tmp, "Syslinux %s", embedded_sl_version_str[0]);
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, tmp), BT_SYSLINUX_V4));
		static_sprintf(tmp, "Syslinux %s", embedded_sl_version_str[1]);
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, tmp), BT_SYSLINUX_V6));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "ReactOS"), BT_REACTOS));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType,
			"Grub " GRUB2_PACKAGE_VERSION), BT_GRUB2));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType,
			"Grub4DOS " GRUB4DOS_VERSION), BT_GRUB4DOS));
	}
	if (advanced_mode)
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "UEFI:NTFS"), BT_UEFI_NTFS));
	if ((!advanced_mode) && (selection_default >= BT_SYSLINUX_V4)) {
		selection_default = BT_FREEDOS;
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

	if ((pt != PARTITION_STYLE_SFD) && !IsWindowEnabled(hBoot)) {
		EnableWindow(hBoot, TRUE);
		EnableWindow(hBootType, TRUE);
		EnableWindow(hSelectISO, TRUE);
		EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL), TRUE);
		EnableWindow(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO), TRUE);
		CheckDlgButton(hMainDialog, IDC_BOOT, uBootChecked);
	}
}

static void SaveVHD(void)
{
	static IMG_SAVE img_save = { 0 };
	char filename[128];
	char path[MAX_PATH];
	int DriveIndex = ComboBox_GetCurSel(hDeviceList);
	EXT_DECL(img_ext, filename, __VA_GROUP__("*.vhd"), __VA_GROUP__(lmprintf(MSG_095)));
	ULARGE_INTEGER free_space;

	if (DriveIndex >= 0)
		static_sprintf(filename, "%s.vhd", DriveLabel.String[DriveIndex]);
	if ((DriveIndex != CB_ERR) && (!format_op_in_progress) && (format_thid == NULL)) {
		img_save.Type = IMG_SAVE_TYPE_VHD;
		img_save.DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, DriveIndex);
		img_save.ImagePath = FileDialog(TRUE, NULL, &img_ext, 0);
		img_save.BufSize = DD_BUFFER_SIZE;
		img_save.DeviceSize = SelectedDrive.DiskSize;
		if (img_save.ImagePath != NULL) {
			// Reset all progress bars
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
			SetTaskbarProgressState(TASKBAR_NORMAL);
			SetTaskbarProgressValue(0, MAX_PROGRESS);
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
			FormatStatus = 0;
			format_op_in_progress = TRUE;
			free_space.QuadPart = 0;
			if ((GetVolumePathNameA(img_save.ImagePath, path, sizeof(path)))
				&& (GetDiskFreeSpaceExA(path, &free_space, NULL, NULL))
				&& ((LONGLONG)free_space.QuadPart > (SelectedDrive.DiskSize + 512))) {
				// Disable all controls except cancel
				EnableControls(FALSE);
				FormatStatus = 0;
				InitProgress(TRUE);
				format_thid = CreateThread(NULL, 0, SaveImageThread, &img_save, 0, NULL);
				if (format_thid != NULL) {
					uprintf("\r\nSave to VHD operation started");
					PrintInfo(0, -1);
					timer = 0;
					static_sprintf(szTimer, "00:00:00");
					SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
					SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
				} else {
					uprintf("Unable to start VHD save thread");
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_START_THREAD);
					safe_free(img_save.ImagePath);
					PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
					format_op_in_progress = FALSE;
				}
			} else {
				if (free_space.QuadPart == 0) {
					uprintf("Unable to isolate drive name for VHD save");
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_PATH_NOT_FOUND;
				} else {
					uprintf("The VHD size is too large for the target drive");
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_FILE_TOO_LARGE;
				}
				safe_free(img_save.ImagePath);
				PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
				format_op_in_progress = FALSE;
			}
		}
	}
}

static void SaveISO(void)
{
	static IMG_SAVE img_save = { 0 };
	char filename[33] = "disc_image.iso";
	EXT_DECL(img_ext, filename, __VA_GROUP__("*.iso"), __VA_GROUP__(lmprintf(MSG_036)));

	if ((format_op_in_progress) || (format_thid != NULL))
		return;

	img_save.Type = IMG_SAVE_TYPE_ISO;
	if (!GetOpticalMedia(&img_save)) {
		uprintf("No dumpable optical media found.");
		return;
	}
	// Adjust the buffer size according to the disc size so that we get a decent speed.
	for (img_save.BufSize = 32 * MB;
		(img_save.BufSize > 8 * MB) && (img_save.DeviceSize <= img_save.BufSize * 64);
		img_save.BufSize /= 2);
	if ((img_save.Label != NULL) && (img_save.Label[0] != 0))
		static_sprintf(filename, "%s.iso", img_save.Label);
	uprintf("ISO media size %s", SizeToHumanReadable(img_save.DeviceSize, FALSE, FALSE));

	img_save.ImagePath = FileDialog(TRUE, NULL, &img_ext, 0);
	if (img_save.ImagePath == NULL)
		return;
	// Reset all progress bars
	SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
	SetTaskbarProgressState(TASKBAR_NORMAL);
	SetTaskbarProgressValue(0, MAX_PROGRESS);
	SendMessage(hProgress, PBM_SETPOS, 0, 0);
	FormatStatus = 0;
	format_op_in_progress = TRUE;
	// Disable all controls except cancel
	EnableControls(FALSE);
	InitProgress(TRUE);
	format_thid = CreateThread(NULL, 0, SaveImageThread, &img_save, 0, NULL);
	if (format_thid != NULL) {
		uprintf("\r\nSave to ISO operation started");
		PrintInfo(0, -1);
		timer = 0;
		static_sprintf(szTimer, "00:00:00");
		SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
		SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
	} else {
		uprintf("Unable to start ISO save thread");
		FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_START_THREAD);
		safe_free(img_save.ImagePath);
		PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
		format_op_in_progress = FALSE;
	}
}

// Check for conflicting processes accessing the drive, and if any,
// ask the user whether they want to proceed.
// Parameter is the maximum amount of time we allow for this call to execute (in ms)
static BOOL CheckDriveAccess(DWORD dwTimeOut)
{
	uint32_t i, j;
	BOOL ret = FALSE, proceed = TRUE;
	BYTE access_mask;
	char *PhysicalPath = NULL, DevPath[MAX_PATH];
	char drive_letter[27], drive_name[] = "?:";
	char *message, title[128];
	DWORD cur_time, end_time = GetTickCount() + dwTimeOut;

	// Get the current selected device
	DWORD DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList));
	if ((DeviceNum < 0x80) || (DeviceNum == (DWORD)-1))
		return FALSE;

	// TODO: "Checking for conflicting processes..." would be better but
	// but "Requesting disk access..." will have to do for now.
	PrintInfo(0, MSG_225);

	// Search for any blocking processes against the physical drive
	PhysicalPath = GetPhysicalName(DeviceNum);
	if (QueryDosDeviceA(&PhysicalPath[4], DevPath, sizeof(DevPath)) != 0) {
		access_mask = SearchProcess(DevPath, dwTimeOut, TRUE, TRUE, TRUE);
		CHECK_FOR_USER_CANCEL;
		if (access_mask != 0) {
			proceed = FALSE;
			uprintf("Found potentially blocking process(es) against %s:", &PhysicalPath[4]);
			for (j = 0; j < BlockingProcess.Index; j++)
				uprintf(BlockingProcess.String[j]);
		}
	}

	// Search for any blocking processes against the logical volume(s)
	GetDriveLetters(DeviceNum, drive_letter);
	for (i = 0; drive_letter[i]; i++) {
		drive_name[0] = drive_letter[i];
		if (QueryDosDeviceA(drive_name, DevPath, sizeof(DevPath)) != 0) {
			StrArrayClear(&BlockingProcess);
			cur_time = GetTickCount();
			if (cur_time >= end_time)
				break;
			access_mask = SearchProcess(DevPath, end_time - cur_time, TRUE, TRUE, TRUE);
			CHECK_FOR_USER_CANCEL;
			// Ignore if all we have is read-only
			if ((access_mask & 0x06) || (access_mask == 0x80)) {
				proceed = FALSE;
				uprintf("Found potentially blocking process(es) against %s", drive_name);
				for (j = 0; j < BlockingProcess.Index; j++)
					uprintf(BlockingProcess.String[j]);
			}
		}
	}

	// Prompt the user if we detected blocking processes
	if (!proceed) {
		// We'll use a system translated string instead of one from rufus.loc
		message = GetMuiString("shell32.dll", 28701);	// "This drive is in use (...) Do you want to format it anyway?"
		if (message != NULL) {
			ComboBox_GetTextU(hDeviceList, title, sizeof(title));
			proceed = Notification(MSG_WARNING_QUESTION, NULL, title, message);
			free(message);
		}
	}
	ret = proceed;

out:
	PrintInfo(0, MSG_210);
	free(PhysicalPath);
	return ret;
}

#ifdef RUFUS_TEST
	extern int SelectionDyn(char* title, char* message, char** szChoice, int nChoices);
#endif

/*
 * Main dialog callback
 */
static INT_PTR CALLBACK MainCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	static DWORD DeviceNum = 0;
	static uint64_t LastRefresh = 0;
	static BOOL first_log_display = TRUE, isMarquee = FALSE, queued_hotplug_event = FALSE;
	static ULONG ulRegister = 0;
	static LPITEMIDLIST pidlDesktop = NULL;
	static MY_SHChangeNotifyEntry NotifyEntry;
	static DWORD_PTR thread_affinity[4];
	DRAWITEMSTRUCT* pDI;
	HDROP droppedFileInfo;
	POINT Point;
	RECT DialogRect, DesktopRect, LangToolbarRect;
	LONG progress_style;
	HDC hDC;
	int nDeviceIndex, fs, tt, pt, i, nWidth, nHeight, nb_devices, selected_language, offset;
	char tmp[128];
	wchar_t* wbuffer = NULL;
	loc_cmd* lcmd = NULL;
	EXT_DECL(img_ext, NULL, __VA_GROUP__("*.img;*.vhd;*.gz;*.bzip2;*.xz;*.lzma;*.Z;*.zip"), __VA_GROUP__(lmprintf(MSG_095)));
	EXT_DECL(iso_ext, NULL, __VA_GROUP__("*.iso"), __VA_GROUP__(lmprintf(MSG_036)));
	LPNMTOOLBAR lpnmtb;

	switch (message) {

	case WM_COMMAND:
#ifdef RUFUS_TEST
		if (LOWORD(wParam) == IDC_TEST) {
			ExtractEfiImgFiles("C:\\rufus");
//			ExtractEFI("C:\\rufus\\efi.img", "C:\\rufus\\efi");
//			uprintf("Proceed = %s", CheckDriveAccess(2000)?"True":"False");
//			char* choices[] = { "Choice 1", "Choice 2", "Choice 3" };
//			SelectionDyn("Test Choice", "Unused", choices, ARRAYSIZE(choices));
			break;
		}
#endif

		if ((LOWORD(wParam) >= UM_LANGUAGE_MENU) && (LOWORD(wParam) < UM_LANGUAGE_MENU_MAX)) {
			selected_language = LOWORD(wParam) - UM_LANGUAGE_MENU;
			i = 0;
			list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
				if (i++ == selected_language) {
					if (selected_locale != lcmd) {
						selected_locale = lcmd;
						selected_langid = get_language_id(lcmd);
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
			PF_INIT(SHChangeNotifyDeregister, Shell32);
			EnableWindow(GetDlgItem(hDlg, IDCANCEL), FALSE);
			if (format_thid != NULL) {
				if ((no_confirmation_on_cancel) || (MessageBoxExU(hMainDialog, lmprintf(MSG_105), lmprintf(MSG_049),
					MB_YESNO|MB_ICONWARNING|MB_IS_RTL, selected_langid) == IDYES)) {
					// Operation may have completed in the meantime
					if (format_thid != NULL) {
						FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
						PrintInfo(0, MSG_201);
						uprintf("Cancelling");
						//  Start a timer to detect blocking operations during ISO file extraction
						if (iso_blocking_status >= 0) {
							last_iso_blocking_status = iso_blocking_status;
							SetTimer(hMainDialog, TID_BLOCKING_TIMER, 3000, BlockingTimer);
						}
					}
				} else {
					EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
				}
				no_confirmation_on_cancel = FALSE;
				return (INT_PTR)TRUE;
			} else if (format_op_in_progress) {
				// User might be trying to cancel during preliminary checks
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
				PrintInfo(0, MSG_201);
				return (INT_PTR)TRUE;
			}
			if ((pfSHChangeNotifyDeregister != NULL) && (ulRegister != 0))
				pfSHChangeNotifyDeregister(ulRegister);
			PostQuitMessage(0);
			StrArrayDestroy(&DriveID);
			StrArrayDestroy(&DriveLabel);
			StrArrayDestroy(&BlockingProcess);
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
				offset = GetSystemMetrics(SM_CXSIZEFRAME) + (int)(2.0f * fScale);
				if (nWindowsVersion >= WINDOWS_10)
					offset += (int)(-14.0f * fScale);
				if (right_to_left_mode)
					Point.x = max(DialogRect.left - offset - nWidth, 0);
				else
					Point.x = min(DialogRect.right + offset, DesktopRect.right - nWidth);

				Point.y = max(DialogRect.top, DesktopRect.top - nHeight);
				MoveWindow(hLogDlg, Point.x, Point.y, nWidth, nHeight, FALSE);
				// The log may have been recentered to fit the screen, in which case, try to shift our main dialog left (or right for RTL)
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				if (right_to_left_mode) {
					Point.x = DialogRect.left;
					GetWindowRect(hLogDlg, &DialogRect);
					Point.x = max(Point.x, DialogRect.right - DialogRect.left + offset);
				} else {
					Point.x = max((DialogRect.left<0)?DialogRect.left:0, Point.x - offset - nWidth);
				}
				MoveWindow(hDlg, Point.x, Point.y, nWidth, nHeight, TRUE);
				first_log_display = FALSE;
			}
			// Display the log Window
			log_displayed = !log_displayed;
			// Set focus on the start button
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)FALSE, 0);
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)hStart, TRUE);
			// Must come last for the log window to get focus
			ShowWindow(hLogDlg, log_displayed?SW_SHOW:SW_HIDE);
			break;
		case IDC_ADVANCED:
			advanced_mode = !advanced_mode;
			WriteSettingBool(SETTING_ADVANCED_MODE, advanced_mode);
			ToggleAdvanced(advanced_mode);
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_LABEL:
			if (HIWORD(wParam) == EN_CHANGE) {
				// We will get EN_CHANGE when we change the label automatically, so we need to detect that
				if (!app_changed_label)
					user_changed_label = TRUE;
				app_changed_label = FALSE;
			}
			break;
		case IDC_DEVICE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			nb_devices = ComboBox_GetCount(hDeviceList);
			PrintStatusDebug(0, (nb_devices==1)?MSG_208:MSG_209, nb_devices);
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
			pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
			// If a user switches to GPT before an image is selected, it is reasonable to assume that they prefer GPT
			prefer_gpt = (pt == PARTITION_STYLE_GPT) && (image_path == NULL);
			// fall-through
		case IDC_FILESYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			tt = GETTARGETTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
			pt = GETPARTTYPE((int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme)));
			if ((selection_default == BT_IMG) && IsChecked(IDC_BOOT)) {
				ToggleImage(FALSE);
				EnableAdvancedBootOptions(FALSE, TRUE);
				SetBoot(fs, tt, pt);
				SetToGo();
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
				if (ComboBox_GetItemData(hBootType, ComboBox_GetCount(hBootType)-1) == (BT_MAX-1)) {
					for (i=BT_SYSLINUX_V4; i<BT_MAX; i++)
						IGNORE_RETVAL(ComboBox_DeleteString(hBootType,  ComboBox_GetCount(hBootType)-1));
					IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, 1));
				}
				break;
			}
			if ((fs == FS_EXFAT) || (fs == FS_UDF) || (fs == FS_REFS) || (pt == PARTITION_STYLE_SFD)) {
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
			SetBoot(fs, tt, pt);
			SetMBRProps();
			SetToGo();
			break;
		case IDC_BOOT:
			EnableAdvancedBootOptions(TRUE, TRUE);
			if (selection_default == BT_IMG)
				ToggleImage(!IsChecked(IDC_BOOT));
			SetProposedLabel(ComboBox_GetCurSel(hDeviceList));
			break;
		case IDC_BOOTTYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			selection_default = (int) ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			EnableAdvancedBootOptions(TRUE, TRUE);
			ToggleImage(!IsChecked(IDC_BOOT) || (selection_default != BT_IMG));
			SetToGo();
			SetProposedLabel(ComboBox_GetCurSel(hDeviceList));
			if (selection_default == BT_UEFI_NTFS) {
				// Try to select NTFS as default
				for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
					fs = (int)ComboBox_GetItemData(hFileSystem, i);
					if (fs == FS_NTFS)
						IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
				}
				SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILESYSTEM,
					ComboBox_GetCurSel(hFileSystem));
			}
			// Reset disk ID to 0x80 if Rufus MBR is used
			if ((selection_default != BT_ISO) && (selection_default != BT_IMG)) {
				IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
			}
			return (INT_PTR)TRUE;
		case IDC_SELECT_ISO:
			if (iso_provided) {
				uprintf("\r\nImage provided: '%s'", image_path);
				iso_provided = FALSE;	// One off thing...
			} else {
				safe_free(image_path);
				EnableWindow(hStatusToolbar, FALSE);
				image_path = FileDialog(FALSE, NULL, (selection_default == BT_IMG)?&img_ext:&iso_ext, 0);
				if (image_path == NULL) {
					CreateTooltip(hSelectISO, lmprintf(MSG_173), -1);
					PrintStatus(0, MSG_086);
					break;
				}
			}
			FormatStatus = 0;
			if (CreateThread(NULL, 0, ISOScanThread, NULL, 0, NULL) == NULL) {
				uprintf("Unable to start ISO scanning thread");
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
			}
			break;
		case IDC_WINDOWS_INSTALL:
		case IDC_WINDOWS_TO_GO:
			if ( (Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_INSTALL)) == BST_CHECKED) ||
				 (Button_GetCheck(GetDlgItem(hMainDialog, IDC_WINDOWS_TO_GO)) == BST_CHECKED) ) {
				SetFSFromISO();
				SetMBRProps();
				SetMBRForUEFI(TRUE);
			}
			break;
		case IDC_RUFUS_MBR:
			if ((HIWORD(wParam)) == BN_CLICKED)
				mbr_selected_by_user = IsChecked(IDC_RUFUS_MBR);
			break;
		case IDC_ENABLE_FIXED_DISKS:
			if ((HIWORD(wParam)) == BN_CLICKED) {
				enable_HDDs = !enable_HDDs;
				PrintStatusTimeout(lmprintf(MSG_253), enable_HDDs);
				GetDevices(0);
			}
			break;
		case IDC_START:
			if (format_thid != NULL) {
				return (INT_PTR)TRUE;
			}
			// Disable all controls except Cancel
			EnableControls(FALSE);
			FormatStatus = 0;
			StrArrayClear(&BlockingProcess);
			format_op_in_progress = TRUE;
			no_confirmation_on_cancel = FALSE;
			// Reset all progress bars
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
			SetTaskbarProgressState(TASKBAR_NORMAL);
			SetTaskbarProgressValue(0, MAX_PROGRESS);
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
			selection_default = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			if (nDeviceIndex != CB_ERR) {
				if (!zero_drive) {
					if ((IsChecked(IDC_BOOT)) && (!BootCheck()))
						goto aborted_start;

					// Display a warning about UDF formatting times
					fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
					if (fs == FS_UDF) {
						dur_secs = (uint32_t)(((double)SelectedDrive.DiskSize) / 1073741824.0f / UDF_FORMAT_SPEED);
						if (dur_secs > UDF_FORMAT_WARN) {
							dur_mins = dur_secs / 60;
							dur_secs -= dur_mins * 60;
							MessageBoxExU(hMainDialog, lmprintf(MSG_112, dur_mins, dur_secs), lmprintf(MSG_113),
								MB_OK | MB_ICONASTERISK | MB_IS_RTL, selected_langid);
						} else {
							dur_secs = 0;
							dur_mins = 0;
						}
					}

					// Ask users how they want to write ISOHybrid images
					if ((IsChecked(IDC_BOOT)) && (img_report.is_bootable_img) &&
						(ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType)) == BT_ISO)) {
						char* iso_image = lmprintf(MSG_036);
						char* dd_image = lmprintf(MSG_095);
						char* choices[2] = { lmprintf(MSG_276, iso_image), lmprintf(MSG_277, dd_image) };
						i = SelectionDialog(lmprintf(MSG_274), lmprintf(MSG_275, iso_image, dd_image, iso_image, dd_image),
							choices, 2);
						if (i < 0) {	// Cancel
							goto aborted_start;
						} else if (i == 2) {
							selection_default = BT_IMG;
							SetComboEntry(hBootType, selection_default);
						}
					}
				}

				if (!CheckDriveAccess(2000))
					goto aborted_start;

				GetWindowTextU(hDeviceList, tmp, ARRAYSIZE(tmp));
				if (MessageBoxExU(hMainDialog, lmprintf(MSG_003, tmp),
					APPLICATION_NAME, MB_OKCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid) == IDCANCEL) 
					goto aborted_start;
				if ((SelectedDrive.nPartitions > 1) && (MessageBoxExU(hMainDialog, lmprintf(MSG_093),
					lmprintf(MSG_094), MB_OKCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid) == IDCANCEL))
					goto aborted_start;
				if ((!zero_drive) && (IsChecked(IDC_BOOT)) && (SelectedDrive.SectorSize != 512) &&
					(MessageBoxExU(hMainDialog, lmprintf(MSG_196, SelectedDrive.SectorSize),
						lmprintf(MSG_197), MB_OKCANCEL|MB_ICONWARNING|MB_IS_RTL, selected_langid) == IDCANCEL))
					goto aborted_start;

				DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
				InitProgress(zero_drive);
				format_thid = CreateThread(NULL, 0, FormatThread, (LPVOID)(uintptr_t)DeviceNum, 0, NULL);
				if (format_thid == NULL) {
					uprintf("Unable to start formatting thread");
					FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|APPERR(ERROR_CANT_START_THREAD);
					PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
				} else {
					uprintf("\r\nFormat operation started");
					PrintInfo(0, -1);
					timer = 0;
					static_sprintf(szTimer, "00:00:00");
					SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
					SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
				}
			}
			if (format_thid != NULL)
				break;
		aborted_start:
			format_op_in_progress = FALSE;
			EnableControls(TRUE);
			zero_drive = FALSE;
			if (queued_hotplug_event)
				SendMessage(hDlg, UM_MEDIA_CHANGE, 0, 0);
			EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
			break;
		case IDC_HASH:
			if ((format_thid == NULL) && (image_path != NULL)) {
				FormatStatus = 0;
				format_op_in_progress = TRUE;
				no_confirmation_on_cancel = TRUE;
				// Reset all progress bars
				SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
				SetTaskbarProgressState(TASKBAR_NORMAL);
				SetTaskbarProgressValue(0, MAX_PROGRESS);
				SendMessage(hProgress, PBM_SETPOS, 0, 0);
				// Disable all controls except cancel
				EnableControls(FALSE);
				InitProgress(FALSE);
				SetThreadAffinity(thread_affinity, CHECKSUM_MAX + 1);
				format_thid = CreateThread(NULL, 0, SumThread, (LPVOID)thread_affinity, 0, NULL);
				if (format_thid != NULL) {
					PrintInfo(0, -1);
					timer = 0;
					static_sprintf(szTimer, "00:00:00");
					SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
					SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
				} else {
					uprintf("Unable to start checksum thread");
					FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_START_THREAD);
					PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
					format_op_in_progress = FALSE;
				}
			}
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

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
		// Instead, use a custom user message, such as UM_MEDIA_CHANGE, to set DBT_CUSTOMEVENT.
		if (format_thid == NULL) {
			switch (wParam) {
			case DBT_DEVICEARRIVAL:
			case DBT_DEVICEREMOVECOMPLETE:
			case DBT_CUSTOMEVENT:	// Sent by our timer refresh function or for card reader media change
				LastRefresh = _GetTickCount64();
				KillTimer(hMainDialog, TID_REFRESH_TIMER);
				if (!format_op_in_progress) {
					queued_hotplug_event = FALSE;
					GetDevices((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
					user_changed_label = FALSE;
				}
				else {
					queued_hotplug_event = TRUE;
				}
				return (INT_PTR)TRUE;
			case DBT_DEVNODES_CHANGED:
				// If it's been more than a second since last device refresh, arm a refresh timer
				if (_GetTickCount64() > LastRefresh + 1000) {
					LastRefresh = _GetTickCount64();
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
		// Make sure fScale is set before the first call to apply localization, so that move/resize scale appropriately
		hDC = GetDC(hDlg);
		fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
		if (hDC != NULL)
			ReleaseDC(hDlg, hDC);
		apply_localization(IDD_DIALOG, hDlg);
		SetUpdateCheck();
		togo_mode = TRUE;	// We display the ToGo controls by default and need to hide them
							// Create the log window (hidden)
		first_log_display = TRUE;
		log_displayed = FALSE;
		hLogDlg = MyCreateDialog(hMainInstance, IDD_LOG, hDlg, (DLGPROC)LogProc);
		InitDialog(hDlg);
		GetDevices(0);
		CheckForUpdates(FALSE);
		// Register MEDIA_INSERTED/MEDIA_REMOVED notifications for card readers
		if ((pfSHChangeNotifyRegister != NULL) && (SUCCEEDED(SHGetSpecialFolderLocation(0, CSIDL_DESKTOP, &pidlDesktop)))) {
			NotifyEntry.pidl = pidlDesktop;
			NotifyEntry.fRecursive = TRUE;
			// NB: The following only works if the media is already formatted.
			// If you insert a blank card, notifications will not be sent... :(
			ulRegister = pfSHChangeNotifyRegister(hDlg, 0x0001 | 0x0002 | 0x8000,
				SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED, UM_MEDIA_CHANGE, 1, &NotifyEntry);
		}
		// Bring our Window on top. We have to go through all *THREE* of these, or Far Manager hides our window :(
		SetWindowPos(hMainDialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowPos(hMainDialog, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowPos(hMainDialog, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

		// Set 'Start' as the selected button if it's enabled, otherwise use 'Select ISO', instead
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)(IsWindowEnabled(hStart) ? hStart : hSelectISO), TRUE);

#if defined(ALPHA)
		// Add a VERY ANNOYING popup for Alpha releases, so that people don't start redistributing them
		MessageBoxA(NULL, "This is an Alpha version of " APPLICATION_NAME " - It is meant to be used for "
			"testing ONLY and should NOT be distributed as a release.", "ALPHA VERSION", MSG_INFO);
#elif defined(TEST)
		// Same thing for Test releases
		MessageBoxA(NULL, "This is a Test version of " APPLICATION_NAME " - It is meant to be used for "
			"testing ONLY and should NOT be distributed as a release.", "TEST VERSION", MSG_INFO);
#endif
		return (INT_PTR)FALSE;

		// The things one must do to get an ellipsis and text alignment on the status bar...
	case WM_DRAWITEM:
		if (wParam == IDC_STATUS) {
			pDI = (DRAWITEMSTRUCT*)lParam;
			pDI->rcItem.top -= (int)((4.0f * fScale) - 6.0f);
			pDI->rcItem.left += (int)(((pDI->itemID == SB_SECTION_MIDDLE) ? -2.0f : 4.0f) * fScale);
			SetBkMode(pDI->hDC, TRANSPARENT);
			switch (pDI->itemID) {
			case SB_SECTION_LEFT:
				SetTextColor(pDI->hDC, GetSysColor(COLOR_BTNTEXT));
				DrawTextExU(pDI->hDC, szStatusMessage, -1, &pDI->rcItem,
					DT_LEFT | DT_END_ELLIPSIS | DT_PATH_ELLIPSIS, NULL);
				return (INT_PTR)TRUE;
			case SB_SECTION_RIGHT:
				SetTextColor(pDI->hDC, GetSysColor(COLOR_3DSHADOW));
				DrawTextExA(pDI->hDC, szTimer, -1, &pDI->rcItem, DT_LEFT, NULL);
				return (INT_PTR)TRUE;
			}
		}
		break;

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case TBN_DROPDOWN:
			lpnmtb = (LPNMTOOLBAR)lParam;

			// We only care about the language button on the language toolbar
			if (lpnmtb->hdr.hwndFrom == hLangToolbar
				&& lpnmtb->iItem == lang_button_id) {
				// Get toolbar button rect and map it to actual screen pixels
				SendMessage(lpnmtb->hdr.hwndFrom, TB_GETRECT, (WPARAM)lpnmtb->iItem, (LPARAM)&LangToolbarRect);
				MapWindowPoints(lpnmtb->hdr.hwndFrom, NULL, (POINT*)&LangToolbarRect, 2);

				// Show the language menu such that it doesn't overlap the button
				ShowLanguageMenu(LangToolbarRect);
				return (INT_PTR)TBDDRET_DEFAULT;
			}
			break;
		}

		break;

	case WM_DROPFILES:
		droppedFileInfo = (HDROP)wParam;
		wbuffer = calloc(MAX_PATH, sizeof(wchar_t));
		if (wbuffer == NULL) {
			uprintf("Failed to alloc buffer for drag-n-drop");
			break;
		}
		DragQueryFileW(droppedFileInfo, 0, wbuffer, MAX_PATH);
		safe_free(image_path);
		image_path = wchar_to_utf8(wbuffer);
		safe_free(wbuffer);

		if (image_path != NULL) {
			iso_provided = TRUE;
			// Simulate ISO selection click
			SendMessage(hDlg, WM_COMMAND, IDC_SELECT_ISO, 0);
		}
		break;

	// This is >>>SUPER WEIRD<<<. After a successful ISO or DD write (e.g. Arch 2016.01)
	// we no longer receive WM_QUERYENDSESSION messages, only WM_ENDSESSION.
	// But if we do a FreeDOS format, WM_QUERYENDSESSION is still sent to us alright.
	// What the heck is going on here?!?
	// Also, even as we try to work around this, WM_ENDSESSION comes too late in the game
	// to prevent shutdown block. So we need to handle the _undocumented_ WM_CLIENTSHUTDOWN.
	case WM_CLOSE:
	case WM_CLIENTSHUTDOWN:
	case WM_QUERYENDSESSION:
	case WM_ENDSESSION:
		if (format_op_in_progress) {
			return (INT_PTR)TRUE;
		}
		if (message == WM_CLOSE) {
			// We must use PostQuitMessage() on VM_CLOSE, to prevent notification sound...
			PostQuitMessage(0);
		} else {
			// ...but we must simulate Cancel on shutdown requests, else the app freezes.
			SendMessage(hDlg, WM_COMMAND, (WPARAM)IDCANCEL, (LPARAM)0);
		}
		break;

	case UM_PROGRESS_INIT:
		isMarquee = (wParam == PBS_MARQUEE);
		if (isMarquee) {
			progress_style = GetWindowLong(hProgress, GWL_STYLE);
			SetWindowLong(hProgress, GWL_STYLE, progress_style | PBS_MARQUEE);
			SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 0);
		} else {
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
		}
		SetTaskbarProgressState(TASKBAR_NORMAL);
		SetTaskbarProgressValue(0, MAX_PROGRESS);

		break;

	case UM_PROGRESS_EXIT:
		if (isMarquee) {
			// Remove marquee style if previously set
			progress_style = GetWindowLong(hProgress, GWL_STYLE);
			SetWindowLong(hProgress, GWL_STYLE, progress_style & (~PBS_MARQUEE));
			SetTaskbarProgressValue(0, MAX_PROGRESS);
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
		} else if (!IS_ERROR(FormatStatus)) {
			SetTaskbarProgressValue(MAX_PROGRESS, MAX_PROGRESS);
			// This is the only way to achieve instantaneous progress transition to 100%
			SendMessage(hProgress, PBM_SETRANGE, 0, ((MAX_PROGRESS+1)<<16) & 0xFFFF0000);
			SendMessage(hProgress, PBM_SETPOS, (MAX_PROGRESS+1), 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
		}
		SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
		SetTaskbarProgressState(TASKBAR_NORMAL);
		break;

	case UM_NO_UPDATE:
		Notification(MSG_INFO, NULL, lmprintf(MSG_243), lmprintf(MSG_247));
		// Need to manually set focus back to "Check Now" for tabbing to work
		SendMessage(hUpdatesDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hUpdatesDlg, IDC_CHECK_NOW), TRUE);
		break;

	case UM_FORMAT_COMPLETED:
		format_thid = NULL;
		// Stop the timer
		KillTimer(hMainDialog, TID_APP_TIMER);
		// Close the cancel MessageBox and Blocking notification if active
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), lmprintf(MSG_049)), WM_COMMAND, IDNO, 0);
		SendMessage(FindWindowA(MAKEINTRESOURCEA(32770), lmprintf(MSG_049)), WM_COMMAND, IDYES, 0);
		EnableWindow(GetDlgItem(hMainDialog, IDCANCEL), TRUE);
		EnableControls(TRUE);
		if (wParam) {
			uprintf("\r\n");
			GetDevices(DeviceNum);
		}
		if (!IS_ERROR(FormatStatus)) {
			// This is the only way to achieve instantaneous progress transition to 100%
			SendMessage(hProgress, PBM_SETRANGE, 0, ((MAX_PROGRESS+1)<<16) & 0xFFFF0000);
			SendMessage(hProgress, PBM_SETPOS, (MAX_PROGRESS+1), 0);
			SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
			SetTaskbarProgressState(TASKBAR_NOPROGRESS);
			PrintInfo(0, MSG_210);
			MessageBeep(MB_OK);
			FlashTaskbar(dialog_handle);
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_PAUSED, 0);
			SetTaskbarProgressState(TASKBAR_PAUSED);
			PrintInfo(0, MSG_211);
			Notification(MSG_INFO, NULL, lmprintf(MSG_211), lmprintf(MSG_041));
		} else {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			PrintInfo(0, MSG_212);
			MessageBeep(MB_ICONERROR);
			FlashTaskbar(dialog_handle);
			if (BlockingProcess.Index > 0)
				ListDialog(lmprintf(MSG_042), lmprintf(MSG_055), BlockingProcess.String, BlockingProcess.Index);
			else
				Notification(MSG_ERROR, NULL, lmprintf(MSG_042), lmprintf(MSG_043, StrError(FormatStatus, FALSE)));
		}
		FormatStatus = 0;
		format_op_in_progress = FALSE;
		return (INT_PTR)TRUE;

	// Ensures that SetPartitionSchemeTooltip() can be called from the original thread
	case UM_SET_PARTITION_SCHEME_TOOLTIP:
		SetPartitionSchemeTooltip();
		break;
	}
	return (INT_PTR)FALSE;
}

static void PrintUsage(char* appname)
{
	char fname[_MAX_FNAME];

	_splitpath(appname, NULL, NULL, fname, NULL);
	printf("\nUsage: %s [-f] [-g] [-h] [-i PATH] [-l LOCALE] [-w TIMEOUT]\n", fname);
	printf("  -f, --fixed\n");
	printf("     Enable the listing of fixed/HDD USB drives\n");
	printf("  -g, --gui\n");
	printf("     Start in GUI mode (disable the 'rufus.com' commandline hogger)\n");
	printf("  -i PATH, --iso=PATH\n");
	printf("     Select the ISO image pointed by PATH to be used on startup\n");
	printf("  -l LOCALE, --locale=LOCALE\n");
	printf("     Select the locale to be used on startup\n");
	printf("  -w TIMEOUT, --wait=TIMEOUT\n");
	printf("     Wait TIMEOUT tens of seconds for the global application mutex to be released.\n");
	printf("     Used when launching a newer version of " APPLICATION_NAME " from a running application.\n");
	printf("  -h, --help\n");
	printf("     This usage guide.\n");
}

static HANDLE SetHogger(BOOL attached_console, BOOL disable_hogger)
{
	INPUT* input;
	BYTE* hog_data;
	DWORD hog_size, Size;
	HANDLE hogmutex = NULL, hFile = NULL;
	int i;

	if (!attached_console)
		return NULL;

	hog_data = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_XT_HOGGER),
		_RT_RCDATA, cmdline_hogger, &hog_size, FALSE);
	if ((hog_data != NULL) && (!disable_hogger)) {
		// Create our synchronisation mutex
		hogmutex = CreateMutexA(NULL, TRUE, "Global/Rufus_CmdLine");

		// Extract the hogger resource
		hFile = CreateFileA(cmdline_hogger, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile != INVALID_HANDLE_VALUE) {
			// coverity[check_return]
			WriteFile(hFile, hog_data, hog_size, &Size, NULL);
		}
		safe_closehandle(hFile);

		// Now launch the file from the commandline, by simulating keypresses
		input = (INPUT*)calloc(strlen(cmdline_hogger)+1, sizeof(INPUT));
		if (input != NULL) {
			for (i = 0; i < (int)strlen(cmdline_hogger); i++) {
				input[i].type = INPUT_KEYBOARD;
				input[i].ki.dwFlags = KEYEVENTF_UNICODE;
				input[i].ki.wScan = (wchar_t)cmdline_hogger[i];
			}
			input[i].type = INPUT_KEYBOARD;
			input[i].ki.wVk = VK_RETURN;
			SendInput(i + 1, input, sizeof(INPUT));
			free(input);
		}
	}
	if (hogmutex != NULL)
		Sleep(200);	// Need to add a delay, otherwise we may get some printout before the hogger
	return hogmutex;
}


/*
 * Application Entrypoint
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	const char* rufus_loc = "rufus.loc";
	int i, opt, option_index = 0, argc = 0, si = 0, lcid = GetUserDefaultUILanguage();
	int wait_for_mutex = 0;
	FILE* fd;
	BOOL attached_console = FALSE, external_loc_file = FALSE, lgp_set = FALSE, automount = TRUE;
	BOOL disable_hogger = FALSE, previous_enable_HDDs = FALSE, vc = IsRegistryNode(REGKEY_HKCU, vs_reg);
	BYTE *loc_data;
	DWORD loc_size, size;
	char tmp_path[MAX_PATH] = "", loc_file[MAX_PATH] = "", ini_path[MAX_PATH] = "", ini_flags[] = "rb";
	char *tmp, *locale_name = NULL, **argv = NULL;
	wchar_t **wenv, **wargv;
	PF_TYPE_DECL(CDECL, int, __wgetmainargs, (int*, wchar_t***, wchar_t***, int, int*));
	PF_TYPE_DECL(WINAPI, BOOL, ChangeWindowMessageFilter, (UINT message, DWORD dwFlag));
	HANDLE mutex = NULL, hogmutex = NULL, hFile = NULL;
	HWND hDlg = NULL;
	HDC hDC;
	MSG msg;
	struct option long_options[] = {
		{"fixed",   no_argument,       NULL, 'f'},
		{"gui",     no_argument,       NULL, 'g'},
		{"help",    no_argument,       NULL, 'h'},
		{"iso",     required_argument, NULL, 'i'},
		{"locale",  required_argument, NULL, 'l'},
		{"wait",    required_argument, NULL, 'w'},
		{0, 0, NULL, 0}
	};

	// Disable loading system DLLs from the current directory (sideloading mitigation)
	SetDllDirectoryA("");

	uprintf("*** " APPLICATION_NAME " init ***\n");
	PF_INIT(GetTickCount64, kernel32);

	// Reattach the console, if we were started from commandline
	if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
		attached_console = TRUE;
		IGNORE_RETVAL(freopen("CONIN$", "r", stdin));
		IGNORE_RETVAL(freopen("CONOUT$", "w", stdout));
		IGNORE_RETVAL(freopen("CONOUT$", "w", stderr));
		_flushall();
	}

	// We have to process the arguments before we acquire the lock and process the locale
	PF_INIT(__wgetmainargs, Msvcrt);
	if (pf__wgetmainargs != NULL) {
		pf__wgetmainargs(&argc, &wargv, &wenv, 1, &si);
		argv = (char**)calloc(argc, sizeof(char*));
		if (argv != NULL) {
			// Non getopt parameter check
			for (i = 0; i < argc; i++) {
				argv[i] = wchar_to_utf8(wargv[i]);
				// Check for " /W" (wait for mutex release for pre 1.3.3 versions)
				if (strcmp(argv[i], "/W") == 0)
					wait_for_mutex = 150;	// Try to acquire the mutex for 15 seconds
				// We need to find if we need to disable the hogger BEFORE we start
				// processing arguments with getopt, as we may want to print messages
				// on the commandline then, which the hogger makes more intuitive.
				if ((strcmp(argv[i], "-g") == 0) || (strcmp(argv[i], "--gui") == 0))
					disable_hogger = TRUE;
			}
			// If our application name contains a 'p' (for "portable") create a 'rufus.ini'
			// NB: argv[0] is populated in the previous loop
			tmp = &argv[0][strlen(argv[0]) - 1];
			while ((((uintptr_t)tmp) > ((uintptr_t)argv[0])) && (*tmp != '\\'))
				tmp--;
			if ((strchr(tmp, 'p') != NULL) || (strchr(tmp, 'P') != NULL))
				ini_flags[0] = 'a';

			// Now enable the hogger before processing the rest of the arguments
			hogmutex = SetHogger(attached_console, disable_hogger);

			while ((opt = getopt_long(argc, argv, "?fghi:w:l:", long_options, &option_index)) != EOF) {
				switch (opt) {
				case 'f':
					enable_HDDs = TRUE;
					break;
				case 'g':
					// No need to reprocess that option
					break;
				case 'i':
					if (_access(optarg, 0) != -1) {
						safe_free(image_path);
						image_path = safe_strdup(optarg);
						iso_provided = TRUE;
					}
					else {
						printf("Could not find ISO image '%s'\n", optarg);
					}
					break;
				case 'l':
					if (isdigitU(optarg[0])) {
						lcid = (int)strtol(optarg, NULL, 0);
					}
					else {
						safe_free(locale_name);
						locale_name = safe_strdup(optarg);
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
			}
		}
	} else {
		uprintf("Could not access UTF-16 args");
	}

	// Retrieve various app & system directories
	if (GetCurrentDirectoryU(sizeof(app_dir), app_dir) == 0) {
		uprintf("Could not get current directory: %s", WindowsErrorString());
		app_dir[0] = 0;
	}
	if (GetSystemDirectoryU(system_dir, sizeof(system_dir)) == 0) {
		uprintf("Could not get system directory: %s", WindowsErrorString());
		static_strcpy(system_dir, "C:\\Windows\\System32");
	}
	if (GetTempPathU(sizeof(temp_dir), temp_dir) == 0) {
		uprintf("Could not get temp directory: %s", WindowsErrorString());
		static_strcpy(temp_dir, ".\\");
	}
	// Construct Sysnative ourselves as there is no GetSysnativeDirectory() call
	// By default (64bit app running on 64 bit OS or 32 bit app running on 32 bit OS)
	// Sysnative and System32 are the same
	static_strcpy(sysnative_dir, system_dir);
	// But if the app is 32 bit and the OS is 64 bit, Sysnative must differ from System32
#if (!defined(_WIN64) && !defined(BUILD64))
	if (is_x64()) {
		if (GetSystemWindowsDirectoryU(sysnative_dir, sizeof(sysnative_dir)) == 0) {
			uprintf("Could not get Windows directory: %s", WindowsErrorString());
			static_strcpy(sysnative_dir, "C:\\Windows");
		}
		static_strcat(sysnative_dir, "\\Sysnative");
	}
#endif

	// Look for a .ini file in the current app directory
	static_sprintf(ini_path, "%s\\rufus.ini", app_dir);
	fd = fopenU(ini_path, ini_flags);	// Will create the file if portable mode is requested
	vc |= (safe_strcmp(GetSignatureName(NULL), cert_name[0]) == 0);
	if (fd != NULL) {
		ini_file = ini_path;
		fclose(fd);
	}
	uprintf("Will use settings from %s", (ini_file != NULL)?"INI file":"registry");

	// Use the locale specified by the settings, if any
	tmp = ReadSettingStr(SETTING_LOCALE);
	if (tmp[0] != 0) {
		locale_name = safe_strdup(tmp);
		uprintf("found locale '%s'", locale_name);
	}

	// Restore user-saved settings
	advanced_mode = ReadSettingBool(SETTING_ADVANCED_MODE);
	preserve_timestamps = ReadSettingBool(SETTING_PRESERVE_TIMESTAMPS);
	use_fake_units = !ReadSettingBool(SETTING_USE_PROPER_SIZE_UNITS);
	usb_debug = ReadSettingBool(SETTING_ENABLE_USB_DEBUG);
	detect_fakes = !ReadSettingBool(SETTING_DISABLE_FAKE_DRIVES_CHECK);
	allow_dual_uefi_bios = ReadSettingBool(SETTING_ENABLE_WIN_DUAL_EFI_BIOS);
	force_large_fat32 = ReadSettingBool(SETTING_FORCE_LARGE_FAT32_FORMAT);
	enable_vmdk = ReadSettingBool(SETTING_ENABLE_VMDK_DETECTION);
	disable_file_indexing = ReadSettingBool(SETTING_DISABLE_FILE_INDEXING);

	// Initialize the global scaling, in case we need it before we initialize the dialog
	hDC = GetDC(NULL);
	fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
	if (hDC != NULL)
		ReleaseDC(NULL, hDC);

	// Init localization
	init_localization();

	// Seek for a loc file in the current directory
	if (GetFileAttributesU(rufus_loc) == INVALID_FILE_ATTRIBUTES) {
		uprintf("loc file not found in current directory - embedded one will be used");

		loc_data = (BYTE*)GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_LC_RUFUS_LOC), _RT_RCDATA, "embedded.loc", &loc_size, FALSE);
		if ( (GetTempFileNameU(temp_dir, APPLICATION_NAME, 0, loc_file) == 0) || (loc_file[0] == 0) ) {
			// Last ditch effort to get a loc file - just extract it to the current directory
			static_strcpy(loc_file, rufus_loc);
		}

		hFile = CreateFileU(loc_file, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if ((hFile == INVALID_HANDLE_VALUE) || (!WriteFileWithRetry(hFile, loc_data, loc_size, &size, WRITE_RETRIES))) {
			uprintf("localization: unable to extract '%s': %s", loc_file, WindowsErrorString());
			safe_closehandle(hFile);
			goto out;
		}
		uprintf("localization: extracted data to '%s'", loc_file);
		safe_closehandle(hFile);
	} else {
		static_sprintf(loc_file, "%s\\%s", app_dir, rufus_loc);
		external_loc_file = TRUE;
		uprintf("using external loc file '%s'", loc_file);
	}

	if ( (!get_supported_locales(loc_file))
	  || ((selected_locale = ((locale_name == NULL)?get_locale_from_lcid(lcid, TRUE):get_locale_from_name(locale_name, TRUE))) == NULL) ) {
		uprintf("FATAL: Could not access locale!");
		MessageBoxA(NULL, "The locale data is missing or invalid. This application will now exit.",
			"Fatal error", MB_ICONSTOP|MB_SYSTEMMODAL);
		goto out;
	}
	selected_langid = get_language_id(selected_locale);

	// This is needed as there appears to be a *FLAW* in Windows allowing the app to run unelevated with some
	// weirdly configured user accounts, even as we explicitly set 'requireAdministrator' in the manifest...
	if (!IsCurrentProcessElevated()) {
		uprintf("FATAL: No administrative privileges!");
		// Load the translation before we print the error
		get_loc_data_file(loc_file, selected_locale);
		right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT);
		MessageBoxExU(NULL, lmprintf(MSG_289), lmprintf(MSG_288), MB_ICONSTOP | MB_IS_RTL | MB_SYSTEMMODAL, selected_langid);
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
		right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT);
		// Set MB_SYSTEMMODAL to prevent Far Manager from stealing focus...
		MessageBoxExU(NULL, lmprintf(MSG_002), lmprintf(MSG_001), MB_ICONSTOP|MB_IS_RTL|MB_SYSTEMMODAL, selected_langid);
		goto out;
	}

	// Save instance of the application for further reference
	hMainInstance = hInstance;

	// Initialize COM for folder selection
	IGNORE_RETVAL(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

	// Some dialogs have Rich Edit controls and won't display without this
	if (GetLibraryHandle("Riched20") == NULL) {
		uprintf("Could not load RichEdit library - some dialogs may not display: %s\n", WindowsErrorString());
	}

	// Set the Windows version
	GetWindowsVersion();

	// Increase the application privileges (SE_DEBUG_PRIVILEGE), so that we can report
	// the Windows Services preventing access to the disk or volume we want to format.
	EnablePrivileges();

	// We use local group policies rather than direct registry manipulation
	// 0x9e disables removable and fixed drive notifications
	lgp_set = SetLGP(FALSE, &existing_key, ep_reg, "NoDriveTypeAutorun", 0x9e);

	if (nWindowsVersion > WINDOWS_XP) {
		// Re-enable AutoMount if needed
		if (!GetAutoMount(&automount)) {
			uprintf("Could not get AutoMount status");
			automount = TRUE;	// So that we don't try to change its status on exit
		} else if (!automount) {
			uprintf("AutoMount was detected as disabled - temporarily re-enabling it");
			if (!SetAutoMount(TRUE))
				uprintf("Failed to enable AutoMount");
		}
	}

relaunch:
	ubprintf("Localization set to '%s'", selected_locale->txt[0]);
	right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT);
	SetProcessDefaultLayout(right_to_left_mode?LAYOUT_RTL:0);
	if (get_loc_data_file(loc_file, selected_locale))
		WriteSettingStr(SETTING_LOCALE, selected_locale->txt[0]);

	if (!vc) {
		if (MessageBoxExU(NULL, lmprintf(MSG_296), lmprintf(MSG_295),
			MB_YESNO | MB_ICONWARNING | MB_IS_RTL | MB_SYSTEMMODAL, selected_langid) != IDYES)
			goto out;
		vc = TRUE;
	}

	/*
	 * Create the main Window
	 */
	hDlg = MyCreateDialog(hInstance, IDD_DIALOG, NULL, MainCallback);
	if (hDlg == NULL) {
		MessageBoxExU(NULL, "Could not create Window", "DialogBox failure",
			MB_ICONSTOP|MB_IS_RTL|MB_SYSTEMMODAL, selected_langid);
		goto out;
	}
	if ((relaunch_rc.left > -65536) && (relaunch_rc.top > -65536))
		SetWindowPos(hDlg, HWND_TOP, relaunch_rc.left, relaunch_rc.top, 0, 0, SWP_NOSIZE);

	// Enable drag-n-drop through the message filter (for Vista or later)
	if (nWindowsVersion >= WINDOWS_VISTA) {
		PF_INIT(ChangeWindowMessageFilter, user32);
		if (pfChangeWindowMessageFilter != NULL) {
			// NB: We use ChangeWindowMessageFilter() here because
			// ChangeWindowMessageFilterEx() is not available on Vista
			pfChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
			pfChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
			// CopyGlobalData is needed sine we are running elevated
			pfChangeWindowMessageFilter(WM_COPYGLOBALDATA, MSGFLT_ADD);
		}
	}

	// Set the hook to automatically close Windows' "You need to format the disk in drive..." prompt
	if (!SetFormatPromptHook())
		uprintf("Warning: Could not set 'Format Disk' prompt auto-close");

	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing and process "magic" commands
	while(GetMessage(&msg, NULL, 0, 0)) {
		// ** *****  **** ** ******** *
		// .,ABCDEFGHIJKLMNOPQRSTUVWXYZ

		// Ctrl-A => Select the log data
		if ( (IsWindowVisible(hLogDlg)) && (GetKeyState(VK_CONTROL) & 0x8000) &&
			(msg.message == WM_KEYDOWN) && (msg.wParam == 'A') ) {
			// Might also need ES_NOHIDESEL property if you want to select when not active
			Edit_SetSel(hLog, 0, -1);
		}
		// Alt-. => Enable USB enumeration debug
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == VK_OEM_PERIOD)) {
			usb_debug = !usb_debug;
			WriteSettingBool(SETTING_ENABLE_USB_DEBUG, usb_debug);
			PrintStatusTimeout(lmprintf(MSG_270), usb_debug);
			GetDevices(0);
			continue;
		}
		// Alt-, => Disable physical drive locking
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == VK_OEM_COMMA)) {
			lock_drive = !lock_drive;
			PrintStatusTimeout(lmprintf(MSG_282), lock_drive);
			continue;
		}
		// Alt-B => Toggle fake drive detection during bad blocks check
		// By default, Rufus will check for fake USB flash drives that mistakenly present
		// more capacity than they already have by looping over the flash. This check which
		// is enabled by default is performed by writing the block number sequence and reading
		// it back during the bad block check.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'B')) {
			detect_fakes = !detect_fakes;
			WriteSettingBool(SETTING_DISABLE_FAKE_DRIVES_CHECK, !detect_fakes);
			PrintStatusTimeout(lmprintf(MSG_256), detect_fakes);
			continue;
		}
		// Alt C => Force the update check to be successful
		// This will set the reported current version of Rufus to 0.0.0.0 when performing an update
		// check, so that it always succeeds. This is useful for translators.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'C')) {
			force_update = !force_update;
			PrintStatusTimeout(lmprintf(MSG_259), force_update);
			continue;
		}
		// Alt-D => Delete the NoDriveTypeAutorun key on exit (useful if the app crashed)
		// This key is used to disable Windows popup messages when an USB drive is plugged in.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'D')) {
			PrintStatus(2000, MSG_255);
			existing_key = FALSE;
			continue;
		}
		// Alt-E => Enhanced installation mode (allow dual UEFI/BIOS mode and FAT32 for Windows)
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'E')) {
			allow_dual_uefi_bios = !allow_dual_uefi_bios;
			WriteSettingBool(SETTING_ENABLE_WIN_DUAL_EFI_BIOS, allow_dual_uefi_bios);
			PrintStatusTimeout(lmprintf(MSG_266), allow_dual_uefi_bios);
			SetMBRForUEFI(TRUE);
			continue;
		}
		// Alt-F => Toggle detection of USB HDDs
		// By default Rufus does not list USB HDDs. This is a safety feature aimed at avoiding
		// unintentional formatting of backup drives instead of USB keys.
		// When enabled, Rufus will list and allow the formatting of USB HDDs.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'F')) {
			enable_HDDs = !enable_HDDs;
			PrintStatusTimeout(lmprintf(MSG_253), enable_HDDs);
			GetDevices(0);
			CheckDlgButton(hMainDialog, IDC_ENABLE_FIXED_DISKS, enable_HDDs?BST_CHECKED:BST_UNCHECKED);
			continue;
		}
		// Alt-I => Toggle ISO support
		// This is useful if you have an ISOHybrid image and you want to force Rufus to use
		// DD-mode when writing the data.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'I')) {
			enable_iso = !enable_iso;
			PrintStatusTimeout(lmprintf(MSG_262), enable_iso);
			if (image_path != NULL) {
				iso_provided = TRUE;
				dont_display_image_name = TRUE;
				SendMessage(hDlg, WM_COMMAND, IDC_SELECT_ISO, 0);
			}
			continue;
		}
		// Alt J => Toggle Joliet support for ISO9660 images
		// Some ISOs (Ubuntu) have Joliet extensions but expect applications not to use them,
		// due to their reliance on filenames that are > 64 chars (the Joliet max length for
		// a file name). This option allows users to ignore Joliet when using such images.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'J')) {
			enable_joliet = !enable_joliet;
			PrintStatusTimeout(lmprintf(MSG_257), enable_joliet);
			continue;
		}
		// Alt K => Toggle Rock Ridge support for ISO9660 images
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'K')) {
			enable_rockridge = !enable_rockridge;
			PrintStatusTimeout(lmprintf(MSG_258), enable_rockridge);
			continue;
		}
		// Alt-L => Force Large FAT32 format to be used on < 32 GB drives
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'L')) {
			force_large_fat32 = !force_large_fat32;
			WriteSettingBool(SETTING_FORCE_LARGE_FAT32_FORMAT, force_large_fat32);
			PrintStatusTimeout(lmprintf(MSG_254), force_large_fat32);
			GetDevices(0);
			continue;
		}
		// Alt N => Enable NTFS compression
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'N')) {
			enable_ntfs_compression = !enable_ntfs_compression;
			PrintStatusTimeout(lmprintf(MSG_260), enable_ntfs_compression);
			continue;
		}
		// Alt-O => Save from Optical drive to ISO
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'O')) {
			SaveISO();
			continue;
		}
		// Alt-Q => Disable file indexing (for file systems that support it)
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Q')) {
			disable_file_indexing = !disable_file_indexing;
			WriteSettingBool(SETTING_DISABLE_FILE_INDEXING, disable_file_indexing);
			PrintStatusTimeout(lmprintf(MSG_290), !disable_file_indexing);
			continue;
		}

		// Alt-R => Remove all the registry keys that may have been created by Rufus
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'R')) {
			PrintStatus(2000, DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME "\\" APPLICATION_NAME)?MSG_248:MSG_249);
			// Also try to delete the upper key (company name) if it's empty (don't care about the result)
			DeleteRegistryKey(REGKEY_HKCU, COMPANY_NAME);
			continue;
		}
		// Alt-S => Disable size limit for ISOs
		// By default, Rufus will not copy ISOs that are larger than in size than
		// the target USB drive. If this is enabled, the size check is disabled.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'S')) {
			size_check = !size_check;
			PrintStatusTimeout(lmprintf(MSG_252), size_check);
			GetDevices(0);
			continue;
		}
		// Alt-T => Preserve timestamps when extracting ISO files
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'T')) {
			preserve_timestamps = !preserve_timestamps;
			WriteSettingBool(SETTING_PRESERVE_TIMESTAMPS, preserve_timestamps);
			PrintStatusTimeout(lmprintf(MSG_269), preserve_timestamps);
			continue;
		}
		// Alt-U => Use PROPER size units, instead of this whole Kibi/Gibi nonsense
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'U')) {
			use_fake_units = !use_fake_units;
			WriteSettingBool(SETTING_USE_PROPER_SIZE_UNITS, !use_fake_units);
			PrintStatusTimeout(lmprintf(MSG_263), !use_fake_units);
			GetDevices(0);
			continue;
		}
		// Alt-V => Save selected device to *UNCOMPRESSED* VHD
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'V')) {
			SaveVHD();
			continue;
		}
		// Alt-W => Enable VMWare disk detection
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'W')) {
			enable_vmdk = !enable_vmdk;
			WriteSettingBool(SETTING_ENABLE_VMDK_DETECTION, enable_vmdk);
			PrintStatusTimeout(lmprintf(MSG_265), enable_vmdk);
			GetDevices(0);
			continue;
		}
		// Alt-X => Delete the 'rufus_files' subdirectory
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'X')) {
			static_sprintf(tmp_path, "%s\\%s", app_dir, FILES_DIR);
			PrintStatus(2000, MSG_264, tmp_path);
			SHDeleteDirectoryExU(NULL, tmp_path, FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION);
			continue;
		}
		// Alt-Z => Zero the drive
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Z')) {
			zero_drive = TRUE;
			// Simulate a button click for Start
			PostMessage(hDlg, WM_COMMAND, (WPARAM)IDC_START, 0);
		}

		// Hazardous cheat modes require Ctrl + Alt
		// Ctrl-Alt-F => List non USB removable drives such as eSATA, etc - CAUTION!!!
		if ((msg.message == WM_KEYDOWN) && (msg.wParam == 'F') &&
			(GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000)) {
			list_non_usb_removable_drives = !list_non_usb_removable_drives;
			if (list_non_usb_removable_drives) {
				previous_enable_HDDs = enable_HDDs;
				enable_HDDs = TRUE;
			} else {
				enable_HDDs = previous_enable_HDDs;
			}
			CheckDlgButton(hMainDialog, IDC_ENABLE_FIXED_DISKS, enable_HDDs ? BST_CHECKED : BST_UNCHECKED);
			PrintStatusTimeout(lmprintf(MSG_287), list_non_usb_removable_drives);
			uprintf("%sListing of non-USB removable drives %s",
				(list_non_usb_removable_drives)?"CAUTION: ":"", (list_non_usb_removable_drives)?"enabled":"disabled");
			if (list_non_usb_removable_drives)
				uprintf("By using this unofficial cheat mode you forfeit ANY RIGHT to complain if you lose valuable data!");
			GetDevices(0);
			continue;
		}

		// Let the system handle dialog messages (e.g. those from the tab key)
		if (!IsDialogMessage(hDlg, &msg) && !IsDialogMessage(hLogDlg, &msg)) {
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
	// Destroy the hogger mutex first, so that the cmdline app can exit and we can delete it
	if (attached_console && !disable_hogger) {
		ReleaseMutex(hogmutex);
		safe_closehandle(hogmutex);
	}
	if ((!external_loc_file) && (loc_file[0] != 0))
		DeleteFileU(loc_file);
	DestroyAllTooltips();
	ClrFormatPromptHook();
	exit_localization();
	safe_free(image_path);
	safe_free(locale_name);
	safe_free(update.download_url);
	safe_free(update.release_notes);
	safe_free(grub2_buf);
	if (argv != NULL) {
		for (i=0; i<argc; i++) safe_free(argv[i]);
		safe_free(argv);
	}
	if (lgp_set)
		SetLGP(TRUE, &existing_key, ep_reg, "NoDriveTypeAutorun", 0);
	if ((nWindowsVersion > WINDOWS_XP) && (!automount) && (!SetAutoMount(FALSE)))
		uprintf("Failed to restore AutoMount to disabled");
	ubflush();
	// Unconditional delete with retry, just in case...
	for (i = 0; (!DeleteFileA(cmdline_hogger)) && (i <= 10); i++)
		Sleep(200);
	CloseHandle(mutex);
	CLOSE_OPENED_LIBRARIES;
	if (attached_console) {
		SetWindowPos(GetConsoleWindow(), HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		FreeConsole();
	}
	uprintf("*** " APPLICATION_NAME " exit ***\n");
#ifdef _CRTDBG_MAP_ALLOC
	_CrtDumpMemoryLeaks();
#endif

	return 0;
}
