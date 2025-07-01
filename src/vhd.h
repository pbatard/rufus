/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling definitions and prototypes
 * Copyright © 2022-2025 Pete Batard <pete@akeo.ie>
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

#include <stdint.h>
#include <windows.h>
#include <virtdisk.h>

#pragma once

// Someone's going to have to explain to me why trying to define ANY static inline function
// calls in wimlib.h in DEBUG produces "C2143: syntax error: missing ';' before '{'"  with
// MSVC, UNLESS we add the following 2 include statements here (in no particular order).
// WHAT. THE. ACTUAL. FUCK?!?!?!
#if defined(_MSC_VER) && defined(_DEBUG)
#include "msapi_utf8.h"
#include "wimlib.h"
#endif

#define WIM_MAGIC							0x0000004D4957534DULL	// "MSWIM\0\0\0"

#define MBR_SIZE							512	// Might need to review this once we see bootable 4k systems

#define VIRTUAL_STORAGE_TYPE_DEVICE_FFU                    99
#define CREATE_VIRTUAL_DISK_VERSION_2                       2
#define CREATE_VIRTUAL_DISK_FLAG_CREATE_BACKING_STORAGE     8

#ifndef CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_BLOCK_SIZE
#define CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_BLOCK_SIZE   0
#endif

#ifndef CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_SECTOR_SIZE
#define CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_SECTOR_SIZE  0
#endif

typedef struct
{
	CREATE_VIRTUAL_DISK_VERSION Version;
	union
	{
		struct
		{
			GUID                  UniqueId;
			ULONGLONG             MaximumSize;
			ULONG                 BlockSizeInBytes;
			ULONG                 SectorSizeInBytes;
			PCWSTR                ParentPath;
			PCWSTR                SourcePath;
		} Version1;
		struct
		{
			GUID                   UniqueId;
			ULONGLONG              MaximumSize;
			ULONG                  BlockSizeInBytes;
			ULONG                  SectorSizeInBytes;
			ULONG                  PhysicalSectorSizeInBytes;
			PCWSTR                 ParentPath;
			PCWSTR                 SourcePath;
			OPEN_VIRTUAL_DISK_FLAG OpenFlags;
			VIRTUAL_STORAGE_TYPE   ParentVirtualStorageType;
			VIRTUAL_STORAGE_TYPE   SourceVirtualStorageType;
			GUID                   ResiliencyGuid;
		} Version2;
	};
} STOPGAP_CREATE_VIRTUAL_DISK_PARAMETERS;

// From https://docs.microsoft.com/en-us/previous-versions/msdn10/dd834960(v=msdn.10)
// as well as https://msfn.org/board/topic/150700-wimgapi-wimmountimage-progressbar/
enum WIMMessage {
	WIM_MSG = WM_APP + 0x1476,
	WIM_MSG_TEXT,
	WIM_MSG_PROGRESS,	// Indicates an update in the progress of an image application.
	WIM_MSG_PROCESS,	// Enables the caller to prevent a file or a directory from being captured or applied.
	WIM_MSG_SCANNING,	// Indicates that volume information is being gathered during an image capture.
	WIM_MSG_SETRANGE,	// Indicates the number of files that will be captured or applied.
	WIM_MSG_SETPOS,		// Indicates the number of files that have been captured or applied.
	WIM_MSG_STEPIT,		// Indicates that a file has been either captured or applied.
	WIM_MSG_COMPRESS,	// Enables the caller to prevent a file resource from being compressed during a capture.
	WIM_MSG_ERROR,		// Alerts the caller that an error has occurred while capturing or applying an image.
	WIM_MSG_ALIGNMENT,	// Enables the caller to align a file resource on a particular alignment boundary.
	WIM_MSG_RETRY,		// Sent when the file is being reapplied because of a network timeout.
	WIM_MSG_SPLIT,		// Enables the caller to align a file resource on a particular alignment boundary.
	WIM_MSG_FILEINFO,	// Used in conjunction with WimApplyImages()'s WIM_FLAG_FILEINFO flag to provide detailed file info.
	WIM_MSG_INFO,		// Sent when an info message is available.
	WIM_MSG_WARNING,	// Sent when a warning message is available.
	WIM_MSG_CHK_PROCESS,
	WIM_MSG_SUCCESS = 0,
	WIM_MSG_ABORT_IMAGE = -1
};

extern uint32_t GetWimVersion(const char* image);
extern BOOL WimExtractFile(const char* wim_image, int index, const char* src, const char* dst);
extern BOOL WimApplyImage(const char* image, int index, const char* dst);
extern BOOL WimSplitFile(const char* src, const char* dst);
extern int8_t IsBootableImage(const char* path);
extern char* VhdMountImageAndGetSize(const char* path, uint64_t* disksize);
#define VhdMountImage(path) VhdMountImageAndGetSize(path, NULL)
extern void VhdUnmountImage(void);
extern BOOL SaveImage(void);
extern void OpticalDiscSaveImage(void);
extern DWORD WINAPI IsoSaveImageThread(void* param);
