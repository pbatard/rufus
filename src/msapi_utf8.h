/*
 * MSAPI_UTF8: Common API calls using UTF-8 strings
 * Compensating for what Microsoft should have done a long long time ago.
 * Also see http://utf8everywhere.org/
 *
 * Copyright © 2010-2015 Pete Batard <pete@akeo.ie>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <windows.h>
#include <stdio.h>
#include <shlobj.h>
#include <ctype.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <setupapi.h>
#include <direct.h>
#include <share.h>
#include <fcntl.h>
#include <io.h>

#pragma once
#if defined(_MSC_VER)
// disable VS2012 Code Analysis warnings that are intentional
#pragma warning(disable: 6387)	// Don't care about bad params
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define _LTEXT(txt) L##txt
#define LTEXT(txt) _LTEXT(txt)

#define wchar_to_utf8_no_alloc(wsrc, dest, dest_size) \
	WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, dest, dest_size, NULL, NULL)
#define utf8_to_wchar_no_alloc(src, wdest, wdest_size) \
	MultiByteToWideChar(CP_UTF8, 0, src, -1, wdest, wdest_size)
#define Edit_ReplaceSelU(hCtrl, str) ((void)SendMessageLU(hCtrl, EM_REPLACESEL, (WPARAM)FALSE, str))
#define ComboBox_AddStringU(hCtrl, str) ((int)(DWORD)SendMessageLU(hCtrl, CB_ADDSTRING, (WPARAM)FALSE, str))
#define ComboBox_InsertStringU(hCtrl, index, str) ((int)(DWORD)SendMessageLU(hCtrl, CB_INSERTSTRING, (WPARAM)index, str))
#define ComboBox_GetTextU(hCtrl, str, max_str) GetWindowTextU(hCtrl, str, max_str)
#define GetSaveFileNameU(p) GetOpenSaveFileNameU(p, TRUE)
#define GetOpenFileNameU(p) GetOpenSaveFileNameU(p, FALSE)
#define ListView_SetItemTextU(hwndLV,i,iSubItem_,pszText_) { LVITEMW _ms_wlvi; _ms_wlvi.iSubItem = iSubItem_; \
	_ms_wlvi.pszText = utf8_to_wchar(pszText_); \
	SNDMSG((hwndLV),LVM_SETITEMTEXTW,(WPARAM)(i),(LPARAM)&_ms_wlvi); sfree(_ms_wlvi.pszText);}

// Never ever use isdigit() or isspace(), etc. on UTF-8 strings!
// These calls take an int and char is signed so MS compilers will produce an assert error on anything that's > 0x80
#define isasciiU(c) isascii((unsigned char)(c))
#define iscntrlU(c) iscntrl((unsigned char)(c))
#define isdigitU(c) isdigit((unsigned char)(c))
#define isspaceU(c) isspace((unsigned char)(c))
#define isxdigitU(c) isxdigit((unsigned char)(c))
// NB: other issomething() calls are not implemented as they may require multibyte UTF-8 sequences to be converted 

#define sfree(p) do {if (p != NULL) {free((void*)(p)); p = NULL;}} while(0)
#define wconvert(p)     wchar_t* w ## p = utf8_to_wchar(p)
#define walloc(p, size) wchar_t* w ## p = (wchar_t*)calloc(size, sizeof(wchar_t))
#define wfree(p) sfree(w ## p)

/*
 * Converts an UTF-16 string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
static __inline char* wchar_to_utf8(const wchar_t* wstr)
{
	int size = 0;
	char* str = NULL;

	// Find out the size we need to allocate for our converted string
	size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((str = (char*)calloc(size, 1)) == NULL)
		return NULL;

	if (wchar_to_utf8_no_alloc(wstr, str, size) != size) {
		sfree(str);
		return NULL;
	}

	return str;
}

/*
 * Converts an UTF8 string to UTF-16 (allocate returned string)
 * Returns NULL on error
 */
static __inline wchar_t* utf8_to_wchar(const char* str)
{
	int size = 0;
	wchar_t* wstr = NULL;

	// Find out the size we need to allocate for our converted string
	size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	if (size <= 1)	// An empty string would be size 1
		return NULL;

	if ((wstr = (wchar_t*)calloc(size, sizeof(wchar_t))) == NULL)
		return NULL;

	if (utf8_to_wchar_no_alloc(str, wstr, size) != size) {
		sfree(wstr);
		return NULL;
	}
	return wstr;
}

