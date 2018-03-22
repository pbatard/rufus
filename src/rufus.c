/*
 * Rufus: The Reliable USB Formatting Utility
 * Copyright © 2011-2018 Pete Batard <pete@akeo.ie>
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

#include "ui.h"
#include "drive.h"
#include "settings.h"
#include "bled/bled.h"
#include "../res/grub/grub_version.h"
#include "../res/grub2/grub2_version.h"

static const char* cmdline_hogger = "rufus.com";
static const char* FileSystemLabel[FS_MAX] = { "FAT", "FAT32", "NTFS", "UDF", "exFAT", "ReFS" };
static const char* ep_reg = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer";
static const char* vs_reg = "Software\\Microsoft\\VisualStudio";
// Number of steps for each FS for FCC_STRUCTURE_PROGRESS
const int nb_steps[FS_MAX] = { 5, 5, 12, 1, 10 };
static BOOL existing_key = FALSE;	// For LGP set/restore
static BOOL size_check = TRUE;
static BOOL log_displayed = FALSE;
static BOOL iso_provided = FALSE;
static BOOL user_notified = FALSE;
static BOOL relaunch = FALSE;
static BOOL dont_display_image_name = FALSE;
static BOOL user_changed_label = FALSE;
static BOOL app_changed_label = FALSE;
static BOOL allowed_filesystem[FS_MAX] = { 0 };
static int64_t last_iso_blocking_status;
static int selection_default, row_height, advanced_device_section_height, advanced_format_section_height, image_index;
static int device_vpos, format_vpos, status_vpos;
static int ddh, bw, hw, fw;	// DropDown Height, Main button width, half dropdown width, full dropdown width
static int sw, mw, bsw, hbw, sbw, ssw, tw, dbw;
static UINT_PTR UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
static RECT relaunch_rc = { -65536, -65536, 0, 0};
static UINT uQFChecked = BST_CHECKED, uMBRChecked = BST_UNCHECKED;
static HFONT hInfoFont;
static WNDPROC progress_original_proc = NULL;
static HANDLE format_thid = NULL, dialog_handle = NULL;
static HWND hSelectImage = NULL, hStart = NULL;
static HICON hIconSave, hIconDown, hIconUp, hIconLang, hIconAbout, hIconSettings, hIconLog;
static char szTimer[12] = "00:00:00";
static wchar_t wtbtext[2][128];
static unsigned int timer;
static char uppercase_select[64], uppercase_start[64], uppercase_close[64], uppercase_cancel[64];

extern BOOL enable_iso, enable_joliet, enable_rockridge, enable_ntfs_compression;
extern uint8_t* grub2_buf;
extern long grub2_len;
extern char* szStatusMessage;
extern const char* old_c32_name[NB_OLD_C32];
extern const char* cert_name[3];
extern const char* sfd_name;

/*
 * Globals
 */
OPENED_LIBRARIES_VARS;
HINSTANCE hMainInstance;
HWND hMainDialog, hMultiToolbar, hAdvancedDeviceToolbar, hAdvancedFormatToolbar, hUpdatesDlg = NULL;
HIMAGELIST hUpImageList, hDownImageList;
BUTTON_IMAGELIST bi_iso = { 0 }, bi_up = { 0 }, bi_down = { 0 }, bi_save = { 0 };
char szFolderPath[MAX_PATH], app_dir[MAX_PATH], system_dir[MAX_PATH], temp_dir[MAX_PATH], sysnative_dir[MAX_PATH];
char *image_path = NULL, *short_image_path;
float fScale = 1.0f;
int default_fs, fs, bt, pt, tt;
int cbw, ddw, ddbh = 0, bh = 0; // (empty) check box width, (empty) drop down width, button height (for and without dropdown match)
uint32_t dur_mins, dur_secs;
loc_cmd* selected_locale = NULL;
WORD selected_langid = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
DWORD MainThreadId;
HWND hDeviceList, hPartitionScheme, hTargetSystem, hFileSystem, hClusterSize, hLabel, hBootType, hNBPasses, hLog = NULL;
HWND hLogDialog = NULL, hProgress = NULL, hDiskID;
BOOL use_own_c32[NB_OLD_C32] = {FALSE, FALSE}, mbr_selected_by_user = FALSE, display_togo_option = FALSE;
BOOL iso_op_in_progress = FALSE, format_op_in_progress = FALSE, right_to_left_mode = FALSE, progress_in_use = FALSE, has_uefi_csm;
BOOL enable_HDDs = FALSE, force_update = FALSE, enable_ntfs_compression = FALSE, no_confirmation_on_cancel = FALSE, lock_drive = TRUE;
BOOL advanced_mode_device, advanced_mode_format, allow_dual_uefi_bios, detect_fakes, enable_vmdk, force_large_fat32, usb_debug, use_fake_units, preserve_timestamps;
BOOL zero_drive = FALSE, list_non_usb_removable_drives = FALSE, disable_file_indexing, large_drive = FALSE, write_as_image = FALSE;
int dialog_showing = 0;
uint16_t rufus_version[3], embedded_sl_version[2];
char embedded_sl_version_str[2][12] = { "?.??", "?.??" };
char embedded_sl_version_ext[2][32];
char ClusterSizeLabel[MAX_CLUSTER_SIZES][64];
char msgbox[1024], msgbox_title[32], *ini_file = NULL;
RUFUS_UPDATE update = { {0,0,0}, {0,0}, NULL, NULL};
StrArray DriveID, DriveLabel, DriveHub, BlockingProcess, ImageList;
uint32_t DrivePort[MAX_DRIVES];

static void ToggleImageOption(void);

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
static void SetComboEntry(HWND hDlg, int data) {
	int i;
	for (i = 0; i < ComboBox_GetCount(hDlg); i++) {
		if (ComboBox_GetItemData(hDlg, i) == data) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hDlg, i));
			break;
		}
	}
	if (i == ComboBox_GetCount(hDlg))
		IGNORE_RETVAL(ComboBox_SetCurSel(hDlg, 0));
}

// Fill in the cluster size names
static void SetClusterSizeLabels(void)
{
	unsigned int i, j, msg_id;
	safe_sprintf(ClusterSizeLabel[0], 64, lmprintf(MSG_029));
	for (i=512, j=1, msg_id=MSG_026; j<MAX_CLUSTER_SIZES; i<<=1, j++) {
		if (i > 8192) {
			i /= 1024;
			msg_id++;
		}
		safe_sprintf(ClusterSizeLabel[j], 64, "%d %s", i, lmprintf(msg_id));
	}
}

static void SetAllowedFileSystems(void)
{
	int i;

	memset(allowed_filesystem, 0, sizeof(allowed_filesystem));
	// Nothing is allowed if we don't have a drive
	if (ComboBox_GetCurSel(hDeviceList) < 0)
		return;
	switch (selection_default) {
	case BT_NON_BOOTABLE:
		for (i = 0; i < FS_MAX; i++)
			allowed_filesystem[i] = TRUE;
		break;
	case BT_MSDOS:
	case BT_FREEDOS:
		allowed_filesystem[FS_FAT16] = TRUE;
		allowed_filesystem[FS_FAT32] = TRUE;
		break;
	case BT_IMAGE:
		if (!HAS_WINDOWS(img_report) || (tt != TT_BIOS) || allow_dual_uefi_bios) {
			if (!HAS_WINTOGO(img_report) || (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) != 1)) {
				allowed_filesystem[FS_FAT16] = TRUE;
				allowed_filesystem[FS_FAT32] = TRUE;
			}
		}
		allowed_filesystem[FS_NTFS] = TRUE;
		break;
	case BT_SYSLINUX_V6:
		allowed_filesystem[FS_NTFS] = TRUE;
		// Fall through
	case BT_SYSLINUX_V4:
	case BT_REACTOS:
	case BT_GRUB4DOS:
	case BT_GRUB2:
		allowed_filesystem[FS_FAT16] = TRUE;
		allowed_filesystem[FS_FAT32] = TRUE;
		break;
	case BT_UEFI_NTFS:
		allowed_filesystem[FS_NTFS] = TRUE;
		break;
	}

	// Reset disk ID to 0x80 if Rufus MBR is used
	if (selection_default != BT_IMAGE) {
		IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
	}
}

// Populate the Boot selection dropdown
static void SetBootOptions(void)
{
	char tmp[32];

	IGNORE_RETVAL(ComboBox_ResetContent(hBootType));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, lmprintf(MSG_279)), BT_NON_BOOTABLE));
	if (nWindowsVersion < WINDOWS_10)	// The diskcopy.dll along with its MS-DOS floppy image was removed in Windows 10
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "MS-DOS"), BT_MSDOS));
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "FreeDOS"), BT_FREEDOS));
	image_index = (nWindowsVersion < WINDOWS_10) ? 3 : 2;
	IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType,
		(image_path == NULL) ? lmprintf(MSG_281, lmprintf(MSG_280)) : short_image_path), BT_IMAGE));

	if (advanced_mode_device) {
		static_sprintf(tmp, "Syslinux %s", embedded_sl_version_str[0]);
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, tmp), BT_SYSLINUX_V4));
		static_sprintf(tmp, "Syslinux %s", embedded_sl_version_str[1]);
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, tmp), BT_SYSLINUX_V6));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "ReactOS"), BT_REACTOS));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType,
			"Grub " GRUB2_PACKAGE_VERSION), BT_GRUB2));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType,
			"Grub4DOS " GRUB4DOS_VERSION), BT_GRUB4DOS));
		IGNORE_RETVAL(ComboBox_SetItemData(hBootType, ComboBox_AddStringU(hBootType, "UEFI:NTFS"), BT_UEFI_NTFS));
	}
	if ((!advanced_mode_device) && (selection_default >= BT_SYSLINUX_V4)) {
		selection_default = BT_IMAGE;
		CheckDlgButton(hMainDialog, IDC_DISK_ID, BST_UNCHECKED);
	}
	SetComboEntry(hBootType, selection_default);
}

