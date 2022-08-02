/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling definitions and prototypes
 * Copyright © 2022 Pete Batard <pete@akeo.ie>
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

#pragma once

#define VHD_FOOTER_COOKIE					{ 'c', 'o', 'n', 'e', 'c', 't', 'i', 'x' }

#define VHD_FOOTER_FEATURES_NONE			0x00000000
#define VHD_FOOTER_FEATURES_TEMPORARY		0x00000001
#define VHD_FOOTER_FEATURES_RESERVED		0x00000002

#define VHD_FOOTER_FILE_FORMAT_V1_0			0x00010000

#define VHD_FOOTER_DATA_OFFSET_FIXED_DISK	0xFFFFFFFFFFFFFFFFULL

#define VHD_FOOTER_CREATOR_HOST_OS_WINDOWS	{ 'W', 'i', '2', 'k' }
#define VHD_FOOTER_CREATOR_HOST_OS_MAC		{ 'M', 'a', 'c', ' ' }

#define VHD_FOOTER_TYPE_FIXED_HARD_DISK		0x00000002
#define VHD_FOOTER_TYPE_DYNAMIC_HARD_DISK	0x00000003
#define VHD_FOOTER_TYPE_DIFFER_HARD_DISK	0x00000004

#define WIM_MAGIC							0x0000004D4957534DULL	// "MSWIM\0\0\0"
#define WIM_HAS_API_EXTRACT					1
#define WIM_HAS_7Z_EXTRACT					2
#define WIM_HAS_API_APPLY					4
#define WIM_HAS_EXTRACT(r)					(r & (WIM_HAS_API_EXTRACT|WIM_HAS_7Z_EXTRACT))

#define SECONDS_SINCE_JAN_1ST_2000			946684800

#define INVALID_CALLBACK_VALUE				0xFFFFFFFF

#define WIM_FLAG_RESERVED					0x00000001
#define WIM_FLAG_VERIFY						0x00000002
#define WIM_FLAG_INDEX						0x00000004
#define WIM_FLAG_NO_APPLY					0x00000008
#define WIM_FLAG_NO_DIRACL					0x00000010
#define WIM_FLAG_NO_FILEACL					0x00000020
#define WIM_FLAG_SHARE_WRITE				0x00000040
#define WIM_FLAG_FILEINFO					0x00000080
#define WIM_FLAG_NO_RP_FIX					0x00000100

// Bitmask for the kind of progress we want to report in the WIM progress callback
#define WIM_REPORT_PROGRESS					0x00000001
#define WIM_REPORT_PROCESS					0x00000002
#define WIM_REPORT_FILEINFO					0x00000004

#define WIM_GENERIC_READ					GENERIC_READ
#define WIM_OPEN_EXISTING					OPEN_EXISTING
#define WIM_UNDOCUMENTED_BULLSHIT			0x20000000

#define MBR_SIZE							512	// Might need to review this once we see bootable 4k systems


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

/*
 * VHD Fixed HD footer (Big Endian)
 * http://download.microsoft.com/download/f/f/e/ffef50a5-07dd-4cf8-aaa3-442c0673a029/Virtual%20Hard%20Disk%20Format%20Spec_10_18_06.doc
 * NB: If a dymamic implementation is needed, check the GPL v3 compatible C++ implementation from:
 * https://sourceforge.net/p/urbackup/backend/ci/master/tree/fsimageplugin/
 */
#pragma pack(push, 1)
typedef struct vhd_footer {
	char		cookie[8];
	uint32_t	features;
	uint32_t	file_format_version;
	uint64_t	data_offset;
	uint32_t	timestamp;
	char		creator_app[4];
	uint32_t	creator_version;
	char		creator_host_os[4];
	uint64_t	original_size;
	uint64_t	current_size;
	union {
		uint32_t	geometry;
		struct {
			uint16_t	cylinders;
			uint8_t		heads;
			uint8_t		sectors;
		} chs;
	} disk_geometry;
	uint32_t	disk_type;
	uint32_t	checksum;
	uuid_t		unique_id;
	uint8_t		saved_state;
	uint8_t		reserved[427];
} vhd_footer;
#pragma pack(pop)

extern uint8_t WimExtractCheck(BOOL bSilent);
extern BOOL WimExtractFile(const char* wim_image, int index, const char* src, const char* dst, BOOL bSilent);
extern BOOL WimExtractFile_API(const char* image, int index, const char* src, const char* dst, BOOL bSilent);
extern BOOL WimExtractFile_7z(const char* image, int index, const char* src, const char* dst, BOOL bSilent);
extern BOOL WimApplyImage(const char* image, int index, const char* dst);
extern char* WimMountImage(const char* image, int index);
extern BOOL WimUnmountImage(const char* image, int index);
extern BOOL WimIsValidIndex(const char* image, int index);
extern int8_t IsBootableImage(const char* path);
extern BOOL AppendVHDFooter(const char* vhd_path);
