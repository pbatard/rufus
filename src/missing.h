/*
* Rufus: The Reliable USB Formatting Utility
* Constants and defines missing from various toolchains
* Copyright © 2016-2017 Pete Batard <pete@akeo.ie>
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
#include <stdint.h>

#pragma once

/* Convenient to have around */
#define KB                   1024LL
#define MB                1048576LL
#define GB             1073741824LL
#define TB          1099511627776LL

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if defined(__GNUC__)
#define ALIGNED(m) __attribute__ ((__aligned__(m)))
#elif defined(_MSC_VER)
#define ALIGNED(m) __declspec(align(m))
#endif

/*
 * Prefetch 64 bytes at address m, for read-only operation
 * We account for these built-in calls doing nothing if the
 * line has already been fetched, or if the address is invalid.
 */
#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH64(m) do { __builtin_prefetch(m, 0, 0); __builtin_prefetch(m+32, 0, 0); } while(0)
#elif defined(_MSC_VER)
#if defined(_M_IX86) || defined (_M_X64)
#define PREFETCH64(m) do { _m_prefetch(m); _m_prefetch(m+32); } while(0)
#else
// _m_prefetch() doesn't seem to exist for MSVC/ARM
#define PREFETCH64(m)
#endif
#endif

/* Read/write with endianness swap */
#if defined (_MSC_VER) && (_MSC_VER >= 1300)
#include <stdlib.h>
#if !defined(__clang__)
#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)
#endif
#define bswap_uint64 _byteswap_uint64
#define bswap_uint32 _byteswap_ulong
#define bswap_uint16 _byteswap_ushort
#elif defined (__GNUC__) && ((__GNUC__ > 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ >= 3)))
#define bswap_uint64 __builtin_bswap64
#define bswap_uint32 __builtin_bswap32
#define bswap_uint16 __builtin_bswap16
#endif
#define read_swap16(p) bswap_uint16(*(const uint16_t*)(const uint8_t*)(p))
#define read_swap32(p) bswap_uint32(*(const uint32_t*)(const uint8_t*)(p))
#define read_swap64(p) bswap_uint64(*(const uint64_t*)(const uint8_t*)(p))
#define write_swap16(p,v) (*(uint16_t*)(void*)(p)) = bswap_uint16(v)
#define write_swap32(p,v) (*(uint32_t*)(void*)(p)) = bswap_uint32(v)
#define write_swap64(p,v) (*(uint32_t*)(void*)(p)) = bswap_uint64(v)

/*
 * Nibbled from https://github.com/hanji/popcnt/blob/master/populationcount.cpp
 * Since MSVC x86_32 does not have intrinsic popcount64 and I don't have all day
 */
static __inline int popcnt64(register uint64_t u)
{
	u = (u & 0x5555555555555555) + ((u >> 1) & 0x5555555555555555);
	u = (u & 0x3333333333333333) + ((u >> 2) & 0x3333333333333333);
	u = (u & 0x0f0f0f0f0f0f0f0f) + ((u >> 4) & 0x0f0f0f0f0f0f0f0f);
	u = (u & 0x00ff00ff00ff00ff) + ((u >> 8) & 0x00ff00ff00ff00ff);
	u = (u & 0x0000ffff0000ffff) + ((u >> 16) & 0x0000ffff0000ffff);
	u = (u & 0x00000000ffffffff) + ((u >> 32) & 0x00000000ffffffff);
	return (int)u;
}

static __inline void *_reallocf(void *ptr, size_t size) {
	void *ret = realloc(ptr, size);
	if (!ret)
		free(ptr);
	return ret;
}

/* Why oh why does Microsoft have to make everybody suffer with their braindead use of Unicode? */
#define _RT_ICON			MAKEINTRESOURCEA(3)
#define _RT_DIALOG			MAKEINTRESOURCEA(5)
#define _RT_RCDATA			MAKEINTRESOURCEA(10)
#define _RT_GROUP_ICON		MAKEINTRESOURCEA((ULONG_PTR)(MAKEINTRESOURCEA(3) + 11))

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

/* Clang/MinGW32 has an issue with intptr_t */
#ifndef _UINTPTR_T_DEFINED
#define _UINTPTR_T_DEFINED
#ifdef _WIN64
typedef unsigned __int64 uintptr_t;
#else
typedef unsigned int uintptr_t;
#endif
#endif

/*
 * IFile[Open]Dialog interface for Vista and later (from MinGW headers)
 */
#ifndef __IFileDialog_INTERFACE_DEFINED__
#define __IFileDialog_INTERFACE_DEFINED__

