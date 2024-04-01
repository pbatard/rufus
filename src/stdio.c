/*
 * Rufus: The Reliable USB Formatting Utility
 * Standard User I/O Routines (logging, status, error, etc.)
 * Copyright © 2011-2024 Pete Batard <pete@akeo.ie>
 * Copyright © 2020 Mattiwatti <mattiwatti@gmail.com>
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
#include <wininet.h>
#include <winternl.h>
#include <dbghelp.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "rufus.h"
#include "missing.h"
#include "settings.h"
#include "resource.h"
#include "msapi_utf8.h"
#include "localization.h"
#include "bled/bled.h"

#define FACILITY_WIM            322
#define DEFAULT_BASE_ADDRESS    0x100000000ULL
#define RSDS_SIG                0x53445352

/*
 * Globals
 */
const HANDLE hRufus = (HANDLE)0x0000005275667573ULL;	// "\0\0\0Rufus"
HWND hStatus;
size_t ubuffer_pos = 0;
char ubuffer[UBUFFER_SIZE];	// Buffer for ubpushf() messages we don't log right away
static uint64_t archive_size;

#pragma pack(push, 1)
typedef struct {
	DWORD   Signature;	// "RSDS"
	GUID    Guid;
	DWORD   Age;
	CHAR    PdbName[1];
} debug_info_t;
#pragma pack(pop)

void uprintf(const char *format, ...)
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

	wbuf = utf8_to_wchar(buf);
	// Send output to Windows debug facility
	// coverity[dont_call]
	OutputDebugStringW(wbuf);
	if ((hLog != NULL) && (hLog != INVALID_HANDLE_VALUE)) {
		// Send output to our log Window
		Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
		Edit_ReplaceSel(hLog, wbuf);
		// Make sure the message scrolls into view
		Edit_Scroll(hLog, Edit_GetLineCount(hLog), 0);
	}
	free(wbuf);
}

void uprintfs(const char* str)
{
	wchar_t* wstr;
	wstr = utf8_to_wchar(str);
	// coverity[dont_call]
	OutputDebugStringW(wstr);
	if ((hLog != NULL) && (hLog != INVALID_HANDLE_VALUE)) {
		Edit_SetSel(hLog, MAX_LOG_SIZE, MAX_LOG_SIZE);
		Edit_ReplaceSel(hLog, wstr);
		Edit_Scroll(hLog, Edit_GetLineCount(hLog), 0);
	}
	free(wstr);
}

uint32_t read_file(const char* path, uint8_t** buf)
{
	FILE* fd = fopenU(path, "rb");
	if (fd == NULL) {
		uprintf("Error: Can't open file '%s'", path);
		return 0;
	}

	fseek(fd, 0L, SEEK_END);
	uint32_t size = (uint32_t)ftell(fd);
	fseek(fd, 0L, SEEK_SET);

	// +1 so we can add an extra NUL
	*buf = malloc(size + 1);
	if (*buf == NULL) {
		uprintf("Error: Can't allocate %d bytes buffer for file '%s'", size, path);
		size = 0;
		goto out;
	}
	if (fread(*buf, 1, size, fd) != size) {
		uprintf("Error: Can't read '%s'", path);
		size = 0;
	}
	// Always NUL terminate the file
	(*buf)[size] = 0;

out:
	fclose(fd);
	if (size == 0) {
		free(*buf);
		*buf = NULL;
	}
	return size;
}

uint32_t write_file(const char* path, const uint8_t* buf, const uint32_t size)
{
	uint32_t written;
	FILE* fd = fopenU(path, "wb");
	if (fd == NULL) {
		uprintf("Error: Can't create '%s'", path);
		return 0;
	}
	written = (uint32_t)fwrite(buf, 1, size, fd);
	if (written != size)
		uprintf("Error: Can't write '%s'", path);
	fclose(fd);
	return written;
}

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

