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

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <dwmapi.h>
#include <richedit.h>
#include <uxtheme.h>
#include <versionhelpers.h>
#include <vssym32.h>
#include <math.h>

#include "rufus.h"
#include "ui.h"
#include "settings.h"
#include "darkmode.h"

PF_TYPE_DECL(WINAPI, BOOL, AllowDarkModeForWindow, (HWND, BOOL));
PF_TYPE_DECL(WINAPI, PreferredAppMode, SetPreferredAppMode, (PreferredAppMode));
PF_TYPE_DECL(WINAPI, VOID, FlushMenuThemes, (VOID));
PF_TYPE_DECL(WINAPI, BOOL, SetWindowCompositionAttribute, (HWND, WINDOWCOMPOSITIONATTRIBDATA*));

BOOL is_darkmode_enabled = FALSE;

static COLORREF color_accent = TOOLBAR_ICON_COLOR;

static inline BOOL IsAtLeastWin10Build(DWORD buildNumber)
{
	OSVERSIONINFOEXW osvi = { 0 };

	if (!IsWindows10OrGreater())
		return FALSE;

	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwBuildNumber = buildNumber;

	return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL));
}

static inline BOOL IsAtLeastWin10(void)
{
	return IsAtLeastWin10Build(WIN10_1809);
}

static inline BOOL IsAtLeastWin11(void)
{
	return IsAtLeastWin10Build(WIN11_21H2);
}

static inline BOOL IsHighContrast(void)
{
	HIGHCONTRASTW highContrast = { 0 };

	highContrast.cbSize = sizeof(HIGHCONTRASTW);
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRASTW), &highContrast, FALSE))
		return (highContrast.dwFlags & HCF_HIGHCONTRASTON) == HCF_HIGHCONTRASTON;
	return FALSE;
}

BOOL GetDarkModeFromRegistry(void)
{
	DWORD data = 0, size = sizeof(data);

	if (!IsAtLeastWin10() || IsHighContrast())
		return FALSE;

	// 0 = follow system, 1 = dark mode always, anything else = light mode always
	switch (ReadSetting32(SETTING_DARK_MODE)) {
	case 0:
		if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &data, &size) == ERROR_SUCCESS)
			// Dark mode is 0, light mode is 1
			return (data == 0);
		return FALSE;
	case 1:
		return TRUE;
	default:
		return FALSE;
	}
}

void InitDarkMode(HWND hWnd)
{
	if (!IsAtLeastWin10() || IsHighContrast())
		goto out;

	PF_INIT_ID_OR_OUT(AllowDarkModeForWindow, UxTheme, 133);
	PF_INIT_ID_OR_OUT(SetPreferredAppMode, UxTheme, 135);
	PF_INIT_ID_OR_OUT(FlushMenuThemes, UxTheme, 136);

	pfAllowDarkModeForWindow(hWnd, is_darkmode_enabled);
	pfSetPreferredAppMode(is_darkmode_enabled ? AppModeForceDark : AppModeForceLight);
	pfFlushMenuThemes();
	return;

out:
	is_darkmode_enabled = FALSE;
}

void SetDarkTitleBar(HWND hWnd)
{
	if (IsAtLeastWin11()) {
		DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &is_darkmode_enabled, sizeof(is_darkmode_enabled));
		return;
	}
	if (IsAtLeastWin10Build(WIN10_1903)) {
		PF_INIT_OR_OUT(SetWindowCompositionAttribute, user32);
		WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &is_darkmode_enabled, sizeof(is_darkmode_enabled) };
		pfSetWindowCompositionAttribute(hWnd, &data);
		return;
	}
	// only for Windows 10 1809 build 17763
	if (IsAtLeastWin10()) {
		SetPropW(hWnd, L"UseImmersiveDarkModeColors", (HANDLE)(intptr_t)is_darkmode_enabled);
		return;
	}

out:
	is_darkmode_enabled = FALSE;
}

void SetDarkTheme(HWND hWnd)
{
	SetWindowTheme(hWnd, is_darkmode_enabled ? L"DarkMode_Explorer" : NULL, NULL);
}

/*
 * Accent color functions
 */
// Adapted from https://stackoverflow.com/a/56678483
static double LinearValue(double color_channel)
{
	color_channel /= 255.0;
	if (color_channel <= 0.04045)
		return color_channel / 12.92;
	return pow((color_channel + 0.055) / 1.055, 2.4);
}

static double CalculatePerceivedLightness(COLORREF clr)
{
	double r, g, b, luminance, lightness;

	r = LinearValue((double)GetRValue(clr));
	g = LinearValue((double)GetGValue(clr));
	b = LinearValue((double)GetBValue(clr));
	luminance = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
	lightness = (luminance <= 216.0 / 24389.0) ?
		(luminance * 24389.0 / 27.0) :
		((pow(luminance, 1.0 / 3.0) * 116.0) - 16.0);
	return lightness;
}

BOOL InitAccentColor(void)
{
	const double lightnessTreshold = 50.0 - 4.0;
	BOOL opaque = TRUE;

	if (SUCCEEDED(DwmGetColorizationColor(&color_accent, &opaque))) {
		color_accent = RGB(GetBValue(color_accent), GetGValue(color_accent), GetRValue(color_accent));
		// Check if accent color is too dark
		if (CalculatePerceivedLightness(color_accent) < lightnessTreshold) {
			color_accent = DARKMODE_TOOLBAR_ICON_COLOR;
			return FALSE;
		}
		return TRUE;
	}
	color_accent = TOOLBAR_ICON_COLOR;
	return FALSE;
}

