/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard Dialog Routines (Browse for folder, About, etc)
 * Copyright © 2011-2014 Pete Batard <pete@akeo.ie>
 *
 * Based on zadig_stdlg.c, part of libwdi: http://libwdi.akeo.ie
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
#include <richedit.h>

#include "rufus.h"
#include "msapi_utf8.h"
#include "registry.h"
#include "resource.h"
#include "license.h"
#include "localization.h"

#if (_WIN32_WINNT >= 0x0600)
PF_TYPE_DECL(WINAPI, HRESULT, SHCreateItemFromParsingName, (PCWSTR, IBindCtx*, REFIID, void **));
#endif
PF_TYPE_DECL(WINAPI, LPITEMIDLIST, SHSimpleIDListFromPath, (PCWSTR pszPath));
#define INIT_VISTA_SHELL32         PF_INIT(SHCreateItemFromParsingName, Shell32)
#define INIT_XP_SHELL32            PF_INIT(SHSimpleIDListFromPath, Shell32)
#define IS_VISTA_SHELL32_AVAILABLE (pfSHCreateItemFromParsingName != NULL)

/* Globals */
static HICON hMessageIcon = (HICON)INVALID_HANDLE_VALUE;
static char* szMessageText = NULL;
static char* szMessageTitle = NULL;
static HWND hBrowseEdit;
static WNDPROC pOrgBrowseWndproc;
static const SETTEXTEX friggin_microsoft_unicode_amateurs = {ST_DEFAULT, CP_UTF8};
static BOOL notification_is_question;
static const notification_info* notification_more_info;
static BOOL reg_commcheck = FALSE;
static WNDPROC original_wndproc = NULL;

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
			pidl = (*pfSHSimpleIDListFromPath)(wpath);
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

	dialog_showing++;
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

		hr = (*pfSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID)&si_path);
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
		dialog_showing--;
		return;
	}
fallback:
	if (pfod != NULL) {
		pfod->lpVtbl->Release(pfod);
	}
#else
	dialog_showing++;
#endif
	INIT_XP_SHELL32;
	memset(&bi, 0, sizeof(BROWSEINFOW));
	bi.hwndOwner = hMainDialog;
	bi.lpszTitle = utf8_to_wchar(lmprintf(MSG_106));
	bi.lpfn = BrowseInfoCallback;
	// BIF_NONEWFOLDERBUTTON = 0x00000200 is unknown on MinGW
	bi.ulFlags = BIF_RETURNFSANCESTORS | BIF_RETURNONLYFSDIRS |
		BIF_DONTGOBELOWDOMAIN | BIF_EDITBOX | 0x00000200;
	pidl = SHBrowseForFolderW(&bi);
	if (pidl != NULL) {
		CoTaskMemFree(pidl);
	}
	safe_free(bi.lpszTitle);
	dialog_showing--;
}

/*
 * Return the UTF8 path of a file selected through a load or save dialog
 * Will use the newer IFileOpenDialog if *compiled* for Vista or later
 * All string parameters are UTF-8
 * IMPORTANT NOTE: On Vista and later, remember that you need to call
 * CoInitializeEx() for *EACH* thread you invoke FileDialog from, as
 * GetDisplayName() will return error 0x8001010E otherwise.
 */
