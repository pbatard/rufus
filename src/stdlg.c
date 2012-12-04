/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard Dialog Routines (Browse for folder, About, etc)
 * Copyright © 2011-2012 Pete Batard <pete@akeo.ie>
 *
 * Based on zadig_stdlg.c, part of libwdi: http://libwdi.sf.net
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
#include <shlobj.h>
#include <commdlg.h>
#include <sddl.h>
#include <richedit.h>

#include "rufus.h"
#include "msapi_utf8.h"
#include "registry.h"
#include "resource.h"
#include "license.h"

/* The following is only available on Vista and later */
#if (_WIN32_WINNT >= 0x0600)
static HRESULT (WINAPI *pSHCreateItemFromParsingName)(PCWSTR, IBindCtx*, REFIID, void **) = NULL;
#endif
#define INIT_VISTA_SHELL32 if (pSHCreateItemFromParsingName == NULL) {						\
	pSHCreateItemFromParsingName = (HRESULT (WINAPI *)(PCWSTR, IBindCtx*, REFIID, void **))	\
			GetProcAddress(GetModuleHandleA("SHELL32"), "SHCreateItemFromParsingName");		\
	}
#define IS_VISTA_SHELL32_AVAILABLE (pSHCreateItemFromParsingName != NULL)
// And this one is simply not available in MinGW32
static LPITEMIDLIST (WINAPI *pSHSimpleIDListFromPath)(PCWSTR pszPath) = NULL;
#define INIT_XP_SHELL32 if (pSHSimpleIDListFromPath == NULL) {								\
	pSHSimpleIDListFromPath = (LPITEMIDLIST (WINAPI *)(PCWSTR))								\
			GetProcAddress(GetModuleHandleA("SHELL32"), "SHSimpleIDListFromPath");			\
	}

/* Globals */
static HICON hMessageIcon = (HICON)INVALID_HANDLE_VALUE;
static char* szMessageText = NULL;
static char* szMessageTitle = NULL;
enum WindowsVersion nWindowsVersion = WINDOWS_UNDEFINED;
static HWND hBrowseEdit;
static WNDPROC pOrgBrowseWndproc;
static const SETTEXTEX friggin_microsoft_unicode_amateurs = {ST_DEFAULT, CP_UTF8};
static BOOL notification_is_question;
static const notification_info* notification_more_info;
static BOOL reg_commcheck = FALSE;

/*
 * Detect Windows version
 */
enum WindowsVersion DetectWindowsVersion(void)
{
	OSVERSIONINFO OSVersion;

	memset(&OSVersion, 0, sizeof(OSVERSIONINFO));
	OSVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (GetVersionEx(&OSVersion) == 0)
		return WINDOWS_UNDEFINED;
	if (OSVersion.dwPlatformId != VER_PLATFORM_WIN32_NT)
		return WINDOWS_UNSUPPORTED;
	// See the Remarks section from http://msdn.microsoft.com/en-us/library/windows/desktop/ms724833.aspx
	if ((OSVersion.dwMajorVersion < 5) || ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 0)))
		return WINDOWS_UNSUPPORTED;		// Win2k or earlier
	if ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 1))
		return WINDOWS_XP;
	if ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 2))
		return  WINDOWS_2003;
	if ((OSVersion.dwMajorVersion == 6) && (OSVersion.dwMinorVersion == 0))
		return  WINDOWS_VISTA;
	if ((OSVersion.dwMajorVersion == 6) && (OSVersion.dwMinorVersion == 1))
		return  WINDOWS_7;
	if ((OSVersion.dwMajorVersion == 6) && (OSVersion.dwMinorVersion == 2))
		return  WINDOWS_8;
	if ((OSVersion.dwMajorVersion > 6) || ((OSVersion.dwMajorVersion == 6) && (OSVersion.dwMinorVersion >= 3)))
		return  WINDOWS_9;
	return WINDOWS_UNSUPPORTED;
}

/*
 * String array manipulation
 */
void StrArrayCreate(StrArray* arr, size_t initial_size)
{
	if (arr == NULL) return;
	arr->Max = initial_size; arr->Index = 0;
	arr->Table = (char**)calloc(arr->Max, sizeof(char*));
	if (arr->Table == NULL)
		uprintf("Could not allocate string array\n");
}

void StrArrayAdd(StrArray* arr, const char* str)
{
	if ((arr == NULL) || (arr->Table == NULL))
		return;
	if (arr->Index == arr->Max) {
		arr->Max *= 2;
		arr->Table = (char**)realloc(arr->Table, arr->Max*sizeof(char*));
		if (arr->Table == NULL) {
			uprintf("Could not reallocate string array\n");
			return;
		}
	}
	arr->Table[arr->Index] = safe_strdup(str);
	if (arr->Table[arr->Index++] == NULL) {
		uprintf("Could not store string in array\n");
	}
}

void StrArrayClear(StrArray* arr)
{
	size_t i;
	if ((arr == NULL) || (arr->Table == NULL))
		return;
	for (i=0; i<arr->Index; i++) {
		safe_free(arr->Table[i]);
	}
	arr->Index = 0;
}

void StrArrayDestroy(StrArray* arr)
{
	StrArrayClear(arr);
	if (arr != NULL)
		safe_free(arr->Table);
}

/*
 * Retrieve the SID of the current user. The returned PSID must be freed by the caller using LocalFree()
 */