// Rufus uses monocolour icons, so changing colour is easy
BOOL ChangeIconColor(HICON* hIcon, COLORREF new_color)
{
	HDC hdcScreen = NULL, hdcBitmap = NULL;
	HBITMAP hbm = NULL;
	BITMAP bmp = { 0 };
	ICONINFO ii;
	HICON hIconNew = NULL;
	RGBQUAD* pixels = NULL;

	if (!*hIcon || !is_darkmode_enabled)
		return FALSE;

	if (new_color == 0)
		new_color = color_accent;

	hdcBitmap = CreateCompatibleDC(NULL);
	hdcScreen = GetDC(NULL);
	if (hdcScreen) {
		if (hdcBitmap) {
			if (GetIconInfo(*hIcon, &ii) && GetObject(ii.hbmColor, sizeof(BITMAP), &bmp)) {
				BITMAPINFO bmi = { 0 };
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = bmp.bmWidth;
				bmi.bmiHeader.biHeight = -bmp.bmHeight;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;

				pixels = (RGBQUAD*)malloc((size_t)bmp.bmWidth * bmp.bmHeight * sizeof(RGBQUAD));
				if (pixels && GetDIBits(hdcBitmap, ii.hbmColor, 0, bmp.bmHeight, pixels, &bmi, DIB_RGB_COLORS)) {
					for (int i = 0; i < bmp.bmWidth * bmp.bmHeight; i++) {
						if (pixels[i].rgbReserved != 0) {
							pixels[i].rgbRed = GetRValue(new_color);
							pixels[i].rgbGreen = GetGValue(new_color);
							pixels[i].rgbBlue = GetBValue(new_color);
						}
					}

					hbm = CreateCompatibleBitmap(hdcScreen, bmp.bmWidth, bmp.bmHeight);
					if (hbm != NULL) {
						SetDIBits(hdcBitmap, hbm, 0, bmp.bmHeight, pixels, &bmi, DIB_RGB_COLORS);
						if (ii.hbmColor != NULL)
							DeleteObject(ii.hbmColor);
						ii.hbmColor = hbm;
						hIconNew = CreateIconIndirect(&ii);
					} else {
						safe_free(pixels);
						DeleteObject(ii.hbmColor);
						DeleteObject(ii.hbmMask);
						DeleteDC(hdcBitmap);
						ReleaseDC(NULL, hdcScreen);
						return FALSE;
					}
				}

				safe_free(pixels);
				safe_delete_object(hbm);
				DeleteObject(ii.hbmColor);
				DeleteObject(ii.hbmMask);
			}
		}
		ReleaseDC(NULL, hdcScreen);
	}

	if (hdcBitmap != NULL)
		DeleteDC(hdcBitmap);

	if (hIconNew == NULL)
		return FALSE;
	DestroyIcon(*hIcon);
	*hIcon = hIconNew;

	return TRUE;
}

/*
 * Dark mode custom colors
 */
static ThemeResources theme_resources = {
	NULL, NULL, NULL, NULL, NULL, NULL
};

static HBRUSH GetDlgBackgroundBrush(void)
{
	if (theme_resources.hbrBackground == NULL)
		theme_resources.hbrBackground = CreateSolidBrush(DARKMODE_NORMAL_DIALOG_BACKGROUND_COLOR);
	return theme_resources.hbrBackground;
}

static HBRUSH GetCtrlBackgroundBrush(void)
{
	if (theme_resources.hbrBackgroundControl == NULL)
		theme_resources.hbrBackgroundControl = CreateSolidBrush(DARKMODE_NORMAL_CONTROL_BACKGROUND_COLOR);
	return theme_resources.hbrBackgroundControl;
}

static HBRUSH GetHotBackgroundBrush(void)
{
	if (theme_resources.hbrBackgroundHot == NULL)
		theme_resources.hbrBackgroundHot = CreateSolidBrush(DARKMODE_HOT_CONTROL_BACKGROUND_COLOR);
	return theme_resources.hbrBackgroundHot;
}

static HBRUSH GetEdgeBrush(void)
{
	if (theme_resources.hbrEdge == NULL)
		theme_resources.hbrEdge = CreateSolidBrush(DARKMODE_NORMAL_CONTROL_EDGE_COLOR);
	return theme_resources.hbrEdge;
}

static HPEN GetEdgePen(void)
{
	if (theme_resources.hpnEdge == NULL)
		theme_resources.hpnEdge = CreatePen(PS_SOLID, 1, DARKMODE_NORMAL_CONTROL_EDGE_COLOR);
	return theme_resources.hpnEdge;
}
static HPEN GetHotEdgePen(void)
{
	if (theme_resources.hpnEdgeHot == NULL)
		theme_resources.hpnEdgeHot = CreatePen(PS_SOLID, 1, DARKMODE_HOT_CONTROL_EDGE_COLOR);
	return theme_resources.hpnEdgeHot;
}

void DestroyDarkModeGDIObjects(void)
{
	safe_delete_object(theme_resources.hbrBackground);
	safe_delete_object(theme_resources.hbrBackgroundControl);
	safe_delete_object(theme_resources.hbrBackgroundHot);
	safe_delete_object(theme_resources.hbrEdge);
	safe_delete_object(theme_resources.hpnEdge);
	safe_delete_object(theme_resources.hpnEdgeHot);
}

/*
 * Paint round rect helpers
 */
static void PaintRoundRect(HDC hdc, const RECT rect, HPEN hpen, HBRUSH hBrush, int width, int height)
{
	HBRUSH holdBrush = (HBRUSH)SelectObject(hdc, hBrush);
	HPEN holdPen = (HPEN)SelectObject(hdc, hpen);

	RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, width, height);
	SelectObject(hdc, holdBrush);
	SelectObject(hdc, holdPen);
}

