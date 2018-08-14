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

#include <windows.h>
#include <stdint.h>
#include "resource.h"

#pragma once

// Progress bar colors
#define PROGRESS_BAR_NORMAL_TEXT_COLOR		RGB(0x00, 0x00, 0x00)
#define PROGRESS_BAR_INVERTED_TEXT_COLOR	RGB(0xFF, 0xFF, 0xFF)
#define PROGRESS_BAR_BACKGROUND_COLOR		RGB(0xE6, 0xE6, 0xE6)
#define PROGRESS_BAR_BOX_COLOR				RGB(0xBC, 0xBC, 0xBC)
#define PROGRESS_BAR_NORMAL_COLOR			RGB(0x06, 0xB0, 0x25)
#define PROGRESS_BAR_PAUSED_COLOR			RGB(0xDA, 0xCB, 0x26)
#define PROGRESS_BAR_ERROR_COLOR			RGB(0xDA, 0x26, 0x26)

// Toolbar icons main color
#define TOOLBAR_ICON_COLOR					RGB(0x29, 0x80, 0xB9)

// Toolbar default style
#define TOOLBAR_STYLE						( WS_CHILD | WS_TABSTOP | WS_VISIBLE | \
											  WS_CLIPSIBLINGS | WS_CLIPCHILDREN  | \
											  CCS_NOPARENTALIGN | CCS_NODIVIDER  | \
											  TBSTYLE_FLAT | TBSTYLE_BUTTON      | \
											  TBSTYLE_AUTOSIZE | TBSTYLE_LIST    | \
											  TBSTYLE_TOOLTIPS )

extern HWND hMultiToolbar, hSaveToolbar, hHashToolbar, hAdvancedDeviceToolbar, hAdvancedFormatToolbar;
extern HFONT hInfoFont;
extern UINT_PTR UM_LANGUAGE_MENU_MAX;
extern BOOL advanced_mode_device, advanced_mode_format, force_large_fat32, app_changed_size;
extern loc_cmd* selected_locale;
extern uint64_t persistence_size;
extern const char *sfd_name, *flash_type[BADLOCKS_PATTERN_TYPES];
extern char *short_image_path, image_option_txt[128];
extern int advanced_device_section_height, advanced_format_section_height;
extern int windows_to_go_selection, persistence_unit_selection;
extern int selection_default, cbw, ddw, ddbh, bh;

extern void SetComboEntry(HWND hDlg, int data);
extern void GetBasicControlsWidth(HWND hDlg);
extern void GetMainButtonsWidth(HWND hDlg);
extern void GetHalfDropwdownWidth(HWND hDlg);
extern void GetFullWidth(HWND hDlg);
extern void PositionMainControls(HWND hDlg);
extern void AdjustForLowDPI(HWND hDlg);
extern void SetSectionHeaders(HWND hDlg);
extern void SetPeristencePos(uint64_t pos);
extern void SetPersistenceSize(void);
extern void TogglePersistenceControls(BOOL display);
extern void ToggleAdvancedDeviceOptions(BOOL enable);
extern void ToggleAdvancedFormatOptions(BOOL enable);
extern void ToggleImageOptions(void);
extern void CreateSmallButtons(HWND hDlg);
extern void CreateAdditionalControls(HWND hDlg);
extern void InitProgress(BOOL bOnlyFormat);
extern void ShowLanguageMenu(RECT rcExclude);
extern void SetPassesTooltip(void);
extern void SetBootTypeDropdownWidth(void);
extern void OnPaint(HDC hdc);
