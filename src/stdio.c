/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard User I/O Routines (logging, status, etc.)
 * Copyright © 2011-2017 Pete Batard <pete@akeo.ie>
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
#include <math.h>

#include "rufus.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"

/*
 * Globals
 */
HWND hStatus;
size_t ubuffer_pos = 0;
char ubuffer[UBUFFER_SIZE];	// Buffer for ubpushf() messages we don't log right away

#ifdef RUFUS_LOGGING
void _uprintf(const char *format, ...)
{
	static char buf[4096];
	char* p = buf;
	wchar_t* wbuf;
	va_list args;
	int n;

	va_start(args, format);
	n = safe_vsnprintf(p, sizeof(buf)-3, format, args); // buf-3 is room for CR/LF/NUL
	va_end(args);

	p += (n < 0)?sizeof(buf)-3:n;

	while((p>buf) && (isspaceU(p[-1])))
		*--p = '\0';

	*p++ = '\r';
	*p++ = '\n';
	*p   = '\0';

	// Yay, Windows 10 *FINALLY* added actual Unicode support for OutputDebugStringW()!
	wbuf = utf8_to_wchar(buf);
	// Send output to Windows debug facility
	OutputDebugStringW(wbuf);
	if ((hLog != NULL) && (hLog != INVALID_HANDLE_VALUE)) {
		// Send output to our log Window
		Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
		Edit_ReplaceSel(hLog, wbuf);
		// Make sure the message scrolls into view
		// (Or see code commented in LogProc:WM_SHOWWINDOW for a less forceful scroll)
		Edit_Scroll(hLog, Edit_GetLineCount(hLog), 0);
	}
	free(wbuf);
}
#endif

// Prints a bitstring of a number of any size, with or without leading zeroes.
// See also the printbits() and printbitslz() helper macros in rufus.h
char *_printbits(size_t const size, void const * const ptr, int leading_zeroes)
{
	// sizeof(uintmax_t) so that we have enough space to store whatever is thrown at us
	static char str[sizeof(uintmax_t) * 8 + 3];
	size_t i;
	uint8_t* b = (uint8_t*)ptr;
	uintmax_t mask, lzmask = 0, val = 0;

	// Little endian, the SCOURGE of any rational computing
	for (i = 0; i < size; i++)
		val |= ((uintmax_t)b[i]) << (8 * i);

	str[0] = '0';
	str[1] = 'b';
	if (leading_zeroes)
		lzmask = 1ULL << (size * 8 - 1);
	for (i = 2, mask = 1ULL << (sizeof(uintmax_t) * 8 - 1); mask != 0; mask >>= 1) {
		if ((i > 2) || (lzmask & mask))
			str[i++] = (val & mask) ? '1' : '0';
		else if (val & mask)
			str[i++] = '1';
	}
	str[i] = '\0';
	return str;
}

// Display an hex dump of buffer 'buf'
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

// Convert a windows error to human readable string
const char *WindowsErrorString(void)
{
static char err_string[256] = {0};

	DWORD size;
	DWORD error_code, format_error;

	error_code = GetLastError();

	static_sprintf(err_string, "[0x%08lX] ", error_code);

	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS, NULL, HRESULT_CODE(error_code),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), &err_string[strlen(err_string)],
		sizeof(err_string)-(DWORD)strlen(err_string), NULL);
	if (size == 0) {
		format_error = GetLastError();
		if ((format_error) && (format_error != 0x13D))		// 0x13D, decode error, is returned for unknown codes
			static_sprintf(err_string, "Windows error code 0x%08lX (FormatMessage error code 0x%08lX)",
				error_code, format_error);
		else
			static_sprintf(err_string, "Unknown error 0x%08lX", error_code);
	}

	SetLastError(error_code);	// Make sure we don't change the errorcode on exit
	return err_string;
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

// Find upper power of 2
static __inline uint16_t upo2(uint16_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v++;
	return v;
}