static void PaintRoundFrameRect(HDC hdc, const RECT rect, HPEN hpen, int width, int height)
{
	PaintRoundRect(hdc, rect, hpen, GetStockObject(NULL_BRUSH), width, height);
}

/*
 * Button section, checkbox, radio, and groupbox style buttons
 */
static void RenderButton(HWND hWnd, HDC hdc, HTHEME hTheme, int iPartID, int iStateID)
{
	HFONT hFont = NULL, hOldFont;
	BOOL created_font = FALSE;
	LOGFONT lf;
	RECT rcClient = { 0 }, rcText = { 0 }, rcBackground, rcFocus;
	WCHAR buffer[MAX_PATH] = { '\0' };
	SIZE szBox = { 0 };
	LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	DWORD flags, ui_state;
	DTTOPTS dtto = { 0 };

	const BOOL isMultiline = (nStyle & BS_MULTILINE) == BS_MULTILINE;
	const BOOL isTop = (nStyle & BS_TOP) == BS_TOP;
	const BOOL isBottom = (nStyle & BS_BOTTOM) == BS_BOTTOM;
	const BOOL isCenter = (nStyle & BS_CENTER) == BS_CENTER;
	const BOOL isRight = (nStyle & BS_RIGHT) == BS_RIGHT;
	const BOOL isVCenter = (nStyle & BS_VCENTER) == BS_VCENTER;

	if (SUCCEEDED(GetThemeFont(hTheme, hdc, iPartID, iStateID, TMT_FONT, &lf))) {
		hFont = CreateFontIndirect(&lf);
		created_font = TRUE;
	}
	if (!hFont)
		hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
	hOldFont = (HFONT)SelectObject(hdc, hFont);

	flags = DT_LEFT | (isMultiline ? DT_WORDBREAK : DT_SINGLELINE);
	if (isCenter)
		flags |= DT_CENTER;
	else if (isRight)
		flags |= DT_RIGHT;
	if (isVCenter || (!isMultiline && !isBottom && !isTop))
		flags |= DT_VCENTER;
	else if (isBottom)
		flags |= DT_BOTTOM;

	ui_state = (DWORD)SendMessage(hWnd, WM_QUERYUISTATE, 0, 0);
	if ((ui_state & UISF_HIDEACCEL) == UISF_HIDEACCEL)
		flags |= DT_HIDEPREFIX;

	GetClientRect(hWnd, &rcClient);
	GetWindowText(hWnd, buffer, _countof(buffer));
	GetThemePartSize(hTheme, hdc, iPartID, iStateID, NULL, TS_DRAW, &szBox);
	GetThemeBackgroundContentRect(hTheme, hdc, iPartID, iStateID, &rcClient, &rcText);
	rcBackground = rcClient;
	if (!isMultiline)
		rcBackground.top += (rcText.bottom - rcText.top - szBox.cy) / 2;
	rcBackground.bottom = rcBackground.top + szBox.cy;
	rcBackground.right = rcBackground.left + szBox.cx;
	rcText.left = rcBackground.right + 3;

	DrawThemeParentBackground(hWnd, hdc, &rcClient);
	DrawThemeBackground(hTheme, hdc, iPartID, iStateID, &rcBackground, NULL);

	dtto.dwSize = sizeof(DTTOPTS);
	dtto.dwFlags = DTT_TEXTCOLOR;
	dtto.crText = !IsWindowEnabled(hWnd) ? DARKMODE_DISABLED_TEXT_COLOR : DARKMODE_NORMAL_TEXT_COLOR;

	// coverity[negative_returns]
	DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, buffer, -1, flags, &rcText, &dtto);

	if (((SendMessage(hWnd, BM_GETSTATE, 0, 0) & BST_FOCUS) == BST_FOCUS) && ((ui_state & UISF_HIDEFOCUS) != UISF_HIDEFOCUS)) {
		dtto.dwFlags |= DTT_CALCRECT;
		// coverity[negative_returns]
		DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, buffer, -1, flags | DT_CALCRECT, &rcText, &dtto);
		rcFocus = rcText;
		rcFocus.bottom++;
		rcFocus.left--;
		rcFocus.right++;
		DrawFocusRect(hdc, &rcFocus);
	}

	SelectObject(hdc, hOldFont);
	if (created_font)
		DeleteObject(hFont);
}

static void PaintButton(HWND hWnd, HDC hdc, ButtonData* pButtonData)
{
	const DWORD state = (DWORD)SendMessage(hWnd, BM_GETSTATE, 0, 0);
	int part_id = BP_CHECKBOX, state_id = RBS_UNCHECKEDNORMAL;
	BP_ANIMATIONPARAMS anim_params = { 0 };
	HANIMATIONBUFFER anim_buffer;
	RECT rcClient = { 0 };
	HDC hdcFrom = NULL, hdcTo = NULL;

	switch (GetWindowLongPtr(hWnd, GWL_STYLE) & BS_TYPEMASK) {
	case BS_CHECKBOX:
	case BS_AUTOCHECKBOX:
	case BS_3STATE:
	case BS_AUTO3STATE:
		part_id = BP_CHECKBOX;
		break;
	case BS_RADIOBUTTON:
	case BS_AUTORADIOBUTTON:
		part_id = BP_RADIOBUTTON;
		break;
	default:
		break;
	}

	// States of BP_CHECKBOX and BP_RADIOBUTTON are the same
	if (!IsWindowEnabled(hWnd))
		state_id = RBS_UNCHECKEDDISABLED;
	else if (state & BST_PUSHED)
		state_id = RBS_UNCHECKEDPRESSED;
	else if (state & BST_HOT)
		state_id = RBS_UNCHECKEDHOT;
	if (state & BST_CHECKED)
		state_id += 4;
	if (BufferedPaintRenderAnimation(hWnd, hdc))
		return;

	anim_params.cbSize = sizeof(BP_ANIMATIONPARAMS);
	anim_params.style = BPAS_LINEAR;
	if (state_id != pButtonData->iStateID)
		GetThemeTransitionDuration(pButtonData->hTheme, part_id, pButtonData->iStateID, state_id, TMT_TRANSITIONDURATIONS, &anim_params.dwDuration);

	GetClientRect(hWnd, &rcClient);
	anim_buffer = BeginBufferedAnimation(hWnd, hdc, &rcClient, BPBF_COMPATIBLEBITMAP, NULL, &anim_params, &hdcFrom, &hdcTo);
	if (anim_buffer != NULL) {
		if (hdcFrom != NULL)
			RenderButton(hWnd, hdcFrom, pButtonData->hTheme, part_id, pButtonData->iStateID);
		if (hdcTo != NULL)
			RenderButton(hWnd, hdcTo, pButtonData->hTheme, part_id, state_id);
		pButtonData->iStateID = state_id;
		EndBufferedAnimation(anim_buffer, TRUE);
	} else {
		RenderButton(hWnd, hdc, pButtonData->hTheme, part_id, state_id);
		pButtonData->iStateID = state_id;
	}
}

