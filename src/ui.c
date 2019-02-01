/*
 * Rufus: The Reliable USB Formatting Utility
 * UI-related function calls
 * Copyright © 2018 Pete Batard <pete@akeo.ie>
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
#include <winioctl.h>
#include <assert.h>

#include "rufus.h"
#include "drive.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

#include "ui.h"
#include "ui_data.h"

UINT_PTR UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
HIMAGELIST hUpImageList, hDownImageList;
int advanced_device_section_height, advanced_format_section_height;
// (empty) check box width, (empty) drop down width, button height (for and without dropdown match)
int cbw, ddw, ddbh = 0, bh = 0;
// Row Height, DropDown Height, Main button width, half dropdown width, full dropdown width
static int rh, ddh, bw, hw, fw;
// See GetFullWidth() for details on how these values are used
static int sw, mw, bsw, sbw, ssw, tw, dbw;
static WNDPROC progress_original_proc = NULL;
static wchar_t wtbtext[2][128];

/*
 * The following is used to allocate slots within the progress bar
 * 0 means unused (no operation or no progress allocated to it)
 * +n means allocate exactly n bars (n percent of the progress bar)
 * -n means allocate a weighted slot of n from all remaining
 *    bars. E.g. if 80 slots remain and the sum of all negative entries
 *    is 10, -4 will allocate 4/10*80 = 32 bars (32%) for OP progress
 */
static int nb_slots[OP_MAX];
static float slot_end[OP_MAX+1];	// shifted +1 so that we can subtract 1 to OP indexes
static float previous_end;

// Set the combo selection according to the data
void SetComboEntry(HWND hDlg, int data)
{
	int i, nb_entries = ComboBox_GetCount(hDlg);

	if (nb_entries <= 0)
		return;
	for (i = 0; i < nb_entries; i++) {
		if (ComboBox_GetItemData(hDlg, i) == data) {
			IGNORE_RETVAL(ComboBox_SetCurSel(hDlg, i));
			return;
		}
	}
	if (i == nb_entries)
		IGNORE_RETVAL(ComboBox_SetCurSel(hDlg, 0));
}

// Move a control along the Y axis
static __inline void MoveCtrlY(HWND hDlg, int nID, int vertical_shift) {
	ResizeMoveCtrl(hDlg, GetDlgItem(hDlg, nID), 0, vertical_shift, 0, 0, 1.0f);
}

