/*
 * win32_common.c - Windows code common to applying and capturing images.
 */

/*
 * Copyright (C) 2013-2016 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef _WIN32

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/win32_common.h"

#include "wimlib/assert.h"
#include "wimlib/error.h"
#include "wimlib/util.h"
#include "wimlib/win32_vss.h"

static bool
win32_modify_privilege(const wchar_t *privilege, bool enable)
{
	HANDLE hToken;
	LUID luid;
	TOKEN_PRIVILEGES newState = { 0 };
	bool ret = FALSE;

	if (!OpenProcessToken(GetCurrentProcess(),
			      TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
			      &hToken))
		goto out;

	if (!LookupPrivilegeValue(NULL, privilege, &luid))
		goto out_close_handle;

	newState.PrivilegeCount = 1;
	newState.Privileges[0].Luid = luid;
	newState.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : 0);
	SetLastError(ERROR_SUCCESS);
	ret = AdjustTokenPrivileges(hToken, FALSE, &newState, 0, NULL, NULL);
	if (ret && GetLastError() == ERROR_NOT_ALL_ASSIGNED)
		ret = FALSE;
out_close_handle:
	CloseHandle(hToken);
out:
	return ret;
}

static bool
win32_modify_capture_privileges(bool enable)
{
	bool ok = true;
	ok &= win32_modify_privilege(SE_BACKUP_NAME, enable);
	ok &= win32_modify_privilege(SE_SECURITY_NAME, enable);
	return ok;
}

static bool
win32_modify_apply_privileges(bool enable)
{
	bool ok = true;
	ok &= win32_modify_privilege(SE_RESTORE_NAME, enable);
	ok &= win32_modify_privilege(SE_SECURITY_NAME, enable);
	ok &= win32_modify_privilege(SE_TAKE_OWNERSHIP_NAME, enable);
	ok &= win32_modify_privilege(SE_MANAGE_VOLUME_NAME, enable);
	return ok;
}

static void
win32_release_capture_and_apply_privileges(void)
{
	win32_modify_capture_privileges(false);
	win32_modify_apply_privileges(false);
}

/* Pointers to dynamically loaded functions  */

NTSTATUS (WINAPI *func_RtlDosPathNameToNtPathName_U_WithStatus)
		(IN PCWSTR DosName,
		 OUT PUNICODE_STRING NtName,
		 OUT PCWSTR *PartName,
		 OUT PRTL_RELATIVE_NAME_U RelativeName) = NULL;

BOOLEAN (WINAPI *func_RtlDosPathNameToNtPathName_U)
		(IN PCWSTR DosName,
		 OUT PUNICODE_STRING NtName,
		 OUT PCWSTR* PartName,
		 OUT PRTL_RELATIVE_NAME_U RelativeName) = NULL;

NTSTATUS (WINAPI *func_RtlCreateSystemVolumeInformationFolder)
		(PCUNICODE_STRING VolumeRootPath) = NULL;

static bool acquired_privileges = false;

static HMODULE ntdll_handle = NULL;

static int
init_ntdll(void)
{
	ntdll_handle = LoadLibrary(L"ntdll.dll");

	if (!ntdll_handle) {
		ERROR("Unable to load ntdll.dll");
		return WIMLIB_ERR_UNSUPPORTED;
	}

	func_RtlDosPathNameToNtPathName_U_WithStatus =
		(void *)GetProcAddress(ntdll_handle,
				       "RtlDosPathNameToNtPathName_U_WithStatus");

	func_RtlDosPathNameToNtPathName_U =
		(void*)GetProcAddress(ntdll_handle,
			           "RtlDosPathNameToNtPathName_U");

	func_RtlCreateSystemVolumeInformationFolder =
		(void *)GetProcAddress(ntdll_handle,
				       "RtlCreateSystemVolumeInformationFolder");

	return 0;
}