static PSID GetSID(void) {
	TOKEN_USER* tu = NULL;
	DWORD len;
	HANDLE token;
	PSID ret = NULL;
	char* psid_string = NULL;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		uprintf("OpenProcessToken failed: %s\n", WindowsErrorString());
		return NULL;
	}

	if (!GetTokenInformation(token, TokenUser, tu, 0, &len)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			uprintf("GetTokenInformation (pre) failed: %s\n", WindowsErrorString());
			return NULL;
		}
		tu = (TOKEN_USER*)calloc(1, len);
	}
	if (tu == NULL) {
		return NULL;
	}

	if (GetTokenInformation(token, TokenUser, tu, len, &len)) {
		/*
		 * now of course, the interesting thing is that if you return tu->User.Sid
		 * but free tu, the PSID pointer becomes invalid after a while.
		 * The workaround? Convert to string then back to PSID
		 */
		if (!ConvertSidToStringSidA(tu->User.Sid, &psid_string)) {
			uprintf("Unable to convert SID to string: %s\n", WindowsErrorString());
			ret = NULL;
		} else {
			if (!ConvertStringSidToSidA(psid_string, &ret)) {
				uprintf("Unable to convert string back to SID: %s\n", WindowsErrorString());
				ret = NULL;
			}
			// MUST use LocalFree()
			LocalFree(psid_string);
		}
	} else {
		ret = NULL;
		uprintf("GetTokenInformation (real) failed: %s\n", WindowsErrorString());
	}
	free(tu);
	return ret;
}

/*
 * We need a sub-callback to read the content of the edit box on exit and update
 * our path, else if what the user typed does match the selection, it is discarded.
 * Talk about a convoluted way of producing an intuitive folder selection dialog
 */
INT CALLBACK BrowseDlgCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message) {
	case WM_DESTROY:
		GetWindowTextU(hBrowseEdit, szFolderPath, sizeof(szFolderPath));
		break;
	}
	return (INT)CallWindowProc(pOrgBrowseWndproc, hDlg, message, wParam, lParam);
}

/*
 * Main BrowseInfo callback to set the initial directory and populate the edit control
 */
INT CALLBACK BrowseInfoCallback(HWND hDlg, UINT message, LPARAM lParam, LPARAM pData)
{
	char dir[MAX_PATH];
	wchar_t* wpath;
	LPITEMIDLIST pidl;

	switch(message) {
	case BFFM_INITIALIZED:
		pOrgBrowseWndproc = (WNDPROC)SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)BrowseDlgCallback);
		// Windows hides the full path in the edit box by default, which is bull.
		// Get a handle to the edit control to fix that
		hBrowseEdit = FindWindowExA(hDlg, NULL, "Edit", NULL);
		SetWindowTextU(hBrowseEdit, szFolderPath);
		SetFocus(hBrowseEdit);
		// On XP, BFFM_SETSELECTION can't be used with a Unicode Path in SendMessageW
		// or a pidl (at least with MinGW) => must use SendMessageA
		if (nWindowsVersion <= WINDOWS_XP) {
			SendMessageLU(hDlg, BFFM_SETSELECTION, (WPARAM)TRUE, szFolderPath);
		} else {
			// On Windows 7, MinGW only properly selects the specified folder when using a pidl
			wpath = utf8_to_wchar(szFolderPath);
			pidl = (*pSHSimpleIDListFromPath)(wpath);
			safe_free(wpath);
			// NB: see http://connect.microsoft.com/VisualStudio/feedback/details/518103/bffm-setselection-does-not-work-with-shbrowseforfolder-on-windows-7
			// for details as to why we send BFFM_SETSELECTION twice.
			SendMessageW(hDlg, BFFM_SETSELECTION, (WPARAM)FALSE, (LPARAM)pidl);
			Sleep(100);
			PostMessageW(hDlg, BFFM_SETSELECTION, (WPARAM)FALSE, (LPARAM)pidl);
		}
		break;
	case BFFM_SELCHANGED:
		// Update the status
		if (SHGetPathFromIDListU((LPITEMIDLIST)lParam, dir)) {
			SendMessageLU(hDlg, BFFM_SETSTATUSTEXT, 0, dir);
			SetWindowTextU(hBrowseEdit, dir);
		}
		break;
	}
	return 0;
}

/*
 * Browse for a folder and update the folder edit box
 * Will use the newer IFileOpenDialog if *compiled* for Vista or later
 */
