/*
 * Linux Compatibility Layer for Rufus
 * Copyright © 2025 Linux Port Contributors
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

#pragma once

#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <dirent.h>
#include <errno.h>
#include <wchar.h>
#include <locale.h>
#include <ctype.h>

// Windows type replacements
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t QWORD;
typedef int BOOL;
typedef void* HANDLE;
typedef void* PVOID;
typedef char* PSTR;
typedef const char* PCSTR;
typedef wchar_t* PWSTR;
typedef const wchar_t* PCWSTR;

// Windows constants
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_READ O_RDONLY
#define GENERIC_WRITE O_WRONLY
#define FILE_SHARE_READ 0
#define FILE_SHARE_WRITE 0
#define OPEN_EXISTING 0
#define FILE_ATTRIBUTE_NORMAL 0

// Error codes
#define ERROR_SUCCESS 0
#define ERROR_ACCESS_DENIED EACCES
#define ERROR_SHARING_VIOLATION EBUSY
#define ERROR_NOT_READY ENODEV
#define ERROR_WRITE_PROTECT EROFS
#define ERROR_DEVICE_IN_USE EBUSY
#define ERROR_NO_MEDIA_IN_DRIVE ENOMEDIUM
#define ERROR_CANCELLED ECANCELED
#define ERROR_GEN_FAILURE EIO
#define ERROR_LABEL_TOO_LONG ENAMETOOLONG
#define ERROR_NOT_SUPPORTED ENOTSUP
#define ERROR_OFFSET_ALIGNMENT_VIOLATION EINVAL

// Windows API replacements
HANDLE CreateFileA(PCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                   void* lpSecurityAttributes, DWORD dwCreationDisposition,
                   DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

BOOL CloseHandle(HANDLE hObject);
BOOL DeviceIoControl(HANDLE hDevice, DWORD dwIoControlCode, void* lpInBuffer,
                     DWORD nInBufferSize, void* lpOutBuffer, DWORD nOutBufferSize,
                     DWORD* lpBytesReturned, void* lpOverlapped);

DWORD GetLastError(void);
void SetLastError(DWORD dwErrCode);
char* WindowsErrorString(void);
uint64_t GetTickCount64(void);
void Sleep(DWORD dwMilliseconds);

// Mount manager constants and structures
#define MOUNTMGR_DOS_DEVICE_NAME "/dev/mount_manager"
#define IOCTL_MOUNTMGR_SET_AUTO_MOUNT 0x1001
#define IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT 0x1002

// File system control constants
#define FSCTL_ALLOW_EXTENDED_DASD_IO 0x2001
#define FSCTL_LOCK_VOLUME 0x2002

// String manipulation functions
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define strncat_s(dst, dstsz, src, count) strncat(dst, src, count)
#define _snprintf_s(dst, dstsz, count, fmt, ...) snprintf(dst, dstsz, fmt, ##__VA_ARGS__)
#define _vsnprintf_s(dst, dstsz, count, fmt, args) vsnprintf(dst, dstsz, fmt, args)
#define _TRUNCATE SIZE_MAX

// Linux-specific structures for drive information
typedef struct {
    char path[MAX_PATH];
    char model[256];
    char vendor[256];
    uint64_t size;
    BOOL is_usb;
    BOOL is_removable;
    int fd;
} LINUX_DRIVE_INFO;

// Global error handling
extern DWORD g_last_error;

// Linux-specific functions for drive enumeration and handling
BOOL linux_enumerate_drives(void);
BOOL linux_get_drive_info(const char* device_path, LINUX_DRIVE_INFO* drive_info);
BOOL linux_format_drive(const char* device_path, const char* filesystem, const char* label);
BOOL linux_write_image_to_drive(const char* device_path, const char* image_path);

// Device detection functions
BOOL linux_is_usb_device(const char* device_path);
BOOL linux_is_removable_device(const char* device_path);
uint64_t linux_get_device_size(const char* device_path);

// Filesystem functions
BOOL linux_create_filesystem(const char* device_path, const char* fs_type, const char* label);
BOOL linux_mount_device(const char* device_path, const char* mount_point);
BOOL linux_unmount_device(const char* device_path);

#endif /* LINUX_COMPAT_H */
