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
#include <dwmapi.h>
#include <dbt.h>
#include <io.h>
#include <getopt.h>
#include <assert.h>

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
static int selected_pt = -1, selected_fs = FS_UNKNOWN, preselected_fs = FS_UNKNOWN;
static int image_index = 0;
static RECT relaunch_rc = { -65536, -65536, 0, 0};
static UINT uQFChecked = BST_CHECKED, uMBRChecked = BST_UNCHECKED;
static HANDLE format_thid = NULL, dialog_handle = NULL;
static HWND hSelectImage = NULL, hStart = NULL;
static char szTimer[12] = "00:00:00";
static unsigned int timer;
static char uppercase_select[64], uppercase_start[64], uppercase_close[64], uppercase_cancel[64];

extern BOOL enable_iso, enable_joliet, enable_rockridge, enable_ntfs_compression;
extern uint8_t* grub2_buf;
extern long grub2_len;
extern char* szStatusMessage;
extern const char* old_c32_name[NB_OLD_C32];
extern const char* cert_name[3];

/*
 * Globals
 */
OPENED_LIBRARIES_VARS;
RUFUS_UPDATE update = { { 0,0,0 },{ 0,0 }, NULL, NULL };
HINSTANCE hMainInstance;
HWND hMainDialog, hMultiToolbar, hSaveToolbar, hHashToolbar, hAdvancedDeviceToolbar, hAdvancedFormatToolbar, hUpdatesDlg = NULL;
HFONT hInfoFont;
uint8_t image_options = IMOP_WINTOGO;
uint16_t rufus_version[3], embedded_sl_version[2];
uint32_t dur_mins, dur_secs, DrivePort[MAX_DRIVES];;
loc_cmd* selected_locale = NULL;
WORD selected_langid = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
DWORD MainThreadId;
HWND hDeviceList, hPartitionScheme, hTargetSystem, hFileSystem, hClusterSize, hLabel, hBootType, hNBPasses, hLog = NULL;
HWND hLogDialog = NULL, hProgress = NULL, hDiskID;
BOOL is_x86_32, use_own_c32[NB_OLD_C32] = { FALSE, FALSE }, mbr_selected_by_user = FALSE;
BOOL iso_op_in_progress = FALSE, format_op_in_progress = FALSE, right_to_left_mode = FALSE, has_uefi_csm;
BOOL enable_HDDs = FALSE, force_update = FALSE, enable_ntfs_compression = FALSE, no_confirmation_on_cancel = FALSE, lock_drive = TRUE;
BOOL advanced_mode_device, advanced_mode_format, allow_dual_uefi_bios, detect_fakes, enable_vmdk, force_large_fat32, usb_debug;
BOOL use_fake_units, preserve_timestamps = FALSE, fast_zeroing = FALSE, app_changed_size = FALSE;
BOOL zero_drive = FALSE, list_non_usb_removable_drives = FALSE, enable_file_indexing, large_drive = FALSE;
BOOL write_as_image = FALSE, installed_uefi_ntfs;
uint64_t persistence_size = 0;
float fScale = 1.0f;
int dialog_showing = 0, selection_default = BT_IMAGE, windows_to_go_selection = 0, persistence_unit_selection = -1;
int default_fs, fs, bt, pt, tt; // file system, boot type, partition type, target type
char szFolderPath[MAX_PATH], app_dir[MAX_PATH], system_dir[MAX_PATH], temp_dir[MAX_PATH], sysnative_dir[MAX_PATH];
char embedded_sl_version_str[2][12] = { "?.??", "?.??" };
char embedded_sl_version_ext[2][32];
char ClusterSizeLabel[MAX_CLUSTER_SIZES][64];
char msgbox[1024], msgbox_title[32], *ini_file = NULL, *image_path = NULL, *short_image_path;
char image_option_txt[128];
StrArray DriveID, DriveLabel, DriveHub, BlockingProcess, ImageList;
// Number of steps for each FS for FCC_STRUCTURE_PROGRESS
const int nb_steps[FS_MAX] = { 5, 5, 12, 1, 10 };
const char* flash_type[BADLOCKS_PATTERN_TYPES] = { "SLC", "MLC", "TLC" };

// TODO: Remember to update copyright year in stdlg's AboutCallback() WM_INITDIALOG,
// localization_data.sh and the .rc when the year changes!

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
		allowed_filesystem[FS_NTFS] = TRUE;
		// Don't allow anything besides NTFS if the image has a >4GB file
		if ((image_path != NULL) && (img_report.has_4GB_file))
			break;
		if (!HAS_WINDOWS(img_report) || (tt != TT_BIOS) || allow_dual_uefi_bios) {
			if (!HAS_WINTOGO(img_report) || (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) != 1)) {
				allowed_filesystem[FS_FAT16] = TRUE;
				allowed_filesystem[FS_FAT32] = TRUE;
			}
		}
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
		// Override partition type selection to GPT for drives larger than 2TB
		if (SelectedDrive.DiskSize > 2 * TB)
			selected_pt = PARTITION_STYLE_GPT;
		// Try to reselect the current drive's partition scheme
		int preferred_pt = SelectedDrive.PartitionStyle;
		if (allowed_partition_scheme[PARTITION_STYLE_MBR]) 
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, "MBR"), PARTITION_STYLE_MBR));
		if (allowed_partition_scheme[PARTITION_STYLE_GPT])
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, "GPT"), PARTITION_STYLE_GPT));
		if (allowed_partition_scheme[PARTITION_STYLE_SFD])
			IGNORE_RETVAL(ComboBox_SetItemData(hPartitionScheme,
				ComboBox_AddStringU(hPartitionScheme, sfd_name), PARTITION_STYLE_SFD));
		// Override the partition scheme according to the current 
		if (bt == BT_NON_BOOTABLE)
			preferred_pt = (selected_pt >= 0) ? selected_pt : PARTITION_STYLE_MBR;
		else if (bt == BT_UEFI_NTFS)
			preferred_pt = (selected_pt >= 0) ? selected_pt : PARTITION_STYLE_GPT;
		else if ((bt == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso)) {
			if (HAS_WINDOWS(img_report) && img_report.has_efi)
				preferred_pt = allow_dual_uefi_bios? PARTITION_STYLE_MBR :
					((selected_pt >= 0) ? selected_pt : PARTITION_STYLE_GPT);
			if (img_report.is_bootable_img)
				preferred_pt = (selected_pt >= 0) ? selected_pt : PARTITION_STYLE_MBR;
		}
		SetComboEntry(hPartitionScheme, preferred_pt);
		pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
	}

	has_uefi_csm = FALSE;
	if (allowed_target_system[0] && (pt != PARTITION_STYLE_GPT)) {
		IGNORE_RETVAL(ComboBox_SetItemData(hTargetSystem,
			ComboBox_AddStringU(hTargetSystem, lmprintf(MSG_031)), TT_BIOS));
		has_uefi_csm = TRUE;
	}
	if (allowed_target_system[1] && !((pt == PARTITION_STYLE_MBR) && (bt == BT_IMAGE) && IS_BIOS_BOOTABLE(img_report) && IS_EFI_BOOTABLE(img_report)) )
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
		// Re-select last user-selected FS
		SelectedDrive.FSType = selected_fs;
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
	int i, fs_tmp, preferred_fs = FS_UNKNOWN;
	uint32_t fs_mask = 0;
	BOOL windows_to_go = (image_options & IMOP_WINTOGO) && (bt == BT_IMAGE) && HAS_WINTOGO(img_report) &&
		(ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1);

	if (image_path == NULL)
		return;

	// Create a mask of all the FS's available
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs_tmp = (int)ComboBox_GetItemData(hFileSystem, i);
		fs_mask |= 1<<fs_tmp;
	}

	if ((preferred_fs == FS_UNKNOWN) && (preselected_fs != FS_UNKNOWN)) {
		// If the FS requested from the command line is valid use it
		if (fs_mask & (1 << preselected_fs)) {
			preferred_fs = preselected_fs;
		}
	}

	if (preferred_fs == FS_UNKNOWN) {
		// Syslinux and EFI have precedence over bootmgr (unless the user selected BIOS as target type)
		if ((HAS_SYSLINUX(img_report)) || (HAS_REACTOS(img_report)) || HAS_KOLIBRIOS(img_report) ||
			(IS_EFI_BOOTABLE(img_report) && (tt == TT_UEFI) && (!windows_to_go))) {
			if (fs_mask & (1 << FS_FAT32)) {
				preferred_fs = FS_FAT32;
			} else if ((fs_mask & (1 << FS_FAT16)) && !HAS_KOLIBRIOS(img_report)) {
				preferred_fs = FS_FAT16;
			}
		} else if ((windows_to_go) || HAS_BOOTMGR(img_report) || HAS_WINPE(img_report)) {
			if (fs_mask & (1 << FS_NTFS)) {
				preferred_fs = FS_NTFS;
			}
		}
	}

	// The presence of a 4GB file forces the use of NTFS as default FS if available
	if (img_report.has_4GB_file && (fs_mask & (1 << FS_NTFS))) {
		preferred_fs = FS_NTFS;
	}

	// Try to select the FS
	for (i=0; i<ComboBox_GetCount(hFileSystem); i++) {
		fs_tmp = (int)ComboBox_GetItemData(hFileSystem, i);
		if (fs_tmp == preferred_fs)
			IGNORE_RETVAL(ComboBox_SetCurSel(hFileSystem, i));
	}

	SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE_INTERNAL<<16) | IDC_FILE_SYSTEM,
		ComboBox_GetCurSel(hFileSystem));
}

