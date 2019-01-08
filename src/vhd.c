/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling functions
 * Copyright © 2013-2016 Pete Batard <pete@akeo.ie>
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
#include <stdlib.h>
#include <io.h>
#include <rpc.h>
#include <time.h>

#include "rufus.h"
#include "missing.h"
#include "resource.h"
#include "msapi_utf8.h"

#include "drive.h"
#include "registry.h"
#include "bled/bled.h"

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

#define SECONDS_SINCE_JAN_1ST_2000			946684800

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

// WIM API Prototypes
#define WIM_GENERIC_READ            GENERIC_READ
#define WIM_OPEN_EXISTING           OPEN_EXISTING
#define WIM_UNDOCUMENTED_BULLSHIT   0x20000000
PF_TYPE_DECL(WINAPI, HANDLE, WIMCreateFile, (PWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMSetTemporaryPath, (HANDLE, PWSTR));
PF_TYPE_DECL(WINAPI, HANDLE, WIMLoadImage, (HANDLE, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMApplyImage, (HANDLE, PCWSTR, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMExtractImagePath, (HANDLE, PWSTR, PWSTR, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMGetImageInformation, (HANDLE, PVOID, PDWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMCloseHandle, (HANDLE));
PF_TYPE_DECL(WINAPI, DWORD, WIMRegisterMessageCallback, (HANDLE, FARPROC, PVOID));
PF_TYPE_DECL(WINAPI, DWORD, WIMUnregisterMessageCallback, (HANDLE, FARPROC));
PF_TYPE_DECL(RPC_ENTRY, RPC_STATUS, UuidCreate, (UUID __RPC_FAR*));

static uint8_t wim_flags = 0;
static char sevenzip_path[MAX_PATH];
static const char conectix_str[] = VHD_FOOTER_COOKIE;
static uint32_t wim_nb_files, wim_proc_files;
static BOOL count_files;
static uint64_t LastRefresh;

static BOOL Get7ZipPath(void)
{
	if ( (GetRegistryKeyStr(REGKEY_HKCU, "7-Zip\\Path", sevenzip_path, sizeof(sevenzip_path)))
	  || (GetRegistryKeyStr(REGKEY_HKLM, "7-Zip\\Path", sevenzip_path, sizeof(sevenzip_path))) ) {
		static_strcat(sevenzip_path, "\\7z.exe");
		return (_access(sevenzip_path, 0) != -1);
	}
	return FALSE;
}

BOOL AppendVHDFooter(const char* vhd_path)
{
	const char creator_os[4] = VHD_FOOTER_CREATOR_HOST_OS_WINDOWS;
	const char creator_app[4] = { 'r', 'u', 'f', 's' };
	BOOL r = FALSE;
	DWORD size;
	LARGE_INTEGER li;
	HANDLE handle = INVALID_HANDLE_VALUE;
	vhd_footer* footer = NULL;
	uint64_t totalSectors;
	uint16_t cylinders = 0;
	uint8_t heads, sectorsPerTrack;
	uint32_t cylinderTimesHeads;
	uint32_t checksum;
	size_t i;

	PF_INIT(UuidCreate, Rpcrt4);
	handle = CreateFileU(vhd_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	li.QuadPart = 0;
	if ((handle == INVALID_HANDLE_VALUE) || (!SetFilePointerEx(handle, li, &li, FILE_END))) {
		uprintf("Could not open image '%s': %s", vhd_path, WindowsErrorString());
		goto out;
	}
	footer = (vhd_footer*)calloc(1, sizeof(vhd_footer));
	if (footer == NULL) {
		uprintf("Could not allocate VHD footer");
		goto out;
	}

	memcpy(footer->cookie, conectix_str, sizeof(footer->cookie));
	footer->features = bswap_uint32(VHD_FOOTER_FEATURES_RESERVED);
	footer->file_format_version = bswap_uint32(VHD_FOOTER_FILE_FORMAT_V1_0);
	footer->data_offset = bswap_uint64(VHD_FOOTER_DATA_OFFSET_FIXED_DISK);
	footer->timestamp = bswap_uint32((uint32_t)(_time64(NULL) - SECONDS_SINCE_JAN_1ST_2000));
	memcpy(footer->creator_app, creator_app, sizeof(creator_app));
	footer->creator_version = bswap_uint32((rufus_version[0]<<16)|rufus_version[1]);
	memcpy(footer->creator_host_os, creator_os, sizeof(creator_os));
	footer->original_size = bswap_uint64(li.QuadPart);
	footer->current_size = footer->original_size;
	footer->disk_type = bswap_uint32(VHD_FOOTER_TYPE_FIXED_HARD_DISK);
	if ((pfUuidCreate == NULL) || (pfUuidCreate(&footer->unique_id) != RPC_S_OK))
		uprintf("Warning: could not set VHD UUID");

	// Compute CHS, as per the VHD specs
	totalSectors = li.QuadPart / 512;
	if (totalSectors > 65535 * 16 * 255) {
		totalSectors = 65535 * 16 * 255;
	}

	if (totalSectors >= 65535 * 16 * 63) {
		sectorsPerTrack = 255;
		heads = 16;
		cylinderTimesHeads = (uint32_t)(totalSectors / sectorsPerTrack);
	} else {
		sectorsPerTrack = 17;
		cylinderTimesHeads = (uint32_t)(totalSectors / sectorsPerTrack);

		heads = (cylinderTimesHeads + 1023) / 1024;

		if (heads < 4) {
			heads = 4;
		}
		if (cylinderTimesHeads >= ((uint32_t)heads * 1024) || heads > 16) {
			sectorsPerTrack = 31;
			heads = 16;
			cylinderTimesHeads = (uint32_t)(totalSectors / sectorsPerTrack);
		}
		if (cylinderTimesHeads >= ((uint32_t)heads * 1024)) {
			sectorsPerTrack = 63;
			heads = 16;
			cylinderTimesHeads = (uint32_t)(totalSectors / sectorsPerTrack);
		}
	}
	cylinders = cylinderTimesHeads / heads;
	footer->disk_geometry.chs.cylinders = bswap_uint16(cylinders);
	footer->disk_geometry.chs.heads = heads;
	footer->disk_geometry.chs.sectors = sectorsPerTrack;

	// Compute the VHD footer checksum
	for (checksum=0, i=0; i<sizeof(vhd_footer); i++)
		checksum += ((uint8_t*)footer)[i];
	footer->checksum = bswap_uint32(~checksum);

	if (!WriteFileWithRetry(handle, footer, sizeof(vhd_footer), &size, WRITE_RETRIES)) {
		uprintf("Could not write VHD footer: %s", WindowsErrorString());
		goto out;
	}
	r = TRUE;

out:
	safe_free(footer);
	safe_closehandle(handle);
	return r;
}

typedef struct {
	const char* ext;
	bled_compression_type type;
} comp_assoc;

static comp_assoc file_assoc[] = {
	{ ".zip", BLED_COMPRESSION_ZIP },
	{ ".Z", BLED_COMPRESSION_LZW },
	{ ".gz", BLED_COMPRESSION_GZIP },
	{ ".lzma", BLED_COMPRESSION_LZMA },
	{ ".bz2", BLED_COMPRESSION_BZIP2 },
	{ ".xz", BLED_COMPRESSION_XZ },
};

// For now we consider that an image that matches a known extension is bootable
#define MBR_SIZE 512	// Might need to review this once we see bootable 4k systems
BOOL IsCompressedBootableImage(const char* path)
{
	char *p;
	unsigned char *buf = NULL;
	int i;
	BOOL r = FALSE;
	int64_t dc;

	img_report.compression_type = BLED_COMPRESSION_NONE;
	for (p = (char*)&path[strlen(path)-1]; (*p != '.') && (p != path); p--);

	if (p == path)
		return FALSE;

	for (i = 0; i<ARRAYSIZE(file_assoc); i++) {
		if (strcmp(p, file_assoc[i].ext) == 0) {
			img_report.compression_type = file_assoc[i].type;
			buf = malloc(MBR_SIZE);
			if (buf == NULL)
				return FALSE;
			FormatStatus = 0;
			bled_init(_uprintf, NULL, &FormatStatus);
			dc = bled_uncompress_to_buffer(path, (char*)buf, MBR_SIZE, file_assoc[i].type);
			bled_exit();
			if (dc != MBR_SIZE) {
				free(buf);
				return FALSE;
			}
			r = (buf[0x1FE] == 0x55) && (buf[0x1FF] == 0xAA);
			free(buf);
			return r;
		}
	}

	return FALSE;
}


BOOL IsBootableImage(const char* path)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	LARGE_INTEGER liImageSize;
	vhd_footer* footer = NULL;
	DWORD size;
	size_t i;
	uint32_t checksum, old_checksum;
	LARGE_INTEGER ptr;
	BOOL is_bootable_img = FALSE;

	uprintf("Disk image analysis:");
	handle = CreateFileU(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("  Could not open image '%s'", path);
		goto out;
	}

	is_bootable_img = (BOOLEAN)IsCompressedBootableImage(path);
	if (img_report.compression_type == BLED_COMPRESSION_NONE)
		is_bootable_img = (BOOLEAN)AnalyzeMBR(handle, "  Image", FALSE);

	if (!GetFileSizeEx(handle, &liImageSize)) {
		uprintf("  Could not get image size: %s", WindowsErrorString());
		goto out;
	}
	img_report.image_size = (uint64_t)liImageSize.QuadPart;

	size = sizeof(vhd_footer);
	if ((img_report.compression_type == BLED_COMPRESSION_NONE) && (img_report.image_size >= (512 + size))) {
		footer = (vhd_footer*)malloc(size);
		ptr.QuadPart = img_report.image_size - size;
		if ( (footer == NULL) || (!SetFilePointerEx(handle, ptr, NULL, FILE_BEGIN)) ||
			 (!ReadFile(handle, footer, size, &size, NULL)) || (size != sizeof(vhd_footer)) ) {
			uprintf("  Could not read VHD footer");
			goto out;
		}
		if (memcmp(footer->cookie, conectix_str, sizeof(footer->cookie)) == 0) {
			img_report.image_size -= sizeof(vhd_footer);
			if ( (bswap_uint32(footer->file_format_version) != VHD_FOOTER_FILE_FORMAT_V1_0)
			  || (bswap_uint32(footer->disk_type) != VHD_FOOTER_TYPE_FIXED_HARD_DISK)) {
				uprintf("  Unsupported type of VHD image");
				is_bootable_img = FALSE;
				goto out;
			}
			// Might as well validate the checksum while we're at it
			old_checksum = bswap_uint32(footer->checksum);
			footer->checksum = 0;
			for (checksum=0, i=0; i<sizeof(vhd_footer); i++)
				checksum += ((uint8_t*)footer)[i];
			checksum = ~checksum;
			if (checksum != old_checksum)
				uprintf("  Warning: VHD footer seems corrupted (checksum: %04X, expected: %04X)", old_checksum, checksum);
			// Need to remove the footer from our payload
			uprintf("  Image is a Fixed Hard Disk VHD file");
			img_report.is_vhd = TRUE;
		}
	}

out:
	safe_free(footer);
	safe_closehandle(handle);
	return is_bootable_img;
}

#define WIM_HAS_API_EXTRACT 1
#define WIM_HAS_7Z_EXTRACT  2
#define WIM_HAS_API_APPLY   4
#define WIM_HAS_EXTRACT(r) (r & (WIM_HAS_API_EXTRACT|WIM_HAS_7Z_EXTRACT))

// Find out if we have any way to extract/apply WIM files on this platform
// Returns a bitfield of the methods we can use (1 = Extract using wimgapi, 2 = Extract using 7-Zip, 4 = Apply using wimgapi)
uint8_t WimExtractCheck(void)
{
	PF_INIT(WIMCreateFile, Wimgapi);
	PF_INIT(WIMSetTemporaryPath, Wimgapi);
	PF_INIT(WIMLoadImage, Wimgapi);
	PF_INIT(WIMApplyImage, Wimgapi);
	PF_INIT(WIMExtractImagePath, Wimgapi);
	PF_INIT(WIMGetImageInformation, Wimgapi);
	PF_INIT(WIMRegisterMessageCallback, Wimgapi);
	PF_INIT(WIMUnregisterMessageCallback, Wimgapi);
	PF_INIT(WIMCloseHandle, Wimgapi);

	if (pfWIMCreateFile && pfWIMSetTemporaryPath && pfWIMLoadImage && pfWIMExtractImagePath && pfWIMCloseHandle)
		wim_flags |= WIM_HAS_API_EXTRACT;
	if (Get7ZipPath())
		wim_flags |= WIM_HAS_7Z_EXTRACT;
	if ((wim_flags & WIM_HAS_API_EXTRACT) && pfWIMApplyImage && pfWIMRegisterMessageCallback && pfWIMUnregisterMessageCallback)
		wim_flags |= WIM_HAS_API_APPLY;

	uprintf("WIM extraction method(s) supported: %s%s%s", (wim_flags & WIM_HAS_7Z_EXTRACT)?"7-Zip":
		((wim_flags & WIM_HAS_API_EXTRACT)?"":"NONE"),
		(WIM_HAS_EXTRACT(wim_flags) == (WIM_HAS_API_EXTRACT|WIM_HAS_7Z_EXTRACT))?", ":
		"", (wim_flags & WIM_HAS_API_EXTRACT)?"wimgapi.dll":"");
	uprintf("WIM apply method supported: %s", (wim_flags & WIM_HAS_API_APPLY)?"wimgapi.dll":"NONE");
	return wim_flags;
}


// Extract a file from a WIM image using wimgapi.dll (Windows 7 or later)
// NB: if you want progress from a WIM callback, you must run the WIM API call in its own thread
// (which we don't do here) as it won't work otherwise. Thanks go to Erwan for figuring this out!
BOOL WimExtractFile_API(const char* image, int index, const char* src, const char* dst)
{
	static char* index_name = "[1].xml";
	BOOL r = FALSE;
	DWORD dw = 0;
	HANDLE hWim = NULL;
	HANDLE hImage = NULL;
	HANDLE hFile = NULL;
	wchar_t wtemp[MAX_PATH] = {0};
	wchar_t* wimage = utf8_to_wchar(image);
	wchar_t* wsrc = utf8_to_wchar(src);
	wchar_t* wdst = utf8_to_wchar(dst);
	char* wim_info;

	PF_INIT_OR_OUT(WIMCreateFile, Wimgapi);
	PF_INIT_OR_OUT(WIMSetTemporaryPath, Wimgapi);
	PF_INIT_OR_OUT(WIMLoadImage, Wimgapi);
	PF_INIT_OR_OUT(WIMExtractImagePath, Wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, Wimgapi);

	uprintf("Opening: %s:[%d] (API)", image, index);
	if (GetTempPathW(ARRAYSIZE(wtemp), wtemp) == 0) {
		uprintf("  Could not fetch temp path: %s", WindowsErrorString());
		goto out;
	}

	// Thanks to dism++ for figuring out that you can use UNDOCUMENTED FLAG 0x20000000
	// to open newer install.wim/install.esd images, without running into obnoxious error:
	// [0x0000000B] An attempt was made to load a program with an incorrect format.
	// No thanks to Microsoft for NOT DOCUMENTING THEIR UTTER BULLSHIT with the WIM API!
	hWim = pfWIMCreateFile(wimage, WIM_GENERIC_READ, WIM_OPEN_EXISTING,
		(img_report.wininst_version >= SPECIAL_WIM_VERSION) ? WIM_UNDOCUMENTED_BULLSHIT : 0, 0, NULL);
	if (hWim == NULL) {
		uprintf("  Could not access image: %s", WindowsErrorString());
		goto out;
	}

	if (!pfWIMSetTemporaryPath(hWim, wtemp)) {
		uprintf("  Could not set temp path: %s", WindowsErrorString());
		goto out;
	}

	uprintf("Extracting: %s (From %s)", dst, src);
	if (safe_strcmp(src, index_name) == 0) {
		if (!pfWIMGetImageInformation(hWim, &wim_info, &dw)) {
			uprintf("  Could not access WIM info: %s", WindowsErrorString());
			goto out;
		}
		hFile = CreateFileW(wdst, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if ((hFile == INVALID_HANDLE_VALUE) || (!WriteFile(hFile, wim_info, dw, &dw, NULL))) {
			uprintf("  Could not extract file: %s", WindowsErrorString());
			goto out;
		}
	} else {
		hImage = pfWIMLoadImage(hWim, (DWORD)index);
		if (hImage == NULL) {
			uprintf("  Could not set index: %s", WindowsErrorString());
			goto out;
		}
		if (!pfWIMExtractImagePath(hImage, wsrc, wdst, 0)) {
			uprintf("  Could not extract file: %s", WindowsErrorString());
			goto out;
		}
	}
	r = TRUE;

out:
	if ((hImage != NULL) || (hWim != NULL)) {
		uprintf("Closing: %s", image);
		if (hImage != NULL) pfWIMCloseHandle(hImage);
		if (hWim != NULL) pfWIMCloseHandle(hWim);
	}
	safe_closehandle(hFile);
	safe_free(wimage);
	safe_free(wsrc);
	safe_free(wdst);
	return r;
}

// Extract a file from a WIM image using 7-Zip
BOOL WimExtractFile_7z(const char* image, int index, const char* src, const char* dst)
{
	int n;
	size_t i;
	char cmdline[MAX_PATH];
	char tmpdst[MAX_PATH];
	char index_prefix[] = "#\\";

	uprintf("Opening: %s:[%d] (7-Zip)", image, index);

	if ((image == NULL) || (src == NULL) || (dst == NULL))
		return FALSE;

	// If you shove more than 9 images in a WIM, don't come complaining
	// that this breaks!
	index_prefix[0] = '0' + index;

	uprintf("Extracting: %s (From %s)", dst, src);

	// 7z has a quirk where the image index MUST be specified if a
	// WIM has multiple indexes, but it MUST be removed if there is
	// only one image. Because of this (and because 7z will not
	// return an error code if it can't extract the file), we need
	// to issue 2 passes. See github issue #680.
	for (n = 0; n < 2; n++) {
		static_strcpy(tmpdst, dst);
		for (i = strlen(tmpdst) - 1; (i > 0) && (tmpdst[i] != '\\') && (tmpdst[i] != '/'); i--);
		tmpdst[i] = 0;

		static_sprintf(cmdline, "\"%s\" -y e \"%s\" %s%s", sevenzip_path,
			image, (n == 0) ? index_prefix : "", src);
		if (RunCommand(cmdline, tmpdst, FALSE) != 0) {
			uprintf("  Could not launch 7z.exe: %s", WindowsErrorString());
			return FALSE;
		}

		for (i = safe_strlen(src); (i > 0) && (src[i] != '\\') && (src[i] != '/'); i--);
		if (i == 0)
			static_strcat(tmpdst, "\\");
		static_strcat(tmpdst, &src[i]);
		if (_access(tmpdst, 0) == 0)
			// File was extracted => move on
			break;
	}

	if (n >= 2) {
		uprintf("  7z.exe did not extract %s", tmpdst);
		return FALSE;
	}

	// coverity[toctou]
	if (rename(tmpdst, dst) != 0) {
		uprintf("  Could not rename %s to %s: errno %d", tmpdst, dst, errno);
		return FALSE;
	}

	return TRUE;
}

// Extract a file from a WIM image
BOOL WimExtractFile(const char* image, int index, const char* src, const char* dst)
{
	if ((wim_flags == 0) && (!WIM_HAS_EXTRACT(WimExtractCheck())))
		return FALSE;
	if ((image == NULL) || (src == NULL) || (dst == NULL))
		return FALSE;

	// Prefer 7-Zip as, unsurprisingly, it's faster than the Microsoft way,
	// but allow fallback if 7-Zip doesn't succeed
	return ( ((wim_flags & WIM_HAS_7Z_EXTRACT) && WimExtractFile_7z(image, index, src, dst))
		  || ((wim_flags & WIM_HAS_API_EXTRACT) && WimExtractFile_API(image, index, src, dst)) );
}

// Apply image functionality
static const char *_image, *_dst;
static int _index;

// From http://msdn.microsoft.com/en-us/library/windows/desktop/dd834960.aspx
// as well as http://www.msfn.org/board/topic/150700-wimgapi-wimmountimage-progressbar/
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
	WIM_MSG_SUCCESS = 0x00000000,
	WIM_MSG_ABORT_IMAGE = -1
};

#define INVALID_CALLBACK_VALUE 0xFFFFFFFF

#define WIM_FLAG_RESERVED      0x00000001
#define WIM_FLAG_VERIFY        0x00000002
#define WIM_FLAG_INDEX         0x00000004
#define WIM_FLAG_NO_APPLY      0x00000008
#define WIM_FLAG_NO_DIRACL     0x00000010
#define WIM_FLAG_NO_FILEACL    0x00000020
#define WIM_FLAG_SHARE_WRITE   0x00000040
#define WIM_FLAG_FILEINFO      0x00000080
#define WIM_FLAG_NO_RP_FIX     0x00000100

// Progress callback
DWORD WINAPI WimProgressCallback(DWORD dwMsgId, WPARAM wParam, LPARAM lParam, PVOID pvIgnored)
{
	PBOOL pbCancel = NULL;
	PWIN32_FIND_DATA pFileData;
	const char* level = NULL;
	uint64_t size;
	float apply_percent;

	switch (dwMsgId) {
	case WIM_MSG_PROGRESS:
		// The default WIM progress is useless (freezes at 95%, which is usually when only half
		// the files have been processed), so we don't use it
#if 0
		PrintInfo(0, MSG_267, (DWORD)wParam);
		UpdateProgress(OP_DOS, 0.98f*(DWORD)wParam);
#endif
		break;
	case WIM_MSG_PROCESS:
		// The amount of files processed is overwhelming (16k+ for a typical image),
		// and trying to display it *WILL* slow us down, so we don't.
#if 0
		uprintf("%S", (PWSTR)wParam);
		PrintStatus(0, MSG_000, str);	// MSG_000 is "%s"
#endif
		if (count_files) {
			wim_nb_files++;
		} else {
			wim_proc_files++;
			if (GetTickCount64() > LastRefresh + 100) {
				// At the end of an actual apply, the WIM API re-lists a bunch of directories it
				// already processed, so we end up with more entries than counted - ignore those.
				if (wim_proc_files > wim_nb_files)
					wim_proc_files = wim_nb_files;
				LastRefresh = GetTickCount64();
				// x^3 progress, so as not to give a better idea right from the onset
				// as to the dismal speed with which the WIM API can actually apply files...
				apply_percent = 4.636942595f * ((float)wim_proc_files) / ((float)wim_nb_files);
				apply_percent = apply_percent * apply_percent * apply_percent;
				PrintInfo(0, MSG_267, apply_percent);
				UpdateProgress(OP_DOS, apply_percent);
			}
		}
		// Halt on error
		if (IS_ERROR(FormatStatus)) {
			pbCancel = (PBOOL)lParam;
			*pbCancel = TRUE;
			break;
		}
		break;
	case WIM_MSG_FILEINFO:
		pFileData = (PWIN32_FIND_DATA)lParam;
		if (pFileData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			uprintf("Creating: %S", (PWSTR)wParam);
		} else {
			size = (((uint64_t)pFileData->nFileSizeHigh) << 32) + pFileData->nFileSizeLow;
			uprintf("Extracting: %S (%s)", (PWSTR)wParam, SizeToHumanReadable(size, FALSE, FALSE));
		}
		break;
	case WIM_MSG_RETRY:
		level = "retry";
		// fall through
	case WIM_MSG_INFO:
		if (level == NULL) level = "info";
		// fall through
	case WIM_MSG_WARNING:
		if (level == NULL) level = "warning";
		// fall through
	case WIM_MSG_ERROR:
		if (level == NULL) level = "error";
		SetLastError((DWORD)lParam);
		uprintf("Apply %s: %S [err = %d]\n", level, (PWSTR)wParam, WindowsErrorString());
		break;
	}

	return IS_ERROR(FormatStatus)?WIM_MSG_ABORT_IMAGE:WIM_MSG_SUCCESS;
}

// Apply a WIM image using wimgapi.dll (Windows 7 or later)
// http://msdn.microsoft.com/en-us/library/windows/desktop/dd851944.aspx
// To get progress, we must run this call within its own thread
static DWORD WINAPI WimApplyImageThread(LPVOID param)
{
	BOOL r = FALSE;
	HANDLE hWim = NULL;
	HANDLE hImage = NULL;
	wchar_t wtemp[MAX_PATH] = {0};
	wchar_t* wimage = utf8_to_wchar(_image);
	wchar_t* wdst = utf8_to_wchar(_dst);

	PF_INIT_OR_OUT(WIMRegisterMessageCallback, Wimgapi);
	PF_INIT_OR_OUT(WIMCreateFile, Wimgapi);
	PF_INIT_OR_OUT(WIMSetTemporaryPath, Wimgapi);
	PF_INIT_OR_OUT(WIMLoadImage, Wimgapi);
	PF_INIT_OR_OUT(WIMApplyImage, Wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, Wimgapi);
	PF_INIT_OR_OUT(WIMUnregisterMessageCallback, Wimgapi);

	uprintf("Opening: %s:[%d]", _image, _index);

	if (pfWIMRegisterMessageCallback(NULL, (FARPROC)WimProgressCallback, NULL) == INVALID_CALLBACK_VALUE) {
		uprintf("  Could not set progress callback: %s", WindowsErrorString());
		goto out;
	}

	if (GetTempPathW(ARRAYSIZE(wtemp), wtemp) == 0) {
		uprintf("  Could not fetch temp path: %s", WindowsErrorString());
		goto out;
	}

	hWim = pfWIMCreateFile(wimage, WIM_GENERIC_READ, WIM_OPEN_EXISTING,
		(img_report.wininst_version >= SPECIAL_WIM_VERSION) ? WIM_UNDOCUMENTED_BULLSHIT : 0, 0, NULL);
	if (hWim == NULL) {
		uprintf("  Could not access image: %s", WindowsErrorString());
		goto out;
	}

	if (!pfWIMSetTemporaryPath(hWim, wtemp)) {
		uprintf("  Could not set temp path: %s", WindowsErrorString());
		goto out;
	}

	hImage = pfWIMLoadImage(hWim, (DWORD)_index);
	if (hImage == NULL) {
		uprintf("  Could not set index: %s", WindowsErrorString());
		goto out;
	}

	uprintf("Applying Windows image...");
	// Run a first pass using WIM_FLAG_NO_APPLY to count the files
	wim_nb_files = 0;
	wim_proc_files = 0;
	LastRefresh = 0;
	count_files = TRUE;
	if (!pfWIMApplyImage(hImage, wdst, WIM_FLAG_NO_APPLY)) {
		uprintf("  Could not count the files to apply: %s", WindowsErrorString());
		goto out;
	}
	count_files = FALSE;
	// Actual apply
	if (!pfWIMApplyImage(hImage, wdst, WIM_FLAG_FILEINFO)) {
		uprintf("  Could not apply image: %s", WindowsErrorString());
		goto out;
	}
	PrintInfo(0, MSG_267, 99.8f);
	UpdateProgress(OP_DOS, 99.8f);
	r = TRUE;

out:
	if ((hImage != NULL) || (hWim != NULL)) {
		uprintf("Closing: %s", _image);
		if (hImage != NULL) pfWIMCloseHandle(hImage);
		if (hWim != NULL) pfWIMCloseHandle(hWim);
	}
	if (pfWIMUnregisterMessageCallback != NULL)
		pfWIMUnregisterMessageCallback(NULL, (FARPROC)WimProgressCallback);
	safe_free(wimage);
	safe_free(wdst);
	ExitThread((DWORD)r);
}

BOOL WimApplyImage(const char* image, int index, const char* dst)
{
	HANDLE handle;
	DWORD dw = 0;
	_image = image;
	_index = index;
	_dst = dst;

	handle = CreateThread(NULL, 0, WimApplyImageThread, NULL, 0, NULL);
	if (handle == NULL) {
		uprintf("Unable to start apply-image thread");
		return FALSE;
	}
	WaitForSingleObject(handle, INFINITE);
	if (!GetExitCodeThread(handle, &dw))
		return FALSE;
	return (BOOL)dw;
}
