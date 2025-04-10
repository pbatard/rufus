/*
 * Rufus: The Reliable USB Formatting Utility
 * Dark mode for Rufus
 * Copyright © 2025 ozone10
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

#pragma once

// WinAPI
#include <Windows.h>

BOOL IsDarkModeEnabled(void);
BOOL GetDarkModeFromReg(void);
void InitDarkMode(HWND hWnd);
void SetDarkTitleBar(HWND hWnd);
void SetDarkTheme(HWND hWnd);
BOOL InitAccentColor(void);
BOOL ChangeIconColor(HICON* hIcon, COLORREF newColor);
COLORREF GetTextColorForDarkMode(void);
COLORREF GetDisabledTextColor(void);
COLORREF GetControlBackgroundColor(void);
COLORREF GetEdgeColor(void);
void DestroyDarkModeGDIObjects(void);
void SubclassCtlColor(HWND hWnd);
void SubclassNotifyCustomDraw(HWND hWnd);
void SubclassStatusBar(HWND hWnd);
void SubclassProgressBarControl(HWND hWnd);
void SetDarkModeForChild(HWND hParent);

static __inline void SetDarkModeForDlg(HWND hWnd)
{
	if (IsDarkModeEnabled()) {
		SetDarkTitleBar(hWnd);
		SubclassCtlColor(hWnd);
	}
}

static __inline void InitAndSetDarkModeForMainDlg(HWND hWnd)
{
	if (GetDarkModeFromReg()) {
		InitDarkMode(hWnd);
		InitAccentColor();
		SetDarkModeForDlg(hWnd);
		SubclassNotifyCustomDraw(hWnd);
	}
}
