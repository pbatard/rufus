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

#include "darkmode.h"
#include "rufus.h"
#include <dwmapi.h>
#include <math.h>
#include <richedit.h>
#include <Shlwapi.h>
#include <Uxtheme.h>
#include <VersionHelpers.h>
#include <vssym32.h>


#define WIN10_22H2 19045 // Windows 10 22H2 (Last)
#define WIN11_21H2 22000 // Windows 11 first "stable" build

#define TOOLBAR_ICON_COLOR      RGB(0x29, 0x80, 0xB9) // from "ui.h"
#define TOOLBAR_ICON_COLOR_GOLD RGB(0xFF, 0xD7, 0x00) // gold
static COLORREF cAccent = TOOLBAR_ICON_COLOR;

typedef enum _PreferredAppMode {
	Default,
	AllowDark,
	ForceDark,
	ForceLight,
	Max
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


PF_TYPE_DECL(WINAPI, BOOL, AllowDarkModeForWindow, (HWND, BOOL));
PF_TYPE_DECL(WINAPI, PreferredAppMode, SetPreferredAppMode, (PreferredAppMode));
PF_TYPE_DECL(WINAPI, VOID, FlushMenuThemes, (VOID));
PF_TYPE_DECL(WINAPI, BOOL, SetWindowCompositionAttribute, (HWND, WINDOWCOMPOSITIONATTRIBDATA*));

static BOOL isDarkEnabled = FALSE;

static BOOL IsAtLeastWin10Build(DWORD buildNumber)
{
	if (!IsWindows10OrGreater()) {
		return FALSE;
	}

	const ULONGLONG mask = VerSetConditionMask(0, VER_BUILDNUMBER, VER_GREATER_EQUAL);

	OSVERSIONINFOEXW osvi;
	ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW));
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwBuildNumber = buildNumber;
	return VerifyVersionInfoW(&osvi, VER_BUILDNUMBER, mask);
}

static BOOL IsAtLeastWin10(void)
{
	return IsAtLeastWin10Build(WIN10_22H2);
}

static BOOL IsAtLeastWin11(void)
{
	return IsAtLeastWin10Build(WIN11_21H2);
}

static BOOL IsHighContrast(void)
{
	HIGHCONTRASTW highContrast;
	ZeroMemory(&highContrast, sizeof(HIGHCONTRASTW));
	highContrast.cbSize = sizeof(HIGHCONTRASTW);
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(HIGHCONTRASTW), &highContrast, FALSE)) {
		return (highContrast.dwFlags & HCF_HIGHCONTRASTON) == HCF_HIGHCONTRASTON;
	}
	return FALSE;
}

BOOL IsDarkModeEnabled(void)
{
	return isDarkEnabled;
}

BOOL GetDarkModeFromReg(void)
{
	DWORD data = 0;
	DWORD dwBufSize = sizeof(data);
	LPCWSTR lpSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	LPCWSTR lpValue = L"AppsUseLightTheme";

	LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, lpSubKey, lpValue, RRF_RT_REG_DWORD, NULL, &data, &dwBufSize);
	if (result != ERROR_SUCCESS) {
		isDarkEnabled = FALSE;
		return isDarkEnabled;
	}

	// dark mode is 0, light mode is 1
	isDarkEnabled = (data == 0) ? TRUE : FALSE;
	return isDarkEnabled;
}


void InitDarkMode(HWND hWnd)
{
	if (!IsAtLeastWin10() || IsHighContrast()) {
		isDarkEnabled = FALSE;
		return;
	}

	BOOL enableDark = IsDarkModeEnabled();

	PF_INIT_ID_OR_OUT(AllowDarkModeForWindow, UxTheme, 133);
	PF_INIT_ID_OR_OUT(SetPreferredAppMode, UxTheme, 135);
	PF_INIT_ID_OR_OUT(FlushMenuThemes, UxTheme, 136);

	pfAllowDarkModeForWindow(hWnd, enableDark);
	PreferredAppMode appMode = enableDark ? ForceDark : ForceLight;
	pfSetPreferredAppMode(appMode);
	pfFlushMenuThemes();
	return;

out:
	isDarkEnabled = FALSE;
}

void SetDarkTitleBar(HWND hWnd)
{
	BOOL enableDark = IsDarkModeEnabled();

	if (IsAtLeastWin11()) {
		DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enableDark, sizeof(enableDark));
	}
	else if (IsAtLeastWin10()) {
		PF_INIT_OR_OUT(SetWindowCompositionAttribute, user32);

		WINDOWCOMPOSITIONATTRIBDATA data = { WCA_USEDARKMODECOLORS, &enableDark, sizeof(enableDark) };
		pfSetWindowCompositionAttribute(hWnd, &data);
	}
	else {
out:
		isDarkEnabled = FALSE;
	}
}

void SetDarkTheme(HWND hWnd)
{
	SetWindowTheme(hWnd, IsDarkModeEnabled() ? L"DarkMode_Explorer" : NULL, NULL);
}

/*
 * Accent color functions
 */

// adapted from https://stackoverflow.com/a/56678483
static double LinearValue(double colorChannel)
{
	colorChannel /= 255.0;
	if (colorChannel <= 0.04045) {
		return colorChannel / 12.92;
	}
	return pow((colorChannel + 0.055) / 1.055, 2.4);
}

static double CalculatePerceivedLightness(COLORREF c)
{
	double r = LinearValue((double)GetRValue(c));
	double g = LinearValue((double)GetGValue(c));
	double b = LinearValue((double)GetBValue(c));

	double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;

	double lightness = (luminance <= 216.0 / 24389.0) ?
		(luminance * 24389.0 / 27.0) :
		(pow(luminance, 1.0 / 3.0) * 116.0 - 16.0);

	return lightness;
}

BOOL InitAccentColor(void)
{
	BOOL opaque = TRUE;
	if (SUCCEEDED(DwmGetColorizationColor(&cAccent, &opaque))) {
		cAccent = RGB(GetBValue(cAccent), GetGValue(cAccent), GetRValue(cAccent));
		// check if accent color is too dark
		static double lightnessTreshold = 50.0 - 4.0;
		if (CalculatePerceivedLightness(cAccent) < lightnessTreshold) {
			cAccent = TOOLBAR_ICON_COLOR_GOLD;
			return FALSE;
		}
		return TRUE;
	}
	cAccent = TOOLBAR_ICON_COLOR;
	return FALSE;
}