static void SetMBRProps(void)
{
	BOOL needs_masquerading = HAS_WINPE(img_report) && (!img_report.uses_minint);

	if ((!mbr_selected_by_user) && ((image_path == NULL) || (bt != BT_IMAGE) || (fs != FS_NTFS) || HAS_GRUB(img_report) ||
		((image_options & IMOP_WINTOGO) && (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1)) )) {
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, BST_UNCHECKED);
		IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, 0));
		return;
	}

	uMBRChecked = (needs_masquerading || HAS_BOOTMGR(img_report) || mbr_selected_by_user)?BST_CHECKED:BST_UNCHECKED;
	if (IsWindowEnabled(GetDlgItem(hMainDialog, IDC_RUFUS_MBR)))
		CheckDlgButton(hMainDialog, IDC_RUFUS_MBR, uMBRChecked);
	IGNORE_RETVAL(ComboBox_SetCurSel(hDiskID, needs_masquerading?1:0));
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

static void EnableQuickFormat(BOOL enable)
{
	HWND hCtrl = GetDlgItem(hMainDialog, IDC_QUICK_FORMAT);

	// Keep track of the current state if we are going to disable it
	if (IsWindowEnabled(hCtrl) && !enable) {
		uQFChecked = IsChecked(IDC_QUICK_FORMAT);
	}

	// Disable/restore the quick format control depending on large FAT32 or ReFS
	if (((fs == FS_FAT32) && ((SelectedDrive.DiskSize > LARGE_FAT32_SIZE) || (force_large_fat32))) || (fs == FS_REFS)) {
		enable = FALSE;
		CheckDlgButton(hMainDialog, IDC_QUICK_FORMAT, BST_CHECKED);
	}

	// Restore state if we are re-enabling the control
	if (!IsWindowEnabled(hCtrl) && enable) {
		CheckDlgButton(hMainDialog, IDC_QUICK_FORMAT, uQFChecked);
	}

	// Now enable or disable the control
	EnableWindow(hCtrl, enable);
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
	EnableWindow(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SLIDER), actual_enable);
	// Make sure we set the range if we have persistence
	if ((image_path != NULL) && HAS_PERSISTENCE(img_report))
		SetPersistenceSize();
	EnableWindow(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE), (persistence_size != 0) && actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_PERSISTENCE_UNITS), (persistence_size != 0) && actual_enable);
	EnableMBRBootOptions(actual_enable, remove_checkboxes);

	EnableWindow(GetDlgItem(hMainDialog, IDC_LABEL), actual_enable);
	EnableQuickFormat(actual_enable);
	EnableWindow(GetDlgItem(hMainDialog, IDC_BAD_BLOCKS), actual_enable_bb);
	EnableWindow(GetDlgItem(hMainDialog, IDC_NB_PASSES), actual_enable_bb);
	EnableWindow(GetDlgItem(hMainDialog, IDC_EXTENDED_LABEL), actual_enable);
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
	EnableWindow(hHashToolbar, bEnable && (bt == BT_IMAGE) && (image_path != NULL));

	// Toggle CLOSE/CANCEL
	SetDlgItemTextU(hMainDialog, IDCANCEL, bEnable ? uppercase_close : uppercase_cancel);

	// Only enable the following controls if a device is active
	bEnable = (ComboBox_GetCurSel(hDeviceList) < 0) ? FALSE : bEnable;
	EnableWindow(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION), bEnable);
	EnableWindow(hSaveToolbar, bEnable);

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
static BOOL PopulateProperties(void)
{
	char* device_tooltip;
	int device_index = ComboBox_GetCurSel(hDeviceList);
	char fs_type[32];

	memset(&SelectedDrive, 0, sizeof(SelectedDrive));
	EnableWindow(hStart, FALSE);

	if (device_index < 0)
		goto out;

	persistence_unit_selection = -1;
	// Get data from the currently selected drive
	SelectedDrive.DeviceNumber = (DWORD)ComboBox_GetItemData(hDeviceList, device_index);
	// This fills the SelectedDrive properties
	GetDrivePartitionData(SelectedDrive.DeviceNumber, fs_type, sizeof(fs_type), FALSE);
	SetPartitionSchemeAndTargetSystem(FALSE);
	// Attempt to reselect the last file system explicitly set by the user
	if (!SetFileSystemAndClusterSize((selected_fs == FS_UNKNOWN) ? fs_type : NULL)) {
		SetProposedLabel(-1);
		uprintf("No file system is selectable for this drive\n");
		return FALSE;
	}

	EnableControls(TRUE);

	// Set a proposed label according to the size (eg: "256MB", "8GB")
	static_sprintf(SelectedDrive.proposed_label,
		SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, use_fake_units));

	// Add a tooltip (with the size of the device in parenthesis)
	device_tooltip = (char*) malloc(safe_strlen(DriveID.String[device_index]) + 32);
	if (device_tooltip != NULL) {
		if (right_to_left_mode)
			safe_sprintf(device_tooltip, safe_strlen(DriveID.String[device_index]) + 32, "(%s) %s",
				SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, FALSE), DriveID.String[device_index]);
		else
			safe_sprintf(device_tooltip, safe_strlen(DriveID.String[device_index]) + 32, "%s (%s)",
				DriveID.String[device_index], SizeToHumanReadable(SelectedDrive.DiskSize, FALSE, FALSE));
		CreateTooltip(hDeviceList, device_tooltip, -1);
		free(device_tooltip);
	}

