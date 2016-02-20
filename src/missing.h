/*
* Rufus: The Reliable USB Formatting Utility
* Constants and defines missing from various toolchains
* Copyright © 2016 Pete Batard <pete@akeo.ie>
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
#include <commctrl.h>
#include <shlobj.h>
#include <wininet.h>

#pragma once

/* Convenient to have around */
#define KB                   1024LL
#define MB                1048576LL
#define GB             1073741824LL
#define TB          1099511627776LL

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if defined(_MSC_VER)
#define bswap_uint64 _byteswap_uint64
#define bswap_uint32 _byteswap_ulong
#define bswap_uint16 _byteswap_ushort
#else
#define bswap_uint64 __builtin_bswap64
#define bswap_uint32 __builtin_bswap32
#define bswap_uint16 __builtin_bswap16
#endif

static __inline void *_reallocf(void *ptr, size_t size) {
	void *ret = realloc(ptr, size);
	if (!ret)
		free(ptr);
	return ret;
}

/* UI redefinitions for WDK and MinGW */
#ifndef PBM_SETSTATE
#define PBM_SETSTATE (WM_USER+16)
#endif
#ifndef PBST_NORMAL
#define PBST_NORMAL 1
#endif
#ifndef PBST_ERROR
#define PBST_ERROR 2
#endif
#ifndef PBST_PAUSED
#define PBST_PAUSED 3
#endif
#ifndef BUTTON_IMAGELIST_ALIGN_CENTER
#define BUTTON_IMAGELIST_ALIGN_CENTER 4
#endif
#ifndef BCM_SETIMAGELIST
#define BCM_SETIMAGELIST 0x1602
#endif
#ifndef DBT_CUSTOMEVENT
#define DBT_CUSTOMEVENT 0x8006
#endif
#ifndef ERROR_FILE_TOO_LARGE
#define ERROR_FILE_TOO_LARGE 223
#endif
#ifndef MSGFLT_ADD
#define MSGFLT_ADD 1
#endif
#ifndef WM_CLIENTSHUTDOWN
#define WM_CLIENTSHUTDOWN 0x3B
#endif
#ifndef WM_COPYGLOBALDATA
#define WM_COPYGLOBALDATA 0x49
#endif
#ifndef PBS_MARQUEE
#define PBS_MARQUEE 0x08
#endif
#ifndef PBM_SETMARQUEE
#define PBM_SETMARQUEE (WM_USER+10)
#endif

typedef struct {
	HIMAGELIST himl;
	RECT margin;
	UINT uAlign;
} MY_BUTTON_IMAGELIST;

typedef struct
{
	LPCITEMIDLIST pidl;
	BOOL fRecursive;
} MY_SHChangeNotifyEntry;

/* The following is used for native ISO mounting in Windows 8 or later */
#define VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT \
	{ 0xEC984AECL, 0xA0F9, 0x47e9, { 0x90, 0x1F, 0x71, 0x41, 0x5A, 0x66, 0x34, 0x5B } }

typedef enum _VIRTUAL_DISK_ACCESS_MASK {
	VIRTUAL_DISK_ACCESS_NONE = 0x00000000,
	VIRTUAL_DISK_ACCESS_ATTACH_RO = 0x00010000,
	VIRTUAL_DISK_ACCESS_ATTACH_RW = 0x00020000,
	VIRTUAL_DISK_ACCESS_DETACH = 0x00040000,
	VIRTUAL_DISK_ACCESS_GET_INFO = 0x00080000,
	VIRTUAL_DISK_ACCESS_CREATE = 0x00100000,
	VIRTUAL_DISK_ACCESS_METAOPS = 0x00200000,
	VIRTUAL_DISK_ACCESS_READ = 0x000d0000,
	VIRTUAL_DISK_ACCESS_ALL = 0x003f0000,
	VIRTUAL_DISK_ACCESS_WRITABLE = 0x00320000
} VIRTUAL_DISK_ACCESS_MASK;

typedef enum _OPEN_VIRTUAL_DISK_FLAG {
	OPEN_VIRTUAL_DISK_FLAG_NONE = 0x00000000,
	OPEN_VIRTUAL_DISK_FLAG_NO_PARENTS = 0x00000001,
	OPEN_VIRTUAL_DISK_FLAG_BLANK_FILE = 0x00000002,
	OPEN_VIRTUAL_DISK_FLAG_BOOT_DRIVE = 0x00000004,
	OPEN_VIRTUAL_DISK_FLAG_CACHED_IO = 0x00000008,
	OPEN_VIRTUAL_DISK_FLAG_CUSTOM_DIFF_CHAIN = 0x00000010
} OPEN_VIRTUAL_DISK_FLAG;

