/*
 * error.c - logging and error code translation
 */

/*
 * Copyright (C) 2012, 2013, 2014 Eric Biggers
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* Make sure the POSIX-compatible strerror_r() is declared, rather than the GNU
 * version, which has a different return type. */
#ifdef _GNU_SOURCE
#  define GNU_SOURCE_WAS_DEFINED
#  undef _GNU_SOURCE
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200112L
#  endif
#endif
#include <string.h>
#ifdef GNU_SOURCE_WAS_DEFINED
#  define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdarg.h>

#include "wimlib.h"
#include "wimlib/error.h"
#include "wimlib/test_support.h"
#include "wimlib/util.h"
#include "wimlib/win32.h"

bool wimlib_print_errors = false;
FILE *wimlib_error_file = NULL; /* Set in wimlib_global_init() */
static bool wimlib_owns_error_file = false;

static void
wimlib_vmsg(const tchar *tag, const tchar *format, va_list va, bool perror)
{
	if (!wimlib_print_errors)
		return;
	int errno_save = errno;
	fflush(stdout);
	tfputs(tag, wimlib_error_file);
	tvfprintf(wimlib_error_file, format, va);
	if (perror && errno_save != 0) {
		tchar buf[64];
		int res;
		res = tstrerror_r(errno_save, buf, ARRAY_LEN(buf));
		if (res) {
			tsprintf(buf,
				 T("unknown error (errno=%d)"),
				 errno_save);
		}
	#ifdef _WIN32
		if (errno_save == EBUSY)
			tstrcpy(buf, T("Resource busy"));
	#endif
		tfprintf(wimlib_error_file, T(": %"TS), buf);
	}
	tputc(T('\n'), wimlib_error_file);
	fflush(wimlib_error_file);
	errno = errno_save;
}

void
wimlib_error(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[ERROR] "), format, va, false);
	va_end(va);
}

void
wimlib_error_with_errno(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[ERROR] "), format, va, true);
	va_end(va);
}

void
wimlib_warning(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[WARNING] "), format, va, false);
	va_end(va);
}

void
wimlib_warning_with_errno(const tchar *format, ...)
{
	va_list va;

	va_start(va, format);
	wimlib_vmsg(T("\r[WARNING] "), format, va, true);
	va_end(va);
}

void
print_byte_field(const u8 *field, size_t len, FILE *out)
{
	while (len--)
		tfprintf(out, T("%02hhx"), *field++);
}

WIMLIBAPI int
wimlib_set_print_errors(bool show_error_messages)
{
	wimlib_print_errors = show_error_messages;
	return 0;
}

WIMLIBAPI int
wimlib_set_error_file(FILE *fp)
{
	if (wimlib_owns_error_file)
		fclose(wimlib_error_file);
	wimlib_error_file = fp;
	wimlib_print_errors = (fp != NULL);
	wimlib_owns_error_file = false;
	return 0;
}

WIMLIBAPI int
wimlib_set_error_file_by_name(const tchar *path)
{
	FILE *fp;

#ifdef _WIN32
	fp = win32_open_logfile(path);
#else
	fp = fopen(path, "a");
#endif
	if (!fp)
		return WIMLIB_ERR_OPEN;
	wimlib_set_error_file(fp);
	wimlib_owns_error_file = true;
	return 0;
}