static void SetPartitionSchemeAndTargetSystem(BOOL only_target)
{
	//                                   MBR,  GPT,  SFD
	BOOL allowed_partition_scheme[3] = { TRUE, TRUE, FALSE };
	//                                   BIOS, UEFI, DUAL
	BOOL allowed_target_system[3]    = { TRUE, TRUE, FALSE };
	BOOL is_windows_to_go_selected;

	if (!only_target)
		IGNORE_RETVAL(ComboBox_ResetContent(hPartitionScheme));
	IGNORE_RETVAL(ComboBox_ResetContent(hTargetSystem));

	bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	is_windows_to_go_selected = (bt == BT_IMAGE) && (image_path != NULL) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1);
	// If no device is selected, don't populate anything
	if (ComboBox_GetCurSel(hDeviceList) < 0)
		return;
	switch (bt) {
	case BT_NON_BOOTABLE:
		allowed_partition_scheme[PARTITION_STYLE_SFD] = TRUE;
		allowed_target_system[0] = FALSE;
		allowed_target_system[1] = FALSE;
		allowed_target_system[2] = TRUE;
		break;
	case BT_IMAGE:
		if (image_path == NULL)
			break;
		// Check if image is EFI bootable
		if (!IS_EFI_BOOTABLE(img_report)) {
			allowed_partition_scheme[PARTITION_STYLE_GPT] = FALSE;
			allowed_target_system[1] = FALSE;
			break;
		}
		// Image is EFI bootable => set dual BIOS + UEFI and so on...
		if (IS_BIOS_BOOTABLE(img_report)) {
			if (!HAS_WINDOWS(img_report) || allow_dual_uefi_bios || is_windows_to_go_selected) {
				allowed_target_system[0] = FALSE;
				allowed_target_system[1] = TRUE;
				allowed_target_system[2] = TRUE;
			}
		} else {
			allowed_target_system[0] = FALSE;
		}
		break;
	case BT_MSDOS:
	case BT_FREEDOS:
	case BT_SYSLINUX_V4:
	case BT_SYSLINUX_V6:
	case BT_REACTOS:
	case BT_GRUB4DOS:
	case BT_GRUB2:
		allowed_partition_scheme[PARTITION_STYLE_GPT] = FALSE;
		allowed_target_system[1] = FALSE;
		break;
	case BT_UEFI_NTFS:
		allowed_target_system[0] = FALSE;
		break;
	}

	if (!only_target) {
		if (allowed_partition_scheme[PARTITION_STYLE_MBR]) 
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, "MBR"), PARTITION_STYLE_MBR));
		if (allowed_partition_scheme[PARTITION_STYLE_GPT])
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, "GPT"), PARTITION_STYLE_GPT));
		if (allowed_partition_scheme[PARTITION_STYLE_SFD])
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, sfd_name), PARTITION_STYLE_SFD));
		SetComboEntry(hPartitionScheme, PARTITION_STYLE_GPT);
		pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
	}

	has_uefi_csm = FALSE;
	if (allowed_target_system[0] && (pt != PARTITION_STYLE_GPT)) {
		IGNORE_RETVAL(ComboBox_SetItemData(hTargetSystem,
			ComboBox_AddStringU(hTargetSystem, lmprintf(MSG_031)), TT_BIOS));
		has_uefi_csm = TRUE;
	}
	if (allowed_target_system[1] && !((pt == PARTITION_STYLE_MBR) && IS_BIOS_BOOTABLE(img_report) && IS_EFI_BOOTABLE(img_report)) )
		IGNORE_RETVAL(ComboBox_SetItemData(hTargetSystem,
			ComboBox_AddStringU(hTargetSystem, lmprintf(MSG_032)), TT_UEFI));
	if (allowed_target_system[2] && ((pt != PARTITION_STYLE_GPT) || (bt == BT_NON_BOOTABLE)))
		IGNORE_RETVAL(ComboBox_SetItemData(hTargetSystem,
			ComboBox_AddStringU(hTargetSystem, lmprintf(MSG_033)), TT_BIOS));
	IGNORE_RETVAL(ComboBox_SetCurSel(hTargetSystem, 0));
	tt = (int)ComboBox_GetItemData(hTargetSystem, ComboBox_GetCurSel(hTargetSystem));
	// Can't update a tooltip from a thread, so we send a message instead
	SendMessage(hMainDialog, UM_UPDATE_CSM_TOOLTIP, 0, 0);
}