typedef enum _OPEN_VIRTUAL_DISK_VERSION {
	OPEN_VIRTUAL_DISK_VERSION_UNSPECIFIED = 0,
	OPEN_VIRTUAL_DISK_VERSION_1 = 1,
	OPEN_VIRTUAL_DISK_VERSION_2 = 2
} OPEN_VIRTUAL_DISK_VERSION;

typedef enum _ATTACH_VIRTUAL_DISK_FLAG {
	ATTACH_VIRTUAL_DISK_FLAG_NONE = 0x00000000,
	ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY = 0x00000001,
	ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER = 0x00000002,
	ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME = 0x00000004,
	ATTACH_VIRTUAL_DISK_FLAG_NO_LOCAL_HOST = 0x00000008
} ATTACH_VIRTUAL_DISK_FLAG;

typedef enum _ATTACH_VIRTUAL_DISK_VERSION {
	ATTACH_VIRTUAL_DISK_VERSION_UNSPECIFIED = 0,
	ATTACH_VIRTUAL_DISK_VERSION_1 = 1
} ATTACH_VIRTUAL_DISK_VERSION;

typedef enum _DETACH_VIRTUAL_DISK_FLAG {
	DETACH_VIRTUAL_DISK_FLAG_NONE = 0x00000000
} DETACH_VIRTUAL_DISK_FLAG;

#ifndef _VIRTUAL_STORAGE_TYPE_DEFINED
#define _VIRTUAL_STORAGE_TYPE_DEFINED
typedef struct _VIRTUAL_STORAGE_TYPE {
	ULONG DeviceId;
	GUID  VendorId;
} VIRTUAL_STORAGE_TYPE, *PVIRTUAL_STORAGE_TYPE;
#endif

typedef struct _OPEN_VIRTUAL_DISK_PARAMETERS {
	OPEN_VIRTUAL_DISK_VERSION Version;
	union {
		struct {
			ULONG RWDepth;
		} Version1;
		struct {
			BOOL GetInfoOnly;
			BOOL ReadOnly;
			GUID ResiliencyGuid;
		} Version2;
	};
} OPEN_VIRTUAL_DISK_PARAMETERS, *POPEN_VIRTUAL_DISK_PARAMETERS;

typedef struct _ATTACH_VIRTUAL_DISK_PARAMETERS {
	ATTACH_VIRTUAL_DISK_VERSION Version;
	union {
		struct {
			ULONG Reserved;
		} Version1;
	};
} ATTACH_VIRTUAL_DISK_PARAMETERS, *PATTACH_VIRTUAL_DISK_PARAMETERS;

/* Networking constants missing from MinGW */
#if !defined(ERROR_INTERNET_DISCONNECTED)
#define ERROR_INTERNET_DISCONNECTED (INTERNET_ERROR_BASE + 163)
#endif
#if !defined(ERROR_INTERNET_SERVER_UNREACHABLE)
#define ERROR_INTERNET_SERVER_UNREACHABLE (INTERNET_ERROR_BASE + 164)
#endif
#if !defined(ERROR_INTERNET_PROXY_SERVER_UNREACHABLE)
#define ERROR_INTERNET_PROXY_SERVER_UNREACHABLE (INTERNET_ERROR_BASE + 165)
#endif
#if !defined(ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT)
#define ERROR_INTERNET_BAD_AUTO_PROXY_SCRIPT (INTERNET_ERROR_BASE + 166)
#endif
#if !defined(ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT)
#define ERROR_INTERNET_UNABLE_TO_DOWNLOAD_SCRIPT (INTERNET_ERROR_BASE + 167)
#endif
#if !defined(ERROR_INTERNET_FAILED_DUETOSECURITYCHECK)
#define ERROR_INTERNET_FAILED_DUETOSECURITYCHECK (INTERNET_ERROR_BASE + 171)
#endif
#if !defined(ERROR_INTERNET_NOT_INITIALIZED)
#define ERROR_INTERNET_NOT_INITIALIZED (INTERNET_ERROR_BASE + 172)
#endif
#if !defined(ERROR_INTERNET_NEED_MSN_SSPI_PKG)
#define ERROR_INTERNET_NEED_MSN_SSPI_PKG (INTERNET_ERROR_BASE + 173)
#endif
#if !defined(ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY)
#define ERROR_INTERNET_LOGIN_FAILURE_DISPLAY_ENTITY_BODY (INTERNET_ERROR_BASE + 174)
#endif
