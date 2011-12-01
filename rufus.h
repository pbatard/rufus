/*
 * Rufus: The Resourceful USB Formatting Utility
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
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
#include <Windows.h>
#include <winioctl.h>				// for MEDIA_TYPE

#pragma once

#define RUFUS_DEBUG

#define STR_NO_LABEL                "NO_LABEL"
#define RUFUS_CANCELBOX_TITLE       "Rufus - Cancellation"
#define DRIVE_INDEX_MIN             0x80
#define DRIVE_INDEX_MAX             0xC0
#define MAX_DRIVES                  16
#define MAX_TOOLTIPS                16
#define PROPOSEDLABEL_TOLERANCE     0.10
#define FS_DEFAULT                  FS_FAT32
#define WHITE                       RGB(255,255,255)
#define SEPARATOR_GREY              RGB(223,223,223)
#define RUFUS_URL                   "https://github.com/pbatard/rufus/wiki/Rufus"
#define BUG_URL                     "https://github.com/pbatard/rufus/issues"
#define IGNORE_RETVAL(expr)         do { (void)(expr); } while(0)
#ifndef ARRAYSIZE
#define ARRAYSIZE(A)                (sizeof(A)/sizeof((A)[0]))
#endif
#define IsChecked(CheckBox_ID)      (IsDlgButtonChecked(hMainDialog, CheckBox_ID) == BST_CHECKED)

#define safe_free(p) do {free((void*)p); p = NULL;} while(0)
#define safe_closehandle(h) do {if (h != INVALID_HANDLE_VALUE) {CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_min(a, b) min((size_t)(a), (size_t)(b))
#define safe_strcp(dst, dst_max, src, count) do {memcpy(dst, src, safe_min(count, dst_max)); \
	((char*)dst)[safe_min(count, dst_max)-1] = 0;} while(0)
#define safe_strcpy(dst, dst_max, src) safe_strcp(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strncat(dst, dst_max, src, count) strncat(dst, src, safe_min(count, dst_max - safe_strlen(dst) - 1))
#define safe_strcat(dst, dst_max, src) safe_strncat(dst, dst_max, src, safe_strlen(src)+1)
#define safe_strcmp(str1, str2) strcmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_stricmp(str1, str2) _stricmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2))
#define safe_strncmp(str1, str2, count) strncmp(((str1==NULL)?"<NULL>":str1), ((str2==NULL)?"<NULL>":str2), count)
#define safe_closehandle(h) do {if (h != INVALID_HANDLE_VALUE) {CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_unlockclose(h) do {if (h != INVALID_HANDLE_VALUE) {UnlockDrive(h); CloseHandle(h); h = INVALID_HANDLE_VALUE;}} while(0)
#define safe_sprintf _snprintf
#define safe_strlen(str) ((((char*)str)==NULL)?0:strlen(str))
#define safe_strdup _strdup
#if defined(_MSC_VER)
#define safe_vsnprintf(buf, size, format, arg) _vsnprintf_s(buf, size, _TRUNCATE, format, arg)
#else
#define safe_vsnprintf vsnprintf
#endif

/*
 * Globals
 */
extern HINSTANCE hMainInstance;
extern HWND hMainDialog;
extern HWND hStatus;
extern float fScale;
extern char szFolderPath[MAX_PATH];

/*
 * Shared prototypes
 */
extern char *WindowsErrorString(void);
extern void CenterDialog(HWND hDlg);
extern void CreateStatusBar(void);
extern INT_PTR CreateAboutBox(void);
extern HWND CreateTooltip(HWND hControl, const char* message, int duration);
extern void DestroyTooltip(HWND hWnd);
extern void DestroyAllTooltips(void);
extern void Notification(int type, char* text, char* title);
extern BOOL ExtractMSDOS(const char* path);

/* Basic String Array */
typedef struct {
	char** Table;
	size_t Size;
	size_t Index;
	size_t Max;
} StrArray;
extern void StrArrayCreate(StrArray* arr, size_t initial_size);
extern void StrArrayAdd(StrArray* arr, const char* str);
extern void StrArrayClear(StrArray* arr);
extern void StrArrayDestroy(StrArray* arr);

#ifdef RUFUS_DEBUG
extern void _uprintf(const char *format, ...);
#define uprintf(...) _uprintf(__VA_ARGS__)
#else
#define uprintf(...)
#endif

/* Custom Windows messages */
enum user_message_type {
	UM_FORMAT_PROGRESS = WM_APP,
	UM_FORMAT_COMPLETED
};

/* Custom notifications */
enum MessageType {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR
};

/* File system indexes in our FS combobox */
// TODO: FormatEx should support "NTFS", "FAT", "FAT32", "UDF", and "EXFAT" as per
// http://msdn.microsoft.com/en-us/library/windows/desktop/aa819439.aspx

