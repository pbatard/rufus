/*
 * Rufus: The Reliable USB Formatting Utility
 * Process search functionality
 *
 * Modified from Process Hacker:
 *   https://github.com/processhacker2/processhacker2/
 * Copyright © 2009-2016 wj32
 * Copyright © 2017 dmex
 * Copyright © 2017 Pete Batard <pete@akeo.ie>
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

// Process Hacker does some filtering using Object Types, but this doesn't help us.
// Keep this option, just in case.
// #define USE_OBJECT_TYPES

PF_TYPE_DECL(NTAPI, PVOID, RtlCreateHeap, (ULONG, PVOID, SIZE_T, SIZE_T, PVOID, PRTL_HEAP_PARAMETERS));
PF_TYPE_DECL(NTAPI, PVOID, RtlDestroyHeap, (PVOID));
PF_TYPE_DECL(NTAPI, PVOID, RtlAllocateHeap, (PVOID, ULONG, SIZE_T));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlFreeHeap, (PVOID, ULONG, PVOID));
#ifdef USE_OBJECT_TYPES
PF_TYPE_DECL(NTAPI, VOID, RtlInitUnicodeString, (PUNICODE_STRING, PCWSTR));
PF_TYPE_DECL(NTAPI, BOOLEAN, RtlEqualUnicodeString, (PCUNICODE_STRING, PCUNICODE_STRING, BOOLEAN));
#endif

PF_TYPE_DECL(NTAPI, NTSTATUS, NtQuerySystemInformation, (SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryInformationFile, (HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtQueryObject, (HANDLE, OBJECT_INFORMATION_CLASS, PVOID, ULONG, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtDuplicateObject, (HANDLE, HANDLE, HANDLE, PHANDLE, ACCESS_MASK, ULONG, ULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcess, (PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtOpenProcessToken, (HANDLE, ACCESS_MASK, PHANDLE));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtAdjustPrivilegesToken, (HANDLE, BOOLEAN, PTOKEN_PRIVILEGES, ULONG, PTOKEN_PRIVILEGES, PULONG));
PF_TYPE_DECL(NTAPI, NTSTATUS, NtClose, (HANDLE));

static PVOID PhHeapHandle = NULL;

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
		safe_sprintf(unknown, sizeof(unknown), "Unknown error 0x%08lx", Status);
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

#ifdef USE_OBJECT_TYPES
NTSTATUS PhEnumObjectTypes(POBJECT_TYPES_INFORMATION *ObjectTypes)
{
	NTSTATUS status = STATUS_SUCCESS;
	PVOID buffer;
	ULONG bufferSize;
	ULONG returnLength;

	PF_INIT_OR_SET_STATUS(NtQueryObject, Ntdll);
	if (!NT_SUCCESS(status))
		return status;

	bufferSize = 0x1000;
	buffer = PhAllocate(bufferSize);

	while ((status = pfNtQueryObject(NULL, ObjectTypesInformation, buffer, bufferSize, &returnLength)) == STATUS_INFO_LENGTH_MISMATCH) {
		PhFree(buffer);
		bufferSize *= 2;

		// Fail if we're resizing the buffer to something very large.
		if (bufferSize > PH_LARGE_BUFFER_SIZE)
			return STATUS_INSUFFICIENT_RESOURCES;

		buffer = PhAllocate(bufferSize);
	}

	if (!NT_SUCCESS(status)) {
		PhFree(buffer);
		return status;
	}

	*ObjectTypes = (POBJECT_TYPES_INFORMATION)buffer;

	return status;
}

ULONG PhGetObjectTypeNumber(PUNICODE_STRING TypeName)
{
	NTSTATUS status = STATUS_SUCCESS;
	POBJECT_TYPES_INFORMATION objectTypes;
	POBJECT_TYPE_INFORMATION objectType;
	ULONG objectIndex = -1;
	ULONG i;

	PF_INIT_OR_SET_STATUS(RtlEqualUnicodeString, NtDll);
	if (!NT_SUCCESS(status))
		return -1;

	status = PhEnumObjectTypes(&objectTypes);
	if (NT_SUCCESS(status)) {
		objectType = PH_FIRST_OBJECT_TYPE(objectTypes);

		for (i = 0; i < objectTypes->NumberOfTypes; i++) {
			if (pfRtlEqualUnicodeString(&objectType->TypeName, TypeName, TRUE)) {
				if (nWindowsVersion >= WINDOWS_8_1) {
					objectIndex = objectType->TypeIndex;
					break;
				} else if (nWindowsVersion >= WINDOWS_7) {
					objectIndex = i + 2;
					break;
				} else {
					objectIndex = i + 1;
					break;
				}
			}

			objectType = PH_NEXT_OBJECT_TYPE(objectType);
		}

		PhFree(objectTypes);
	}

	return objectIndex;
}
#endif

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
 * Search all the processes and list the ones that have a specific handle open.
 *
 * \param HandleName The name of the handle to look for.
 * \param bPartialMatch Whether partial matches should be allowed.
 * \param bIgnoreSelf Whether the current process should be listed.
 *
 * \return TRUE if matching processes were found, FALSE otherwise.
 */