// https://stackoverflow.com/a/20926332/1069307
// https://msdn.microsoft.com/en-us/library/windows/desktop/bb226818.aspx
void GetBasicControlsWidth(HWND hDlg)
{
	int checkbox_internal_spacing = 12, dropdown_internal_spacing = 15;
	RECT rc = { 0, 0, 4, 8 };
	SIZE sz;

	// Compute base unit sizes since GetDialogBaseUnits() returns garbage data.
	// See http://support.microsoft.com/kb/125681
	MapDialogRect(hDlg, &rc);
	sz.cx = rc.right;
	sz.cy = rc.bottom;

	// TODO: figure out the specifics of each Windows version
	if (nWindowsVersion == WINDOWS_10) {
		checkbox_internal_spacing = 10;
		dropdown_internal_spacing = 13;
	}

	// Checkbox and (blank) dropdown widths
	cbw = MulDiv(checkbox_internal_spacing, sz.cx, 4);
	ddw = MulDiv(dropdown_internal_spacing, sz.cx, 4);

	// Spacing width between half-length dropdowns (sep) as well as left margin
	GetWindowRect(GetDlgItem(hDlg, IDC_TARGET_SYSTEM), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sw = rc.left;
	GetWindowRect(GetDlgItem(hDlg, IDC_PARTITION_TYPE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sw -= rc.right;
	mw = rc.left;

	// Small button width
	SendMessage(hSaveToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	sbw = sz.cx;

	// Small separator widths and button height
	GetWindowRect(GetDlgItem(hDlg, IDC_SAVE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	bh = rc.bottom - rc.top;
	ssw = rc.left;
	GetWindowRect(hDeviceList, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	ssw -= rc.right;

	// CSM tooltip separator width
	GetWindowRect(GetDlgItem(hDlg, IDS_CSM_HELP_TXT), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	tw = rc.left;
	GetWindowRect(hTargetSystem, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	tw -= rc.right;
}

// Compute the minimum size of the main buttons
void GetMainButtonsWidth(HWND hDlg)
{
	unsigned int i;
	RECT rc;

	GetWindowRect(GetDlgItem(hDlg, main_button_ids[0]), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	bw = rc.right - rc.left;

	for (i = 0; i < ARRAYSIZE(main_button_ids); i++)
		bw = max(bw, GetTextWidth(hDlg, main_button_ids[i]) + cbw);
	// The 'CLOSE' button is also be used to display 'CANCEL' => measure that too
	bw = max(bw, GetTextSize(GetDlgItem(hDlg, IDCANCEL), lmprintf(MSG_007)).cx + cbw);
}

// The following goes over the data that gets populated into the half-width dropdowns
// (Partition scheme, Target System, Disk ID, File system, Cluster size, Nb passes)
// to figure out the minimum width we should allocate.
void GetHalfDropwdownWidth(HWND hDlg)
{
	RECT rc;
	unsigned int i, j, msg_id;
	char tmp[256];

	// Initialize half width to the UI's default size
	GetWindowRect(GetDlgItem(hDlg, IDC_PARTITION_TYPE), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	hw = rc.right - rc.left - ddw;

	// "Super Floppy Disk" is the longuest entry in the Partition Scheme dropdown
	hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_PARTITION_TYPE), (char*)sfd_name).cx);

	// This is basically the same as SetClusterSizeLabels() except we're adding (Default) to each entry
	for (i = 512, j = 1, msg_id = MSG_026; j<MAX_CLUSTER_SIZES; i <<= 1, j++) {
		if (i > 8192) {
			i /= 1024;
			msg_id++;
		}
		safe_sprintf(tmp, 64, "%d %s", i, lmprintf(msg_id));
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_CLUSTER_SIZE), lmprintf(MSG_030, tmp)).cx);
	}
	// We don't go over file systems, because none of them will be longer than "Super Floppy Disk"
	// We do however go over the BIOS vs UEFI entries, as some of these are translated
	for (msg_id = MSG_031; msg_id <= MSG_033; msg_id++)
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_TARGET_SYSTEM), lmprintf(msg_id)).cx);

	// Just in case, we also do the number of passes
	for (i = 1; i <= 5; i++) {
		char* msg = (i == 1) ? lmprintf(MSG_034, 1) : lmprintf(MSG_035, (i == 2) ? 2 : 4, (i == 2) ? "" : lmprintf(MSG_087, flash_type[i - 3]));
		hw = max(hw, GetTextSize(GetDlgItem(hDlg, IDC_TARGET_SYSTEM), msg).cx);
	}

	// Finally, we must ensure that we'll have enough space for the 2 checkbox controls
	// that end up with a half dropdown
	hw = max(hw, GetTextWidth(hDlg, IDC_RUFUS_MBR) - sw);
	hw = max(hw, GetTextWidth(hDlg, IDC_BAD_BLOCKS) - sw);

	// Add the width of a blank dropdown
	hw += ddw;
}

/*
* dbw = dialog border width
* mw  = margin width
* fw  = full dropdown width
* hd  = half dropdown width
* bsw = boot selection dropdown width
* sw  = separator width
* ssw = small separator width
* bw  = button width
* sbw = small button width
*
*      |                        fw                            |
*      |          bsw          | ssw | sbw | ssw |     bw     |
*  8 ->|<-      96       ->|<-    24    ->|<-      96       ->|<- 8
*  mw  |        hw         |      sw      |        hw         |  mw
*                             |     bw     | ssw |     bw     |
*/
void GetFullWidth(HWND hDlg)
{
	RECT rc;
	int i;

	// Get the dialog border width
	GetWindowRect(hDlg, &rc);
	dbw = rc.right - rc.left;
	GetClientRect(hDlg, &rc);
	dbw -= rc.right - rc.left;

	// Compute the minimum size needed for the Boot Selection dropdown
	GetWindowRect(GetDlgItem(hDlg, IDC_BOOT_SELECTION), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);

	bsw = max(rc.right - rc.left, GetTextSize(hBootType, lmprintf(MSG_279)).cx + ddw);
	bsw = max(bsw, GetTextSize(hBootType, lmprintf(MSG_281, lmprintf(MSG_280))).cx + ddw);

	// Initialize full width to the UI's default size
	GetWindowRect(GetDlgItem(hDlg, IDS_DEVICE_TXT), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	fw = rc.right - rc.left - ddw;

	// Go through the Image Options for Windows To Go
	fw = max(fw, GetTextSize(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_117)).cx);
	fw = max(fw, GetTextSize(GetDlgItem(hDlg, IDC_IMAGE_OPTION), lmprintf(MSG_118)).cx);

	// Now deal with full length checkbox lines
	for (i = 0; i<ARRAYSIZE(full_width_checkboxes); i++)
		fw = max(fw, GetTextWidth(hDlg, full_width_checkboxes[i]));

	// All of the above is for text only, so we need to add dd space
	fw += ddw;

	// Our min also needs to be longer than 2 half length dropdowns + spacer
	fw = max(fw, 2 * hw + sw);

	// Now that we have our minimum full width, adjust the button width if needed
	// Adjust according to min full width
	bw = max(bw, (fw - 2 * ssw - sw) / 4);
	// Adjust according to min boot selection width
	bw = max(bw, (bsw + sbw - sw) / 3);

	// Adjust according to min half width
	bw = max(bw, (hw / 2) - ssw);

	// Now that our button width is set, we can adjust the rest
	hw = max(hw, 2 * bw + ssw);
	fw = max(fw, 2 * hw + sw);

	bsw = max(bsw, fw - bw - 2 * ssw - sbw);

	// TODO: Also pick a few choice messages from info/status
}

void PositionMainControls(HWND hDlg)
{
	RECT rc;
	HWND hCtrl, hPrevCtrl;
	SIZE sz;
	DWORD padding;
	int i, x, button_fudge = 2;

	// Start by resizing the whole dialog
	GetWindowRect(hDlg, &rc);
	// Don't forget to add the dialog border width, since we resize the whole dialog
	SetWindowPos(hDlg, NULL, -1, -1, fw + 2 * mw + dbw, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

	// Resize the height of the label, persistence size and progress bar to the height of standard dropdowns
	hCtrl = GetDlgItem(hDlg, IDC_DEVICE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	ddh = rc.bottom - rc.top;
	ddbh = ddh + button_fudge;
	bh = max(bh, ddbh);
	hCtrl = GetDlgItem(hDlg, IDC_LABEL);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, hAdvancedFormatToolbar, rc.left, rc.top, rc.right - rc.left, ddh, SWP_NOZORDER);
	hCtrl = GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, GetDlgItem(hDlg, IDC_PERSISTENCE_SLIDER), rc.left, rc.top, rc.right - rc.left, ddh, SWP_NOZORDER);
	GetWindowRect(hProgress, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hProgress, hNBPasses, rc.left, rc.top, rc.right - rc.left, ddh, SWP_NOZORDER);

	// Get the height of a typical row
	hCtrl = GetDlgItem(hDlg, IDS_BOOT_SELECTION_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	rh = rc.top;
	hCtrl = GetDlgItem(hDlg, IDS_DEVICE_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	rh -= rc.top;

	// Get the height of the advanced options
	hCtrl = GetDlgItem(hDlg, IDC_LIST_USB_HDD);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_device_section_height = rc.top;
	hCtrl = GetDlgItem(hDlg, IDC_RUFUS_MBR);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_device_section_height = rc.bottom - advanced_device_section_height;

	hCtrl = GetDlgItem(hDlg, IDC_QUICK_FORMAT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_format_section_height = rc.top;
	hCtrl = GetDlgItem(hDlg, IDC_BAD_BLOCKS);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	advanced_format_section_height = rc.bottom - advanced_format_section_height;

	// Get the vertical position of the sections text
	hCtrl = GetDlgItem(hDlg, IDS_DRIVE_PROPERTIES_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	section_vpos[0] = rc.top + 2 * sz.cy / 3;
	hCtrl = GetDlgItem(hDlg, IDS_FORMAT_OPTIONS_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	section_vpos[1] = rc.top + 2 * sz.cy / 3;
	hCtrl = GetDlgItem(hDlg, IDS_STATUS_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz = GetTextSize(hCtrl, NULL);
	section_vpos[2] = rc.top + 2 * sz.cy / 3;

	// Seriously, who designed this bullshit API call where you pass a SIZE
	// struct but can only retrieve one of cx or cy at a time?!?
	SendMessage(hMultiToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	GetWindowRect(GetDlgItem(hDlg, IDC_ABOUT), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hMultiToolbar, hProgress, rc.left, rc.top, sz.cx, ddbh, 0);

	// Reposition the main buttons
	for (i = 0; i < ARRAYSIZE(main_button_ids); i++) {
		hCtrl = GetDlgItem(hDlg, main_button_ids[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		x = mw + fw - bw;
		if (i % 2 == 1)
			x -= bw + ssw;
		hPrevCtrl = GetNextWindow(hCtrl, GW_HWNDPREV);
		SetWindowPos(hCtrl, hPrevCtrl, x, rc.top, bw, ddbh, 0);
	}

	// Reposition the Save button
	hCtrl = GetDlgItem(hDlg, IDC_SAVE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hSaveToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SendMessage(hSaveToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(sz.cx, ddbh));
	// Microsoft, how I loathe thee!!!
	padding = (DWORD)SendMessage(hSaveToolbar, TB_GETPADDING, 0, 0);
	sz.cx = padding & 0xFFFF;
	sz.cy = padding >> 16;
	SendMessage(hSaveToolbar, TB_SETPADDING, 0, MAKELPARAM(sz.cx + 3, sz.cy + 2));
	SetWindowPos(hSaveToolbar, hDeviceList, mw + fw - sbw, rc.top, sbw, ddbh, 0);

	// Reposition the Hash button
	hCtrl = GetDlgItem(hDlg, IDC_HASH);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hHashToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SendMessage(hHashToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(sz.cx, ddbh));
	padding = (DWORD)SendMessage(hHashToolbar, TB_GETPADDING, 0, 0);
	sz.cx = padding & 0xFFFF;
	sz.cy = padding >> 16;
	SendMessage(hHashToolbar, TB_SETPADDING, 0, MAKELPARAM(sz.cx + 3, sz.cy + 2));
	SetWindowPos(hHashToolbar, hBootType, mw + bsw + ssw, rc.top, sbw, ddbh, 0);

	// Reposition the Persistence slider and resize it to the boot selection width
	hCtrl = GetDlgItem(hDlg, IDC_PERSISTENCE_SLIDER);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, GetDlgItem(hDlg, IDC_IMAGE_OPTION), mw, rc.top, bsw, rc.bottom - rc.top, 0);

	// Reposition the Persistence Units dropdown (no need to resize)
	hCtrl = GetDlgItem(hDlg, IDC_PERSISTENCE_UNITS);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	sz.cx = fw - (rc.right - rc.left);
	SetWindowPos(hCtrl, GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE), mw + sz.cx, rc.top, rc.right - rc.left, rc.bottom - rc.top, 0);
	ShowWindow(hCtrl, SW_HIDE);

	// Reposition and resize the Persistence Size edit
	hCtrl = GetDlgItem(hDlg, IDC_PERSISTENCE_SIZE);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, GetDlgItem(hDlg, IDC_PERSISTENCE_SLIDER), mw + bsw + ssw, rc.top, fw - bsw - ssw, rc.bottom - rc.top, 0);
	EnableWindow(hCtrl, FALSE);

	// Reposition the CSM help tip
	hCtrl = GetDlgItem(hDlg, IDS_CSM_HELP_TXT);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SetWindowPos(hCtrl, hTargetSystem, mw + fw + tw, rc.top, sbw, rc.bottom - rc.top, 0);

	if (advanced_mode_device) {
		// Still need to adjust the width of the device selection dropdown
		GetWindowRect(hDeviceList, &rc);
		MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
		SetWindowPos(hDeviceList, HWND_TOP, rc.left, rc.top, fw - ssw - sbw, rc.bottom - rc.top, 0);
	}

	// Resize the full width controls
	for (i = 0; i < ARRAYSIZE(full_width_controls); i++) {
		hCtrl = GetDlgItem(hDlg, full_width_controls[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		hPrevCtrl = GetNextWindow(hCtrl, GW_HWNDPREV);
		SetWindowPos(hCtrl, hPrevCtrl, rc.left, rc.top, fw, rc.bottom - rc.top, 0);
	}

	// Resize the half drowpdowns
	for (i = 0; i < ARRAYSIZE(half_width_ids); i++) {
		hCtrl = GetDlgItem(hDlg, half_width_ids[i]);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		// First 5 controls are on the left handside
		// First 2 controls may overflow into separator
		hPrevCtrl = GetNextWindow(hCtrl, GW_HWNDPREV);
		SetWindowPos(hCtrl, hPrevCtrl, (i < 5) ? rc.left : mw + hw + sw, rc.top,
			(i <2) ? hw + sw : hw, rc.bottom - rc.top, 0);
	}

	// Resize the boot selection dropdown
	hCtrl = GetDlgItem(hDlg, IDC_BOOT_SELECTION);
	GetWindowRect(hCtrl, &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	hPrevCtrl = GetNextWindow(hCtrl, GW_HWNDPREV);
	SetWindowPos(hCtrl, hPrevCtrl, rc.left, rc.top, bsw, rc.bottom - rc.top, 0);
}

static void ResizeDialogs(int shift)
{
	RECT rc;
	POINT point;

	// Resize the main dialog
	GetWindowRect(hMainDialog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top);
	MoveWindow(hMainDialog, rc.left, rc.top, point.x, point.y + shift, TRUE);

	// Resize the log
	GetWindowRect(hLogDialog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top);
	MoveWindow(hLogDialog, rc.left, rc.top, point.x, point.y + shift, TRUE);
	MoveCtrlY(hLogDialog, IDC_LOG_CLEAR, shift);
	MoveCtrlY(hLogDialog, IDC_LOG_SAVE, shift);
	MoveCtrlY(hLogDialog, IDCANCEL, shift);
	GetWindowRect(hLog, &rc);
	point.x = (rc.right - rc.left);
	point.y = (rc.bottom - rc.top) + shift;
	SetWindowPos(hLog, NULL, 0, 0, point.x, point.y, SWP_NOZORDER);
	// Don't forget to scroll the edit to the bottom after resize
	Edit_Scroll(hLog, 0, Edit_GetLineCount(hLog));
}

// Thanks to Microsoft atrocious DPI handling, we must adjust for low DPI
void AdjustForLowDPI(HWND hDlg)
{
	static int ddy = 4;
	int i, j;
	RECT rc;
	HWND hCtrl, hPrevCtrl;
	int dy = 0;

	if (fScale >= 1.3f)
		return;

	for (i = 0; i < ARRAYSIZE(adjust_dpi_ids); i++) {
		dy += ddy;
		// "...and the other thing I really like about Microsoft's UI handling is how "
		//."you never have to introduce weird hardcoded constants all over the place, "
		// "just to make your UI look good...", said NO ONE ever.
		if (adjust_dpi_ids[i][0] == IDC_QUICK_FORMAT)
			dy += 1;
		for (j = 0; j < 5; j++) {
			if (adjust_dpi_ids[i][j] == 0)
				break;
			hCtrl = GetDlgItem(hDlg, adjust_dpi_ids[i][j]);
			GetWindowRect(hCtrl, &rc);
			MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
			hPrevCtrl = GetNextWindow(hCtrl, GW_HWNDPREV);
			SetWindowPos(hCtrl, hPrevCtrl, rc.left, rc.top + dy,
				rc.right - rc.left, rc.bottom - rc.top, 0);
		}
	}

	section_vpos[1] += 9 * ddy;
	section_vpos[2] += 16 * ddy + 1;
	advanced_device_section_height += 3 * ddy;
	advanced_format_section_height += 3 * ddy + 1;

	ResizeDialogs(dy + 2 * ddy);
	InvalidateRect(hDlg, NULL, TRUE);
}

void SetSectionHeaders(HWND hDlg)
{
	RECT rc;
	HWND hCtrl;
	SIZE sz;
	HFONT hf;
	wchar_t wtmp[128];
	size_t wlen;
	int i;

	// Set the section header fonts and resize the static controls accordingly
	hf = CreateFontA(-MulDiv(14, GetDeviceCaps(GetDC(hMainDialog), LOGPIXELSY), 72), 0, 0, 0,
		FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, PROOF_QUALITY, 0, "Segoe UI");

	for (i = 0; i < ARRAYSIZE(section_control_ids); i++) {
		SendDlgItemMessageA(hDlg, section_control_ids[i], WM_SETFONT, (WPARAM)hf, TRUE);
		hCtrl = GetDlgItem(hDlg, section_control_ids[i]);
		memset(wtmp, 0, sizeof(wtmp));
		GetWindowTextW(hCtrl, wtmp, ARRAYSIZE(wtmp));
		wlen = wcslen(wtmp);
		wtmp[wlen++] = L' ';
		wtmp[wlen++] = L' ';
		SetWindowTextW(hCtrl, wtmp);
		GetWindowRect(hCtrl, &rc);
		MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
		sz = GetTextSize(hCtrl, NULL);
		SetWindowPos(hCtrl, NULL, rc.left, rc.top, sz.cx, sz.cy, SWP_NOZORDER);
	}
}

// Toggle "advanced" options
void ToggleAdvancedDeviceOptions(BOOL enable)
{
	RECT rc;
	SIZE sz;
	TBBUTTONINFO button_info;
	int i, shift = advanced_device_section_height;

	if (!enable)
		shift = -shift;
	section_vpos[1] += shift;
	section_vpos[2] += shift;

	// Toggle the Hide/Show toolbar text
	utf8_to_wchar_no_alloc(lmprintf((enable) ? MSG_122 : MSG_121, lmprintf(MSG_119)), wtbtext[0], ARRAYSIZE(wtbtext[0]));
	button_info.cbSize = sizeof(button_info);
	button_info.dwMask = TBIF_TEXT;
	button_info.pszText = wtbtext[0];
	SendMessage(hAdvancedDeviceToolbar, TB_SETBUTTONINFO, (WPARAM)IDC_ADVANCED_DRIVE_PROPERTIES, (LPARAM)&button_info);
	SendMessage(hAdvancedDeviceToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)((enable) ? hUpImageList : hDownImageList));
	GetWindowRect(hAdvancedDeviceToolbar, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SendMessage(hAdvancedDeviceToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedDeviceToolbar, hTargetSystem, rc.left, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Move the controls up or down
	for (i = 0; i<ARRAYSIZE(advanced_device_move_ids); i++)
		MoveCtrlY(hMainDialog, advanced_device_move_ids[i], shift);

	// Hide or show the various advanced options
	for (i = 0; i<ARRAYSIZE(advanced_device_toggle_ids); i++)
		ShowWindow(GetDlgItem(hMainDialog, advanced_device_toggle_ids[i]), enable ? SW_SHOW : SW_HIDE);

	GetWindowRect(hDeviceList, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SetWindowPos(hDeviceList, HWND_TOP, rc.left, rc.top, enable ? fw - ssw - sbw : fw, rc.bottom - rc.top, 0);

	// Resize the main dialog and log window
	ResizeDialogs(shift);

	// Never hurts to force Windows' hand
	InvalidateRect(hMainDialog, NULL, TRUE);
}

void ToggleAdvancedFormatOptions(BOOL enable)
{
	RECT rc;
	SIZE sz;
	TBBUTTONINFO button_info;
	int i, shift = advanced_format_section_height;

	if (!enable)
		shift = -shift;
	section_vpos[2] += shift;

	// Toggle the Hide/Show toolbar text
	utf8_to_wchar_no_alloc(lmprintf((enable) ? MSG_122 : MSG_121, lmprintf(MSG_120)), wtbtext[1], ARRAYSIZE(wtbtext[0]));
	button_info.cbSize = sizeof(button_info);
	button_info.dwMask = TBIF_TEXT;
	button_info.pszText = wtbtext[1];
	SendMessage(hAdvancedFormatToolbar, TB_SETBUTTONINFO, (WPARAM)IDC_ADVANCED_FORMAT_OPTIONS, (LPARAM)&button_info);
	SendMessage(hAdvancedFormatToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)((enable) ? hUpImageList : hDownImageList));
	GetWindowRect(hAdvancedFormatToolbar, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SendMessage(hAdvancedFormatToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	SetWindowPos(hAdvancedFormatToolbar, hClusterSize, rc.left, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Move the controls up or down
	for (i = 0; i<ARRAYSIZE(advanced_format_move_ids); i++)
		MoveCtrlY(hMainDialog, advanced_format_move_ids[i], shift);

	// Hide or show the various advanced options
	for (i = 0; i<ARRAYSIZE(advanced_format_toggle_ids); i++)
		ShowWindow(GetDlgItem(hMainDialog, advanced_format_toggle_ids[i]), enable ? SW_SHOW : SW_HIDE);

	// Resize the main dialog and log window
	ResizeDialogs(shift);

	// Never hurts to force Windows' hand
	InvalidateRect(hMainDialog, NULL, TRUE);
}

// Toggle the display of peristence unit dropdown and resize the size field
void TogglePersistenceControls(BOOL display)
{
	RECT rc;
	HWND hSize, hUnits;
	LONG_PTR style;
	LONG width = fw - bsw - ssw;
	hSize = GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE);
	hUnits = GetDlgItem(hMainDialog, IDC_PERSISTENCE_UNITS);

	style = GetWindowLongPtr(hSize, GWL_EXSTYLE);
	if (display)
		style |= WS_EX_RIGHT;
	else
		style &= ~WS_EX_RIGHT;
	SetWindowLongPtr(hSize, GWL_EXSTYLE, style);

	if (display) {
		GetWindowRect(hUnits, &rc);
		MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
		width -= (rc.right - rc.left) + ssw;
	}

	GetWindowRect(hSize, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	SetWindowPos(hSize, GetDlgItem(hMainDialog, IDC_PERSISTENCE_SLIDER), mw + bsw + ssw, rc.top, width, rc.bottom - rc.top, 0);

	EnableWindow(hSize, display ? TRUE : FALSE);
	EnableWindow(hUnits, display ? TRUE : FALSE);
	ShowWindow(hUnits, display ? SW_SHOW : SW_HIDE);
}

void SetPeristencePos(uint64_t pos)
{
	char tmp[64];

	if (pos != 0) {
		TogglePersistenceControls(TRUE);
		static_sprintf(tmp, "%ld", (LONG)pos);
	} else {
		TogglePersistenceControls(FALSE);
		static_sprintf(tmp, "0 (%s)", lmprintf(MSG_124));
	}
	app_changed_size = TRUE;
	SetWindowTextU(GetDlgItem(hMainDialog, IDC_PERSISTENCE_SIZE), tmp);
}

void SetPersistenceSize(void)
{
	int i, proposed_unit_selection = 0;
	LONGLONG base_unit = MB;
	HWND hCtrl;
	uint64_t max = 0, pos = 0;

	if (ComboBox_GetCurSel(hDeviceList) >= 0) {
		max = SelectedDrive.DiskSize - img_report.projected_size;
		persistence_size = min(persistence_size, max);
		pos = persistence_size;

		// Reset the the Persistence Units dropdown
		hCtrl = GetDlgItem(hMainDialog, IDC_PERSISTENCE_UNITS);
		IGNORE_RETVAL(ComboBox_ResetContent(hCtrl));
		for (i = 0; i < 3; i++) {
			IGNORE_RETVAL(ComboBox_SetItemData(hCtrl, ComboBox_AddStringU(hCtrl, lmprintf(MSG_022 + i)), i));
			// If we have more than 7 discrete positions, set this unit as our base
			if (SelectedDrive.DiskSize > 7 * base_unit)
				proposed_unit_selection = i;
			base_unit *= 1024;
			// Don't allow a base unit unless the drive is at least twice the size of that unit
			if (SelectedDrive.DiskSize < 2 * base_unit)
				break;
		}
		if (persistence_unit_selection < 0)
			persistence_unit_selection = proposed_unit_selection;

		IGNORE_RETVAL(ComboBox_SetCurSel(hCtrl, persistence_unit_selection));
		pos /= MB;
		max /= MB;
		for (i = 0; i < persistence_unit_selection; i++) {
			pos /= 1024;
			max /= 1024;
		}
	}

	hCtrl = GetDlgItem(hMainDialog, IDC_PERSISTENCE_SLIDER);
	// Wow! Unless you set *all* these redraw WPARAMs to true, the one from
	// TBM_SETPOS gets completely ignored if the value is zero!
	SendMessage(hCtrl, TBM_SETRANGEMIN, (WPARAM)TRUE, (LPARAM)0);
	SendMessage(hCtrl, TBM_SETRANGEMAX, (WPARAM)TRUE, (LPARAM)max);
	SendMessage(hCtrl, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)pos);

	SetPeristencePos(pos);
}

// Toggle the Image Option dropdown (Windows To Go or persistence settings)
void ToggleImageOptions(void)
{
	BOOL has_wintogo, has_persistence;
	uint8_t entry_image_options = image_options;
	int i, shift = rh;

	has_wintogo = ((bt == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso) && (nWindowsVersion >= WINDOWS_8) && (HAS_WINTOGO(img_report)));
	has_persistence = ((bt == BT_IMAGE) && (image_path != NULL) && (img_report.is_iso) && (HAS_PERSISTENCE(img_report)));

	assert(popcnt8(image_options) <= 1);

	// Keep a copy of the "Image Option" text (so that we don't have to duplicate its transation in the .loc)
	if (image_option_txt[0] == 0)
		GetWindowTextU(GetDlgItem(hMainDialog, IDS_IMAGE_OPTION_TXT), image_option_txt, sizeof(image_option_txt));

	if ( ((has_wintogo) && !(image_options & IMOP_WINTOGO)) ||
		 ((!has_wintogo) && (image_options & IMOP_WINTOGO)) ) {
		image_options ^= IMOP_WINTOGO;
		// Set the Windows To Go selection in the dropdown
		IGNORE_RETVAL(ComboBox_SetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION), windows_to_go_selection));
	}

	if (((has_persistence) && !(image_options & IMOP_PERSISTENCE)) ||
		((!has_persistence) && (image_options & IMOP_PERSISTENCE))) {
		image_options ^= IMOP_PERSISTENCE;
	}

	if ( ((entry_image_options != 0) && (has_wintogo || has_persistence)) ||
		 ((entry_image_options == 0) && !(has_wintogo || has_persistence)) )
		shift = 0;

	if (shift != 0) {
		if (entry_image_options != 0)
			shift = -shift;
		section_vpos[1] += shift;
		section_vpos[2] += shift;

		for (i = 0; i < ARRAYSIZE(image_option_move_ids); i++)
			MoveCtrlY(hMainDialog, image_option_move_ids[i], shift);

		// Resize the main dialog and log window
		ResizeDialogs(shift);
	}

	// Hide or show the boot options
	for (i = 0; i < ARRAYSIZE(image_option_toggle_ids); i++) {
		ShowWindow(GetDlgItem(hMainDialog, image_option_toggle_ids[i][0]),
			(image_options & image_option_toggle_ids[i][1]) ? SW_SHOW : SW_HIDE);
	}
	// Set the dropdown default selection
	if (image_options & IMOP_WINTOGO) {
		SetWindowTextU(GetDlgItem(hMainDialog, IDS_IMAGE_OPTION_TXT), image_option_txt);
		IGNORE_RETVAL(ComboBox_SetCurSel(GetDlgItem(hMainDialog, IDC_IMAGE_OPTION), windows_to_go_selection));
	} else if (image_options & IMOP_PERSISTENCE) {
		SetWindowTextU(GetDlgItem(hMainDialog, IDS_IMAGE_OPTION_TXT), lmprintf(MSG_123));
		TogglePersistenceControls(persistence_size != 0);
		SetPersistenceSize();
	}
	// If you don't force a redraw here, all kind of bad UI artifacts happen...
	InvalidateRect(hMainDialog, NULL, TRUE);
}

// We need to create the small toolbar buttons first so that we can compute their width
void CreateSmallButtons(HWND hDlg)
{
	HIMAGELIST hImageList;
	HICON hIconSave, hIconHash;
	int icon_offset = 0, i16 = GetSystemMetrics(SM_CXSMICON);
	TBBUTTON tbToolbarButtons[1];
	unsigned char* buffer;
	DWORD bufsize;

	if (i16 >= 28)
		icon_offset = 20;
	else if (i16 >= 20)
		icon_offset = 10;

	hSaveToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, TOOLBAR_STYLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_SAVE_TOOLBAR, hMainInstance, NULL);
	hImageList = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_HIGHQUALITYSCALE | ILC_MIRROR, 1, 0);
	buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDI_SAVE_16 + icon_offset), _RT_RCDATA, "save icon", &bufsize, FALSE);
	hIconSave = CreateIconFromResourceEx(buffer, bufsize, TRUE, 0x30000, 0, 0, 0);
	ImageList_AddIcon(hImageList, hIconSave);
	DestroyIcon(hIconSave);
	SendMessage(hSaveToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hImageList);
	SendMessage(hSaveToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_SAVE;
	tbToolbarButtons[0].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hSaveToolbar, TB_ADDBUTTONS, (WPARAM)1, (LPARAM)&tbToolbarButtons);

	hHashToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, TOOLBAR_STYLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_HASH_TOOLBAR, hMainInstance, NULL);
	hImageList = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_HIGHQUALITYSCALE | ILC_MIRROR, 1, 0);
	buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(IDI_HASH_16 + icon_offset), _RT_RCDATA, "hash icon", &bufsize, FALSE);
	hIconHash = CreateIconFromResourceEx(buffer, bufsize, TRUE, 0x30000, 0, 0, 0);
	ImageList_AddIcon(hImageList, hIconHash);
	DestroyIcon(hIconHash);
	SendMessage(hHashToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hImageList);
	SendMessage(hHashToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_HASH;
	tbToolbarButtons[0].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hHashToolbar, TB_ADDBUTTONS, (WPARAM)1, (LPARAM)&tbToolbarButtons);
}

static INT_PTR CALLBACK ProgressCallback(HWND hCtrl, UINT message, WPARAM wParam, LPARAM lParam)
{
	HDC hDC;
	RECT rc, rc2;
	PAINTSTRUCT ps;
	SIZE size;
	LONG full_right;
	wchar_t winfo[128];
	static BOOL marquee_mode = FALSE;
	static uint32_t pos = 0, min = 0, max = 0xFFFF;
	static COLORREF color = PROGRESS_BAR_NORMAL_COLOR;

	switch (message) {

	case PBM_SETSTATE:
		switch (wParam) {
		case PBST_NORMAL:
			color = PROGRESS_BAR_NORMAL_COLOR;
			break;
		case PBST_PAUSED:
			color = PROGRESS_BAR_PAUSED_COLOR;
			break;
		case PBST_ERROR:
			color = PROGRESS_BAR_ERROR_COLOR;
			break;
		}
		return (INT_PTR)TRUE;

	case PBM_SETRANGE:
		// Don't bother sanity checking min and max: If *you* want to
		// be an ass about the progress bar range, it's *your* problem.
		min = (uint32_t)(lParam & 0xFFFF);
		max = (uint32_t)(lParam >> 16);
		return (INT_PTR)TRUE;

	case PBM_SETPOS:
		pos = (WORD)wParam;
		InvalidateRect(hProgress, NULL, TRUE);
		return (INT_PTR)TRUE;

	case PBM_SETMARQUEE:
		if ((wParam == TRUE) && (!marquee_mode)) {
			marquee_mode = TRUE;
			pos = min;
			color = PROGRESS_BAR_NORMAL_COLOR;
			SetTimer(hCtrl, TID_MARQUEE_TIMER, MARQUEE_TIMER_REFRESH, NULL);
			InvalidateRect(hProgress, NULL, TRUE);
		} else if ((wParam == FALSE) && (marquee_mode)) {
			marquee_mode = FALSE;
			KillTimer(hCtrl, TID_MARQUEE_TIMER);
			pos = min;
			InvalidateRect(hProgress, NULL, TRUE);
		}
		return (INT_PTR)TRUE;

	case WM_TIMER:
		if ((wParam == TID_MARQUEE_TIMER) && marquee_mode) {
			pos += max((max - min) / (1000 / MARQUEE_TIMER_REFRESH), 1);
			if ((pos > max) || (pos < min))
				pos = min;
			InvalidateRect(hProgress, NULL, TRUE);
			return (INT_PTR)TRUE;
		}
		return (INT_PTR)FALSE;

	case WM_PAINT:
		hDC = BeginPaint(hCtrl, &ps);
		GetClientRect(hCtrl, &rc);
		rc2 = rc;
		InflateRect(&rc, -1, -1);
		SelectObject(hDC, GetStockObject(DC_PEN));
		SelectObject(hDC, GetStockObject(NULL_BRUSH));
		// TODO: Handle SetText message so we can avoid this call
		GetWindowTextW(hProgress, winfo, ARRAYSIZE(winfo));
		SelectObject(hDC, hInfoFont);
		GetTextExtentPoint32(hDC, winfo, (int)wcslen(winfo), &size);
		if (size.cx > rc.right)
			size.cx = rc.right;
		if (size.cy > rc.bottom)
			size.cy = rc.bottom;
		full_right = rc.right;
		if (marquee_mode) {
			// Optional first segment
			if (pos + ((max - min) / 5) > max) {
				rc.right = MulDiv(pos + ((max - min) / 5) - max, rc.right, max - min);
				SetTextColor(hDC, PROGRESS_BAR_INVERTED_TEXT_COLOR);
				SetBkColor(hDC, color);
				ExtTextOut(hDC, (full_right - size.cx) / 2, (rc.bottom - size.cy) / 2,
					ETO_CLIPPED | ETO_OPAQUE | ETO_NUMERICSLOCAL, &rc, winfo, (int)wcslen(winfo), NULL);
				rc.left = rc.right;
				rc.right = full_right;
			}
			// Optional second segment
			if (pos > min) {
				rc.right = MulDiv(pos - min, rc.right, max - min);
				SetTextColor(hDC, PROGRESS_BAR_NORMAL_TEXT_COLOR);
				SetBkColor(hDC, PROGRESS_BAR_BACKGROUND_COLOR);
				ExtTextOut(hDC, (full_right - size.cx) / 2, (rc.bottom - size.cy) / 2,
					ETO_CLIPPED | ETO_OPAQUE | ETO_NUMERICSLOCAL, &rc, winfo, (int)wcslen(winfo), NULL);
				rc.left = rc.right;
				rc.right = full_right;
			}
			// Second to last segment
			rc.right = MulDiv(pos - min + ((max - min) / 5), rc.right, max - min);
			SetTextColor(hDC, PROGRESS_BAR_INVERTED_TEXT_COLOR);
			SetBkColor(hDC, color);
			ExtTextOut(hDC, (full_right - size.cx) / 2, (rc.bottom - size.cy) / 2,
				ETO_CLIPPED | ETO_OPAQUE | ETO_NUMERICSLOCAL, &rc, winfo, (int)wcslen(winfo), NULL);
		} else {
			// First segment
			rc.right = (pos > min) ? MulDiv(pos - min, rc.right, max - min) : rc.left;
			SetTextColor(hDC, PROGRESS_BAR_INVERTED_TEXT_COLOR);
			SetBkColor(hDC, color);
			ExtTextOut(hDC, (full_right - size.cx) / 2, (rc.bottom - size.cy) / 2,
				ETO_CLIPPED | ETO_OPAQUE | ETO_NUMERICSLOCAL, &rc, winfo, (int)wcslen(winfo), NULL);
		}
		// Last segment
		rc.left = rc.right;
		rc.right = full_right;
		SetTextColor(hDC, PROGRESS_BAR_NORMAL_TEXT_COLOR);
		SetBkColor(hDC, PROGRESS_BAR_BACKGROUND_COLOR);
		ExtTextOut(hDC, (full_right - size.cx) / 2, (rc.bottom - size.cy) / 2,
			ETO_CLIPPED | ETO_OPAQUE | ETO_NUMERICSLOCAL, &rc, winfo, (int)wcslen(winfo), NULL);
		// Bounding rectangle
		SetDCPenColor(hDC, PROGRESS_BAR_BOX_COLOR);
		Rectangle(hDC, rc2.left, rc2.top, rc2.right, rc2.bottom);
		EndPaint(hCtrl, &ps);
		return (INT_PTR)TRUE;
	}

	return CallWindowProc(progress_original_proc, hCtrl, message, wParam, lParam);
}

void CreateAdditionalControls(HWND hDlg)
{
	HINSTANCE hDll;
	HIMAGELIST hToolbarImageList;
	HICON hIcon, hIconUp, hIconDown;
	RECT rc;
	SIZE sz;
	int icon_offset = 0, i, i16, s16, toolbar_dx = -4 - ((fScale > 1.49f) ? 1 : 0) - ((fScale > 1.99f) ? 1 : 0);
	TBBUTTON tbToolbarButtons[7];
	unsigned char* buffer;
	DWORD bufsize;

	s16 = i16 = GetSystemMetrics(SM_CXSMICON);
	if (s16 >= 54)
		s16 = 64;
	else if (s16 >= 40)
		s16 = 48;
	else if (s16 >= 28)
		s16 = 32;
	else if (s16 >= 20)
		s16 = 24;
	if (i16 >= 28)
		icon_offset = 20;
	else if (i16 >= 20)
		icon_offset = 10;

	// Fetch the up and down expand icons for the advanced options toolbar
	hDll = GetLibraryHandle("ComDlg32");
	hIconDown = (HICON)LoadImage(hDll, MAKEINTRESOURCE(577), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hIconUp = (HICON)LoadImage(hDll, MAKEINTRESOURCE(578), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	// Fallback to using Shell32 if we can't locate the icons we want in ComDlg32
	hDll = GetLibraryHandle("Shell32");
	if (hIconUp == NULL)
		hIconUp = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16749), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	if (hIconDown == NULL)
		hIconDown = (HICON)LoadImage(hDll, MAKEINTRESOURCE(16750), IMAGE_ICON, s16, s16, LR_DEFAULTCOLOR | LR_SHARED);
	hUpImageList = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_HIGHQUALITYSCALE, 1, 0);
	hDownImageList = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_HIGHQUALITYSCALE, 1, 0);
	ImageList_AddIcon(hUpImageList, hIconUp);
	ImageList_AddIcon(hDownImageList, hIconDown);

	// Create the advanced options toolbars
	memset(wtbtext, 0, sizeof(wtbtext));
	utf8_to_wchar_no_alloc(lmprintf((advanced_mode_device) ? MSG_122 : MSG_121, lmprintf(MSG_119)), wtbtext[0], ARRAYSIZE(wtbtext[0]));
	hAdvancedDeviceToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, TOOLBAR_STYLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_ADVANCED_DEVICE_TOOLBAR, hMainInstance, NULL);
	SendMessage(hAdvancedDeviceToolbar, CCM_SETVERSION, (WPARAM)6, 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_ADVANCED_DRIVE_PROPERTIES;
	tbToolbarButtons[0].fsStyle = BTNS_SHOWTEXT | BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iString = (INT_PTR)wtbtext[0];
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hAdvancedDeviceToolbar, TB_SETIMAGELIST, 0, (LPARAM)hUpImageList);
	SendMessage(hAdvancedDeviceToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	SendMessage(hAdvancedDeviceToolbar, TB_ADDBUTTONS, 1, (LPARAM)&tbToolbarButtons);
	GetWindowRect(GetDlgItem(hDlg, IDC_ADVANCED_DRIVE_PROPERTIES), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hAdvancedDeviceToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	// Yeah, so, like, TB_GETIDEALSIZE totally super doesn't work on Windows 7, for low zoom factor and when compiled with MSVC...
	if (sz.cx < 16)
		sz.cx = fw;
	SetWindowPos(hAdvancedDeviceToolbar, hTargetSystem, rc.left + toolbar_dx, rc.top, sz.cx, rc.bottom - rc.top, 0);

	utf8_to_wchar_no_alloc(lmprintf((advanced_mode_format) ? MSG_122 : MSG_121, lmprintf(MSG_120)), wtbtext[1], ARRAYSIZE(wtbtext[1]));
	hAdvancedFormatToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, TOOLBAR_STYLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_ADVANCED_FORMAT_TOOLBAR, hMainInstance, NULL);
	SendMessage(hAdvancedFormatToolbar, CCM_SETVERSION, (WPARAM)6, 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON));
	tbToolbarButtons[0].idCommand = IDC_ADVANCED_FORMAT_OPTIONS;
	tbToolbarButtons[0].fsStyle = BTNS_SHOWTEXT | BTNS_AUTOSIZE;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iString = (INT_PTR)wtbtext[1];
	tbToolbarButtons[0].iBitmap = 0;
	SendMessage(hAdvancedFormatToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hUpImageList);
	SendMessage(hAdvancedFormatToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	SendMessage(hAdvancedFormatToolbar, TB_ADDBUTTONS, (WPARAM)1, (LPARAM)&tbToolbarButtons);
	GetWindowRect(GetDlgItem(hDlg, IDC_ADVANCED_FORMAT_OPTIONS), &rc);
	MapWindowPoints(NULL, hDlg, (POINT*)&rc, 2);
	SendMessage(hAdvancedFormatToolbar, TB_GETIDEALSIZE, (WPARAM)FALSE, (LPARAM)&sz);
	if (sz.cx < 16)
		sz.cx = fw;
	SetWindowPos(hAdvancedFormatToolbar, hClusterSize, rc.left + toolbar_dx, rc.top, sz.cx, rc.bottom - rc.top, 0);

	// Create the multi toolbar
	hMultiToolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, TOOLBAR_STYLE,
		0, 0, 0, 0, hMainDialog, (HMENU)IDC_MULTI_TOOLBAR, hMainInstance, NULL);
	hToolbarImageList = ImageList_Create(i16, i16, ILC_COLOR32 | ILC_HIGHQUALITYSCALE, 8, 0);
	for (i = 0; i < ARRAYSIZE(multitoolbar_icons); i++) {
		buffer = GetResource(hMainInstance, MAKEINTRESOURCEA(multitoolbar_icons[i] + icon_offset),
			_RT_RCDATA, "toolbar icon", &bufsize, FALSE);
		hIcon = CreateIconFromResourceEx(buffer, bufsize, TRUE, 0x30000, 0, 0, 0);
		// Mirror the "world" icon on RTL since we can't use an ImageList mirroring flag for that...
		if (right_to_left_mode && (i == 0))
			hIcon = CreateMirroredIcon(hIcon);
		ImageList_AddIcon(hToolbarImageList, hIcon);
		DestroyIcon(hIcon);
	}
	SendMessage(hMultiToolbar, TB_SETIMAGELIST, (WPARAM)0, (LPARAM)hToolbarImageList);
	SendMessage(hMultiToolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
	memset(tbToolbarButtons, 0, sizeof(TBBUTTON) * ARRAYSIZE(tbToolbarButtons));
	tbToolbarButtons[0].idCommand = IDC_LANG;
	tbToolbarButtons[0].fsStyle = BTNS_BUTTON;
	tbToolbarButtons[0].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[0].iBitmap = 0;
	tbToolbarButtons[1].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[1].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[1].iBitmap = I_IMAGENONE;
	tbToolbarButtons[1].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[2].idCommand = IDC_ABOUT;
	tbToolbarButtons[2].fsStyle = BTNS_BUTTON;
	tbToolbarButtons[2].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[2].iBitmap = 1;
	tbToolbarButtons[3].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[3].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[3].iBitmap = I_IMAGENONE;
	tbToolbarButtons[3].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[4].idCommand = IDC_SETTINGS;
	tbToolbarButtons[4].fsStyle = BTNS_BUTTON;
	tbToolbarButtons[4].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[4].iBitmap = 2;
	tbToolbarButtons[5].fsStyle = BTNS_AUTOSIZE;
	tbToolbarButtons[5].fsState = TBSTATE_INDETERMINATE;
	tbToolbarButtons[5].iBitmap = I_IMAGENONE;
	tbToolbarButtons[5].iString = (fScale < 1.5f) ? (INT_PTR)L"" : (INT_PTR)L" ";
	tbToolbarButtons[6].idCommand = IDC_LOG;
	tbToolbarButtons[6].fsStyle = BTNS_BUTTON;
	tbToolbarButtons[6].fsState = TBSTATE_ENABLED;
	tbToolbarButtons[6].iBitmap = 3;
	SendMessage(hMultiToolbar, TB_ADDBUTTONS, (WPARAM)7, (LPARAM)&tbToolbarButtons);
	SendMessage(hMultiToolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(i16, ddbh));

	// Subclass the progress bar so that we can write on it
	progress_original_proc = (WNDPROC)SetWindowLongPtr(hProgress, GWLP_WNDPROC, (LONG_PTR)ProgressCallback);
}

// Set up progress bar real estate allocation
void InitProgress(BOOL bOnlyFormat)
{
	int i;
	float last_end = 0.0f, slots_discrete = 0.0f, slots_analog = 0.0f;

	memset(nb_slots, 0, sizeof(nb_slots));
	memset(slot_end, 0, sizeof(slot_end));
	previous_end = 0.0f;

	if (bOnlyFormat) {
		nb_slots[OP_FORMAT] = -1;
	} else {
		nb_slots[OP_ANALYZE_MBR] = 1;
		if (IsChecked(IDC_BAD_BLOCKS)) {
			nb_slots[OP_BADBLOCKS] = -1;
		}
		if (bt != BT_NON_BOOTABLE) {
			// 1 extra slot for PBR writing
			switch (selection_default) {
			case BT_MSDOS:
				nb_slots[OP_DOS] = 3 + 1;
				break;
			case BT_FREEDOS:
				nb_slots[OP_DOS] = 5 + 1;
				break;
			case BT_IMAGE:
				nb_slots[OP_DOS] = img_report.is_iso ? -1 : 0;
				break;
			default:
				nb_slots[OP_DOS] = 2 + 1;
				break;
			}
		}
		if (selection_default == BT_IMAGE && !img_report.is_iso) {
			nb_slots[OP_FORMAT] = -1;
		} else {
			nb_slots[OP_ZERO_MBR] = 1;
			nb_slots[OP_PARTITION] = 1;
			nb_slots[OP_FIX_MBR] = 1;
			nb_slots[OP_CREATE_FS] =
				nb_steps[ComboBox_GetItemData(hFileSystem, ComboBox_GetCurSel(hFileSystem))];
			if ((!IsChecked(IDC_QUICK_FORMAT))
				|| ((fs == FS_FAT32) && ((SelectedDrive.DiskSize >= LARGE_FAT32_SIZE) || (force_large_fat32)))) {
				nb_slots[OP_FORMAT] = -1;
			}
			nb_slots[OP_FINALIZE] = ((selection_default == BT_IMAGE) && (fs == FS_NTFS)) ? 3 : 2;
		}
	}

	for (i = 0; i<OP_MAX; i++) {
		if (nb_slots[i] > 0) {
			slots_discrete += nb_slots[i] * 1.0f;
		}
		if (nb_slots[i] < 0) {
			slots_analog += nb_slots[i] * 1.0f;
		}
	}

	for (i = 0; i<OP_MAX; i++) {
		if (nb_slots[i] == 0) {
			slot_end[i + 1] = last_end;
		} else if (nb_slots[i] > 0) {
			slot_end[i + 1] = last_end + (1.0f * nb_slots[i]);
		} else if (nb_slots[i] < 0) {
			slot_end[i + 1] = last_end + (((100.0f - slots_discrete) * nb_slots[i]) / slots_analog);
		}
		last_end = slot_end[i + 1];
	}

	// If there's no analog, adjust our discrete ends to fill the whole bar
	if (slots_analog == 0.0f) {
		for (i = 0; i<OP_MAX; i++) {
			slot_end[i + 1] *= 100.0f / slots_discrete;
		}
	}
}

// Position the progress bar within each operation range
void UpdateProgress(int op, float percent)
{
	int pos;
	static uint64_t LastRefresh = 0;

	if ((op < 0) || (op >= OP_MAX)) {
		duprintf("UpdateProgress: invalid op %d\n", op);
		return;
	}
	if (percent > 100.1f) {
		//		duprintf("UpdateProgress(%d): invalid percentage %0.2f\n", op, percent);
		return;
	}
	if ((percent < 0.0f) && (nb_slots[op] <= 0)) {
		duprintf("UpdateProgress(%d): error negative percentage sent for negative slot value\n", op);
		return;
	}
	if (nb_slots[op] == 0)
		return;
	if (previous_end < slot_end[op]) {
		previous_end = slot_end[op];
	}

	if (percent < 0.0f) {
		// Negative means advance one slot (1.0%) - requires a positive slot allocation
		previous_end += (slot_end[op + 1] - slot_end[op]) / (1.0f * nb_slots[op]);
		pos = (int)(previous_end / 100.0f * MAX_PROGRESS);
	} else {
		pos = (int)((previous_end + ((slot_end[op + 1] - previous_end) * (percent / 100.0f))) / 100.0f * MAX_PROGRESS);
	}
	if (pos > MAX_PROGRESS) {
		duprintf("UpdateProgress(%d): rounding error - pos %d is greater than %d\n", op, pos, MAX_PROGRESS);
		pos = MAX_PROGRESS;
	}

	// Reduce the refresh rate, to avoid weird effects on the sliding part of progress bar
	if (GetTickCount64() > LastRefresh + (2 * MAX_REFRESH)) {
		LastRefresh = GetTickCount64();
		SendMessage(hProgress, PBM_SETPOS, (WPARAM)pos, 0);
		SetTaskbarProgressValue(pos, MAX_PROGRESS);
	}
}

void ShowLanguageMenu(RECT rcExclude)
{
	TPMPARAMS tpm;
	HMENU menu;
	RECT rc;
	LONG nb_items = 1, adjust = 0;
	loc_cmd* lcmd = NULL;
	char lang[256];
	char *search = "()";
	char *l, *r, *str;

	UM_LANGUAGE_MENU_MAX = UM_LANGUAGE_MENU;
	menu = CreatePopupMenu();
	list_for_each_entry(lcmd, &locale_list, loc_cmd, list) {
		// The appearance of LTR languages must be fixed for RTL menus
		if ((right_to_left_mode) && (!(lcmd->ctrl_id & LOC_RIGHT_TO_LEFT))) {
			str = safe_strdup(lcmd->txt[1]);
			l = strtok(str, search);
			r = strtok(NULL, search);
			static_sprintf(lang, LEFT_TO_RIGHT_EMBEDDING "(%s) " POP_DIRECTIONAL_FORMATTING "%s", r, l);
			safe_free(str);
		} else {
			static_strcpy(lang, lcmd->txt[1]);
		}
		InsertMenuU(menu, -1, MF_BYPOSITION | ((selected_locale == lcmd) ? MF_CHECKED : 0), UM_LANGUAGE_MENU_MAX++, lang);
		nb_items++;
	}

	// Empirical adjust if we have a small enough number of languages to select
	if (nb_items < 20) {
		GetWindowRect(hMultiToolbar, &rc);
		MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
		adjust = rc.top - (nb_items * ddh) / 2;
	}

	// Open the menu such that it doesn't overlap the specified rect
	tpm.cbSize = sizeof(TPMPARAMS);
	tpm.rcExclude = rcExclude;
	TrackPopupMenuEx(menu, 0,
		// In RTL languages, the menu should be placed at the bottom-right of the rect
		right_to_left_mode ? rcExclude.right : rcExclude.left,
		rcExclude.bottom + adjust, hMainDialog, &tpm);
	DestroyMenu(menu);
}

void SetPassesTooltip(void)
{
	const unsigned int pattern[BADLOCKS_PATTERN_TYPES][BADBLOCK_PATTERN_COUNT] =
	{ BADBLOCK_PATTERN_SLC, BADCLOCK_PATTERN_MLC, BADBLOCK_PATTERN_TLC };
	int sel = ComboBox_GetCurSel(hNBPasses);
	int type = (sel < 2) ? 0 : sel - 2;
	CreateTooltip(hNBPasses, lmprintf(MSG_153 + ((sel >= 2) ? 3 : sel),
		pattern[type][0], pattern[type][1], pattern[type][2], pattern[type][3]), -1);
}

void SetBootTypeDropdownWidth(void)
{
	HDC hDC;
	HFONT hFont;
	SIZE sz;
	RECT rc;

	if (image_path == NULL)
		return;
	// Set the maximum width of the dropdown according to the image selected
	GetWindowRect(hBootType, &rc);
	MapWindowPoints(NULL, hMainDialog, (POINT*)&rc, 2);
	hDC = GetDC(hBootType);
	hFont = (HFONT)SendMessageA(hBootType, WM_GETFONT, 0, 0);
	SelectObject(hDC, hFont);
	GetTextExtentPointU(hDC, short_image_path, &sz);
	safe_release_dc(hBootType, hDC);
	SendMessage(hBootType, CB_SETDROPPEDWIDTH, (WPARAM)max(sz.cx + 10, rc.right - rc.left), (LPARAM)0);
}

// Create the horizontal section lines
void OnPaint(HDC hdc)
{
	int i;
	HPEN hp = CreatePen(0, (fScale < 1.5f) ? 2 : 3, RGB(0, 0, 0));
	SelectObject(hdc, hp);
	for (i = 0; i < ARRAYSIZE(section_vpos); i++) {
		MoveToEx(hdc, mw + 10, section_vpos[i], NULL);
		LineTo(hdc, mw + fw, section_vpos[i]);
	}
}
