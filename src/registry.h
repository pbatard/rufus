/*
 * Rufus: The Reliable USB Formatting Utility
 * Registry access
 * Copyright © 2012-2013 Pete Batard <pete@akeo.ie>
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define REGKEY_HKCU                 HKEY_CURRENT_USER
#define REGKEY_HKLM                 HKEY_LOCAL_MACHINE

/*
 * List of registry keys used by this application
 * These keys go into HKCU\Software\COMPANY_NAME\APPLICATION_NAME\
 */
#define REGKEY_VERBOSE_UPDATES      "VerboseUpdateCheck"
#define REGKEY_LAST_UPDATE          "LastUpdateCheck"
#define REGKEY_UPDATE_INTERVAL      "UpdateCheckInterval"
#define REGKEY_INCLUDE_BETAS        "CheckForBetas"
#define REGKEY_COMM_CHECK           "CommCheck"

/* Delete a registry key from <key_root>\Software and all its values
   If the key has subkeys, this call will fail. */
static __inline BOOL DeleteRegistryKey(HKEY key_root, const char* key_name)
{
	HKEY hSoftware = NULL;
	LONG s;

	if (RegOpenKeyExA(key_root, "SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS) {
		return FALSE;
	}

	s = RegDeleteKeyA(hSoftware, key_name);
	if ((s != ERROR_SUCCESS) && (s != ERROR_FILE_NOT_FOUND)) {
		SetLastError(s);
		uprintf("Failed to delete registry key HKCU\\Software\\%s: %s", key_name,
			(s == ERROR_ACCESS_DENIED)?"Key is not empty":WindowsErrorString());
	}
	RegCloseKey(hSoftware);
	return ((s == ERROR_SUCCESS) || (s == ERROR_FILE_NOT_FOUND));
}

/* Read a generic registry key value. If a short key_name is used, assume that it belongs to
   the application and create the app subkey if required */
static __inline BOOL _GetRegistryKey(HKEY key_root, const char* key_name, DWORD reg_type, LPBYTE dest, DWORD dest_size)
{
	BOOL r = FALSE;
	size_t i = 0;
	LONG s;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = -1, dwSize = dest_size;
	char long_key_name[256] = "SOFTWARE\\";
	memset(dest, 0, dest_size);

	for (i=safe_strlen(key_name); i>0; i--) {
		if (key_name[i] == '\\')
			break;
	}

	if (i != 0) {
		safe_strcat(long_key_name, sizeof(long_key_name), key_name);
		long_key_name[sizeof("SOFTWARE\\") + i-1] = 0;
		i++;
		if (RegOpenKeyExA(key_root, long_key_name, 0, KEY_READ, &hApp) != ERROR_SUCCESS)
			goto out;
	} else {
		if ( (RegOpenKeyExA(key_root, "SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS)
			 || (RegCreateKeyExA(hSoftware, COMPANY_NAME "\\" APPLICATION_NAME, 0, NULL, 0,
				KEY_SET_VALUE|KEY_QUERY_VALUE|KEY_CREATE_SUB_KEY, NULL, &hApp, &dwDisp) != ERROR_SUCCESS) )
		goto out;
	}

	s = RegQueryValueExA(hApp, &key_name[i], NULL, &dwType, (LPBYTE)dest, &dwSize);
	// No key means default value of 0 or empty string
	if ((s == ERROR_FILE_NOT_FOUND) || ((s == ERROR_SUCCESS) && (dwType = reg_type) && (dwSize > 0))) {
		r = TRUE;
	}
out:
	RegCloseKey(hSoftware);
	RegCloseKey(hApp);
	return r;
}

/* Write a generic registry key value (create the key if it doesn't exist) */
static __inline BOOL _SetRegistryKey(HKEY key_root, const char* key_name, DWORD reg_type, LPBYTE src, DWORD src_size)
{
	BOOL r = FALSE;
	HKEY hSoftware = NULL, hApp = NULL;
	DWORD dwDisp, dwType = reg_type;

	if ( (RegOpenKeyExA(key_root, "SOFTWARE", 0, KEY_READ|KEY_CREATE_SUB_KEY, &hSoftware) != ERROR_SUCCESS)
	  || (RegCreateKeyExA(hSoftware, COMPANY_NAME "\\" APPLICATION_NAME, 0, NULL, 0,
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
#define GetRegistryKey64(root, key, pval) _GetRegistryKey(root, key, REG_QWORD, (LPBYTE)pval, sizeof(LONGLONG))
#define SetRegistryKey64(root, key, val) _SetRegistryKey(root, key, REG_QWORD, (LPBYTE)&val, sizeof(LONGLONG))
// Check that a key is accessible for R/W (will create a key if not already existing)
static __inline BOOL CheckRegistryKey64(HKEY root, const char* key) {
	LONGLONG val;
	return GetRegistryKey64(root, key, &val); // && SetRegistryKey64(key, val));
}
static __inline int64_t ReadRegistryKey64(HKEY root, const char* key) {
	LONGLONG val;
	GetRegistryKey64(root, key, &val);
	return (int64_t)val;
}
static __inline BOOL WriteRegistryKey64(HKEY root, const char* key, int64_t val) {
	LONGLONG tmp = (LONGLONG)val;
	return SetRegistryKey64(root, key, tmp);
}

/* Helpers for 32 bit registry operations */
#define GetRegistryKey32(root, key, pval) _GetRegistryKey(root, key, REG_DWORD, (LPBYTE)pval, sizeof(DWORD))
#define SetRegistryKey32(root, key, val) _SetRegistryKey(root, key, REG_DWORD, (LPBYTE)&val, sizeof(DWORD))
static __inline BOOL CheckRegistryKey32(HKEY root, const char* key) {
	DWORD val;
	return (GetRegistryKey32(root, key, &val) && SetRegistryKey32(root, key, val));
}
static __inline int32_t ReadRegistryKey32(HKEY root, const char* key) {
	DWORD val;
	GetRegistryKey32(root, key, &val);
	return (int32_t)val;
}
static __inline BOOL WriteRegistryKey32(HKEY root, const char* key, int32_t val) {
	DWORD tmp = (DWORD)val;
	return SetRegistryKey32(root, key, tmp);
}

/* Helpers for boolean registry operations */
#define GetRegistryKeyBool(root, key) (ReadRegistryKey32(root, key) != 0)
#define SetRegistryKeyBool(root, key, b) WriteRegistryKey32(root, key, (b)?1:0)
#define CheckRegistryKeyBool CheckRegistryKey32

/* Helpers for String registry operations */
#define GetRegistryKeyStr(root, key, str, len) _GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)len)
#define SetRegistryKeyStr(root, key, str) _SetRegistryKey(root, key, REG_SZ, (LPBYTE)str, safe_strlen(str))
// Use a static buffer - don't allocate
static __inline char* ReadRegistryKeyStr(HKEY root, const char* key) {
	static char str[512];
	_GetRegistryKey(root, key, REG_SZ, (LPBYTE)str, (DWORD)sizeof(str)-1);
	return str;
}
#define WriteRegistryKeyStr SetRegistryKeyStr

#ifdef __cplusplus
}
#endif