enum _FILEOPENDIALOGOPTIONS {
	FOS_OVERWRITEPROMPT = 0x2,
	FOS_STRICTFILETYPES = 0x4,
	FOS_NOCHANGEDIR = 0x8,
	FOS_PICKFOLDERS = 0x20,
	FOS_FORCEFILESYSTEM = 0x40,
	FOS_ALLNONSTORAGEITEMS = 0x80,
	FOS_NOVALIDATE = 0x100,
	FOS_ALLOWMULTISELECT = 0x200,
	FOS_PATHMUSTEXIST = 0x800,
	FOS_FILEMUSTEXIST = 0x1000,
	FOS_CREATEPROMPT = 0x2000,
	FOS_SHAREAWARE = 0x4000,
	FOS_NOREADONLYRETURN = 0x8000,
	FOS_NOTESTFILECREATE = 0x10000,
	FOS_HIDEMRUPLACES = 0x20000,
	FOS_HIDEPINNEDPLACES = 0x40000,
	FOS_NODEREFERENCELINKS = 0x100000,
	FOS_DONTADDTORECENT = 0x2000000,
	FOS_FORCESHOWHIDDEN = 0x10000000,
	FOS_DEFAULTNOMINIMODE = 0x20000000,
	FOS_FORCEPREVIEWPANEON = 0x40000000
};

typedef enum FDAP {
	FDAP_BOTTOM = 0,
	FDAP_TOP = 1
} FDAP;

typedef DWORD FILEOPENDIALOGOPTIONS;

DEFINE_GUID(IID_IFileDialog, 0x42f85136, 0xdb7e, 0x439c, 0x85, 0xf1, 0xe4, 0x07, 0x5d, 0x13, 0x5f, 0xc8);

typedef struct IFileDialogVtbl {
	BEGIN_INTERFACE

		/*** IUnknown methods ***/
		HRESULT(STDMETHODCALLTYPE *QueryInterface)(
			IFileDialog* This,
			REFIID riid,
			void **ppvObject);

	ULONG(STDMETHODCALLTYPE *AddRef)(
		IFileDialog* This);

	ULONG(STDMETHODCALLTYPE *Release)(
		IFileDialog* This);

	/*** IModalWindow methods ***/
	HRESULT(STDMETHODCALLTYPE *Show)(
		IFileDialog* This,
		HWND hwndOwner);

	/*** IFileDialog methods ***/
	HRESULT(STDMETHODCALLTYPE *SetFileTypes)(
		IFileDialog* This,
		UINT cFileTypes,
		const COMDLG_FILTERSPEC *rgFilterSpec);

	HRESULT(STDMETHODCALLTYPE *SetFileTypeIndex)(
		IFileDialog* This,
		UINT iFileType);

	HRESULT(STDMETHODCALLTYPE *GetFileTypeIndex)(
		IFileDialog* This,
		UINT *piFileType);

	HRESULT(STDMETHODCALLTYPE *Advise)(
		IFileDialog* This,
		IFileDialogEvents *pfde,
		DWORD *pdwCookie);

	HRESULT(STDMETHODCALLTYPE *Unadvise)(
		IFileDialog* This,
		DWORD dwCookie);

	HRESULT(STDMETHODCALLTYPE *SetOptions)(
		IFileDialog* This,
		FILEOPENDIALOGOPTIONS fos);

	HRESULT(STDMETHODCALLTYPE *GetOptions)(
		IFileDialog* This,
		FILEOPENDIALOGOPTIONS *pfos);

	HRESULT(STDMETHODCALLTYPE *SetDefaultFolder)(
		IFileDialog* This,
		IShellItem *psi);

	HRESULT(STDMETHODCALLTYPE *SetFolder)(
		IFileDialog* This,
		IShellItem *psi);

	HRESULT(STDMETHODCALLTYPE *GetFolder)(
		IFileDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *GetCurrentSelection)(
		IFileDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *SetFileName)(
		IFileDialog* This,
		LPCWSTR pszName);

	HRESULT(STDMETHODCALLTYPE *GetFileName)(
		IFileDialog* This,
		LPWSTR *pszName);

	HRESULT(STDMETHODCALLTYPE *SetTitle)(
		IFileDialog* This,
		LPCWSTR pszTitle);

	HRESULT(STDMETHODCALLTYPE *SetOkButtonLabel)(
		IFileDialog* This,
		LPCWSTR pszText);

	HRESULT(STDMETHODCALLTYPE *SetFileNameLabel)(
		IFileDialog* This,
		LPCWSTR pszLabel);

	HRESULT(STDMETHODCALLTYPE *GetResult)(
		IFileDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *AddPlace)(
		IFileDialog* This,
		IShellItem *psi,
		FDAP fdap);

	HRESULT(STDMETHODCALLTYPE *SetDefaultExtension)(
		IFileDialog* This,
		LPCWSTR pszDefaultExtension);

	HRESULT(STDMETHODCALLTYPE *Close)(
		IFileDialog* This,
		HRESULT hr);

	HRESULT(STDMETHODCALLTYPE *SetClientGuid)(
		IFileDialog* This,
		REFGUID guid);

	HRESULT(STDMETHODCALLTYPE *ClearClientData)(
		IFileDialog* This);

	HRESULT(STDMETHODCALLTYPE *SetFilter)(
		IFileDialog* This,
		IShellItemFilter *pFilter);

	END_INTERFACE
} IFileDialogVtbl;
interface IFileDialog {
	CONST_VTBL IFileDialogVtbl* lpVtbl;
};
#endif