static COLORREF GetAccentColor(void)
{
	return cAccent;
}

// Rufus has only icons with one color, so changing color is easy
BOOL ChangeIconColor(HICON* hIcon, COLORREF newColor) {
	HDC hdcScreen, hdcBitmap = NULL;
	HBITMAP hbm = NULL;
	BITMAP bm;
	ZeroMemory(&bm, sizeof(BITMAP));
	ICONINFO ii;
	HICON hIconNew = NULL;
	RGBQUAD* pixels = NULL;

	if (!*hIcon || !IsDarkModeEnabled()) {
		return FALSE;
	}

	if (newColor == 0) {
		newColor = GetAccentColor();
	}

	hdcBitmap = CreateCompatibleDC(NULL);
	hdcScreen = GetDC(NULL);
	if (hdcScreen) {
		if (hdcBitmap) {
			if (GetIconInfo(*hIcon, &ii) && GetObject(ii.hbmColor, sizeof(BITMAP), &bm)) {
				BITMAPINFO bmi = { 0 };
				bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmi.bmiHeader.biWidth = bm.bmWidth;
				bmi.bmiHeader.biHeight = -bm.bmHeight;
				bmi.bmiHeader.biPlanes = 1;
				bmi.bmiHeader.biBitCount = 32;
				bmi.bmiHeader.biCompression = BI_RGB;

				pixels = (RGBQUAD*)malloc((size_t)bm.bmWidth * bm.bmHeight * sizeof(RGBQUAD));
				if (pixels && GetDIBits(hdcBitmap, ii.hbmColor, 0, bm.bmHeight, pixels, &bmi, DIB_RGB_COLORS)) {
					for (int i = 0; i < bm.bmWidth * bm.bmHeight; i++) {
						if (pixels[i].rgbReserved != 0) {
							pixels[i].rgbRed = GetRValue(newColor);
							pixels[i].rgbGreen = GetGValue(newColor);
							pixels[i].rgbBlue = GetBValue(newColor);
						}
					}

					hbm = CreateCompatibleBitmap(hdcScreen, bm.bmWidth, bm.bmHeight);
					if (hbm) {
						SetDIBits(hdcBitmap, hbm, 0, bm.bmHeight, pixels, &bmi, DIB_RGB_COLORS);

						if (ii.hbmColor) {
							DeleteObject(ii.hbmColor);
							ii.hbmColor = NULL;
						}

						ii.hbmColor = hbm;
						hIconNew = CreateIconIndirect(&ii);
					}
					else {
						free(pixels);
						pixels = NULL;
						DeleteObject(ii.hbmColor);
						DeleteObject(ii.hbmMask);
						DeleteDC(hdcBitmap);
						ReleaseDC(NULL, hdcScreen);
						return FALSE;
					}
				}

				free(pixels);
				pixels = NULL;
				if (hbm) DeleteObject(hbm);
				DeleteObject(ii.hbmColor);
				DeleteObject(ii.hbmMask);
			}
		}
		ReleaseDC(NULL, hdcScreen);
	}

	if (hdcBitmap) {
		DeleteDC(hdcBitmap);
	}

	if (!hIconNew) {
		return FALSE;
	}
	DestroyIcon(*hIcon);
	*hIcon = hIconNew;

	return TRUE;
}

/*
 * Dark mode custom colors
 */

static HBRUSH hbrBackground = NULL;
static HBRUSH hbrBackgroundControl = NULL;
static HBRUSH hbrBackgroundHot = NULL;
static HBRUSH hbrEdge = NULL;
static HPEN hpnEdge = NULL;
static HPEN hpnEdgeHot = NULL;

COLORREF GetTextColorForDarkMode(void)
{
	return 0xE0E0E0;
}

COLORREF GetDisabledTextColor(void)
{
	return 0x808080;
}

static COLORREF GetBackgroundColor(void)
{
	return 0x202020;
}

COLORREF GetControlBackgroundColor(void)
{
	return 0x383838;
}

static COLORREF GetHotBackgroundColor(void)
{
	return 0x454545;
}

COLORREF GetEdgeColor(void)
{
	return 0x646464;
}

static COLORREF GetHotEdgeColor(void)
{
	return 0x9B9B9B;
}

static HBRUSH GetBackgroundBrush(void)
{
	if (hbrBackground == NULL) {
		hbrBackground = CreateSolidBrush(GetBackgroundColor());
	}
	return hbrBackground;
}

static HBRUSH GetControlBackgroundBrush(void)
{
	if (hbrBackgroundControl == NULL) {
		hbrBackgroundControl = CreateSolidBrush(GetControlBackgroundColor());
	}
	return hbrBackgroundControl;
}

static HBRUSH GetHotBackgroundBrush(void)
{
	if (hbrBackgroundHot == NULL) {
		hbrBackgroundHot = CreateSolidBrush(GetHotBackgroundColor());
	}
	return hbrBackgroundHot;
}

static HBRUSH GetEdgeBrush(void)
{
	if (hbrEdge == NULL) {
		hbrEdge = CreateSolidBrush(GetEdgeColor());
	}
	return hbrEdge;
}

static HPEN GetEdgePen(void)
{
	if (hpnEdge == NULL) {
		hpnEdge = CreatePen(PS_SOLID, 1, GetEdgeColor());
	}
	return hpnEdge;
}
static HPEN GetHotEdgePen(void)
{
	if (hpnEdgeHot == NULL) {
		hpnEdgeHot = CreatePen(PS_SOLID, 1, GetHotEdgeColor());
	}
	return hpnEdgeHot;
}

void DestroyDarkModeGDIObjects(void)
{
	safe_delete_object(hbrBackground);
	safe_delete_object(hbrBackgroundControl);
	safe_delete_object(hbrBackgroundHot);
	safe_delete_object(hbrEdge);
	safe_delete_object(hpnEdge);
	safe_delete_object(hpnEdgeHot);
}

/*
 * Button section, checkbox, radio, and groupbox style buttons
 */
static void PaintRoundFrameRect(HDC hdc, const RECT rect, const HPEN hpen, int width, int height)
{
	HBRUSH holdBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
	HPEN holdPen = (HPEN)SelectObject(hdc, hpen);
	RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, width, height);
	SelectObject(hdc, holdBrush);
	SelectObject(hdc, holdPen);
}

typedef struct _ButtonData {
	HTHEME hTheme;
	int iStateID;
} ButtonData;

