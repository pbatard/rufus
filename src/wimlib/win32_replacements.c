/*
 * win32_replacements.c - Replacements for various functions not available on
 * Windows, such as fsync().
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

#include <errno.h>
#include <io.h>	/* for _get_osfhandle()  */
#include <fcntl.h>

#include "wimlib/win32_common.h"

#include "wimlib/assert.h"
#include "wimlib/glob.h"
#include "wimlib/error.h"
#include "wimlib/timestamp.h"
#include "wimlib/util.h"

static int
win32_error_to_errno(DWORD err_code)
{
	/* This mapping is that used in Cygwin.
	 * Some of these choices are arbitrary. */
	switch (err_code) {
	case ERROR_ACCESS_DENIED:
		return EACCES;
	case ERROR_ACTIVE_CONNECTIONS:
		return EAGAIN;
	case ERROR_ALREADY_EXISTS:
		return EEXIST;
	case ERROR_BAD_DEVICE:
		return ENODEV;
	case ERROR_BAD_EXE_FORMAT:
		return ENOEXEC;
	case ERROR_BAD_NETPATH:
		return ENOENT;
	case ERROR_BAD_NET_NAME:
		return ENOENT;
	case ERROR_BAD_NET_RESP:
		return ENOSYS;
	case ERROR_BAD_PATHNAME:
		return ENOENT;
	case ERROR_BAD_PIPE:
		return EINVAL;
	case ERROR_BAD_UNIT:
		return ENODEV;
	case ERROR_BAD_USERNAME:
		return EINVAL;
	case ERROR_BEGINNING_OF_MEDIA:
		return EIO;
	case ERROR_BROKEN_PIPE:
		return EPIPE;
	case ERROR_BUSY:
		return EBUSY;
	case ERROR_BUS_RESET:
		return EIO;
	case ERROR_CALL_NOT_IMPLEMENTED:
		return ENOSYS;
	case ERROR_CANNOT_MAKE:
		return EPERM;
	case ERROR_CHILD_NOT_COMPLETE:
		return EBUSY;
	case ERROR_COMMITMENT_LIMIT:
		return EAGAIN;
	case ERROR_CRC:
		return EIO;
	case ERROR_DEVICE_DOOR_OPEN:
		return EIO;
	case ERROR_DEVICE_IN_USE:
		return EAGAIN;
	case ERROR_DEVICE_REQUIRES_CLEANING:
		return EIO;
	case ERROR_DIRECTORY:
		return ENOTDIR;
	case ERROR_DIR_NOT_EMPTY:
		return ENOTEMPTY;
	case ERROR_DISK_CORRUPT:
		return EIO;
	case ERROR_DISK_FULL:
		return ENOSPC;
#ifdef ENOTUNIQ
	case ERROR_DUP_NAME:
		return ENOTUNIQ;
#endif
	case ERROR_EAS_DIDNT_FIT:
		return ENOSPC;
#ifdef ENOTSUP
	case ERROR_EAS_NOT_SUPPORTED:
		return ENOTSUP;
#endif
	case ERROR_EA_LIST_INCONSISTENT:
		return EINVAL;
	case ERROR_EA_TABLE_FULL:
		return ENOSPC;
	case ERROR_END_OF_MEDIA:
		return ENOSPC;
	case ERROR_EOM_OVERFLOW:
		return EIO;
	case ERROR_EXE_MACHINE_TYPE_MISMATCH:
		return ENOEXEC;
	case ERROR_EXE_MARKED_INVALID:
		return ENOEXEC;
	case ERROR_FILEMARK_DETECTED:
		return EIO;
	case ERROR_FILENAME_EXCED_RANGE:
		return ENAMETOOLONG;
	case ERROR_FILE_CORRUPT:
		return EEXIST;
	case ERROR_FILE_EXISTS:
		return EEXIST;
	case ERROR_FILE_INVALID:
		return ENXIO;
	case ERROR_FILE_NOT_FOUND:
		return ENOENT;
	case ERROR_HANDLE_DISK_FULL:
		return ENOSPC;
#ifdef ENODATA
	case ERROR_HANDLE_EOF:
		return ENODATA;
#endif
	case ERROR_INVALID_ADDRESS:
		return EINVAL;
	case ERROR_INVALID_AT_INTERRUPT_TIME:
		return EINTR;
	case ERROR_INVALID_BLOCK_LENGTH:
		return EIO;
	case ERROR_INVALID_DATA:
		return EINVAL;
	case ERROR_INVALID_DRIVE:
		return ENODEV;
	case ERROR_INVALID_EA_NAME:
		return EINVAL;
	case ERROR_INVALID_EXE_SIGNATURE:
		return ENOEXEC;
#ifdef EBADRQC
	case ERROR_INVALID_FUNCTION:
		return EBADRQC;
#endif
	case ERROR_INVALID_HANDLE:
		return EBADF;
	case ERROR_INVALID_NAME:
		return ENOENT;
	case ERROR_INVALID_PARAMETER:
		return EINVAL;
	case ERROR_INVALID_SIGNAL_NUMBER:
		return EINVAL;
	case ERROR_IOPL_NOT_ENABLED:
		return ENOEXEC;
	case ERROR_IO_DEVICE:
		return EIO;
	case ERROR_IO_INCOMPLETE:
		return EAGAIN;
	case ERROR_IO_PENDING:
		return EAGAIN;
	case ERROR_LOCK_VIOLATION:
		return EBUSY;
	case ERROR_MAX_THRDS_REACHED:
		return EAGAIN;
	case ERROR_META_EXPANSION_TOO_LONG:
		return EINVAL;
	case ERROR_MOD_NOT_FOUND:
		return ENOENT;
#ifdef EMSGSIZE
	case ERROR_MORE_DATA:
		return EMSGSIZE;
#endif
	case ERROR_NEGATIVE_SEEK:
		return EINVAL;
	case ERROR_NETNAME_DELETED:
		return ENOENT;
	case ERROR_NOACCESS:
		return EFAULT;
	case ERROR_NONE_MAPPED:
		return EINVAL;
	case ERROR_NONPAGED_SYSTEM_RESOURCES:
		return EAGAIN;
#ifdef ENOLINK
	case ERROR_NOT_CONNECTED:
		return ENOLINK;
#endif
	case ERROR_NOT_ENOUGH_MEMORY:
		return ENOMEM;
	case ERROR_NOT_OWNER:
		return EPERM;
#ifdef ENOMEDIUM
	case ERROR_NOT_READY:
		return ENOMEDIUM;
#endif
	case ERROR_NOT_SAME_DEVICE:
		return EXDEV;
	case ERROR_NOT_SUPPORTED:
		return ENOSYS;
	case ERROR_NO_DATA:
		return EPIPE;
	case ERROR_NO_DATA_DETECTED:
		return EIO;
#ifdef ENOMEDIUM
	case ERROR_NO_MEDIA_IN_DRIVE:
		return ENOMEDIUM;
#endif
#ifdef ENMFILE
	case ERROR_NO_MORE_FILES:
		return ENMFILE;
#endif
#ifdef ENMFILE
	case ERROR_NO_MORE_ITEMS:
		return ENMFILE;
#endif
	case ERROR_NO_MORE_SEARCH_HANDLES:
		return ENFILE;
	case ERROR_NO_PROC_SLOTS:
		return EAGAIN;
	case ERROR_NO_SIGNAL_SENT:
		return EIO;
	case ERROR_NO_SYSTEM_RESOURCES:
		return EFBIG;
	case ERROR_NO_TOKEN:
		return EINVAL;
	case ERROR_OPEN_FAILED:
		return EIO;
	case ERROR_OPEN_FILES:
		return EAGAIN;
	case ERROR_OUTOFMEMORY:
		return ENOMEM;
	case ERROR_PAGED_SYSTEM_RESOURCES:
		return EAGAIN;
	case ERROR_PAGEFILE_QUOTA:
		return EAGAIN;
	case ERROR_PATH_NOT_FOUND:
		return ENOENT;
	case ERROR_PIPE_BUSY:
		return EBUSY;
	case ERROR_PIPE_CONNECTED:
		return EBUSY;
#ifdef ECOMM
	case ERROR_PIPE_LISTENING:
		return ECOMM;
	case ERROR_PIPE_NOT_CONNECTED:
		return ECOMM;
#endif
	case ERROR_POSSIBLE_DEADLOCK:
		return EDEADLOCK;
	case ERROR_PRIVILEGE_NOT_HELD:
		return EPERM;
	case ERROR_PROCESS_ABORTED:
		return EFAULT;
	case ERROR_PROC_NOT_FOUND:
		return ESRCH;
#ifdef ENONET
	case ERROR_REM_NOT_LIST:
		return ENONET;
#endif
	case ERROR_SECTOR_NOT_FOUND:
		return EINVAL;
	case ERROR_SEEK:
		return EINVAL;
	case ERROR_SETMARK_DETECTED:
		return EIO;
	case ERROR_SHARING_BUFFER_EXCEEDED:
		return ENOLCK;
	case ERROR_SHARING_VIOLATION:
		return EBUSY;
	case ERROR_SIGNAL_PENDING:
		return EBUSY;
	case ERROR_SIGNAL_REFUSED:
		return EIO;
#ifdef ELIBBAD
	case ERROR_SXS_CANT_GEN_ACTCTX:
		return ELIBBAD;
#endif
	case ERROR_THREAD_1_INACTIVE:
		return EINVAL;
	case ERROR_TOO_MANY_LINKS:
		return EMLINK;
	case ERROR_TOO_MANY_OPEN_FILES:
		return EMFILE;
	case ERROR_WAIT_NO_CHILDREN:
		return ECHILD;
	case ERROR_WORKING_SET_QUOTA:
		return EAGAIN;
	case ERROR_WRITE_PROTECT:
		return EROFS;
	default:
		return -1;
	}
}

