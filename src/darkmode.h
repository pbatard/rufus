/*
 * Rufus: The Reliable USB Formatting Utility
 * Dark mode UI implementation
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

#include <windows.h>
#include <uxtheme.h>

extern BOOL is_darkmode_enabled;

typedef enum _WindowsBuild {
	WIN10_1809 = 17763, // first build to support dark mode
	WIN10_1903 = 18362,
	WIN10_22H2 = 19045,
	WIN11_21H2 = 22000,
} WindowsBuild;

typedef enum _SubclassID {
	ButtonSubclassID = 42,
	GroupboxSubclassID,
	WindowNotifySubclassID,
	StatusBarSubclassID,
	ProgressBarSubclassID,
	StaticTextSubclassID,
	WindowCtlColorSubclassID
} SubclassID;

typedef enum _PreferredAppMode {
	AppModeDefault,
	AppModeAllowDark,
	AppModeForceDark,
	AppModeForceLight,
} PreferredAppMode;

typedef enum _WINDOWCOMPOSITIONATTRIB {
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_HOLOGRAPHIC = 23,
	WCA_EXCLUDED_FROM_DDA = 24,
	WCA_PASSIVEUPDATEMODE = 25,
	WCA_USEDARKMODECOLORS = 26,
	WCA_LAST = 27
} WINDOWCOMPOSITIONATTRIB;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef struct {
	HBRUSH hbrBackground;
	HBRUSH hbrBackgroundControl;
	HBRUSH hbrBackgroundHot;
	HBRUSH hbrEdge;
	HPEN hpnEdge;
	HPEN hpnEdgeHot;
} ThemeResources;

typedef struct {
	HTHEME hTheme;
	int iStateID;
} ButtonData;

typedef struct {
	HFONT hFont;
} StatusBarData;

typedef struct {
	HTHEME hTheme;
	int iStateID;
} ProgressBarData;

typedef struct {
	BOOL disabled;
} StaticTextData;

typedef struct {
	int horizontal;
	int vertical;
	int between;
} Borders;

BOOL GetDarkModeFromRegistry(void);
void InitDarkMode(HWND hWnd);
void SetDarkTitleBar(HWND hWnd);
void SetDarkTheme(HWND hWnd);
BOOL InitAccentColor(void);
BOOL ChangeIconColor(HICON* hIcon, COLORREF newColor);
void DestroyDarkModeGDIObjects(void);
void SubclassCtlColor(HWND hWnd);
void SubclassNotifyCustomDraw(HWND hWnd);
void SubclassStatusBar(HWND hWnd);
void SubclassProgressBarControl(HWND hWnd);
void SetDarkModeForChild(HWND hParent);

static __inline void SetDarkModeForDlg(HWND hWnd)
{
	if (is_darkmode_enabled) {
		SetDarkTitleBar(hWnd);
		SubclassCtlColor(hWnd);
	}
}

static __inline void InitAndSetDarkModeForMainDlg(HWND hWnd)
{
	is_darkmode_enabled = GetDarkModeFromRegistry();
	if (is_darkmode_enabled) {
		InitDarkMode(hWnd);
		InitAccentColor();
		SetDarkModeForDlg(hWnd);
		SubclassNotifyCustomDraw(hWnd);
	}
}
