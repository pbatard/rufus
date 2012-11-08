/*
 * Rufus: The Reliable USB Formatting Utility
 * Registry access
 * Copyright (c) 2012 Pete Batard <pete@akeo.ie>
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Application root subkey, under HKCU\Software
 * Typically "<Company Name>\<Application>"
 */
#define REGKEY_APPLICATION          "Akeo Systems\\Rufus"

/*
 * List of registry keys used by this application
 */
// Dispa
#define REGKEY_VERBOSE_UPDATES      "VerboseUpdateCheck"
#define REGKEY_DISABLE_UPDATES      "DisableUpdateCheck"
#define REGKEY_LAST_UPDATE          "LastUpdateCheck"
#define REGKEY_UPDATE_INTERVAL      "UpdateCheckInterval"


/* Read a generic registry key value (create the app key if it doesn't exist) */
static __inline BOOL _GetRegistryKey(const char* key_name, DWORD reg_type, LPBYTE dest, DWORD dest_size)
{
	BOOL r = FALSE;
	LONG s;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = -1, dwSize = dest_size;
	memset(dest, 0, dest_size);

	if ( (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS)
	  || (RegCreateKeyExA(hSoftware, REGKEY_APPLICATION, 0, NULL, 0,
		KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_CREATE_SUB_KEY, NULL, &hApp, &dwDisp) != ERROR_SUCCESS) ) {
		goto out;
	}

	s = RegQueryValueExA(hApp, key_name, NULL, &dwType, (LPBYTE)dest, &dwSize);
	// No key means default value of 0 or empty string
	if ((s == ERROR_FILE_NOT_FOUND) || ((s == ERROR_SUCCESS) && (dwType = reg_type) && (dwSize = dest_size))) {
		r = TRUE;
	}
out:
	RegCloseKey(hSoftware);
	RegCloseKey(hApp);
	return r;
}

/* Write a generic registry key value (create the app if it doesn't exist) */
static __inline BOOL _SetRegistryKey(const char* key_name, DWORD reg_type, LPBYTE src, DWORD src_size)
{
	BOOL r = FALSE;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = reg_type;

	if ( (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS)
	  || (RegCreateKeyExA(hSoftware, REGKEY_APPLICATION, 0, NULL, 0,
		KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_CREATE_SUB_KEY, NULL, &hApp, &dwDisp) != ERROR_SUCCESS) ) {
		goto out;
	}

	r = (RegSetValueExA(hApp, key_name, 0, dwType, src, src_size) == ERROR_SUCCESS);

out:
	RegCloseKey(hSoftware);
	RegCloseKey(hApp);
	return r;
}

/* Helpers for 64 bit registry operations */
#define GetRegistryKey64(key, pval) _GetRegistryKey(key, REG_QWORD, (LPBYTE)pval, sizeof(LONGLONG))
#define SetRegistryKey64(key, val) _SetRegistryKey(key, REG_QWORD, (LPBYTE)&val, sizeof(LONGLONG))
// Check that a key is accessible for R/W (will create a key if not already existing)
static __inline BOOL CheckRegistryKey64(const char* key) {
	LONGLONG val;
	return GetRegistryKey64(key, &val); // && SetRegistryKey64(key, val));
}
static __inline int64_t ReadRegistryKey64(const char* key) {
	LONGLONG val;
	GetRegistryKey64(key, &val);
	return (int64_t)val;
}
static __inline void WriteRegistryKey64(const char* key, int64_t val) {
	LONGLONG tmp = (LONGLONG)val;
	SetRegistryKey64(key, tmp);
}

/* Helpers for 32 bit registry operations */
#define GetRegistryKey32(key, pval) _GetRegistryKey(key, REG_DWORD, (LPBYTE)pval, sizeof(DWORD))
#define SetRegistryKey32(key, val) _SetRegistryKey(key, REG_DWORD, (LPBYTE)&val, sizeof(DWORD))
static __inline BOOL CheckRegistryKey32(const char* key) {
	DWORD val;
	return (GetRegistryKey32(key, &val) && SetRegistryKey32(key, val));
}
static __inline int32_t ReadRegistryKey32(const char* key) {
	DWORD val;
	GetRegistryKey32(key, &val);
	return (int32_t)val;
}
static __inline void WriteRegistryKey32(const char* key, int32_t val) {
	DWORD tmp = (DWORD)val;
	SetRegistryKey32(key, tmp);
}

/* Helpers for boolean registry operations */
#define GetRegistryKeyBool(key) (ReadRegistryKey32(key) != 0)
#define SetRegistryKeyBool(key) WriteRegistryKey32(key, 1)
#define CheckRegistryKeyBool CheckRegistryKey32

/* Helpers for String registry operations */
#define GetRegistryKeyStr(key, str, len) _GetRegistryKey(key, REG_SZ, (LPBYTE)str, (DWORD)len)
#define SetRegistryKeyStr(key, str) _SetRegistryKey(key, REG_SZ, (LPBYTE)str, safe_strlen(str))
// Use a static buffer - don't allocate
static __inline char* ReadRegistryKeyStr(const char* key) {
	static char str[512];
	_GetRegistryKey(key, REG_SZ, (LPBYTE)str, (DWORD)sizeof(str)-1);
	return str;
}
#define WriteRegistryKeyStr SetRegistryKeyStr

#ifdef __cplusplus
}
#endif