/* One-time initialization for Windows capture/apply code.  */
int
win32_global_init(int init_flags)
{
	int ret;

	/* Try to acquire useful privileges.  */
	if (!(init_flags & WIMLIB_INIT_FLAG_DONT_ACQUIRE_PRIVILEGES)) {
		ret = WIMLIB_ERR_INSUFFICIENT_PRIVILEGES;
		if (!win32_modify_capture_privileges(true))
			if (init_flags & WIMLIB_INIT_FLAG_STRICT_CAPTURE_PRIVILEGES)
				goto out_drop_privs;
		if (!win32_modify_apply_privileges(true))
			if (init_flags & WIMLIB_INIT_FLAG_STRICT_APPLY_PRIVILEGES)
				goto out_drop_privs;
		acquired_privileges = true;
	}

	ret = init_ntdll();
	if (ret)
		goto out_drop_privs;

	return 0;

out_drop_privs:
	win32_release_capture_and_apply_privileges();
	return ret;
}

void
win32_global_cleanup(void)
{
	vss_global_cleanup();

	if (acquired_privileges)
		win32_release_capture_and_apply_privileges();

	FreeLibrary(ntdll_handle);
	ntdll_handle = NULL;
}

/*
 * Translates a Win32-namespace path into an NT-namespace path.
 *
 * On success, returns 0.  The NT-namespace path will be stored in the
 * UNICODE_STRING structure pointed to by nt_path.  nt_path->Buffer will be set
 * to a new buffer that must later be freed with HeapFree().  (Really
 * RtlHeapFree(), but HeapFree() seems to be the same thing.)
 *
 * On failure, returns WIMLIB_ERR_NOMEM or WIMLIB_ERR_INVALID_PARAM.
 */
int
win32_path_to_nt_path(const wchar_t *win32_path, UNICODE_STRING *nt_path)
{
	NTSTATUS status;

	if (func_RtlDosPathNameToNtPathName_U_WithStatus) {
		status = (*func_RtlDosPathNameToNtPathName_U_WithStatus)(win32_path,
									 nt_path,
									 NULL, NULL);
	} else if (func_RtlDosPathNameToNtPathName_U) {
		if ((*func_RtlDosPathNameToNtPathName_U)(win32_path, nt_path, NULL, NULL))
			status = STATUS_SUCCESS;
		else
			status = STATUS_NO_MEMORY;
	} else {
		winnt_error(STATUS_UNSUCCESSFUL, L"RtlDosPathNameToNtPathName_U functions not found");
		return WIMLIB_ERR_RESOURCE_NOT_FOUND;
	}

	if (likely(NT_SUCCESS(status)))
		return 0;

	if (status == STATUS_NO_MEMORY)
		return WIMLIB_ERR_NOMEM;

	winnt_error(status, L"\"%ls\": invalid path name", win32_path);
	return WIMLIB_ERR_INVALID_PARAM;
}

int
win32_get_drive_path(const wchar_t *file_path, wchar_t drive_path[7])
{
	tchar *file_abspath;

	file_abspath = realpath(file_path, NULL);
	if (!file_abspath)
		return WIMLIB_ERR_NOMEM;

	if (file_abspath[0] == L'\0' || file_abspath[1] != L':') {
		ERROR("\"%ls\": Path format not recognized", file_abspath);
		FREE(file_abspath);
		return WIMLIB_ERR_UNSUPPORTED;
	}

	wsprintf(drive_path, L"\\\\.\\%lc:", file_abspath[0]);
	FREE(file_abspath);
	return 0;
}

/* Try to attach an instance of the Windows Overlay Filesystem filter driver to
 * the specified drive (such as C:)  */
bool
win32_try_to_attach_wof(const wchar_t *drive)
{
	HMODULE fltlib;
	bool retval = false;

	/* Use FilterAttach() from Fltlib.dll.  */

	fltlib = LoadLibrary(L"Fltlib.dll");

	if (!fltlib) {
		WARNING("Failed to load Fltlib.dll");
		return retval;
	}

	HRESULT (WINAPI *func_FilterAttach)(LPCWSTR lpFilterName,
					    LPCWSTR lpVolumeName,
					    LPCWSTR lpInstanceName,
					    DWORD dwCreatedInstanceNameLength,
					    LPWSTR lpCreatedInstanceName);

	func_FilterAttach = (void *)GetProcAddress(fltlib, "FilterAttach");

	if (func_FilterAttach) {
		HRESULT res;

		res = (*func_FilterAttach)(L"wof", drive, NULL, 0, NULL);

		if (res != S_OK)
			res = (*func_FilterAttach)(L"wofadk", drive, NULL, 0, NULL);

		if (res == S_OK)
			retval = true;
	} else {
		WARNING("FilterAttach() does not exist in Fltlib.dll");
	}

	FreeLibrary(fltlib);

	return retval;
}