static const tchar * const error_strings[] = {
	[WIMLIB_ERR_SUCCESS]
		= T("Success"),
	[WIMLIB_ERR_ALREADY_LOCKED]
		= T("The WIM is already locked for writing"),
	[WIMLIB_ERR_DECOMPRESSION]
		= T("The WIM contains invalid compressed data"),
	[WIMLIB_ERR_FUSE]
		= T("An error was returned by fuse_main()"),
	[WIMLIB_ERR_GLOB_HAD_NO_MATCHES]
		= T("The provided file glob did not match any files"),
	[WIMLIB_ERR_IMAGE_COUNT]
		= T("Inconsistent image count among the metadata "
			"resources, the WIM header, and/or the XML data"),
	[WIMLIB_ERR_IMAGE_NAME_COLLISION]
		= T("Tried to add an image with a name that is already in use"),
	[WIMLIB_ERR_INSUFFICIENT_PRIVILEGES]
		= T("The user does not have sufficient privileges"),
	[WIMLIB_ERR_INTEGRITY]
		= T("The WIM file is corrupted (failed integrity check)"),
	[WIMLIB_ERR_INVALID_CAPTURE_CONFIG]
		= T("The contents of the capture configuration file were invalid"),
	[WIMLIB_ERR_INVALID_CHUNK_SIZE]
		= T("The compression chunk size was unrecognized"),
	[WIMLIB_ERR_INVALID_COMPRESSION_TYPE]
		= T("The compression type was unrecognized"),
	[WIMLIB_ERR_INVALID_HEADER]
		= T("The WIM header was invalid"),
	[WIMLIB_ERR_INVALID_IMAGE]
		= T("Tried to select an image that does not exist in the WIM"),
	[WIMLIB_ERR_INVALID_INTEGRITY_TABLE]
		= T("The WIM's integrity table is invalid"),
	[WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY]
		= T("An entry in the WIM's lookup table is invalid"),
	[WIMLIB_ERR_INVALID_METADATA_RESOURCE]
		= T("The metadata resource is invalid"),
	[WIMLIB_ERR_INVALID_OVERLAY]
		= T("Conflicting files in overlay when creating a WIM image"),
	[WIMLIB_ERR_INVALID_PARAM]
		= T("An invalid parameter was given"),
	[WIMLIB_ERR_INVALID_PART_NUMBER]
		= T("The part number or total parts of the WIM is invalid"),
	[WIMLIB_ERR_INVALID_PIPABLE_WIM]
		= T("The pipable WIM is invalid"),
	[WIMLIB_ERR_INVALID_REPARSE_DATA]
		= T("The reparse data of a reparse point was invalid"),
	[WIMLIB_ERR_INVALID_RESOURCE_HASH]
		= T("The SHA-1 message digest of a WIM resource did not match the expected value"),
	[WIMLIB_ERR_INVALID_UTF8_STRING]
		= T("A string was not a valid UTF-8 string"),
	[WIMLIB_ERR_INVALID_UTF16_STRING]
		= T("A string was not a valid UTF-16 string"),
	[WIMLIB_ERR_IS_DIRECTORY]
		= T("One of the specified paths to delete was a directory"),
	[WIMLIB_ERR_IS_SPLIT_WIM]
		= T("The WIM is part of a split WIM, which is not supported for this operation"),
	[WIMLIB_ERR_LINK]
		= T("Failed to create a hard or symbolic link when extracting "
			"a file from the WIM"),
	[WIMLIB_ERR_METADATA_NOT_FOUND]
		= T("The WIM does not contain image metadata; it only contains file data"),
	[WIMLIB_ERR_MKDIR]
		= T("Failed to create a directory"),
	[WIMLIB_ERR_MQUEUE]
		= T("Failed to create or use a POSIX message queue"),
	[WIMLIB_ERR_NOMEM]
		= T("Ran out of memory"),
	[WIMLIB_ERR_NOTDIR]
		= T("Expected a directory"),
	[WIMLIB_ERR_NOTEMPTY]
		= T("Directory was not empty"),
	[WIMLIB_ERR_NOT_A_REGULAR_FILE]
		= T("One of the specified paths to extract did not "
		    "correspond to a regular file"),
	[WIMLIB_ERR_NOT_A_WIM_FILE]
		= T("The file did not begin with the magic characters that "
			"identify a WIM file"),
	[WIMLIB_ERR_NO_FILENAME]
		= T("The WIM is not identified with a filename"),
	[WIMLIB_ERR_NOT_PIPABLE]
		= T("The WIM was not captured such that it can be "
		    "applied from a pipe"),
	[WIMLIB_ERR_NTFS_3G]
		= T("NTFS-3G encountered an error (check errno)"),
	[WIMLIB_ERR_OPEN]
		= T("Failed to open a file"),
	[WIMLIB_ERR_OPENDIR]
		= T("Failed to open a directory"),
	[WIMLIB_ERR_PATH_DOES_NOT_EXIST]
		= T("The path does not exist in the WIM image"),
	[WIMLIB_ERR_READ]
		= T("Could not read data from a file"),
	[WIMLIB_ERR_READLINK]
		= T("Could not read the target of a symbolic link"),
	[WIMLIB_ERR_RENAME]
		= T("Could not rename a file"),
	[WIMLIB_ERR_REPARSE_POINT_FIXUP_FAILED]
		= T("Unable to complete reparse point fixup"),
	[WIMLIB_ERR_RESOURCE_NOT_FOUND]
		= T("A file resource needed to complete the operation was missing from the WIM"),
	[WIMLIB_ERR_RESOURCE_ORDER]
		= T("The components of the WIM were arranged in an unexpected order"),
	[WIMLIB_ERR_SET_ATTRIBUTES]
		= T("Failed to set attributes on extracted file"),
	[WIMLIB_ERR_SET_REPARSE_DATA]
		= T("Failed to set reparse data on extracted file"),
	[WIMLIB_ERR_SET_SECURITY]
		= T("Failed to set file owner, group, or other permissions on extracted file"),
	[WIMLIB_ERR_SET_SHORT_NAME]
		= T("Failed to set short name on extracted file"),
	[WIMLIB_ERR_SET_TIMESTAMPS]
		= T("Failed to set timestamps on extracted file"),
	[WIMLIB_ERR_SPLIT_INVALID]
		= T("The WIM is part of an invalid split WIM"),
	[WIMLIB_ERR_STAT]
		= T("Could not read the metadata for a file or directory"),
	[WIMLIB_ERR_UNEXPECTED_END_OF_FILE]
		= T("Unexpectedly reached the end of the file"),
	[WIMLIB_ERR_UNICODE_STRING_NOT_REPRESENTABLE]
		= T("A Unicode string could not be represented in the current locale's encoding"),
	[WIMLIB_ERR_UNKNOWN_VERSION]
		= T("The WIM file is marked with an unknown version number"),
	[WIMLIB_ERR_UNSUPPORTED]
		= T("The requested operation is unsupported"),
	[WIMLIB_ERR_UNSUPPORTED_FILE]
		= T("A file in the directory tree to archive was not of a supported type"),
	[WIMLIB_ERR_WIM_IS_READONLY]
		= T("The WIM is read-only (file permissions, header flag, or split WIM)"),
	[WIMLIB_ERR_WRITE]
		= T("Failed to write data to a file"),
	[WIMLIB_ERR_XML]
		= T("The XML data of the WIM is invalid"),
	[WIMLIB_ERR_WIM_IS_ENCRYPTED]
		= T("The WIM file (or parts of it) is encrypted"),
	[WIMLIB_ERR_WIMBOOT]
		= T("Failed to set WIMBoot pointer data"),
	[WIMLIB_ERR_ABORTED_BY_PROGRESS]
		= T("The operation was aborted by the library user"),
	[WIMLIB_ERR_UNKNOWN_PROGRESS_STATUS]
		= T("The user-provided progress function returned an unrecognized value"),
	[WIMLIB_ERR_MKNOD]
		= T("Unable to create a special file (e.g. device node or socket)"),
	[WIMLIB_ERR_MOUNTED_IMAGE_IS_BUSY]
		= T("There are still files open on the mounted WIM image"),
	[WIMLIB_ERR_NOT_A_MOUNTPOINT]
		= T("There is not a WIM image mounted on the directory"),
	[WIMLIB_ERR_NOT_PERMITTED_TO_UNMOUNT]
		= T("The current user does not have permission to unmount the WIM image"),
	[WIMLIB_ERR_FVE_LOCKED_VOLUME]
		= T("The volume must be unlocked before it can be used"),
	[WIMLIB_ERR_UNABLE_TO_READ_CAPTURE_CONFIG]
		= T("The capture configuration file could not be read"),
	[WIMLIB_ERR_WIM_IS_INCOMPLETE]
		= T("The WIM file is incomplete"),
	[WIMLIB_ERR_COMPACTION_NOT_POSSIBLE]
		= T("The WIM file cannot be compacted because of its format, "
		    "its layout, or the write parameters specified by the user"),
	[WIMLIB_ERR_IMAGE_HAS_MULTIPLE_REFERENCES]
		= T("The WIM image cannot be modified because it is currently "
		    "referenced from multiple places"),
	[WIMLIB_ERR_DUPLICATE_EXPORTED_IMAGE]
		= T("The destination WIM already contains one of the source images"),
	[WIMLIB_ERR_CONCURRENT_MODIFICATION_DETECTED]
		= T("A file being added to a WIM image was concurrently modified"),
	[WIMLIB_ERR_SNAPSHOT_FAILURE]
		= T("Unable to create a filesystem snapshot"),
	[WIMLIB_ERR_INVALID_XATTR]
		= T("An extended attribute entry in the WIM image is invalid"),
	[WIMLIB_ERR_SET_XATTR]
		= T("Failed to set an extended attribute on an extracted file"),
#ifdef ENABLE_TEST_SUPPORT
	[WIMLIB_ERR_IMAGES_ARE_DIFFERENT]
		= T("A difference was detected between the two images being compared"),
#endif
};

WIMLIBAPI const tchar *
wimlib_get_error_string(enum wimlib_error_code _code)
{
	unsigned int code = (unsigned int)_code;

	if (code >= ARRAY_LEN(error_strings) || error_strings[code] == NULL)
		return T("Unknown error");

	return error_strings[code];
}