const UINT_PTR g_buttonSubclassID = 42;
const UINT_PTR g_groupboxSubclassID = 42;

static void RenderButton(HWND hWnd, HDC hdc, HTHEME hTheme, int iPartID, int iStateID)
{
	RECT rcClient = { 0 };
	wchar_t szText[256] = { '\0' };
	DWORD nState = (DWORD)SendMessage(hWnd, BM_GETSTATE, 0, 0);
	DWORD uiState = (DWORD)SendMessage(hWnd, WM_QUERYUISTATE, 0, 0);
	LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);

	HFONT hFont = NULL;
	HFONT hOldFont = NULL;
	HFONT hCreatedFont = NULL;
	LOGFONT lf;
	if (SUCCEEDED(GetThemeFont(hTheme, hdc, iPartID, iStateID, TMT_FONT, &lf))) {
		hCreatedFont = CreateFontIndirect(&lf);
		hFont = hCreatedFont;
	}

	if (!hFont) {
		hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
	}

	hOldFont = (HFONT)SelectObject(hdc, hFont);

	DWORD dtFlags = DT_LEFT; // DT_LEFT is 0
	dtFlags |= (nStyle & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
	dtFlags |= ((nStyle & BS_CENTER) == BS_CENTER) ? DT_CENTER : (nStyle & BS_RIGHT) ? DT_RIGHT : 0;
	dtFlags |= ((nStyle & BS_VCENTER) == BS_VCENTER) ? DT_VCENTER : (nStyle & BS_BOTTOM) ? DT_BOTTOM : 0;
	dtFlags |= (uiState & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0;

	if (!(nStyle & BS_MULTILINE) && !(nStyle & BS_BOTTOM) && !(nStyle & BS_TOP)) {
		dtFlags |= DT_VCENTER;
	}

	GetClientRect(hWnd, &rcClient);
	GetWindowText(hWnd, szText, _countof(szText));

	SIZE szBox = { 13, 13 };
	GetThemePartSize(hTheme, hdc, iPartID, iStateID, NULL, TS_DRAW, &szBox);

	RECT rcText = rcClient;
	GetThemeBackgroundContentRect(hTheme, hdc, iPartID, iStateID, &rcClient, &rcText);

	RECT rcBackground = rcClient;
	if ((dtFlags & DT_SINGLELINE) == DT_SINGLELINE) {
		rcBackground.top += (rcText.bottom - rcText.top - szBox.cy) / 2;
	}
	rcBackground.bottom = rcBackground.top + szBox.cy;
	rcBackground.right = rcBackground.left + szBox.cx;
	rcText.left = rcBackground.right + 3;

	DrawThemeParentBackground(hWnd, hdc, &rcClient);
	DrawThemeBackground(hTheme, hdc, iPartID, iStateID, &rcBackground, NULL);

	DTTOPTS dtto;
	ZeroMemory(&dtto, sizeof(DTTOPTS));
	dtto.dwSize = sizeof(DTTOPTS);
	dtto.dwFlags = DTT_TEXTCOLOR;
	dtto.crText = ((nStyle & WS_DISABLED) == WS_DISABLED) ? GetDisabledTextColor() : GetTextColorForDarkMode();

	DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, szText, -1, dtFlags, &rcText, &dtto);

	if (((nState & BST_FOCUS) == BST_FOCUS) && ((uiState & UISF_HIDEFOCUS) != UISF_HIDEFOCUS)) {
		RECT rcTextOut = rcText;
		dtto.dwFlags |= DTT_CALCRECT;
		DrawThemeTextEx(hTheme, hdc, iPartID, iStateID, szText, -1, dtFlags | DT_CALCRECT, &rcTextOut, &dtto);
		RECT rcFocus = rcTextOut;
		rcFocus.bottom++;
		rcFocus.left--;
		rcFocus.right++;
		DrawFocusRect(hdc, &rcFocus);
	}

	if (hCreatedFont) {
		DeleteObject(hCreatedFont);
	}
	SelectObject(hdc, hOldFont);
}

static void PaintButton(HWND hWnd, HDC hdc, ButtonData buttonData)
{
	DWORD nState = (DWORD)SendMessage(hWnd, BM_GETSTATE, 0, 0);
	const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	const LONG_PTR nButtonStyle = nStyle & BS_TYPEMASK;

	int iPartID = BP_CHECKBOX;

	if (nButtonStyle == BS_CHECKBOX ||
		nButtonStyle == BS_AUTOCHECKBOX ||
		nButtonStyle == BS_3STATE ||
		nButtonStyle == BS_AUTO3STATE) {
		iPartID = BP_CHECKBOX;
	}
	else if (nButtonStyle == BS_RADIOBUTTON ||
		nButtonStyle == BS_AUTORADIOBUTTON) {
		iPartID = BP_RADIOBUTTON;
	}

	// states of BP_CHECKBOX and BP_RADIOBUTTON are the same
	int iStateID = RBS_UNCHECKEDNORMAL;

	if (nStyle & WS_DISABLED)		iStateID = RBS_UNCHECKEDDISABLED;
	else if (nState & BST_PUSHED)	iStateID = RBS_UNCHECKEDPRESSED;
	else if (nState & BST_HOT)		iStateID = RBS_UNCHECKEDHOT;

	if (nState & BST_CHECKED)		iStateID += 4;

	if (BufferedPaintRenderAnimation(hWnd, hdc)) {
		return;
	}

	BP_ANIMATIONPARAMS animParams;
	ZeroMemory(&animParams, sizeof(BP_ANIMATIONPARAMS));
	animParams.cbSize = sizeof(BP_ANIMATIONPARAMS);
	animParams.style = BPAS_LINEAR;
	if (iStateID != buttonData.iStateID) {
		GetThemeTransitionDuration(buttonData.hTheme, iPartID, buttonData.iStateID, iStateID, TMT_TRANSITIONDURATIONS, &animParams.dwDuration);
	}

	RECT rcClient = { 0 };
	GetClientRect(hWnd, &rcClient);

	HDC hdcFrom = NULL;
	HDC hdcTo = NULL;
	HANIMATIONBUFFER hbpAnimation = BeginBufferedAnimation(hWnd, hdc, &rcClient, BPBF_COMPATIBLEBITMAP, NULL, &animParams, &hdcFrom, &hdcTo);
	if (hbpAnimation) {
		if (hdcFrom) {
			RenderButton(hWnd, hdcFrom, buttonData.hTheme, iPartID, buttonData.iStateID);
		}
		if (hdcTo) {
			RenderButton(hWnd, hdcTo, buttonData.hTheme, iPartID, iStateID);
		}

		buttonData.iStateID = iStateID;

		EndBufferedAnimation(hbpAnimation, TRUE);
	}
	else {
		RenderButton(hWnd, hdc, buttonData.hTheme, iPartID, iStateID);

		buttonData.iStateID = iStateID;
	}
}

