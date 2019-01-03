/*
 * Rufus: The Reliable USB Formatting Utility
 * Process search functionality
 *
 * Modified from Process Hacker:
 *   https://github.com/processhacker2/processhacker2/
 * Copyright © 2017-2019 Pete Batard <pete@akeo.ie>
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

#include "rufus.h"
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
PF_TYPE_DECL(NTAPI, NTSTATUS, NtWow64ReadVirtualMemory64, (HANDLE, PVOID64, PVOID, ULONG64, PULONG64));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryObject, (HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtDuplicateObject, (HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcess, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, CLIENT_ID*));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcessToken, (HANDLE, ACCESS_MASK, PHANDLE));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtAdjustPrivilegesToken, (HANDLE, BOOLEAN, PTOKEN_PRIVILEGES, ULONG, PTOKEN_PRIVILEGES, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtClose, (HANDLE));

// This one is only available on Vista or later...
PF_TYPE_DECL(WINAPI, BOOL, QueryFullProcessImageNameW, (HANDLE, DWORD, LPWSTR, PDWORD));

static PVOID PhHeapHandle = NULL;
static char* _HandleName;
static BOOL _bPartialMatch, _bIgnoreSelf, _bQuiet;
static BYTE access_mask;
extern StrArray BlockingProcess;

/*
 * Convert an NT Status to an error message
 *
 * \param Status An operattonal status.
 *
 * \return An error message string.
 *
 */