void BrowseForFolder(void) {

	BROWSEINFOW bi;
	LPITEMIDLIST pidl;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	WCHAR *wpath;
	size_t i;
	HRESULT hr;
	IShellItem *psi = NULL;
	IShellItem *si_path = NULL;	// Automatically freed
	IFileOpenDialog *pfod = NULL;
	WCHAR *fname;
	char* tmp_path = NULL;

	// Even if we have Vista support with the compiler,
	// it does not mean we have the Vista API available
	INIT_VISTA_SHELL32;
	if (IS_VISTA_SHELL32_AVAILABLE) {
		hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileOpenDialog, (LPVOID)&pfod);
		if (FAILED(hr)) {
			uprintf("CoCreateInstance for FileOpenDialog failed: error %X\n", hr);
			pfod = NULL;	// Just in case
			goto fallback;
		}
		hr = pfod->lpVtbl->SetOptions(pfod, FOS_PICKFOLDERS);
		if (FAILED(hr)) {
			uprintf("Failed to set folder option for FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		// Set the initial folder (if the path is invalid, will simply use last)
		wpath = utf8_to_wchar(szFolderPath);
		// The new IFileOpenDialog makes us split the path
		fname = NULL;
		if ((wpath != NULL) && (wcslen(wpath) >= 1)) {
			for (i=wcslen(wpath)-1; i!=0; i--) {
				if (wpath[i] == L'\\') {
					wpath[i] = 0;
					fname = &wpath[i+1];
					break;
				}
			}
		}

		hr = (*pSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID)&si_path);
		if (SUCCEEDED(hr)) {
			if (wpath != NULL) {
				hr = pfod->lpVtbl->SetFolder(pfod, si_path);
			}
			if (fname != NULL) {
				hr = pfod->lpVtbl->SetFileName(pfod, fname);
			}
		}
		safe_free(wpath);

		hr = pfod->lpVtbl->Show(pfod, hMainDialog);
		if (SUCCEEDED(hr)) {
			hr = pfod->lpVtbl->GetResult(pfod, &psi);
			if (SUCCEEDED(hr)) {
				psi->lpVtbl->GetDisplayName(psi, SIGDN_FILESYSPATH, &wpath);
				tmp_path = wchar_to_utf8(wpath);
				CoTaskMemFree(wpath);
				if (tmp_path == NULL) {
					uprintf("Could not convert path\n");
				} else {
					safe_strcpy(szFolderPath, MAX_PATH, tmp_path);
					safe_free(tmp_path);
				}
			} else {
				uprintf("Failed to set folder option for FileOpenDialog: error %X\n", hr);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			uprintf("Could not show FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		pfod->lpVtbl->Release(pfod);
		return;
	}
fallback:
	if (pfod != NULL) {
		pfod->lpVtbl->Release(pfod);
	}
#endif
	INIT_XP_SHELL32;
	memset(&bi, 0, sizeof(BROWSEINFOW));
	bi.hwndOwner = hMainDialog;
	bi.lpszTitle = L"Please select the installation folder:";
	bi.lpfn = BrowseInfoCallback;
	// BIF_NONEWFOLDERBUTTON = 0x00000200 is unknown on MinGW
	bi.ulFlags = BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS |
		BIF_DONTGOBELOWDOMAIN | BIF_EDITBOX | 0x00000200;
	pidl = SHBrowseForFolderW(&bi);
	if (pidl != NULL) {
		CoTaskMemFree(pidl);
	}
}

/*
 * read or write I/O to a file
 * buffer is allocated by the procedure. path is UTF-8
 */
BOOL FileIO(BOOL save, char* path, char** buffer, DWORD* size)
{
	SECURITY_ATTRIBUTES s_attr, *ps = NULL;
	SECURITY_DESCRIPTOR s_desc;
	PSID sid = NULL;
	HANDLE handle;
	BOOL r;
	BOOL ret = FALSE;

	// Change the owner from admin to regular user
	sid = GetSID();
	if ( (sid != NULL)
	  && InitializeSecurityDescriptor(&s_desc, SECURITY_DESCRIPTOR_REVISION)
	  && SetSecurityDescriptorOwner(&s_desc, sid, FALSE) ) {
		s_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
		s_attr.bInheritHandle = FALSE;
		s_attr.lpSecurityDescriptor = &s_desc;
		ps = &s_attr;
	} else {
		uprintf("Could not set security descriptor: %s\n", WindowsErrorString());
	}

	if (!save) {
		*buffer = NULL;
	}
	handle = CreateFileU(path, save?GENERIC_WRITE:GENERIC_READ, FILE_SHARE_READ,
		ps, save?CREATE_ALWAYS:OPEN_EXISTING, 0, NULL);

	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("Could not %s file '%s'\n", save?"create":"open", path);
		goto out;
	}

	if (save) {
		r = WriteFile(handle, *buffer, *size, size, NULL);
	} else {
		*size = GetFileSize(handle, NULL);
		*buffer = (char*)malloc(*size);
		if (*buffer == NULL) {
			uprintf("Could not allocate buffer for reading file\n");
			goto out;
		}
		r = ReadFile(handle, *buffer, *size, size, NULL);
	}

	if (!r) {
		uprintf("I/O Error: %s\n", WindowsErrorString());
		goto out;
	}

	PrintStatus(0, TRUE, "%s '%s'", save?"Saved":"Opened", path);
	ret = TRUE;

out:
	CloseHandle(handle);
	if (!ret) {
		// Only leave a buffer allocated if successful
		*size = 0;
		if (!save) {
			safe_free(*buffer);
		}
	}
	return ret;
}


/*
 * Return the UTF8 path of a file selected through a load or save dialog
 * Will use the newer IFileOpenDialog if *compiled* for Vista or later
 * All string parameters are UTF-8
 */
char* FileDialog(BOOL save, char* path, char* filename, char* ext, char* ext_desc)
{
	DWORD tmp;
	OPENFILENAMEA ofn;
	char selected_name[MAX_PATH];
	char* ext_string = NULL;
	size_t i, ext_strlen;
	BOOL r;
	char* filepath = NULL;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	HRESULT hr = FALSE;
	IFileDialog *pfd = NULL;
	IShellItem *psiResult;
	COMDLG_FILTERSPEC filter_spec[2];
	char* ext_filter;
	wchar_t *wpath = NULL, *wfilename = NULL;
	IShellItem *si_path = NULL;	// Automatically freed

	INIT_VISTA_SHELL32;
	if (IS_VISTA_SHELL32_AVAILABLE) {
		// Setup the file extension filter table
		ext_filter = (char*)malloc(safe_strlen(ext)+3);
		if (ext_filter != NULL) {
			safe_sprintf(ext_filter, safe_strlen(ext)+3, "*.%s", ext);
			filter_spec[0].pszSpec = utf8_to_wchar(ext_filter);
			safe_free(ext_filter);
			filter_spec[0].pszName = utf8_to_wchar(ext_desc);
			filter_spec[1].pszSpec = L"*.*";
			filter_spec[1].pszName = L"All files";
		}

		hr = CoCreateInstance(save?&CLSID_FileSaveDialog:&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileDialog, (LPVOID)&pfd);

		if (FAILED(hr)) {
			uprintf("CoCreateInstance for FileOpenDialog failed: error %X\n", hr);
			pfd = NULL;	// Just in case
			goto fallback;
		}

		// Set the file extension filters
		pfd->lpVtbl->SetFileTypes(pfd, 2, filter_spec);

		// Set the default directory
		wpath = utf8_to_wchar(path);
		hr = (*pSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID) &si_path);
		if (SUCCEEDED(hr)) {
			pfd->lpVtbl->SetFolder(pfd, si_path);
		}
		safe_free(wpath);

		// Set the default filename
		wfilename = utf8_to_wchar(filename);
		if (wfilename != NULL) {
			pfd->lpVtbl->SetFileName(pfd, wfilename);
		}

		// Display the dialog
		hr = pfd->lpVtbl->Show(pfd, hMainDialog);

		// Cleanup
		safe_free(wfilename);
		safe_free(filter_spec[0].pszSpec);
		safe_free(filter_spec[0].pszName);

		if (SUCCEEDED(hr)) {
			// Obtain the result of the user's interaction with the dialog.
			hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
			if (SUCCEEDED(hr)) {
				hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &wpath);
				if (SUCCEEDED(hr)) {
					filepath = wchar_to_utf8(wpath);
					CoTaskMemFree(wpath);
				}
				psiResult->lpVtbl->Release(psiResult);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			uprintf("Could not show FileOpenDialog: error %X\n", hr);
			goto fallback;
		}
		pfd->lpVtbl->Release(pfd);
		return filepath;
	}

fallback:
	if (pfd != NULL) {
		pfd->lpVtbl->Release(pfd);
	}
#endif

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hMainDialog;
	// File name
	safe_strcpy(selected_name, MAX_PATH, filename);
	ofn.lpstrFile = selected_name;
	ofn.nMaxFile = MAX_PATH;
	// Set the file extension filters
	ext_strlen = safe_strlen(ext_desc) + 2*safe_strlen(ext) + sizeof(" (*.)\0*.\0All Files (*.*)\0*.*\0\0");
	ext_string = (char*)malloc(ext_strlen);
	safe_sprintf(ext_string, ext_strlen, "%s (*.%s)\r*.%s\rAll Files (*.*)\r*.*\r\0", ext_desc, ext, ext);
	// Microsoft could really have picked a better delimiter!
	for (i=0; i<ext_strlen; i++) {
		if (ext_string[i] == '\r') {
			ext_string[i] = 0;
		}
	}
	ofn.lpstrFilter = ext_string;
	// Initial dir
	ofn.lpstrInitialDir = path;
	ofn.Flags = OFN_OVERWRITEPROMPT;
	// Show Dialog
	if (save) {
		r = GetSaveFileNameU(&ofn);
	} else {
		r = GetOpenFileNameU(&ofn);
	}
	if (r) {
		filepath = safe_strdup(selected_name);
	} else {
		tmp = CommDlgExtendedError();
		if (tmp != 0) {
			uprintf("Could not selected file for %s. Error %X\n", save?"save":"open", tmp);
		}
	}
	safe_free(ext_string);
	return filepath;
}

/*
 * Create the application status bar
 */
void CreateStatusBar(void)
{
	RECT rect;
	int edge[2];

	// Create the status bar.
	hStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_STATUS,  hMainInstance, NULL);

	// Create 2 status areas
	GetClientRect(hMainDialog, &rect);
	edge[0] = rect.right - (int)(58.0f*fScale);
	edge[1] = rect.right;
	SendMessage(hStatus, SB_SETPARTS, (WPARAM) 2, (LPARAM)&edge);
}