static LRESULT CALLBACK ButtonSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	ButtonData* pButtonData = (ButtonData*)dwRefData;

	switch (uMsg)
	{
	case WM_UPDATEUISTATE:
	{
		if (HIWORD(wParam) & (UISF_HIDEACCEL | UISF_HIDEFOCUS)) {
			InvalidateRect(hWnd, NULL, FALSE);
		}
		break;
	}

	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, ButtonSubclass, uIdSubclass);
		if (pButtonData->hTheme) {
			CloseThemeData(pButtonData->hTheme);
		}
		free(pButtonData);
		break;
	}

	case WM_ERASEBKGND:
	{
		if (pButtonData->hTheme == NULL) {
			pButtonData->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
		}

		if (pButtonData->hTheme) {
			return TRUE;
		}
		break;
	}

	case WM_THEMECHANGED:
	{
		if (pButtonData->hTheme) {
			CloseThemeData(pButtonData->hTheme);
		}
		break;
	}

	case WM_PRINTCLIENT:
	case WM_PAINT:
	{
		if (pButtonData->hTheme == NULL) {
			pButtonData->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
			if (pButtonData->hTheme == NULL) {
				break;
			}
		}
		PAINTSTRUCT ps;
		ZeroMemory(&ps, sizeof(PAINTSTRUCT));
		HDC hdc = (HDC)wParam;
		if (!hdc) {
			hdc = BeginPaint(hWnd, &ps);
		}

		PaintButton(hWnd, hdc, *pButtonData);

		if (ps.hdc) {
			EndPaint(hWnd, &ps);
		}

		return 0;
	}

	case WM_SIZE:
	case WM_DESTROY:
	{
		BufferedPaintStopAllAnimations(hWnd);
		break;
	}

	case WM_ENABLE:
	{
		// skip the button's normal wndproc so it won't redraw out of wm_paint
		LRESULT lr = DefWindowProc(hWnd, uMsg, wParam, lParam);
		InvalidateRect(hWnd, NULL, FALSE);
		return lr;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassButtonControl(HWND hWnd)
{
	if (GetWindowSubclass(hWnd, ButtonSubclass, g_buttonSubclassID, NULL) == FALSE) {
		DWORD_PTR pButtonData = (DWORD_PTR)calloc(0, sizeof(ButtonData));
		SetWindowSubclass(hWnd, ButtonSubclass, g_buttonSubclassID, pButtonData);
	}
}

static void PaintGroupbox(HWND hWnd, HDC hdc, ButtonData buttonData)
{
	const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	BOOL isDisabled = (nStyle & WS_DISABLED) == WS_DISABLED;
	int iPartID = BP_GROUPBOX;
	int iStateID = isDisabled ? GBS_DISABLED : GBS_NORMAL;

	RECT rcClient = { 0 };
	GetClientRect(hWnd, &rcClient);

	rcClient.bottom -= 1;

	RECT rcText = rcClient;
	RECT rcBackground = rcClient;

	HFONT hFont = NULL;
	HFONT hOldFont = NULL;
	HFONT hCreatedFont = NULL;
	LOGFONT lf;
	if (SUCCEEDED(GetThemeFont(buttonData.hTheme, hdc, iPartID, iStateID, TMT_FONT, &lf))) {
		hCreatedFont = CreateFontIndirect(&lf);
		hFont = hCreatedFont;
	}

	if (!hFont) {
		hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
	}

	hOldFont = (HFONT)SelectObject(hdc, hFont);

	wchar_t szText[256] = { '\0' };
	GetWindowText(hWnd, szText, _countof(szText));

	BOOL isCenter = (nStyle & BS_CENTER) == BS_CENTER;

	if (szText[0]) {
		SIZE textSize = { 0 };
		GetTextExtentPoint32(hdc, szText, (int)wcslen(szText), &textSize);

		int centerPosX = isCenter ? ((rcClient.right - rcClient.left - textSize.cx) / 2) : 7;

		rcBackground.top += textSize.cy / 2;
		rcText.left += centerPosX;
		rcText.bottom = rcText.top + textSize.cy;
		rcText.right = rcText.left + textSize.cx + 4;

		ExcludeClipRect(hdc, rcText.left, rcText.top, rcText.right, rcText.bottom);
	}
	else {
		SIZE textSize = { 0 };
		GetTextExtentPoint32(hdc, L"M", 1, &textSize);
		rcBackground.top += textSize.cy / 2;
	}

	RECT rcContent = rcBackground;
	GetThemeBackgroundContentRect(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, &rcBackground, &rcContent);
	ExcludeClipRect(hdc, rcContent.left, rcContent.top, rcContent.right, rcContent.bottom);

	PaintRoundFrameRect(hdc, rcBackground, GetEdgePen(), 0, 0);

	SelectClipRgn(hdc, NULL);

	if (szText[0]) {
		rcText.right -= 2;
		rcText.left += 2;

		DTTOPTS dtto;
		ZeroMemory(&dtto, sizeof(DTTOPTS));
		dtto.dwSize = sizeof(DTTOPTS);
		dtto.dwFlags = DTT_TEXTCOLOR;
		dtto.crText = isDisabled ? GetDisabledTextColor() : GetTextColorForDarkMode();

		DWORD textFlags = isCenter ? DT_CENTER : DT_LEFT;

		if (SendMessage(hWnd, WM_QUERYUISTATE, 0, 0) != (LRESULT)NULL) {
			textFlags |= DT_HIDEPREFIX;
		}

		DrawThemeTextEx(buttonData.hTheme, hdc, BP_GROUPBOX, iStateID, szText, -1, textFlags | DT_SINGLELINE, &rcText, &dtto);
	}

	if (hCreatedFont) DeleteObject(hCreatedFont);
	SelectObject(hdc, hOldFont);
}

static LRESULT CALLBACK GroupboxSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	ButtonData* pButtonData = (ButtonData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, ButtonSubclass, uIdSubclass);
		if (pButtonData->hTheme) {
			CloseThemeData(pButtonData->hTheme);
		}
		free(pButtonData);
		break;
	}

	case WM_ERASEBKGND:
	{
		if (pButtonData->hTheme == NULL) {
			pButtonData->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
		}

		if (pButtonData->hTheme) {
			return TRUE;
		}
		break;
	}

	case WM_THEMECHANGED:
	{
		if (pButtonData->hTheme) {
			CloseThemeData(pButtonData->hTheme);
		}
		break;
	}

	case WM_PRINTCLIENT:
	case WM_PAINT:
	{
		if (pButtonData->hTheme == NULL) {
			pButtonData->hTheme = OpenThemeData(hWnd, VSCLASS_BUTTON);
			if (pButtonData->hTheme == NULL) {
				break;
			}
		}
		PAINTSTRUCT ps;
		ZeroMemory(&ps, sizeof(PAINTSTRUCT));
		HDC hdc = (HDC)wParam;
		if (!hdc) {
			hdc = BeginPaint(hWnd, &ps);
		}

		PaintGroupbox(hWnd, hdc, *pButtonData);

		if (ps.hdc) {
			EndPaint(hWnd, &ps);
		}

		return 0;
	}

	case WM_ENABLE:
	{
		RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassGroupboxControl(HWND hWnd)
{
	if (GetWindowSubclass(hWnd, GroupboxSubclass, g_groupboxSubclassID, NULL) == FALSE) {
		DWORD_PTR pButtonData = (DWORD_PTR)calloc(1, sizeof(ButtonData));
		SetWindowSubclass(hWnd, GroupboxSubclass, g_groupboxSubclassID, pButtonData);
	}
}

/*
 * Toolbar section
 */

static UINT_PTR g_windowNotifySubclassID = 42;

static LRESULT DarkToolBarNotifyCustomDraw(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LPNMTBCUSTOMDRAW lpnmtbcd = (LPNMTBCUSTOMDRAW)lParam;
	static int roundCornerValue = 0;

	switch (lpnmtbcd->nmcd.dwDrawStage) {
	case CDDS_PREPAINT:
	{
		if (IsAtLeastWin11()) {
			roundCornerValue = 5;
		}

		FillRect(lpnmtbcd->nmcd.hdc, &lpnmtbcd->nmcd.rc, GetBackgroundBrush());
		return CDRF_NOTIFYITEMDRAW;
	}

	case CDDS_ITEMPREPAINT:
	{
		lpnmtbcd->hbrMonoDither = GetBackgroundBrush();
		lpnmtbcd->hbrLines = GetEdgeBrush();
		lpnmtbcd->hpenLines = GetEdgePen();
		lpnmtbcd->clrText = GetTextColorForDarkMode();
		lpnmtbcd->clrTextHighlight = GetTextColorForDarkMode();
		lpnmtbcd->clrBtnFace = GetBackgroundColor();
		lpnmtbcd->clrBtnHighlight = GetControlBackgroundColor();
		lpnmtbcd->clrHighlightHotTrack = GetHotBackgroundColor();
		lpnmtbcd->nStringBkMode = TRANSPARENT;
		lpnmtbcd->nHLStringBkMode = TRANSPARENT;

		if ((lpnmtbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT) {
			HBRUSH holdBrush = (HBRUSH)SelectObject(lpnmtbcd->nmcd.hdc, GetHotBackgroundBrush());
			HPEN holdPen = (HPEN)SelectObject(lpnmtbcd->nmcd.hdc, GetHotEdgePen());
			RoundRect(lpnmtbcd->nmcd.hdc, lpnmtbcd->nmcd.rc.left, lpnmtbcd->nmcd.rc.top, lpnmtbcd->nmcd.rc.right, lpnmtbcd->nmcd.rc.bottom, roundCornerValue, roundCornerValue);
			SelectObject(lpnmtbcd->nmcd.hdc, holdBrush);
			SelectObject(lpnmtbcd->nmcd.hdc, holdPen);

			lpnmtbcd->nmcd.uItemState &= ~(CDIS_CHECKED | CDIS_HOT);
		}
		else if ((lpnmtbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED) {
			HBRUSH holdBrush = (HBRUSH)SelectObject(lpnmtbcd->nmcd.hdc, GetControlBackgroundBrush());
			HPEN holdPen = (HPEN)SelectObject(lpnmtbcd->nmcd.hdc, GetEdgePen());
			RoundRect(lpnmtbcd->nmcd.hdc, lpnmtbcd->nmcd.rc.left, lpnmtbcd->nmcd.rc.top, lpnmtbcd->nmcd.rc.right, lpnmtbcd->nmcd.rc.bottom, roundCornerValue, roundCornerValue);
			SelectObject(lpnmtbcd->nmcd.hdc, holdBrush);
			SelectObject(lpnmtbcd->nmcd.hdc, holdPen);

			lpnmtbcd->nmcd.uItemState &= ~CDIS_CHECKED;
		}

		LRESULT lr = TBCDRF_USECDCOLORS;
		if ((lpnmtbcd->nmcd.uItemState & CDIS_SELECTED) == CDIS_SELECTED) {
			lr |= TBCDRF_NOBACKGROUND;
		}
		return lr;
	}

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
	{
		return CDRF_NOTIFYITEMDRAW;
	}

	case CDDS_ITEMPREPAINT:
	{
		switch (lpnmcd->dwItemSpec) {
		case TBCD_THUMB:
		{
			if ((lpnmcd->uItemState & CDIS_SELECTED) == CDIS_SELECTED) {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetControlBackgroundBrush());
				return CDRF_SKIPDEFAULT;
			}
			break;
		}

		case TBCD_CHANNEL:
		{
			if (IsWindowEnabled(lpnmcd->hdr.hwndFrom) == FALSE) {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetBackgroundBrush());
				PaintRoundFrameRect(lpnmcd->hdc, lpnmcd->rc, GetEdgePen(), 0, 0);
			}
			else {
				FillRect(lpnmcd->hdc, &lpnmcd->rc, GetControlBackgroundBrush());
			}
			return CDRF_SKIPDEFAULT;
		}

		default:
			break;
		}
		break;
	}

	default:
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK WindowNotifySubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	(void)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, WindowNotifySubclass, uIdSubclass);
		break;
	}

	case WM_NOTIFY:
	{
		LPNMHDR nmhdr = (LPNMHDR)lParam;
		WCHAR str[32] = { 0 };
		GetClassName(nmhdr->hwndFrom, str, ARRAYSIZE(str));

		switch (nmhdr->code) {
		case NM_CUSTOMDRAW:
		{
			if (_wcsicmp(str, TOOLBARCLASSNAME) == 0) {
				return DarkToolBarNotifyCustomDraw(hWnd, uMsg, wParam, lParam);
			}
			else if (_wcsicmp(str, TRACKBAR_CLASS) == 0) {
				return DarkTrackBarNotifyCustomDraw(hWnd, uMsg, wParam, lParam);
			}
		}
		break;
		}
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassNotifyCustomDraw(HWND hWnd)
{
	if (GetWindowSubclass(hWnd, WindowNotifySubclass, g_windowNotifySubclassID, NULL) == FALSE) {
		SetWindowSubclass(hWnd, WindowNotifySubclass, g_windowNotifySubclassID, 0);
	}
}

/*
 * Status bar section
 */

typedef struct _StatusBarData {
	HFONT hFont;
} StatusBarData;

const UINT_PTR g_statusBarSubclassID = 42;

static void PaintStatusBar(HWND hWnd, HDC hdc, StatusBarData statusBarData)
{
	struct {
		int horizontal;
		int vertical;
		int between;
	} borders;
	ZeroMemory(&borders, sizeof(borders));
	SendMessage(hWnd, SB_GETBORDERS, 0, (LPARAM)&borders);

	RECT rcClient = { 0 };
	GetClientRect(hWnd, &rcClient);

	HPEN holdPen = (HPEN)SelectObject(hdc, GetEdgePen());
	HFONT holdFont = (HFONT)SelectObject(hdc, statusBarData.hFont);

	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, GetTextColorForDarkMode());

	FillRect(hdc, &rcClient, GetBackgroundBrush());

	int nParts = (int)SendMessage(hWnd, SB_GETPARTS, 0, 0);
	RECT rcPart = { 0 };
	RECT rcIntersect = { 0 };
	for (int i = 0; i < nParts; ++i) {
		SendMessage(hWnd, SB_GETRECT, i, (LPARAM)&rcPart);
		if (IntersectRect(&rcIntersect, &rcPart, &rcClient) == FALSE) {
			continue;
		}

		if (i < nParts - 1) {
			POINT edges[] = {
				{rcPart.right - borders.between, rcPart.top + 1},
				{rcPart.right - borders.between, rcPart.bottom - 3}
			};
			Polyline(hdc, edges, _countof(edges));
		}

		DWORD cchText = LOWORD(SendMessage(hWnd, SB_GETTEXTLENGTH, i, 0));
		LPWSTR buffer = (LPWSTR)calloc(cchText, sizeof(WCHAR));
		LRESULT lr = SendMessage(hWnd, SB_GETTEXT, i, (LPARAM)buffer);
		BOOL ownerDraw = FALSE;
		if (cchText == 0 && (lr & ~(SBT_NOBORDERS | SBT_POPOUT | SBT_RTLREADING)) != 0) {
			ownerDraw = TRUE;
		}

		rcPart.left += borders.between;
		rcPart.right -= borders.vertical;

		if (ownerDraw) {
			UINT id = GetDlgCtrlID(hWnd);
			DRAWITEMSTRUCT dis = {
				0
				, 0
				, (UINT)i
				, ODA_DRAWENTIRE
				, id
				, hWnd
				, hdc
				, rcPart
				, (ULONG_PTR)lr
			};

			SendMessage(GetParent(hWnd), WM_DRAWITEM, id, (LPARAM)&dis);
		}
		else {
			if (buffer) {
				DrawText(hdc, buffer, (int)cchText, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
			}
		}
		free(buffer);
	}

	POINT edgeHor[] = {
		{rcClient.left, rcClient.top},
		{rcClient.right, rcClient.top}
	};
	Polyline(hdc, edgeHor, _countof(edgeHor));

	SelectObject(hdc, holdFont);
	SelectObject(hdc, holdPen);
}

static LRESULT CALLBACK StatusBarSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData)
{
	StatusBarData* pStatusBarData = (StatusBarData*)dwRefData;

	switch (uMsg) {
	case WM_ERASEBKGND:
	{
		RECT rcClient = { 0 };
		GetClientRect(hWnd, &rcClient);
		FillRect((HDC)wParam, &rcClient, GetBackgroundBrush());
		return TRUE;
	}

	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		PaintStatusBar(hWnd, hdc, *pStatusBarData);

		EndPaint(hWnd, &ps);
		return 0;
	}

	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, StatusBarSubclass, uIdSubclass);
		if (pStatusBarData->hFont != NULL) {
			DeleteObject(pStatusBarData->hFont);
			pStatusBarData->hFont = NULL;
		}
		free(pStatusBarData);
		break;
	}

	case WM_THEMECHANGED:
	{
		if (pStatusBarData->hFont != NULL) {
			DeleteObject(pStatusBarData->hFont);
			pStatusBarData->hFont = NULL;
		}

		LOGFONT lf;
		NONCLIENTMETRICS ncm;
		ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
		ncm.cbSize = sizeof(NONCLIENTMETRICS);
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0) == TRUE) {
			lf = ncm.lfStatusFont;
			pStatusBarData->hFont = CreateFontIndirect(&lf);
		}

		if (uMsg != WM_THEMECHANGED) {
			return 0;
		}
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassStatusBar(HWND hWnd)
{
	if (IsDarkModeEnabled() &&
		GetWindowSubclass(hWnd, StatusBarSubclass, g_statusBarSubclassID, NULL) == FALSE)
	{
		StatusBarData* statusBarData = (StatusBarData*)malloc(sizeof(StatusBarData));
		if (statusBarData == NULL) {
			return; // something wrong happened
		}

		LOGFONT lf;
		NONCLIENTMETRICS ncm;
		ZeroMemory(&ncm, sizeof(NONCLIENTMETRICS));
		ncm.cbSize = sizeof(NONCLIENTMETRICS);
		if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0) == TRUE) {
			lf = ncm.lfStatusFont;
			statusBarData->hFont = CreateFontIndirect(&lf);
		}
		DWORD_PTR pStatusBarData = (DWORD_PTR)statusBarData;
		SetWindowSubclass(hWnd, StatusBarSubclass, g_statusBarSubclassID, pStatusBarData);
	}
}