static __inline DWORD FormatMessageU(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId,
									 DWORD dwLanguageId, char* lpBuffer, DWORD nSize, va_list *Arguments)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpBuffer, nSize);
	ret = FormatMessageW(dwFlags, lpSource, dwMessageId, dwLanguageId, wlpBuffer, nSize, Arguments);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nSize)) == 0)) {
		err = GetLastError();
		ret = 0;
	}
	wfree(lpBuffer);
	SetLastError(err);
	return ret;
}

// SendMessage, with LPARAM as UTF-8 string
static __inline LRESULT SendMessageLU(HWND hWnd, UINT Msg, WPARAM wParam, const char* lParam)
{
	LRESULT ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lParam);
	ret = SendMessageW(hWnd, Msg, wParam, (LPARAM)wlParam);
	err = GetLastError();
	wfree(lParam);
	SetLastError(err);
	return ret;
}

static __inline int DrawTextExU(HDC hDC, LPCSTR lpchText, int nCount, LPRECT lpRect, UINT uFormat, LPDRAWTEXTPARAMS lpDTParams)
{
	int ret;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpchText);
	ret = DrawTextExW(hDC, wlpchText, nCount, lpRect, uFormat, lpDTParams);
	err = GetLastError();
	wfree(lpchText);
	SetLastError(err);
	return ret;
}

static __inline BOOL SHGetPathFromIDListU(LPCITEMIDLIST pidl, char* pszPath)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	walloc(pszPath, MAX_PATH);
	ret = SHGetPathFromIDListW(pidl, wpszPath);
	err = GetLastError();
	if ((ret) && (wchar_to_utf8_no_alloc(wpszPath, pszPath, MAX_PATH) == 0)) {
		err = GetLastError();
		ret = FALSE;
	}
	wfree(pszPath);
	SetLastError(err);
	return ret;
}

static __inline HWND CreateWindowU(char* lpClassName, char* lpWindowName,
	DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent,
	HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
	HWND ret = NULL;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpClassName);
	wconvert(lpWindowName);
	ret = CreateWindowW(wlpClassName, wlpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	err = GetLastError();
	wfree(lpClassName);
	wfree(lpWindowName);
	SetLastError(err);
	return ret;
}

static __inline int MessageBoxU(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
	int ret;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpText);
	wconvert(lpCaption);
	ret = MessageBoxW(hWnd, wlpText, wlpCaption, uType);
	err = GetLastError();
	wfree(lpText);
	wfree(lpCaption);
	SetLastError(err);
	return ret;
}

static __inline int DrawTextU(HDC hDC, LPCSTR lpText, int nCount, LPRECT lpRect, UINT uFormat)
{
	int ret;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpText);
	ret = DrawTextW(hDC, wlpText, nCount, lpRect, uFormat);
	err = GetLastError();
	wfree(lpText);
	SetLastError(err);
	return ret;
}

static __inline int GetWindowTextU(HWND hWnd, char* lpString, int nMaxCount)
{
	int ret = 0;
	DWORD err = ERROR_INVALID_DATA;
	walloc(lpString, nMaxCount);
	ret = GetWindowTextW(hWnd, wlpString, nMaxCount);
	err = GetLastError();
	if ( (ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpString, lpString, nMaxCount)) == 0) ) {
		err = GetLastError();
	}
	wfree(lpString);
	SetLastError(err);
	return ret;
}

static __inline BOOL SetWindowTextU(HWND hWnd, const char* lpString)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpString);
	ret = SetWindowTextW(hWnd, wlpString);
	err = GetLastError();
	wfree(lpString);
	SetLastError(err);
	return ret;
}