/*
 * Center a dialog with regards to the main application Window or the desktop
 */
void CenterDialog(HWND hDlg)
{
	POINT Point;
	HWND hParent;
	RECT DialogRect;
	RECT ParentRect;
	int nWidth;
	int nHeight;

	// Get the size of the dialog box.
	GetWindowRect(hDlg, &DialogRect);

	// Get the parent
	hParent = GetParent(hDlg);
	if (hParent == NULL) {
		hParent = GetDesktopWindow();
	}
	GetClientRect(hParent, &ParentRect);

	// Calculate the height and width of the current dialog
	nWidth = DialogRect.right - DialogRect.left;
	nHeight = DialogRect.bottom - DialogRect.top;

	// Find the center point and convert to screen coordinates.
	Point.x = (ParentRect.right - ParentRect.left) / 2;
	Point.y = (ParentRect.bottom - ParentRect.top) / 2;
	ClientToScreen(hParent, &Point);

	// Calculate the new x, y starting point.
	Point.x -= nWidth / 2;
	Point.y -= nHeight / 2 + 35;

	// Move the window.
	MoveWindow(hDlg, Point.x, Point.y, nWidth, nHeight, FALSE);
}

/*
 * License callback
 */
INT_PTR CALLBACK LicenseCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		SetDlgItemTextA(hDlg, IDC_LICENSE_TEXT, gplv3);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

/*
 * About dialog callback
 */