char* FileDialog(BOOL save, char* path, const ext_t* ext, DWORD options)
{
	DWORD tmp;
	OPENFILENAMEA ofn;
	char selected_name[MAX_PATH];
	char *ext_string = NULL, *all_files = NULL;
	size_t i, j, ext_strlen;
	BOOL r;
	char* filepath = NULL;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	HRESULT hr = FALSE;
	IFileDialog *pfd = NULL;
	IShellItem *psiResult;
	COMDLG_FILTERSPEC* filter_spec;
	wchar_t *wpath = NULL, *wfilename = NULL;
	IShellItem *si_path = NULL;	// Automatically freed
#endif

	if ((ext == NULL) || (ext->count == 0) || (ext->extension == NULL) || (ext->description == NULL))
		return NULL;
	dialog_showing++;

#if (_WIN32_WINNT >= 0x0600)	// Vista and later
	INIT_VISTA_SHELL32;
	filter_spec = (COMDLG_FILTERSPEC*)calloc(ext->count + 1, sizeof(COMDLG_FILTERSPEC));
	if ((IS_VISTA_SHELL32_AVAILABLE) && (filter_spec != NULL)) {
		// Setup the file extension filter table
		for (i=0; i<ext->count; i++) {
			filter_spec[i].pszSpec = utf8_to_wchar(ext->extension[i]);
			filter_spec[i].pszName = utf8_to_wchar(ext->description[i]);
		}
		filter_spec[i].pszSpec = L"*.*";
		filter_spec[i].pszName = utf8_to_wchar(lmprintf(MSG_107));

		hr = CoCreateInstance(save?&CLSID_FileSaveDialog:&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC,
			&IID_IFileDialog, (LPVOID)&pfd);

		if (FAILED(hr)) {
			SetLastError(hr);
			uprintf("CoCreateInstance for FileOpenDialog failed: %s\n", WindowsErrorString());
			pfd = NULL;	// Just in case
			goto fallback;
		}

		// Set the file extension filters
		pfd->lpVtbl->SetFileTypes(pfd, (UINT)ext->count+1, filter_spec);

		// Set the default directory
		wpath = utf8_to_wchar(path);
		hr = (*pfSHCreateItemFromParsingName)(wpath, NULL, &IID_IShellItem, (LPVOID) &si_path);
		if (SUCCEEDED(hr)) {
			pfd->lpVtbl->SetFolder(pfd, si_path);
		}
		safe_free(wpath);

		// Set the default filename
		wfilename = utf8_to_wchar((ext->filename == NULL)?ext->extension[0]:ext->filename);
		if (wfilename != NULL) {
			pfd->lpVtbl->SetFileName(pfd, wfilename);
		}

		// Display the dialog
		hr = pfd->lpVtbl->Show(pfd, hMainDialog);

		// Cleanup
		safe_free(wfilename);
		for (i=0; i<ext->count; i++) {
			safe_free(filter_spec[i].pszSpec);
			safe_free(filter_spec[i].pszName);
		}
		safe_free(filter_spec[i].pszName);
		safe_free(filter_spec);

		if (SUCCEEDED(hr)) {
			// Obtain the result of the user's interaction with the dialog.
			hr = pfd->lpVtbl->GetResult(pfd, &psiResult);
			if (SUCCEEDED(hr)) {
				hr = psiResult->lpVtbl->GetDisplayName(psiResult, SIGDN_FILESYSPATH, &wpath);
				if (SUCCEEDED(hr)) {
					filepath = wchar_to_utf8(wpath);
					CoTaskMemFree(wpath);
				} else {
					SetLastError(hr);
					uprintf("Unable to access file path: %s\n", WindowsErrorString());
				}
				psiResult->lpVtbl->Release(psiResult);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			SetLastError(hr);
			uprintf("Could not show FileOpenDialog: %s\n", WindowsErrorString());
			goto fallback;
		}
		pfd->lpVtbl->Release(pfd);
		dialog_showing--;
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
	// Selected File name
	static_sprintf(selected_name, "%s", (ext->filename == NULL)?ext->extension[0]:ext->filename);
	ofn.lpstrFile = selected_name;
	ofn.nMaxFile = MAX_PATH;
	// Set the file extension filters
	all_files = lmprintf(MSG_107);
	ext_strlen = 0;
	for (i=0; i<ext->count; i++) {
		ext_strlen += safe_strlen(ext->description[i]) + 2*safe_strlen(ext->extension[i]) + sizeof(" ()\r\r");
	}
	ext_strlen += safe_strlen(all_files) + sizeof(" (*.*)\r*.*\r");
	ext_string = (char*)malloc(ext_strlen+1);
	if (ext_string == NULL)
		return NULL;
	ext_string[0] = 0;
	for (i=0, j=0; i<ext->count; i++) {
		j += _snprintf(&ext_string[j], ext_strlen-j, "%s (%s)\r%s\r", ext->description[i], ext->extension[i], ext->extension[i]);
	}
	j = _snprintf(&ext_string[j], ext_strlen-j, "%s (*.*)\r*.*\r", all_files);
	// Microsoft could really have picked a better delimiter!
	for (i=0; i<ext_strlen; i++) {
		if (ext_string[i] == '\r') {
			ext_string[i] = 0;
		}
	}
	ofn.lpstrFilter = ext_string;
	ofn.nFilterIndex = 1;
	ofn.lpstrInitialDir = path;
	ofn.Flags = OFN_OVERWRITEPROMPT | options;
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
			uprintf("Could not select file for %s. Error %X\n", save?"save":"open", tmp);
		}
	}
	safe_free(ext_string);
	dialog_showing--;
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
 * See http://msdn.microsoft.com/en-us/library/windows/desktop/ms644996.aspx#init_box
 */
void CenterDialog(HWND hDlg)
{
	HWND hParent;
	RECT rc, rcDlg, rcParent;

	if ((hParent = GetParent(hDlg)) == NULL) {
		hParent = GetDesktopWindow();
	}

	GetWindowRect(hParent, &rcParent);
	GetWindowRect(hDlg, &rcDlg);
	CopyRect(&rc, &rcParent);

	// Offset the parent and dialog box rectangles so that right and bottom
	// values represent the width and height, and then offset the parent again
	// to discard space taken up by the dialog box.
	OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top);
	OffsetRect(&rc, -rc.left, -rc.top);
	OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);

	SetWindowPos(hDlg, HWND_TOP, rcParent.left + (rc.right / 2), rcParent.top + (rc.bottom / 2) - 25, 0, 0, SWP_NOSIZE);
}

