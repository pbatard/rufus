/*
 * Rufus: The Reliable USB Formatting Utility
 * Process search functionality
 *
 * Modified from System Informer (a.k.a. Process Hacker):
 *   https://github.com/winsiderss/systeminformer
 * Copyright © 2017-2024 Pete Batard <pete@akeo.ie>
 * Copyright © 2017 dmex
 * Copyright © 2009-2016 wj32
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
#include <assert.h>

#include "rufus.h"
#include "drive.h"
#include "process.h"
#include "missing.h"
#include "msapi_utf8.h"

PF_TYPE_DECL(NTAPI, PVOID, RtlCreateHeap, (ULONG, PVOID, SIZE_T, SIZE_T, PVOID, PRTL_HEAP_PARAMETERS));
PF_TYPE_DECL(NTAPI, PVOID, RtlDestroyHeap, (PVOID));
PF_TYPE_DECL(NTAPI, PVOID, RtlAllocateHeap, (PVOID, ULONG, SIZE_T));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlFreeHeap, (PVOID, ULONG, PVOID));

PF_TYPE_DECL(NTAPI, NTSTATUS, NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryInformationFile, (HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryInformationProcess, (HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtWow64QueryInformationProcess64, (HANDLE, ULONG, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtWow64ReadVirtualMemory64, (HANDLE, ULONGLONG, PVOID, ULONG64, PULONG64));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryObject, (HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtDuplicateObject, (HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcess, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, CLIENT_ID*));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcessToken, (HANDLE, ACCESS_MASK, PHANDLE));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtAdjustPrivilegesToken, (HANDLE, BOOLEAN, PTOKEN_PRIVILEGES, ULONG, PTOKEN_PRIVILEGES, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtClose, (HANDLE));

static PVOID PhHeapHandle = NULL;
static HANDLE hSearchProcessThread = NULL;
static BlockingProcess blocking_process = { 0 };

extern StrArray BlockingProcessList;

/*
 * Convert an NT Status to an error message
 *
 * \param Status An operattonal status.
 *
 * \return An error message string.
 *
 */
char* NtStatusError(NTSTATUS Status) {
	static char unknown[32];

	switch (Status) {
	case STATUS_SUCCESS:
		return "Operation Successful";
	case STATUS_UNSUCCESSFUL:
		return "Operation Failed";
	case STATUS_BUFFER_OVERFLOW:
		return "Buffer Overflow";
	case STATUS_NOT_IMPLEMENTED:
		return "Not Implemented";
	case STATUS_INFO_LENGTH_MISMATCH:
		return "Info Length Mismatch";
	case STATUS_INVALID_HANDLE:
		return "Invalid Handle.";
	case STATUS_INVALID_PARAMETER:
		return "Invalid Parameter";
	case STATUS_NO_MEMORY:
		return "Not Enough Quota";
	case STATUS_ACCESS_DENIED:
		return "Access Denied";
	case STATUS_BUFFER_TOO_SMALL:
		return "Buffer Too Small";
	case STATUS_OBJECT_TYPE_MISMATCH:
		return "Wrong Type";
	case STATUS_OBJECT_NAME_INVALID:
		return "Object Name Invalid";
	case STATUS_OBJECT_NAME_NOT_FOUND:
		return "Object Name not found";
	case STATUS_OBJECT_PATH_INVALID:
		return "Object Path Invalid";
	case STATUS_SHARING_VIOLATION:
		return "Sharing Violation";
	case STATUS_INSUFFICIENT_RESOURCES:
		return "Insufficient resources";
	case STATUS_NOT_SUPPORTED:
		return "Operation is not supported";
	default:
		static_sprintf(unknown, "Unknown error 0x%08lx", Status);
		return unknown;
	}
}

static NTSTATUS PhCreateHeap(VOID)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (PhHeapHandle != NULL)
		return STATUS_ALREADY_COMPLETE;

	PF_INIT_OR_SET_STATUS(RtlCreateHeap, Ntdll);
	
	if (NT_SUCCESS(status)) {
		PhHeapHandle = pfRtlCreateHeap(HEAP_NO_SERIALIZE | HEAP_GROWABLE, NULL, 2 * MB, 1 * MB, NULL, NULL);
		if (PhHeapHandle == NULL)
			status = STATUS_UNSUCCESSFUL;
	}

	return status;
}

static NTSTATUS PhDestroyHeap(VOID)
{
	NTSTATUS status = STATUS_SUCCESS;

	if (PhHeapHandle == NULL)
		return STATUS_ALREADY_COMPLETE;

	PF_INIT_OR_SET_STATUS(RtlDestroyHeap, Ntdll);

	if (NT_SUCCESS(status)) {
		if (pfRtlDestroyHeap(PhHeapHandle) == NULL) {
			PhHeapHandle = NULL;
		} else {
			status = STATUS_UNSUCCESSFUL;
		}
	}

	return status;
}