static __inline int GetWindowTextLengthU(HWND hWnd)
{
	int ret = 0;
	DWORD err = ERROR_INVALID_DATA;
	wchar_t* wbuf = NULL;
	char* buf = NULL;

	ret = GetWindowTextLengthW(hWnd);
	err = GetLastError();
	if (ret == 0) goto out;
	wbuf = calloc(ret, sizeof(wchar_t));
	err = GetLastError();
	if (wbuf == NULL) {
		err = ERROR_OUTOFMEMORY; ret = 0; goto out;
	}
	ret = GetWindowTextW(hWnd, wbuf, ret);
	err = GetLastError();
	if (ret == 0) goto out;
	buf = wchar_to_utf8(wbuf);
	err = GetLastError();
	if (buf == NULL) {
		err = ERROR_OUTOFMEMORY; ret = 0; goto out;
	}
	ret = (int)strlen(buf) + 2;	// GetDlgItemText seems to add a character
	err = GetLastError();
out:
	sfree(wbuf);
	sfree(buf);
	SetLastError(err);
	return ret;
}

static __inline UINT GetDlgItemTextU(HWND hDlg, int nIDDlgItem, char* lpString, int nMaxCount)
{
	UINT ret = 0;
	DWORD err = ERROR_INVALID_DATA;
	walloc(lpString, nMaxCount);
	ret = GetDlgItemTextW(hDlg, nIDDlgItem, wlpString, nMaxCount);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpString, lpString, nMaxCount)) == 0)) {
		err = GetLastError();
	}
	wfree(lpString);
	SetLastError(err);
	return ret;
}

static __inline BOOL SetDlgItemTextU(HWND hDlg, int nIDDlgItem, const char* lpString)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpString);
	ret = SetDlgItemTextW(hDlg, nIDDlgItem, wlpString);
	err = GetLastError();
	wfree(lpString);
	SetLastError(err);
	return ret;
}

static __inline BOOL InsertMenuU(HMENU hMenu, UINT uPosition, UINT uFlags, UINT_PTR uIDNewItem, const char* lpNewItem)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpNewItem);
	ret = InsertMenuW(hMenu, uPosition, uFlags, uIDNewItem, wlpNewItem);
	err = GetLastError();
	wfree(lpNewItem);
	SetLastError(err);
	return ret;
}

static __inline int ComboBox_GetLBTextU(HWND hCtrl, int index, char* lpString)
{
	int size;
	DWORD err = ERROR_INVALID_DATA;
	wchar_t* wlpString;
	if (lpString == NULL)
		return CB_ERR;
	size = (int)SendMessageW(hCtrl, CB_GETLBTEXTLEN, (WPARAM)index, (LPARAM)0);
	if (size < 0)
		return size;
	wlpString = (wchar_t*)calloc(size+1, sizeof(wchar_t));
	size = (int)SendMessageW(hCtrl, CB_GETLBTEXT, (WPARAM)index, (LPARAM)wlpString);
	err = GetLastError();
	if (size > 0)
		wchar_to_utf8_no_alloc(wlpString, lpString, size+1);
	wfree(lpString);
	SetLastError(err);
	return size;
}

static __inline HANDLE CreateFileU(const char* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
								   LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
								   DWORD dwFlagsAndAttributes,  HANDLE hTemplateFile)
{
	HANDLE ret = INVALID_HANDLE_VALUE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpFileName);
	ret = CreateFileW(wlpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
	err = GetLastError();
	wfree(lpFileName);
	SetLastError(err);
	return ret;
}

static __inline BOOL CopyFileU(const char* lpExistingFileName, const char* lpNewFileName, BOOL bFailIfExists)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpExistingFileName);
	wconvert(lpNewFileName);
	ret = CopyFileW(wlpExistingFileName, wlpNewFileName, bFailIfExists);
	err = GetLastError();
	wfree(lpExistingFileName);
	wfree(lpNewFileName);
	SetLastError(err);
	return ret;
}

static __inline BOOL DeleteFileU(const char* lpFileName)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpFileName);
	ret = DeleteFileW(wlpFileName);
	err = GetLastError();
	wfree(lpFileName);
	SetLastError(err);
	return ret;
}

static __inline int PathGetDriveNumberU(char* lpPath)
{
	int ret = 0;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpPath);
	ret = PathGetDriveNumberW(wlpPath);
	err = GetLastError();
	wfree(lpPath);
	SetLastError(err);
	return ret;
}