// Convert a Windows error to human readable string
// One really has to wonder why the hell FormatMessage() was designed not to
// handle FORMAT_MESSAGE_FROM_HMODULE automatically according to the facility...
const char *WindowsErrorString(void)
{
	static char err_string[256] = { 0 };

	DWORD size, presize;
	DWORD error_code, format_error;
	HANDLE hModule = NULL;

	error_code = GetLastError();
	// Check for specific facility error codes
	switch (HRESULT_FACILITY(error_code)) {
	case FACILITY_NULL:
		// Special case for internet related errors, that don't actually have a facility
		// set but still require a hModule into wininet to display the messages.
		if ((error_code >= INTERNET_ERROR_BASE) && (error_code <= INTERNET_ERROR_LAST))
			hModule = GetModuleHandleA("wininet.dll");
		break;
	case FACILITY_ITF:
		hModule = GetModuleHandleA("vdsutil.dll");
		break;
	case FACILITY_WIM:
		hModule = GetModuleHandleA("wimgapi.dll");
		break;
	default:
		break;
	}
	static_sprintf(err_string, "[0x%08lX] ", error_code);
	presize = (DWORD)strlen(err_string);

	// coverity[var_deref_model]
	size = FormatMessageU(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
		((hModule != NULL) ? FORMAT_MESSAGE_FROM_HMODULE : 0), hModule,
		HRESULT_CODE(error_code), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
		&err_string[presize], (DWORD)(sizeof(err_string)-strlen(err_string)), NULL);
	if (size == 0) {
		format_error = GetLastError();
		switch (format_error) {
		case ERROR_SUCCESS:
			static_sprintf(err_string, "[0x%08lX] (No Windows Error String)", error_code);
			break;
		case ERROR_MR_MID_NOT_FOUND:
		case ERROR_MUI_FILE_NOT_FOUND:
		case ERROR_MUI_FILE_NOT_LOADED:
			static_sprintf(err_string, "[0x%08lX] (NB: This system was unable to provide an English error message)", error_code);
			break;
		default:
			static_sprintf(err_string, "[0x%08lX] (FormatMessage error code 0x%08lX)",
				error_code, format_error);
			break;
		}
	} else {
		// Microsoft may suffix CRLF to error messages, which we need to remove...
		assert(presize > 2);
		size += presize - 2;
		// Cannot underflow if the above assert passed since our first char is neither of the following
		while ((err_string[size] == 0x0D) || (err_string[size] == 0x0A) || (err_string[size] == 0x20))
			err_string[size--] = 0;
	}

	SetLastError(error_code);	// Make sure we don't change the errorcode on exit
	return err_string;
}