// http://stackoverflow.com/questions/431470/window-border-width-and-height-in-win32-how-do-i-get-it
SIZE GetBorderSize(HWND hDlg)
{
	RECT rect = {0, 0, 0, 0};
	SIZE size = {0, 0};
	WINDOWINFO wi;
	wi.cbSize = sizeof(WINDOWINFO);

	GetWindowInfo(hDlg, &wi);

	AdjustWindowRectEx(&rect, wi.dwStyle, FALSE, wi.dwExStyle);
	size.cx = rect.right - rect.left;
	size.cy = rect.bottom - rect.top;
	return size;
}

void ResizeMoveCtrl(HWND hDlg, HWND hCtrl, int dx, int dy, int dw, int dh)
{
	RECT rect;
	POINT point;
	SIZE border = {0, 0};

	GetWindowRect(hCtrl, &rect);
	point.x = right_to_left_mode?rect.right:rect.left;
	point.y = rect.top;
	ScreenToClient(hDlg, &point);
	GetClientRect(hCtrl, &rect);

	// If we're dealing with a dialog, we must take the border into account
	if ((hCtrl == hDlg) || (hDlg == NULL))
		border = GetBorderSize(hCtrl);
	MoveWindow(hCtrl, point.x + (int)(fScale*(float)dx), point.y + (int)(fScale*(float)dy),
		(rect.right - rect.left) + (int)(fScale*(float)dw + border.cx),
		(rect.bottom - rect.top) + (int)(fScale*(float)dh + border.cy), TRUE);
	InvalidateRect(hCtrl, NULL, TRUE);
}

/*
 * License callback
 */