static void
set_errno_from_win32_error(DWORD err)
{
	errno = win32_error_to_errno(err);
}

static void
set_errno_from_GetLastError(void)
{
	set_errno_from_win32_error(GetLastError());
}

/* Replacement for POSIX fsync() */
int
fsync(int fd)
{
	HANDLE h;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE)
		goto err;
	if (!FlushFileBuffers(h))
		goto err_set_errno;
	return 0;
err_set_errno:
	set_errno_from_GetLastError();
err:
	return -1;
}

/* Use the Win32 API to get the number of processors.  */
unsigned
get_available_cpus(void)
{
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
}

/* Use the Win32 API to get the amount of available memory.  */
u64
get_available_memory(void)
{
	MEMORYSTATUSEX status = {
		.dwLength = sizeof(status),
	};
	GlobalMemoryStatusEx(&status);
	return (u64)min(status.ullTotalPhys, status.ullTotalVirtual) * 85 / 100;
}

/* Replacement for POSIX-2008 realpath().  Warning: partial functionality only
 * (resolved_path must be NULL).   Also I highly doubt that GetFullPathName
 * really does the right thing under all circumstances. */
wchar_t *
realpath(const wchar_t *path, wchar_t *resolved_path)
{
	DWORD ret;
	DWORD err;
	wimlib_assert(resolved_path == NULL);

	ret = GetFullPathNameW(path, 0, NULL, NULL);
	if (!ret) {
		err = GetLastError();
		goto fail_win32;
	}

	resolved_path = MALLOC(ret * sizeof(wchar_t));
	if (!resolved_path)
		goto out;
	ret = GetFullPathNameW(path, ret, resolved_path, NULL);
	if (!ret) {
		err = GetLastError();
		FREE(resolved_path);
		resolved_path = NULL;
		goto fail_win32;
	}
	goto out;
fail_win32:
	set_errno_from_win32_error(err);
out:
	return resolved_path;
}