/**
 * Allocates a block of memory.
 *
 * \param Size The number of bytes to allocate.
 *
 * \return A pointer to the allocated block of memory.
 */
static PVOID PhAllocate(SIZE_T Size)
{
	if (PhHeapHandle == NULL)
		return NULL;

	PF_INIT(RtlAllocateHeap, Ntdll);
	if (pfRtlAllocateHeap == NULL)
		return NULL;

	return pfRtlAllocateHeap(PhHeapHandle, 0, Size);
}

/**
 * Frees a block of memory allocated with PhAllocate().
 *
 * \param Memory A pointer to a block of memory.
 */
static VOID PhFree(PVOID Memory)
{
	if (PhHeapHandle == NULL)
		return;

	PF_INIT(RtlFreeHeap, Ntdll);
	if (pfRtlFreeHeap != NULL)
		pfRtlFreeHeap(PhHeapHandle, 0, Memory);
}

/**
 * Enumerates all open handles.
 *
 * \param Handles A variable which receives a pointer to a structure containing information about
 * all opened handles. You must free the structure using PhFree() when you no longer need it.
 *
 * \return An NTStatus indicating success or the error code.
 */
NTSTATUS PhEnumHandlesEx(PSYSTEM_HANDLE_INFORMATION_EX *Handles)
{
	static ULONG initialBufferSize = 0x10000;
	NTSTATUS status = STATUS_SUCCESS;
	PVOID buffer;
	ULONG bufferSize;

	PF_INIT_OR_SET_STATUS(NtQuerySystemInformation, Ntdll);
	if (!NT_SUCCESS(status))
		return status;

	bufferSize = initialBufferSize;
	buffer = PhAllocate(bufferSize);
	if (buffer == NULL)
		return STATUS_NO_MEMORY;

	while ((status = pfNtQuerySystemInformation(SystemExtendedHandleInformation,
		buffer, bufferSize, NULL)) == STATUS_INFO_LENGTH_MISMATCH) {
		PhFree(buffer);
		bufferSize *= 2;

		// Fail if we're resizing the buffer to something very large.
		if (bufferSize > PH_LARGE_BUFFER_SIZE)
			return STATUS_INSUFFICIENT_RESOURCES;

		buffer = PhAllocate(bufferSize);
		if (buffer == NULL)
			return STATUS_NO_MEMORY;
	}

	if (!NT_SUCCESS(status)) {
		PhFree(buffer);
		return status;
	}

	if (bufferSize <= 0x200000)
		initialBufferSize = bufferSize;
	*Handles = (PSYSTEM_HANDLE_INFORMATION_EX)buffer;

	return status;
}

/**
 * Opens a process.
 *
 * \param ProcessHandle A variable which receives a handle to the process.
 * \param DesiredAccess The desired access to the process.
 * \param ProcessId The ID of the process.
 *
 * \return An NTStatus indicating success or the error code.
 */
NTSTATUS PhOpenProcess(PHANDLE ProcessHandle, ACCESS_MASK DesiredAccess, HANDLE ProcessId)
{
	NTSTATUS status = STATUS_SUCCESS;
	OBJECT_ATTRIBUTES objectAttributes;
	CLIENT_ID clientId;

	if ((LONG_PTR)ProcessId == (LONG_PTR)GetCurrentProcessId()) {
		*ProcessHandle = NtCurrentProcess();
		return 0;
	}

	PF_INIT_OR_SET_STATUS(NtOpenProcess, Ntdll);
	if (!NT_SUCCESS(status))
		return status;

	clientId.UniqueProcess = ProcessId;
	clientId.UniqueThread = NULL;

	InitializeObjectAttributes(&objectAttributes, NULL, 0, NULL, NULL);
	status = pfNtOpenProcess(ProcessHandle, DesiredAccess, &objectAttributes, &clientId);

	return status;
}

/**
 * Query processes with open handles to a file, volume or disk.
 *
 * \param VolumeOrFileHandle The handle to the target.
 * \param Information The returned list of processes.
 *
 * \return An NTStatus indicating success or the error code.
 */