INT_PTR CALLBACK LicenseCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_INITDIALOG:
		apply_localization(IDD_LICENSE, hDlg);
		CenterDialog(hDlg);
		SetDlgItemTextA(hDlg, IDC_LICENSE_TEXT, gplv3);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			reset_localization(IDD_LICENSE);
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
	char about_blurb[2048];
	const char* edit_text[2] = {about_blurb, additional_copyrights};
	HWND hEdit[2];
	TEXTRANGEW tr;
	ENLINK* enl;
	wchar_t wUrl[256];

	switch (message) {
	case WM_INITDIALOG:
		// Execute dialog localization
		apply_localization(IDD_ABOUTBOX, hDlg);
		SetTitleBarIcon(hDlg);
		CenterDialog(hDlg);
		if (reg_commcheck)
			ShowWindow(GetDlgItem(hDlg, IDC_ABOUT_UPDATES), SW_SHOW);
		safe_sprintf(about_blurb, sizeof(about_blurb), about_blurb_format, lmprintf(MSG_174),
			lmprintf(MSG_175, rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]),
			right_to_left_mode?"Akeo \\\\ Pete Batard 2011-2014 © Copyright":"Copyright © 2011-2014 Pete Batard / Akeo",
			lmprintf(MSG_176), lmprintf(MSG_177), lmprintf(MSG_178));
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
				wUrl[ARRAYSIZE(wUrl)-1] = 0;
				ShellExecuteW(hDlg, L"open", wUrl, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
		case IDCANCEL:
			reset_localization(IDD_ABOUTBOX);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_ABOUT_LICENSE:
			DialogBoxW(hMainInstance, MAKEINTRESOURCEW(IDD_LICENSE + IDD_IS_RTL), hDlg, LicenseCallback);
			break;
		case IDC_ABOUT_UPDATES:
			DialogBoxW(hMainInstance, MAKEINTRESOURCEW(IDD_UPDATE_POLICY + IDD_IS_RTL), hDlg, UpdateCallback);
			break;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

INT_PTR CreateAboutBox(void)
{
	INT_PTR r;
	dialog_showing++;
	r = DialogBoxW(hMainInstance, MAKEINTRESOURCEW(IDD_ABOUTBOX + IDD_IS_RTL), hMainDialog, AboutCallback);
	dialog_showing--;
	return r;
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
	// To use the system message font
	NONCLIENTMETRICS ncm;
	HFONT hDlgFont;

	switch (message) {
	case WM_INITDIALOG:
		// Get the system message box font. See http://stackoverflow.com/a/6057761
		ncm.cbSize = sizeof(ncm);
		// If we're compiling with the Vista SDK or later, the NONCLIENTMETRICS struct
		// will be the wrong size for previous versions, so we need to adjust it.
		#if defined(_MSC_VER) && (_MSC_VER >= 1500) && (WINVER >= 0x0600)
		if (nWindowsVersion >= WINDOWS_VISTA) {
			// In versions of Windows prior to Vista, the iPaddedBorderWidth member
			// is not present, so we need to subtract its size from cbSize.
			ncm.cbSize -= sizeof(ncm.iPaddedBorderWidth);
		}
		#endif
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
		hDlgFont = CreateFontIndirect(&(ncm.lfMessageFont));
		// Set the dialog to use the system message box font
		SendMessage(hDlg, WM_SETFONT, (WPARAM)hDlgFont, MAKELPARAM(TRUE, 0));
		SendMessage(GetDlgItem(hDlg, IDC_NOTIFICATION_TEXT), WM_SETFONT, (WPARAM)hDlgFont, MAKELPARAM(TRUE, 0));
		SendMessage(GetDlgItem(hDlg, IDC_MORE_INFO), WM_SETFONT, (WPARAM)hDlgFont, MAKELPARAM(TRUE, 0));
		SendMessage(GetDlgItem(hDlg, IDYES), WM_SETFONT, (WPARAM)hDlgFont, MAKELPARAM(TRUE, 0));
		SendMessage(GetDlgItem(hDlg, IDNO), WM_SETFONT, (WPARAM)hDlgFont, MAKELPARAM(TRUE, 0));

		apply_localization(IDD_NOTIFICATION, hDlg);
		white_brush = CreateSolidBrush(WHITE);
		separator_brush = CreateSolidBrush(SEPARATOR_GREY);
		SetTitleBarIcon(hDlg);
		CenterDialog(hDlg);
		// Change the default icon
		if (Static_SetIcon(GetDlgItem(hDlg, IDC_NOTIFICATION_ICON), hMessageIcon) == 0) {
			uprintf("Could not set dialog icon\n");
		}
		// Set the dialog title
		if (szMessageTitle != NULL) {
			SetWindowTextU(hDlg, szMessageTitle);
		}
		// Enable/disable the buttons and set text
		if (!notification_is_question) {
			SetWindowTextU(GetDlgItem(hDlg, IDNO), lmprintf(MSG_006));
		} else {
			ShowWindow(GetDlgItem(hDlg, IDYES), SW_SHOW);
		}
		if ((notification_more_info != NULL) && (notification_more_info->callback != NULL)) {
			ShowWindow(GetDlgItem(hDlg, IDC_MORE_INFO), SW_SHOW);
		}
		// Set the control text
		if (szMessageText != NULL) {
			SetWindowTextU(GetDlgItem(hDlg, IDC_NOTIFICATION_TEXT), szMessageText);
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
				DialogBoxW(hMainInstance, MAKEINTRESOURCEW(notification_more_info->id),
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

	dialog_showing++;
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
	ret = (DialogBoxW(hMainInstance, MAKEINTRESOURCEW(IDD_NOTIFICATION + IDD_IS_RTL), hMainDialog, NotificationCallback) == IDYES);
	safe_free(szMessageText);
	dialog_showing--;
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
	if (i >= MAX_TOOLTIPS) {
		uprintf("Maximum number of tooltips reached (%d)\n", MAX_TOOLTIPS);
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
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS | ((right_to_left_mode)?TTF_RTLREADING:0);
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
	if (i >= MAX_TOOLTIPS) return;
	DestroyWindow(ttlist[i].hTip);
	safe_free(ttlist[i].wstring);
	ttlist[i].original_proc = NULL;
	ttlist[i].hTip = NULL;
	ttlist[i].hCtrl = NULL;
}

void DestroyAllTooltips(void)
{
	int i, j;

	for (i=0, j=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) continue;
		j++;
		DestroyWindow(ttlist[i].hTip);
		safe_free(ttlist[i].wstring);
		ttlist[i].original_proc = NULL;
		ttlist[i].hTip = NULL;
		ttlist[i].hCtrl = NULL;
	}
}

/* Determine if a Windows is being displayed or not */
BOOL IsShown(HWND hDlg)
{
	WINDOWPLACEMENT placement = {0};
	placement.length = sizeof(WINDOWPLACEMENT);
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
	int32_t freq;
	char update_policy_text[4096];

	switch (message) {
	case WM_INITDIALOG:
		apply_localization(IDD_UPDATE_POLICY, hDlg);
		SetTitleBarIcon(hDlg);
		CenterDialog(hDlg);
		hFrequency = GetDlgItem(hDlg, IDC_UPDATE_FREQUENCY);
		hBeta = GetDlgItem(hDlg, IDC_INCLUDE_BETAS);
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, lmprintf(MSG_013)), -1));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, lmprintf(MSG_030, lmprintf(MSG_014))), 86400));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, lmprintf(MSG_015)), 604800));
		IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, lmprintf(MSG_016)), 2629800));
		freq = ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL);
		EnableWindow(GetDlgItem(hDlg, IDC_CHECK_NOW), (freq != 0));
		EnableWindow(hBeta, (freq >= 0));
		switch(freq) {
		case -1:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 0));
			break;
		case 0:
		case 86400:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 1));
			break;
		case 604800:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 2));
			break;
		case 2629800:
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 3));
			break;
		default:
			IGNORE_RETVAL(ComboBox_SetItemData(hFrequency, ComboBox_AddStringU(hFrequency, lmprintf(MSG_017)), freq));
			IGNORE_RETVAL(ComboBox_SetCurSel(hFrequency, 4));
			break;
		}
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, lmprintf(MSG_008)));
		IGNORE_RETVAL(ComboBox_AddStringU(hBeta, lmprintf(MSG_009)));
		IGNORE_RETVAL(ComboBox_SetCurSel(hBeta, GetRegistryKeyBool(REGKEY_HKCU, REGKEY_INCLUDE_BETAS)?0:1));
		hPolicy = GetDlgItem(hDlg, IDC_POLICY);
		SendMessage(hPolicy, EM_AUTOURLDETECT, 1, 0);
		safe_sprintf(update_policy_text, sizeof(update_policy_text), update_policy, lmprintf(MSG_179),
			lmprintf(MSG_180), lmprintf(MSG_181), lmprintf(MSG_182), lmprintf(MSG_183), lmprintf(MSG_184),
			lmprintf(MSG_185), lmprintf(MSG_186));
		SendMessageA(hPolicy, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)update_policy_text);
		SendMessage(hPolicy, EM_SETSEL, -1, -1);
		SendMessage(hPolicy, EM_SETEVENTMASK, 0, ENM_LINK);
		SendMessageA(hPolicy, EM_SETBKGNDCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			reset_localization(IDD_UPDATE_POLICY);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_CHECK_NOW:
			CheckForUpdates(TRUE);
			return (INT_PTR)TRUE;
		case IDC_UPDATE_FREQUENCY:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			freq = (int32_t)ComboBox_GetItemData(hFrequency, ComboBox_GetCurSel(hFrequency));
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, (DWORD)freq);
			EnableWindow(hBeta, (freq >= 0));
			return (INT_PTR)TRUE;
		case IDC_INCLUDE_BETAS:
			if (HIWORD(wParam) != CBN_SELCHANGE)
				break;
			SetRegistryKeyBool(REGKEY_HKCU, REGKEY_INCLUDE_BETAS, ComboBox_GetCurSel(hBeta) == 0);
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
	char filename[MAX_PATH] = "", exename[] = APPLICATION_NAME ".exe";
	size_t fn_len, exe_len;

	// Test if we have access to the registry. If not, forget it.
	WriteRegistryKey32(REGKEY_HKCU, REGKEY_COMM_CHECK, commcheck);
	if (ReadRegistryKey32(REGKEY_HKCU, REGKEY_COMM_CHECK) != commcheck)
		return FALSE;
	reg_commcheck = TRUE;

	// If the update interval is not set, this is the first time we run so prompt the user
	if (ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == 0) {

		// Add a hack for people who'd prefer the app not to prompt about update settings on first run.
		// If the executable is called "rufus.exe", without version, we disable the prompt
		GetModuleFileNameU(NULL, filename, sizeof(filename));
		fn_len = safe_strlen(filename);
		exe_len = safe_strlen(exename);
		if ((fn_len > exe_len) && (safe_stricmp(&filename[fn_len-exe_len], exename) == 0)) {
			uprintf("Short name used - Disabling initial update policy prompt\n");
			enable_updates = TRUE;
		} else {
			enable_updates = Notification(MSG_QUESTION, &more_info, lmprintf(MSG_004), lmprintf(MSG_005));
		}
		if (!enable_updates) {
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, -1);
			return FALSE;
		}
		// If the user hasn't set the interval in the dialog, set to default
		if ( (ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == 0) ||
			 ((ReadRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL) == -1) && enable_updates) )
			WriteRegistryKey32(REGKEY_HKCU, REGKEY_UPDATE_INTERVAL, 86400);
	}
	return TRUE;
}