/* A quick hack to get reasonable rename() semantics on Windows, in particular
 * deleting the destination file instead of failing with ERROR_FILE_EXISTS and
 * working around any processes that may have the destination file open.
 *
 * Note: This is intended to be called when overwriting a regular file with an
 * updated copy and is *not* a fully POSIX compliant rename().  For that you may
 * wish to take a look at Cygwin's implementation, but be prepared...
 *
 * Return 0 on success, -1 on regular error, or 1 if the destination file was
 * deleted but the source could not be renamed and therefore should not be
 * deleted.
 */
int
win32_rename_replacement(const wchar_t *srcpath, const wchar_t *dstpath)
{
	wchar_t *tmpname;

	/* Normally, MoveFileExW() with the MOVEFILE_REPLACE_EXISTING flag does
	 * what we want.  */

	if (MoveFileExW(srcpath, dstpath, MOVEFILE_REPLACE_EXISTING))
		return 0;

	/* MoveFileExW() failed.  One way this can happen is if any process has
	 * the destination file open, in which case ERROR_ACCESS_DENIED is
	 * produced.  This can commonly happen if there is a backup or antivirus
	 * program monitoring or scanning the files.  This behavior is very
	 * different from the behavior of POSIX rename(), which simply unlinks
	 * the destination file and allows other processes to keep it open!  */

	if (GetLastError() != ERROR_ACCESS_DENIED)
		goto err_set_errno;

	/* We can work around the above-mentioned problem by renaming the
	 * destination file to yet another temporary file, then "deleting" it,
	 * which on Windows will in fact not actually delete it immediately but
	 * rather mark it for deletion when the last handle to it is closed.  */
	{
		static const wchar_t orig_suffix[] = L".orig";
		const size_t num_rand_chars = 9;
		wchar_t *p;

		size_t dstlen = wcslen(dstpath);

		tmpname = alloca(sizeof(wchar_t) *
				 (dstlen + ARRAY_LEN(orig_suffix) + num_rand_chars + 1));
		p = tmpname;
		p = wmempcpy(p, dstpath, dstlen);
		p = wmempcpy(p, orig_suffix, ARRAY_LEN(orig_suffix));
		get_random_alnum_chars(p, num_rand_chars);
		p += num_rand_chars;
		*p = L'\0';
	}

	if (!MoveFile(dstpath, tmpname))
		goto err_set_errno;

	if (!DeleteFile(tmpname)) {
		set_errno_from_GetLastError();
		WARNING_WITH_ERRNO("Failed to delete original file "
				   "(moved to \"%ls\")", tmpname);
	}

	if (!MoveFile(srcpath, dstpath)) {
		set_errno_from_GetLastError();
		WARNING_WITH_ERRNO("Atomic semantics not respected in "
				   "failed rename() (new file is at \"%ls\")",
				   srcpath);
		return 1;
	}

	return 0;

err_set_errno:
	set_errno_from_GetLastError();
	return -1;
}