/*
 * Progress bar section
 */

typedef struct _ProgressBarData {
	HTHEME hTheme;
	int iStateID;
} ProgressBarData;

const UINT_PTR g_progressBarSubclassID = 42;

static void GetProgressBarRects(HWND hWnd, RECT* rcEmpty, RECT* rcFilled)
{
	int pos = (int)SendMessage(hWnd, PBM_GETPOS, 0, 0);

	PBRANGE range = { 0, 0 };
	SendMessage(hWnd, PBM_GETRANGE, TRUE, (LPARAM)&range);
	int min = range.iLow;
	int max = range.iHigh;

	int currPos = pos - min;
	if (currPos != 0) {
		int totalWidth = rcEmpty->right - rcEmpty->left;
		rcFilled->left = rcEmpty->left;
		rcFilled->top = rcEmpty->top;
		rcFilled->bottom = rcEmpty->bottom;
		rcFilled->right = rcEmpty->left + (int)((double)(currPos) / (max - min) * totalWidth);

		rcEmpty->left = rcFilled->right; // to avoid painting under filled part
	}
}

static void PaintProgressBar(HWND hWnd, HDC hdc, ProgressBarData progressBarData)
{
	RECT rcClient = { 0 };
	GetClientRect(hWnd, &rcClient);

	PaintRoundFrameRect(hdc, rcClient, GetEdgePen(), 0, 0);

	InflateRect(&rcClient, -1, -1);
	rcClient.left = 1;

	RECT rcFill = { 0 };
	GetProgressBarRects(hWnd, &rcClient, &rcFill);
	DrawThemeBackground(progressBarData.hTheme, hdc, PP_FILL, progressBarData.iStateID, &rcFill, NULL);
	FillRect(hdc, &rcClient, GetControlBackgroundBrush());
}