BOOL SearchProcess(char* HandleName, BOOL bPartialMatch, BOOL bIgnoreSelf)
{
	const char *access_rights_str[8] = { "n", "r", "w", "rw", "x", "rx", "wx", "rwx" };
	NTSTATUS status = STATUS_SUCCESS;
	PSYSTEM_HANDLE_INFORMATION_EX handles = NULL;
	POBJECT_NAME_INFORMATION buffer = NULL;
#ifdef USE_OBJECT_TYPES
	UNICODE_STRING fileTypeName;
	ULONG fileObjectTypeIndex = -1;
#endif
	ULONG_PTR i;
	ULONG_PTR pid[2];
	ULONG_PTR last_access_denied_pid = 0;
	ULONG bufferSize;
	USHORT wHandleNameLen;
	WCHAR *wHandleName = NULL;
	HANDLE dupHandle = NULL;
	HANDLE processHandle = NULL;
	BOOLEAN bFound = FALSE;
	ULONG access_rights = 0;
	char exe_path[MAX_PATH] = { 0 };
	int cur_pid;

	PF_INIT_OR_SET_STATUS(NtQueryObject, Ntdll);
	PF_INIT_OR_SET_STATUS(NtDuplicateObject, NtDll);
	PF_INIT_OR_SET_STATUS(NtClose, NtDll);
#ifdef USE_OBJECT_TYPES
	PF_INIT_OR_SET_STATUS(RtlInitUnicodeString, NtDll);
#endif

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

	wHandleName = utf8_to_wchar(HandleName);
	wHandleNameLen = (USHORT)wcslen(wHandleName);

	bufferSize = 0x200;
	buffer = PhAllocate(bufferSize);
	if (buffer == NULL)
		goto out;

#ifdef USE_OBJECT_TYPES
	pfRtlInitUnicodeString(&fileTypeName, L"File");
	fileObjectTypeIndex = PhGetObjectTypeNumber(&fileTypeName);
	if (fileObjectTypeIndex < 0)
		uprintf("Warning: Could not get Object Index for file types");
#endif

	for (i = 0; ; i++) {
		ULONG attempts = 8;
		PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX handleInfo =
			(i < handles->NumberOfHandles) ? &handles->Handles[i] : NULL;

		if ((dupHandle != NULL) && (processHandle != NtCurrentProcess())) {
			pfNtClose(dupHandle);
			dupHandle = NULL;
		}

#ifdef USE_OBJECT_TYPES
		// Only look for File objects type
		if ((fileObjectTypeIndex >= 0 ) && (handleInfo != NULL) &&
			(handleInfo->ObjectTypeIndex != (USHORT)fileObjectTypeIndex))
			continue;
#endif

		// Update the current handle's process PID and compare against last
		// Note: Be careful about not trying to overflow our list!
		pid[cur_pid] = (handleInfo != NULL) ? handleInfo->UniqueProcessId : -1;

		if (pid[0] != pid[1]) {
			cur_pid = (cur_pid + 1) % 2;

			// If we're switching process and found a match, print it
			if (bFound) {
				uprintf("o '%s' (pid: %ld, access: %s)", exe_path, pid[cur_pid], access_rights_str[access_rights & 0x7]);
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
			status = PhOpenProcess(&processHandle, PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION,
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
			if (bIgnoreSelf)
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
		if ((!bPartialMatch) && (wHandleNameLen != buffer->Name.Length))
			continue;

		// Likewise, if we are looking for a partial match and the current length is smaller
		if ((bPartialMatch) && (wHandleNameLen > buffer->Name.Length))
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
			access_rights = (access_rights & 0x3) | 0x4;

		// If this is the very first process we find, print a header
		if (exe_path[0] == 0)
			uprintf("WARNING: The following process(es) or service(s) are accessing %s:", HandleName);

		if (!GetModuleFileNameExU(processHandle, 0, exe_path, MAX_PATH - 1))
			safe_sprintf(exe_path, MAX_PATH, "Unknown_Process_%" PRIu64,
				(ULONGLONG) handleInfo->UniqueProcessId);
	}

out:
	if (exe_path[0] != 0)
		uprintf("You should try to close these applications before attempting to reformat the drive.");
	else
		uprintf("NOTE: Could not identify the process(es) or service(s) accessing %s", HandleName);

	free(wHandleName);
	PhFree(buffer);
	PhFree(handles);
	PhDestroyHeap();
	return bFound;
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
