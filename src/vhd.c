/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling functions
 * Copyright (c) 2013 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"
#include "msapi_utf8.h"

#define WIM_GENERIC_READ	GENERIC_READ
#define WIM_OPEN_EXISTING	OPEN_EXISTING

typedef HANDLE (WINAPI *WIMCreateFile_t)(
	PWSTR   pszWimPath,
	DWORD   dwDesiredAccess,
	DWORD   dwCreationDisposition,
	DWORD   dwFlagsAndAttributes,
	DWORD   dwCompressionType,
	PDWORD  pdwCreationResult
);

typedef BOOL (WINAPI *WIMSetTemporaryPath_t)(
	HANDLE  hWim,
	PWSTR   pszPath
);

typedef HANDLE (WINAPI *WIMLoadImage_t)(
	HANDLE  hWim,
	DWORD   dwImageIndex
);

typedef BOOL (WINAPI *WIMExtractImagePath_t)(
	HANDLE  hImage,
	PWSTR   pszImagePath,
	PWSTR   pszDestinationPath,
	DWORD   dwExtractFlags
);

typedef BOOL (WINAPI *WIMCloseHandle_t)(
	HANDLE  hObj
);

// Extract a file from a WIM image
// NB: Don't bother trying to get progress from a WIM callback - it doesn't work!
BOOL WIMExtractFile(const char* image, int index, const char* src, const char* dst)
{
	BOOL r = FALSE;
	DWORD dw = 0;
	HANDLE hWim = NULL;
	HANDLE hImage = NULL;
	wchar_t wtemp[MAX_PATH] = {0};
	wchar_t* wimage = utf8_to_wchar(image);
	wchar_t* wsrc = utf8_to_wchar(src);
	wchar_t* wdst = utf8_to_wchar(dst);
	PF_DECL(WIMCreateFile);
	PF_DECL(WIMSetTemporaryPath);
	PF_DECL(WIMLoadImage);
	PF_DECL(WIMExtractImagePath);
	PF_DECL(WIMCloseHandle);

	PF_INIT_OR_OUT(WIMCreateFile, wimgapi);
	PF_INIT_OR_OUT(WIMSetTemporaryPath, wimgapi);
	PF_INIT_OR_OUT(WIMLoadImage, wimgapi);
	PF_INIT_OR_OUT(WIMExtractImagePath, wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, wimgapi);

	// TODO: check for NULL and missing wimgapi.dll

	if (GetTempPathW(ARRAYSIZE(wtemp), wtemp) == 0) {
		uprintf("Could not fetch temp path: %s\n", WindowsErrorString());
		goto out;
	}

	uprintf("Opening: %s (index #%d)\n", image, index);
	hWim = pfWIMCreateFile(wimage, WIM_GENERIC_READ, WIM_OPEN_EXISTING, 0, 0, &dw);
	if (hWim == NULL) {
		uprintf("  Error: '%s': %s\n", WindowsErrorString());
		goto out;
	}

	if (!pfWIMSetTemporaryPath(hWim, wtemp)) {
		uprintf("  Error setting temp path: %s\n", WindowsErrorString());
		goto out;
	}

	hImage = pfWIMLoadImage(hWim, (DWORD)index);
	if (hImage == NULL) {
		uprintf("  Error setting index: %s.\n", WindowsErrorString());
		goto out;
	}

	uprintf("Extracting: %s (From \\%s)\n", dst, src);
	if (!pfWIMExtractImagePath(hImage, wsrc, wdst, 0)) {
		uprintf("  Could not extract file: %s.\n", WindowsErrorString());
		goto out;
	}
	r = TRUE;

out:
	if ((hImage != NULL) || (hWim != NULL)) {
		uprintf("Closing: %s\n", image);
	}
	if (hImage != NULL) pfWIMCloseHandle(hImage);
	if (hWim != NULL) pfWIMCloseHandle(hWim);
	safe_free(wimage);
	safe_free(wsrc);
	safe_free(wdst);
	return r;
}