out:
	SetProposedLabel(device_index);
	return TRUE;
}

// Callback for the log window
BOOL CALLBACK LogCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	HFONT hf;
	LONG lfHeight;
	LONG_PTR style;
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
		style = GetWindowLongPtr(hLog, GWL_EXSTYLE);
		style &= ~(WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR);
		SetWindowLongPtr(hLog, GWL_EXSTYLE, style);
		style = GetWindowLongPtr(hLog, GWL_STYLE);
		style &= ~(ES_RIGHT);
		SetWindowLongPtr(hLog, GWL_STYLE, style);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCANCEL:
			ShowWindow(hDlg, SW_HIDE);
			log_displayed = FALSE;
			// Set focus to the Cancel button on the main dialog
			// This avoids intempestive tooltip display from the log toolbar buttom
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDCANCEL), TRUE);
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
		// Set focus to the Cancel button on the main dialog
		// This avoids intempestive tooltip display from the log toolbar buttom
		SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDCANCEL), TRUE);
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
	static char inst_str[] = " [1/#]";
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
	PRINT_ISO_PROP(HAS_BOOTMGR(img_report), "  Uses: Bootmgr (%s)",
		HAS_BOOTMGR_BIOS(img_report) ? (HAS_BOOTMGR_EFI(img_report) ? "BIOS and UEFI" : "BIOS only") : "UEFI only");
	PRINT_ISO_PROP(HAS_WINPE(img_report), "  Uses: WinPE %s", (img_report.uses_minint) ? "(with /minint)" : "");
	if (HAS_WININST(img_report)) {
		inst_str[4] = '0' + img_report.wininst_index;
		uprintf("  Uses: Install.%s%s (version %d.%d.%d%s)", &img_report.wininst_path[0][strlen(img_report.wininst_path[0]) - 3],
			(img_report.wininst_index > 1) ? inst_str : "", (img_report.wininst_version >> 24) & 0xff,
			(img_report.wininst_version >> 16) & 0xff, (img_report.wininst_version >> 8) & 0xff,
			(img_report.wininst_version >= SPECIAL_WIM_VERSION) ? "+": "");
	}
	PRINT_ISO_PROP(img_report.has_symlinks, "  Note: This ISO uses symbolic links, which will not be replicated due to file system limitations.");
	PRINT_ISO_PROP(img_report.has_symlinks, "  Because of this, some features from this image may not work...");
}

// Insert the image name into the Boot selection dropdown
static void UpdateImage(void)
{
	assert(image_index != 0);

	if (ComboBox_GetItemData(hBootType, image_index) == BT_IMAGE)
		ComboBox_DeleteString(hBootType, image_index);
	ComboBox_InsertStringU(hBootType, image_index,
		(image_path == NULL) ? lmprintf(MSG_281, lmprintf(MSG_280)) : short_image_path);
	ComboBox_SetItemData(hBootType, image_index, BT_IMAGE);
	IGNORE_RETVAL(ComboBox_SetCurSel(hBootType, image_index));
	bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
	SetBootTypeDropdownWidth();
}

// The scanning process can be blocking for message processing => use a thread
DWORD WINAPI ISOScanThread(LPVOID param)
{
	int i;

	if (image_path == NULL)
		goto out;
	format_op_in_progress = TRUE;
	PrintInfoDebug(0, MSG_202);
	user_notified = FALSE;
	EnableControls(FALSE);
	memset(&img_report, 0, sizeof(img_report));
	img_report.is_iso = (BOOLEAN)ExtractISO(image_path, "", TRUE);
	img_report.is_bootable_img = (BOOLEAN)IsBootableImage(image_path);

	if ((FormatStatus == (ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | ERROR_CANCELLED)) ||
		(img_report.image_size == 0) || (!img_report.is_iso && !img_report.is_bootable_img)) {
		// Failed to scan image
		SendMessage(hMainDialog, UM_PROGRESS_EXIT, 0, 0);
		safe_free(image_path);
		UpdateImage();
		SetMBRProps();
		PopulateProperties();
		PrintInfoDebug(0, MSG_203);
		PrintStatus(0, MSG_203);
		EnableControls(TRUE);
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
			if (i != 0)
				i++;
			short_image_path = &image_path[i];
			PrintStatus(0, MSG_205, short_image_path);
			UpdateImage();
			uprintf("Using image: %s (%s)", short_image_path, SizeToHumanReadable(img_report.image_size, FALSE, FALSE));
		}
		ToggleImageOptions();
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
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE_INTERNAL<<16) | IDC_FILE_SYSTEM,
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
	format_op_in_progress = FALSE;
	PrintInfo(0, MSG_210);
	ExitThread(0);
}