NTSTATUS PhQueryProcessesUsingVolumeOrFile(HANDLE VolumeOrFileHandle,
	PFILE_PROCESS_IDS_USING_FILE_INFORMATION *Information)
{
	static ULONG initialBufferSize = 16 * KB;
	NTSTATUS status = STATUS_SUCCESS;
	PVOID buffer;
	ULONG bufferSize;
	IO_STATUS_BLOCK isb;

	PF_INIT_OR_SET_STATUS(NtQueryInformationFile, NtDll);
	if (!NT_SUCCESS(status))
		return status;

	bufferSize = initialBufferSize;
	buffer = PhAllocate(bufferSize);
	if (buffer == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	while ((status = pfNtQueryInformationFile(VolumeOrFileHandle, &isb, buffer, bufferSize,
		FileProcessIdsUsingFileInformation)) == STATUS_INFO_LENGTH_MISMATCH) {
		PhFree(buffer);
		bufferSize *= 2;
		// Fail if we're resizing the buffer to something very large.
		if (bufferSize > 64 * MB)
			return STATUS_INSUFFICIENT_RESOURCES;
		buffer = PhAllocate(bufferSize);
	}

	if (!NT_SUCCESS(status)) {
		PhFree(buffer);
		return status;
	}

	if (bufferSize <= 64 * MB)
		initialBufferSize = bufferSize;
	*Information = (PFILE_PROCESS_IDS_USING_FILE_INFORMATION)buffer;

	return status;
}

/**
 * Query the full commandline that was used to create a process.
 * This can be helpful to differentiate between service instances (svchost.exe).
 * Taken from: https://stackoverflow.com/a/14012919/1069307
 *
 * \param hProcess A handle to a process.
 *
 * \return A Unicode commandline string, or NULL on error.
 *         The returned string must be freed by the caller.
 */
static PWSTR GetProcessCommandLine(HANDLE hProcess)
{
	PWSTR wcmdline = NULL;
	BOOL wow;
	DWORD pp_offset, cmd_offset;
	NTSTATUS status = STATUS_SUCCESS;
	SYSTEM_INFO si;
	PBYTE peb = NULL, pp = NULL;

	// Determine if 64 or 32-bit processor
	GetNativeSystemInfo(&si);
	if ((si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) || (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)) {
		pp_offset = 0x20;
		cmd_offset = 0x70;
	} else {
		pp_offset = 0x10;
		cmd_offset = 0x40;
	}

	// PEB and Process Parameters (we only need the beginning of these structs)
	peb = (PBYTE)calloc(pp_offset + 8, 1);
	if (peb == NULL)
		goto out;
	pp = (PBYTE)calloc(cmd_offset + 16, 1);
	if (pp == NULL)
		goto out;

	IsWow64Process(GetCurrentProcess(), &wow);
	if (wow) {
		// 32-bit process running on a 64-bit OS
		PROCESS_BASIC_INFORMATION_WOW64 pbi = { 0 };
		ULONGLONG params;
		UNICODE_STRING_WOW64* ucmdline;

		PF_INIT_OR_OUT(NtWow64QueryInformationProcess64, NtDll);
		PF_INIT_OR_OUT(NtWow64ReadVirtualMemory64, NtDll);

		status = pfNtWow64QueryInformationProcess64(hProcess, 0, &pbi, sizeof(pbi), NULL);
		if (!NT_SUCCESS(status))
			goto out;

		status = pfNtWow64ReadVirtualMemory64(hProcess, pbi.PebBaseAddress, peb, pp_offset + 8, NULL);
		if (!NT_SUCCESS(status))
			goto out;

		// Read Process Parameters from the 64-bit address space
		params = (ULONGLONG) *((ULONGLONG*)(peb + pp_offset));
		status = pfNtWow64ReadVirtualMemory64(hProcess, params, pp, cmd_offset + 16, NULL);
		if (!NT_SUCCESS (status))
			goto out;

		ucmdline = (UNICODE_STRING_WOW64*)(pp + cmd_offset);
		wcmdline = (PWSTR)calloc(ucmdline->Length + 1, sizeof(WCHAR));
		if (wcmdline == NULL)
			goto out;
		status = pfNtWow64ReadVirtualMemory64(hProcess, ucmdline->Buffer, wcmdline, ucmdline->Length, NULL);
		if (!NT_SUCCESS(status)) {
			safe_free(wcmdline);
			goto out;
		}
	} else {
		// 32-bit process on a 32-bit OS, or 64-bit process on a 64-bit OS
		PROCESS_BASIC_INFORMATION pbi = { 0 };
		PBYTE* params;
		UNICODE_STRING* ucmdline;

		PF_INIT_OR_OUT(NtQueryInformationProcess, NtDll);

		status = pfNtQueryInformationProcess(hProcess, 0, &pbi, sizeof(pbi), NULL);
		if (!NT_SUCCESS(status))
			goto out;

		// Read PEB
		if (!ReadProcessMemory(hProcess, pbi.PebBaseAddress, peb, pp_offset + 8, NULL))
			goto out;

		// Read Process Parameters
		params = (PBYTE*)*(LPVOID*)(peb + pp_offset);
		if (!ReadProcessMemory(hProcess, params, pp, cmd_offset + 16, NULL))
			goto out;

		ucmdline = (UNICODE_STRING*)(pp + cmd_offset);
		// In the absolute, someone could craft a process with dodgy attributes to try to cause an overflow
		ucmdline->Length = min(ucmdline->Length, 512);
		wcmdline = (PWSTR)calloc(ucmdline->Length + 1, sizeof(WCHAR));
		if (!ReadProcessMemory(hProcess, ucmdline->Buffer, wcmdline, ucmdline->Length, NULL)) {
			safe_free(wcmdline);
			goto out;
		}
	}

out:
	free(peb);
	free(pp);
	return wcmdline;
}


/**
 * The search process thread.
 * Note: Avoid using uprintf statements here, as it may lock the thread.
 *
 * \param param The thread parameters.
 *
 * \return A thread exit code.
 */
static DWORD WINAPI SearchProcessThread(LPVOID param)
{
	BOOL bInitSuccess = FALSE;
	NTSTATUS status = STATUS_SUCCESS;
	PSYSTEM_HANDLE_INFORMATION_EX handles = NULL;
	POBJECT_NAME_INFORMATION buffer = NULL;
	ULONG_PTR i;
	ULONG_PTR pid[2];
	ULONG_PTR last_access_denied_pid = 0;
	ULONG bufferSize;
	wchar_t** wHandleName = NULL;
	USHORT* wHandleNameLen = NULL;
	HANDLE dupHandle = NULL;
	HANDLE processHandle = NULL;
	HANDLE hLock = NULL;
	BOOLEAN bFound = FALSE, bGotCmdLine;
	ULONG access_rights = 0;
	DWORD size;
	wchar_t wexe_path[MAX_PATH], *wcmdline;
	uint64_t start_time;
	char cmdline[MAX_PATH] = { 0 }, tmp[64];
	int cur_pid, j, nHandles = 0;

	PF_INIT_OR_OUT(NtQueryObject, Ntdll);
	PF_INIT_OR_OUT(NtDuplicateObject, NtDll);
	PF_INIT_OR_OUT(NtClose, NtDll);

	// Initialize the blocking process struct
	memset(&blocking_process, 0, sizeof(blocking_process));
	hLock = CreateMutexA(NULL, TRUE, NULL);
	if (hLock == NULL)
		goto out;
	blocking_process.hStart = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (blocking_process.hStart == NULL)
		goto out;
	if (!ReleaseMutex(hLock))
		goto out;
	// Only assign the mutex handle once our init is complete
	blocking_process.hLock = hLock;

	if (!NT_SUCCESS(PhCreateHeap()))
		goto out;

	// Wait until we are signaled active one way or another
	if (!blocking_process.bActive &&
		(WaitForSingleObject(blocking_process.hStart, INFINITE) != WAIT_OBJECT_0)) {
		goto out;
	}

	bInitSuccess = TRUE;
	while (blocking_process.bActive) {
		// Get a lock to our data
		if (WaitForSingleObject(hLock, SEARCH_PROCESS_LOCK_TIMEOUT) != WAIT_OBJECT_0)
			goto out;
		// No handles to check => just sleep for a while
		if (blocking_process.nHandles == 0) {
			ReleaseMutex(hLock);
			Sleep(500);
			continue;
		}
		// Work on our own copy of the handle names so we don't have to hold the
		// mutex for string comparison. Update only if the version has changed.
		if (blocking_process.nVersion[0] != blocking_process.nVersion[1]) {
			assert(blocking_process.wHandleName != NULL && blocking_process.nHandles != 0);
			if (blocking_process.wHandleName == NULL || blocking_process.nHandles == 0) {
				ReleaseMutex(hLock);
				goto out;
			}
			if (wHandleName != NULL) {
				for (j = 0; j < nHandles; j++)
					free(wHandleName[j]);
				free(wHandleName);
			}
			safe_free(wHandleNameLen);
			nHandles = blocking_process.nHandles;
			wHandleName = calloc(nHandles, sizeof(wchar_t*));
			if (wHandleName == NULL) {
				ReleaseMutex(hLock);
				goto out;
			}
			wHandleNameLen = calloc(nHandles, sizeof(USHORT));
			if (wHandleNameLen == NULL) {
				ReleaseMutex(hLock);
				goto out;
			}
			for (j = 0; j < nHandles; j++) {
				wHandleName[j] = wcsdup(blocking_process.wHandleName[j]);
				wHandleNameLen[j] = (USHORT)wcslen(blocking_process.wHandleName[j]);
				if (wHandleName[j] == NULL) {
					ReleaseMutex(hLock);
					goto out;
				}
			}
			blocking_process.nVersion[1] = blocking_process.nVersion[0];
			blocking_process.nPass = 0;
		}
		ReleaseMutex(hLock);

		start_time = GetTickCount64();
		// Get a list of all opened handles
		if (!NT_SUCCESS(PhEnumHandlesEx(&handles))) {
			Sleep(1000);
			continue;
		}

		pid[0] = (ULONG_PTR)0;
		cur_pid = 1;
		bufferSize = 0x200;
		buffer = PhAllocate(bufferSize);
		if (buffer == NULL)
			goto out;

		for (i = 0; blocking_process.bActive; i++) {
			ULONG attempts = 8;
			PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX handleInfo = NULL;

			// We are seeing reports of application crashes due to access
			// violation exceptions here, so, since this is not critical code,
			// we add an exception handler to ignore them.
			TRY_AND_HANDLE(
				EXCEPTION_ACCESS_VIOLATION,
				{ handleInfo = (i < handles->NumberOfHandles) ? &handles->Handles[i] : NULL; },
				{ continue; }
			);

			if ((dupHandle != NULL) && (processHandle != NtCurrentProcess())) {
				TRY_AND_HANDLE(
					EXCEPTION_ACCESS_VIOLATION,
					{ pfNtClose(dupHandle); },
					{ continue; }
				);
				dupHandle = NULL;
			}

			// Update the current handle's process PID and compare against last
			// Note: Be careful about not trying to overflow our list!
			TRY_AND_HANDLE(
				EXCEPTION_ACCESS_VIOLATION,
				{ pid[cur_pid] = (handleInfo != NULL) ? handleInfo->UniqueProcessId : -1; },
				{ continue; }
			);

			if (pid[0] != pid[1]) {
				cur_pid = (cur_pid + 1) % 2;

				// If we're switching process and found a match, store it
				if (bFound) {
					if (WaitForSingleObject(hLock, SEARCH_PROCESS_LOCK_TIMEOUT) == WAIT_OBJECT_0) {
						ProcessEntry* pe = blocking_process.Process;
						// Prune entries that have not been detected for a few passes
						for (j = 0; j < MAX_BLOCKING_PROCESSES; j++)
							if (pe[j].pid != 0 && pe[j].seen_on_pass < blocking_process.nPass - 1)
								pe[j].pid = 0;
						// Try to reuse an existing entry for the current pid
						for (j = 0; (j < MAX_BLOCKING_PROCESSES) && (pe[j].pid != pid[cur_pid]); j++);
						if (j == MAX_BLOCKING_PROCESSES)
							for (j = 0; (j < MAX_BLOCKING_PROCESSES) && (pe[j].pid != 0); j++);
						if (j != MAX_BLOCKING_PROCESSES) {
							pe[j].pid = pid[cur_pid];
							pe[j].access_rights = access_rights & 0x7;
							pe[j].seen_on_pass = blocking_process.nPass;
							static_strcpy(pe[j].cmdline, cmdline);
						} else if (usb_debug) {
							// coverity[dont_call]
							OutputDebugStringA("SearchProcessThread: No empty slot!\n");
						}
						ReleaseMutex(hLock);
					}
					bFound = FALSE;
					access_rights = 0;
				}

				// Close the previous handle
				if (processHandle != NULL) {
					if (processHandle != NtCurrentProcess())
						pfNtClose(processHandle);
					processHandle = NULL;
				}
			}

			// Exit thread condition
			if (!blocking_process.bActive)
				goto out;

			// Exit loop condition
			if (i >= handles->NumberOfHandles)
				break;

			if (handleInfo == NULL)
				continue;

			// Don't bother with processes we can't access
			if (handleInfo->UniqueProcessId == last_access_denied_pid)
				continue;

			// Filter out handles that aren't opened with Read (bit 0), Write (bit 1) or Execute (bit 5) access
			if ((handleInfo->GrantedAccess & 0x23) == 0)
				continue;

			// Open the process to which the handle we are after belongs, if not already opened
			if (pid[0] != pid[1]) {
				status = PhOpenProcess(&processHandle, PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
					(HANDLE)handleInfo->UniqueProcessId);
				// There exists some processes we can't access
				if (!NT_SUCCESS(status)) {
					processHandle = NULL;
					if (status == STATUS_ACCESS_DENIED) {
						last_access_denied_pid = handleInfo->UniqueProcessId;
					}
					continue;
				}
			}

			// Now duplicate this handle onto our own process, so that we can access its properties
			if (processHandle == NtCurrentProcess())
				continue;
			status = pfNtDuplicateObject(processHandle, (HANDLE)handleInfo->HandleValue,
				NtCurrentProcess(), &dupHandle, 0, 0, 0);
			if (!NT_SUCCESS(status))
				continue;

			// Filter non-storage handles. We're not interested in them and they make NtQueryObject() freeze
			if (GetFileType(dupHandle) != FILE_TYPE_DISK)
				continue;

			// A loop is needed because the I/O subsystem likes to give us the wrong return lengths...
			do {
				ULONG returnSize;
				// TODO: We might potentially still need a timeout on ObjectName queries, as PH does...
				status = pfNtQueryObject(dupHandle, ObjectNameInformation, buffer, bufferSize, &returnSize);
				if (status == STATUS_BUFFER_OVERFLOW || status == STATUS_INFO_LENGTH_MISMATCH ||
					status == STATUS_BUFFER_TOO_SMALL) {
					bufferSize = returnSize;
					PhFree(buffer);
					buffer = PhAllocate(bufferSize);
				} else {
					break;
				}
			} while (--attempts);
			if (!NT_SUCCESS(status))
				continue;

			for (j = 0; j < nHandles; j++) {
				// Don't bother comparing if length of our handle string is larger than the current data
				if (wHandleNameLen[j] > buffer->Name.Length)
					continue;
				// Match against our target string(s)
				if (wcsncmp(wHandleName[j], buffer->Name.Buffer, wHandleNameLen[j]) == 0)
					break;
			}
			if (j == nHandles)
				continue;
			bFound = TRUE;

			// Keep a mask of all the access rights being used
			access_rights |= handleInfo->GrantedAccess;
			// The Executable bit is in a place we don't like => reposition it
			if (access_rights & 0x20)
				access_rights = (access_rights & 0x03) | 0x04;
			access_rights &= 0x07;

			// Where possible, try to get the full command line
			bGotCmdLine = FALSE;
			size = MAX_PATH;
			wcmdline = GetProcessCommandLine(processHandle);
			if (wcmdline != NULL) {
				bGotCmdLine = TRUE;
				wchar_to_utf8_no_alloc(wcmdline, cmdline, sizeof(cmdline));
				free(wcmdline);
			}

			// If we couldn't get the full commandline, try to get the executable path
			if (!bGotCmdLine)
				bGotCmdLine = (GetModuleFileNameExU(processHandle, 0, cmdline, MAX_PATH - 1) != 0);

			// The above may not work on all Windows version, so fall back to QueryFullProcessImageName
			if (!bGotCmdLine) {
				bGotCmdLine = (QueryFullProcessImageNameW(processHandle, 0, wexe_path, &size) != FALSE);
				if (bGotCmdLine)
					wchar_to_utf8_no_alloc(wexe_path, cmdline, sizeof(cmdline));
			}

			// Still nothing? Try GetProcessImageFileName. Note that GetProcessImageFileName uses
			// '\Device\Harddisk#\Partition#\' instead drive letters
			if (!bGotCmdLine) {
				bGotCmdLine = (GetProcessImageFileNameW(processHandle, wexe_path, MAX_PATH) != 0);
				if (bGotCmdLine)
					wchar_to_utf8_no_alloc(wexe_path, cmdline, sizeof(cmdline));
			}

			// Complete failure => Just craft a default process name that includes the PID
			if (!bGotCmdLine) {
				static_sprintf(cmdline, "Unknown_Process_%" PRIu64,
					(ULONGLONG)handleInfo->UniqueProcessId);
			}
		}
		PhFree(buffer);
		PhFree(handles);
		// We are the only ones updating the counter so no need for lock
		blocking_process.nPass++;
		// In extended debug mode, notify how much time our search took to the debug facility
		if (usb_debug) {
			static_sprintf(tmp, "Process search run #%d completed in %llu ms\n",
				blocking_process.nPass, GetTickCount64() - start_time);
			// coverity[dont_call]
			OutputDebugStringA(tmp);
		}
		Sleep(1000);
	}

out:
	if (!bInitSuccess)
		uprintf("Warning: Could not start process handle enumerator!");

	if (wHandleName != NULL) {
		for (j = 0; j < nHandles; j++)
			free(wHandleName[j]);
		free(wHandleName);
	}
	safe_free(wHandleNameLen);

	PhDestroyHeap();
	if ((hLock != NULL) && (hLock != INVALID_HANDLE_VALUE) &&
		(WaitForSingleObject(hLock, 1000) == WAIT_OBJECT_0)) {
		blocking_process.hLock = NULL;
		blocking_process.bActive = FALSE;
		for (i = 0; i < blocking_process.nHandles; i++)
			free(blocking_process.wHandleName[i]);
		safe_free(blocking_process.wHandleName);
		safe_closehandle(blocking_process.hStart);
		ReleaseMutex(hLock);
	}
	safe_closehandle(hLock);

	ExitThread(0);
}

/**
 * Start the process search thread.
 *
 * \return TRUE on success, FALSE otherwise.
 *
 */
BOOL StartProcessSearch(void)
{
	int i;

	if (hSearchProcessThread != NULL)
		return TRUE;

	hSearchProcessThread = CreateThread(NULL, 0, SearchProcessThread, NULL, 0, NULL);
	if (hSearchProcessThread == NULL) {
		uprintf("Failed to start process search thread: %s", WindowsErrorString());
		return FALSE;
	}
	SetThreadPriority(SearchProcessThread, THREAD_PRIORITY_LOWEST);

	// Wait until we have hLock
	for (i = 0; (i < 50) && (blocking_process.hLock == NULL); i++)
		Sleep(100);
	if (i >= 50) {
		uprintf("Failed to start process search thread: hLock init failure!");
		TerminateThread(hSearchProcessThread, 0);
		CloseHandle(hSearchProcessThread);
		hSearchProcessThread = NULL;
		return FALSE;
	}

	return TRUE;
}

/**
 * Stop the process search thread..
 *
 */
void StopProcessSearch(void)
{
	if (hSearchProcessThread == NULL)
		return;

	// No need for a lock on this one
	blocking_process.bActive = FALSE;
	if (WaitForSingleObject(hSearchProcessThread, SEARCH_PROCESS_LOCK_TIMEOUT) != WAIT_OBJECT_0) {
		uprintf("Process search thread did not exit within timeout - forcefully terminating it!");
		TerminateThread(hSearchProcessThread, 0);
		CloseHandle(hSearchProcessThread);
	}
	hSearchProcessThread = NULL;
}

/**
 * Set up the handles that the process search will run against.
 *
 * \param DeviceNum The device number for the currently selected drive.
 *
 * \return TRUE on success, FALSE otherwise.
 *
 */
BOOL SetProcessSearch(DWORD DeviceNum)
{
	char* PhysicalPath = NULL, DevPath[MAX_PATH];
	char drive_letter[27], drive_name[] = "?:";
	uint32_t i, nHandles = 0;
	wchar_t** wHandleName = NULL;

	if (hSearchProcessThread == NULL) {
		uprintf("Process search thread is not started!");
		return FALSE;
	}

	assert(blocking_process.hLock != NULL);

	// Populate the handle names
	wHandleName = calloc(MAX_NUM_HANDLES, sizeof(wchar_t*));
	if (wHandleName == NULL)
		return FALSE;
	// Physical drive handle name
	PhysicalPath = GetPhysicalName(DeviceNum);
	if (QueryDosDeviceA(&PhysicalPath[4], DevPath, sizeof(DevPath)) != 0)
		wHandleName[nHandles++] = utf8_to_wchar(DevPath);
	free(PhysicalPath);
	// Logical drive(s) handle name(s)
	GetDriveLetters(DeviceNum, drive_letter);
	for (i = 0; nHandles < MAX_NUM_HANDLES && drive_letter[i]; i++) {
		drive_name[0] = drive_letter[i];
		if (QueryDosDeviceA(drive_name, DevPath, sizeof(DevPath)) != 0)
			wHandleName[nHandles++] = utf8_to_wchar(DevPath);
	}
	if (WaitForSingleObject(blocking_process.hLock, SEARCH_PROCESS_LOCK_TIMEOUT) != WAIT_OBJECT_0) {
		uprintf("Could not obtain process search lock");
		free(wHandleName);
		nHandles = 0;
		return FALSE;
	}

	if (blocking_process.wHandleName != NULL)
		for (i = 0; i < blocking_process.nHandles; i++)
			free(blocking_process.wHandleName[i]);
	free(blocking_process.wHandleName);
	blocking_process.wHandleName = wHandleName;
	blocking_process.nHandles = nHandles;
	blocking_process.nVersion[0]++;
	blocking_process.bActive = TRUE;
	if (!SetEvent(blocking_process.hStart))
		uprintf("Could not signal start event to process search: %s", WindowsErrorString());
	return ReleaseMutex(blocking_process.hLock);
}

/**
 * Check whether the corresponding PID is that of a running process.
 *
 * \param pid The PID of the process to check.
 *
 * \return TRUE if the process is detected as currently running, FALSE otherwise.
 *
 */
static BOOL IsProcessRunning(uint64_t pid)
{
	HANDLE hProcess = NULL;
	DWORD dwExitCode;
	BOOL ret = FALSE;
	NTSTATUS status;

	PF_INIT_OR_OUT(NtClose, NtDll);

	status = PhOpenProcess(&hProcess, PROCESS_QUERY_LIMITED_INFORMATION, (HANDLE)pid);
	if (!NT_SUCCESS(status) || (hProcess == NULL))
		return FALSE;
	if (GetExitCodeProcess(hProcess, &dwExitCode))
		ret = (dwExitCode == STILL_ACTIVE);
	pfNtClose(hProcess);
out:
	return ret;
}

/**
 * Report the result of the process search.
 *
 * \param timeout Maximum time that should be spend in this function before aborting (in ms).
 * \param access_mask Desired access mask (x = 0x4, w = 0x2, r = 0x1).
 * \param bIgnoreStaleProcesses Whether to ignore processes that are no longer active.
 *
 * \return The combined access mask of all the matching processes found.
 *         The BlockingProcessList string array is also updated with the results.
 *
 */
BYTE GetProcessSearch(uint32_t timeout, uint8_t access_mask, BOOL bIgnoreStaleProcesses)
{
	const char* access_rights_str[8] = { "n", "r", "w", "rw", "x", "rx", "wx", "rwx" };
	char tmp[MAX_PATH];
	int i, j;
	uint32_t elapsed = 0;
	BYTE returned_mask = 0;

	StrArrayClear(&BlockingProcessList);
	if (hSearchProcessThread == NULL) {
		uprintf("Process search thread is not started!");
		return 0;
	}

	assert(blocking_process.hLock != NULL);
	if (blocking_process.hLock == NULL)
		return 0;

retry:
	if (WaitForSingleObject(blocking_process.hLock, SEARCH_PROCESS_LOCK_TIMEOUT) != WAIT_OBJECT_0)
		return 0;

	// Make sure we have at least one pass with the current handles in order to report them.
	// If we have a timeout, wait until timeout has elapsed to give up.
	if ((blocking_process.nVersion[0] != blocking_process.nVersion[1]) ||
		(blocking_process.nPass < 1)) {
		ReleaseMutex(blocking_process.hLock);
		if (elapsed < timeout) {
			Sleep(100);
			elapsed += 100;
			goto retry;
		}
		if (timeout != 0)
			uprintf("Timeout while retrieving conflicting process list");
		return 0;
	}

	for (i = 0, j = 0; i < MAX_BLOCKING_PROCESSES; i++) {
		if (blocking_process.Process[i].pid == 0)
			continue;
		if ((blocking_process.Process[i].access_rights & access_mask) == 0)
			continue;
		if (bIgnoreStaleProcesses && !IsProcessRunning(blocking_process.Process[i].pid))
			continue;
		returned_mask |= blocking_process.Process[i].access_rights;
		static_sprintf(tmp, "● [%llu] %s (%s)", blocking_process.Process[i].pid, blocking_process.Process[i].cmdline,
			access_rights_str[blocking_process.Process[i].access_rights & 0x7]);
		StrArrayAdd(&BlockingProcessList, tmp, TRUE);
		if (j++ == 0)
			uprintf("WARNING: The following application(s) or service(s) are accessing the drive:");
		// tmp may contain a '%' so don't feed it as a naked format string
		uprintf("%s", tmp);
	}
	if (j != 0)
		uprintf("You should close these applications before retrying the operation.");
	ReleaseMutex(blocking_process.hLock);

	return returned_mask & access_mask;
}

/**
 * Alternative search for processes keeping a handle on a specific disk or volume
 * Note that this search requires opening the disk or volume, which may not always
 * be convenient for our usage (since we might be looking for processes preventing
 * us to open said target in exclusive mode).
 *
 * At least on Windows 11, this no longer seems to work as querying a logical or
 * physical volume seems to return almost ALL the processes that are running,
 * including the ones that are not actually accessing the handle.
 *
 * \param HandleName The name of the handle to look for.
 *
 * \return TRUE if processes were found, FALSE otherwise.
 */
BOOL SearchProcessAlt(char* HandleName)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG i;
	HANDLE searchHandle = NULL;
	BOOLEAN bFound = FALSE;
	PFILE_PROCESS_IDS_USING_FILE_INFORMATION info = NULL;

	status = PhCreateHeap();
	if (!NT_SUCCESS(status))
		goto out;

	// Note that the access rights being used with CreateFile() might matter...
	searchHandle = CreateFileA(HandleName, FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	status = PhQueryProcessesUsingVolumeOrFile(searchHandle, &info);

	if (NT_SUCCESS(status) && (info->NumberOfProcessIdsInList > 0)) {
		bFound = TRUE;
		uprintf("WARNING: The following process(es) or service(s) are accessing %s:", HandleName);
		for (i = 0; i < info->NumberOfProcessIdsInList; i++) {
			uprintf("o Process with PID %llu", (uint64_t)info->ProcessIdList[i]);
		}
	}

out:
	safe_closehandle(searchHandle);
	PhFree(info);
	PhDestroyHeap();
	if (!NT_SUCCESS(status))
		uprintf("SearchProcessAlt('%s') failed: %s", HandleName, NtStatusError(status));
	return bFound;
}

/**
 * Increase the privileges of the current application.
 *
 * \return TRUE if the request was successful.
 */
BOOL EnablePrivileges(void)
{
	// List of the privileges we require. A list of requestable privileges can
	// be obtained at https://technet.microsoft.com/en-us/library/dn221963.aspx
	const DWORD requestedPrivileges[] = {
		SE_DEBUG_PRIVILEGE,
	};
	NTSTATUS status = STATUS_NOT_IMPLEMENTED;
	HANDLE tokenHandle;

	PF_INIT_OR_OUT(NtClose, NtDll);
	PF_INIT_OR_OUT(NtOpenProcessToken, NtDll);
	PF_INIT_OR_OUT(NtAdjustPrivilegesToken, NtDll);

	status = pfNtOpenProcessToken(NtCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tokenHandle);

	if (NT_SUCCESS(status)) {
		CHAR privilegesBuffer[FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) +
			sizeof(LUID_AND_ATTRIBUTES) * ARRAYSIZE(requestedPrivileges)] = { 0 };
		PTOKEN_PRIVILEGES privileges;
		ULONG i;

		privileges = (PTOKEN_PRIVILEGES)privilegesBuffer;
		privileges->PrivilegeCount = ARRAYSIZE(requestedPrivileges);

		for (i = 0; i < privileges->PrivilegeCount; i++) {
			privileges->Privileges[i].Attributes = SE_PRIVILEGE_ENABLED;
			privileges->Privileges[i].Luid.HighPart = 0;
			privileges->Privileges[0].Luid.LowPart = requestedPrivileges[i];
		}

		status = pfNtAdjustPrivilegesToken(tokenHandle, FALSE, privileges, 0, NULL, NULL);

		pfNtClose(tokenHandle);
	}

out:
	if (!NT_SUCCESS(status))
		ubprintf("NOTE: Could not set process privileges: %s", NtStatusError(status));
	return NT_SUCCESS(status);
}
