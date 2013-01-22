/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard I/O Routines (logging, status, etc.)
 * Copyright (c) 2011-2013 Pete Batard <pete@akeo.ie>
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
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "msapi_utf8.h"
#include "rufus.h"
#include "resource.h"

/*
 * Globals
 */
HWND hStatus;

#ifdef RUFUS_DEBUG
void _uprintf(const char *format, ...)
{
	char buf[4096], *p = buf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspace((unsigned char)p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	// Send output to Windows debug facility
	OutputDebugStringA(buf);
	// Send output to our log Window
	Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
	Edit_ReplaceSelU(hLog, buf);
	// Make sure the message scrolls into view
	// (Or see code commented in LogProc:WM_SHOWWINDOW for a less forceful scroll)
	SendMessage(hLog, EM_LINESCROLL, 0, SendMessage(hLog, EM_GETLINECOUNT, 0, 0));
}
#endif

void DumpBufferHex(void *buf, size_t size)
{
	unsigned char* buffer = (unsigned char*)buf;
	size_t i, j, k;
	char line[80] = "";

	for (i=0; i<size; i+=16) {
		if (i!=0)
			uprintf("%s\n", line);
		line[0] = 0;
		sprintf(&line[strlen(line)], "  %08x  ", (unsigned int)i);
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				sprintf(&line[strlen(line)], "%02x", buffer[i+j]);
			} else {
				sprintf(&line[strlen(line)], "  ");
			}
			sprintf(&line[strlen(line)], " ");
		}
		sprintf(&line[strlen(line)], " ");
		for(j=0,k=0; k<16; j++,k++) {
			if (i+j < size) {
				if ((buffer[i+j] < 32) || (buffer[i+j] > 126)) {
					sprintf(&line[strlen(line)], ".");
				} else {
					sprintf(&line[strlen(line)], "%c", buffer[i+j]);
				}
			}
		}
	}
	uprintf("%s\n", line);
}

/*
 * Convert a windows error to human readable string
 * uses retval as errorcode, or, if 0, use GetLastError()
 */
const char *WindowsErrorString(void)
{
static char err_string[256] = {0};

	DWORD size;
	DWORD error_code, format_error;

	error_code = GetLastError();

	safe_sprintf(err_string, sizeof(err_string), "[0x%08X] ", error_code);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_code,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &err_string[strlen(err_string)],
		sizeof(err_string)-(DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if ((format_error) && (format_error != 0x13D))		// 0x13D, decode error, is returned for unknown codes
			safe_sprintf(err_string, sizeof(err_string),
				"Windows error code 0x%08X (FormatMessage error code 0x%08X)", error_code, format_error);
		else
			safe_sprintf(err_string, sizeof(err_string), "Unknown error 0x%08X", error_code);
	}
	return err_string;
}

/*
 * Display a message on the status bar. If duration is non zero, ensures that message
 * is displayed for at least duration ms regardless, regardless of any other incoming
 * message
 */
static BOOL bStatusTimerArmed = FALSE;
char szStatusMessage[256] = { 0 };
static void CALLBACK PrintStatusTimeout(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	bStatusTimerArmed = FALSE;
	// potentially display lower priority message that was overridden
	SendMessageLU(GetDlgItem(hMainDialog, IDC_STATUS), SB_SETTEXTW,
						SBT_OWNERDRAW | 0, szStatusMessage);
	KillTimer(hMainDialog, TID_MESSAGE);
}

void PrintStatus(unsigned int duration, BOOL debug, const char *format, ...)
{
	char *p = szStatusMessage;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(szStatusMessage)-1, format, args); // room for NUL
	va_end(args);

	p += (n < 0)?sizeof(szStatusMessage)-1:n;

	while((p>szStatusMessage) && (isspace(p[-1])))
		*--p = '\0';

	*p   = '\0';

	if (debug)
		uprintf("%s\n", szStatusMessage);

	if ((duration) || (!bStatusTimerArmed)) {
		SendMessageLU(GetDlgItem(hMainDialog, IDC_STATUS), SB_SETTEXTA,
						SBT_OWNERDRAW | 0, szStatusMessage);
	}

	if (duration) {
		SetTimer(hMainDialog, TID_MESSAGE, duration, PrintStatusTimeout);
		bStatusTimerArmed = TRUE;
	}
}