// Populate the Allocation unit size field
static BOOL SetClusterSizes(int FSType)
{
	char* szClustSize;
	int i, k, default_index = 0;
	ULONG j;

	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));

	if ((FSType < 0) || (FSType >= FS_MAX)) {
		return FALSE;
	}

	if ((SelectedDrive.ClusterSize[FSType].Allowed == 0)
		|| (SelectedDrive.ClusterSize[FSType].Default == 0)) {
		return FALSE;
	}

	for (i = 0, j = 0x100, k = 0; j<0x10000000; i++, j <<= 1) {
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

// Populate the File System and Cluster Size dropdowns
static BOOL SetFileSystemAndClusterSize(char* fs_type)
{
	int fs_index;
	LONGLONG i;
	char tmp[128] = "", *entry;

	IGNORE_RETVAL(ComboBox_ResetContent(hFileSystem));
	IGNORE_RETVAL(ComboBox_ResetContent(hClusterSize));
	default_fs = FS_UNKNOWN;
	memset(&SelectedDrive.ClusterSize, 0, sizeof(SelectedDrive.ClusterSize));

/*
 * See https://support.microsoft.com/en-gb/help/140365/default-cluster-size-for-ntfs--fat--and-exfat
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

		// exFAT
		SelectedDrive.ClusterSize[FS_EXFAT].Allowed = 0x03FFFE00;
		if (SelectedDrive.DiskSize < 256*MB)	// < 256 MB
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 4*KB;
		else if (SelectedDrive.DiskSize < 32*GB)	// < 32 GB
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 32*KB;
		else
			SelectedDrive.ClusterSize[FS_EXFAT].Default = 128*KB;

		// UDF
		SelectedDrive.ClusterSize[FS_UDF].Allowed = SINGLE_CLUSTERSIZE_DEFAULT;
		SelectedDrive.ClusterSize[FS_UDF].Default = 1;

		// ReFS (only supported for Windows 8.1 and later and for fixed disks)
		if (SelectedDrive.DiskSize >= 512*MB) {
			if ((nWindowsVersion >= WINDOWS_8_1) && (SelectedDrive.MediaType == FixedMedia)) {
				SelectedDrive.ClusterSize[FS_REFS].Allowed = SINGLE_CLUSTERSIZE_DEFAULT;
				SelectedDrive.ClusterSize[FS_REFS].Default = 1;
			}
		}
	}

	// Only add the filesystems we can service
	SetAllowedFileSystems();
	SetClusterSizeLabels();

	for (fs_index = 0; fs_index < FS_MAX; fs_index++) {
		// Remove all cluster sizes that are below the sector size
		if (SelectedDrive.ClusterSize[fs_index].Allowed != SINGLE_CLUSTERSIZE_DEFAULT) {
			SelectedDrive.ClusterSize[fs_index].Allowed &= ~(SelectedDrive.SectorSize - 1);
			if ((SelectedDrive.ClusterSize[fs_index].Default & SelectedDrive.ClusterSize[fs_index].Allowed) == 0)
				// We lost our default => Use rightmost bit to select the new one
				SelectedDrive.ClusterSize[fs_index].Default =
				SelectedDrive.ClusterSize[fs_index].Allowed & (-(LONG)SelectedDrive.ClusterSize[fs_index].Allowed);
		}

		if (SelectedDrive.ClusterSize[fs_index].Allowed != 0) {
			tmp[0] = 0;
			// Tell the user if we're going to use Large FAT32 or regular
			if ((fs_index == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32)))
				static_strcat(tmp, "Large ");
			static_strcat(tmp, FileSystemLabel[fs_index]);
			if (default_fs == FS_UNKNOWN) {
				entry = lmprintf(MSG_030, tmp);
				default_fs = fs_index;
			} else {
				entry = tmp;
			}
			if (allowed_filesystem[fs_index]) {
				IGNORE_RETVAL(ComboBox_SetItemData(hFileSystem,
					ComboBox_AddStringU(hFileSystem, entry), fs_index));
			}
		}
	}

	// re-select existing FS if it's one we know
	SelectedDrive.FSType = FS_UNKNOWN;
	if (safe_strlen(fs_type) != 0) {
		for (SelectedDrive.FSType = FS_MAX - 1; SelectedDrive.FSType >= 0; SelectedDrive.FSType--) {
			if (safe_strcmp(fs_type, FileSystemLabel[SelectedDrive.FSType]) == 0) {
				break;
			}
		}
	} else {
		SelectedDrive.FSType = FS_UNKNOWN;
	}

	for (i = 0; i<ComboBox_GetCount(hFileSystem); i++) {
		if (ComboBox_GetItemData(hFileSystem, i) == SelectedDrive.FSType) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
			break;
		}
	}

	if (i == ComboBox_GetCount(hFileSystem)) {
		// failed to reselect => pick default
		SetComboEntry(hFileSystem, default_fs);
	}

	return SetClusterSizes((int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)));
}

static void SetFSFromISO(void)
{
	int i, fs_tmp, selected_fs = FS_UNKNOWN;
	uint32_t fs_mask = 0;
	BOOL windows_to_go = display_togo_option && (bt == BT_IMAGE) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1);

	if (image_path == NULL)
		return;

	// Create a mask of all the FS's available
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs_tmp = (int)ComboBox_GetItemData(hFileSystem, i);
		fs_mask |= 1<<fs_tmp;
	}

	// The presence of a 4GB file forces the use of NTFS as default FS
	if (img_report.has_4GB_file) {
		if (fs_mask & (1 << FS_NTFS)) {
			selected_fs = FS_NTFS;
		}
	// Syslinux and EFI have precedence over bootmgr (unless the user selected BIOS as target type)
	} else if ((HAS_SYSLINUX(img_report)) || (HAS_REACTOS(img_report)) || HAS_KOLIBRIOS(img_report) ||
		(IS_EFI_BOOTABLE(img_report) && (tt == TT_UEFI) && (!windows_to_go))) {
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
		fs_tmp = (int)ComboBox_GetItemData(hFileSystem, i);
		if (fs_tmp == selected_fs)
			IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
	}

	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILE_SYSTEM,
		ComboBox_GetCurSel(hFileSystem));
}

static void SetMBRProps(void)
{
	BOOL needs_masquerading = HAS_WINPE(img_report) && (!img_report.uses_minint);

	if ((!mbr_selected_by_user) && ((image_path == NULL) || (bt != BT_IMAGE) || (fs != FS_NTFS) || HAS_GRUB(img_report) ||
		((display_togo_option) && (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1)) )) {
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
	if (((bt != BT_IMAGE) && (display_togo_option)) || ((bt == BT_IMAGE) && (HAS_WINTOGO(img_report)) && (!display_togo_option))) {
		ToggleImageOption();
	}
}

// This handles the enabling/disabling of the "Add fixes for old BIOSes" and "Use Rufus MBR" controls
static void EnableMBRBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	BOOL actual_enable_mbr = (bt > BT_IMAGE) ? FALSE: enable;
	BOOL actual_enable_fix = enable;
	static UINT uXPartChecked = BST_UNCHECKED;

	if ((pt != PARTITION_STYLE_MBR) || (tt != TT_BIOS) || ((bt == BT_IMAGE) && !IS_BIOS_BOOTABLE(img_report))) {
		// These options cannot apply if we aren't using MBR+BIOS, or are using an image that isn't BIOS bootable
		actual_enable_mbr = FALSE;
		actual_enable_fix = FALSE;
	} else {
		// If we are using an image, the Rufus MBR only applies if it's for Windows
		if ((bt == BT_IMAGE) && !HAS_WINPE(img_report) && !HAS_BOOTMGR(img_report)) {
			actual_enable_mbr = FALSE;
			mbr_selected_by_user = FALSE;
		}
		if (bt == BT_NON_BOOTABLE) {
			actual_enable_fix = FALSE;
		}
	}

	if (remove_checkboxes) {
		// Store/Restore the checkbox states
		if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && !actual_enable_mbr) {
			uMBRChecked = IsChecked(IDC_RUFUS_MBR);
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
			uXPartChecked = IsChecked(IDC_OLD_BIOS_FIXES);
			CheckDlgButton(hMainDialog, IDC_OLD_BIOS_FIXES, BST_UNCHECKED);
		} else if (!IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)) && actual_enable_mbr) {
			CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
			CheckDlgButton(hMainDialog, IDC_OLD_BIOS_FIXES, uXPartChecked);
		}
	}

	EnableWindow(GetDlgItem(hMainDialog, IDC_OLD_BIOS_FIXES), actual_enable_fix);
	EnableWindow(GetDlgItem(hMainDialog, IDC_RUFUS_MBR), actual_enable_mbr);
	EnableWindow(hDiskID, actual_enable_mbr);
}

static void EnableBootOptions(BOOL enable, BOOL remove_checkboxes)
{
	BOOL actual_enable_bb, actual_enable = enable;

	// If no device is selected, don't enable anything
	if (ComboBox_GetCurSel(hDeviceList) < 0)
		actual_enable = FALSE;
	// If boot selection is set to image, but no image is currently selected, don't enable anything
	if ((bt == BT_IMAGE) && (image_path == NULL))
		actual_enable = FALSE;
	actual_enable_bb = actual_enable;
	// If we are dealing with a pure DD image, remove all options except Bad Blocks check
	if ((bt == BT_IMAGE) && (img_report.is_bootable_img) && (!img_report.is_iso))
		actual_enable = FALSE;

	EnableWindow(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION), actual_enable);
	EnableMBRBootOptions(actual_enable, remove_checkboxes);

	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_QUICK_FORMAT), actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_BAD_BLOCKS), actual_enable_bb);
	EnableWindow(GetDlgItem(hMainDialog, IDC_NB_PASSES), actual_enable_bb);
	EnableWindow(GetDlgItem(hMainDialog, IDC_EXTENDED_LABEL), actual_enable);
}

static void SetProposedLabel(int ComboIndex)
{
	const char no_label[] = STR_NO_LABEL, empty[] = "";

	app_changed_label = TRUE;
	// If bootable ISO creation is selected, and we have an ISO selected with a valid name, use that
	// Also some distros (eg. Arch) require the USB to have the same label as the ISO
	if ((bt == BT_IMAGE) && (image_path != NULL) && (img_report.label[0] != 0)) {
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

// Toggle controls according to operation
static void EnableControls(BOOL bEnable)
{
	// The following only get disabled on format/checksum and otherwise remain enabled,
	// even if no device or image are selected
	EnableWindow(hDeviceList, bEnable);
	EnableWindow(hBootType, bEnable);
	EnableWindow(hSelectImage, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_LIST_USB_HDD), bEnable);
	EnableWindow(hAdvancedDeviceToolbar, bEnable);
	EnableWindow(hAdvancedFormatToolbar, bEnable);
	SendMessage(hMultiToolbar, TB_ENABLEBUTTON, (WPARAM)IDC_LANG, (LPARAM)bEnable);
	SendMessage(hMultiToolbar, TB_ENABLEBUTTON, (WPARAM)IDC_ABOUT, (LPARAM)bEnable);
	SendMessage(hMultiToolbar, TB_ENABLEBUTTON, (WPARAM)IDC_SETTINGS, (LPARAM)bEnable);

	// Checksum button is enabled if an image has been selected
	EnableWindow(GetDlgItem(hMainDialog, IDC_HASH), bEnable && (bt == BT_IMAGE) && (image_path != NULL));

	// Toggle CLOSE/CANCEL
	SetDlgItemTextU(hMainDialog, IDCANCEL, bEnable ? uppercase_close : uppercase_cancel);

	// Only enable the following controls if a device is active
	bEnable = (ComboBox_GetCurSel(hDeviceList) < 0) ? FALSE : bEnable;
	EnableWindow(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION), bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_SAVE), bEnable);

	// Enable or disable the Start button and the other boot options
	bEnable = ((bt == BT_IMAGE) && (image_path == NULL)) ? FALSE : bEnable;
	EnableWindow(hStart, bEnable);
	EnableBootOptions(bEnable, FALSE);

	// Finally, only enable the half-size dropdowns if we aren't dealing with a pure DD image
	bEnable = ((bt == BT_IMAGE) && (image_path != NULL) && (!img_report.is_iso)) ? FALSE : bEnable;
	EnableWindow(hPartitionScheme, bEnable);
	EnableWindow(hTargetSystem, bEnable);
	EnableWindow(GetDlgItem(hMainDialog, IDS_CSM_HELP_TXT), bEnable);
	EnableWindow(hFileSystem, bEnable);
	EnableWindow(hClusterSize, bEnable);
}

// Populate the UI main dropdown properties.
// This should be called on device or boot type change.
static BOOL PopulateProperties(int device_index)
{
	char* device_tooltip;
	char fs_type[32];

	memset(&SelectedDrive, 0, sizeof(SelectedDrive));
	EnableWindow(hStart, FALSE);

	if (device_index < 0)
		goto out;

	// Get data from the currently selected drive
	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, device_index);
	GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_type, sizeof(fs_type), FALSE);

	SetPartitionSchemeAndTargetSystem(FALSE);
	if (!SetFileSystemAndClusterSize(fs_type)) {
		SetProposedLabel(-1);
		uprintf("No file system is selectable for this drive\n");
		return FALSE;
	}

	EnableControls(TRUE);

	// Set a proposed label according to the size (eg: "256MB", "8GB")
	static_sprintf(SelectedDrive.proposed_label,
		SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, use_fake_units));

	// Add a tooltip (with the size of the device in parenthesis)
	device_tooltip = (char*) malloc(safe_strlen(DriveID.String[device_index]) + 16);
	if (device_tooltip != NULL) {
		safe_sprintf(device_tooltip, safe_strlen(DriveID.String[device_index]) + 16, "%s (%s)",
			DriveID.String[device_index], SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, FALSE));
		CreateTooltip(hDeviceList, device_tooltip, -1);
		free(device_tooltip);
	}

out:
	SetProposedLabel(device_index);
	return TRUE;
}

// Set up progress bar real estate allocation
static void InitProgress(BOOL bOnlyFormat)
{
	int i;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	memset(nb_slots, 0, sizeof(nb_slots));
	memset(slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	if (bOnlyFormat) {
		nb_slots[OP_FORMAT] = -1;
	} else {
		nb_slots[OP_ANALYZE_MBR] = 1;
		if (IsChecked(IDC_BAD_BLOCKS)) {
			nb_slots[OP_BADBLOCKS] = -1;
		}
		if (bt != BT_NON_BOOTABLE) {
			// 1 extra slot for PBR writing
			switch (selection_default) {
			case BT_MSDOS:
				nb_slots[OP_DOS] = 3+1;
				break;
			case BT_FREEDOS:
				nb_slots[OP_DOS] = 5+1;
				break;
			case BT_IMAGE:
				nb_slots[OP_DOS] = img_report.is_iso ? -1 : 0;
				break;
			default:
				nb_slots[OP_DOS] = 2+1;
				break;
			}
		}
		if (selection_default == BT_IMAGE && !img_report.is_iso) {
			nb_slots[OP_FORMAT] = -1;
		} else {
			nb_slots[OP_ZERO_MBR] = 1;
			nb_slots[OP_PARTITION] = 1;
			nb_slots[OP_FIX_MBR] = 1;
			nb_slots[OP_CREATE_FS] =
				nb_steps[ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))];
			if ( (!IsChecked(IDC_QUICK_FORMAT))
			  || ((fs == FS_FAT32) && ((SelectedDrive.DiskSize >= LARGE_FAT32_SIZE) || (force_large_fat32))) ) {
				nb_slots[OP_FORMAT] = -1;
			}
			nb_slots[OP_FINALIZE] = ((selection_default == BT_IMAGE) && (fs == FS_NTFS))?3:2;
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

	// If there's no analog, adjust our discrete ends to fill the whole bar
	if (slots_analog == 0.0f) {
		for (i=0; i<OP_MAX; i++) {
			slot_end[i+1] *= 100.0f / slots_discrete;
		}
	}
}

// Position the progress bar within each operation range
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
	if (GetTickCount64() > LastRefresh + (2 * MAX_REFRESH)) {
		LastRefresh = GetTickCount64();
		SendMessage(hProgress, PBM_SETPOS, (WPARAM)pos, 0);
		SetTaskbarProgressValue(pos, MAX_PROGRESS);
	}
}

// Callback for the log window
BOOL CALLBACK LogCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
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
		hDC = GetDC(NULL);
		lfHeight = -MulDiv(8, GetDeviceCaps(hDC, LOGPIXELSY), 72);
		safe_release_dc(NULL, hDC);
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
	case UM_RESIZE_BUTTONS:
		// Resize our buttons for low scaling factors
		ResizeButtonHeight(hDlg, IDCANCEL);
		ResizeButtonHeight(hDlg, IDC_LOG_SAVE);
		ResizeButtonHeight(hDlg, IDC_LOG_CLEAR);
		return TRUE;
	}
	return FALSE;
}

// Timer in the right part of the status area
static void CALLBACK ClockTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	timer++;
	static_sprintf(szTimer, "%02d:%02d:%02d", timer/3600, (timer%3600)/60, timer%60);
	SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
}

// Device Refresh Timer
static void CALLBACK RefreshTimer(HWND hWnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	// DO NOT USE WM_DEVICECHANGE - IT MAY BE FILTERED OUT BY WINDOWS!
	SendMessage(hWnd, UM_MEDIA_CHANGE, 0, 0);
}

// Detect and notify about a blocking operation during ISO extraction cancellation
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
		if ( ((!display_togo_option) && (HAS_WINTOGO(img_report))) || ((display_togo_option) && (!HAS_WINTOGO(img_report))) )
			ToggleImageOption();
}

// Move a control along the Y axis
static __inline void MoveCtrlY(HWND hDlg, int nID, int vertical_shift) {
	ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, nID), 0, vertical_shift, 0, 0, 1.0f);
}

static void SetPassesTooltip(void)
{
	const unsigned char pattern[] = BADBLOCK_PATTERNS;
	CreateTooltip(hNBPasses, lmprintf(MSG_153 + ComboBox_GetCurSel(hNBPasses),
		pattern[0], pattern[1], pattern[2], pattern[3]), -1);
}

static void ResizeDialogs(int shift)
{
	RECT rc;
	POINT point;

	// Resize the main dialog
	GetWindowRect(hMainDialog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top);
	MoveWindow(hMainDialog, rc.left, rc.top, point.x, point.y + shift, TRUE);

	// Resize the log
	GetWindowRect(hLogDialog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top);
	MoveWindow(hLogDialog, rc.left, rc.top, point.x, point.y + shift, TRUE);
	MoveCtrlY(hLogDialog, IDC_LOG_CLEAR, shift);
	MoveCtrlY(hLogDialog, IDC_LOG_SAVE, shift);
	MoveCtrlY(hLogDialog, IDCANCEL, shift);
	GetWindowRect(hLog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top) + shift;
	SetWindowPos(hLog, NULL, 0, 0, point.x, point.y, SWP_NOZORDER);
	// Don't forget to scroll the edit to the bottom after resize
	Edit_Scroll(hLog, 0, Edit_GetLineCount(hLog));
}

// Toggle "advanced" options
static void ToggleAdvancedDeviceOptions(BOOL enable)
{
	RECT rc;
	SIZE sz;
	TBBUTTONINFO button_info;
	int i, shift = advanced_device_section_height;

	if (!enable)
		shift = -shift;
	format_vpos += shift;
	status_vpos += shift;

	// Toggle the Hide/Show toolbar text
	utf8_to_wchar_no_alloc(lmprintf((enable) ? MSG_122 : MSG_121, lmprintf(MSG_119)), wtbtext[0], ARRAYSIZE(wtbtext[0]));
	button_info.cbSize = sizeof(button_info);
	button_info.dwMask = TBIF_TEXT;
	button_info.pszText = wtbtext[0];
	SendMessage(hAdvancedDeviceToolbar, TB_SETBUTTONINFO, (WPARAM)IDC_ADVANCED_DRIVE_PROPERTIES, (LPARAM)&button_info);
	SendMessage(hAdvancedDeviceToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)((enable) ? hUpImageList : hDownImageList));
	GetWindowRect(hAdvancedDeviceToolbar, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SendMessage(hAdvancedDeviceToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedDeviceToolbar, HWND_TOP, rc.left, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Move the controls up or down
	for (i = 0; i<ARRAYSIZE(advanced_device_move_ids); i++)
		MoveCtrlY(hMainDialog, advanced_device_move_ids[i], shift);

	// Hide or show the various advanced options
	for (i = 0; i<ARRAYSIZE(advanced_device_toggle_ids); i++)
		ShowWindow(GetDlgItem(hMainDialog, advanced_device_toggle_ids[i]), enable ? SW_SHOW : SW_HIDE);

	GetWindowRect(hDeviceList, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SetWindowPos(hDeviceList, HWND_TOP, rc.left, rc.top,
		enable ? fw - ssw - sbw : fw, rc.bottom - rc.top, 0);

	// Resize the main dialog and log window
	ResizeDialogs(shift);

	// Never hurts to force Windows' hand
	InvalidateRect(hMainDialog, NULL, TRUE);
}

static void ToggleAdvancedFormatOptions(BOOL enable)
{
	RECT rc;
	SIZE sz;
	TBBUTTONINFO button_info;
	int i, shift = advanced_format_section_height;

	if (!enable)
		shift = -shift;
	status_vpos += shift;

	// Toggle the Hide/Show toolbar text
	utf8_to_wchar_no_alloc(lmprintf((enable) ? MSG_122 : MSG_121, lmprintf(MSG_120)), wtbtext[1], ARRAYSIZE(wtbtext[0]));
	button_info.cbSize = sizeof(button_info);
	button_info.dwMask = TBIF_TEXT;
	button_info.pszText = wtbtext[1];
	SendMessage(hAdvancedFormatToolbar, TB_SETBUTTONINFO, (WPARAM)IDC_ADVANCED_FORMAT_OPTIONS, (LPARAM)&button_info);
	SendMessage(hAdvancedFormatToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)((enable) ? hUpImageList : hDownImageList));
	GetWindowRect(hAdvancedFormatToolbar, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SendMessage(hAdvancedFormatToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedFormatToolbar, HWND_TOP, rc.left, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Move the controls up or down
	for (i = 0; i<ARRAYSIZE(advanced_format_move_ids); i++)
		MoveCtrlY(hMainDialog, advanced_format_move_ids[i], shift);

	// Hide or show the various advanced options
	for (i = 0; i<ARRAYSIZE(advanced_format_toggle_ids); i++)
		ShowWindow(GetDlgItem(hMainDialog, advanced_format_toggle_ids[i]), enable ? SW_SHOW : SW_HIDE);

	// Resize the main dialog and log window
	ResizeDialogs(shift);

	// Never hurts to force Windows' hand
	InvalidateRect(hMainDialog, NULL, TRUE);
}

// Toggle the Image Option dropdown (Windows To Go or Casper settings)
static void ToggleImageOption(void)
{
	HWND hCtrl;
	int i, shift = row_height;
	// Windows To Go mode is only available for Windows 8 or later due to the lack
	// of an ISO mounting API on previous versions.
	// But we still need to be able to hide the Windows To Go option on startup.
	if ((nWindowsVersion < WINDOWS_8) && (!display_togo_option))
		return;

	display_togo_option = !display_togo_option;
	if (display_togo_option) {
		hCtrl = GetDlgItem(hMainDialog, IDC_IMAGE_OPTION);
		// Populate the dropdown
		IGNORE_RETVAL(ComboBox_ResetContent(hCtrl));
		IGNORE_RETVAL(ComboBox_SetItemData(hCtrl, ComboBox_AddStringU(hCtrl, lmprintf(MSG_117)), FALSE));
		IGNORE_RETVAL(ComboBox_SetItemData(hCtrl, ComboBox_AddStringU(hCtrl, lmprintf(MSG_118)), TRUE));
		IGNORE_RETVAL(ComboBox_SetCurSel(hCtrl, 0));
	} else
		shift = -shift;
	format_vpos += shift;
	status_vpos += shift;

	// Move the controls up or down
	for (i = 0; i<ARRAYSIZE(image_option_move_ids); i++)
		MoveCtrlY(hMainDialog, image_option_move_ids[i], shift);

	// Resize the main dialog and log window
	ResizeDialogs(shift);

	// Hide or show the boot options
	for (i = 0; i < ARRAYSIZE(image_option_toggle_ids); i++)
		ShowWindow(GetDlgItem(hMainDialog, image_option_toggle_ids[i]), display_togo_option ? SW_SHOW : SW_HIDE);

	// If you don't force a redraw here, all kind of bad UI artifacts happen...
	InvalidateRect(hMainDialog, NULL, TRUE);
}

static void SetBootTypeDropdownWidth(void)
{
	HDC hDC;
	HFONT hFont;
	SIZE sz;
	RECT rc;

	if (image_path == NULL)
		return;
	// Set the maximum width of the dropdown according to the image selected
	GetWindowRect(hBootType, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	hDC = GetDC(hBootType);
	hFont = (HFONT)SendMessageA(hBootType, WM_GETFONT, 0, 0);
	SelectObject(hDC, hFont);
	GetTextExtentPointU(hDC, short_image_path, &sz);
	safe_release_dc(hBootType, hDC);
	SendMessage(hBootType, CB_SETDROPPEDWIDTH, (WPARAM)max(sz.cx + 10, rc.right - rc.left), (LPARAM)0);
}

// Insert the image name into the Boot selection dropdown
static void UpdateImage(void)
{
	int index;

	for (index = 0; index < ComboBox_GetCount(hBootType); index++) {
		if (ComboBox_GetItemData(hBootType, index) == BT_IMAGE) {
			break;
		}
	}

	if (image_path != NULL) {
		ComboBox_DeleteString(hBootType, index);
		ComboBox_InsertStringU(hBootType, index, short_image_path);
		ComboBox_SetItemData(hBootType, index, BT_IMAGE);
		IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, index));
		bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
		SetBootTypeDropdownWidth();
	}
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
		safe_free(image_path);
		EnableControls(TRUE);
		SetMBRProps();
		PopulateProperties(ComboBox_GetCurSel(hDeviceList));
		PrintInfoDebug(0, MSG_203);
		PrintStatus(0, MSG_203);
		goto out;
	}

	if (img_report.is_bootable_img) {
		uprintf("  Image is a %sbootable %s image",
			(img_report.compression_type != BLED_COMPRESSION_NONE) ? "compressed " : "", img_report.is_vhd ? "VHD" : "disk");
		selection_default = BT_IMAGE;
	}

	if (img_report.is_iso) {
		DisplayISOProps();
		// If we have an ISOHybrid, but without an ISO method we support, disable ISO support altogether
		if (IS_DD_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report) && !IS_EFI_BOOTABLE(img_report)) {
			uprintf("This ISOHybrid is not compatible with any of the ISO boot methods we support");
			img_report.is_iso = FALSE;
		}
		selection_default = BT_IMAGE;
	}
	if (!IS_DD_BOOTABLE(img_report) && !IS_BIOS_BOOTABLE(img_report) && !IS_EFI_BOOTABLE(img_report)) {
		// No boot method that we support
		PrintInfo(0, MSG_081);
		safe_free(image_path);
		MessageBoxExU(hMainDialog, lmprintf(MSG_082), lmprintf(MSG_081), MB_OK | MB_ICONINFORMATION | MB_IS_RTL, selected_langid);
		PrintStatus(0, MSG_086);
		EnableControls(TRUE);
		SetMBRProps();
	} else {
		if (!dont_display_image_name) {
			for (i = (int)safe_strlen(image_path); (i > 0) && (image_path[i] != '\\'); i--);
			short_image_path = &image_path[i + 1];
			PrintStatus(0, MSG_205, short_image_path);
			UpdateImage();
			uprintf("Using image: %s (%s)", short_image_path, SizeToHumanReadable(img_report.image_size, FALSE, FALSE));
		}
		EnableControls(TRUE);
		// Set Target and FS accordingly
		if (img_report.is_iso) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, image_index));
			SetPartitionSchemeAndTargetSystem(FALSE);
			SetFileSystemAndClusterSize(NULL);
			SetFSFromISO();
			SetMBRProps();
			SetProposedLabel(ComboBox_GetCurSel(hDeviceList));
		} else {
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE << 16) | IDC_FILE_SYSTEM,
				ComboBox_GetCurSel(hFileSystem));
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

static BOOL BootCheck(void)
{
	int i, r;
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
	if (bt == BT_IMAGE) {
		// We should never be there
		if (image_path == NULL) {
			uprintf("Spock gone crazy error in %s:%d", __FILE__, __LINE__);
			MessageBoxExU(hMainDialog, "image_path is NULL. Please report this error to the author of this application", "Logic error", MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
		if ((size_check) && (img_report.projected_size > (uint64_t)SelectedDrive.DiskSize)) {
			// This ISO image is too big for the selected target
			MessageBoxExU(hMainDialog, lmprintf(MSG_089), lmprintf(MSG_088), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			return FALSE;
		}
		if (IS_DD_BOOTABLE(img_report) && !img_report.is_iso) {
			// Pure DD images are fine at this stage
			return TRUE;
		}
		if ((display_togo_option) && (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1)) {
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

static INT_PTR CALLBACK ProgressCallback(HWND hCtrl, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	RECT rect;
	PAINTSTRUCT ps;
	wchar_t winfo[128] = L"Copying ISO files...";

	switch (message) {

	case WM_PAINT:
		hDC = BeginPaint(hCtrl, &ps);
		CallWindowProc(progress_original_proc, hCtrl, message, (WPARAM)hDC, lParam);
		GetWindowTextW(hProgress, winfo, ARRAYSIZE(winfo));
		SetBkMode(hDC, TRANSPARENT);
		SelectObject(hDC, hInfoFont);
		SetTextColor(hDC, RGB(0xFF, 0xFF, 0xFF));
		SetTextAlign(hDC, TA_CENTER | TA_BASELINE);
		GetClientRect(hCtrl, &rect);
		ExtTextOutW(hDC, rect.right / 2, rect.bottom / 2 + (int)(4.0f * fScale),
			ETO_CLIPPED | ETO_NUMERICSLOCAL | (right_to_left_mode ? ETO_RTLREADING : 0),
			&rect, winfo, (int)wcslen(winfo), NULL);
		EndPaint(hCtrl, &ps);
		return (INT_PTR)TRUE;
	}

	return CallWindowProc(progress_original_proc, hCtrl, message, wParam, lParam);
}

static void CreateAdditionalControls(HWND hDlg)
{
	HINSTANCE hDll;
	HIMAGELIST hToolbarImageList;
	RECT rc;
	SIZE sz;
	int i16, s16, toolbar_dx = -4 - ((fScale > 1.49f) ? 1 : 0) - ((fScale > 1.99f) ? 1 : 0);
	TBBUTTON tbToolbarButtons[7];

	s16 = i16 = GetSystemMetrics(SM_CXSMICON);
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;

	// Load system icons (NB: Use the excellent http://www.nirsoft.net/utils/iconsext.html to find icon IDs)
	hDll = GetLibraryHandle("Shell32");
	hIconSave = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16761), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hIconLog = (HICON)LoadImage(hDll, MAKEINTRESOURCE(281), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hIconAbout = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16783), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hIconSettings = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16826), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	if (hIconSettings == NULL)
		hIconSettings = (HICON)LoadImage(hDll, MAKEINTRESOURCE(153), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);

	if (nWindowsVersion >= WINDOWS_8) {
		// Use the icon from the Windows 8+ 'Language' Control Panel
		hDll = GetLibraryHandle("UserLanguagesCpl");
		hIconLang = (HICON)LoadImage(hDll, MAKEINTRESOURCE(1), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	} else {
		// Otherwise use the globe icon, from the Internet Options Control Panel
		hDll = GetLibraryHandle("inetcpl.cpl");
		hIconLang = (HICON)LoadImage(hDll, MAKEINTRESOURCE(1313), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	}

	// Fetch the up and down expand icons for the advanced options toolbar
	hDll = GetLibraryHandle("ComDlg32");
	hIconDown = (HICON)LoadImage(hDll, MAKEINTRESOURCE(577), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hIconUp = (HICON)LoadImage(hDll, MAKEINTRESOURCE(578), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	// Fallback to using Shell32 if we can't locate the icons we want in ComDlg32
	hDll = GetLibraryHandle("Shell32");
	if (hIconUp == NULL)
		hIconUp = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16749), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	if (hIconDown == NULL)
		hIconDown = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16750), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hUpImageList = ImageList_Create(i16, i16, ILC_COLOR32, 1, 0);
	hDownImageList = ImageList_Create(i16, i16, ILC_COLOR32, 1, 0);
	ImageList_AddIcon(hUpImageList, hIconUp);
	ImageList_AddIcon(hDownImageList, hIconDown);

	// Create the advanced options toolbars
	memset(wtbtext, 0, sizeof(wtbtext));
	utf8_to_wchar_no_alloc(lmprintf((advanced_mode_device) ? MSG_122 : MSG_121, lmprintf(MSG_119)), wtbtext[0], ARRAYSIZE(wtbtext[0]));
	hAdvancedDeviceToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | WS_TABSTOP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CCS_NOPARENTALIGN |
		CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_AUTOSIZE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_ADVANCED_DEVICE_TOOLBAR, hMainInstance, NULL);
	SendMessage(hAdvancedDeviceToolbar, CCM_SETVERSION, (WPARAM)6, 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_ADVANCED_DRIVE_PROPERTIES;
	tbToolbarButtons[0].fsStyle = BTNS_SHOWTEXT | BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iString = (INT_PTR)wtbtext[0];
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hAdvancedDeviceToolbar, TB_SETIMAGELIST, 0, (LPARAM)hUpImageList);
	SendMessage(hAdvancedDeviceToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	SendMessage(hAdvancedDeviceToolbar, TB_ADDBUTTONS, 1, (LPARAM)&tbToolbarButtons);
	GetWindowRect(GetDlgItem(hDlg, IDC_ADVANCED_DRIVE_PROPERTIES), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hAdvancedDeviceToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedDeviceToolbar, HWND_TOP, rc.left + toolbar_dx, rc.top, sz.cx, rc.bottom - rc.top, 0);

	utf8_to_wchar_no_alloc(lmprintf((advanced_mode_format) ? MSG_122 : MSG_121, lmprintf(MSG_120)), wtbtext[1], ARRAYSIZE(wtbtext[1]));
	hAdvancedFormatToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | WS_TABSTOP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN  | CCS_NOPARENTALIGN |
		CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_AUTOSIZE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_ADVANCED_FORMAT_TOOLBAR, hMainInstance, NULL);
	SendMessage(hAdvancedFormatToolbar, CCM_SETVERSION, (WPARAM)6, 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_ADVANCED_FORMAT_OPTIONS;
	tbToolbarButtons[0].fsStyle = BTNS_SHOWTEXT | BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iString = (INT_PTR)wtbtext[1];
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hAdvancedFormatToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hUpImageList);
	SendMessage(hAdvancedFormatToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	SendMessage(hAdvancedFormatToolbar, TB_ADDBUTTONS, (WPARAM)1, (LPARAM)&tbToolbarButtons);
	GetWindowRect(GetDlgItem(hDlg, IDC_ADVANCED_FORMAT_OPTIONS), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hAdvancedFormatToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedFormatToolbar, HWND_TOP, rc.left + toolbar_dx, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Create the bottom toolbar
	hMultiToolbar = CreateWindowExW(0, TOOLBARCLASSNAME, NULL,
		WS_CHILD | WS_TABSTOP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CCS_NOPARENTALIGN |
		CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_AUTOSIZE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_MULTI_TOOLBAR, hMainInstance, NULL);
	hToolbarImageList = ImageList_Create(i16, i16, ILC_COLOR32, 4, 0);
	ImageList_AddIcon(hToolbarImageList, hIconLang);
	ImageList_AddIcon(hToolbarImageList, hIconAbout);
	ImageList_AddIcon(hToolbarImageList, hIconSettings);
	ImageList_AddIcon(hToolbarImageList, hIconLog);
	SendMessage(hMultiToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hToolbarImageList);
	SendMessage(hMultiToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON) * ARRAYSIZE(tbToolbarButtons));
	tbToolbarButtons[0].idCommand = IDC_LANG;
	tbToolbarButtons[0].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iBitmap = 0;
	tbToolbarButtons[1].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[1].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[1].iBitmap = I_IMAGENONE;
	tbToolbarButtons[1].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[2].idCommand = IDC_ABOUT;
	tbToolbarButtons[2].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[2].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[2].iBitmap = 1;
	tbToolbarButtons[3].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[3].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[3].iBitmap = I_IMAGENONE;
	tbToolbarButtons[3].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[4].idCommand = IDC_SETTINGS;
	tbToolbarButtons[4].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[4].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[4].iBitmap = 2;
	tbToolbarButtons[5].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[5].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[5].iBitmap = I_IMAGENONE;
	tbToolbarButtons[5].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[6].idCommand = IDC_LOG;
	tbToolbarButtons[6].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[6].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[6].iBitmap = 3;
	SendMessage(hMultiToolbar, TB_ADDBUTTONS, (WPARAM)7, (LPARAM)&tbToolbarButtons);

	// Set the icons on the the buttons
	bi_save.himl = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	ImageList_ReplaceIcon(bi_save.himl, -1, hIconSave);
	SetRect(&bi_save.margin, 0, 1, 0, 0);
	bi_save.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_down.himl = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	ImageList_ReplaceIcon(bi_down.himl, -1, hIconDown);
	SetRect(&bi_down.margin, 0, 0, 0, 0);
	bi_down.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;
	bi_up.himl = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_MASK, 1, 0);
	ImageList_ReplaceIcon(bi_up.himl, -1, hIconUp);
	SetRect(&bi_up.margin, 0, 0, 0, 0);
	bi_up.uAlign = BUTTON_IMAGELIST_ALIGN_CENTER;

	SendMessage(GetDlgItem(hDlg, IDC_SAVE), BCM_SETIMAGELIST, 0, (LPARAM)&bi_save);
}

// https://stackoverflow.com/a/20926332/1069307
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb226818.aspx
static void GetBasicControlsWidth(HWND hDlg)
{
	int checkbox_internal_spacing = 12, dropdown_internal_spacing = 15;
	RECT rc = { 0, 0, 4, 8 };
	SIZE bu;

	// Compute base unit sizes since GetDialogBaseUnits() returns garbage data.
	// See http://support.microsoft.com/kb/125681
	MapDialogRect(hDlg, &rc);
	bu.cx = rc.right;
	bu.cy = rc.bottom;

	// TODO: figure out the specifics of each Windows version
	if (nWindowsVersion == WINDOWS_10) {
		checkbox_internal_spacing = 10;
		dropdown_internal_spacing = 13;
	}

	// Checkbox and (blank) dropdown widths
	cbw = MulDiv(checkbox_internal_spacing, bu.cx, 4);
	ddw = MulDiv(dropdown_internal_spacing, bu.cx, 4);

	// Spacing width between half-length dropdowns (sep) as well as left margin
	GetWindowRect(GetDlgItem(hDlg, IDC_TARGET_SYSTEM), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sw = rc.left;
	GetWindowRect(GetDlgItem(hDlg, IDC_PARTITION_TYPE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sw -= rc.right;
	mw = rc.left;

	// Small button and small separator widths
	GetWindowRect(GetDlgItem(hDlg, IDC_SAVE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sbw = rc.right - rc.left;
	bh = rc.bottom - rc.top;
	ssw = rc.left;
	GetWindowRect(hDeviceList, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	ssw -= rc.right;

	// Hash button width
	GetWindowRect(GetDlgItem(hDlg, IDC_HASH), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	hbw = rc.right - rc.left;

	// CSM tooltip separator width
	GetWindowRect(GetDlgItem(hDlg, IDS_CSM_HELP_TXT), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	tw = rc.left;
	GetWindowRect(hTargetSystem, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	tw -= rc.right;
}

// Compute the minimum size of the main buttons
static void GetMainButtonsWidth(HWND hDlg)
{
	unsigned int i;
	RECT rc;

	GetWindowRect(GetDlgItem(hDlg, main_button_ids[0]), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	bw = rc.right - rc.left;

	for (i = 0; i < ARRAYSIZE(main_button_ids); i++)
		bw = max(bw, GetTextWidth(hDlg, main_button_ids[i]) + cbw);
	// The 'CLOSE' button is also be used to display 'CANCEL' => measure that too
	bw = max(bw, GetTextSize(GetDlgItem(hDlg, IDCANCEL), lmprintf(MSG_007)).cx + cbw);
}

// The following goes over the data that gets populated into the half-width dropdowns
// (Partition scheme, Target System, Disk ID, File system, Cluster size, Nb passes)
// to figure out the minimum width we should allocate.
static void GetHalfDropwdownWidth(HWND hDlg)
{
	RECT rc;
	unsigned int i, j, msg_id;
	char tmp[256];

	// Initialize half width to the UI's default size
	GetWindowRect(GetDlgItem(hDlg, IDC_PARTITION_TYPE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	hw = rc.right - rc.left - ddw;

	// "Super Floppy Disk" is the longuest entry in the Partition Scheme dropdown
	hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_PARTITION_TYPE), (char*)sfd_name).cx);

	// This is basically the same as SetClusterSizeLabels() except we're adding (Default) to each entry
	for (i = 512, j = 1, msg_id = MSG_026; j<MAX_CLUSTER_SIZES; i <<= 1, j++) {
		if (i > 8192) {
			i /= 1024;
			msg_id++;
		}
		safe_sprintf(tmp, 64, "%d %s", i, lmprintf(msg_id));
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_CLUSTER_SIZE), lmprintf(MSG_030, tmp)).cx);
	}
	// We don't go over file systems, because none of them will be longer than "Super Floppy Disk"
	// We do however go over the BIOS vs UEFI entries, as some of these are translated
	for (msg_id = MSG_031; msg_id <= MSG_033; msg_id++)
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_TARGET_SYSTEM), lmprintf(msg_id)).cx);

	// Just in case, we also do the number of passes
	for (i = 1; i <= 4; i++)
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_TARGET_SYSTEM),
			lmprintf((i == 1) ? MSG_034 : MSG_035, i)).cx);

	// Finally, we must ensure that we'll have enough space for the 2 checkbox controls
	// that end up with a half dropdown
	hw = max(hw, GetTextWidth(hDlg, IDC_RUFUS_MBR) - sw);
	hw = max(hw, GetTextWidth(hDlg, IDC_BAD_BLOCKS) - sw);

	// Add the width of a blank dropdown
	hw += ddw;
}

/*
 * dbw = dialog border width
 * mw  = margin width
 * fw  = full dropdown width
 * hd  = half dropdown width
 * bsw = boot selection dropdown width
 * sw  = separator width
 * ssw = small separator width
 * bw  = button width
 * sbw = small button width
 * hbw = hash button width
 * 
 *      |                      fw                      |
 *      |          bsw          | ssw | hbw | ssw | bw |
 *  8 ->|<-    96     ->|<-    24    ->|<-    96     ->|<- 8
 *  mw  |      hw       |      sw      |      hw       |  mw
 *                                     | bw | ssw | bw |
 */
static void GetFullWidth(HWND hDlg)
{
	RECT rc;
	int i;

	// Get the dialog border width
	GetWindowRect(hDlg, &rc);
	dbw = rc.right - rc.left;
	GetClientRect(hDlg, &rc);
	dbw -= rc.right - rc.left;

	// Compute the minimum size needed for the Boot Selection dropdown
	GetWindowRect(GetDlgItem(hDlg, IDC_BOOT_SELECTION), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);

	bsw = max(rc.right - rc.left, GetTextSize(hBootType, lmprintf(MSG_279)).cx + ddw);
	bsw = max(bsw, GetTextSize(hBootType, lmprintf(MSG_281, lmprintf(MSG_280))).cx + ddw);

	// Initialize full width to the UI's default size
	GetWindowRect(GetDlgItem(hDlg, IDC_IMAGE_OPTION), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	fw = rc.right - rc.left - ddw;

	// Go through the Image Options for Windows To Go
	fw = max(fw, GetTextSize(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_117)).cx);
	fw = max(fw, GetTextSize(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_118)).cx);

	// Now deal with full length checkbox lines
	for (i=0; i<ARRAYSIZE(full_width_checkboxes); i++)
		fw = max(fw, GetTextWidth(hDlg, full_width_checkboxes[i]));

	// All of the above is for text only, so we need to add dd space
	fw += ddw;

	// Our min also needs to be longer than 2 half length dropdowns + spacer
	fw = max(fw, 2 * hw + sw);

	// Now that we have our minimum full width, adjust the button width if needed
	// Adjust according to min full width
	bw = max(bw, (fw - 2 * ssw - sw) / 4);
	// Adjust according to min boot selection width
	bw = max(bw, (bsw + hbw - sw) / 3);

	// Adjust according to min half width
	bw = max(bw, (hw / 2) - ssw);

	// Now that our button width is set, we can adjust the rest
	hw = max(hw, 2 * bw + ssw);
	fw = max(fw, 2 * hw + sw);

	bsw = max(bsw, fw - bw - 2 * ssw - hbw);

	// TODO: Also pick a few choice messages from info/status
}

static void PositionControls(HWND hDlg)
{
	RECT rc, rcSelectedImage;
	HWND hCtrl;
	SIZE sz;
	int i, x, button_fudge = 2;

	// Start by resizing the whole dialog
	GetWindowRect(hDlg, &rc);
	// Don't forget to add the dialog border width, since we resize the whole dialog
	SetWindowPos(hDlg, NULL, -1, -1, fw + 2*mw + dbw, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

	// Resize the height of the label and progress bar to the height of standard dropdowns
	hCtrl = GetDlgItem(hDlg, IDC_DEVICE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	ddh = rc.bottom - rc.top;
	ddbh = ddh + button_fudge;
	bh = max(bh, ddbh);
	hCtrl = GetDlgItem(hDlg, IDC_LABEL);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, NULL, rc.left, rc.top, rc.right - rc.left, ddh, SWP_NOZORDER);
	GetWindowRect(hProgress, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hProgress, NULL, rc.left, rc.top, rc.right - rc.left, ddh, SWP_NOZORDER);

	// Get the height of a typical row
	hCtrl = GetDlgItem(hDlg, IDS_BOOT_SELECTION_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	row_height = rc.top;
	hCtrl = GetDlgItem(hDlg, IDS_DEVICE_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	row_height -= rc.top;

	// Get the height of the advanced options
	hCtrl = GetDlgItem(hDlg, IDC_LIST_USB_HDD);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_device_section_height = rc.top;
	hCtrl = GetDlgItem(hDlg, IDC_RUFUS_MBR);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_device_section_height = rc.bottom - advanced_device_section_height;

	hCtrl = GetDlgItem(hDlg, IDC_QUICK_FORMAT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_format_section_height = rc.top;
	hCtrl = GetDlgItem(hDlg, IDC_BAD_BLOCKS);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_format_section_height = rc.bottom - advanced_format_section_height;

	// Get the vertical position of the sections text
	hCtrl = GetDlgItem(hDlg, IDS_DRIVE_PROPERTIES_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	device_vpos = rc.top + 2 * sz.cy / 3;
	hCtrl = GetDlgItem(hDlg, IDS_FORMAT_OPTIONS_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	format_vpos = rc.top + 2 * sz.cy / 3;
	hCtrl = GetDlgItem(hDlg, IDS_STATUS_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	status_vpos = rc.top + 2 * sz.cy / 3;

	// Seriously, who designed this bullshit API call where you pass a SIZE
	// struct but can only retreive one of cx or cy at a time?!?
	SendMessage(hMultiToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	GetWindowRect(GetDlgItem(hDlg, IDC_ABOUT), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hMultiToolbar, HWND_TOP, rc.left, rc.top, sz.cx, ddh, 0);

	// Reposition the main buttons
	for (i = 0; i < ARRAYSIZE(main_button_ids); i++) {
		hCtrl = GetDlgItem(hDlg, main_button_ids[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		x = mw + fw - bw;
		if (i % 2 == 1)
			x -= bw + ssw;
		SetWindowPos(hCtrl, HWND_TOP, x, rc.top, bw, ddbh, 0);
	}

	// Reposition the Hash button
	hCtrl = GetDlgItem(hDlg, IDC_HASH);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	x -= ssw + hbw;
	SetWindowPos(hCtrl, HWND_TOP, x, rc.top, hbw, rc.bottom - rc.top, 0);

	// Reposition the Save button
	hCtrl = GetDlgItem(hDlg, IDC_SAVE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, HWND_TOP, mw + fw - sbw, rc.top, sbw, rc.bottom - rc.top, 0);

	// Reposition the CSM help tip
	hCtrl = GetDlgItem(hDlg, IDS_CSM_HELP_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, HWND_TOP, mw + fw + tw, rc.top, sbw, rc.bottom - rc.top, 0);

	if (advanced_mode_device) {
		// Still need to adjust the width of the device selection dropdown
		GetWindowRect(hDeviceList, &rc);
		MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
		SetWindowPos(hDeviceList, HWND_TOP, rc.left, rc.top, fw - ssw - sbw, rc.bottom - rc.top, 0);
	}

	// Resize the full width controls
	for (i = 0; i < ARRAYSIZE(full_width_controls); i++) {
		hCtrl = GetDlgItem(hDlg, full_width_controls[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		SetWindowPos(hCtrl, HWND_TOP, rc.left, rc.top, fw, rc.bottom - rc.top, 0);
	}

	// Resize the half drowpdowns
	for (i = 0; i < ARRAYSIZE(half_width_ids); i++) {
		hCtrl = GetDlgItem(hDlg, half_width_ids[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		// First 5 controls are on the left handside
		// First 2 controls may overflow into separator
		SetWindowPos(hCtrl, HWND_TOP, (i < 5) ? rc.left : mw + hw + sw, rc.top,
			(i <2) ? hw + sw : hw, rc.bottom - rc.top, 0);
	}

	// Resize the boot selection dropdown
	hCtrl = GetDlgItem(hDlg, IDC_BOOT_SELECTION);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, HWND_TOP, rc.left, rc.top, bsw, rc.bottom - rc.top, 0);

	hCtrl = GetDlgItem(hDlg, IDC_DEVICE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz.cy = rc.top;
	hCtrl = GetDlgItem(hDlg, IDC_SAVE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, NULL, rc.left, sz.cy - 1,
		rc.right - rc.left, ddbh, SWP_NOZORDER);

	hCtrl = GetDlgItem(hDlg, IDC_BOOT_SELECTION);
	GetWindowRect(hCtrl, &rcSelectedImage);
	MapWindowPoints(NULL, hDlg, (POINT*)&rcSelectedImage, 2);
	hCtrl = GetDlgItem(hDlg, IDC_HASH);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, NULL, rc.left, rcSelectedImage.top - 1,
		rc.right - rc.left, ddbh, SWP_NOZORDER);
}

// Thanks to Microsoft atrocious DPI handling, we must adjust for low DPI
static void AdjustForLowDPI(HWND hDlg)
{
	static int ddy = 4;
	int i, j;
	RECT rc;
	HWND hCtrl;
	int dy = 0;

	if (fScale >= 1.3f)
		return;

	for (i = 0; i < ARRAYSIZE(adjust_dpi_ids); i++) {
		dy += ddy;
		// "...and the other thing I really like about Microsoft's UI handling is how "
		//."you never have to introduce weird hardcoded constants all over the place, "
		// "just to make your UI look good...", said NO ONE ever.
		if (adjust_dpi_ids[i][0] == IDC_QUICK_FORMAT)
			dy += 1;
		for (j = 0; j < 5; j++) {
			if (adjust_dpi_ids[i][j] == 0)
				break;
			hCtrl = GetDlgItem(hDlg, adjust_dpi_ids[i][j]);
			GetWindowRect(hCtrl, &rc);
			MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
			SetWindowPos(hCtrl, HWND_TOP, rc.left, rc.top + dy,
				rc.right - rc.left, rc.bottom - rc.top, 0);
		}
	}

	format_vpos += 9 * ddy;
	status_vpos += 16 * ddy + 1;
	advanced_device_section_height += 3 * ddy;
	advanced_format_section_height += 3 * ddy + 1;

	ResizeDialogs(dy + 2*ddy);
	InvalidateRect(hDlg, NULL, TRUE);
}

static void SetSectionHeaders(HWND hDlg)
{
	RECT rc;
	HWND hCtrl;
	SIZE sz;
	HFONT hf;
	wchar_t wtmp[128];
	int i, control[3] = { IDS_DRIVE_PROPERTIES_TXT, IDS_FORMAT_OPTIONS_TXT, IDS_STATUS_TXT };

	// Set the section header fonts and resize the static controls accordingly
	hf = CreateFontA(-MulDiv(14, GetDeviceCaps(GetDC(hMainDialog), LOGPIXELSY), 72), 0, 0, 0,
		FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, "Segoe UI");

	for (i = 0; i < ARRAYSIZE(control); i++) {
		SendDlgItemMessageA(hDlg, control[i], WM_SETFONT, (WPARAM)hf, TRUE);
		hCtrl = GetDlgItem(hDlg, control[i]);
		memset(wtmp, 0, sizeof(wtmp));
		GetWindowTextW(hCtrl, wtmp, ARRAYSIZE(wtmp));
		wtmp[wcslen(wtmp)] = ' ';
		SetWindowTextW(hCtrl, wtmp);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		sz = GetTextSize(hCtrl, NULL);
		SetWindowPos(hCtrl, NULL, rc.left, rc.top, sz.cx, sz.cy, SWP_NOZORDER);
	}
}

// Create the horizontal section lines
void OnPaint(HDC hdc)
{
	HPEN hp = CreatePen(0, (fScale < 1.5f)?2:3, RGB(0, 0, 0));
	SelectObject(hdc, hp);
	MoveToEx(hdc, mw + 10, device_vpos, NULL);
	LineTo(hdc, mw + fw, device_vpos);
	MoveToEx(hdc, mw + 10, format_vpos, NULL);
	LineTo(hdc, mw + fw, format_vpos);
	MoveToEx(hdc, mw + 10, status_vpos, NULL);
	LineTo(hdc, mw + fw, status_vpos);
}

static void InitDialog(HWND hDlg)
{
	DWORD len;
	HDC hDC;
	int i, lfHeight;
	char tmp[128], *token, *buf, *ext;
	static char* resource[2] = { MAKEINTRESOURCEA(IDR_SL_LDLINUX_V4_SYS), MAKEINTRESOURCEA(IDR_SL_LDLINUX_V6_SYS) };

#ifdef RUFUS_TEST
	ShowWindow(GetDlgItem(hDlg, IDC_TEST), SW_SHOW);
#endif

	hDC = GetDC(hDlg);
	lfHeight = -MulDiv(9, GetDeviceCaps(hDC, LOGPIXELSY), 72);
	safe_release_dc(hDlg, hDC);

	// Quite a burden to carry around as parameters
	hMainDialog = hDlg;
	MainThreadId = GetCurrentThreadId();
	hDeviceList = GetDlgItem(hDlg, IDC_DEVICE);
	hPartitionScheme = GetDlgItem(hDlg, IDC_PARTITION_TYPE);
	hTargetSystem = GetDlgItem(hDlg, IDC_TARGET_SYSTEM);
	hFileSystem = GetDlgItem(hDlg, IDC_FILE_SYSTEM);
	hClusterSize = GetDlgItem(hDlg, IDC_CLUSTER_SIZE);
	hLabel = GetDlgItem(hDlg, IDC_LABEL);
	hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
	hBootType = GetDlgItem(hDlg, IDC_BOOT_SELECTION);
	hSelectImage = GetDlgItem(hDlg, IDC_SELECT);
	hNBPasses = GetDlgItem(hDlg, IDC_NB_PASSES);
	hDiskID = GetDlgItem(hDlg, IDC_DISK_ID);
	hStart = GetDlgItem(hDlg, IDC_START);

	// Convert the main button labels to uppercase
	GetWindowTextU(hStart, uppercase_start, sizeof(uppercase_start));
	CharUpperBuffU(uppercase_start, sizeof(uppercase_start));
	SetWindowTextU(hStart, uppercase_start);
	GetWindowTextU(GetDlgItem(hDlg, IDCANCEL), uppercase_close, sizeof(uppercase_close));
	CharUpperBuffU(uppercase_close, sizeof(uppercase_close));
	// Hardcoded exception for German
	if (strcmp("SCHLIEßEN", uppercase_close) == 0)
		strcpy(uppercase_close, "SCHLIESSEN");
	SetWindowTextU(GetDlgItem(hDlg, IDCANCEL), uppercase_close);
	GetWindowTextU(GetDlgItem(hDlg, IDC_SELECT), uppercase_select, sizeof(uppercase_select));
	CharUpperBuffU(uppercase_select, sizeof(uppercase_select));
	SetWindowTextU(GetDlgItem(hDlg, IDC_SELECT), uppercase_select);
	strcpy(uppercase_cancel, lmprintf(MSG_007));
	CharUpperBuffU(uppercase_cancel, sizeof(uppercase_cancel));

	GetBasicControlsWidth(hDlg);
	GetMainButtonsWidth(hDlg);
	GetHalfDropwdownWidth(hDlg);
	GetFullWidth(hDlg);

	// Create the font and brush for the progress messages
	hInfoFont = CreateFontA(lfHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		0, 0, PROOF_QUALITY, 0, "Segoe UI");

	// Create the title bar icon
	SetTitleBarIcon(hDlg);
	GetWindowTextA(hDlg, tmp, sizeof(tmp));
	// Count on Microsoft for making it more attractive to read a
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

	selection_default = BT_IMAGE;
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

	// Fill up the boot options dropdown
	SetBootOptions();

	// Fill up the MBR masqueraded disk IDs ("8 disks should be enough for anybody")
	IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_030, LEFT_TO_RIGHT_EMBEDDING "0x80" POP_DIRECTIONAL_FORMATTING)), 0x80));
	for (i=1; i<=7; i++) {
		IGNORE_RETVAL(ComboBox_SetItemData(hDiskID, ComboBox_AddStringU(hDiskID, lmprintf(MSG_109, 0x80+i, i+1)), 0x80+i));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));

	// Create the string arrays
	StrArrayCreate(&DriveID, MAX_DRIVES);
	StrArrayCreate(&DriveLabel, MAX_DRIVES);
	StrArrayCreate(&DriveHub, MAX_DRIVES);
	StrArrayCreate(&BlockingProcess, 16);
	StrArrayCreate(&ImageList, 16);
	// Set various checkboxes
	CheckDlgButton(hDlg, IDC_QUICK_FORMAT, BST_CHECKED);
	CheckDlgButton(hDlg, IDC_EXTENDED_LABEL, BST_CHECKED);

	CreateAdditionalControls(hDlg);
	SetSectionHeaders(hDlg);
	PositionControls(hDlg);
	AdjustForLowDPI(hDlg);
	// Because we created the log dialog before we computed our sizes, we need to send a custom message
	SendMessage(hLogDialog, UM_RESIZE_BUTTONS, 0, 0);
	// Create the status line and initialize the taskbar icon for progress overlay
	CreateStatusBar();

	// Subclass the progress bar so that we can write on it
	progress_original_proc = (WNDPROC)SetWindowLongPtr(hProgress, GWLP_WNDPROC, (LONG_PTR)ProgressCallback);

	// Set the various tooltips
	CreateTooltip(hFileSystem, lmprintf(MSG_157), -1);
	CreateTooltip(hClusterSize, lmprintf(MSG_158), -1);
	CreateTooltip(hLabel, lmprintf(MSG_159), -1);
	CreateTooltip(hAdvancedDeviceToolbar, lmprintf(MSG_160), -1);
	CreateTooltip(hAdvancedFormatToolbar, lmprintf(MSG_160), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_BAD_BLOCKS), lmprintf(MSG_161), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_QUICK_FORMAT), lmprintf(MSG_162), -1);
	CreateTooltip(hBootType, lmprintf(MSG_164), -1);
	CreateTooltip(hSelectImage, lmprintf(MSG_165), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_EXTENDED_LABEL), lmprintf(MSG_166), 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_RUFUS_MBR), lmprintf(MSG_167), 10000);
	CreateTooltip(hDiskID, lmprintf(MSG_168), 10000);
	CreateTooltip(GetDlgItem(hDlg, IDC_OLD_BIOS_FIXES), lmprintf(MSG_169), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_LIST_USB_HDD), lmprintf(MSG_170), -1);
	CreateTooltip(hStart, lmprintf(MSG_171), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_ABOUT), lmprintf(MSG_172), -1);
	CreateTooltip(hPartitionScheme, lmprintf(MSG_163), -1);
	CreateTooltip(hTargetSystem, lmprintf(MSG_150), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDS_CSM_HELP_TXT), lmprintf(MSG_151), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDC_HASH), lmprintf(MSG_272), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_SAVE), lmprintf(MSG_304), -1);
	CreateTooltip(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_305), 30000);

	if (!advanced_mode_device)	// Hide as needed, since we display the advanced controls by default
		ToggleAdvancedDeviceOptions(FALSE);
	if (!advanced_mode_format)
		ToggleAdvancedFormatOptions(FALSE);
	ToggleImageOption();

	// Process commandline parameters
	if (iso_provided) {
		// Simulate a button click for image selection
		PostMessage(hDlg, WM_COMMAND, IDC_SELECT, 0);
	}
	SetBootTypeDropdownWidth();

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
	uint64_t cur_time, end_time = GetTickCount64() + dwTimeOut;

	// Get the current selected device
	DWORD DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList));
	if ((DeviceNum < 0x80) || (DeviceNum == (DWORD)-1))
		return FALSE;

	// "Checking for conflicting processes..."
	PrintInfo(0, MSG_278);

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
			cur_time = GetTickCount64();
			if (cur_time >= end_time)
				break;
			access_mask = SearchProcess(DevPath, (DWORD)(end_time - cur_time), TRUE, TRUE, TRUE);
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
	static SHChangeNotifyEntry NotifyEntry;
	static DWORD_PTR thread_affinity[4];
	static HFONT hyperlink_font = NULL;
	DRAWITEMSTRUCT* pDI;
	LPTOOLTIPTEXT lpttt;
	HDROP droppedFileInfo;
	POINT Point;
	RECT rc, DialogRect, DesktopRect;
	LONG progress_style;
	HDC hDC;
	PAINTSTRUCT ps;
	int nDeviceIndex, i, nWidth, nHeight, nb_devices, selected_language, offset;
	char tmp[128];
	wchar_t* wbuffer = NULL;
	loc_cmd* lcmd = NULL;
	wchar_t wtooltip[128];

	switch (message) {

	case WM_COMMAND:
#ifdef RUFUS_TEST
		if (LOWORD(wParam) == IDC_TEST) {
			ToggleImageSettings("blah.iso");
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
			if (ulRegister != 0)
				SHChangeNotifyDeregister(ulRegister);
			PostQuitMessage(0);
			StrArrayDestroy(&DriveID);
			StrArrayDestroy(&DriveLabel);
			StrArrayDestroy(&DriveHub);
			StrArrayDestroy(&BlockingProcess);
			StrArrayDestroy(&ImageList);
			DestroyAllTooltips();
			DestroyWindow(hLogDialog);
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
				GetWindowRect(hLogDialog, &DialogRect);
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
				MoveWindow(hLogDialog, Point.x, Point.y, nWidth, nHeight, FALSE);
				// The log may have been recentered to fit the screen, in which case, try to shift our main dialog left (or right for RTL)
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				if (right_to_left_mode) {
					Point.x = DialogRect.left;
					GetWindowRect(hLogDialog, &DialogRect);
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
			ShowWindow(hLogDialog, log_displayed?SW_SHOW:SW_HIDE);
			break;
		case IDC_ADVANCED_DRIVE_PROPERTIES:
			advanced_mode_device = !advanced_mode_device;
			WriteSettingBool(SETTING_ADVANCED_MODE_DEVICE, advanced_mode_device);
			ToggleAdvancedDeviceOptions(advanced_mode_device);
			SetBootOptions();
			bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			EnableControls(TRUE);
			SetFileSystemAndClusterSize(NULL);
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE << 16) | IDC_FILE_SYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_ADVANCED_FORMAT_OPTIONS:
			advanced_mode_format = !advanced_mode_format;
			WriteSettingBool(SETTING_ADVANCED_MODE_FORMAT, advanced_mode_format);
			ToggleAdvancedFormatOptions(advanced_mode_format);
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
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE<<16) | IDC_FILE_SYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			DeviceNum = (nDeviceIndex == CB_ERR) ? 0 : (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
			break;
		case IDC_IMAGE_OPTION:
			SetFileSystemAndClusterSize(NULL);
			break;
		case IDC_NB_PASSES:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetPassesTooltip();
			break;
		case IDC_TARGET_SYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			tt = (int)ComboBox_GetItemData(hTargetSystem, ComboBox_GetCurSel(hTargetSystem));
			SendMessage(hMainDialog, UM_UPDATE_CSM_TOOLTIP, 0, 0);
			SetFileSystemAndClusterSize(NULL);
			break;
		case IDC_PARTITION_TYPE:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
			SetPartitionSchemeAndTargetSystem(TRUE);
			SetFileSystemAndClusterSize(NULL);
			EnableMBRBootOptions(TRUE, FALSE);
			break;
		case IDC_FILE_SYSTEM:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			SetClusterSizes(fs);
			// Disable/restore the quick format control depending on large FAT32 or ReFS
			if ( ((fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32))) || (fs == FS_REFS) ) {
				if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_QUICK_FORMAT))) {
					uQFChecked = IsChecked(IDC_QUICK_FORMAT);
					CheckDlgButton(hMainDialog, IDC_QUICK_FORMAT, BST_CHECKED);
					EnableWindow(GetDlgItem(hMainDialog, IDC_QUICK_FORMAT), FALSE);
				}
			} else {
				if (!IsWindowEnabled(GetDlgItem(hMainDialog, IDC_QUICK_FORMAT))) {
					CheckDlgButton(hMainDialog, IDC_QUICK_FORMAT, uQFChecked);
					EnableWindow(GetDlgItem(hMainDialog, IDC_QUICK_FORMAT), TRUE);
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
			EnableMBRBootOptions(TRUE, FALSE);
			SetMBRProps();
			break;
		case IDC_BOOT_SELECTION:
			bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			if ((HIWORD(wParam) != CBN_SELCHANGE) || (bt == selection_default))
				break;
			selection_default = bt;
			SetPartitionSchemeAndTargetSystem(FALSE);
			SetFileSystemAndClusterSize(NULL);
			SetToGo();
			SetProposedLabel(ComboBox_GetCurSel(hDeviceList));
			EnableControls(TRUE);
			tt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
			pt = (int)ComboBox_GetItemData(hTargetSystem, ComboBox_GetCurSel(hTargetSystem));
			return (INT_PTR)TRUE;
		case IDC_SELECT:
			if (iso_provided) {
				uprintf("\r\nImage provided: '%s'", image_path);
				iso_provided = FALSE;	// One off thing...
			} else {
				char* old_image_path = image_path;
				// If declared globaly, lmprintf(MSG_036) would be called on each message...
				EXT_DECL(img_ext, NULL, __VA_GROUP__("*.iso;*.img;*.vhd;*.gz;*.bzip2;*.bz2;*.xz;*.lzma;*.Z;*.zip"),
					__VA_GROUP__(lmprintf(MSG_036)));
				image_path = FileDialog(FALSE, NULL, &img_ext, 0);
				if (image_path == NULL) {
					if (old_image_path != NULL) {
						// Reselect previous image
						image_path = old_image_path;
					} else {
						CreateTooltip(hSelectImage, lmprintf(MSG_173), -1);
						PrintStatus(0, MSG_086);
					}
					break;
				} else {
					free(old_image_path);
				}
			}
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
		case IDC_LIST_USB_HDD:
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
			// Just in case
			bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			tt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
			pt = (int)ComboBox_GetItemData(hTargetSystem, ComboBox_GetCurSel(hTargetSystem));
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			write_as_image = FALSE;
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
					if ((bt != BT_NON_BOOTABLE) && (!BootCheck()))
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

					if ((bt == BT_IMAGE) && IS_DD_BOOTABLE(img_report)) {
						if (img_report.is_iso) {
							// Ask users how they want to write ISOHybrid images
							char* iso_image = lmprintf(MSG_036);
							char* dd_image = lmprintf(MSG_095);
							char* choices[2] = { lmprintf(MSG_276, iso_image), lmprintf(MSG_277, dd_image) };
							i = SelectionDialog(lmprintf(MSG_274), lmprintf(MSG_275, iso_image, dd_image, iso_image, dd_image),
								choices, 2);
							if (i < 0)	// Cancel
								goto aborted_start;
							else if (i == 2)
								write_as_image = TRUE;
						} else {
							write_as_image = TRUE;
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
				if ((!zero_drive) && (bt != BT_NON_BOOTABLE) && (SelectedDrive.SectorSize != 512) &&
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
		case IDC_LANG:
			// Show the language menu such that it doesn't overlap the button
			SendMessage(hMultiToolbar, TB_GETRECT, (WPARAM)IDC_ABOUT, (LPARAM)&rc);
			MapWindowPoints(hDlg, NULL, (POINT*)&rc, 2);
			rc.left += cbw / 2;
			ShowLanguageMenu(rc);
			break;
		case IDC_SETTINGS:
			MyDialogBox(hMainInstance, IDD_UPDATE_POLICY, hDlg, UpdateCallback);
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
		case IDC_SAVE:
			SaveVHD();
			break;
		default:
			return (INT_PTR)FALSE;
		}
		return (INT_PTR)TRUE;

	case UM_UPDATE_CSM_TOOLTIP:
		ShowWindow(GetDlgItem(hMainDialog, IDS_CSM_HELP_TXT), ((tt == TT_UEFI) || has_uefi_csm) ? SW_SHOW : SW_HIDE);
		CreateTooltip(GetDlgItem(hMainDialog, IDS_CSM_HELP_TXT), lmprintf((tt == TT_UEFI) ? MSG_152 : MSG_151), 30000);
		break;
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
				LastRefresh = GetTickCount64();
				KillTimer(hMainDialog, TID_REFRESH_TIMER);
				if (!format_op_in_progress) {
					queued_hotplug_event = FALSE;
					GetDevices((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
					user_changed_label = FALSE;
					EnableControls(TRUE);
					if (ComboBox_GetCurSel(hDeviceList) < 0) {
						SetPartitionSchemeAndTargetSystem(FALSE);
						SetFileSystemAndClusterSize(NULL);
						ShowWindow(GetDlgItem(hDlg, IDS_CSM_HELP_TXT), SW_HIDE);
					}
				} else {
					queued_hotplug_event = TRUE;
				}
				return (INT_PTR)TRUE;
			case DBT_DEVNODES_CHANGED:
				// If it's been more than a second since last device refresh, arm a refresh timer
				if (GetTickCount64() > LastRefresh + 1000) {
					LastRefresh = GetTickCount64();
					SetTimer(hMainDialog, TID_REFRESH_TIMER, 1000, RefreshTimer);
				}
				break;
			default:
				break;
			}
		}
		break;

	case WM_INITDIALOG:
		// Make sure fScale is set before the first call to apply localization, so that move/resize scale appropriately
		hDC = GetDC(hDlg);
		fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
		safe_release_dc(hDlg, hDC);
		apply_localization(IDD_DIALOG, hDlg);
		SetUpdateCheck();
		display_togo_option = TRUE;	// We display the ToGo controls by default and need to hide them
		first_log_display = TRUE;
		log_displayed = FALSE;
		hLogDialog = MyCreateDialog(hMainInstance, IDD_LOG, hDlg, (DLGPROC)LogCallback);
		InitDialog(hDlg);
		GetDevices(0);
		EnableControls(TRUE);
		CheckForUpdates(FALSE);
		// Register MEDIA_INSERTED/MEDIA_REMOVED notifications for card readers
		if (SUCCEEDED(SHGetSpecialFolderLocation(0, CSIDL_DESKTOP, &pidlDesktop))) {
			NotifyEntry.pidl = pidlDesktop;
			NotifyEntry.fRecursive = TRUE;
			// NB: The following only works if the media is already formatted.
			// If you insert a blank card, notifications will not be sent... :(
			ulRegister = SHChangeNotifyRegister(hDlg, 0x0001 | 0x0002 | 0x8000,
				SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED, UM_MEDIA_CHANGE, 1, &NotifyEntry);
		}
		// Bring our Window on top. We have to go through all *THREE* of these, or Far Manager hides our window :(
		SetWindowPos(hMainDialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowPos(hMainDialog, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowPos(hMainDialog, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

		// Set 'START' as the selected button if it's enabled, otherwise use 'SELECT', instead
		SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)(IsWindowEnabled(hStart) ? hStart : hSelectImage), TRUE);

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
			pDI->rcItem.left += (int)(4.0f * fScale);
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

	case WM_PAINT:
		hDC = BeginPaint(hDlg, &ps);
		OnPaint(hDC);
		EndPaint(hDlg, &ps);
		break;

	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam != GetDlgItem(hDlg, IDS_CSM_HELP_TXT))
			return FALSE;
		SetBkMode((HDC)wParam, TRANSPARENT);
		CreateStaticFont((HDC)wParam, &hyperlink_font, FALSE);
		SelectObject((HDC)wParam, hyperlink_font);
		SetTextColor((HDC)wParam, RGB(0, 0, 125));	// DARK_BLUE
		return (INT_PTR)CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case TTN_GETDISPINFO:
			lpttt = (LPTOOLTIPTEXT)lParam;
			switch (lpttt->hdr.idFrom) {
			case IDC_ABOUT:
				utf8_to_wchar_no_alloc(lmprintf(MSG_302), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
			case IDC_SETTINGS:
				utf8_to_wchar_no_alloc(lmprintf(MSG_301), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
			case IDC_LANG:
				utf8_to_wchar_no_alloc(lmprintf(MSG_273), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
			case IDC_LOG:
				utf8_to_wchar_no_alloc(lmprintf(MSG_303), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
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
			// Simulate image selection click
			SendMessage(hDlg, WM_COMMAND, IDC_SELECT, 0);
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
		progress_in_use = TRUE;
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
		progress_in_use = FALSE;
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
	vc |= (safe_strcmp(GetSignatureName(NULL, NULL), cert_name[0]) == 0);
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
	advanced_mode_device = ReadSettingBool(SETTING_ADVANCED_MODE_DEVICE);
	advanced_mode_format = ReadSettingBool(SETTING_ADVANCED_MODE_FORMAT);
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
	safe_release_dc(NULL, hDC);

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

	// Set the Windows version
	GetWindowsVersion();

	// ...and nothing of value was lost
	if (nWindowsVersion < WINDOWS_7) {
		// Load the translation before we print the error
		get_loc_data_file(loc_file, selected_locale);
		right_to_left_mode = ((selected_locale->ctrl_id) & LOC_RIGHT_TO_LEFT);
		// Set MB_SYSTEMMODAL to prevent Far Manager from stealing focus...
		MessageBoxExU(NULL, lmprintf(MSG_294), lmprintf(MSG_293), MB_ICONSTOP | MB_IS_RTL | MB_SYSTEMMODAL, selected_langid);
		goto out;
	}

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

	// Increase the application privileges (SE_DEBUG_PRIVILEGE), so that we can report
	// the Windows Services preventing access to the disk or volume we want to format.
	EnablePrivileges();

	// We use local group policies rather than direct registry manipulation
	// 0x9e disables removable and fixed drive notifications
	lgp_set = SetLGP(FALSE, &existing_key, ep_reg, "NoDriveTypeAutorun", 0x9e);

	// Re-enable AutoMount if needed
	if (!GetAutoMount(&automount)) {
		uprintf("Could not get AutoMount status");
		automount = TRUE;	// So that we don't try to change its status on exit
	} else if (!automount) {
		uprintf("AutoMount was detected as disabled - temporarily re-enabling it");
		if (!SetAutoMount(TRUE))
			uprintf("Failed to enable AutoMount");
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

	// Enable drag-n-drop through the message filter
	ChangeWindowMessageFilter(WM_DROPFILES, MSGFLT_ADD);
	ChangeWindowMessageFilter(WM_COPYDATA, MSGFLT_ADD);
	// CopyGlobalData is needed sine we are running elevated
	ChangeWindowMessageFilter(WM_COPYGLOBALDATA, MSGFLT_ADD);

	// Set the hook to automatically close Windows' "You need to format the disk in drive..." prompt
	if (!SetFormatPromptHook())
		uprintf("Warning: Could not set 'Format Disk' prompt auto-close");

	ShowWindow(hDlg, SW_SHOWNORMAL);
	UpdateWindow(hDlg);

	// Do our own event processing and process "magic" commands
	while(GetMessage(&msg, NULL, 0, 0)) {
		// ** *****  **** ** ***** ****
		// .,ABCDEFGHIJKLMNOPQRSTUVWXYZ

		// Ctrl-A => Select the log data
		if ( (IsWindowVisible(hLogDialog)) && (GetKeyState(VK_CONTROL) & 0x8000) &&
			(msg.message == WM_KEYDOWN) && (msg.wParam == 'A') ) {
			// Might also need ES_NOHIDESEL property if you want to select when not active
			Edit_SetSel(hLog, 0, -1);
			continue;
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
		// Alt-C => Cycle USB port for currently selected device
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'C')) {
			int index = ComboBox_GetCurSel(hDeviceList);
			if (index >= 0)
				ResetDevice(index);
			continue;
		}
		// Alt-D => Delete the 'rufus_files' subdirectory
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'D')) {
			static_sprintf(tmp_path, "%s\\%s", app_dir, FILES_DIR);
			PrintStatus(2000, MSG_264, tmp_path);
			SHDeleteDirectoryExU(NULL, tmp_path, FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION);
			continue;
		}
		// Alt-E => Enhanced installation mode (allow dual UEFI/BIOS mode and FAT32 for Windows)
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'E')) {
			allow_dual_uefi_bios = !allow_dual_uefi_bios;
			WriteSettingBool(SETTING_ENABLE_WIN_DUAL_EFI_BIOS, allow_dual_uefi_bios);
			PrintStatusTimeout(lmprintf(MSG_266), allow_dual_uefi_bios);
			SetPartitionSchemeAndTargetSystem(FALSE);
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
			CheckDlgButton(hMainDialog, IDC_LIST_USB_HDD, enable_HDDs?BST_CHECKED:BST_UNCHECKED);
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
				SendMessage(hDlg, WM_COMMAND, IDC_SELECT, 0);
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
		// Alt-W => Enable VMWare disk detection
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'W')) {
			enable_vmdk = !enable_vmdk;
			WriteSettingBool(SETTING_ENABLE_VMDK_DETECTION, enable_vmdk);
			PrintStatusTimeout(lmprintf(MSG_265), enable_vmdk);
			GetDevices(0);
			continue;
		}
		// Alt-X => Delete the NoDriveTypeAutorun key on exit (useful if the app crashed)
		// This key is used to disable Windows popup messages when an USB drive is plugged in.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'X')) {
			PrintStatus(2000, MSG_255);
			existing_key = FALSE;
			continue;
		}
		// Alt Y => Force the update check to be successful
		// This will set the reported current version of Rufus to 0.0.0.0 when performing an update
		// check, so that it always succeeds. This is useful for translators.
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Y')) {
			force_update = !force_update;
			PrintStatusTimeout(lmprintf(MSG_259), force_update);
			continue;
		}
		// Alt-Z => Zero the drive
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Z')) {
			zero_drive = TRUE;
			// Simulate a button click for Start
			PostMessage(hDlg, WM_COMMAND, (WPARAM)IDC_START, 0);
			continue;
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
			CheckDlgButton(hMainDialog, IDC_LIST_USB_HDD, enable_HDDs ? BST_CHECKED : BST_UNCHECKED);
			PrintStatusTimeout(lmprintf(MSG_287), list_non_usb_removable_drives);
			uprintf("%sListing of non-USB removable drives %s",
				(list_non_usb_removable_drives)?"CAUTION: ":"", (list_non_usb_removable_drives)?"enabled":"disabled");
			if (list_non_usb_removable_drives)
				uprintf("By using this unofficial cheat mode you forfeit ANY RIGHT to complain if you lose valuable data!");
			GetDevices(0);
			continue;
		}

		// Let the system handle dialog messages (e.g. those from the tab key)
		if (!IsDialogMessage(hDlg, &msg) && !IsDialogMessage(hLogDialog, &msg)) {
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
	if ((!automount) && (!SetAutoMount(FALSE)))
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
