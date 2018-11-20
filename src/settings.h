/*
 * Rufus: The Reliable USB Formatting Utility
 * Settings access, through either registry or INI file
 * Copyright © 2015-2016 Pete Batard <pete@akeo.ie>
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
#include <stdint.h>
#include "rufus.h"
#include "registry.h"

#pragma once
extern char* ini_file;

/*
 * List of setting names used by this application
 */
#define SETTING_VERBOSE_UPDATES             "VerboseUpdateCheck"
#define SETTING_LAST_UPDATE                 "LastUpdateCheck"
#define SETTING_UPDATE_INTERVAL             "UpdateCheckInterval"
#define SETTING_INCLUDE_BETAS               "CheckForBetas"
#define SETTING_COMM_CHECK                  "CommCheck64"
#define SETTING_LOCALE                      "Locale"
#define SETTING_DISABLE_LGP                 "DisableLGP"
#define SETTING_DISABLE_SECURE_BOOT_NOTICE  "DisableSecureBootNotice"

#define SETTING_ADVANCED_MODE               "AdvancedMode"
#define SETTING_ADVANCED_MODE_DEVICE        "ShowAdvancedDriveProperties"
#define SETTING_ADVANCED_MODE_FORMAT        "ShowAdvancedFormatOptions"
#define SETTING_PRESERVE_TIMESTAMPS         "PreserveTimestamps"
#define SETTING_USE_PROPER_SIZE_UNITS       "UseProperSizeUnits"
#define SETTING_ENABLE_USB_DEBUG            "EnableUsbDebug"
#define SETTING_DISABLE_FAKE_DRIVES_CHECK   "DisableFakeDrivesCheck"
#define SETTING_ENABLE_WIN_DUAL_EFI_BIOS    "EnableWindowsDualUefiBiosMode"
#define SETTING_FORCE_LARGE_FAT32_FORMAT    "ForceLargeFat32Formatting"
#define SETTING_ENABLE_VMDK_DETECTION       "EnableVmdkDetection"
#define SETTING_ENABLE_FILE_INDEXING        "EnableFileIndexing"



static __inline BOOL CheckIniKey(const char* key) {
	char* str = get_token_data_file(key, ini_file);
	BOOL ret = (str != NULL);
	safe_free(str);
	return ret;
}
#define CheckIniKey64 CheckIniKey
#define CheckIniKey32 CheckIniKey
#define CheckIniKeyBool CheckIniKey
#define CheckIniKeyStr CheckIniKey

static __inline int64_t ReadIniKey64(const char* key) {
	int64_t val = 0;
	char* str = get_token_data_file(key, ini_file);
	if (str != NULL) {
		val = _strtoi64(str, NULL, 0);
		free(str);
	}
	return val;
}
static __inline BOOL WriteIniKey64(const char* key, int64_t val) {
	char str[24];
	static_sprintf(str, "%" PRIi64, val);
	return (set_token_data_file(key, str, ini_file) != NULL);
}

static __inline int32_t ReadIniKey32(const char* key) {
	int32_t val = 0;
	char* str = get_token_data_file(key, ini_file);
	if (str != NULL) {
		val = strtol(str, NULL, 0);
		free(str);
	}
	return val;
}
static __inline BOOL WriteIniKey32(const char* key, int32_t val) {
	char str[12];
	static_sprintf(str, "%d", val);
	return (set_token_data_file(key, str, ini_file) != NULL);
}

static __inline char* ReadIniKeyStr(const char* key) {
	static char str[512];
	char* val;
	str[0] = 0;
	val = get_token_data_file(key, ini_file);
	if (val != NULL) {
		static_strcpy(str, val);
		free(val);
	}
	return str;
}

static __inline BOOL WriteIniKeyStr(const char* key, const char* val) {
	return (set_token_data_file(key, val, ini_file) != NULL);
}

/* Helpers for boolean operations */
#define ReadIniKeyBool(key) (ReadIniKey32(key) != 0)
#define WriteIniKeyBool(key, b) WriteIniKey32(key, (b)?1:0)

/*
 * Read and store settings from/to ini file or registry
 */
static __inline int64_t ReadSetting64(const char* key) {
	return (ini_file != NULL)?ReadIniKey64(key):ReadRegistryKey64(REGKEY_HKCU, key);
}
static __inline BOOL WriteSetting64(const char* key, int64_t val) {
	return (ini_file != NULL)?WriteIniKey64(key, val):WriteRegistryKey64(REGKEY_HKCU, key, val);
}
static __inline int32_t ReadSetting32(const char* key) {
	return (ini_file != NULL)?ReadIniKey32(key):ReadRegistryKey32(REGKEY_HKCU, key);
}
static __inline BOOL WriteSetting32(const char* key, int32_t val) {
	return (ini_file != NULL)?WriteIniKey32(key, val):WriteRegistryKey32(REGKEY_HKCU, key, val);
}
static __inline BOOL ReadSettingBool(const char* key) {
	return (ini_file != NULL)?ReadIniKeyBool(key):ReadRegistryKeyBool(REGKEY_HKCU, key);
}
static __inline BOOL WriteSettingBool(const char* key, BOOL val) {
	return (ini_file != NULL)?WriteIniKeyBool(key, val):WriteRegistryKeyBool(REGKEY_HKCU, key, val);
}
static __inline char* ReadSettingStr(const char* key) {
	return (ini_file != NULL)?ReadIniKeyStr(key):ReadRegistryKeyStr(REGKEY_HKCU, key);
}
static __inline BOOL WriteSettingStr(const char* key, char* val) {
	return (ini_file != NULL)?WriteIniKeyStr(key, val):WriteRegistryKeyStr(REGKEY_HKCU, key, val);
}
