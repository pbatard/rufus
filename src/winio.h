/*
* Rufus: The Reliable USB Formatting Utility
* Windows I/O redefinitions, that would be totally unnecessary had
* Microsoft done a proper job with their asynchronous APIs.
* Copyright © 2021-2024 Pete Batard <pete@akeo.ie>
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
#include "msapi_utf8.h"

#pragma once

// https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped
// See Microsoft? It's not THAT hard to define an OVERLAPPED struct in a manner that
// doesn't qualify as an example of "Crimes against humanity" in the Geneva convention.
typedef struct {
	ULONG_PTR                           Internal[2];
	ULONG64                             Offset;
	HANDLE                              hEvent;
	BOOL                                bOffsetUpdated;
} NOW_THATS_WHAT_I_CALL_AN_OVERLAPPED;

// File Descriptor for asynchronous accesses.
// The status field is a threestate value reflecting the result
// of the current asynchronous read operation:
//  1: Read was successful and completed synchronously
// -1: Read is pending asynchronously
//  0: Read Error
typedef struct {
	HANDLE                              hFile;
	INT                                 iStatus;
	NOW_THATS_WHAT_I_CALL_AN_OVERLAPPED Overlapped;
} ASYNC_FD;

/// <summary>
/// Open a file for asynchronous access. The values for the flags are the same as the ones
/// for the native CreateFile() call. Note that FILE_FLAG_OVERLAPPED will always be added
/// to dwFlagsAndAttributes before the file is instantiated, and that an internal
/// OVERLAPPED structure with its associated wait event is also created.
/// </summary>
/// <param name="lpFileName">The name of the file or device to be created or opened</param>
/// <param name="dwDesiredAccess">The requested access to the file or device</param>
/// <param name="dwShareMode">The requested sharing mode of the file or device</param>
/// <param name="dwCreationDisposition">Action to take on a file or device that exists or does not exist</param>
/// <param name="dwFlagsAndAttributes">The file or device attributes and flags</param>
/// <returns>Non NULL on success</returns>
static __inline HANDLE CreateFileAsync(LPCSTR lpFileName, DWORD dwDesiredAccess,
	DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes)
{
	ASYNC_FD* fd = calloc(sizeof(ASYNC_FD), 1);
	if (fd == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return NULL;
	}
	fd->Overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
	fd->hFile = CreateFileU(lpFileName, dwDesiredAccess, dwShareMode, NULL,
		dwCreationDisposition, FILE_FLAG_OVERLAPPED | dwFlagsAndAttributes, NULL);
	if (fd->hFile == INVALID_HANDLE_VALUE) {
		CloseHandle(fd->Overlapped.hEvent);
		free(fd);
		return NULL;
	}
	return fd;
}

/// <summary>
/// Close a previously opened asynchronous file
/// </summary>
/// <param name="h">An async handle, created by a call to CreateFileAsync()</param>
static __inline VOID CloseFileAsync(HANDLE h)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	if (fd == NULL || fd == INVALID_HANDLE_VALUE)
		return;
	CloseHandle(fd->hFile);
	CloseHandle(fd->Overlapped.hEvent);
	free(fd);
}

/// <summary>
/// Initiate a read operation for asynchronous I/O.
/// </summary>
/// <param name="h">An async handle, created by a call to CreateFileAsync()</param>
/// <param name="lpBuffer">The buffer that receives the data</param>
/// <param name="nNumberOfBytesToRead">Number of bytes requested</param>
/// <returns>TRUE on success, FALSE on error</returns>
static __inline BOOL ReadFileAsync(HANDLE h, LPVOID lpBuffer, DWORD nNumberOfBytesToRead)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	fd->Overlapped.bOffsetUpdated = FALSE;
	if (!ReadFile(fd->hFile, lpBuffer, nNumberOfBytesToRead, NULL,
		(OVERLAPPED*)&fd->Overlapped))
		// TODO: Is it possible to get ERROR_HANDLE_EOF here?
		fd->iStatus = (GetLastError() == ERROR_IO_PENDING) ? -1 : 0;
	else
		fd->iStatus = 1;
	return (fd->iStatus != 0);
}

/// <summary>
/// Initiate a write operation for asynchronous I/O.
/// </summary>
/// <param name="h">An async handle, created by a call to CreateFileAsync()</param>
/// <param name="lpBuffer">The buffer that contains the data</param>
/// <param name="nNumberOfBytesToWrite">Number of bytes to write</param>
/// <returns>TRUE on success, FALSE on error</returns>
static __inline BOOL WriteFileAsync(HANDLE h, LPVOID lpBuffer, DWORD nNumberOfBytesToWrite)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	fd->Overlapped.bOffsetUpdated = FALSE;
	if (!WriteFile(fd->hFile, lpBuffer, nNumberOfBytesToWrite, NULL,
		(OVERLAPPED*)&fd->Overlapped))
		// TODO: Is it possible to get ERROR_HANDLE_EOF here?
		fd->iStatus = (GetLastError() == ERROR_IO_PENDING) ? -1 : 0;
	else
		fd->iStatus = 1;
	return (fd->iStatus != 0);
}

/// <summary>
/// Wait for an asynchronous operation to complete, with timeout.
/// This function also succeeds if the I/O already completed synchronously.
/// </summary>
/// <param name="h">An async handle, created by a call to CreateFileAsync()</param>
/// <param name="dwTimeout">A timeout value, in ms</param>
/// <returns>TRUE on success, FALSE on error</returns>
static __inline BOOL WaitFileAsync(HANDLE h, DWORD dwTimeout)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	if (fd->iStatus > 0)	// Read completed synchronously
		return TRUE;
	return (WaitForSingleObject(fd->Overlapped.hEvent, dwTimeout) == WAIT_OBJECT_0);
}

/// <summary>
/// Return the number of bytes read or written and keep track/update the
/// current offset for an asynchronous read operation.
/// </summary>
/// <param name="h">An async handle, created by a call to CreateFileAsync()</param>
/// <param name="lpNumberOfBytes">A pointer that receives the number of bytes transferred.</param>
/// <returns>TRUE on success, FALSE on error</returns>
static __inline BOOL GetSizeAsync(HANDLE h, LPDWORD lpNumberOfBytes)
{
	ASYNC_FD* fd = (ASYNC_FD*)h;
	// Previous call to [Read/Write]FileAsync() failed
	if (fd->iStatus == 0) {
		*lpNumberOfBytes = 0;
		return FALSE;
	}
	// Detect if we already read the size and updated the offset
	if (fd->Overlapped.bOffsetUpdated) {
		SetLastError(ERROR_NO_MORE_ITEMS);
		return FALSE;
	}
	if (!GetOverlappedResultEx(fd->hFile, (OVERLAPPED*)&fd->Overlapped,
		lpNumberOfBytes, WRITE_TIMEOUT, (fd->iStatus < 0)))
		// When reading from VHD/VHDX we get SECTOR_NOT_FOUND rather than EOF for the end of the drive
		return (GetLastError() == ERROR_HANDLE_EOF || GetLastError() == ERROR_SECTOR_NOT_FOUND);
	fd->Overlapped.Offset += *lpNumberOfBytes;
	fd->Overlapped.bOffsetUpdated = TRUE;
	return TRUE;
}