static LRESULT CALLBACK ProgressBarSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	ProgressBarData* pProgressBarData = (ProgressBarData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, ProgressBarSubclass, uIdSubclass);
		if (pProgressBarData->hTheme) {
			CloseThemeData(pProgressBarData->hTheme);
		}
		free(pProgressBarData);
		break;
	}

	case WM_ERASEBKGND:
	{
		if (pProgressBarData->hTheme == NULL) {
			pProgressBarData->hTheme = OpenThemeData(hWnd, VSCLASS_PROGRESS);
		}

		if (pProgressBarData->hTheme) {
			return TRUE;
		}
		break;
	}

	case WM_THEMECHANGED:
	{
		if (pProgressBarData->hTheme) {
			CloseThemeData(pProgressBarData->hTheme);
		}
		break;
	}

	case PBM_SETSTATE:
	{
		switch (wParam) {
		case PBST_NORMAL:
			// PBFS_PARTIAL for cyan color
			pProgressBarData->iStateID = PBFS_NORMAL; // green
			break;
		case PBST_ERROR:
			pProgressBarData->iStateID = PBFS_ERROR; // red
			break;
		case PBST_PAUSED:
			pProgressBarData->iStateID = PBFS_PAUSED; // yellow
			break;
		}
		break;
	}

	case WM_PAINT:
	{
		if (pProgressBarData->hTheme == NULL) {
			pProgressBarData->hTheme = OpenThemeData(hWnd, VSCLASS_PROGRESS);
			if (pProgressBarData->hTheme == NULL) {
				break;
			}
		}

		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		PaintProgressBar(hWnd, hdc, *pProgressBarData);

		EndPaint(hWnd, &ps);

		return 0;

	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassProgressBarControl(HWND hWnd)
{
	if (IsDarkModeEnabled() &&
		GetWindowSubclass(hWnd, ProgressBarSubclass, g_progressBarSubclassID, NULL) == FALSE) {
		DWORD_PTR pProgressBarData = (DWORD_PTR)calloc(1, sizeof(ProgressBarData));
		SetWindowSubclass(hWnd, ProgressBarSubclass, g_progressBarSubclassID, pProgressBarData);
	}
}

/*
 * Static text section
 */

typedef struct _StaticTextData {
	BOOL isDisabled;
} StaticTextData;

const UINT_PTR g_staticTextSubclassID = 42;

static LRESULT CALLBACK StaticTextSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	StaticTextData* pStaticTextData = (StaticTextData*)dwRefData;

	switch (uMsg) {
	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, StaticTextSubclass, uIdSubclass);
		free(pStaticTextData);
		break;
	}

	case WM_ENABLE:
	{
		pStaticTextData->isDisabled = (wParam == FALSE);

		const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		if (pStaticTextData->isDisabled)
			SetWindowLongPtr(hWnd, GWL_STYLE, nStyle & ~WS_DISABLED);

		RECT rcClient = { 0 };
		GetClientRect(hWnd, &rcClient);
		MapWindowPoints(hWnd, GetParent(hWnd), (LPPOINT)&rcClient, 2);
		RedrawWindow(GetParent(hWnd), &rcClient, NULL, RDW_INVALIDATE | RDW_UPDATENOW);

		if (pStaticTextData->isDisabled)
			SetWindowLongPtr(hWnd, GWL_STYLE, nStyle | WS_DISABLED);

		return 0;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static void SubclassStaticText(HWND hWnd)
{
	if (GetWindowSubclass(hWnd, StaticTextSubclass, g_staticTextSubclassID, NULL) == FALSE) {
		DWORD_PTR pStaticTextData = (DWORD_PTR)calloc(1, sizeof(StaticTextData));
		SetWindowSubclass(hWnd, StaticTextSubclass, g_staticTextSubclassID, pStaticTextData);
	}
}

/*
 * Ctl color messages section
 */

const UINT_PTR g_WindowCtlColorSubclassID = 42;

static LRESULT OnCtlColor(HDC hdc)
{
	SetTextColor(hdc, GetTextColorForDarkMode());
	SetBkColor(hdc, GetBackgroundColor());
	return (LRESULT)GetBackgroundBrush();
}

static LRESULT OnCtlColorStatic(WPARAM wParam, LPARAM lParam)
{
	HDC hdc = (HDC)wParam;
	HWND hWnd = (HWND)lParam;
	WCHAR str[32] = { 0 };
	GetClassName(hWnd, str, ARRAYSIZE(str));

	if (_wcsicmp(str, WC_STATIC) == 0) {
		const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		if ((nStyle & SS_NOTIFY) == SS_NOTIFY) {
			DWORD_PTR dwRefData = 0;
			COLORREF cText = cAccent;
			if (GetWindowSubclass(hWnd, StaticTextSubclass, g_staticTextSubclassID, &dwRefData) == TRUE) {
				StaticTextData* pStaticTextData = (StaticTextData*)dwRefData;
				if (pStaticTextData->isDisabled)
					cText = GetDisabledTextColor();
			}
			SetTextColor(hdc, cText);
			SetBkColor(hdc, GetBackgroundColor());
			return (LRESULT)GetBackgroundBrush();
		}
	}
	// read-only WC_EDIT
	return OnCtlColor(hdc);
}

static LRESULT OnCtlColorControl(HDC hdc)
{
	SetTextColor(hdc, GetTextColorForDarkMode());
	SetBkColor(hdc, GetControlBackgroundColor());
	return (LRESULT)GetControlBackgroundBrush();
}


static INT_PTR OnCtlColorListbox(WPARAM wParam, LPARAM lParam)
{
	HDC hdc = (HDC)wParam;
	HWND hWnd = (HWND)lParam;

	const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
	BOOL isComboBox = (nStyle & LBS_COMBOBOX) == LBS_COMBOBOX;
	if ((!isComboBox || !isDarkEnabled) && IsWindowEnabled(hWnd)) {
		return (INT_PTR)OnCtlColorControl(hdc);
	}
	return (INT_PTR)OnCtlColor(hdc);
}

static LRESULT CALLBACK WindowCtlColorSubclass(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR uIdSubclass,
	DWORD_PTR dwRefData
)
{
	(void)dwRefData;

	switch (uMsg) {
	case WM_ERASEBKGND:
	{
		RECT rcClient = { 0 };
		GetClientRect(hWnd, &rcClient);
		FillRect((HDC)wParam, &rcClient, GetBackgroundBrush());
		return TRUE;
	}

	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, WindowCtlColorSubclass, uIdSubclass);
		break;
	}

	case WM_CTLCOLOREDIT:
	{
		return OnCtlColorControl((HDC)wParam);
	}

	case WM_CTLCOLORLISTBOX:
	{
		return OnCtlColorListbox(wParam, lParam);
	}

	case WM_CTLCOLORDLG:
	{
		return OnCtlColor((HDC)wParam);
	}

	case WM_CTLCOLORSTATIC:
	{
		return OnCtlColorStatic(wParam, lParam);
	}

	case WM_PRINTCLIENT:
	{
		return TRUE;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void SubclassCtlColor(HWND hWnd)
{
	if (GetWindowSubclass(hWnd, WindowCtlColorSubclass, g_WindowCtlColorSubclassID, NULL) == FALSE) {
		SetWindowSubclass(hWnd, WindowCtlColorSubclass, g_WindowCtlColorSubclassID, 0);
	}
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
		const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		if ((nStyle & SS_NOTIFY) == SS_NOTIFY) {
			SubclassStaticText(hWnd);
		}
	}
	else if (_wcsicmp(str, WC_BUTTON) == 0) {
		const LONG_PTR nButtonStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		switch (nButtonStyle & BS_TYPEMASK) {
		case BS_CHECKBOX:
		case BS_AUTOCHECKBOX:
		case BS_3STATE:
		case BS_AUTO3STATE:
		case BS_RADIOBUTTON:
		case BS_AUTORADIOBUTTON:
		{
			if (IsAtLeastWin11()) {
				SetDarkTheme(hWnd);
			}
			SubclassButtonControl(hWnd);
			break;
		}

		case BS_GROUPBOX:
		{
			SubclassGroupboxControl(hWnd);
			break;
		}

		case BS_PUSHBUTTON:
		case BS_DEFPUSHBUTTON:
		case BS_SPLITBUTTON:
		case BS_DEFSPLITBUTTON:
		{
			SetDarkTheme(hWnd);
			break;
		}

		default:
		{
			break;
		}
		}
	}
	else if (_wcsicmp(str, WC_COMBOBOX) == 0) {
		SetWindowTheme(hWnd, L"DarkMode_CFD", NULL);
	}
	else if (_wcsicmp(str, TOOLBARCLASSNAME) == 0) {
		HWND hTips = (HWND)SendMessage(hWnd, TB_GETTOOLTIPS, 0, 0);
		if (hTips != NULL) {
			SetDarkTheme(hTips);
		}
	}
	else if (_wcsicmp(str, WC_EDIT) == 0) {
		const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		if (((nStyle & WS_VSCROLL) == WS_VSCROLL) || ((nStyle & WS_HSCROLL) == WS_HSCROLL)) {
			const LONG_PTR nExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

			SetWindowLongPtr(hWnd, GWL_STYLE, nStyle | WS_BORDER);
			SetWindowLongPtr(hWnd, GWL_EXSTYLE, nExStyle & ~WS_EX_CLIENTEDGE);
			SetDarkTheme(hWnd);
		}
		else {
			SetWindowTheme(hWnd, L"DarkMode_CFD", NULL);
		}
	}
	else if (_wcsicmp(str, RICHEDIT_CLASS) == 0) {
		const LONG_PTR nStyle = GetWindowLongPtr(hWnd, GWL_STYLE);
		const LONG_PTR nExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);

		BOOL hasStaticEdge = (nExStyle & WS_EX_STATICEDGE) == WS_EX_STATICEDGE;
		COLORREF cBg = (hasStaticEdge ? GetControlBackgroundColor() : GetBackgroundColor());

		CHARFORMATW cf;
		ZeroMemory(&cf, sizeof(CHARFORMATW));
		cf.cbSize = sizeof(CHARFORMATW);
		cf.dwMask = CFM_COLOR;
		cf.crTextColor = GetTextColorForDarkMode();

		SendMessage(hWnd, EM_SETBKGNDCOLOR, 0, (LPARAM)cBg);
		SendMessage(hWnd, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
		SetWindowLongPtr(hWnd, GWL_STYLE, nStyle | WS_BORDER);
		SetWindowLongPtr(hWnd, GWL_EXSTYLE, nExStyle & ~WS_EX_STATICEDGE);
		SetWindowTheme(hWnd, NULL, L"DarkMode_Explorer::ScrollBar");
	}

	return TRUE;
}

void SetDarkModeForChild(HWND hParent)
{
	if (IsDarkModeEnabled()) {
		EnumChildWindows(hParent, DarkModeForChildCallback, (LPARAM)NULL);
	}
}