char* GuidToString(const GUID* guid, BOOL bDecorated)
{
	static char guid_string[MAX_GUID_STRING_LENGTH];

	if (guid == NULL) return NULL;
	sprintf(guid_string, bDecorated ? "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}" :
		"%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
		(uint32_t)guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
	return guid_string;
}

GUID* StringToGuid(const char* str)
{
	static GUID guid;

	if (str == NULL) return NULL;
	if (sscanf(str, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		(uint32_t*)&guid.Data1, (uint32_t*)&guid.Data2, (uint32_t*)&guid.Data3,
		(uint32_t*)&guid.Data4[0], (uint32_t*)&guid.Data4[1], (uint32_t*)&guid.Data4[2],
		(uint32_t*)&guid.Data4[3], (uint32_t*)&guid.Data4[4], (uint32_t*)&guid.Data4[5],
		(uint32_t*)&guid.Data4[6], (uint32_t*)&guid.Data4[7]) != 11)
		return NULL;
	return &guid;
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
	const char* dir = ((right_to_left_mode) && (!copy_to_log)) ? LEFT_TO_RIGHT_MARK : "";
	double hr_size = (double)size;
	double t;
	uint16_t i_size;
	char **_msg_table = copy_to_log ? default_msg_table : msg_table;
	const double divider = fake_units ? 1000.0 : 1024.0;

	for (suffix = 0; suffix < MAX_SIZE_SUFFIXES - 1; suffix++) {
		if (hr_size < divider)
			break;
		hr_size /= divider;
	}
	if (suffix == 0) {
		static_sprintf(str_size, "%s%d%s %s", dir, (int)hr_size, dir, _msg_table[MSG_020 - MSG_000]);
	} else if (fake_units) {
		if (hr_size < 8) {
			static_sprintf(str_size, (fabs((hr_size * 10.0) - (floor(hr_size + 0.5) * 10.0)) < 0.5) ?
				"%0.0f%s":"%0.1f%s", hr_size, _msg_table[MSG_020 + suffix - MSG_000]);
		} else {
			t = (double)upo2((uint16_t)hr_size);
			i_size = (uint16_t)((fabs(1.0f - (hr_size / t)) < 0.05f) ? t : hr_size);
			static_sprintf(str_size, "%s%d%s %s", dir, i_size, dir, _msg_table[MSG_020 + suffix - MSG_000]);
		}
	} else {
		static_sprintf(str_size, (hr_size * 10.0 - (floor(hr_size) * 10.0)) < 0.5?
			"%s%0.0f%s %s":"%s%0.1f%s %s", dir, hr_size, dir, _msg_table[MSG_020 + suffix - MSG_000]);
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
	case ERROR_CANT_DOWNLOAD:
		return lmprintf(MSG_242);
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

typedef struct
{
	LPCWSTR lpFileName;
	DWORD dwDesiredAccess;
	DWORD dwShareMode;
	DWORD dwCreationDisposition;
	DWORD dwFlagsAndAttributes;
	HANDLE hFile;
	DWORD dwError;
} cfx_params_t;

// Thread used by CreateFileWithTimeout() below
DWORD WINAPI CreateFileWithTimeoutThread(void* params)
{
	cfx_params_t* cfx_params = (cfx_params_t*)params;
	HANDLE hFile = CreateFileW(cfx_params->lpFileName, cfx_params->dwDesiredAccess,
		cfx_params->dwShareMode, NULL, cfx_params->dwCreationDisposition,
		cfx_params->dwFlagsAndAttributes, NULL);

	cfx_params->dwError = (hFile == INVALID_HANDLE_VALUE) ? GetLastError() : NOERROR;
	cfx_params->hFile = hFile;

	return cfx_params->dwError;
}

// A UTF-8 CreateFile() with timeout
HANDLE CreateFileWithTimeout(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, HANDLE hTemplateFile, DWORD dwTimeOut)
{
	HANDLE hThread;
	wconvert(lpFileName);

	cfx_params_t params = {
		wlpFileName,
		dwDesiredAccess,
		dwShareMode,
		dwCreationDisposition,
		dwFlagsAndAttributes,
		INVALID_HANDLE_VALUE,
		ERROR_IO_PENDING,
	};

	hThread = CreateThread(NULL, 0, CreateFileWithTimeoutThread, &params, 0, NULL);
	if (hThread != NULL) {
		if (WaitForSingleObject(hThread, dwTimeOut) == WAIT_TIMEOUT) {
			CancelSynchronousIo(hThread);
			WaitForSingleObject(hThread, INFINITE);
			params.dwError = WAIT_TIMEOUT;
		}
		CloseHandle(hThread);
	} else {
		params.dwError = GetLastError();
	}

	wfree(lpFileName);
	SetLastError(params.dwError);
	return params.hFile;
}

// A WriteFile() equivalent, with up to nNumRetries write attempts on error.
BOOL WriteFileWithRetry(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
	LPDWORD lpNumberOfBytesWritten, DWORD nNumRetries)
{
	DWORD nTry;
	BOOL readFilePointer;
	LARGE_INTEGER liFilePointer, liZero = { { 0,0 } };
	DWORD NumberOfBytesWritten;

	if (lpNumberOfBytesWritten == NULL)
		lpNumberOfBytesWritten = &NumberOfBytesWritten;

	// Need to get the current file pointer in case we need to retry
	readFilePointer = SetFilePointerEx(hFile, liZero, &liFilePointer, FILE_CURRENT);
	if (!readFilePointer)
		uprintf("Warning: Could not read file pointer %s", WindowsErrorString());

	if (nNumRetries == 0)
		nNumRetries = 1;
	for (nTry = 1; nTry <= nNumRetries; nTry++) {
		// Need to rewind our file position on retry - if we can't even do that, just give up
		if ((nTry > 1) && (!SetFilePointerEx(hFile, liFilePointer, NULL, FILE_BEGIN))) {
			uprintf("Could not set file pointer - Aborting");
			break;
		}
		if (WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, NULL)) {
			LastWriteError = 0;
			if (nNumberOfBytesToWrite == *lpNumberOfBytesWritten)
				return TRUE;
			// Some large drives return 0, even though all the data was written - See github #787 */
			if (large_drive && (*lpNumberOfBytesWritten == 0)) {
				uprintf("Warning: Possible short write");
				return TRUE;
			}
			uprintf("Wrote %d bytes but requested %d", *lpNumberOfBytesWritten, nNumberOfBytesToWrite);
		} else {
			uprintf("Write error %s", WindowsErrorString());
			LastWriteError = RUFUS_ERROR(GetLastError());
		}
		// If we can't reposition for the next run, just abort
		if (!readFilePointer)
			break;
		if (nTry < nNumRetries) {
			uprintf("Retrying in %d seconds...", WRITE_TIMEOUT / 1000);
			// TODO: Call GetProcessSearch() here?
			Sleep(WRITE_TIMEOUT);
		}
	}
	if (SCODE_CODE(GetLastError()) == ERROR_SUCCESS)
		SetLastError(RUFUS_ERROR(ERROR_WRITE_FAULT));
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

#define STATUS_SUCCESS					((NTSTATUS)0x00000000L)
#define STATUS_PROCEDURE_NOT_FOUND		((NTSTATUS)0xC000007AL)
#define FILE_ATTRIBUTE_VALID_FLAGS		0x00007FB7
#define NtCurrentPeb()					(NtCurrentTeb()->ProcessEnvironmentBlock)
#define RtlGetProcessHeap()				(NtCurrentPeb()->Reserved4[1]) // NtCurrentPeb()->ProcessHeap, mangled due to deficiencies in winternl.h

PF_TYPE_DECL(NTAPI, NTSTATUS, NtCreateFile, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlDosPathNameToNtPathNameW, (PCWSTR, PUNICODE_STRING, PWSTR*, PVOID));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlFreeHeap, (PVOID, ULONG, PVOID));
PF_TYPE_DECL(NTAPI, VOID, RtlSetLastWin32ErrorAndNtStatusFromNtStatus, (NTSTATUS));

HANDLE CreatePreallocatedFile(const char* lpFileName, DWORD dwDesiredAccess,
	DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes, LONGLONG fileSize)
{
	HANDLE fileHandle = INVALID_HANDLE_VALUE;
	OBJECT_ATTRIBUTES objectAttributes;
	IO_STATUS_BLOCK ioStatusBlock;
	UNICODE_STRING ntPath;
	ULONG fileAttributes, flags = 0;
	LARGE_INTEGER allocationSize;
	NTSTATUS status = STATUS_SUCCESS;

	PF_INIT_OR_SET_STATUS(NtCreateFile, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlDosPathNameToNtPathNameW, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlFreeHeap, Ntdll);
	PF_INIT_OR_SET_STATUS(RtlSetLastWin32ErrorAndNtStatusFromNtStatus, Ntdll);

	if (!NT_SUCCESS(status)) {
		return CreateFileU(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
			dwCreationDisposition, dwFlagsAndAttributes, NULL);
	}

	wconvert(lpFileName);

	// Determine creation disposition and flags
	switch (dwCreationDisposition) {
	case CREATE_NEW:
		dwCreationDisposition = FILE_CREATE;
		break;
	case CREATE_ALWAYS:
		dwCreationDisposition = FILE_OVERWRITE_IF;
		break;
	case OPEN_EXISTING:
		dwCreationDisposition = FILE_OPEN;
		break;
	case OPEN_ALWAYS:
		dwCreationDisposition = FILE_OPEN_IF;
		break;
	case TRUNCATE_EXISTING:
		dwCreationDisposition = FILE_OVERWRITE;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_OVERLAPPED) == 0)
		flags |= FILE_SYNCHRONOUS_IO_NONALERT;

	if ((dwFlagsAndAttributes & FILE_FLAG_WRITE_THROUGH) != 0)
		flags |= FILE_WRITE_THROUGH;

	if ((dwFlagsAndAttributes & FILE_FLAG_NO_BUFFERING) != 0)
		flags |= FILE_NO_INTERMEDIATE_BUFFERING;

	if ((dwFlagsAndAttributes & FILE_FLAG_RANDOM_ACCESS) != 0)
		flags |= FILE_RANDOM_ACCESS;

	if ((dwFlagsAndAttributes & FILE_FLAG_SEQUENTIAL_SCAN) != 0)
		flags |= FILE_SEQUENTIAL_ONLY;

	if ((dwFlagsAndAttributes & FILE_FLAG_DELETE_ON_CLOSE) != 0) {
		flags |= FILE_DELETE_ON_CLOSE;
		dwDesiredAccess |= DELETE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_BACKUP_SEMANTICS) != 0) {
		if ((dwDesiredAccess & GENERIC_ALL) != 0)
			flags |= (FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REMOTE_INSTANCE);
		else {
			if ((dwDesiredAccess & GENERIC_READ) != 0)
				flags |= FILE_OPEN_FOR_BACKUP_INTENT;

			if ((dwDesiredAccess & GENERIC_WRITE) != 0)
				flags |= FILE_OPEN_REMOTE_INSTANCE;
		}
	} else {
		flags |= FILE_NON_DIRECTORY_FILE;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_OPEN_REPARSE_POINT) != 0)
		flags |= FILE_OPEN_REPARSE_POINT;

	if ((dwFlagsAndAttributes & FILE_FLAG_OPEN_NO_RECALL) != 0)
		flags |= FILE_OPEN_NO_RECALL;

	fileAttributes = dwFlagsAndAttributes & (FILE_ATTRIBUTE_VALID_FLAGS & ~FILE_ATTRIBUTE_DIRECTORY);

	dwDesiredAccess |= (SYNCHRONIZE | FILE_READ_ATTRIBUTES);

	// Convert DOS path to NT format
	if (!pfRtlDosPathNameToNtPathNameW(wlpFileName, &ntPath, NULL, NULL)) {
		wfree(lpFileName);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	InitializeObjectAttributes(&objectAttributes, &ntPath, 0, NULL, NULL);

	if (lpSecurityAttributes != NULL) {
		if (lpSecurityAttributes->bInheritHandle)
			objectAttributes.Attributes |= OBJ_INHERIT;
		objectAttributes.SecurityDescriptor = lpSecurityAttributes->lpSecurityDescriptor;
	}

	if ((dwFlagsAndAttributes & FILE_FLAG_POSIX_SEMANTICS) == 0)
		objectAttributes.Attributes |= OBJ_CASE_INSENSITIVE;

	allocationSize.QuadPart = fileSize;

	// Call NtCreateFile
	status = pfNtCreateFile(&fileHandle, dwDesiredAccess, &objectAttributes, &ioStatusBlock,
		&allocationSize, fileAttributes, dwShareMode, dwCreationDisposition, flags, NULL, 0);

	pfRtlFreeHeap(RtlGetProcessHeap(), 0, ntPath.Buffer);
	wfree(lpFileName);
	pfRtlSetLastWin32ErrorAndNtStatusFromNtStatus(status);

	return fileHandle;
}