enum {
	FS_UNKNOWN = -1,
	FS_FAT16 = 0,
	FS_FAT32,
	FS_NTFS,
	FS_EXFAT,
	FS_MAX
};

typedef struct {
	DWORD DeviceType;
	ULONG DeviceNumber;
	ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_REDEF;

/*
 * typedefs for the function prototypes. Use the something like:
 *   PF_DECL(FormatEx);
 * which translates to:
 *   FormatEx_t pfFormatEx = NULL;
 * in your code, to declare the entrypoint and then use:
 *   PF_INIT(FormatEx, fmifs);
 * which translates to:
 *   pfFormatEx = (FormatEx_t) GetProcAddress(GetDLLHandle("fmifs"), "FormatEx");
 * to make it accessible.
 */
static __inline HMODULE GetDLLHandle(char* szDLLName)
{
	HMODULE h = NULL;
	if ((h = GetModuleHandleA(szDLLName)) == NULL)
		h = LoadLibraryA(szDLLName);
	return h;
}
#define PF_DECL(proc) proc##_t pf##proc = NULL
#define PF_INIT(proc, dllname) pf##proc = (proc##_t) GetProcAddress(GetDLLHandle(#dllname), #proc)
#define PF_INIT_OR_OUT(proc, dllname) \
	PF_INIT(proc, dllname); if (pf##proc == NULL) { \
	uprintf("unable to access %s DLL: %s", #dllname, \
	WindowsErrorString()); goto out; }

/* Callback command types (some errorcode were filled from HPUSBFW V2.2.3 and their
   designation from msdn.microsoft.com/en-us/library/windows/desktop/aa819439.aspx */
typedef enum {
	FCC_PROGRESS,
	FCC_DONE_WITH_STRUCTURE,
	FCC_UNKNOWN2,
	FCC_INCOMPATIBLE_FILE_SYSTEM,
	FCC_UNKNOWN4,
	FCC_UNKNOWN5,
	FCC_ACCESS_DENIED,
	FCC_MEDIA_WRITE_PROTECTED,
	FCC_VOLUME_IN_USE,
	FCC_CANT_QUICK_FORMAT,
	FCC_UNKNOWNA,
	FCC_DONE,
	FCC_BAD_LABEL,
	FCC_UNKNOWND,
	FCC_OUTPUT,
	FCC_STRUCTURE_PROGRESS,
	FCC_CLUSTER_SIZE_TOO_SMALL,
	FCC_CLUSTER_SIZE_TOO_BIG,
	FCC_VOLUME_TOO_SMALL,
	FCC_VOLUME_TOO_BIG,
	FCC_NO_MEDIA_IN_DRIVE,
} FILE_SYSTEM_CALLBACK_COMMAND;

typedef struct {
	DWORD Lines;
	CHAR* Output;
} TEXTOUTPUT, *PTEXTOUTPUT;

typedef BOOLEAN (__stdcall *FILE_SYSTEM_CALLBACK)(
	FILE_SYSTEM_CALLBACK_COMMAND Command,
	ULONG                        Action,
	PVOID                        pData
);

/* Parameter names aligned to
   http://msdn.microsoft.com/en-us/library/windows/desktop/aa819439.aspx */
typedef VOID (WINAPI *FormatEx_t)(
	WCHAR*               DriveRoot,
	MEDIA_TYPE           MediaType,		// See WinIoCtl.h
	WCHAR*               FileSystemTypeName,
	WCHAR*               Label,
	BOOL                 QuickFormat,
	ULONG                DesiredUnitAllocationSize,
	FILE_SYSTEM_CALLBACK Callback
);

/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa383357.aspx */
typedef enum  {
	FPF_COMPRESSED   = 0x01 
} FILE_SYSTEM_PROP_FLAG;

typedef BOOLEAN (WINAPI* EnableVolumeCompression_t)(
	WCHAR*          DriveRoot,
	ULONG           CompressionFlags	// FILE_SYSTEM_PROP_FLAG
);

/* Custom application errors */
#define FAC(f)                         (f<<16)
#define ERROR_INCOMPATIBLE_FS          (APPLICATION_ERROR_MASK|0x1201)
#define ERROR_CANT_QUICK_FORMAT        (APPLICATION_ERROR_MASK|0x1202)
#define ERROR_INVALID_CLUSTER_SIZE     (APPLICATION_ERROR_MASK|0x1203)
#define ERROR_INVALID_VOLUME_SIZE      (APPLICATION_ERROR_MASK|0x1204)
#define ERROR_CANT_START_THREAD        (APPLICATION_ERROR_MASK|0x1205)
