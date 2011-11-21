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

#pragma once

#define RUFUS_DEBUG

#define APP_VERSION                 "Rufus v1.0.0.1"
#define MAX_TOOLTIPS                16
#define WHITE                       RGB(255,255,255)
#define SEPARATOR_GREY              RGB(223,223,223)
#define RUFUS_URL                   "https://github.com/pbatard/rufus/wiki/Rufus"
#define BUG_URL                     "https://github.com/pbatard/rufus/issues"
#define IGNORE_RETVAL(expr)         do { (void)(expr); } while(0)
#ifndef ARRAYSIZE
#define ARRAYSIZE(A)                (sizeof(A)/sizeof((A)[0]))
#endif

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
extern HWND CreateTooltip(HWND hControl, char* message, int duration);
extern void DestroyTooltip(HWND hWnd);
extern void DestroyAllTooltips(void);

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

/* Custom notifications */
enum MessageType {
	MSG_INFO,
	MSG_WARNING,
	MSG_ERROR
};

typedef struct {
	DWORD DeviceType;
	ULONG DeviceNumber;
	ULONG PartitionNumber;
} STORAGE_DEVICE_NUMBER_REDEF;

typedef struct _SCSI_PASS_THROUGH {
	USHORT Length;
	UCHAR ScsiStatus;
	UCHAR PathId;
	UCHAR TargetId;
	UCHAR Lun;
	UCHAR CdbLength;
	UCHAR SenseInfoLength;
	UCHAR DataIn;
	ULONG DataTransferLength;
	ULONG TimeOutValue;
	ULONG_PTR DataBufferOffset;
	ULONG SenseInfoOffset;
	UCHAR Cdb[16];
} SCSI_PASS_THROUGH,*PSCSI_PASS_THROUGH;

typedef struct _SCSI_PASS_THROUGH_WITH_BUFFERS {
	SCSI_PASS_THROUGH Spt;
	ULONG             Filler;
	UCHAR             SenseBuf[32];
	UCHAR             DataBuf[512];
} SCSI_PASS_THROUGH_WITH_BUFFERS, *PSCSI_PASS_THROUGH_WITH_BUFFERS;