// Likewise, boot check will block message processing => use a thread
static DWORD WINAPI BootCheckThread(LPVOID param)
{
	int i, r;
	FILE *fd;
	DWORD len;
	WPARAM ret = -1;
	BOOL in_files_dir = FALSE;
	const char* grub = "grub";
	const char* core_img = "core.img";
	const char* ldlinux = "ldlinux";
	const char* syslinux = "syslinux";
	const char* ldlinux_ext[3] = { "sys", "bss", "c32" };
	char tmp[MAX_PATH], tmp2[MAX_PATH];

	syslinux_ldlinux_len[0] = 0; syslinux_ldlinux_len[1] = 0;
	safe_free(grub2_buf);

	if (ComboBox_GetCurSel(hDeviceList) == CB_ERR)
		goto out;

	if ((zero_drive) || (bt == BT_NON_BOOTABLE)) {
		// Nothing to check
		ret = 0;
		goto out;
	}

	if (bt == BT_IMAGE) {
		assert(image_path != NULL);
		if (image_path == NULL)
			goto out;
		if ((size_check) && (img_report.projected_size > (uint64_t)SelectedDrive.DiskSize)) {
			// This ISO image is too big for the selected target
			MessageBoxExU(hMainDialog, lmprintf(MSG_089), lmprintf(MSG_088), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
		}
		if (IS_DD_BOOTABLE(img_report) && !img_report.is_iso) {
			// Pure DD images are fine at this stage
			ret = 0;
			goto out;
		}
		if ((image_options & IMOP_WINTOGO) && (ComboBox_GetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION)) == 1)) {
			if (fs != FS_NTFS) {
				// Windows To Go only works for NTFS
				MessageBoxExU(hMainDialog, lmprintf(MSG_097, "Windows To Go"), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
				goto out;
			}
			if (SelectedDrive.MediaType != FixedMedia) {
				if ((tt == TT_UEFI) && (pt == PARTITION_STYLE_GPT) && (nWindowsBuildNumber < 15000)) {
					// Up to Windows 10 Creators Update, we were screwed, since we need access to 2 partitions at the same time.
					// Thankfully, the newer Windows allow mounting multiple partitions on the same REMOVABLE drive.
					MessageBoxExU(hMainDialog, lmprintf(MSG_198), lmprintf(MSG_190), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
					goto out;
				}
			}
			// If multiple versions are available, asks the user to select one before we commit to format the drive
			switch(SetWinToGoIndex()) {
			case -1:
				MessageBoxExU(hMainDialog, lmprintf(MSG_073), lmprintf(MSG_291), MB_OK | MB_ICONERROR | MB_IS_RTL, selected_langid);
				// fall through
			case -2:
				goto out;
			default:
				break;
			}
		} else if (tt == TT_UEFI) {
			if (!IS_EFI_BOOTABLE(img_report)) {
				// Unsupported ISO
				MessageBoxExU(hMainDialog, lmprintf(MSG_091), lmprintf(MSG_090), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
				goto out;
			}
			if (HAS_WIN7_EFI(img_report) && (!WimExtractCheck())) {
				// Your platform cannot extract files from WIM archives => download 7-zip?
				if (MessageBoxExU(hMainDialog, lmprintf(MSG_102), lmprintf(MSG_101), MB_YESNO|MB_ICONERROR|MB_IS_RTL, selected_langid) == IDYES)
					ShellExecuteA(hMainDialog, "open", SEVENZIP_URL, NULL, NULL, SW_SHOWNORMAL);
				goto out;
			}
		} else if ( ((fs == FS_NTFS) && !HAS_WINDOWS(img_report) && !HAS_GRUB(img_report) && 
					 (!HAS_SYSLINUX(img_report) || (SL_MAJOR(img_report.sl_version) <= 5)))
				 || ((IS_FAT(fs)) && (!HAS_SYSLINUX(img_report)) && (!allow_dual_uefi_bios) && !IS_EFI_BOOTABLE(img_report) &&
					 (!HAS_REACTOS(img_report)) && !HAS_KOLIBRIOS(img_report) && (!HAS_GRUB(img_report)))
				 || ((IS_FAT(fs)) && (HAS_WINDOWS(img_report) || HAS_WININST(img_report)) && (!allow_dual_uefi_bios)) ) {
			// Incompatible FS and ISO
			MessageBoxExU(hMainDialog, lmprintf(MSG_096), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
		} else if ((fs == FS_FAT16) && HAS_KOLIBRIOS(img_report)) {
			// KolibriOS doesn't support FAT16
			MessageBoxExU(hMainDialog, lmprintf(MSG_189), lmprintf(MSG_099), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
		}
		if ((IS_FAT(fs)) && (img_report.has_4GB_file)) {
			// This ISO image contains a file larger than 4GB file (FAT32)
			MessageBoxExU(hMainDialog, lmprintf(MSG_100), lmprintf(MSG_099), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
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
					goto out;
				else if (r == IDYES) {
					static_sprintf(tmp, "%s-%s", grub, img_report.grub2_version);
					IGNORE_RETVAL(_mkdir(tmp));
					IGNORE_RETVAL(_chdir(tmp));
					static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, grub, img_report.grub2_version, core_img);
					grub2_len = (long)DownloadSignedFile(tmp, core_img, hMainDialog, FALSE);
					if ((grub2_len == 0) && (DownloadStatus == 404)) {
						// Couldn't locate the file on the server => try to download without the version extra
						uprintf("Extended version was not found, trying main version...");
						static_strcpy(tmp2, img_report.grub2_version);
						// Isolate the #.### part
						for (i = 0; ((tmp2[i] >= '0') && (tmp2[i] <= '9')) || (tmp2[i] == '.'); i++);
						tmp2[i] = 0;
						static_sprintf(tmp, "%s/%s-%s/%s", FILES_URL, grub, tmp2, core_img);
						grub2_len = (long)DownloadSignedFile(tmp, core_img, hMainDialog, FALSE);
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
								len = DownloadSignedFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog, TRUE);
								if (len == 0) {
									uprintf("Could not download file - cancelling");
									goto out;
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
						goto out;
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
						syslinux_ldlinux_len[i] = DownloadSignedFile(tmp, &tmp[sizeof(FILES_URL)],
							hMainDialog, (*img_report.sl_version_ext == 0));
						if ((syslinux_ldlinux_len[i] == 0) && (DownloadStatus == 404) && (*img_report.sl_version_ext != 0)) {
							// Couldn't locate the file on the server => try to download without the version extra
							uprintf("Extended version was not found, trying main version...");
							static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, img_report.sl_version_str,
								ldlinux, ldlinux_ext[i]);
							syslinux_ldlinux_len[i] = DownloadSignedFile(tmp, &tmp[sizeof(FILES_URL)],
								hMainDialog, (*img_report.sl_version_ext == 0));
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
								goto out;
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
				goto out;
			if (r == IDYES) {
				static_sprintf(tmp, "%s-%s", syslinux, embedded_sl_version_str[1]);
				IGNORE_RETVAL(_mkdir(tmp));
				static_sprintf(tmp, "%s/%s-%s/%s.%s", FILES_URL, syslinux, embedded_sl_version_str[1], ldlinux, ldlinux_ext[2]);
				if (DownloadSignedFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog, TRUE) == 0)
					goto out;
			}
		}
	} else if (bt == BT_MSDOS) {
		if ((size_check) && (ComboBox_GetItemData(hClusterSize, ComboBox_GetCurSel(hClusterSize)) >= 65536)) {
			// MS-DOS cannot boot from a drive using a 64 kilobytes Cluster size
			MessageBoxExU(hMainDialog, lmprintf(MSG_110), lmprintf(MSG_111), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
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
				goto out;
			if (r == IDYES) {
				static_sprintf(tmp, "grub4dos-%s", GRUB4DOS_VERSION);
				IGNORE_RETVAL(_mkdir(tmp));
				static_sprintf(tmp, "%s/grub4dos-%s/grldr", FILES_URL, GRUB4DOS_VERSION);
				if (DownloadSignedFile(tmp, &tmp[sizeof(FILES_URL)], hMainDialog, TRUE) == 0)
					goto out;
			}
		}
	}

uefi_target:
	if (bt == BT_UEFI_NTFS) {
		fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
		if (fs != FS_NTFS) {
			MessageBoxExU(hMainDialog, lmprintf(MSG_097, "UEFI:NTFS"), lmprintf(MSG_092), MB_OK|MB_ICONERROR|MB_IS_RTL, selected_langid);
			goto out;
		}
	}
	ret = 0;

out:
	PostMessage(hMainDialog, UM_FORMAT_START, ret, 0);
	ExitThread((DWORD)ret);
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

static void InitDialog(HWND hDlg)
{
	DWORD len;
	HWND hCtrl;
	HDC hDC;
	int i, lfHeight;
	char tmp[128], *token, *buf, *ext, *msg;
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

	CreateSmallButtons(hDlg);
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

	// Redefine the title to be able to add "Alpha" or "Beta"
	static_sprintf(tmp, APPLICATION_NAME " %d.%d.%d%s%s", rufus_version[0], rufus_version[1], rufus_version[2],
		IsAlphaOrBeta(), (ini_file != NULL)?"(Portable)":"");
	SetWindowTextU(hDlg, tmp);
	// Now that we have a title, we can find the handle of our Dialog
	dialog_handle = FindWindowA(NULL, tmp);
	uprintf(APPLICATION_NAME " " APPLICATION_ARCH " v%d.%d.%d%s%s", rufus_version[0], rufus_version[1], rufus_version[2],
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
	uprintf("System locale ID: 0x%04X (%s)", GetUserDefaultUILanguage(), GetCurrentMUI());
	ubflush();
	if (selected_locale->ctrl_id & LOC_NEEDS_UPDATE) {
		uprintf("NOTE: The %s translation requires an update, but the current translator hasn't submitted "
			"one. Because of this, some messages will only be displayed in English.", selected_locale->txt[1]);
		uprintf("If you think you can help update this translation, please e-mail the author of this application");
	}

	CreateTaskbarList();
	SetTaskbarProgressState(TASKBAR_NORMAL);

	// Use maximum granularity for the progress bar
	SendMessage(hProgress, PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);

	// Fill up the passes
	for (i = 1; i <= 5; i++) {
		msg = (i == 1) ? lmprintf(MSG_034, 1) : lmprintf(MSG_035, (i == 2) ? 2 : 4, (i == 2) ? "" : lmprintf(MSG_087, flash_type[i - 3]));
		IGNORE_RETVAL(ComboBox_AddStringU(hNBPasses, msg));
	}
	IGNORE_RETVAL(ComboBox_SetCurSel(hNBPasses, 0));
	SetPassesTooltip();

	// Fill up the boot options dropdown
	SetBootOptions();

	// Fill up the Image Options Windows To Go dropdown
	hCtrl = GetDlgItem(hMainDialog, IDC_IMAGE_OPTION);
	IGNORE_RETVAL(ComboBox_SetItemData(hCtrl, ComboBox_AddStringU(hCtrl, lmprintf(MSG_117)), FALSE));
	IGNORE_RETVAL(ComboBox_SetItemData(hCtrl, ComboBox_AddStringU(hCtrl, lmprintf(MSG_118)), TRUE));

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
	PositionMainControls(hDlg);
	AdjustForLowDPI(hDlg);
	// Because we created the log dialog before we computed our sizes, we need to send a custom message
	SendMessage(hLogDialog, UM_RESIZE_BUTTONS, 0, 0);
	// Limit the amount of characters for the Persistence size field
	SendMessage(GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE), EM_LIMITTEXT, 7, 0);
	// Create the status line and initialize the taskbar icon for progress overlay
	CreateStatusBar();

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
	CreateTooltip(hPartitionScheme, lmprintf(MSG_163), -1);
	CreateTooltip(hTargetSystem, lmprintf(MSG_150), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDS_CSM_HELP_TXT), lmprintf(MSG_151), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_305), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDC_PERSISTENCE_SLIDER), lmprintf(MSG_125), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE), lmprintf(MSG_125), 30000);
	CreateTooltip(GetDlgItem(hDlg, IDC_PERSISTENCE_UNITS), lmprintf(MSG_126), 30000);

	if (!advanced_mode_device)	// Hide as needed, since we display the advanced controls by default
		ToggleAdvancedDeviceOptions(FALSE);
	if (!advanced_mode_format)
		ToggleAdvancedFormatOptions(FALSE);
	ToggleImageOptions();

	// Process commandline parameters
	if (iso_provided) {
		// Simulate a button click for image selection
		PostMessage(hDlg, WM_COMMAND, IDC_SELECT, 0);
	}
	SetBootTypeDropdownWidth();

	CheckDlgButton(hMainDialog, IDC_LIST_USB_HDD, enable_HDDs ? BST_CHECKED : BST_UNCHECKED);

	PrintInfo(0, MSG_210);
}

static void PrintStatusTimeout(const char* str, BOOL val)
{
	PrintStatus(STATUS_MSG_TIMEOUT, (val)?MSG_250:MSG_251, str);
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
	char title[128];
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
		ComboBox_GetTextU(hDeviceList, title, sizeof(title));
		proceed = Notification(MSG_WARNING_QUESTION, NULL, NULL, title, lmprintf(MSG_132));
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
	LONG lPos;
	BOOL set_selected_fs;
	DRAWITEMSTRUCT* pDI;
	LPTOOLTIPTEXT lpttt;
	HDROP droppedFileInfo;
	POINT Point;
	RECT rc, DialogRect, DesktopRect;
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
			DeletePartitions((DWORD)ComboBox_GetItemData(hDeviceList, ComboBox_GetCurSel(hDeviceList)));
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
						// Avoid the FS being reset on language change
						selected_fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
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
				EnableWindow(GetDlgItem(hDlg, IDCANCEL), TRUE);
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
				// Can't link to dwmapi.lib since it sideloads dwapi.dll *before* we get a chance
				// to prevent local directory lookup (Sideloading mitigation).
				PF_TYPE_DECL(WINAPI, HRESULT, DwmGetWindowAttribute, (HWND, DWORD, PVOID, DWORD));
				GetClientRect(GetDesktopWindow(), &DesktopRect);
				GetWindowRect(hLogDialog, &DialogRect);
				nWidth = DialogRect.right - DialogRect.left;
				nHeight = DialogRect.bottom - DialogRect.top;
				GetWindowRect(hDlg, &DialogRect);
				offset = GetSystemMetrics(SM_CXBORDER);
				if (nWindowsVersion >= WINDOWS_10) {
					PF_INIT(DwmGetWindowAttribute, Dwmapi);
					// See https://stackoverflow.com/a/42491227/1069307
					// I agree with Stephen Hazel: Whoever at Microsoft thought it would be a great idea to
					// add a *FRIGGING INVISIBLE BORDER* in Windows 10 should face the harshest punishment!
					if (pfDwmGetWindowAttribute != NULL) {
						pfDwmGetWindowAttribute(hDlg, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(RECT));
						offset += 2 * (DialogRect.left - rc.left);
					}
				}
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
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE_INTERNAL<<16) | IDC_FILE_SYSTEM,
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
			PopulateProperties();
			nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
			DeviceNum = (nDeviceIndex == CB_ERR) ? 0 : (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
			SendMessage(hMainDialog, WM_COMMAND, (CBN_SELCHANGE_INTERNAL << 16) | IDC_FILE_SYSTEM,
				ComboBox_GetCurSel(hFileSystem));
			break;
		case IDC_IMAGE_OPTION:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetFileSystemAndClusterSize(NULL);
			windows_to_go_selection = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_IMAGE_OPTION));
			break;
		case IDC_PERSISTENCE_SIZE:
			if (HIWORD(wParam) == EN_CHANGE) {
				uint64_t pos;
				// We get EN_CHANGE when we change the size automatically, so we need to detect that
				if (app_changed_size) {
					app_changed_size = FALSE;
					break;
				}
				GetWindowTextA(GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE), tmp, sizeof(tmp));
				lPos = atol(tmp);
				persistence_unit_selection = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_PERSISTENCE_UNITS));
				persistence_size = lPos * MB;
				for (i = 0; i < persistence_unit_selection; i++)
					persistence_size *= 1024;
				if (persistence_size > SelectedDrive.DiskSize - img_report.projected_size)
					persistence_size = SelectedDrive.DiskSize - img_report.projected_size;
				pos = persistence_size / MB;
				for (i = 0; i < persistence_unit_selection; i++)
					pos /= 1024;
				lPos = (LONG)pos;
				SendMessage(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SLIDER), TBM_SETPOS, TRUE, lPos);
				if (persistence_size >= (SelectedDrive.DiskSize - img_report.projected_size)) {
					static_sprintf(tmp, "%ld", lPos);
					app_changed_size = TRUE;
					SetWindowTextU(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE), tmp);
				}
			} else if (HIWORD(wParam) == EN_KILLFOCUS) {
				if (persistence_size == 0) {
					TogglePersistenceControls(FALSE);
					static_sprintf(tmp, "0 (%s)", lmprintf(MSG_124));
					app_changed_size = TRUE;
					SetWindowTextU(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE), tmp);
				}
			}
			break;
		case IDC_PERSISTENCE_UNITS:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			if (ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_PERSISTENCE_UNITS)) == persistence_unit_selection)
				break;
			GetWindowTextA(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE), tmp, sizeof(tmp));
			persistence_size = atol(tmp) * MB;
			for (i = 0; i < persistence_unit_selection; i++)
				persistence_size *= 1024;
			persistence_unit_selection = ComboBox_GetCurSel(GetDlgItem(hDlg, IDC_PERSISTENCE_UNITS));
			SetPersistenceSize();
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
			selected_pt = pt;
			break;
		case IDC_FILE_SYSTEM:
			if ((HIWORD(wParam) != CBN_SELCHANGE) && (HIWORD(wParam) != CBN_SELCHANGE_INTERNAL))
				break;
			if (IsWindowEnabled(hFileSystem))
				EnableQuickFormat(TRUE);
			set_selected_fs = (HIWORD(wParam) == CBN_SELCHANGE);
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem)); 
			SetClusterSizes(fs);
			if (set_selected_fs && (fs > 0))
				selected_fs = fs;
			EnableMBRBootOptions(TRUE, FALSE);
			SetMBRProps();
			break;
		case IDC_BOOT_SELECTION:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			if ((HIWORD(wParam) != CBN_SELCHANGE) || (bt == selection_default))
				break;
			selection_default = bt;
			SetPartitionSchemeAndTargetSystem(FALSE);
			// Try to reselect current FS from the drive for non-bootable
			tmp[0] = 0;
			if ((selected_fs == FS_UNKNOWN) && (SelectedDrive.DeviceNumber != 0))
				GetDrivePartitionData(SelectedDrive.DeviceNumber, tmp, sizeof(tmp), TRUE);
			SetFileSystemAndClusterSize(tmp);
			ToggleImageOptions();
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
			format_op_in_progress = FALSE;
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
			if (format_thid != NULL)
				return (INT_PTR)TRUE;
			// Just in case
			bt = (int)ComboBox_GetItemData(hBootType, ComboBox_GetCurSel(hBootType));
			pt = (int)ComboBox_GetItemData(hPartitionScheme, ComboBox_GetCurSel(hPartitionScheme));
			tt = (int)ComboBox_GetItemData(hTargetSystem, ComboBox_GetCurSel(hTargetSystem));
			fs = (int)ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem));
			write_as_image = FALSE;
			installed_uefi_ntfs = FALSE;
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
			// Create a thread to validate options and download files as needed (so that we can update the UI).
			// On exit, this thread sends message UM_FORMAT_START back to this dialog.
			if (CreateThread(NULL, 0, BootCheckThread, NULL, 0, NULL) == NULL) {
				uprintf("Unable to start boot check thread");
				FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_START_THREAD);
				PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
			}
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
						persistence_unit_selection = -1;
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
			if (nWindowsVersion >= WINDOWS_10)
				pDI->rcItem.top += (int)(1.0f * fScale);
			else if (fScale >= 1.49f)
				pDI->rcItem.top -= (int)(1.5f * fScale);
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
		SetTextColor((HDC)wParam, TOOLBAR_ICON_COLOR);
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
			case IDC_SAVE:
				utf8_to_wchar_no_alloc(lmprintf(MSG_304), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
			case IDC_HASH:
				utf8_to_wchar_no_alloc(lmprintf(MSG_272), wtooltip, ARRAYSIZE(wtooltip));
				lpttt->lpszText = wtooltip;
				break;
			}
			break;
		}
		break;

	case WM_HSCROLL:
		lPos = (LONG)SendMessage(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SLIDER), TBM_GETPOS, 0, 0);
		SetPeristencePos(lPos);
		persistence_size = lPos * MB;
		for (i = 0; i < persistence_unit_selection; i++)
			persistence_size *= 1024;
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
		if (isMarquee)
			SendMessage(hProgress, PBM_SETMARQUEE, TRUE, 0);
		else
			SendMessage(hProgress, PBM_SETPOS, 0, 0);
		SetTaskbarProgressState(TASKBAR_NORMAL);
		SetTaskbarProgressValue(0, MAX_PROGRESS);
		break;

	case UM_PROGRESS_EXIT:
		if (isMarquee) {
			SendMessage(hProgress, PBM_SETMARQUEE, FALSE, 0);
			SetTaskbarProgressValue(0, MAX_PROGRESS);
		} else if (!IS_ERROR(FormatStatus)) {
			SetTaskbarProgressValue(MAX_PROGRESS, MAX_PROGRESS);
		}
		SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_NORMAL, 0);
		SetTaskbarProgressState(TASKBAR_NORMAL);
		break;

	case UM_NO_UPDATE:
		Notification(MSG_INFO, NULL, NULL, lmprintf(MSG_243), lmprintf(MSG_247));
		// Need to manually set focus back to "Check Now" for tabbing to work
		SendMessage(hUpdatesDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hUpdatesDlg, IDC_CHECK_NOW), TRUE);
		break;

	case UM_FORMAT_START:
		if (wParam != 0)
			goto aborted_start;

		if ((pt == PARTITION_STYLE_MBR) && (SelectedDrive.DiskSize > 2 * TB)) {
			if (MessageBoxExU(hMainDialog, lmprintf(MSG_134, SizeToHumanReadable(SelectedDrive.DiskSize - 2 * TB, FALSE, FALSE)),
				lmprintf(MSG_128, "MBR"), MB_YESNO | MB_ICONWARNING | MB_IS_RTL, selected_langid) != IDYES)
				goto aborted_start;
		}

		if (!zero_drive) {
			// Display a warning about UDF formatting times
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
			APPLICATION_NAME, MB_OKCANCEL | MB_ICONWARNING | MB_IS_RTL, selected_langid) == IDCANCEL)
			goto aborted_start;
		if ((SelectedDrive.nPartitions > 1) && (MessageBoxExU(hMainDialog, lmprintf(MSG_093),
			lmprintf(MSG_094), MB_OKCANCEL | MB_ICONWARNING | MB_IS_RTL, selected_langid) == IDCANCEL))
			goto aborted_start;
		if ((!zero_drive) && (bt != BT_NON_BOOTABLE) && (SelectedDrive.SectorSize != 512) &&
			(MessageBoxExU(hMainDialog, lmprintf(MSG_196, SelectedDrive.SectorSize),
				lmprintf(MSG_197), MB_OKCANCEL | MB_ICONWARNING | MB_IS_RTL, selected_langid) == IDCANCEL))
			goto aborted_start;

		nDeviceIndex = ComboBox_GetCurSel(hDeviceList);
		DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, nDeviceIndex);
		InitProgress(zero_drive || write_as_image);
		format_thid = CreateThread(NULL, 0, FormatThread, (LPVOID)(uintptr_t)DeviceNum, 0, NULL);
		if (format_thid == NULL) {
			uprintf("Unable to start formatting thread");
			FormatStatus = ERROR_SEVERITY_ERROR | FAC(FACILITY_STORAGE) | APPERR(ERROR_CANT_START_THREAD);
			PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
		} else {
			uprintf("\r\nFormat operation started");
			PrintInfo(0, -1);
			timer = 0;
			static_sprintf(szTimer, "00:00:00");
			SendMessageA(hStatus, SB_SETTEXTA, SBT_OWNERDRAW | SB_SECTION_RIGHT, (LPARAM)szTimer);
			SetTimer(hMainDialog, TID_APP_TIMER, 1000, ClockTimer);
			// Set focus to the Cancel button
			SendMessage(hMainDialog, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hMainDialog, IDCANCEL), TRUE);
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
			SendMessage(hProgress, PBM_SETPOS, MAX_PROGRESS, 0);
			SetTaskbarProgressState(TASKBAR_NOPROGRESS);
			PrintInfo(0, MSG_210);
			MessageBeep(MB_OK);
			FlashTaskbar(dialog_handle);
			if (installed_uefi_ntfs && (!ReadSettingBool(SETTING_DISABLE_SECURE_BOOT_NOTICE))) {
				notification_info more_info;
				more_info.id = MORE_INFO_URL;
				more_info.url = SECURE_BOOT_MORE_INFO_URL;
				Notification(MSG_INFO, SETTING_DISABLE_SECURE_BOOT_NOTICE, &more_info, lmprintf(MSG_128, "Secure Boot"), lmprintf(MSG_129));
			}
		} else if (SCODE_CODE(FormatStatus) == ERROR_CANCELLED) {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_PAUSED, 0);
			SetTaskbarProgressState(TASKBAR_PAUSED);
			PrintInfo(0, MSG_211);
			Notification(MSG_INFO, NULL, NULL, lmprintf(MSG_211), lmprintf(MSG_041));
		} else {
			SendMessage(hProgress, PBM_SETSTATE, (WPARAM)PBST_ERROR, 0);
			SetTaskbarProgressState(TASKBAR_ERROR);
			PrintInfo(0, MSG_212);
			MessageBeep(MB_ICONERROR);
			FlashTaskbar(dialog_handle);
			if (BlockingProcess.Index > 0) {
				ListDialog(lmprintf(MSG_042), lmprintf(MSG_055), BlockingProcess.String, BlockingProcess.Index);
			} else {
				if (nWindowsVersion >= WINDOWS_10) {
					// Try to detect if 'Controlled Folder Access' is enabled on Windows 10 or later. See also:
					// http://www.winhelponline.com/blog/use-controlled-folder-access-windows-10-windows-defender
					char cmdline[256];
					static const char* ps_cmd[2] = {
						// Return 1 if the 'Get-MpPreference' PowerShell cmdlet exists
						"If (Get-Command -Commandtype Function Get-MpPreference -ErrorAction SilentlyContinue) { Exit 1 } Else { Exit 0 }",
						// Return 1 if Controlled Folder Access is enabled
						"Exit (Get-MpPreference).EnableControlledFolderAccess" };
					switch (SCODE_CODE(FormatStatus)) {
					case ERROR_PARTITION_FAILURE:
					case ERROR_WRITE_FAULT:
						// Find if PowerShell is available at its expected location
						static_sprintf(tmp, "%s\\WindowsPowerShell\\v1.0\\powershell.exe", system_dir);
						if (PathFileExistsU(tmp)) {
							for (i = 0; i < ARRAYSIZE(ps_cmd); i++) {
								// Run the PowerShell commands
								static_sprintf(cmdline, "%s -NonInteractive -NoProfile -Command %s", tmp, ps_cmd[i]);
								if (RunCommand(cmdline, app_dir, TRUE) != 1)
									break;
							}
							if (i == ARRAYSIZE(ps_cmd)) {
								uprintf("\r\nWARNING: 'Controlled Folder Access' appears to be enabled on this system");
								uprintf("You may need to disable this feature, or add an exception, for Rufus to to work...\n");
							}
						}
						break;
					}
				}
				Notification(MSG_ERROR, NULL, NULL, lmprintf(MSG_042), lmprintf(MSG_043, StrError(FormatStatus, FALSE)));
			}
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
	printf("\nUsage: %s [-x] [-g] [-h] [-f FILESYSTEM] [-i PATH] [-l LOCALE] [-w TIMEOUT]\n", fname);
	printf("  -x, --extra-devs\n");
	printf("     List extra devices, such as USB HDDs\n");
	printf("  -g, --gui\n");
	printf("     Start in GUI mode (disable the 'rufus.com' commandline hogger)\n");
	printf("  -i PATH, --iso=PATH\n");
	printf("     Select the ISO image pointed by PATH to be used on startup\n");
	printf("  -l LOCALE, --locale=LOCALE\n");
	printf("     Select the locale to be used on startup\n");
	printf("  -f FILESYSTEM, --filesystem=FILESYSTEM\n");
	printf("     Preselect the file system to be preferred when formatting\n");
	printf("  -w TIMEOUT, --wait=TIMEOUT\n");
	printf("     Wait TIMEOUT tens of seconds for the global application mutex to be released.\n");
	printf("     Used when launching a newer version of " APPLICATION_NAME " from a running application.\n");
	printf("  -h, --help\n");
	printf("     This usage guide.\n");
}