static LRESULT CALLBACK ButtonSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	ButtonData* data = (ButtonData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, ButtonSubclass, uIdSubclass);
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		free(data);
		break;

	case WM_ERASEBKGND:
		if (data->hTheme == NULL)
			data->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
		if (data->hTheme != NULL)
			return TRUE;
		break;

	case WM_PRINTCLIENT:
	case WM_PAINT:
		PAINTSTRUCT ps = { 0 };
		HDC hdc = (HDC)wParam;
		if (data->hTheme == NULL) {
			data->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
			if (data->hTheme == NULL)
				break;
		}
		if (hdc == NULL)
			hdc = BeginPaint(hWnd, &ps);
		PaintButton(hWnd, hdc, data);
		if (ps.hdc != NULL)
			EndPaint(hWnd, &ps);
		return 0;

	case WM_THEMECHANGED:
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		break;

	case WM_SIZE:
	case WM_DESTROY:
		BufferedPaintStopAllAnimations(hWnd);
		break;

	case WM_ENABLE:
		// Skip the button's normal WindowProc so it won't redraw out of WM_PAINT
		LRESULT lr = DefWindowProc(hWnd, uMsg, wParam, lParam);
		InvalidateRect(hWnd, NULL, FALSE);
		return lr;

	case WM_UPDATEUISTATE:
		if (HIWORD(wParam) & (UISF_HIDEACCEL | UISF_HIDEFOCUS))
			InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassButtonControl(HWND hWnd)
{
	void* data = NULL;

	if (GetWindowSubclass(hWnd, ButtonSubclass, (UINT_PTR)ButtonSubclassID, NULL))
		return;

	data = calloc(1, sizeof(ButtonData));
	SetWindowSubclass(hWnd, ButtonSubclass, (UINT_PTR)ButtonSubclassID, (DWORD_PTR)data);
}

static void PaintGroupbox(HWND hWnd, HDC hdc, ButtonData buttonData)
{
	const int iStateID = IsWindowEnabled(hWnd) ? GBS_NORMAL : GBS_DISABLED;
	const BOOL centered = (GetWindowLongPtr(hWnd, GWL_STYLE) & BS_CENTER) == BS_CENTER;
	BOOL font_created = FALSE;
	HFONT hFont = NULL, hOldFont;
	LOGFONT lf;
	WCHAR buffer[MAX_PATH] = { '\0' };
	SIZE szText = { 0 };
	RECT rcClient = { 0 }, rcText, rcBackground, rcContent;

	if (SUCCEEDED(GetThemeFont(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, TMT_FONT, &lf))) {
		hFont = CreateFontIndirect(&lf);
		font_created = TRUE;
	}
	if (hFont == NULL) {
		hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
		font_created = FALSE;
	}
	hOldFont = (HFONT)SelectObject(hdc, hFont);
	GetWindowText(hWnd, buffer, _countof(buffer));
	GetClientRect(hWnd, &rcClient);
	rcClient.bottom -= 1;
	rcText = rcClient; rcBackground = rcClient;

	if (buffer[0]) {
		GetTextExtentPoint32(hdc, buffer, (int)wcslen(buffer), &szText);
		rcBackground.top += szText.cy / 2;
		rcText.left += centered ? ((rcClient.right - rcClient.left - szText.cx) / 2) : 7;
		rcText.bottom = rcText.top + szText.cy;
		rcText.right = rcText.left + szText.cx + 4;
		ExcludeClipRect(hdc, rcText.left, rcText.top, rcText.right, rcText.bottom);
	} else {
		GetTextExtentPoint32(hdc, L"M", 1, &szText);
		rcBackground.top += szText.cy / 2;
	}
	rcContent = rcBackground;

	GetThemeBackgroundContentRect(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, &rcBackground, &rcContent);
	ExcludeClipRect(hdc, rcContent.left, rcContent.top, rcContent.right, rcContent.bottom);
	PaintRoundFrameRect(hdc, rcBackground, GetEdgePen(), 0, 0);
	SelectClipRgn(hdc, NULL);

	if (buffer[0]) {
		DTTOPTS dtto = { 0 };
		DWORD flags = centered ? DT_CENTER : DT_LEFT;
		InflateRect(&rcText, -2, 0);
		dtto.dwSize = sizeof(DTTOPTS);
		dtto.dwFlags = DTT_TEXTCOLOR;
		dtto.crText = IsWindowEnabled(hWnd) ? DARKMODE_NORMAL_TEXT_COLOR : DARKMODE_DISABLED_TEXT_COLOR;
		if (SendMessage(hWnd, WM_QUERYUISTATE, 0, 0) != (LRESULT)NULL)
			flags |= DT_HIDEPREFIX;
		// coverity[negative_returns]
		DrawThemeTextEx(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, buffer, -1, flags | DT_SINGLELINE, &rcText, &dtto);
	}

	SelectObject(hdc, hOldFont);
	if (font_created)
		DeleteObject(hFont);
}