#define MAX_IO_AMOUNT 1048576

static int
do_pread_or_pwrite(int fd, void *buf, size_t count, off_t offset,
		   bool is_pwrite)
{
	HANDLE h;
	LARGE_INTEGER orig_offset;
	DWORD result = 0xFFFFFFFF;
	LARGE_INTEGER relative_offset = { 0 };
	OVERLAPPED overlapped;
	BOOL bret;
	DWORD err = 0;

	h = (HANDLE)_get_osfhandle(fd);
	if (h == INVALID_HANDLE_VALUE)
		goto error;

	if (GetFileType(h) == FILE_TYPE_PIPE) {
		errno = ESPIPE;
		goto error;
	}

	/* Get original position */
	relative_offset.QuadPart = 0;
	if (!SetFilePointerEx(h, relative_offset, &orig_offset, FILE_CURRENT)) {
		err = GetLastError();
		win32_error(err, L"Failed to get original file position");
		goto error;
	}

	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = offset;
	overlapped.OffsetHigh = offset >> 32;

	/* Do the read or write at the specified offset */
	count = min(count, MAX_IO_AMOUNT);
	SetLastError(0);
	if (is_pwrite)
		bret = WriteFile(h, buf, count, &result, &overlapped);
	else
		bret = ReadFile(h, buf, count, &result, &overlapped);
	if (!bret) {
		err = GetLastError();
		win32_error(err, L"Failed to %s %zu bytes at offset %"PRIu64,
			    (is_pwrite ? "write" : "read"), count, offset);
		goto error;
	}

	wimlib_assert(result <= count);

	/* Restore the original position */
	if (!SetFilePointerEx(h, orig_offset, NULL, FILE_BEGIN)) {
		err = GetLastError();
		win32_error(err, L"Failed to restore file position to %"PRIu64,
			    offset);
		goto error;
	}

	return result;

error:
	if (err)
		set_errno_from_win32_error(err);
	return -1;
}