char* GuidToString(const GUID* guid)
{
	static char guid_string[MAX_GUID_STRING_LENGTH];

	if (guid == NULL) return NULL;
	sprintf(guid_string, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(unsigned int)guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
	return guid_string;
}

// Convert a file size to human readable
char* SizeToHumanReadable(LARGE_INTEGER size)
{
	int suffix = 0;
	static char str_size[24];
	const char* sizes[] = { "", "KB", "MB", "GB", "TB" };
	double hr_size = (double)size.QuadPart;
	while ((suffix < ARRAYSIZE(sizes)) && (hr_size >= 1024.0)) {
		hr_size /= 1024.0;
		suffix++;
	}
	if (suffix == 0) {
		safe_sprintf(str_size, sizeof(str_size), "%d bytes", (int)hr_size);
	} else {
		safe_sprintf(str_size, sizeof(str_size), "%0.1f %s", hr_size, sizes[suffix]);
	}
	return str_size;
}

const char* StrError(DWORD error_code)
{
	if ( (!IS_ERROR(error_code)) || (SCODE_CODE(error_code) == ERROR_SUCCESS)) {
		return "Success";
	}
	if (SCODE_FACILITY(error_code) != FACILITY_STORAGE) {
		uprintf("StrError: non storage - %08X (%X)\n", error_code, SCODE_FACILITY(error_code));
		SetLastError(error_code);
		return WindowsErrorString();
	}
	switch (SCODE_CODE(error_code)) {
	case ERROR_GEN_FAILURE:
		return "Undetermined error while formatting";
	case ERROR_INCOMPATIBLE_FS:
		return "Cannot use the selected file system for this media";
	case ERROR_ACCESS_DENIED:
		return "Access to the media is denied";
	case ERROR_WRITE_PROTECT:
		return "Media is write protected";
	case ERROR_DEVICE_IN_USE:
		return "The device is in use by another process\n"
			"Please close any other process that may be accessing the device";
	case ERROR_CANT_QUICK_FORMAT:
		return "Quick format is not available for this device";
	case ERROR_LABEL_TOO_LONG:
		return "The volume label is invalid";
	case ERROR_INVALID_CLUSTER_SIZE:
		return "The selected cluster size is not valid for this device";
	case ERROR_INVALID_VOLUME_SIZE:
		return "The volume size is invalid";
	case ERROR_NO_MEDIA_IN_DRIVE:
		return "Please insert a media in drive";
	case ERROR_NOT_SUPPORTED:
		return "An unsupported command was received";
	case ERROR_NOT_ENOUGH_MEMORY:
		return "Memory allocation error";
	case ERROR_READ_FAULT:
		return "Read error";
	case ERROR_WRITE_FAULT:
		return "Write error";
	case ERROR_OPEN_FAILED:
		return "Could not open media. It may be in use by another process.\n"
			"Please re-plug the media and try again";
	case ERROR_PARTITION_FAILURE:
		return "Error while partitioning drive";
	case ERROR_CANNOT_COPY:
		return "Could not copy files to target drive";
	case ERROR_CANCELLED:
		return "Cancelled by user";
	case ERROR_CANT_START_THREAD:
		return "Unable to create formatting thread";
	case ERROR_BADBLOCKS_FAILURE:
		return "Bad blocks check didn't complete";
	case ERROR_ISO_SCAN:
		return "ISO image scan failure";
	case ERROR_ISO_EXTRACT:
		return "ISO image extraction failure";
	case ERROR_CANT_REMOUNT_VOLUME:
		return "Unable to remount volume. You may have to use the\n"
			"mountvol.exe command to make your device accessible again";
	case ERROR_CANT_PATCH:
		return "Unable to patch/setup files for boot";
	default:
		uprintf("Unknown error: %08X\n", error_code);
		SetLastError(error_code);
		return WindowsErrorString();
	}
}
