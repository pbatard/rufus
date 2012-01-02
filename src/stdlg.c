/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard Dialog Routines (Browse for folder, About, etc)
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"
#include "msapi_utf8.h"
#include "resource.h"
#include "license.h"

/* Windows versions */
enum WindowsVersion {
	WINDOWS_UNDEFINED,
	WINDOWS_UNSUPPORTED,
	WINDOWS_2K,
	WINDOWS_XP,
	WINDOWS_2003_XP64,
	WINDOWS_VISTA,
	WINDOWS_7,
	WINDOWS_8
};

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
enum WindowsVersion nWindowsVersion = WINDOWS_UNSUPPORTED;
static HWND hBrowseEdit;
static WNDPROC pOrgBrowseWndproc;
HFONT hBoldFont = NULL;

/*
 * Detect Windows version
 */
void DetectWindowsVersion(void)
{
	OSVERSIONINFO OSVersion;

	memset(&OSVersion, 0, sizeof(OSVERSIONINFO));
	OSVersion.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	nWindowsVersion = WINDOWS_UNSUPPORTED;
	if ((GetVersionEx(&OSVersion) != 0) && (OSVersion.dwPlatformId == VER_PLATFORM_WIN32_NT)) {
		if ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 0)) {
			nWindowsVersion = WINDOWS_2K;
		} else if ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 1)) {
			nWindowsVersion = WINDOWS_XP;
		} else if ((OSVersion.dwMajorVersion == 5) && (OSVersion.dwMinorVersion == 2)) {
			nWindowsVersion = WINDOWS_2003_XP64;
		} else if (OSVersion.dwMajorVersion == 6) {
			if (OSVersion.dwBuildNumber < 7000) {
				nWindowsVersion = WINDOWS_VISTA;
			} else {
				nWindowsVersion = WINDOWS_7;
			}
		} else if (OSVersion.dwMajorVersion >= 8) {
			nWindowsVersion = WINDOWS_8;
		}
	}
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
	safe_free(arr->Table);
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
			uprintf("CoCreateInstance for FileOpenDialog failed: error %X", hr);
			pfod = NULL;	// Just in case
			goto fallback;
		}
		hr = pfod->lpVtbl->SetOptions(pfod, FOS_PICKFOLDERS);
		if (FAILED(hr)) {
			uprintf("Failed to set folder option for FileOpenDialog: error %X", hr);
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
					uprintf("Could not convert path");
				} else {
					safe_strcpy(szFolderPath, MAX_PATH, tmp_path);
					safe_free(tmp_path);
				}
			} else {
				uprintf("Failed to set folder option for FileOpenDialog: error %X", hr);
			}
		} else if ((hr & 0xFFFF) != ERROR_CANCELLED) {
			// If it's not a user cancel, assume the dialog didn't show and fallback
			uprintf("could not show FileOpenDialog: error %X", hr);
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

void CreateBoldFont(HDC dc) {
	TEXTMETRIC tm;
	LOGFONT lf;

	if (hBoldFont != NULL)
		return;
	GetTextMetrics(dc, &tm);
	lf.lfHeight = tm.tmHeight+1;
	lf.lfWidth = tm.tmAveCharWidth+1;
	lf.lfEscapement = 0;
	lf.lfOrientation = 0;
	lf.lfWeight = FW_BOLD;
	lf.lfItalic = tm.tmItalic;
	lf.lfUnderline = FALSE;
	lf.lfStrikeOut = tm.tmStruckOut;
	lf.lfCharSet = tm.tmCharSet;
	lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
	lf.lfQuality = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = tm.tmPitchAndFamily;
	GetTextFace(dc, LF_FACESIZE, lf.lfFaceName);
	hBoldFont = CreateFontIndirect(&lf);
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
	switch (message) {
	case WM_INITDIALOG:
		CenterDialog(hDlg);
		SetDlgItemTextA(hDlg, IDC_ABOUT_COPYRIGHTS, additional_copyrights);
		break;
	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_RUFUS_BOLD)) {
			CreateBoldFont((HDC)wParam);
			SetBkMode((HDC)wParam, TRANSPARENT);
			SelectObject((HDC)wParam, hBoldFont);
			return (INT_PTR)CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
		}
		break;
	case WM_NOTIFY:
		switch (((LPNMHDR)lParam)->code) {
		case NM_CLICK:
		case NM_RETURN:
			switch (LOWORD(wParam)) {
			case IDC_ABOUT_RUFUS_URL:
				ShellExecuteA(hDlg, "open", RUFUS_URL, NULL, NULL, SW_SHOWNORMAL);
				break;
			case IDC_ABOUT_BUG_URL:
				ShellExecuteA(hDlg, "open", BUG_URL, NULL, NULL, SW_SHOWNORMAL);
				break;
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
			uprintf("could not set dialog icon");
		}
		// Set the dialog title
		if (szMessageTitle != NULL) {
			SetWindowTextA(hDlg, szMessageTitle);
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
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

/*
 * Display a custom notification
 */
BOOL Notification(int type, char* title, char* format, ...)
{
	va_list args;
	szMessageText = (char*)malloc(MAX_PATH);
	if (szMessageText == NULL) return FALSE;
	szMessageTitle = title;
	va_start(args, format);
	safe_vsnprintf(szMessageText, MAX_PATH-1, format, args);
	va_end(args);
	szMessageText[MAX_PATH-1] = 0;

	switch(type) {
	case MSG_WARNING:
		hMessageIcon = LoadIcon(NULL, IDI_WARNING);
		break;
	case MSG_ERROR:
		hMessageIcon = LoadIcon(NULL, IDI_ERROR);
		break;
	case MSG_INFO:
	default:
		hMessageIcon = LoadIcon(NULL, IDI_INFORMATION);
		break;
	}
	DialogBox(hMainInstance, MAKEINTRESOURCE(IDD_NOTIFICATION), hMainDialog, NotificationCallback);
	safe_free(szMessageText);
	return TRUE;
}

struct {
	HWND hTip;
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
HWND CreateTooltip(HWND hControl, const char* message, int duration)
{
	TOOLINFOW toolInfo = {0};
	int i;

	if ( (hControl == NULL) || (message == NULL) ) {
		return (HWND)NULL;
	}

	// Find an empty slot
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == NULL) break;
	}
	if (i == MAX_TOOLTIPS) {
		uprintf("Maximum number of tooltips reached\n");
		return (HWND)NULL;
	}

	// Create the tooltip window
	ttlist[i].hTip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hMainDialog, NULL,
		hMainInstance, NULL);

	if (ttlist[i].hTip == NULL) {
		return (HWND)NULL;
	}

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

	return ttlist[i].hTip;
}

void DestroyTooltip(HWND hWnd)
{
	int i;

	if (hWnd == NULL) return;
	for (i=0; i<MAX_TOOLTIPS; i++) {
		if (ttlist[i].hTip == hWnd) break;
	}
	if (i == MAX_TOOLTIPS) return;
	DestroyWindow(hWnd);
	safe_free(ttlist[i].wstring);
	ttlist[i].original_proc = NULL;
	ttlist[i].hTip = NULL;
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