/* Dumb Windows implementation of pread().  It temporarily changes the file
 * offset, so it is not safe to use with readers/writers on the same file
 * descriptor.  */
ssize_t
win32_pread(int fd, void *buf, size_t count, off_t offset)
{
	return do_pread_or_pwrite(fd, buf, count, offset, false);
}

/* Dumb Windows implementation of pwrite().  It temporarily changes the file
 * offset, so it is not safe to use with readers/writers on the same file
 * descriptor. */
ssize_t
win32_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	return do_pread_or_pwrite(fd, (void*)buf, count, offset, true);
}

/* Replacement for read() which doesn't hide the Win32 error code */
ssize_t
win32_read(int fd, void *buf, size_t count)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD result = 0xFFFFFFFF;

	if (h == INVALID_HANDLE_VALUE)
		return -1;

	count = min(count, MAX_IO_AMOUNT);
	SetLastError(0);
	if (!ReadFile(h, buf, count, &result, NULL)) {
		DWORD err = GetLastError();
		win32_error(err,
			    L"Error reading %zu bytes from fd %d", count, fd);
		set_errno_from_win32_error(err);
		return -1;
	}

	wimlib_assert(result <= count);
	return result;
}

/* Replacement for write() which doesn't hide the Win32 error code */
ssize_t
win32_write(int fd, const void *buf, size_t count)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD result = 0xFFFFFFFF;

	if (h == INVALID_HANDLE_VALUE)
		return -1;

	count = min(count, MAX_IO_AMOUNT);
	SetLastError(0);
	if (!WriteFile(h, buf, count, &result, NULL)) {
		DWORD err = GetLastError();
		win32_error(err,
			    L"Error writing %zu bytes to fd %d", count, fd);
		set_errno_from_win32_error(err);
		return -1;
	}

	wimlib_assert(result <= count);
	return result;
}

/* Replacement for glob() in Windows native builds that operates on wide
 * characters.  */