#ifndef __IFileOpenDialog_INTERFACE_DEFINED__
#define __IFileOpenDialog_INTERFACE_DEFINED__

DEFINE_GUID(IID_IFileOpenDialog, 0xd57c7288, 0xd4ad, 0x4768, 0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60);

typedef struct IFileOpenDialogVtbl {
	BEGIN_INTERFACE

		/*** IUnknown methods ***/
		HRESULT(STDMETHODCALLTYPE *QueryInterface)(
			IFileOpenDialog* This,
			REFIID riid,
			void **ppvObject);

	ULONG(STDMETHODCALLTYPE *AddRef)(
		IFileOpenDialog* This);

	ULONG(STDMETHODCALLTYPE *Release)(
		IFileOpenDialog* This);

	/*** IModalWindow methods ***/
	HRESULT(STDMETHODCALLTYPE *Show)(
		IFileOpenDialog* This,
		HWND hwndOwner);

	/*** IFileDialog methods ***/
	HRESULT(STDMETHODCALLTYPE *SetFileTypes)(
		IFileOpenDialog* This,
		UINT cFileTypes,
		const COMDLG_FILTERSPEC *rgFilterSpec);

	HRESULT(STDMETHODCALLTYPE *SetFileTypeIndex)(
		IFileOpenDialog* This,
		UINT iFileType);

	HRESULT(STDMETHODCALLTYPE *GetFileTypeIndex)(
		IFileOpenDialog* This,
		UINT *piFileType);

	HRESULT(STDMETHODCALLTYPE *Advise)(
		IFileOpenDialog* This,
		IFileDialogEvents *pfde,
		DWORD *pdwCookie);

	HRESULT(STDMETHODCALLTYPE *Unadvise)(
		IFileOpenDialog* This,
		DWORD dwCookie);

	HRESULT(STDMETHODCALLTYPE *SetOptions)(
		IFileOpenDialog* This,
		FILEOPENDIALOGOPTIONS fos);

	HRESULT(STDMETHODCALLTYPE *GetOptions)(
		IFileOpenDialog* This,
		FILEOPENDIALOGOPTIONS *pfos);

	HRESULT(STDMETHODCALLTYPE *SetDefaultFolder)(
		IFileOpenDialog* This,
		IShellItem *psi);

	HRESULT(STDMETHODCALLTYPE *SetFolder)(
		IFileOpenDialog* This,
		IShellItem *psi);

	HRESULT(STDMETHODCALLTYPE *GetFolder)(
		IFileOpenDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *GetCurrentSelection)(
		IFileOpenDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *SetFileName)(
		IFileOpenDialog* This,
		LPCWSTR pszName);

	HRESULT(STDMETHODCALLTYPE *GetFileName)(
		IFileOpenDialog* This,
		LPWSTR *pszName);

	HRESULT(STDMETHODCALLTYPE *SetTitle)(
		IFileOpenDialog* This,
		LPCWSTR pszTitle);

	HRESULT(STDMETHODCALLTYPE *SetOkButtonLabel)(
		IFileOpenDialog* This,
		LPCWSTR pszText);

	HRESULT(STDMETHODCALLTYPE *SetFileNameLabel)(
		IFileOpenDialog* This,
		LPCWSTR pszLabel);

	HRESULT(STDMETHODCALLTYPE *GetResult)(
		IFileOpenDialog* This,
		IShellItem **ppsi);

	HRESULT(STDMETHODCALLTYPE *AddPlace)(
		IFileOpenDialog* This,
		IShellItem *psi,
		FDAP fdap);

	HRESULT(STDMETHODCALLTYPE *SetDefaultExtension)(
		IFileOpenDialog* This,
		LPCWSTR pszDefaultExtension);

	HRESULT(STDMETHODCALLTYPE *Close)(
		IFileOpenDialog* This,
		HRESULT hr);

	HRESULT(STDMETHODCALLTYPE *SetClientGuid)(
		IFileOpenDialog* This,
		REFGUID guid);

	HRESULT(STDMETHODCALLTYPE *ClearClientData)(
		IFileOpenDialog* This);

	HRESULT(STDMETHODCALLTYPE *SetFilter)(
		IFileOpenDialog* This,
		IShellItemFilter *pFilter);

	/*** IFileOpenDialog methods ***/
	HRESULT(STDMETHODCALLTYPE *GetResults)(
		IFileOpenDialog* This,
		IShellItemArray **ppenum);

	HRESULT(STDMETHODCALLTYPE *GetSelectedItems)(
		IFileOpenDialog* This,
		IShellItemArray **ppsai);

	END_INTERFACE
} IFileOpenDialogVtbl;
interface IFileOpenDialog {
	CONST_VTBL IFileOpenDialogVtbl* lpVtbl;
};
#endif