INT_PTR CALLBACK AboutCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;
	const int edit_id[2] = {IDC_ABOUT_BLURB, IDC_ABOUT_COPYRIGHTS};
	char about_blurb[1024];
	const char* edit_text[2] = {about_blurb, additional_copyrights};
	HWND hEdit[2];
	TEXTRANGEW tr;
	ENLINK* enl;
	wchar_t wUrl[256];

	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		if (reg_commcheck)
			ShowWindow(GetDlgItem(hDlg, IDC_ABOUT_UPDATES), SW_SHOW);
		safe_sprintf(about_blurb, sizeof(about_blurb), about_blurb_format, 
			rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]);
		for (i=0; i<ARRAYSIZE(hEdit); i++) {
			hEdit[i] = GetDlgItem(hDlg, edit_id[i]);
			SendMessage(hEdit[i], EM_AUTOURLDETECT, 1, 0);
			/* Can't use SetDlgItemText, because it only works with RichEdit20A... and VS insists
			 * on reverting to RichEdit20W as soon as you edit the dialog. You can try all the W
			 * methods you want, it JUST WON'T WORK unless you use EM_SETTEXTEX. Also see:
			 * http://blog.kowalczyk.info/article/eny/Setting-unicode-rtf-text-in-rich-edit-control.html */
			SendMessageA(hEdit[i], EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)edit_text[i]);
			SendMessage(hEdit[i], EM_SETSEL, -1, -1);
			SendMessage(hEdit[i], EM_SETEVENTMASK, 0, ENM_LINK);
			SendMessage(hEdit[i], EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
		}
		break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case EN_LINK:
			enl = (ENLINK*) lParam;
			if (enl->msg == WM_LBUTTONUP) {
				tr.lpstrText = wUrl;
				tr.chrg.cpMin = enl->chrg.cpMin;
				tr.chrg.cpMax = enl->chrg.cpMax;
				SendMessageW(enl->nmhdr.hwndFrom, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				ShellExecuteW(hDlg, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_ABOUT_LICENSE:
			DialogBoxA(hMainInstance, MAKEINTRESOURCEA(IDD_LICENSE), hDlg, LicenseCallback);
			break;
		case IDC_ABOUT_UPDATES:
			DialogBoxA(hMainInstance, MAKEINTRESOURCEA(IDD_UPDATE_POLICY), hDlg, UpdateCallback);
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

INT_PTR CreateAboutBox(void)
{
	return DialogBoxA(hMainInstance, MAKEINTRESOURCEA(IDD_ABOUTBOX), hMainDialog, AboutCallback);
}

/*
 * We use our own MessageBox for notifications to have greater control (center, no close button, etc)
 */
INT_PTR CALLBACK NotificationCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT loc;
	int i;
	// Prevent resizing
	static LRESULT disabled[9] = { HTLEFT, HTRIGHT, HTTOP, HTBOTTOM, HTSIZE,
		HTTOPLEFT, HTTOPRIGHT, HTBOTTOMLEFT, HTBOTTOMRIGHT };
	static HBRUSH white_brush, separator_brush;

	switch (message) {
	case WM_INITDIALOG:
		white_brush = CreateSolidBrush(WHITE);
		separator_brush = CreateSolidBrush(SEPARATOR_GREY);
		CenterDialog(hDlg);
		// Change the default icon
		if (Static_SetIcon(GetDlgItem(hDlg, IDC_NOTIFICATION_ICON), hMessageIcon) == 0) {
			uprintf("Could not set dialog icon\n");
		}
		// Set the dialog title
		if (szMessageTitle != NULL) {
			SetWindowTextA(hDlg, szMessageTitle);
		}
		// Enable/disable the buttons and set text
		if (!notification_is_question) {
			SetWindowTextA(GetDlgItem(hDlg, IDNO), "Close");
		} else {
			ShowWindow(GetDlgItem(hDlg, IDYES), SW_SHOW);
		}
		if ((notification_more_info != NULL) && (notification_more_info->callback != NULL)) {
			ShowWindow(GetDlgItem(hDlg, IDC_MORE_INFO), SW_SHOW);
		}
		// Set the control text
		if (szMessageText != NULL) {
			SetWindowTextA(GetDlgItem(hDlg, IDC_NOTIFICATION_TEXT), szMessageText);
		}
		return (INT_PTR)TRUE;
	case WM_CTLCOLORSTATIC:
		// Change the background colour for static text and icon
		SetBkMode((HDC)wParam, TRANSPARENT);
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_NOTIFICATION_LINE)) {
			return (INT_PTR)separator_brush;
		}
		return (INT_PTR)white_brush;
	case WM_NCHITTEST:
		// Check coordinates to prevent resize actions
		loc = DefWindowProc(hDlg, message, wParam, lParam);
		for(i = 0; i < 9; i++) {
			if (loc == disabled[i]) {
				return (INT_PTR)TRUE;
			}
		}
		return (INT_PTR)FALSE;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
		case IDYES:
		case IDNO:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_MORE_INFO:
			if (notification_more_info != NULL)
				DialogBoxA(hMainInstance, MAKEINTRESOURCEA(notification_more_info->id),
					hDlg, notification_more_info->callback);
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Display a custom notification
 */
BOOL Notification(int type, const notification_info* more_info, char* title, char* format, ...)
{
	BOOL ret;
	va_list args;
	szMessageText = (char*)malloc(MAX_PATH);
	if (szMessageText == NULL) return FALSE;
	szMessageTitle = title;
	va_start(args, format);
	safe_vsnprintf(szMessageText, MAX_PATH-1, format, args);
	va_end(args);
	szMessageText[MAX_PATH-1] = 0;
	notification_more_info = more_info;
	notification_is_question = FALSE;

	switch(type) {
	case MSG_WARNING:
		hMessageIcon = LoadIcon(NULL, IDI_WARNING);
		break;
	case MSG_ERROR:
		hMessageIcon = LoadIcon(NULL, IDI_ERROR);
		break;
	case MSG_QUESTION:
		hMessageIcon = LoadIcon(NULL, IDI_QUESTION);
		notification_is_question = TRUE;
		break;
	case MSG_INFO:
	default:
		hMessageIcon = LoadIcon(NULL, IDI_INFORMATION);
		break;
	}
	ret = (DialogBox(hMainInstance, MAKEINTRESOURCE(IDD_NOTIFICATION), hMainDialog, NotificationCallback) == IDYES);
	safe_free(szMessageText);
	return ret;
}

static struct {
	HWND hTip;		// Tooltip handle
	HWND hCtrl;		// Handle of the control the tooltip belongs to
	WNDPROC original_proc;
	LPWSTR wstring;
} ttlist[MAX_TOOLTIPS] = { {0} };

INT_PTR CALLBACK TooltipCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	LPNMTTDISPINFOW lpnmtdi;
	int i = MAX_TOOLTIPS;

	// Make sure we have an original proc
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == hDlg) break;
	}
	if (i == MAX_TOOLTIPS) {
		return (INT_PTR)FALSE;
	}

	switch (message)
	{
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case TTN_GETDISPINFOW:
			lpnmtdi = (LPNMTTDISPINFOW)lParam;
			lpnmtdi->lpszText = ttlist[i].wstring;
			SendMessage(hDlg, TTM_SETMAXTIPWIDTH, 0, 300);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(ttlist[i].original_proc, hDlg, message, wParam, lParam);
}