// Convert a size to human readable
char* SizeToHumanReadable(uint64_t size, BOOL copy_to_log, BOOL fake_units)
{
	int suffix;
	static char str_size[32];
	const char* dir = ((right_to_left_mode)&&(!copy_to_log))?RIGHT_TO_LEFT_MARK:"";
	double hr_size = (double)size;
	double t;
	uint16_t i_size;
	char **_msg_table = copy_to_log?default_msg_table:msg_table;
	const double divider = fake_units?1000.0:1024.0;

	for (suffix=0; suffix<MAX_SIZE_SUFFIXES-1; suffix++) {
		if (hr_size < divider)
			break;
		hr_size /= divider;
	}
	if (suffix == 0) {
		static_sprintf(str_size, "%s%d%s %s", dir, (int)hr_size, dir, _msg_table[MSG_020-MSG_000]);
	} else if (fake_units) {
		if (hr_size < 8) {
			static_sprintf(str_size, (fabs((hr_size*10.0)-(floor(hr_size + 0.5)*10.0)) < 0.5)?"%0.0f%s":"%0.1f%s",
				hr_size, _msg_table[MSG_020+suffix-MSG_000]);
		} else {
			t = (double)upo2((uint16_t)hr_size);
			i_size = (uint16_t)((fabs(1.0f-(hr_size / t)) < 0.05f)?t:hr_size);
			static_sprintf(str_size, "%s%d%s %s", dir, i_size, dir, _msg_table[MSG_020+suffix-MSG_000]);
		}
	} else {
		static_sprintf(str_size, (hr_size * 10.0 - (floor(hr_size) * 10.0)) < 0.5?
			"%s%0.0f%s %s":"%s%0.1f%s %s", dir, hr_size, dir, _msg_table[MSG_020+suffix-MSG_000]);
	}
	return str_size;
}

// Convert a YYYYMMDDHHMMSS UTC timestamp to a more human readable version
char* TimestampToHumanReadable(uint64_t ts)
{
	uint64_t rem = ts, divisor = 10000000000ULL;
	uint16_t data[6];
	int i;
	static char str[64];

	for (i = 0; i < 6; i++) {
		data[i] = (uint16_t) ((divisor == 0)?rem:(rem / divisor));
		rem %= divisor;
		divisor /= 100ULL;
	}
	static_sprintf(str, "%04d.%02d.%02d %02d:%02d:%02d (UTC)", data[0], data[1], data[2], data[3], data[4], data[5]);
	return str;
}

// Convert custom error code to messages
const char* _StrError(DWORD error_code)
{
	if ( (!IS_ERROR(error_code)) || (SCODE_CODE(error_code) == ERROR_SUCCESS)) {
		return lmprintf(MSG_050);
	}
	if (SCODE_FACILITY(error_code) != FACILITY_STORAGE) {
		uprintf("StrError: non storage - %08X (%X)\n", error_code, SCODE_FACILITY(error_code));
		SetLastError(error_code);
		return WindowsErrorString();
	}
	switch (SCODE_CODE(error_code)) {
	case ERROR_GEN_FAILURE:
		return lmprintf(MSG_051);
	case ERROR_INCOMPATIBLE_FS:
		return lmprintf(MSG_052);
	case ERROR_ACCESS_DENIED:
		return lmprintf(MSG_053);
	case ERROR_WRITE_PROTECT:
		return lmprintf(MSG_054);
	case ERROR_DEVICE_IN_USE:
		return lmprintf(MSG_055);
	case ERROR_CANT_QUICK_FORMAT:
		return lmprintf(MSG_056);
	case ERROR_LABEL_TOO_LONG:
		return lmprintf(MSG_057);
	case ERROR_INVALID_HANDLE:
		return lmprintf(MSG_058);
	case ERROR_INVALID_CLUSTER_SIZE:
		return lmprintf(MSG_059);
	case ERROR_INVALID_VOLUME_SIZE:
		return lmprintf(MSG_060);
	case ERROR_NO_MEDIA_IN_DRIVE:
		return lmprintf(MSG_061);
	case ERROR_NOT_SUPPORTED:
		return lmprintf(MSG_062);
	case ERROR_NOT_ENOUGH_MEMORY:
		return lmprintf(MSG_063);
	case ERROR_READ_FAULT:
		return lmprintf(MSG_064);
	case ERROR_WRITE_FAULT:
		return lmprintf(MSG_065);
	case ERROR_INSTALL_FAILURE:
		return lmprintf(MSG_066);
	case ERROR_OPEN_FAILED:
		return lmprintf(MSG_067);
	case ERROR_PARTITION_FAILURE:
		return lmprintf(MSG_068);
	case ERROR_CANNOT_COPY:
		return lmprintf(MSG_069);
	case ERROR_CANCELLED:
		return lmprintf(MSG_070);
	case ERROR_CANT_START_THREAD:
		return lmprintf(MSG_071);
	case ERROR_BADBLOCKS_FAILURE:
		return lmprintf(MSG_072);
	case ERROR_ISO_SCAN:
		return lmprintf(MSG_073);
	case ERROR_ISO_EXTRACT:
		return lmprintf(MSG_074);
	case ERROR_CANT_REMOUNT_VOLUME:
		return lmprintf(MSG_075);
	case ERROR_CANT_PATCH:
		return lmprintf(MSG_076);
	case ERROR_CANT_ASSIGN_LETTER:
		return lmprintf(MSG_077);
	case ERROR_CANT_MOUNT_VOLUME:
		return lmprintf(MSG_078);
	case ERROR_NOT_READY:
		return lmprintf(MSG_079);
	case ERROR_BAD_SIGNATURE:
		return lmprintf(MSG_172);
	default:
		SetLastError(error_code);
		return WindowsErrorString();
	}
}

