/*
 * Setup - Wrapper around Microsoft's setup.exe that adds registry
 *         bypasses for in-place Windows 11 upgrade.
 *
 * Copyright © 2024 Pete Batard <pete@akeo.ie>
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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>

static BOOL RegDeleteNodeRecurse(HKEY hKeyRoot, CHAR* lpSubKey)
{
	CHAR* lpEnd;
	LONG lResult;
	DWORD dwSize;
	CHAR szName[MAX_PATH];
	HKEY hKey;
	FILETIME ftWrite;

	// First, see if we can delete the key without having to recurse.
	if (RegDeleteKeyA(hKeyRoot, lpSubKey) == ERROR_SUCCESS)
		return TRUE;

	lResult = RegOpenKeyExA(hKeyRoot, lpSubKey, 0, KEY_READ, &hKey);
	if (lResult != ERROR_SUCCESS)
		return (lResult == ERROR_FILE_NOT_FOUND);

	// Check for an ending slash and add one if it is missing.
	lpEnd = lpSubKey + strlen(lpSubKey);
	if (*(lpEnd - 1) != '\\') {
		*lpEnd++ = '\\';
		*lpEnd = '\0';
	}

	// Enumerate the keys
	dwSize = MAX_PATH;
	if (RegEnumKeyExA(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite) == ERROR_SUCCESS) {
		do {
			*lpEnd = '\0';
			strcat_s(lpSubKey, MAX_PATH, szName);
			if (!RegDeleteNodeRecurse(hKeyRoot, lpSubKey))
				break;
			dwSize = MAX_PATH;
			lResult = RegEnumKeyExA(hKey, 0, szName, &dwSize, NULL, NULL, NULL, &ftWrite);
		} while (lResult == ERROR_SUCCESS);
	}

	*--lpEnd = '\0';
	RegCloseKey(hKey);

	// Try again to delete the key.
	return (RegDeleteKeyA(hKeyRoot, lpSubKey) == ERROR_SUCCESS);
}

static BOOL RegDeleteNode(HKEY hKeyRoot, CHAR* lpSubKey)
{
	CHAR szDelKey[MAX_PATH];

	strcpy_s(szDelKey, MAX_PATH, lpSubKey);
	return RegDeleteNodeRecurse(hKeyRoot, szDelKey);
}

static BOOL RegWriteKey(HKEY hKeyRoot, CHAR* lpKeyParent, CHAR* lpKeyName, DWORD dwType, LPBYTE lpData, DWORD dwDataSize)
{
	BOOL r = FALSE;
	HKEY hRoot = NULL, hApp = NULL;
	DWORD dwDisp;
	HKEY hKey;

	if (RegCreateKeyExA(hKeyRoot, lpKeyParent, 0, NULL, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, NULL, &hKey, &dwDisp) != ERROR_SUCCESS)
		return FALSE;

	r = (RegSetValueExA(hKey, lpKeyName, 0, dwType, lpData, dwDataSize) == ERROR_SUCCESS);
	RegCloseKey(hKey);

	return r;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	CHAR lpBypasses[] = "SQ_SecureBootCapable=TRUE\0SQ_SecureBootEnabled=TRUE\0SQ_TpmVersion=2\0SQ_RamMB=8192\0";
	DWORD dwUpgrade = 1, dwAttrib;
	STARTUPINFOA si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
	WCHAR *wc, wPath[MAX_PATH] = { 0 };

	// If invoked from a different directory, cd to where this executable resides
	if (GetModuleFileName(NULL, wPath, ARRAYSIZE(wPath)) != 0 && (wc = wcsrchr(wPath, L'\\')) != NULL) {
		*wc = L'\0';
		SetCurrentDirectory(wPath);
	}

	// Make sure we have 'setup.dll' in the same directory
	dwAttrib = GetFileAttributesA("setup.dll");
	if (dwAttrib == INVALID_FILE_ATTRIBUTES || dwAttrib & FILE_ATTRIBUTE_DIRECTORY)
		MessageBoxA(NULL, "ERROR: 'setup.dll' was not found", "Windows setup error", MB_OK | MB_ICONWARNING);

	// Apply the registry bypasses to enable Windows 11 24H2 in-place upgrade. Credits to:
	// https://forums.mydigitallife.net/threads/win-11-boot-and-upgrade-fix-kit-v5-0-released.83724/
	RegDeleteNode(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\CompatMarkers");
	RegDeleteNode(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Shared");
	RegDeleteNode(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\TargetVersionUpgradeExperienceIndicators");
	RegWriteKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\HwReqChk",
		"HwReqChkVars", REG_MULTI_SZ, lpBypasses, sizeof(lpBypasses));
	RegWriteKey(HKEY_LOCAL_MACHINE, "SYSTEM\\Setup\\MoSetup", "AllowUpgradesWithUnsupportedTPMOrCPU",
		REG_DWORD, (LPBYTE)&dwUpgrade, sizeof(dwUpgrade));

	// Launch the original 'setup.exe' (that was renamed to 'setup.dll')
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;
	CreateProcessA("setup.dll", lpCmdLine, NULL, NULL, TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return GetLastError();
}