/*
 * Create a tooltip for the control passed as first parameter
 * duration sets the duration in ms. Use -1 for default
 * message is an UTF-8 string
 */
BOOL CreateTooltip(HWND hControl, const char* message, int duration)
{
	TOOLINFOW toolInfo = {0};
	int i;

	if ( (hControl == NULL) || (message == NULL) ) {
		return FALSE;
	}

	// Destroy existing tooltip if any
	DestroyTooltip(hControl);

	// Find an empty slot
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) break;
	}
	if (i == MAX_TOOLTIPS) {
		uprintf("Maximum number of tooltips reached\n");
		return FALSE;
	}

	// Create the tooltip window
	ttlist[i].hTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hMainDialog, NULL,
		hMainInstance, NULL);

	if (ttlist[i].hTip == NULL) {
		return FALSE;
	}
	ttlist[i].hCtrl = hControl;

	// Subclass the tooltip to handle multiline
	ttlist[i].original_proc = (WNDPROC)SetWindowLongPtr(ttlist[i].hTip, GWLP_WNDPROC, (LONG_PTR)TooltipCallback);

	// Set the string to display (can be multiline)
	ttlist[i].wstring = utf8_to_wchar(message);

	// Set tooltip duration (ms)
	PostMessage(ttlist[i].hTip, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)duration);

	// Associate the tooltip to the control
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.hwnd = ttlist[i].hTip;	// Set to the tooltip itself to ease up subclassing
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	toolInfo.uId = (UINT_PTR)hControl;
	toolInfo.lpszText = LPSTR_TEXTCALLBACKW;
	SendMessageW(ttlist[i].hTip, TTM_ADDTOOLW, 0, (LPARAM)&toolInfo);

	return TRUE;
}

/* Destroy a tooltip. hCtrl = handle of the control the tooltip is associated with */
void DestroyTooltip(HWND hControl)
{
	int i;

	if (hControl == NULL) return;
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hCtrl == hControl) break;
	}
	if (i == MAX_TOOLTIPS) return;
	DestroyWindow(ttlist[i].hTip);
	safe_free(ttlist[i].wstring);
	ttlist[i].original_proc = NULL;
	ttlist[i].hTip = NULL;
	ttlist[i].hCtrl = NULL;
}

void DestroyAllTooltips(void)
{
	int i;

	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) continue;
		DestroyWindow(ttlist[i].hTip);
		safe_free(ttlist[i].wstring);
	}
}

/* Determine if a Windows is being displayed or not */
BOOL IsShown(HWND hDlg)
{
	WINDOWPLACEMENT placement;
	if (!GetWindowPlacement(hDlg, &placement))
		return FALSE;
	switch (placement.showCmd) {
	case SW_SHOWNORMAL:
	case SW_SHOWMAXIMIZED:
	case SW_SHOW:
	case SW_SHOWDEFAULT:
		return TRUE;
	default:
		return FALSE;
	}
}

/* Compute the width of a dropdown list entry */
LONG GetEntryWidth(HWND hDropDown, const char *entry)
{ 
	HDC hDC;
	HFONT hFont, hDefFont = NULL;
	SIZE size;
	WCHAR* wentry = NULL;
	int len;

	hDC = GetDC(hDropDown);
	hFont = (HFONT)SendMessage(hDropDown, WM_GETFONT, 0, 0);
	if (hFont != NULL)
		hDefFont = (HFONT)SelectObject(hDC, hFont);
	
	wentry = utf8_to_wchar(entry);
	len = (int)wcslen(wentry)+1;
	GetTextExtentPoint32W(hDC, wentry, len, &size);

	if (hFont != NULL)
		SelectObject(hDC, hDefFont);

	ReleaseDC(hDropDown, hDC);
	free(wentry);
	return size.cx;
}

/*
 * Windows 7 taskbar icon handling (progress bar overlay, etc)
 * Some platforms don't have these, so we redefine
 */
