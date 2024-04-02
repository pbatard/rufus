/*
 * Rufus: The Reliable USB Formatting Utility
 * UI element lists
 * Copyright © 2018-2024 Pete Batard <pete@akeo.ie>
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

#include <windows.h>
#include "resource.h"

#pragma once

// GUIDs needed to set a control's accessibility props
#if defined(_MSC_VER)
const GUID DECLSPEC_SELECTANY CLSID_AccPropServices =
	{ 0xb5f8350b, 0x0548, 0x48b1, { 0xa6, 0xee, 0x88, 0xbd, 0x00, 0xb4, 0xa5, 0xe7 } };
#endif
const GUID DECLSPEC_SELECTANY Name_Property_GUID =
	{ 0xc3a6921b, 0x4a99, 0x44f1, { 0xbc, 0xa6, 0x61, 0x18, 0x70, 0x52, 0xc4, 0x31 } };
const GUID DECLSPEC_SELECTANY HelpText_Property_GUID =
	{ 0x08555685, 0x0977, 0x45c7, { 0xa7, 0xa6, 0xab, 0xaf, 0x56, 0x84, 0x12, 0x1a } };

static int section_control_ids[] = {
	IDS_DRIVE_PROPERTIES_TXT,
	IDS_FORMAT_OPTIONS_TXT,
	IDS_STATUS_TXT
};

static int section_vpos[ARRAYSIZE(section_control_ids)];

static int image_option_move_ids[] = {
	IDS_PARTITION_TYPE_TXT,
	IDC_PARTITION_TYPE,
	IDS_TARGET_SYSTEM_TXT,
	IDC_TARGET_SYSTEM,
	IDS_CSM_HELP_TXT,
	IDC_ADVANCED_DEVICE_TOOLBAR,
	IDC_LIST_USB_HDD,
	IDC_OLD_BIOS_FIXES,
	IDC_UEFI_MEDIA_VALIDATION,
	IDS_FORMAT_OPTIONS_TXT,
	IDS_LABEL_TXT,
	IDC_LABEL,
	IDS_FILE_SYSTEM_TXT,
	IDC_FILE_SYSTEM,
	IDS_CLUSTER_SIZE_TXT,
	IDC_CLUSTER_SIZE,
	IDC_ADVANCED_FORMAT_TOOLBAR,
	IDC_QUICK_FORMAT,
	IDC_BAD_BLOCKS,
	IDC_NB_PASSES,
	IDC_EXTENDED_LABEL,
	IDS_STATUS_TXT,
	IDC_PROGRESS,
	IDC_ABOUT,
	IDC_LOG,
	IDC_MULTI_TOOLBAR,
	IDC_TEST,
	IDC_START,
	IDCANCEL,
	IDC_STATUS,
	IDC_STATUS_TOOLBAR,
};

static int image_option_toggle_ids[][2] = {
	{ IDS_IMAGE_OPTION_TXT, 0x03 },
	{ IDC_IMAGE_OPTION, 0x01 },
	{ IDC_PERSISTENCE_SLIDER, 0x02 },
	{ IDC_PERSISTENCE_SIZE, 0x02 },
	{ IDC_PERSISTENCE_UNITS, 0x02 }
};

static int advanced_device_move_ids[] = {
	IDC_LIST_USB_HDD,
	IDC_OLD_BIOS_FIXES,
	IDC_UEFI_MEDIA_VALIDATION,
	IDS_FORMAT_OPTIONS_TXT,
	IDS_LABEL_TXT,
	IDC_LABEL,
	IDS_FILE_SYSTEM_TXT,
	IDC_FILE_SYSTEM,
	IDS_CLUSTER_SIZE_TXT,
	IDC_CLUSTER_SIZE,
	IDC_ADVANCED_FORMAT_TOOLBAR,
	IDC_QUICK_FORMAT,
	IDC_BAD_BLOCKS,
	IDC_NB_PASSES,
	IDC_EXTENDED_LABEL,
	IDS_STATUS_TXT,
	IDC_PROGRESS,
	IDC_ABOUT,
	IDC_LOG,
	IDC_MULTI_TOOLBAR,
	IDC_TEST,
	IDC_START,
	IDCANCEL,
	IDC_STATUS,
	IDC_STATUS_TOOLBAR,
};

static int advanced_device_toggle_ids[] = {
	IDC_SAVE_TOOLBAR,
	IDC_LIST_USB_HDD,
	IDC_OLD_BIOS_FIXES,
	IDC_UEFI_MEDIA_VALIDATION,
};

static int advanced_format_move_ids[] = {
	IDS_STATUS_TXT,
	IDC_PROGRESS,
	IDC_ABOUT,
	IDC_LOG,
	IDC_MULTI_TOOLBAR,
	IDC_TEST,
	IDC_START,
	IDCANCEL,
	IDC_STATUS,
	IDC_STATUS_TOOLBAR,
};

static int advanced_format_toggle_ids[] = {
	IDC_QUICK_FORMAT,
	IDC_BAD_BLOCKS,
	IDC_NB_PASSES,
	IDC_EXTENDED_LABEL,
};

static int main_button_ids[] = {
	IDC_SELECT,
	IDC_START,
	IDCANCEL,
};

static int full_width_controls[] = {
	IDS_DEVICE_TXT,
	IDS_BOOT_SELECTION_TXT,
	IDS_IMAGE_OPTION_TXT,
	IDC_IMAGE_OPTION,
	IDS_LABEL_TXT,
	IDC_LABEL,
	IDC_ADVANCED_DRIVE_PROPERTIES,
	IDC_LIST_USB_HDD,
	IDC_OLD_BIOS_FIXES,
	IDC_UEFI_MEDIA_VALIDATION,
	IDC_ADVANCED_FORMAT_OPTIONS,
	IDC_QUICK_FORMAT,
	IDC_EXTENDED_LABEL,
	IDC_PROGRESS,
};

static int full_width_checkboxes[] = {
	IDC_LIST_USB_HDD,
	IDC_OLD_BIOS_FIXES,
	IDC_UEFI_MEDIA_VALIDATION,
	IDC_QUICK_FORMAT,
	IDC_EXTENDED_LABEL,
};

static int half_width_ids[] = {
	IDC_BAD_BLOCKS,
	IDS_PARTITION_TYPE_TXT,
	IDC_PARTITION_TYPE,
	IDC_FILE_SYSTEM,
	IDS_TARGET_SYSTEM_TXT,
	IDC_TARGET_SYSTEM,
	IDS_CLUSTER_SIZE_TXT,
	IDC_CLUSTER_SIZE,
	IDC_NB_PASSES,
};

static int adjust_dpi_ids[][5] = {
	{ IDS_DEVICE_TXT, IDC_DEVICE, IDC_SAVE_TOOLBAR, 0, 0 },
	{ IDS_BOOT_SELECTION_TXT, IDC_BOOT_SELECTION, IDC_HASH_TOOLBAR, IDC_SELECT, 0 },
	{ IDS_IMAGE_OPTION_TXT, IDC_IMAGE_OPTION, IDC_PERSISTENCE_SLIDER, IDC_PERSISTENCE_SIZE, IDC_PERSISTENCE_UNITS },
	{ IDS_PARTITION_TYPE_TXT, IDC_PARTITION_TYPE, IDS_TARGET_SYSTEM_TXT, IDC_TARGET_SYSTEM, IDS_CSM_HELP_TXT },
	{ IDC_ADVANCED_DEVICE_TOOLBAR, 0, 0, 0, 0 },
	{ IDC_LIST_USB_HDD, 0, 0, 0, 0 },
	{ IDC_OLD_BIOS_FIXES, 0, 0, 0, 0 },
	{ IDC_UEFI_MEDIA_VALIDATION, 0, 0, 0, 0 },
	{ IDS_FORMAT_OPTIONS_TXT, 0, 0, 0, 0 },
	{ IDS_LABEL_TXT, IDC_LABEL, 0, 0, 0 },
	{ IDS_FILE_SYSTEM_TXT, IDC_FILE_SYSTEM, IDS_CLUSTER_SIZE_TXT, IDC_CLUSTER_SIZE, 0 },
	{ IDC_ADVANCED_FORMAT_TOOLBAR, 0, 0, 0, 0 },
	{ IDC_QUICK_FORMAT, 0, 0, 0, 0 },
	{ IDC_EXTENDED_LABEL, 0, 0, 0, 0 },
	{ IDC_BAD_BLOCKS, IDC_NB_PASSES, 0, 0, 0 },
	{ IDS_STATUS_TXT, 0, 0, 0, 0 },
	{ IDC_PROGRESS, 0, 0, 0, 0 },
	{ IDC_MULTI_TOOLBAR, IDC_TEST, IDC_START, IDCANCEL, 0 }
};

static int multitoolbar_icons[] = {
	IDI_LANG_16,
	IDI_INFO_16,
	IDI_SETTINGS_16,
	IDI_LOG_16
};