const char* StrError(DWORD error_code, BOOL use_default_locale)
{
	const char* ret;
	if (use_default_locale)
		toggle_default_locale();
	ret = _StrError(error_code);
	if (use_default_locale)
		toggle_default_locale();
	return ret;
}

// A WriteFile() equivalent, with up to nNumRetries write attempts on error.
BOOL WriteFileWithRetry(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, DWORD nNumRetries)
{
	DWORD nTry;
	BOOL readFilePointer;
	LARGE_INTEGER liFilePointer, liZero = { { 0,0 } };

	// Need to get the current file pointer in case we need to retry
	readFilePointer = SetFilePointerEx(hFile, liZero, &liFilePointer, FILE_CURRENT);
	if (!readFilePointer)
		uprintf("Warning: Could not read file pointer: %s", WindowsErrorString());

	if (nNumRetries == 0)
		nNumRetries = 1;
	for (nTry = 1; nTry <= nNumRetries; nTry++) {
		// Need to rewind our file position on retry - if we can't even do that, just give up
		if ((nTry > 1) && (!SetFilePointerEx(hFile, liFilePointer, NULL, FILE_BEGIN))) {
			uprintf("Could not set file pointer - Aborting");
			break;
		}
		if (WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, NULL)) {
			if (nNumberOfBytesToWrite == *lpNumberOfBytesWritten)
				return TRUE;
			// Some large drives return 0, even though all the data was written - See github #787 */
			if (large_drive && (*lpNumberOfBytesWritten == 0)) {
				uprintf("Warning: Possible short write");
				return TRUE;
			}
			uprintf("Wrote %d bytes but requested %d", *lpNumberOfBytesWritten, nNumberOfBytesToWrite);
		} else {
			uprintf("Write error [0x%08X]", GetLastError());
		}
		// If we can't reposition for the next run, just abort
		if (!readFilePointer)
			break;
		if (nTry < nNumRetries) {
			uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
			Sleep(WRITE_TIMEOUT);
		}
	}
	if (SCODE_CODE(GetLastError()) == ERROR_SUCCESS)
		SetLastError(ERROR_SEVERITY_ERROR|FAC(FACILITY_STORAGE)|ERROR_WRITE_FAULT);
	return FALSE;
}

// A WaitForSingleObject() equivalent that doesn't block Windows messages
// This is needed, for instance, if you are waiting for a thread that may issue uprintf's
DWORD WaitForSingleObjectWithMessages(HANDLE hHandle, DWORD dwMilliseconds)
{
	uint64_t CurTime, EndTime = GetTickCount64() + dwMilliseconds;
	DWORD res;
	MSG msg;

	do {
		// Read all of the messages in this next loop, removing each message as we read it.
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if ((msg.message == WM_QUIT) || (msg.message == WM_CLOSE)) {
				SetLastError(ERROR_CANCELLED);
				return WAIT_FAILED;
			} else {
				DispatchMessage(&msg);
			}
		}

		// Wait for any message sent or posted to this queue or for the handle to signaled.
		res = MsgWaitForMultipleObjects(1, &hHandle, FALSE, dwMilliseconds, QS_ALLINPUT);

		if (dwMilliseconds != INFINITE) {
			CurTime = GetTickCount64();
			// Account for the case where we may reach the timeout condition while
			// processing timestamps
			if (CurTime < EndTime)
				dwMilliseconds = (DWORD) (EndTime - CurTime);
			else
				res = WAIT_TIMEOUT;
		}
	} while (res == (WAIT_OBJECT_0 + 1));

	return res;
}