static LRESULT CALLBACK GroupboxSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	ButtonData* data = (ButtonData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, ButtonSubclass, uIdSubclass);
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		free(data);
		break;

	case WM_ERASEBKGND:
		if (data->hTheme == NULL)
			data->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
		if (data->hTheme != NULL)
			return TRUE;
		break;

	case WM_PRINTCLIENT:
	case WM_PAINT:
		PAINTSTRUCT ps = { 0 };
		HDC hdc = (HDC)wParam;
		if (data->hTheme == NULL) {
			data->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
			if (data->hTheme == NULL)
				break;
		}
		if (hdc == NULL)
			hdc = BeginPaint(hWnd, &ps);
		PaintGroupbox(hWnd, hdc, *data);
		if (ps.hdc != NULL)
			EndPaint(hWnd, &ps);
		return 0;

	case WM_THEMECHANGED:
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		break;

	case WM_ENABLE:
		RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassGroupboxControl(HWND hWnd)
{
	void* data = NULL;

	if (GetWindowSubclass(hWnd, GroupboxSubclass, (UINT_PTR)GroupboxSubclassID, NULL))
		return;

	data = calloc(1, sizeof(ButtonData));
	SetWindowSubclass(hWnd, GroupboxSubclass, (UINT_PTR)GroupboxSubclassID, (DWORD_PTR)data);
}

/*
 * Toolbar section
 */