static void CreateStaticFont(HDC dc, HFONT* hyperlink_font) {
	TEXTMETRIC tm;
	LOGFONT lf;

	if (*hyperlink_font != NULL)
		return;
	GetTextMetrics(dc, &tm);
	lf.lfHeight = tm.tmHeight;
	lf.lfWidth = 0;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = tm.tmWeight;
	lf.lfItalic = tm.tmItalic;
	lf.lfUnderline = TRUE;
	lf.lfStrikeOut = tm.tmStruckOut;
	lf.lfCharSet = tm.tmCharSet;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = tm.tmPitchAndFamily;
	GetTextFace(dc, LF_FACESIZE, lf.lfFaceName);
	*hyperlink_font = CreateFontIndirect(&lf);
}

/*
 * Work around the limitations of edit control, to display a hand cursor for hyperlinks
 * NB: The LTEXT control must have SS_NOTIFY attribute for this to work
 */
INT_PTR CALLBACK subclass_callback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SETCURSOR:
		if ((HWND)wParam == GetDlgItem(hDlg, IDC_WEBSITE)) {
			SetCursor(LoadCursor(NULL, IDC_HAND));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return CallWindowProc(original_wndproc, hDlg, message, wParam, lParam);
}

/*
 * New version notification dialog
 */
INT_PTR CALLBACK NewVersionCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	int i;
	HWND hNotes;
	char cmdline[] = APPLICATION_NAME " -w 150";
	static char* filepath = NULL;
	static int download_status = 0;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	HFONT hyperlink_font = NULL;
	EXT_DECL(dl_ext, NULL, __VA_GROUP__("*.exe"), __VA_GROUP__(lmprintf(MSG_037)));

	switch (message) {
	case WM_INITDIALOG:
		apply_localization(IDD_NEW_VERSION, hDlg);
		download_status = 0;
		SetTitleBarIcon(hDlg);
		CenterDialog(hDlg);
		// Subclass the callback so that we can change the cursor
		original_wndproc = (WNDPROC)SetWindowLongPtr(hDlg, GWLP_WNDPROC, (LONG_PTR)subclass_callback);
		hNotes = GetDlgItem(hDlg, IDC_RELEASE_NOTES);
		SendMessage(hNotes, EM_AUTOURLDETECT, 1, 0);
		SendMessageA(hNotes, EM_SETTEXTEX, (WPARAM)&friggin_microsoft_unicode_amateurs, (LPARAM)update.release_notes);
		SendMessage(hNotes, EM_SETSEL, -1, -1);
		SendMessage(hNotes, EM_SETEVENTMASK, 0, ENM_LINK);
		SetWindowTextU(GetDlgItem(hDlg, IDC_YOUR_VERSION), lmprintf(MSG_018, 
			rufus_version[0], rufus_version[1], rufus_version[2], rufus_version[3]));
		SetWindowTextU(GetDlgItem(hDlg, IDC_LATEST_VERSION), lmprintf(MSG_019,
			update.version[0], update.version[1], update.version[2], update.version[3]));
		SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD_URL), update.download_url);
		SendMessage(GetDlgItem(hDlg, IDC_PROGRESS), PBM_SETRANGE, 0, (MAX_PROGRESS<<16) & 0xFFFF0000);
		if (update.download_url == NULL)
			EnableWindow(GetDlgItem(hDlg, IDC_DOWNLOAD), FALSE);
		break;
	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam != GetDlgItem(hDlg, IDC_WEBSITE))
			return FALSE;
		// Change the font for the hyperlink
		SetBkMode((HDC)wParam, TRANSPARENT);
		CreateStaticFont((HDC)wParam, &hyperlink_font);
		SelectObject((HDC)wParam, hyperlink_font);
		SetTextColor((HDC)wParam, RGB(0,0,125));	// DARK_BLUE
		return (INT_PTR)CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCLOSE:
		case IDCANCEL:
			reset_localization(IDD_NEW_VERSION);
			safe_free(filepath);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		case IDC_WEBSITE:
			ShellExecuteA(hDlg, "open", RUFUS_URL, NULL, NULL, SW_SHOWNORMAL);
			break;
		case IDC_DOWNLOAD:	// Also doubles as abort and launch function
			switch(download_status) {
			case 1:		// Abort
				FormatStatus = ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_CANCELLED;
				break;
			case 2:		// Launch newer version and close this one
				memset(&si, 0, sizeof(si));
				memset(&pi, 0, sizeof(pi));
				si.cb = sizeof(si);
				if (!CreateProcessU(filepath, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
					PrintStatus(0, FALSE, MSG_214);
					// TODO: produce a message box and add a retry, as the file may be scanned by the Antivirus
					uprintf("Failed to launch new application: %s\n", WindowsErrorString());
				} else {
					PrintStatus(0, FALSE, MSG_213);
					PostMessage(hDlg, WM_COMMAND, (WPARAM)IDCLOSE, 0);
					PostMessage(hMainDialog, WM_CLOSE, 0, 0);
				}
				break;
			default:	// Download
				if (update.download_url == NULL) {
					uprintf("Could not get download URL\n");
					break;
				}
				for (i=(int)strlen(update.download_url); (i>0)&&(update.download_url[i]!='/'); i--);
				dl_ext.filename = &update.download_url[i+1];
				filepath = FileDialog(TRUE, app_dir, &dl_ext, OFN_NOCHANGEDIR);
				if (filepath == NULL) {
					uprintf("Could not get save path\n");
					break;
				}
				DownloadFileThreaded(update.download_url, filepath, hDlg);
				break;
			}
			return (INT_PTR)TRUE;
		}
		break;
	case UM_PROGRESS_INIT:
		FormatStatus = 0;
		download_status = 1;
		SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), lmprintf(MSG_038));
		return (INT_PTR)TRUE;
	case UM_PROGRESS_EXIT:
		if (wParam) {
			SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), lmprintf(MSG_039));
			download_status = 2;
		} else {
			SetWindowTextU(GetDlgItem(hDlg, IDC_DOWNLOAD), lmprintf(MSG_040));
			download_status = 0;
		}
		return (INT_PTR)TRUE;
	}
	return (INT_PTR)FALSE;
}

void DownloadNewVersion(void)
{
	DialogBoxW(hMainInstance, MAKEINTRESOURCEW(IDD_NEW_VERSION + IDD_IS_RTL), hMainDialog, NewVersionCallback);
}

void SetTitleBarIcon(HWND hDlg)
{
	HDC hDC;
	int i16, s16, s32;
	HICON hSmallIcon, hBigIcon;

	// High DPI scaling
	i16 = GetSystemMetrics(SM_CXSMICON);
	hDC = GetDC(hDlg);
	fScale = GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f;
	ReleaseDC(hDlg, hDC);
	// Adjust icon size lookup
	s16 = i16;
	s32 = (int)(32.0f*fScale);
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;
	if (s32 >= 54)
		s32 = 64;
	else if (s32 >= 40)
		s32 = 48;
	else if (s32 >= 28)
		s32 = 32;
	else if (s32 >= 20)
		s32 = 24;

	// Create the title bar icon
	hSmallIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, s16, s16, 0);
	SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hSmallIcon);
	hBigIcon = (HICON)LoadImage(hMainInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON, s32, s32, 0);
	SendMessage (hDlg, WM_SETICON, ICON_BIG, (LPARAM)hBigIcon);
}