typedef enum MY_STPFLAG
{
	MY_STPF_NONE = 0,
	MY_STPF_USEAPPTHUMBNAILALWAYS = 0x1,
	MY_STPF_USEAPPTHUMBNAILWHENACTIVE = 0x2,
	MY_STPF_USEAPPPEEKALWAYS = 0x4,
	MY_STPF_USEAPPPEEKWHENACTIVE = 0x8
} MY_STPFLAG;

typedef enum MY_THUMBBUTTONMASK
{
	MY_THB_BITMAP = 0x1,
	MY_THB_ICON = 0x2,
	MY_THB_TOOLTIP = 0x4,
	MY_THB_FLAGS = 0x8
} MY_THUMBBUTTONMASK;

typedef enum MY_THUMBBUTTONFLAGS
{
	MY_THBF_ENABLED = 0,
	MY_THBF_DISABLED = 0x1,
	MY_THBF_DISMISSONCLICK = 0x2,
	MY_THBF_NOBACKGROUND = 0x4,
	MY_THBF_HIDDEN = 0x8,
	MY_THBF_NONINTERACTIVE = 0x10
} MY_THUMBBUTTONFLAGS;

typedef struct MY_THUMBBUTTON
{
	MY_THUMBBUTTONMASK dwMask;
	UINT iId;
	UINT iBitmap;
	HICON hIcon;
	WCHAR szTip[260];
	MY_THUMBBUTTONFLAGS dwFlags;
} MY_THUMBBUTTON;

/*
typedef enum MY_TBPFLAG
{
	TASKBAR_NOPROGRESS = 0,
	TASKBAR_INDETERMINATE = 0x1,
	TASKBAR_NORMAL = 0x2,
	TASKBAR_ERROR = 0x4,
	TASKBAR_PAUSED = 0x8
} MY_TBPFLAG;
*/

#pragma push_macro("INTERFACE")
#undef  INTERFACE
#define INTERFACE my_ITaskbarList3
DECLARE_INTERFACE_(my_ITaskbarList3, IUnknown) {
	STDMETHOD (QueryInterface) (THIS_ REFIID riid, LPVOID *ppvObj) PURE;
	STDMETHOD_(ULONG, AddRef) (THIS) PURE;
	STDMETHOD_(ULONG, Release) (THIS) PURE;
	STDMETHOD (HrInit) (THIS) PURE;
	STDMETHOD (AddTab) (THIS_ HWND hwnd) PURE;
	STDMETHOD (DeleteTab) (THIS_ HWND hwnd) PURE;
	STDMETHOD (ActivateTab) (THIS_ HWND hwnd) PURE;
	STDMETHOD (SetActiveAlt) (THIS_ HWND hwnd) PURE;
	STDMETHOD (MarkFullscreenWindow) (THIS_ HWND hwnd, int fFullscreen) PURE;
	STDMETHOD (SetProgressValue) (THIS_ HWND hwnd, ULONGLONG ullCompleted, ULONGLONG ullTotal) PURE;
	STDMETHOD (SetProgressState) (THIS_ HWND hwnd, TASKBAR_PROGRESS_FLAGS tbpFlags) PURE;
	STDMETHOD (RegisterTab) (THIS_ HWND hwndTab,HWND hwndMDI) PURE;
	STDMETHOD (UnregisterTab) (THIS_ HWND hwndTab) PURE;
	STDMETHOD (SetTabOrder) (THIS_ HWND hwndTab, HWND hwndInsertBefore) PURE;
	STDMETHOD (SetTabActive) (THIS_ HWND hwndTab, HWND hwndMDI, DWORD dwReserved) PURE;
	STDMETHOD (ThumbBarAddButtons) (THIS_ HWND hwnd, UINT cButtons, MY_THUMBBUTTON* pButton) PURE;
	STDMETHOD (ThumbBarUpdateButtons) (THIS_ HWND hwnd, UINT cButtons, MY_THUMBBUTTON* pButton) PURE;
	STDMETHOD (ThumbBarSetImageList) (THIS_ HWND hwnd, HIMAGELIST himl) PURE;
	STDMETHOD (SetOverlayIcon) (THIS_ HWND hwnd, HICON hIcon, LPCWSTR pszDescription) PURE;
	STDMETHOD (SetThumbnailTooltip) (THIS_ HWND hwnd, LPCWSTR pszTip) PURE;
	STDMETHOD (SetThumbnailClip) (THIS_ HWND hwnd, RECT *prcClip) PURE;
};
const IID my_IID_ITaskbarList3 = 
	{ 0xea1afb91, 0x9e28, 0x4b86, { 0x90, 0xe9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf } };
const IID my_CLSID_TaskbarList = 
	{ 0x56fdf344, 0xfd6d, 0x11d0, { 0x95, 0x8a ,0x0, 0x60, 0x97, 0xc9, 0xa0 ,0x90 } };

static my_ITaskbarList3* ptbl = NULL;

// Create a taskbar icon progressbar
BOOL CreateTaskbarList(void)
{
	HRESULT hr;
	if (nWindowsVersion < WINDOWS_7)
		// Only valid for Windows 7 or later
		return FALSE;
	hr = CoCreateInstance(&my_CLSID_TaskbarList, NULL, CLSCTX_ALL, &my_IID_ITaskbarList3, (LPVOID)&ptbl);
	if (FAILED(hr)) {
		uprintf("CoCreateInstance for TaskbarList failed: error %X\n", hr);
		ptbl = NULL;
		return FALSE;
	}
	return TRUE;
}