static char* NtStatusError(NTSTATUS Status) {
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
 *
 */
static PVOID PhAllocate(SIZE_T Size)
{
	PF_INIT(RtlAllocateHeap, Ntdll);
	if (pfRtlAllocateHeap == NULL)
		return NULL;

	return pfRtlAllocateHeap(PhHeapHandle, 0, Size);
}

/**
 * Frees a block of memory allocated with PhAllocate().
 *
 * \param Memory A pointer to a block of memory.
 *
 */
static VOID PhFree(PVOID Memory)
{
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
		PVOID64 params;
		UNICODE_STRING_WOW64* ucmdline;

		PF_INIT_OR_OUT(NtWow64QueryInformationProcess64, NtDll);
		PF_INIT_OR_OUT(NtWow64ReadVirtualMemory64, NtDll);

		status = pfNtWow64QueryInformationProcess64(hProcess, 0, &pbi, sizeof(pbi), NULL);
		if (!NT_SUCCESS (status))
			goto out;

		status = pfNtWow64ReadVirtualMemory64(hProcess, pbi.PebBaseAddress, peb, pp_offset + 8, NULL);
		if (!NT_SUCCESS (status))
			goto out;

		// Read Process Parameters from the 64-bit address space
		params = (PVOID64) *((PVOID64*)(peb + pp_offset));
		status = pfNtWow64ReadVirtualMemory64(hProcess, params, pp, cmd_offset + 16, NULL);
		if (!NT_SUCCESS (status))
			goto out;

		ucmdline = (UNICODE_STRING_WOW64*)(pp + cmd_offset);
		wcmdline = (PWSTR)calloc(ucmdline->Length + 1, sizeof(WCHAR));
		if (wcmdline == NULL)
			goto out;
		status = pfNtWow64ReadVirtualMemory64(hProcess, ucmdline->Buffer, wcmdline, ucmdline->Length, NULL);
		if (!NT_SUCCESS (status)) {
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
		if (!NT_SUCCESS (status))
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

static DWORD WINAPI SearchProcessThread(LPVOID param)
{
	const char *access_rights_str[8] = { "n", "r", "w", "rw", "x", "rx", "wx", "rwx" };
	char tmp[MAX_PATH];
	NTSTATUS status = STATUS_SUCCESS;
	PSYSTEM_HANDLE_INFORMATION_EX handles = NULL;
	POBJECT_NAME_INFORMATION buffer = NULL;
	ULONG_PTR i;
	ULONG_PTR pid[2];
	ULONG_PTR last_access_denied_pid = 0;
	ULONG bufferSize;
	USHORT wHandleNameLen;
	WCHAR *wHandleName = NULL;
	HANDLE dupHandle = NULL;
	HANDLE processHandle = NULL;
	BOOLEAN bFound = FALSE, bGotCmdLine = FALSE, verbose = !_bQuiet;
	ULONG access_rights = 0;
	DWORD size;
	char cmdline[MAX_PATH] = { 0 };
	wchar_t wexe_path[MAX_PATH], *wcmdline;
	int cur_pid;

	PF_INIT_OR_SET_STATUS(NtQueryObject, Ntdll);
	PF_INIT_OR_SET_STATUS(NtDuplicateObject, NtDll);
	PF_INIT_OR_SET_STATUS(NtClose, NtDll);

	StrArrayClear(&BlockingProcess);

	if (NT_SUCCESS(status))
		status = PhCreateHeap();

	if (NT_SUCCESS(status))
		status = PhEnumHandlesEx(&handles);

	if (!NT_SUCCESS(status)) {
		uprintf("Warning: Could not enumerate process handles: %s", NtStatusError(status));
		goto out;
	}

	pid[0] = (ULONG_PTR)0;
	cur_pid = 1;

	wHandleName = utf8_to_wchar(_HandleName);
	wHandleNameLen = (USHORT)wcslen(wHandleName);

	bufferSize = 0x200;
	buffer = PhAllocate(bufferSize);
	if (buffer == NULL)
		goto out;

	for (i = 0; ; i++) {
		ULONG attempts = 8;
		PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX handleInfo =
			(i < handles->NumberOfHandles) ? &handles->Handles[i] : NULL;

		if ((dupHandle != NULL) && (processHandle != NtCurrentProcess())) {
			pfNtClose(dupHandle);
			dupHandle = NULL;
		}

		// Update the current handle's process PID and compare against last
		// Note: Be careful about not trying to overflow our list!
		pid[cur_pid] = (handleInfo != NULL) ? handleInfo->UniqueProcessId : -1;

		if (pid[0] != pid[1]) {
			cur_pid = (cur_pid + 1) % 2;

			// If we're switching process and found a match, print it
			if (bFound) {
				static_sprintf (tmp, "● [%06u] %s (%s)", (uint32_t)pid[cur_pid], cmdline, access_rights_str[access_rights & 0x7]);
				vuprintf(tmp);
				StrArrayAdd(&BlockingProcess, tmp, TRUE);
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

		CHECK_FOR_USER_CANCEL;

		// Exit loop condition
		if (i >= handles->NumberOfHandles)
			break;

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
				uuprintf("SearchProcess: Could not open process %ld: %s",
					handleInfo->UniqueProcessId, NtStatusError(status));
				processHandle = NULL;
				if (status == STATUS_ACCESS_DENIED) {
					last_access_denied_pid = handleInfo->UniqueProcessId;
				}
				continue;
			}
		}

		// Now duplicate this handle onto our own process, so that we can access its properties
		if (processHandle == NtCurrentProcess()) {
			if (_bIgnoreSelf)
				continue;
			dupHandle = (HANDLE)handleInfo->HandleValue;
		} else {
			status = pfNtDuplicateObject(processHandle, (HANDLE)handleInfo->HandleValue,
				NtCurrentProcess(), &dupHandle, 0, 0, 0);
			if (!NT_SUCCESS(status))
				continue;
		}

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
				uuprintf("SearchProcess: Realloc from %d to %d", bufferSize, returnSize);
				bufferSize = returnSize;
				PhFree(buffer);
				buffer = PhAllocate(bufferSize);
			} else {
				break;
			}
		} while (--attempts);
		if (!NT_SUCCESS(status)) {
			uuprintf("SearchProcess: NtQueryObject failed for handle %X of process %ld: %s",
				handleInfo->HandleValue, handleInfo->UniqueProcessId, NtStatusError(status));
			continue;
		}

		// Don't bother comparing if we are looking for full match and the length is different
		if ((!_bPartialMatch) && (wHandleNameLen != buffer->Name.Length))
			continue;

		// Likewise, if we are looking for a partial match and the current length is smaller
		if ((_bPartialMatch) && (wHandleNameLen > buffer->Name.Length))
			continue;

		// Match against our target string
		if (wcsncmp(wHandleName, buffer->Name.Buffer, wHandleNameLen) != 0)
			continue;

		// If we are here, we have a process accessing our target!
		bFound = TRUE;

		// Keep a mask of all the access rights being used
		access_rights |= handleInfo->GrantedAccess;
		// The Executable bit is in a place we don't like => reposition it
		if (access_rights & 0x20)
			access_rights = (access_rights & 0x03) | 0x04;
		access_mask |= (BYTE) (access_rights & 0x7) + 0x80;	// Bit 7 is always set if a process was found

		// If this is the very first process we find, print a header
		if (cmdline[0] == 0)
			vuprintf("WARNING: The following process(es) or service(s) are accessing %s:", _HandleName);

		// Where possible, try to get the full command line
		wcmdline = GetProcessCommandLine(processHandle);
		if (wcmdline != NULL) {
			bGotCmdLine = TRUE;
			wchar_to_utf8_no_alloc(wcmdline, cmdline, sizeof(cmdline));
			free(wcmdline);
		}

		// If we couldn't get the full commandline, try to get the executable path
		if (!bGotCmdLine)
			bGotCmdLine = (GetModuleFileNameExU(processHandle, 0, cmdline, MAX_PATH - 1) != 0);

		// The above may not work on Windows 7, so try QueryFullProcessImageName (Vista or later)
		if (!bGotCmdLine) {
			size = MAX_PATH;
			PF_INIT(QueryFullProcessImageNameW, kernel32);
			if ( (pfQueryFullProcessImageNameW != NULL) &&
				 (bGotCmdLine = pfQueryFullProcessImageNameW(processHandle, 0, wexe_path, &size)) )
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

out:
	if (cmdline[0] != 0)
		vuprintf("You should close these applications before attempting to reformat the drive.");
	else
		vuprintf("NOTE: Could not identify the process(es) or service(s) accessing %s", _HandleName);

	free(wHandleName);
	PhFree(buffer);
	PhFree(handles);
	PhDestroyHeap();
	ExitThread(0);
}

/**
 * Search all the processes and list the ones that have a specific handle open.
 *
 * \param HandleName The name of the handle to look for.
 * \param dwTimeOut The maximum amounf of time (ms) that may be spent searching
 * \param bPartialMatch Whether partial matches should be allowed.
 * \param bIgnoreSelf Whether the current process should be listed.
 * \param bQuiet Prints minimal output.
 *
 * \return a byte containing the cummulated access rights (f----xwr) from all the handles found
 *         with bit 7 ('f') also set if at least one process was found.
 */
BYTE SearchProcess(char* HandleName, DWORD dwTimeOut, BOOL bPartialMatch, BOOL bIgnoreSelf, BOOL bQuiet)
{
	HANDLE handle;
	DWORD res = 0;

	_HandleName = HandleName;
	_bPartialMatch = bPartialMatch;
	_bIgnoreSelf = bIgnoreSelf;
	_bQuiet = bQuiet;
	access_mask = 0;

	handle = CreateThread(NULL, 0, SearchProcessThread, NULL, 0, NULL);
	if (handle == NULL) {
		uprintf("Warning: Unable to create conflicting process search thread");
		return 0x00;
	}
	res = WaitForSingleObjectWithMessages(handle, dwTimeOut);
	if (res == WAIT_TIMEOUT) {
		// Timeout - kill the thread
		TerminateThread(handle, 0);
		uprintf("Warning: Conflicting process search failed to complete due to timeout");
	} else if (res != WAIT_OBJECT_0) {
		TerminateThread(handle, 0);
		uprintf("Warning: Failed to wait for conflicting process search thread: %s", WindowsErrorString());
	}
	return access_mask;
}

/**
 * Alternative search for processes keeping a handle on a specific disk or volume
 * Note that this search requires opening the disk or volume, which may not always
 * be convenient for our usage (since we might be looking for processes preventing
 * us to open said target in exclusive mode).
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
	searchHandle = CreateFileA(HandleName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	
	status = PhQueryProcessesUsingVolumeOrFile(searchHandle, &info);

	if (NT_SUCCESS(status) && (info->NumberOfProcessIdsInList > 0)) {
		bFound = TRUE;
		uprintf("WARNING: The following process(es) or service(s) are accessing %s:", HandleName);
		for (i = 0; i < info->NumberOfProcessIdsInList; i++) {
			uprintf("o Process with PID %ld", info->ProcessIdList[i]);
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
			sizeof(LUID_AND_ATTRIBUTES) * ARRAYSIZE(requestedPrivileges)];
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