// The following calls are used to resolve the addresses of DLL function calls
// that are not publicly exposed by Microsoft. This is accomplished by downloading
// the relevant .pdb and looking up the relevant address there. Once an address is
// found, it is stored in the Rufus settings so that it can be reused.

PF_TYPE_DECL(WINAPI, BOOL, SymInitialize, (HANDLE, PCSTR, BOOL));
PF_TYPE_DECL(WINAPI, DWORD64, SymLoadModuleEx, (HANDLE, HANDLE, PCSTR, PCSTR, DWORD64, DWORD, PMODLOAD_DATA, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, SymUnloadModule64, (HANDLE, DWORD64));
PF_TYPE_DECL(WINAPI, BOOL, SymEnumSymbols, (HANDLE, ULONG64, PCSTR, PSYM_ENUMERATESYMBOLS_CALLBACK, PVOID));
PF_TYPE_DECL(WINAPI, BOOL, SymCleanup, (HANDLE));

BOOL CALLBACK EnumSymProc(PSYMBOL_INFO pSymInfo, ULONG SymbolSize, PVOID UserContext)
{
	dll_resolver_t* resolver = (dll_resolver_t*)UserContext;
	uint32_t i;

	for (i = 0; i < resolver->count; i++) {
		if (safe_strcmp(pSymInfo->Name, resolver->name[i]) == 0) {
			resolver->address[i] = (uint32_t)pSymInfo->Address;
#if defined(_DEBUG)
			uprintf("%08x: %s", resolver->address[i], resolver->name[i]);
#endif
		}
	}

	return TRUE;
}

uint32_t ResolveDllAddress(dll_resolver_t* resolver)
{
	uint32_t r = 0;
	uint32_t i;
	debug_info_t* info = NULL;
	char url[MAX_PATH], saved_id[MAX_PATH], path[MAX_PATH];
	uint8_t* buf = NULL;
	DWORD* dbuf;
	DWORD64 base_address = 0ULL;

	PF_INIT(SymInitialize, DbgHelp);
	PF_INIT(SymLoadModuleEx, DbgHelp);
	PF_INIT(SymUnloadModule64, DbgHelp);
	PF_INIT(SymEnumSymbols, DbgHelp);
	PF_INIT(SymCleanup, DbgHelp);

	if (pfSymInitialize == NULL || pfSymLoadModuleEx == NULL || pfSymUnloadModule64 == NULL ||
		pfSymEnumSymbols == NULL || pfSymCleanup == NULL || resolver->count == 0 ||
		resolver->path == NULL || resolver->name == NULL || resolver->address == NULL)
		return 0;

	// Get the PDB unique address from the DLL. Note that we can *NOT* use SymGetModuleInfo64() to
	// obtain the data we need because Microsoft either *BOTCHED* or *DELIBERATELY CRIPPLED* their
	// SymLoadModuleEx()/SymLoadModule64() implementation on ARM64, so that the return value is always
	// 0 with GetLastError() set to ERROR_SUCCESS, thereby *FALSELY* indicating that the module is
	// already loaded... So we just load the whole DLL into a buffer and look for an "RSDS" section
	// per https://www.godevtool.com/Other/pdb.htm
	r = read_file(resolver->path, &buf);
	if (r == 0)
		return 0;

	dbuf = (DWORD*)buf;
	for (i = 0; i < (r - sizeof(debug_info_t)) / sizeof(DWORD); i++) {
		if (dbuf[i] == RSDS_SIG) {
			info = (debug_info_t*)&dbuf[i];
			if (safe_strstr(info->PdbName, ".pdb") != NULL)
				break;
		}
	}
	if (info == NULL) {
		uprintf("Could not find debug info in '%s'", resolver->path);
		goto out;
	}

	// Check settings to see if we have existing data for these DLL calls.
	for (i = 0; i < resolver->count; i++) {
		static_sprintf(saved_id, "%s@%s%x:%s", _filenameU(resolver->path),
			GuidToString(&info->Guid, FALSE), (int)info->Age, resolver->name[i]);
		resolver->address[i] = ReadSetting32(saved_id);
		if (resolver->address[i] == 0)
			break;
	}

	if (i == resolver->count) {
		// No need to download the PDB
		r = resolver->count;
		goto out;
	}

	// Download the PDB from Microsoft's symbol servers
	if (MessageBoxExU(hMainDialog, lmprintf(MSG_345), lmprintf(MSG_115),
		MB_YESNO | MB_ICONWARNING | MB_IS_RTL, selected_langid) != IDYES)
		goto out;
	static_sprintf(path, "%s\\%s", temp_dir, info->PdbName);
	static_sprintf(url, "http://msdl.microsoft.com/download/symbols/%s/%s%x/%s",
		info->PdbName, GuidToString(&info->Guid, FALSE), (int)info->Age, info->PdbName);
	if (DownloadToFileOrBufferEx(url, path, SYMBOL_SERVER_USER_AGENT, NULL, hMainDialog, FALSE) < 200 * KB)
		goto out;

	if (!pfSymInitialize(hRufus, NULL, FALSE)) {
		uprintf("Could not initialize DLL symbol handler");
		goto out;
	}

	// NB: SymLoadModuleEx() does not load a PDB unless the file has an explicit '.pdb' extension
	base_address = pfSymLoadModuleEx(hRufus, NULL, path, NULL, DEFAULT_BASE_ADDRESS, 0, NULL, 0);
	assert(base_address == DEFAULT_BASE_ADDRESS);
	// On Windows 11 ARM64 the following call will return *TWO* different addresses for the same
	// call, because most Windows DLL's are ARM64X, which means that they are an unholy union of
	// both X64 and ARM64 code in the same binary...
	// See https://learn.microsoft.com/en-us/windows/arm/arm64x-pe
	// Now this would be all swell and dandy if Microsoft's debugging/symbol APIs had followed
	// and would give us a hint of the architecture behind each duplicate address, but of course,
	// the SYMBOL_INFO passed to EnumSymProc contains no such data. So we currently don't have a
	// way to tell which of the two addresses we get on ARM64 is for which architecture... :(
	pfSymEnumSymbols(hRufus, base_address, "*!*", EnumSymProc, resolver);
	DeleteFileU(path);

	// Store the addresses
	r = 0;
	for (i = 0; i < resolver->count; i++) {
		static_sprintf(saved_id, "%s@%s%x:%s", _filenameU(resolver->path),
			GuidToString(&info->Guid, FALSE), (int)info->Age, resolver->name[i]);
		if (resolver->address[i] != 0) {
			WriteSetting32(saved_id, resolver->address[i]);
			r++;
		}
	}

out:
	free(buf);
	if (base_address != 0)
		pfSymUnloadModule64(hRufus, base_address);
	pfSymCleanup(hRufus);
	return r;
}

static void print_extracted_file(const char* file_path, uint64_t file_length)
{
	char str[MAX_PATH];

	if (file_path == NULL)
		return;
	static_sprintf(str, "%s (%s)", file_path, SizeToHumanReadable(file_length, TRUE, FALSE));
	uprintf("Extracting: %s", str);
	PrintStatus(0, MSG_000, str);	// MSG_000 is "%s"
}

static void update_progress(const uint64_t processed_bytes)
{
	UpdateProgressWithInfo(OP_EXTRACT_ZIP, MSG_348, processed_bytes, archive_size);
}

// Extract content from a zip archive onto the designated directory or drive
BOOL ExtractZip(const char* src_zip, const char* dest_dir)
{
	int64_t extracted_bytes = 0;

	if (src_zip == NULL)
		return FALSE;
	archive_size = _filesizeU(src_zip);
	if (bled_init(256 * KB, NULL, NULL, NULL, update_progress, print_extracted_file, &ErrorStatus) != 0)
		return FALSE;
	uprintf("● Copying files from '%s'", src_zip);
	extracted_bytes = bled_uncompress_to_dir(src_zip, dest_dir, BLED_COMPRESSION_ZIP);
	bled_exit();
	return (extracted_bytes > 0);
}