BOOL SetTaskbarProgressState(TASKBAR_PROGRESS_FLAGS tbpFlags)
{
	if (ptbl == NULL)
		return FALSE;
	return !FAILED(ptbl->lpVtbl->SetProgressState(ptbl, hMainDialog, tbpFlags));
}

BOOL SetTaskbarProgressValue(ULONGLONG ullCompleted, ULONGLONG ullTotal)
{
	if (ptbl == NULL)
		return FALSE;
	return !FAILED(ptbl->lpVtbl->SetProgressValue(ptbl, hMainDialog, ullCompleted, ullTotal));
}
#pragma pop_macro("INTERFACE")


/*
 * Update policy and settings dialog callback
 */
INT_PTR CALLBACK UpdateCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND hPolicy;
	static HWND hFrequency, hBeta;

	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		hFrequency = GetDlgItem(hDlg, IDC_UPDATE_FREQUENCY);
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Disabled"), -1));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Daily (Default)"), 86400));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Weekly"), 604800));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, "Monthly"), 2629800));
		IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 1));
		hBeta = GetDlgItem(hDlg, IDC_INCLUDE_BETAS);
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, "Yes"));
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, "No"));
		IGNORE_RETVAL(ComboBox_SetCurSel(hBeta, 1));
		hPolicy = GetDlgItem(hDlg, IDC_POLICY);
		SendMessage(hPolicy, EM_AUTOURLDETECT, 1, 0);
		SendMessageA(hPolicy, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)update_policy);
		SendMessage(hPolicy, EM_SETSEL, -1, -1);
		SendMessage(hPolicy, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessageA(hPolicy, EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_UPDATE_FREQUENCY:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			WriteRegistryKey32(REGKEY_UPDATE_INTERVAL, (DWORD)ComboBox_GetItemData(hFrequency, ComboBox_GetCurSel(hFrequency)));
			return (INT_PTR)TRUE;
		case IDC_INCLUDE_BETAS:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetRegistryKeyBool(REGKEY_INCLUDE_BETAS, ComboBox_GetCurSel(hBeta) == 0);
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Initial update check setup
 */
BOOL SetUpdateCheck(void)
{
	BOOL enable_updates;
	DWORD commcheck = GetTickCount();
	notification_info more_info = { IDD_UPDATE_POLICY, UpdateCallback };

	// Test if we have access to the registry. If not, forget it.
	WriteRegistryKey32(REGKEY_COMM_CHECK, commcheck);
	if (ReadRegistryKey32(REGKEY_COMM_CHECK) != commcheck)
		return FALSE;
	reg_commcheck = TRUE;

	// If the update interval is not set, this is the first time we run so prompt the user
	if (ReadRegistryKey32(REGKEY_UPDATE_INTERVAL) == 0) {
		enable_updates = Notification(MSG_QUESTION, &more_info,
			APPLICATION_NAME " updates", "Do you want to allow " APPLICATION_NAME " to check for updates?\n");
		if (!enable_updates) {
			WriteRegistryKey32(REGKEY_UPDATE_INTERVAL, -1);	// large enough
			return FALSE;
		}
		// If the user hasn't set the interval in the dialog, set to default
		if ( (ReadRegistryKey32(REGKEY_UPDATE_INTERVAL) == 0) ||
			 ((ReadRegistryKey32(REGKEY_UPDATE_INTERVAL) == -1) && enable_updates) )
			WriteRegistryKey32(REGKEY_UPDATE_INTERVAL, 86400);
	}
	// TODO: check for lastcheck + interval & launch the background thread here?
	// TODO: make sure we check for updates if user just accepted
	return TRUE;
}

/*
 * New version notification dialog
 */
static const char* release_notes;
static const char* download_url;
INT_PTR CALLBACK NewVersionCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;
	HWND hNotes;
	TEXTRANGEW tr;
	ENLINK* enl;
	wchar_t wUrl[256];
	char* filepath;

	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		hNotes = GetDlgItem(hDlg, IDC_RELEASE_NOTES);
		SendMessage(hNotes, EM_AUTOURLDETECT, 1, 0);
		SendMessageA(hNotes, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)release_notes);
		SendMessage(hNotes, EM_SETSEL, -1, -1);
		SendMessage(hNotes, EM_SETEVENTMASK, 0, ENM_LINK);
		break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case EN_LINK:
			enl = (ENLINK*) lParam;
			if (enl->msg == WM_LBUTTONUP) {
				tr.lpstrText = wUrl;
				tr.chrg.cpMin = enl->chrg.cpMin;
				tr.chrg.cpMax = enl->chrg.cpMax;
				SendMessageW(enl->nmhdr.hwndFrom, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
				ShellExecuteW(hDlg, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_DOWNLOAD:
			if (download_url == NULL)
				return (INT_PTR)TRUE;
			for (i=(int)safe_strlen(download_url); (i>0)&&(download_url[i]!='/'); i--);
			filepath = FileDialog(TRUE, app_dir, (char*)&download_url[i+1], "exe", "Application");
			if (filepath != NULL) {
				// TODO: Do we want to close the release notes once download starts?
//				EndDialog(hDlg, 0);
//				SetFocus(hMainDialog);
				DownloadFile(download_url, filepath);
				safe_free(filepath);
			}
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

INT_PTR NewVersionDialog(const char* notes, const char* url)
{
	release_notes = notes;
	download_url = url;
	return DialogBoxA(hMainInstance, MAKEINTRESOURCEA(IDD_NEW_VERSION), hMainDialog, NewVersionCallback);
}