static LRESULT DarkToolBarNotifyCustomDraw(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LPNMTBCUSTOMDRAW lpnmtbcd = (LPNMTBCUSTOMDRAW)lParam;
	static int roundness = 0;

	switch (lpnmtbcd->nmcd.dwDrawStage) {
	case CDDS_PREPAINT:
		if (IsAtLeastWin11())
			roundness = 5;
		FillRect(lpnmtbcd->nmcd.hdc, &lpnmtbcd->nmcd.rc, GetDlgBackgroundBrush());
		return CDRF_NOTIFYITEMDRAW;

	case CDDS_ITEMPREPAINT:
		LRESULT lr = TBCDRF_USECDCOLORS;
		lpnmtbcd->hbrMonoDither = GetDlgBackgroundBrush();
		lpnmtbcd->hbrLines = GetEdgeBrush();
		lpnmtbcd->hpenLines = GetEdgePen();
		lpnmtbcd->clrText = DARKMODE_NORMAL_TEXT_COLOR;
		lpnmtbcd->clrTextHighlight = DARKMODE_NORMAL_TEXT_COLOR;
		lpnmtbcd->clrBtnFace = DARKMODE_NORMAL_DIALOG_BACKGROUND_COLOR;
		lpnmtbcd->clrBtnHighlight = DARKMODE_NORMAL_CONTROL_BACKGROUND_COLOR;
		lpnmtbcd->clrHighlightHotTrack = DARKMODE_HOT_CONTROL_BACKGROUND_COLOR;
		lpnmtbcd->nStringBkMode = TRANSPARENT;
		lpnmtbcd->nHLStringBkMode = TRANSPARENT;
		if ((lpnmtbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT) {
			PaintRoundRect(lpnmtbcd->nmcd.hdc, lpnmtbcd->nmcd.rc, GetHotEdgePen(), GetHotBackgroundBrush(), roundness, roundness);
			lpnmtbcd->nmcd.uItemState &= ~(CDIS_CHECKED | CDIS_HOT);
		} else if ((lpnmtbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED) {
			PaintRoundRect(lpnmtbcd->nmcd.hdc, lpnmtbcd->nmcd.rc, GetEdgePen(), GetCtrlBackgroundBrush(), roundness, roundness);
			lpnmtbcd->nmcd.uItemState &= ~CDIS_CHECKED;
		}
		if ((lpnmtbcd->nmcd.uItemState & CDIS_SELECTED) == CDIS_SELECTED)
			lr |= TBCDRF_NOBACKGROUND;
		return lr;

	default:
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT DarkTrackBarNotifyCustomDraw(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LPNMCUSTOMDRAW lpnmcd = (LPNMCUSTOMDRAW)lParam;

	switch (lpnmcd->dwDrawStage) {
	case CDDS_PREPAINT:
		return CDRF_NOTIFYITEMDRAW;

	case CDDS_ITEMPREPAINT:
		switch (lpnmcd->dwItemSpec) {
		case TBCD_THUMB:
			if ((lpnmcd->uItemState & CDIS_SELECTED) == CDIS_SELECTED) {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetCtrlBackgroundBrush());
				return CDRF_SKIPDEFAULT;
			}
			break;

		case TBCD_CHANNEL:
			if (!IsWindowEnabled(lpnmcd->hdr.hwndFrom)) {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetDlgBackgroundBrush());
				PaintRoundFrameRect(lpnmcd->hdc, lpnmcd->rc, GetEdgePen(), 0, 0);
			} else {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetCtrlBackgroundBrush());
			}
			return CDRF_SKIPDEFAULT;

		default:
			break;
		}
		break;

	default:
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK WindowNotifySubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	(void)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, WindowNotifySubclass, uIdSubclass);
		break;

	case WM_NOTIFY:
		LPNMHDR lpnmhdr = (LPNMHDR)lParam;
		WCHAR str[32] = { 0 };
		GetClassName(lpnmhdr->hwndFrom, str, ARRAYSIZE(str));

		switch (lpnmhdr->code) {
		case NM_CUSTOMDRAW:
			if (_wcsicmp(str, TOOLBARCLASSNAME) == 0)
				return DarkToolBarNotifyCustomDraw(hWnd, uMsg, wParam, lParam);
			if (_wcsicmp(str, TRACKBAR_CLASS) == 0)
				return DarkTrackBarNotifyCustomDraw(hWnd, uMsg, wParam, lParam);
			break;
		}
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassNotifyCustomDraw(HWND hWnd)
{
	if (!GetWindowSubclass(hWnd, WindowNotifySubclass, (UINT_PTR)WindowNotifySubclassID, NULL))
		SetWindowSubclass(hWnd, WindowNotifySubclass, (UINT_PTR)WindowNotifySubclassID, 0);
}

/*
 * Status bar section
 */
static void PaintStatusBar(HWND hWnd, HDC hdc, StatusBarData statusBarData)
{
	const int last_div = (int)SendMessage(hWnd, SB_GETPARTS, 0, 0) - 1;
	Borders borders = { 0 };
	RECT rcClient = { 0 }, rcPart = { 0 }, rcIntersect = { 0 };
	HPEN hOldPen = (HPEN)SelectObject(hdc, GetEdgePen());
	HFONT hOldFont = (HFONT)SelectObject(hdc, statusBarData.hFont);
	LRESULT r1, r2;
	LPWSTR buffer;
	DWORD text_len;
	UINT id;

	SendMessage(hWnd, SB_GETBORDERS, 0, (LPARAM)&borders);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, DARKMODE_NORMAL_TEXT_COLOR);
	GetClientRect(hWnd, &rcClient);
	FillRect(hdc, &rcClient, GetDlgBackgroundBrush());

	for (int i = 0; i <= last_div; i++) {
		SendMessage(hWnd, SB_GETRECT, i, (LPARAM)&rcPart);
		if (!IntersectRect(&rcIntersect, &rcPart, &rcClient))
			continue;
		if (i < last_div) {
			POINT edges[] = {
				{ rcPart.right - borders.between, rcPart.top + 1 },
				{ rcPart.right - borders.between, rcPart.bottom - 3 }
			};
			Polyline(hdc, edges, _countof(edges));
		}

		rcPart.left += borders.between;
		rcPart.right -= borders.vertical;

		r1 = SendMessage(hWnd, SB_GETTEXTLENGTH, i, 0);
		text_len = LOWORD(r1);
		buffer = (LPWSTR)calloc((size_t)text_len + 1, sizeof(WCHAR));
		r2 = SendMessage(hWnd, SB_GETTEXT, i, (LPARAM)buffer);
		if (text_len == 0 && (HIWORD(r1) & SBT_OWNERDRAW)) {
			id = GetDlgCtrlID(hWnd);
			DRAWITEMSTRUCT dis = { 0, 0, (UINT)i, ODA_DRAWENTIRE, id, hWnd, hdc, rcPart, (ULONG_PTR)r2 };
			SendMessage(GetParent(hWnd), WM_DRAWITEM, id, (LPARAM)&dis);
		} else if (buffer != NULL) {
			DrawText(hdc, buffer, (int)text_len, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
		}
		free(buffer);
	}

	POINT horizontal_edge[] = { { rcClient.left, rcClient.top }, { rcClient.right, rcClient.top } };
	Polyline(hdc, horizontal_edge, _countof(horizontal_edge));

	SelectObject(hdc, hOldFont);
	SelectObject(hdc, hOldPen);
}

static LRESULT CALLBACK StatusBarSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	StatusBarData* data = (StatusBarData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, StatusBarSubclass, uIdSubclass);
		if (data->hFont != NULL) {
			DeleteObject(data->hFont);
			data->hFont = NULL;
		}
		free(data);
		break;

	case WM_ERASEBKGND:
		return TRUE;

	case WM_PAINT:
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		PaintStatusBar(hWnd, hdc, *data);
		EndPaint(hWnd, &ps);
		return 0;

	case WM_THEMECHANGED:
		NONCLIENTMETRICS ncm = { 0 };
		ncm.cbSize = sizeof(NONCLIENTMETRICS);
		if (data->hFont != NULL) {
			DeleteObject(data->hFont);
			data->hFont = NULL;
		}
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
			data->hFont = CreateFontIndirect(&ncm.lfStatusFont);
		return 0;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassStatusBar(HWND hWnd)
{
	StatusBarData* data = NULL;
	NONCLIENTMETRICS ncm = { 0 };
	ncm.cbSize = sizeof(NONCLIENTMETRICS);

	if (!is_darkmode_enabled || GetWindowSubclass(hWnd, StatusBarSubclass, (UINT_PTR)StatusBarSubclassID, NULL))
		return;

	data = (StatusBarData*)malloc(sizeof(StatusBarData));
	if (data == NULL)
		return;
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
		data->hFont = CreateFontIndirect(&ncm.lfStatusFont);
	SetWindowSubclass(hWnd, StatusBarSubclass, (UINT_PTR)StatusBarSubclassID, (DWORD_PTR)data);
}

/*
 * Progress bar section
 */
static void GetProgressBarRects(HWND hWnd, RECT* rcEmpty, RECT* rcFilled)
{
	const int pos = (int)SendMessage(hWnd, PBM_GETPOS, 0, 0);
	PBRANGE range = { 0, 0 };
	SendMessage(hWnd, PBM_GETRANGE, TRUE, (LPARAM)&range);
	const int min = range.iLow;
	const int cur_pos = pos - min;

	if (cur_pos != 0) {
		int totalWidth = rcEmpty->right - rcEmpty->left;
		rcFilled->left = rcEmpty->left;
		rcFilled->top = rcEmpty->top;
		rcFilled->bottom = rcEmpty->bottom;
		if (range.iHigh - min != 0)
			rcFilled->right = rcEmpty->left + (int)((double)(cur_pos) / (range.iHigh - min) * totalWidth);
		else
			rcFilled->right = rcEmpty->right;
		rcEmpty->left = rcFilled->right; // To avoid painting under filled part
	}
}

static void PaintProgressBar(HWND hWnd, HDC hdc, ProgressBarData progressBarData)
{
	RECT rcClient = { 0 }, rcFill = { 0 };

	GetClientRect(hWnd, &rcClient);
	PaintRoundFrameRect(hdc, rcClient, GetEdgePen(), 0, 0);
	InflateRect(&rcClient, -1, -1);
	rcClient.left = 1;
	GetProgressBarRects(hWnd, &rcClient, &rcFill);
	DrawThemeBackground(progressBarData.hTheme, hdc, PP_FILL, progressBarData.iStateID, &rcFill, NULL);
	FillRect(hdc, &rcClient, GetCtrlBackgroundBrush());
}

static LRESULT CALLBACK ProgressBarSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	ProgressBarData* data = (ProgressBarData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, ProgressBarSubclass, uIdSubclass);
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		free(data);
		break;

	case WM_ERASEBKGND:
		if (data->hTheme == NULL)
			data->hTheme = OpenThemeData(hWnd, VSCLASS_PROGRESS);
		if (data->hTheme != NULL)
			return TRUE;
		break;

	case WM_PAINT:
		PAINTSTRUCT ps;
		HDC hdc;
		if (data->hTheme == NULL) {
			data->hTheme = OpenThemeData(hWnd, VSCLASS_PROGRESS);
			if (data->hTheme == NULL)
				break;
		}
		hdc = BeginPaint(hWnd, &ps);
		PaintProgressBar(hWnd, hdc, *data);
		EndPaint(hWnd, &ps);
		return 0;

	case WM_THEMECHANGED:
		if (data->hTheme != NULL)
			CloseThemeData(data->hTheme);
		break;

	case PBM_SETSTATE:
		switch (wParam) {
		case PBST_NORMAL:
			data->iStateID = PBFS_NORMAL; // Green
			break;
		case PBST_ERROR:
			data->iStateID = PBFS_ERROR; // Red
			break;
		case PBST_PAUSED:
			data->iStateID = PBFS_PAUSED; // Yellow
			break;
		}
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassProgressBarControl(HWND hWnd)
{
	void* data = NULL;

	if (!is_darkmode_enabled || GetWindowSubclass(hWnd, ProgressBarSubclass, (UINT_PTR)ProgressBarSubclassID, NULL))
		return;

	data = calloc(1, sizeof(ProgressBarData));
	SetWindowSubclass(hWnd, ProgressBarSubclass, (UINT_PTR)ProgressBarSubclassID, (DWORD_PTR)data);
}

/*
 * Static text section
 */
static LRESULT CALLBACK StaticTextSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	StaticTextData* data = (StaticTextData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, StaticTextSubclass, uIdSubclass);
		free(data);
		break;

	case WM_ENABLE:
		RECT rcClient = { 0 };
		const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
		data->disabled = (wParam == FALSE);
		if (data->disabled)
			SetWindowLongPtr(hWnd, GWL_STYLE, style & ~WS_DISABLED);
		GetClientRect(hWnd, &rcClient);
		MapWindowPoints(hWnd, GetParent(hWnd), (LPPOINT)&rcClient, 2);
		RedrawWindow(GetParent(hWnd), &rcClient, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
		if (data->disabled)
			SetWindowLongPtr(hWnd, GWL_STYLE, style | WS_DISABLED);
		return 0;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassStaticText(HWND hWnd)
{
	void* data = NULL;

	if (GetWindowSubclass(hWnd, StaticTextSubclass, (UINT_PTR)StaticTextSubclassID, NULL))
		return;

	data = calloc(1, sizeof(StaticTextData));
	SetWindowSubclass(hWnd, StaticTextSubclass, (UINT_PTR)StaticTextSubclassID, (DWORD_PTR)data);
}

/*
 * Ctl color messages section
 */
static LRESULT OnCtlColorDlg(HDC hdc)
{
	SetTextColor(hdc, DARKMODE_NORMAL_TEXT_COLOR);
	SetBkColor(hdc, DARKMODE_NORMAL_DIALOG_BACKGROUND_COLOR);
	return (LRESULT)GetDlgBackgroundBrush();
}

static LRESULT OnCtlColorStatic(WPARAM wParam, LPARAM lParam)
{
	HDC hdc = (HDC)wParam;
	HWND hWnd = (HWND)lParam;
	WCHAR str[32] = { 0 };

	GetClassName(hWnd, str, ARRAYSIZE(str));
	if (_wcsicmp(str, WC_STATIC) == 0) {
		if ((GetWindowLongPtr(hWnd, GWL_STYLE) & SS_NOTIFY) == SS_NOTIFY) {
			DWORD_PTR dwRefData = 0;
			COLORREF cText = color_accent;
			if (GetWindowSubclass(hWnd, StaticTextSubclass, (UINT_PTR)StaticTextSubclassID, &dwRefData)) {
				if (((StaticTextData*)dwRefData)->disabled)
					cText = DARKMODE_DISABLED_TEXT_COLOR;
			}
			SetTextColor(hdc, cText);
			SetBkColor(hdc, DARKMODE_NORMAL_DIALOG_BACKGROUND_COLOR);
			return (LRESULT)GetDlgBackgroundBrush();
		}
	}
	// Read-only WC_EDIT
	return OnCtlColorDlg(hdc);
}

static LRESULT OnCtlColorCtrl(HDC hdc)
{
	SetTextColor(hdc, DARKMODE_NORMAL_TEXT_COLOR);
	SetBkColor(hdc, DARKMODE_NORMAL_CONTROL_BACKGROUND_COLOR);
	return (LRESULT)GetCtrlBackgroundBrush();
}

static INT_PTR OnCtlColorListbox(WPARAM wParam, LPARAM lParam)
{
	HDC hdc = (HDC)wParam;
	HWND hWnd = (HWND)lParam;
	const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	BOOL isComboBox = (nStyle & LBS_COMBOBOX) == LBS_COMBOBOX;

	if ((!isComboBox || !is_darkmode_enabled) && IsWindowEnabled(hWnd))
		return (INT_PTR)OnCtlColorCtrl(hdc);

	return (INT_PTR)OnCtlColorDlg(hdc);
}

static LRESULT CALLBACK WindowCtlColorSubclass(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	(void)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, WindowCtlColorSubclass, uIdSubclass);
		break;
	case WM_CTLCOLOREDIT:
		return OnCtlColorCtrl((HDC)wParam);
	case WM_CTLCOLORLISTBOX:
		return OnCtlColorListbox(wParam, lParam);
	case WM_CTLCOLORDLG:
		return OnCtlColorDlg((HDC)wParam);
	case WM_CTLCOLORSTATIC:
		return OnCtlColorStatic(wParam, lParam);
	case WM_PRINTCLIENT:
		return TRUE;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassCtlColor(HWND hWnd)
{
	if (!GetWindowSubclass(hWnd, WindowCtlColorSubclass, (UINT_PTR)WindowCtlColorSubclassID, NULL))
		SetWindowSubclass(hWnd, WindowCtlColorSubclass, (UINT_PTR)WindowCtlColorSubclassID, 0);
}

/*
 * Dark mode for child controls
 */
static BOOL CALLBACK DarkModeForChildCallback(HWND hWnd, LPARAM lParam)
{
	(void)lParam;
	WCHAR str[32] = { 0 };
	GetClassName(hWnd, str, ARRAYSIZE(str));

	if (_wcsicmp(str, WC_STATIC) == 0) {
		if ((GetWindowLongPtr(hWnd, GWL_STYLE) & SS_NOTIFY) == SS_NOTIFY)
			SubclassStaticText(hWnd);
	} else if (_wcsicmp(str, WC_BUTTON) == 0) {
		switch (GetWindowLongPtr(hWnd, GWL_STYLE) & BS_TYPEMASK) {
		case BS_CHECKBOX:
		case BS_AUTOCHECKBOX:
		case BS_3STATE:
		case BS_AUTO3STATE:
		case BS_RADIOBUTTON:
		case BS_AUTORADIOBUTTON:
			if (IsAtLeastWin11())
				SetDarkTheme(hWnd);
			SubclassButtonControl(hWnd);
			break;

		case BS_GROUPBOX:
			SubclassGroupboxControl(hWnd);
			break;

		case BS_PUSHBUTTON:
		case BS_DEFPUSHBUTTON:
		case BS_SPLITBUTTON:
		case BS_DEFSPLITBUTTON:
			SetDarkTheme(hWnd);
			break;

		default:
			break;
		}
	} else if (_wcsicmp(str, WC_COMBOBOX) == 0) {
		SetWindowTheme(hWnd, L"DarkMode_CFD", NULL);
	} else if (_wcsicmp(str, TOOLBARCLASSNAME) == 0) {
		HWND hTips = (HWND)SendMessage(hWnd, TB_GETTOOLTIPS, 0, 0);
		if (hTips != NULL) {
			SetDarkTheme(hTips);
		}
	} else if (_wcsicmp(str, WC_EDIT) == 0) {
		const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
		const LONG_PTR ex_style = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
		if (((style & WS_VSCROLL) == WS_VSCROLL) || ((style & WS_HSCROLL) == WS_HSCROLL)) {
			SetWindowLongPtr(hWnd, GWL_STYLE, style | WS_BORDER);
			SetWindowLongPtr(hWnd, GWL_EXSTYLE, ex_style & ~WS_EX_CLIENTEDGE);
			SetDarkTheme(hWnd);
		} else {
			SetWindowTheme(hWnd, L"DarkMode_CFD", NULL);
		}
	} else if (_wcsicmp(str, RICHEDIT_CLASS) == 0) {
		const LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
		const LONG_PTR ex_style = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
		const BOOL has_static_edge = (ex_style & WS_EX_STATICEDGE) == WS_EX_STATICEDGE;
		CHARFORMATW cf = { 0 };
		cf.cbSize = sizeof(CHARFORMATW);
		cf.dwMask = CFM_COLOR;
		cf.crTextColor = DARKMODE_NORMAL_TEXT_COLOR;
		SendMessage(hWnd, EM_SETBKGNDCOLOR, 0, (LPARAM)(has_static_edge ?
			DARKMODE_NORMAL_CONTROL_BACKGROUND_COLOR : DARKMODE_NORMAL_DIALOG_BACKGROUND_COLOR));
		SendMessage(hWnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
		SetWindowLongPtr(hWnd, GWL_STYLE, style | WS_BORDER);
		SetWindowLongPtr(hWnd, GWL_EXSTYLE, ex_style & ~WS_EX_STATICEDGE);
		SetWindowTheme(hWnd, NULL, L"DarkMode_Explorer::ScrollBar");
	}

	return TRUE;
}

void SetDarkModeForChild(HWND hParent)
{
	if (is_darkmode_enabled)
		EnumChildWindows(hParent, DarkModeForChildCallback, (LPARAM)NULL);
}