// This function differs from regular GetTextExtentPoint in that it uses a zero terminated string
static __inline BOOL GetTextExtentPointU(HDC hdc, const char* lpString, LPSIZE lpSize)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(lpString);
	if (wlpString == NULL)
		return FALSE;
	ret = GetTextExtentPoint32W(hdc, wlpString, (int)wcslen(wlpString), lpSize);
	err = GetLastError();
	wfree(lpString);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetCurrentDirectoryU(DWORD nBufferLength, char* lpBuffer)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpBuffer, nBufferLength);
	ret = GetCurrentDirectoryW(nBufferLength, wlpBuffer);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nBufferLength)) == 0)) {
		err = GetLastError();
	}
	wfree(lpBuffer);
	SetLastError(err);
	return ret;
}

static __inline UINT GetSystemDirectoryU(char* lpBuffer, UINT uSize)
{
	UINT ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpBuffer, uSize);
	ret = GetSystemDirectoryW(wlpBuffer, uSize);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, uSize)) == 0)) {
		err = GetLastError();
	}
	wfree(lpBuffer);
	SetLastError(err);
	return ret;
}

static __inline UINT GetSystemWindowsDirectoryU(char* lpBuffer, UINT uSize)
{
	UINT ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpBuffer, uSize);
	ret = GetSystemWindowsDirectoryW(wlpBuffer, uSize);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, uSize)) == 0)) {
		err = GetLastError();
	}
	wfree(lpBuffer);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetTempPathU(DWORD nBufferLength, char* lpBuffer)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpBuffer, nBufferLength);
	ret = GetTempPathW(nBufferLength, wlpBuffer);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nBufferLength)) == 0)) {
		err = GetLastError();
	}
	wfree(lpBuffer);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetTempFileNameU(char* lpPathName, char* lpPrefixString, UINT uUnique, char* lpTempFileName)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	wconvert(lpPathName);
	wconvert(lpPrefixString);
	walloc(lpTempFileName, MAX_PATH);
	ret = GetTempFileNameW(wlpPathName, wlpPrefixString, uUnique, wlpTempFileName);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpTempFileName, lpTempFileName, MAX_PATH)) == 0)) {
		err = GetLastError();
	}
	wfree(lpTempFileName);
	wfree(lpPrefixString);
	wfree(lpPathName);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetModuleFileNameU(HMODULE hModule, char* lpFilename, DWORD nSize)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	walloc(lpFilename, nSize);
	ret = GetModuleFileNameW(hModule, wlpFilename, nSize);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpFilename, lpFilename, nSize)) == 0)) {
		err = GetLastError();
	}
	wfree(lpFilename);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetFullPathNameU(const char* lpFileName, DWORD nBufferLength, char* lpBuffer, char** lpFilePart)
{
	DWORD ret = 0, err = ERROR_INVALID_DATA;
	wchar_t* wlpFilePart;
	wconvert(lpFileName);
	walloc(lpBuffer, nBufferLength);

	// lpFilePart is not supported
	if (lpFilePart != NULL) goto out;

	ret = GetFullPathNameW(wlpFileName, nBufferLength, wlpBuffer, &wlpFilePart);
	err = GetLastError();
	if ((ret != 0) && ((ret = wchar_to_utf8_no_alloc(wlpBuffer, lpBuffer, nBufferLength)) == 0)) {
		err = GetLastError();
	}

out:
	wfree(lpBuffer);
	wfree(lpFileName);
	SetLastError(err);
	return ret;
}

static __inline DWORD GetFileAttributesU(const char* lpFileName)
{
	DWORD ret = 0xFFFFFFFF, err = ERROR_INVALID_DATA;
	wconvert(lpFileName);
	ret = GetFileAttributesW(wlpFileName);
	err = GetLastError();
	wfree(lpFileName);
	SetLastError(err);
	return ret;
}

static __inline int SHCreateDirectoryExU(HWND hwnd, const char* pszPath, SECURITY_ATTRIBUTES *psa)
{
	int ret = ERROR_INVALID_DATA;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(pszPath);
	ret = SHCreateDirectoryExW(hwnd, wpszPath, psa);
	err = GetLastError();
	wfree(pszPath);
	SetLastError(err);
	return ret;
}