int
win32_wglob(const wchar_t *pattern, int flags,
	    int (*errfunc)(const wchar_t *epath, int eerrno),
	    glob_t *pglob)
{
	WIN32_FIND_DATAW dat;
	DWORD err;
	HANDLE hFind;
	int ret;
	size_t nspaces;
	int errno_save;

	const wchar_t *backslash, *end_slash;
	size_t prefix_len;

	backslash = wcsrchr(pattern, L'\\');
	end_slash = wcsrchr(pattern, L'/');

	if (backslash > end_slash)
		end_slash = backslash;

	if (end_slash)
		prefix_len = end_slash - pattern + 1;
	else
		prefix_len = 0;

	/* This function does not support all functionality of the POSIX glob(),
	 * so make sure the parameters are consistent with supported
	 * functionality. */
	wimlib_assert(errfunc == NULL);
	wimlib_assert((flags & GLOB_ERR) == GLOB_ERR);
	wimlib_assert((flags & ~(GLOB_NOSORT | GLOB_ERR)) == 0);

	hFind = FindFirstFileW(pattern, &dat);
	if (hFind == INVALID_HANDLE_VALUE) {
		err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			errno = 0;
			return GLOB_NOMATCH;
		} else {
			set_errno_from_win32_error(err);
			return GLOB_ABORTED;
		}
	}
	pglob->gl_pathc = 0;
	pglob->gl_pathv = NULL;
	nspaces = 0;
	do {
		wchar_t *path;
		if (pglob->gl_pathc == nspaces) {
			size_t new_nspaces;
			wchar_t **pathv;

			new_nspaces = nspaces * 2 + 1;
			pathv = REALLOC(pglob->gl_pathv,
					new_nspaces * sizeof(pglob->gl_pathv[0]));
			if (!pathv)
				goto oom;
			pglob->gl_pathv = pathv;
			nspaces = new_nspaces;
		}
		size_t filename_len = wcslen(dat.cFileName);
		size_t len_needed = prefix_len + filename_len;

		path = MALLOC((len_needed + 1) * sizeof(wchar_t));
		if (!path)
			goto oom;

		wmemcpy(path, pattern, prefix_len);
		wmemcpy(path + prefix_len, dat.cFileName, filename_len + 1);
		pglob->gl_pathv[pglob->gl_pathc++] = path;
	} while (FindNextFileW(hFind, &dat));
	err = GetLastError();
	FindClose(hFind);
	if (err != ERROR_NO_MORE_FILES) {
		set_errno_from_win32_error(err);
		ret = GLOB_ABORTED;
		goto fail_globfree;
	}
	return 0;

oom:
	FindClose(hFind);
	errno = ENOMEM;
	ret = GLOB_NOSPACE;
fail_globfree:
	errno_save = errno;
	globfree(pglob);
	errno = errno_save;
	return ret;
}

void
globfree(glob_t *pglob)
{
	size_t i;
	for (i = 0; i < pglob->gl_pathc; i++)
		FREE(pglob->gl_pathv[i]);
	FREE(pglob->gl_pathv);
}

/* Replacement for fopen(path, "a") that doesn't prevent other processes from
 * reading the file  */
FILE *
win32_open_logfile(const wchar_t *path)
{
	HANDLE h;
	int fd;
	FILE *fp;

	h = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_VALID_FLAGS,
		       NULL, OPEN_ALWAYS, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	fd = _open_osfhandle((intptr_t)h, O_APPEND);
	if (fd < 0) {
		CloseHandle(h);
		return NULL;
	}

	fp = fdopen(fd, "a");
	if (!fp) {
		close(fd);
		return NULL;
	}

	return fp;
}

#define RtlGenRandom SystemFunction036
BOOLEAN WINAPI RtlGenRandom(PVOID RandomBuffer, ULONG RandomBufferLength);

/*
 * Generate @n cryptographically secure random bytes (thread-safe)
 *
 * This is the Windows version.  It uses RtlGenRandom() (actually called
 * SystemFunction036) from advapi32.dll.
 */
void
get_random_bytes(void *p, size_t n)
{
	while (n != 0) {
		u32 count = min(n, UINT32_MAX);

		if (!RtlGenRandom(p, count)) {
			win32_error(GetLastError(),
				    L"RtlGenRandom() failed (count=%u)", count);
			wimlib_assert(0);
			count = 0;
		}
		p = _PTR(p + count);
		n -= count;
	}
}

/* Retrieve the current time as a WIM timestamp.  */
u64
now_as_wim_timestamp(void)
{
	FILETIME ft;

	GetSystemTimeAsFileTime(&ft);

	return ((u64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

#endif /* _WIN32 */
