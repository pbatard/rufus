/*
 * Rufus: The Reliable USB Formatting Utility
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
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
#include <winioctl.h>               // for DISK_GEOMETRY

#pragma once

/* Program options */
#define RUFUS_DEBUG                 // print debug info to Debug facility (use debugview to consult)
#define DISABLE_AUTORUN             // disable new USB drive notification from explorer when application is running

#define STR_NO_LABEL                "NO_LABEL"
#define RUFUS_CANCELBOX_TITLE       "Rufus - Cancellation"
#define DRIVE_INDEX_MIN             0x80
#define DRIVE_INDEX_MAX             0xC0
#define MAX_DRIVES                  16
#define MAX_TOOLTIPS                16
#define MAX_PROGRESS                (0xFFFF-1)	// leave room for 1 more for insta-progress workaround
#define PROPOSEDLABEL_TOLERANCE     0.10
#define FS_DEFAULT                  FS_FAT32
#define WHITE                       RGB(255,255,255)
#define SEPARATOR_GREY              RGB(223,223,223)
#define RUFUS_URL                   "http://rufus.akeo.ie"
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

#ifdef RUFUS_DEBUG
extern void _uprintf(const char *format, ...);
#define uprintf(...) _uprintf(__VA_ARGS__)
#else
#define uprintf(...)
#endif

/* Custom Windows messages */
enum user_message_type {
	UM_FORMAT_COMPLETED = WM_APP
};

/* Custom notifications */
enum notification_type {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR
};

/* Timers used throughout the program */
enum timer_type {
	TID_MESSAGE = 0x1000,
	TID_BADBLOCKS_UPDATE,
	TID_APP_TIMER
};

/* Action type, for progress bar breakdown */
enum action_type {
	OP_BADBLOCKS,
	OP_ZERO_MBR,
	OP_PARTITION,
	OP_FORMAT,
	OP_CREATE_FS,
	OP_FIX_MBR,
	OP_DOS,
	OP_MAX
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

enum dos_type {
	DT_WINME = 0,
	DT_FREEDOS,
	DT_SYSLINUX,
	DT_MAX
};

/* Current drive info */
typedef struct {
	DWORD DeviceNumber;
	LONGLONG DiskSize;
	DISK_GEOMETRY Geometry;
	DWORD FirstSector;
	int FSType;
	struct {
		ULONG Allowed;
		ULONG Default;
	} ClusterSize[FS_MAX];
} RUFUS_DRIVE_INFO;

/*
 * Globals
 */
extern HINSTANCE hMainInstance;
extern HWND hMainDialog, hStatus, hDeviceList, hCapacity;
extern HWND hFileSystem, hClusterSize, hLabel, hDOSType, hNBPasses;
extern float fScale;
extern char szFolderPath[MAX_PATH];
extern DWORD FormatStatus;
extern RUFUS_DRIVE_INFO SelectedDrive;
extern const int nb_steps[FS_MAX];
extern BOOL bWithFreeDOS;

/*
 * Shared prototypes
 */
extern const char *WindowsErrorString(void);
extern void DumpBufferHex(void *buf, size_t size);
extern void PrintStatus(unsigned int duration, const char *format, ...);
extern void UpdateProgress(int op, float percent);
extern const char* StrError(DWORD error_code);
extern void CenterDialog(HWND hDlg);
extern void CreateStatusBar(void);
extern INT_PTR CreateAboutBox(void);
extern HWND CreateTooltip(HWND hControl, const char* message, int duration);
extern void DestroyTooltip(HWND hWnd);
extern void DestroyAllTooltips(void);
extern BOOL Notification(int type, char* title, char* format, ...);
extern BOOL ExtractDOS(const char* path);
extern BOOL ExtractISO(const char* src_iso, const char* dest_dir);
extern BOOL InstallSyslinux(DWORD num, const char* drive_name);
extern void __cdecl FormatThread(void* param);
extern BOOL CreatePartition(HANDLE hDrive);
extern HANDLE GetDriveHandle(DWORD DriveIndex, char* DriveLetter, BOOL bWriteAccess, BOOL bLockDrive);
extern BOOL GetDriveLabel(DWORD DriveIndex, char* letter, char** label);
extern BOOL UnmountDrive(HANDLE hDrive);

__inline static BOOL UnlockDrive(HANDLE hDrive)
{
	DWORD size;
	return DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &size, NULL);
}

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

/* Clang/MinGW32 has an issue with intptr_t */
#ifndef _UINTPTR_T_DEFINED
#define _UINTPTR_T_DEFINED
#ifdef _WIN64
  typedef unsigned __int64 uintptr_t;
#else
  typedef unsigned int uintptr_t;
#endif
#endif

/* We need a redef of this MS structure */
typedef struct {
	DWORD DeviceType;
	ULONG DeviceNumber;
	ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_REDEF;


/* Custom application errors */
#define FAC(f)                         (f<<16)
#define APPERR(err)                    (APPLICATION_ERROR_MASK|err)
#define ERROR_INCOMPATIBLE_FS          0x1201
#define ERROR_CANT_QUICK_FORMAT        0x1202
#define ERROR_INVALID_CLUSTER_SIZE     0x1203
#define ERROR_INVALID_VOLUME_SIZE      0x1204
#define ERROR_CANT_START_THREAD        0x1205
#define ERROR_BADBLOCKS_FAILURE        0x1206