static __inline int SHDeleteDirectoryExU(HWND hwnd, const char* pszPath, FILEOP_FLAGS fFlags)
{
	int ret;
	// String needs to be double NULL terminated, so we just use the length of the UTF-8 string
	// which is always expected to be larger than our UTF-16 one, and add 2 chars for good measure.
	size_t wpszPath_len = strlen(pszPath) + 2;
	wchar_t* wpszPath = (wchar_t*)calloc(wpszPath_len, sizeof(wchar_t));
	SHFILEOPSTRUCTW shfo = { hwnd, FO_DELETE, wpszPath, NULL, fFlags, FALSE, NULL, NULL };
	utf8_to_wchar_no_alloc(pszPath, wpszPath, (int)wpszPath_len);
	// FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION,
	ret = SHFileOperationW(&shfo);
	wfree(pszPath);
	return ret;
}

static __inline BOOL ShellExecuteExU(SHELLEXECUTEINFOA* lpExecInfo)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	SHELLEXECUTEINFOW wExecInfo;

	// Because we're lazy, we'll assume that the A and W structs inherently have the same size
	if (lpExecInfo->cbSize != sizeof(SHELLEXECUTEINFOW)) {
		SetLastError(ERROR_BAD_LENGTH); return FALSE;
	}
	memcpy(&wExecInfo, lpExecInfo, lpExecInfo->cbSize);
	wExecInfo.lpVerb = utf8_to_wchar(lpExecInfo->lpVerb);
	wExecInfo.lpFile = utf8_to_wchar(lpExecInfo->lpFile);
	wExecInfo.lpParameters = utf8_to_wchar(lpExecInfo->lpParameters);
	wExecInfo.lpDirectory = utf8_to_wchar(lpExecInfo->lpDirectory);
	if (wExecInfo.fMask & SEE_MASK_CLASSNAME) {
		wExecInfo.lpClass = utf8_to_wchar(lpExecInfo->lpClass);
	} else {
		wExecInfo.lpClass = NULL;
	}
	ret = ShellExecuteExW(&wExecInfo);
	err = GetLastError();
	// Copy the returned values back
	lpExecInfo->hInstApp = wExecInfo.hInstApp;
	lpExecInfo->hProcess = wExecInfo.hProcess;
	sfree(wExecInfo.lpVerb);
	sfree(wExecInfo.lpFile);
	sfree(wExecInfo.lpParameters);
	sfree(wExecInfo.lpDirectory);
	sfree(wExecInfo.lpClass);
	SetLastError(err);
	return ret;
}

// Doesn't support LPSTARTUPINFOEX struct
static __inline BOOL CreateProcessU(const char* lpApplicationName, const char* lpCommandLine, LPSECURITY_ATTRIBUTES lpProcessAttributes,
									LPSECURITY_ATTRIBUTES lpThreadAttributes, BOOL bInheritHandles, DWORD dwCreationFlags,
									LPVOID lpEnvironment, const char* lpCurrentDirectory, LPSTARTUPINFOA lpStartupInfo,
									LPPROCESS_INFORMATION lpProcessInformation)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	STARTUPINFOW wStartupInfo;
	wconvert(lpApplicationName);
	wconvert(lpCommandLine);
	wconvert(lpCurrentDirectory);

	// Because we're lazy, we'll assume that the A and W structs inherently have the same size
	// Also prevents the use of STARTUPINFOEX
	if (lpStartupInfo->cb != sizeof(STARTUPINFOW)) {
		err = ERROR_BAD_LENGTH; goto out;
	}
	memcpy(&wStartupInfo, lpStartupInfo, lpStartupInfo->cb);
	wStartupInfo.lpDesktop = utf8_to_wchar(lpStartupInfo->lpDesktop);
	wStartupInfo.lpTitle = utf8_to_wchar(lpStartupInfo->lpTitle);
	ret = CreateProcessW(wlpApplicationName, wlpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
		dwCreationFlags, lpEnvironment, wlpCurrentDirectory, &wStartupInfo, lpProcessInformation);
	err = GetLastError();
	sfree(wStartupInfo.lpDesktop);
	sfree(wStartupInfo.lpTitle);