static void
windows_msg(u32 code, const wchar_t *format, va_list va,
	    bool is_ntstatus, bool is_error)
{
	wchar_t _buf[STACK_MAX / 8] = { 0 };
	wchar_t *buf = _buf;
	size_t buflen = ARRAY_LEN(_buf);
	size_t ret;
	size_t n;
	va_list va2;

retry:
	va_copy(va2, va);
	n = vsnwprintf(buf, buflen, format, va2);
	va_end(va2);

	if (n >= buflen)
		goto realloc;

	n += snwprintf(&buf[n], buflen - n,
		       (is_ntstatus ?
			L" (status=%08"PRIx32"): " :
			L" (err=%"PRIu32"): "),
		       code);

	if (n >= buflen)
		goto realloc;

	ret = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS |
				(is_ntstatus ? FORMAT_MESSAGE_FROM_HMODULE : 0),
			    (is_ntstatus ? ntdll_handle : NULL),
			    code,
			    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			    &buf[n],
			    buflen - n,
			    NULL);
	n += ret;

	if (n >= buflen || (ret == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER))
		goto realloc;

        if (buf[n - 1] == L'\n')
		buf[--n] = L'\0';
        if (buf[n - 1] == L'\r')
		buf[--n] = L'\0';
        if (buf[n - 1] == L'.')
		buf[--n] = L'\0';

	if (is_error)
		ERROR("%ls", buf);
	else
		WARNING("%ls", buf);
	if (buf != _buf)
		FREE(buf);
	return;

realloc:
	if (buf != _buf)
		FREE(buf);
	buflen *= 2;
	buf = MALLOC(buflen * sizeof(buf[0]));
	if (buf)
		goto retry;
	ERROR("Ran out of memory while building error message!!!");
}

void
win32_warning(DWORD err, const wchar_t *format, ...)
{
	va_list va;

	va_start(va, format);
	windows_msg(err, format, va, false, false);
	va_end(va);
}

void
win32_error(DWORD err, const wchar_t *format, ...)
{
	va_list va;

	va_start(va, format);
	windows_msg(err, format, va, false, true);
	va_end(va);
}

void
winnt_warning(NTSTATUS status, const wchar_t *format, ...)
{
	va_list va;

	va_start(va, format);
	windows_msg(status, format, va, true, false);
	va_end(va);
}

void
winnt_error(NTSTATUS status, const wchar_t *format, ...)
{
	va_list va;

	va_start(va, format);
	windows_msg(status, format, va, true, true);
	va_end(va);
}

/*
 * Synchronously execute a filesystem control method.  This is a wrapper around
 * NtFsControlFile() that handles STATUS_PENDING.  Note that SYNCHRONIZE
 * permission is, in general, required on the handle.
 */
NTSTATUS
winnt_fsctl(HANDLE h, u32 code, const void* in, u32 in_size,
	void* out, u32 out_size_avail, u32* actual_out_size_ret)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;

	status = NtFsControlFile(h, NULL, NULL, NULL, &iosb, code,
		(void*)in, in_size, out, out_size_avail);
	if (status == STATUS_PENDING) {
		/* Beware: this case is often encountered with remote
		 * filesystems, but rarely with local filesystems.  */

		status = NtWaitForSingleObject(h, FALSE, NULL);
		if (NT_SUCCESS(status)) {
			status = iosb.Status;
		} else {
			/* We shouldn't be issuing ioctls on a handle to which
			 * we don't have SYNCHRONIZE access.  Otherwise we have
			 * no way to wait for them to complete.  */
			wimlib_assert(status != STATUS_ACCESS_DENIED);
		}
	}

	if (NT_SUCCESS(status) && actual_out_size_ret != NULL)
		*actual_out_size_ret = (u32)iosb.Information;

	return status;
}

#endif /* _WIN32 */