static HANDLE SetHogger(void)
{
	INPUT* input;
	BYTE* hog_data;
	DWORD hog_size, Size;
	HANDLE hogmutex = NULL, hFile = NULL;
	int i;

	hog_data = GetResource(hMainInstance, MAKEINTRESOURCEA(IDR_XT_HOGGER),
		_RT_RCDATA, cmdline_hogger, &hog_size, FALSE);
	if (hog_data != NULL) {
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
	wchar_t kernel32_path[MAX_PATH];
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
	PF_TYPE_DECL(WINAPI, BOOL, SetDefaultDllDirectories, (DWORD));
	HANDLE mutex = NULL, hogmutex = NULL, hFile = NULL;
	HWND hDlg = NULL;
	HDC hDC;
	MSG msg;
	struct option long_options[] = {
		{"extra-devs", no_argument,       NULL, 'x'},
		{"gui",        no_argument,       NULL, 'g'},
		{"help",       no_argument,       NULL, 'h'},
		{"iso",        required_argument, NULL, 'i'},
		{"locale",     required_argument, NULL, 'l'},
		{"filesystem", required_argument, NULL, 'f'},
		{"wait",       required_argument, NULL, 'w'},
		{0, 0, NULL, 0}
	};

	// Disable loading system DLLs from the current directory (sideloading mitigation)
	// PS: You know that official MSDN documentation for SetDllDirectory() that explicitly
	// indicates that "If the parameter is an empty string (""), the call removes the current
	// directory from the default DLL search order"? Yeah, that doesn't work. At all.
	// Still, we invoke it, for platforms where the following call might not work...
	SetDllDirectoryA("");

	// Also, even if you use SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32), you're
	// still going to be brought down if you link to wininet.lib or dwmapi.lib, as these two
	// perform their DLL invocations before you've had a chance to execute anything.
	// Of course, this is not something that security "researchers" will bother looking into
	// to try to help fellow developers, when they can get an ego fix by simply throwing
	// generic URLs around and deliberately refusing to practice *responsible disclosure*...
	// Finally, we need to perform the whole gymkhana below, where we can't call on
	// SetDefaultDllDirectories() directly, because Windows 7 doesn't have the API exposed.
	GetSystemDirectoryW(kernel32_path, ARRAYSIZE(kernel32_path));
	wcsncat(kernel32_path, L"\\kernel32.dll", ARRAYSIZE(kernel32_path) - wcslen(kernel32_path) - 1);
	// NB: Because kernel32 should already be loaded, what we do above to ensure that we
	// (re)pick the system one is mostly unnecessary. But since for a hammer everything is a
	// nail... Also, no, Coverity, we never need to care about freeing kernel32 as a library.
	// coverity[leaked_storage]
	pfSetDefaultDllDirectories = (SetDefaultDllDirectories_t)
		GetProcAddress(LoadLibraryW(kernel32_path), "SetDefaultDllDirectories");
	if (pfSetDefaultDllDirectories != NULL)
		pfSetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

	uprintf("*** " APPLICATION_NAME " init ***\n");
	is_x86_32 = (strcmp(APPLICATION_ARCH, "x86") == 0);

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
			// Need to take 'ALPHA' into account
			if ((strchr(tmp, 'p') != NULL) || ((strchr(tmp, 'P') != NULL) && (strchr(tmp, 'P')[1] != 'H')))
				ini_flags[0] = 'a';

			// Now enable the hogger before processing the rest of the arguments
			if (!disable_hogger) {
				// Reattach the console, if we were started from commandline
				if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
					attached_console = TRUE;
					IGNORE_RETVAL(freopen("CONIN$", "r", stdin));
					IGNORE_RETVAL(freopen("CONOUT$", "w", stdout));
					IGNORE_RETVAL(freopen("CONOUT$", "w", stderr));
					_flushall();
					hogmutex = SetHogger();
				}
			}

			while ((opt = getopt_long(argc, argv, "?xghf:i:w:l:", long_options, &option_index)) != EOF) {
				switch (opt) {
				case 'x':
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
				case 'f':
					if (isdigitU(optarg[0])) {
						preselected_fs = (int)strtol(optarg, NULL, 0);
					} else {
						for (i = 0; i < ARRAYSIZE(FileSystemLabel); i++) {
							if (safe_stricmp(optarg, FileSystemLabel[i]) == 0) {
								preselected_fs = i;
								break;
							}
						}
					}
					if ((preselected_fs < FS_UNKNOWN) || (preselected_fs >= FS_MAX))
						preselected_fs = FS_UNKNOWN;
					selected_fs = preselected_fs;
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
	enable_file_indexing = ReadSettingBool(SETTING_ENABLE_FILE_INDEXING);

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
	// We always launch with the image options displaying
	image_options = IMOP_WINTOGO;
	image_option_txt[0] = 0;
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
	if (hDlg != NULL)
		// Make sure any previous dialog is destroyed (e.g. when switching languages)
		DestroyWindow(hDlg);
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
		// Ctrl-L => Open/Close the log
		if ((GetKeyState(VK_CONTROL) & 0x8000) && (msg.message == WM_KEYDOWN) && (msg.wParam == 'L')) {
			SendMessage(hMainDialog, WM_COMMAND, IDC_LOG, 0);
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
		// Alt-Q => Enable file indexing (for file systems that support it)
		// For multiple reasons, file indexing is disabled by default
		if ((msg.message == WM_SYSKEYDOWN) && (msg.wParam == 'Q')) {
			enable_file_indexing = !enable_file_indexing;
			WriteSettingBool(SETTING_ENABLE_FILE_INDEXING, enable_file_indexing);
			PrintStatusTimeout(lmprintf(MSG_290), !enable_file_indexing);
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
			fast_zeroing = FALSE;
			// Simulate a button click for Start
			PostMessage(hDlg, WM_COMMAND, (WPARAM)IDC_START, 0);
			continue;
		}
		// Ctrl-Alt-Z => Zero the drive while trying to skip empty blocks
		if ((msg.message == WM_KEYDOWN) && (msg.wParam == 'Z') &&
			(GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000)) {
			zero_drive = TRUE;
			fast_zeroing = TRUE;
			// Simulate a button click for Start
			PostMessage(hDlg, WM_COMMAND, (WPARAM)IDC_START, 0);
			continue;
		}

		// Other hazardous cheat modes require Ctrl + Alt
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