out:
	wfree(lpApplicationName);
	wfree(lpCommandLine);
	wfree(lpCurrentDirectory);
	SetLastError(err);
	return ret;
}

// NOTE: when used, nFileOffset & nFileExtension MUST be provided
// in number of Unicode characters, NOT number of UTF-8 bytes
static __inline BOOL WINAPI GetOpenSaveFileNameU(LPOPENFILENAMEA lpofn, BOOL save)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	size_t i, len;
	OPENFILENAMEW wofn;
	memset(&wofn, 0, sizeof(wofn));
	wofn.lStructSize = sizeof(wofn);
	wofn.hwndOwner = lpofn->hwndOwner;
	wofn.hInstance = lpofn->hInstance;

	// No support for custom filters
	if (lpofn->lpstrCustomFilter != NULL) goto out;

	// Count on Microsoft to use an moronic scheme for filters
	// that relies on NULL separators and double NULL terminators
	if (lpofn->lpstrFilter != NULL) {
		// Replace the NULLs by something that can be converted
		for (i=0; ; i++) {
			if (lpofn->lpstrFilter[i] == 0) {
				((char*)lpofn->lpstrFilter)[i] = '\r';
				if (lpofn->lpstrFilter[i+1] == 0) {
					break;
				}
			}
		}
		wofn.lpstrFilter = utf8_to_wchar(lpofn->lpstrFilter);
		// And revert
		len = wcslen(wofn.lpstrFilter);	// don't use in the loop as it would be reevaluated
		for (i=0; i<len; i++) {
			if (wofn.lpstrFilter[i] == '\r') {
				((wchar_t*)wofn.lpstrFilter)[i] = 0;
			}
		}
		len = strlen(lpofn->lpstrFilter);
		for (i=0; i<len; i++) {
			if (lpofn->lpstrFilter[i] == '\r') {
				((char*)lpofn->lpstrFilter)[i] = 0;
			}
		}
	} else {
		wofn.lpstrFilter = NULL;
	}
	wofn.nMaxCustFilter = lpofn->nMaxCustFilter;
	wofn.nFilterIndex = lpofn->nFilterIndex;
	wofn.lpstrFile = calloc(lpofn->nMaxFile, sizeof(wchar_t));
	utf8_to_wchar_no_alloc(lpofn->lpstrFile, wofn.lpstrFile, lpofn->nMaxFile);
	wofn.nMaxFile = lpofn->nMaxFile;
	wofn.lpstrFileTitle = calloc(lpofn->nMaxFileTitle, sizeof(wchar_t));
	utf8_to_wchar_no_alloc(lpofn->lpstrFileTitle, wofn.lpstrFileTitle, lpofn->nMaxFileTitle);
	wofn.nMaxFileTitle = lpofn->nMaxFileTitle;
	wofn.lpstrInitialDir = utf8_to_wchar(lpofn->lpstrInitialDir);
	wofn.lpstrTitle = utf8_to_wchar(lpofn->lpstrTitle);
	wofn.Flags = lpofn->Flags;
	wofn.nFileOffset = lpofn->nFileOffset;
	wofn.nFileExtension = lpofn->nFileExtension;
	wofn.lpstrDefExt = utf8_to_wchar(lpofn->lpstrDefExt);
	wofn.lCustData = lpofn->lCustData;
	wofn.lpfnHook = lpofn->lpfnHook;
	wofn.lpTemplateName = utf8_to_wchar(lpofn->lpTemplateName);
	wofn.pvReserved = lpofn->pvReserved;
	wofn.dwReserved = lpofn->dwReserved;
	wofn.FlagsEx = lpofn->FlagsEx;

	if (save) {
		ret = GetSaveFileNameW(&wofn);
	} else {
		ret = GetOpenFileNameW(&wofn);
	}
	err = GetLastError();
	if ( (ret)
	  && ( (wchar_to_utf8_no_alloc(wofn.lpstrFile, lpofn->lpstrFile, lpofn->nMaxFile) == 0)
	    || (wchar_to_utf8_no_alloc(wofn.lpstrFileTitle, lpofn->lpstrFileTitle, lpofn->nMaxFileTitle) == 0) ) ) {
		err = GetLastError();
		ret = FALSE;
	}
out:
	sfree(wofn.lpstrDefExt);
	sfree(wofn.lpstrFile);
	sfree(wofn.lpstrFileTitle);
	sfree(wofn.lpstrFilter);
	sfree(wofn.lpstrInitialDir);
	sfree(wofn.lpstrTitle);
	sfree(wofn.lpTemplateName);
	SetLastError(err);
	return ret;
}

extern BOOL WINAPI UpdateDriverForPlugAndPlayDevicesW(HWND hwndParent, LPCWSTR HardwareId,
	LPCWSTR FullInfPath, DWORD InstallFlags, PBOOL bRebootRequired);

static __inline BOOL UpdateDriverForPlugAndPlayDevicesU(HWND hwndParent, const char* HardwareId, const char* FullInfPath,
														DWORD InstallFlags, PBOOL bRebootRequired)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(HardwareId);
	wconvert(FullInfPath);
	ret = UpdateDriverForPlugAndPlayDevicesW(hwndParent, wHardwareId, wFullInfPath, InstallFlags, bRebootRequired);
	err = GetLastError();
	wfree(HardwareId);
	wfree(FullInfPath);
	SetLastError(err);
	return ret;
}

static __inline BOOL SetupCopyOEMInfU(const char* SourceInfFileName, const char* OEMSourceMediaLocation, DWORD OEMSourceMediaType,
									  DWORD CopyStyle, char* DestinationInfFileName, DWORD DestinationInfFileNameSize,
									  PDWORD RequiredSize, PTSTR DestinationInfFileNameComponent)
{
	BOOL ret = FALSE;
	DWORD err = ERROR_INVALID_DATA;
	wconvert(SourceInfFileName);
	wconvert(OEMSourceMediaLocation);
	walloc(DestinationInfFileName, DestinationInfFileNameSize);

	// DestinationInfFileNameComponent is not supported
	if (DestinationInfFileNameComponent != NULL) goto out;

	ret = SetupCopyOEMInfW(wSourceInfFileName, wOEMSourceMediaLocation, OEMSourceMediaType, CopyStyle,
		wDestinationInfFileName, DestinationInfFileNameSize, RequiredSize, NULL);
	err = GetLastError();
	if ((ret != FALSE) && ((ret = wchar_to_utf8_no_alloc(wDestinationInfFileName, DestinationInfFileName, DestinationInfFileNameSize)) == 0)) {
		err = GetLastError();
	}
out:
	wfree(SourceInfFileName);
	wfree(OEMSourceMediaLocation);
	wfree(DestinationInfFileName);
	SetLastError(err);
	return ret;
}

static __inline int _chdirU(const char *dirname)
{
	int ret;
	wconvert(dirname);
	ret = _wchdir(wdirname);
	wfree(dirname);
	return ret;
}

static __inline FILE* fopenU(const char* filename, const char* mode)
{
	FILE* ret = NULL;
	wconvert(filename);
	wconvert(mode);
	_wfopen_s(&ret, wfilename, wmode);
	wfree(filename);
	wfree(mode);
	return ret;
}

static __inline int _openU(const char *filename, int oflag , int pmode)
{
	int ret = -1;
	int shflag = _SH_DENYNO;
	wconvert(filename);
	// Try to match the share flag to the oflag
	if (oflag & _O_RDONLY)
		shflag = _SH_DENYWR;
	else if (oflag & _O_WRONLY)
		shflag = _SH_DENYRD;
	_wsopen_s(&ret, wfilename, oflag, shflag, pmode);
	wfree(filename);
	return ret;
}

// returned UTF-8 string must be freed
static __inline char* getenvU(const char* varname)
{
	wconvert(varname);
	char *ret;
	wchar_t value[256];
	size_t value_size;
	// MinGW and WDK don't know wdupenv_s, so we use wgetenv_s
	_wgetenv_s(&value_size, value, ARRAYSIZE(value), wvarname);
	ret = wchar_to_utf8(value);
	wfree(varname);
	return ret;
}

static __inline int _mkdirU(const char* dirname)
{
	wconvert(dirname);
	int ret;
	ret = _wmkdir(wdirname);
	wfree(dirname);
	return ret;
}

#ifdef __cplusplus
}
#endif
