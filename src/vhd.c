/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling functions
 * Copyright © 2013-2024 Pete Batard <pete@akeo.ie>
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
#include <windowsx.h>
#include <stdlib.h>
#include <io.h>
#include <rpc.h>
#include <time.h>

#include "rufus.h"
#include "ui.h"
#include "vhd.h"
#include "missing.h"
#include "resource.h"
#include "settings.h"
#include "msapi_utf8.h"

#include "drive.h"
#include "registry.h"
#include "bled/bled.h"

// WIM API Prototypes
PF_TYPE_DECL(WINAPI, HANDLE, WIMCreateFile, (PWSTR, DWORD, DWORD, DWORD, DWORD, PDWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMSetTemporaryPath, (HANDLE, PWSTR));
PF_TYPE_DECL(WINAPI, HANDLE, WIMLoadImage, (HANDLE, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMMountImage, (PCWSTR, PCWSTR, DWORD, PCWSTR));
PF_TYPE_DECL(WINAPI, BOOL, WIMUnmountImage, (PCWSTR, PCWSTR, DWORD, BOOL));
PF_TYPE_DECL(WINAPI, BOOL, WIMApplyImage, (HANDLE, PCWSTR, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMExtractImagePath, (HANDLE, PWSTR, PWSTR, DWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMGetImageInformation, (HANDLE, PVOID, PDWORD));
PF_TYPE_DECL(WINAPI, BOOL, WIMCloseHandle, (HANDLE));
PF_TYPE_DECL(WINAPI, DWORD, WIMRegisterMessageCallback, (HANDLE, FARPROC, PVOID));
PF_TYPE_DECL(WINAPI, DWORD, WIMUnregisterMessageCallback, (HANDLE, FARPROC));

typedef struct {
	int index;
	BOOL commit;
	const char* image;
	const char* dst;
} mount_params_t;

uint32_t wim_nb_files, wim_proc_files, wim_extra_files;
HANDLE wim_thread = NULL;
extern int default_thread_priority;
extern char* save_image_type;
extern BOOL ignore_boot_marker, has_ffu_support;
extern RUFUS_DRIVE rufus_drive[MAX_DRIVES];
extern HANDLE format_thread;

static uint8_t wim_flags = 0;
static uint32_t progress_report_mask;
static uint64_t progress_offset = 0, progress_total = 100;
static wchar_t wmount_path[MAX_PATH] = { 0 }, wmount_track[MAX_PATH] = { 0 };
static char sevenzip_path[MAX_PATH];
static BOOL count_files;
static int progress_op = OP_FILE_COPY, progress_msg = MSG_267;

static BOOL Get7ZipPath(void)
{
	if ( (GetRegistryKeyStr(REGKEY_HKCU, "Software\\7-Zip\\Path", sevenzip_path, sizeof(sevenzip_path)))
	  || (GetRegistryKeyStr(REGKEY_HKLM, "Software\\7-Zip\\Path", sevenzip_path, sizeof(sevenzip_path))) ) {
		static_strcat(sevenzip_path, "\\7z.exe");
		return (_accessU(sevenzip_path, 0) != -1);
	}
	return FALSE;
}

typedef struct {
	const char* ext;
	uint8_t type;
} comp_assoc;

static comp_assoc file_assoc[] = {
	{ ".zip", BLED_COMPRESSION_ZIP },
	{ ".Z", BLED_COMPRESSION_LZW },
	{ ".gz", BLED_COMPRESSION_GZIP },
	{ ".lzma", BLED_COMPRESSION_LZMA },
	{ ".bz2", BLED_COMPRESSION_BZIP2 },
	{ ".xz", BLED_COMPRESSION_XZ },
	{ ".vtsi", BLED_COMPRESSION_VTSI },
	{ ".ffu", BLED_COMPRESSION_MAX },
	{ ".vhd", BLED_COMPRESSION_MAX + 1 },
	{ ".vhdx", BLED_COMPRESSION_MAX + 2 },
};

// Look for a boot marker in the MBR area of the image
static int8_t IsCompressedBootableImage(const char* path)
{
	char *ext = NULL, *physical_disk = NULL;
	unsigned char *buf = NULL;
	int i;
	FILE* fd = NULL;
	BOOL r = 0;
	int64_t dc = 0;

	img_report.compression_type = BLED_COMPRESSION_NONE;
	if (safe_strlen(path) > 4)
		for (ext = (char*)&path[safe_strlen(path) - 1]; (*ext != '.') && (ext != path); ext--);

	for (i = 0; i < ARRAYSIZE(file_assoc); i++) {
		if (safe_stricmp(ext, file_assoc[i].ext) == 0) {
			img_report.compression_type = file_assoc[i].type;
			buf = malloc(MBR_SIZE);
			if (buf == NULL)
				return 0;
			ErrorStatus = 0;
			if (img_report.compression_type < BLED_COMPRESSION_MAX) {
				bled_init(0, uprintf, NULL, NULL, NULL, NULL, &ErrorStatus);
				dc = bled_uncompress_to_buffer(path, (char*)buf, MBR_SIZE, file_assoc[i].type);
				bled_exit();
			} else if (img_report.compression_type == BLED_COMPRESSION_MAX) {
				// Dism, through FfuProvider.dll, can mount a .ffu as a physicaldrive, which we
				// could then use to poke the MBR as we do for VHD... Except Microsoft did design
				// dism to FAIL AND EXIT, after mounting the ffu as a virtual drive, if it doesn't
				// find something that looks like Windows at the specified image index... which it
				// usually won't in our case. So, curse Microsoft and their incredible short-
				// sightedness (or, most likely in this case, intentional malice, by BREACHING the
				// OS contract to keep useful disk APIs for their usage, and their usage only).
				// Then again, considering that .ffu's are GPT based, the marker should always be
				// present, so just check for the FFU signature and pretend there's a marker then.
				if (has_ffu_support) {
					fd = fopenU(path, "rb");
					if (fd != NULL) {
						img_report.is_vhd = TRUE;
						dc = fread(buf, 1, MBR_SIZE, fd);
						fclose(fd);
						// The signature may not be constant, but since the only game in town to
						// create FFU is dism, and dism appears to use "SignedImage " always,.we
						// might as well use this to our advantage.
						if (strncmp(&buf[4], "SignedImage ", 12) == 0) {
							// At this stage, the buffer is only used for marker validation.
							buf[0x1FE] = 0x55;
							buf[0x1FF] = 0xAA;
						}
					} else
						uprintf("Could not open %s: %d", path, errno);
				} else {
					uprintf("  An FFU image was selected, but this system does not have FFU support!");
				}
			} else {
				physical_disk = VhdMountImage(path);
				if (physical_disk != NULL) {
					img_report.is_vhd = TRUE;
					fd = fopenU(physical_disk, "rb");
					if (fd != NULL) {
						dc = fread(buf, 1, MBR_SIZE, fd);
						fclose(fd);
					}
				}
				VhdUnmountImage();
			}
			if (dc != MBR_SIZE) {
				free(buf);
				return FALSE;
			}
			if ((buf[0x1FE] == 0x55) && (buf[0x1FF] == 0xAA))
				r = 1;
			else if (ignore_boot_marker)
				r = 2;
			free(buf);
			return r;
		}
	}

	return FALSE;
}

// 0: non-bootable, 1: bootable, 2: forced bootable
int8_t IsBootableImage(const char* path)
{
	HANDLE handle = INVALID_HANDLE_VALUE;
	LARGE_INTEGER liImageSize;
	DWORD size;
	uint64_t wim_magic = 0;
	LARGE_INTEGER ptr = { 0 };
	int8_t is_bootable_img;

	uprintf("Disk image analysis:");
	handle = CreateFileU(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		uprintf("  Could not open image '%s'", path);
		is_bootable_img = -1;
		goto out;
	}

	is_bootable_img = IsCompressedBootableImage(path) ? 1 : 0;
	if (img_report.compression_type == BLED_COMPRESSION_NONE)
		is_bootable_img = AnalyzeMBR(handle, "  Image", FALSE) ? 1 : (ignore_boot_marker ? 2 : 0);

	if (!GetFileSizeEx(handle, &liImageSize)) {
		uprintf("  Could not get image size: %s", WindowsErrorString());
		is_bootable_img = -2;
		goto out;
	}
	img_report.image_size = (uint64_t)liImageSize.QuadPart;
	size = sizeof(wim_magic);
	IGNORE_RETVAL(SetFilePointerEx(handle, ptr, NULL, FILE_BEGIN));
	img_report.is_windows_img = ReadFile(handle, &wim_magic, size, &size, NULL) && (wim_magic == WIM_MAGIC);
	if (img_report.is_windows_img)
		goto out;

out:
	safe_closehandle(handle);
	return is_bootable_img;
}

// WIM operations progress callback
DWORD WINAPI WimProgressCallback(DWORD dwMsgId, WPARAM wParam, LPARAM lParam, PVOID pvIgnored)
{
	PBOOL pbCancel = NULL;
	PWIN32_FIND_DATA pFileData;
	const char* level = NULL;
	uint64_t size;

	switch (dwMsgId) {
	case WIM_MSG_PROGRESS:
		// The default WIM progress is useless for apply (freezes at 95%, which is usually when
		// only half the files have been processed), so we only use it for mounting/unmounting.
		if (!(progress_report_mask & WIM_REPORT_PROGRESS))
			break;
		UpdateProgressWithInfo(progress_op, progress_msg, progress_offset + wParam, progress_total);
		break;
	case WIM_MSG_PROCESS:
		if (!(progress_report_mask & WIM_REPORT_PROCESS))
			break;
		// The amount of files processed is overwhelming (16k+ for a typical image),
		// and trying to display it *WILL* slow us down, so we don't.
#if 0
		uprintf("%S", (PWSTR)wParam);
		PrintStatus(0, MSG_000, str);	// MSG_000 is "%s"
#endif
		if (count_files) {
			wim_nb_files++;
		} else {
			// At the end of an actual apply, the WIM API re-lists a bunch of directories it already processed,
			// so, even as we try to compensate, we might end up with more entries than counted - ignore those.
			if (wim_proc_files < wim_nb_files)
				wim_proc_files++;
			else
				wim_extra_files++;
			UpdateProgressWithInfo(progress_op, progress_msg, wim_proc_files, wim_nb_files);
		}
		// Halt on error
		if (IS_ERROR(ErrorStatus)) {
			pbCancel = (PBOOL)lParam;
			*pbCancel = TRUE;
			break;
		}
		break;
	case WIM_MSG_FILEINFO:
		if (!(progress_report_mask & WIM_REPORT_FILEINFO))
			break;
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
		uprintf("WIM processing %s: %S [%s]\n", level, (PWSTR)wParam, WindowsErrorString());
		break;
	}

	return IS_ERROR(ErrorStatus) ? WIM_MSG_ABORT_IMAGE : WIM_MSG_SUCCESS;
}

// Find out if we have any way to extract/apply WIM files on this platform
// Returns a bitfield of the methods we can use (1 = Extract using wimgapi, 2 = Extract using 7-Zip, 4 = Apply using wimgapi)
uint8_t WimExtractCheck(BOOL bSilent)
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

	suprintf("WIM extraction method(s) supported: %s%s%s", (wim_flags & WIM_HAS_7Z_EXTRACT)?"7-Zip":
		((wim_flags & WIM_HAS_API_EXTRACT)?"":"NONE"),
		(WIM_HAS_EXTRACT(wim_flags) == (WIM_HAS_API_EXTRACT|WIM_HAS_7Z_EXTRACT))?", ":
		"", (wim_flags & WIM_HAS_API_EXTRACT)?"wimgapi.dll":"");
	suprintf("WIM apply method supported: %s", (wim_flags & WIM_HAS_API_APPLY)?"wimgapi.dll":"NONE");
	return wim_flags;
}

//
// Looks like Microsoft's idea of "mount" for WIM images involves the creation
// of a as many virtual junctions as there exist directories on the image...
// So, yeah, this is both very slow and wasteful of space.
//
// NB: You can see mounted WIMs, along with their mountpoint, by checking:
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WIMMount\Mounted Images
// You can also mount/unmount images from an elevated prompt with something like:
// dism /mount-image [/readonly] /imagefile:F:\sources\boot.wim /index:2 /mountdir:C:\test\offline
// dism /unmount-image /discard /mountdir:C:\test\offline
//
static DWORD WINAPI WimMountImageThread(LPVOID param)
{
	BOOL r = FALSE;
	wconvert(temp_dir);
	mount_params_t* mp = (mount_params_t*)param;
	wchar_t* wimage = utf8_to_wchar(mp->image);

	PF_INIT_OR_OUT(WIMRegisterMessageCallback, Wimgapi);
	PF_INIT_OR_OUT(WIMMountImage, Wimgapi);
	PF_INIT_OR_OUT(WIMUnmountImage, Wimgapi);
	PF_INIT_OR_OUT(WIMUnregisterMessageCallback, Wimgapi);

	if (wmount_path[0] != 0) {
		uprintf("WimMountImage: An image is already mounted. Trying to unmount it...");
		if (pfWIMUnmountImage(wmount_path, wimage, mp->index, FALSE))
			uprintf("WimMountImage: Successfully unmounted existing image..");
		else
			goto out;
	}
	if (GetTempFileNameW(wtemp_dir, L"Ruf", 0, wmount_path) == 0) {
		uprintf("WimMountImage: Can not generate mount directory: %s", WindowsErrorString());
		goto out;
	}
	DeleteFileW(wmount_path);
	if (!CreateDirectoryW(wmount_path, 0)) {
		uprintf("WimMountImage: Can not create mount directory '%S': %s", wmount_path, WindowsErrorString());
		goto out;
	}
	if (GetTempFileNameW(wtemp_dir, L"Ruf", 0, wmount_track) == 0) {
		uprintf("WimMountImage: Can not generate tracking directory: %s", WindowsErrorString());
		goto out;
	}
	DeleteFileW(wmount_track);
	if (!CreateDirectoryW(wmount_track, 0)) {
		uprintf("WimMountImage: Can not create tracking directory '%S': %s", wmount_track, WindowsErrorString());
		goto out;
	}

	progress_report_mask = WIM_REPORT_PROGRESS;
	progress_op = OP_PATCH;
	progress_msg = MSG_325;
	progress_offset = 1;
	progress_total = PATCH_PROGRESS_TOTAL;
	if (pfWIMRegisterMessageCallback(NULL, (FARPROC)WimProgressCallback, NULL) == INVALID_CALLBACK_VALUE) {
		uprintf("WimMountImage: Could not set progress callback: %s", WindowsErrorString());
		goto out;
	}

	r = pfWIMMountImage(wmount_path, wimage, mp->index, wmount_track);
	pfWIMUnregisterMessageCallback(NULL, (FARPROC)WimProgressCallback);
	if (!r) {
		uprintf("Could not mount '%S [%d]' on '%S': %s", wimage, mp->index, wmount_path, WindowsErrorString());
		goto out;
	}
	uprintf("Mounted '%S [%d]' on '%S'", wimage, mp->index, wmount_path);

out:
	if (!r) {
		if (wmount_track[0] != 0) {
			RemoveDirectoryW(wmount_track);
			wmount_track[0] = 0;
		}
		if (wmount_path[0] != 0) {
			RemoveDirectoryW(wmount_path);
			wmount_path[0] = 0;
		}
	}
	wfree(temp_dir);
	safe_free(wimage);
	ExitThread((DWORD)r);
}

// Returns the temporary mount path on success, NULL on error.
// Returned path must be freed by the caller.
char* WimMountImage(const char* image, int index)
{
	char* mount_path = NULL;
	DWORD dw = 0;
	mount_params_t mp = { 0 };
	mp.image = image;
	mp.index = index;

	// Try to unmount an existing stale image, if there is any
	mount_path = WimGetExistingMountPoint(image, index);
	if (mount_path != NULL) {
		uprintf("WARNING: Found stale '%s [%d]' image mounted on '%s' - Attempting to unmount it...",
			image, index, mount_path);
		utf8_to_wchar_no_alloc(mount_path, wmount_path, ARRAYSIZE(wmount_path));
		wmount_track[0] = 0;
		WimUnmountImage(image, index, FALSE);
	}

	wim_thread = CreateThread(NULL, 0, WimMountImageThread, &mp, 0, NULL);
	if (wim_thread == NULL) {
		uprintf("Unable to start mount-image thread");
		return NULL;
	}
	SetThreadPriority(wim_thread, default_thread_priority);
	WaitForSingleObject(wim_thread, INFINITE);
	if (!GetExitCodeThread(wim_thread, &dw))
		dw = 0;
	wim_thread = NULL;
	return (dw) ? wchar_to_utf8(wmount_path) : NULL;
}

static DWORD WINAPI WimUnmountImageThread(LPVOID param)
{
	BOOL r = FALSE;
	mount_params_t* mp = (mount_params_t*)param;
	wchar_t* wimage = utf8_to_wchar(mp->image);

	PF_INIT_OR_OUT(WIMRegisterMessageCallback, Wimgapi);
	PF_INIT_OR_OUT(WIMUnmountImage, Wimgapi);
	PF_INIT_OR_OUT(WIMUnregisterMessageCallback, Wimgapi);

	if (wmount_path[0] == 0) {
		uprintf("WimUnmountImage: No image is mounted");
		goto out;
	}

	progress_report_mask = WIM_REPORT_PROGRESS;
	progress_op = OP_PATCH;
	progress_msg = MSG_325;
	progress_offset = 105;
	progress_total = PATCH_PROGRESS_TOTAL;
	if (pfWIMRegisterMessageCallback(NULL, (FARPROC)WimProgressCallback, NULL) == INVALID_CALLBACK_VALUE) {
		uprintf("WimUnmountImage: Could not set progress callback: %s", WindowsErrorString());
		goto out;
	}

	r = pfWIMUnmountImage(wmount_path, wimage, mp->index, mp->commit);
	pfWIMUnregisterMessageCallback(NULL, (FARPROC)WimProgressCallback);
	if (!r) {
		uprintf("Could not unmount '%S': %s", wmount_path, WindowsErrorString());
		goto out;
	}
	uprintf("Unmounted '%S [%d]'", wmount_path, mp->index);
	if (wmount_track[0] != 0 && !RemoveDirectoryW(wmount_track))
		uprintf("Could not delete '%S' : %s", wmount_track, WindowsErrorString());
	wmount_track[0] = 0;
	if (wmount_path[0] != 0 && !RemoveDirectoryW(wmount_path))
		uprintf("Could not delete '%S' : %s", wmount_path, WindowsErrorString());
	wmount_path[0] = 0;
out:
	safe_free(wimage);
	ExitThread((DWORD)r);
}

BOOL WimUnmountImage(const char* image, int index, BOOL commit)
{
	DWORD dw = 0;
	mount_params_t mp = { 0 };
	mp.image = image;
	mp.index = index;
	mp.commit = commit;

	wim_thread = CreateThread(NULL, 0, WimUnmountImageThread, &mp, 0, NULL);
	if (wim_thread == NULL) {
		uprintf("Unable to start unmount-image thread");
		return FALSE;
	}
	SetThreadPriority(wim_thread, default_thread_priority);
	WaitForSingleObject(wim_thread, INFINITE);
	if (!GetExitCodeThread(wim_thread, &dw))
		dw = 0;
	wim_thread = NULL;
	return dw;
}

// Get the existing mount point (if any) for the image + index passed as parameters.
// Needed because Windows refuses to mount two images with the same path/index even
// if the previous has become stale or deleted. This situation may occur if the user
// force-closed Rufus when 'boot.wim' was mounted, thus leaving them unable to mount
// 'boot.wim' for subsequent sessions unless they invoke dism /Unmount-Wim manually.
// This basically parses HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\WIMMount\Mounted Images
// to see if an instance exists with the image/index passed as parameter and returns
// the mount point of this image if found, or NULL otherwise.
char* WimGetExistingMountPoint(const char* image, int index)
{
	static char path[MAX_PATH];
	char class[MAX_PATH] = "", guid[40], key_name[MAX_PATH];
	HKEY hKey;
	DWORD dw = 0, i, k, nb_subkeys = 0, class_size;
	DWORD cbMaxSubKey, cchMaxClass, cValues, cchMaxValue;
	DWORD cbMaxValueData, cbSecurityDescriptor;
	FILETIME ftLastWriteTime;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\WIMMount\\Mounted Images",
		0, KEY_READ, &hKey) != ERROR_SUCCESS)
		return NULL;

	class_size = sizeof(class);
	RegQueryInfoKeyA(hKey, class, &class_size, NULL, &nb_subkeys,
		&cbMaxSubKey, &cchMaxClass, &cValues, &cchMaxValue,
		&cbMaxValueData, &cbSecurityDescriptor, &ftLastWriteTime);

	for (k = 0; k < nb_subkeys; k++) {
		dw = sizeof(guid);
		if (RegEnumKeyExA(hKey, k, guid, &dw, NULL, NULL, NULL,
			&ftLastWriteTime) == ERROR_SUCCESS) {
			static_sprintf(key_name, "SOFTWARE\\Microsoft\\WIMMount\\Mounted Images\\%s\\WIM Path", guid);
			if (GetRegistryKeyStr(HKEY_LOCAL_MACHINE, key_name, path, sizeof(path)) &&
				(stricmp(path, image) != 0))
				continue;
			static_sprintf(key_name, "SOFTWARE\\Microsoft\\WIMMount\\Mounted Images\\%s\\Image Index", guid);
			if (GetRegistryKey32(HKEY_LOCAL_MACHINE, key_name, &i) && (i != (DWORD)index))
				continue;
			path[0] = 0;
			static_sprintf(key_name, "SOFTWARE\\Microsoft\\WIMMount\\Mounted Images\\%s\\Mount Path", guid);
			if (GetRegistryKeyStr(HKEY_LOCAL_MACHINE, key_name, path, sizeof(path)))
				break;
		}
	}
	if (k >= nb_subkeys)
		path[0] = 0;

	RegCloseKey(hKey);

	return (path[0] == 0) ? NULL: path;
}

// Extract a file from a WIM image using wimgapi.dll
// NB: if you want progress from a WIM callback, you must run the WIM API call in its own thread
// (which we don't do here) as it won't work otherwise. Thanks go to Erwan for figuring this out!
BOOL WimExtractFile_API(const char* image, int index, const char* src, const char* dst, BOOL bSilent)
{
	static char* index_name = "[1].xml";
	BOOL r = FALSE;
	DWORD dw = 0;
	HANDLE hWim = NULL;
	HANDLE hImage = NULL;
	HANDLE hFile = NULL;
	wchar_t wtemp[MAX_PATH] = { 0 };
	wchar_t* wimage = utf8_to_wchar(image);
	wchar_t* wsrc = utf8_to_wchar(src);
	wchar_t* wdst = utf8_to_wchar(dst);
	wchar_t* wim_info;

	PF_INIT_OR_OUT(WIMCreateFile, Wimgapi);
	PF_INIT_OR_OUT(WIMSetTemporaryPath, Wimgapi);
	PF_INIT_OR_OUT(WIMLoadImage, Wimgapi);
	PF_INIT_OR_OUT(WIMExtractImagePath, Wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, Wimgapi);

	suprintf("Opening: %s:[%d] (API)", image, index);
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

	suprintf("Extracting: %s (From %s)", dst, src);
	if (safe_strcmp(src, index_name) == 0) {
		if (!pfWIMGetImageInformation(hWim, &wim_info, &dw) || (dw == 0)) {
			uprintf("  Could not access WIM info: %s", WindowsErrorString());
			goto out;
		}
		hFile = CreateFileW(wdst, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if ((hFile == INVALID_HANDLE_VALUE) || (!WriteFile(hFile, wim_info, dw, &dw, NULL))) {
			suprintf("  Could not extract file: %s", WindowsErrorString());
			goto out;
		}
	} else {
		hImage = pfWIMLoadImage(hWim, (DWORD)index);
		if (hImage == NULL) {
			uprintf("  Could not set index: %s", WindowsErrorString());
			goto out;
		}
		if (!pfWIMExtractImagePath(hImage, wsrc, wdst, 0)) {
			suprintf("  Could not extract file: %s", WindowsErrorString());
			goto out;
		}
	}
	r = TRUE;

out:
	if ((hImage != NULL) || (hWim != NULL)) {
		suprintf("Closing: %s", image);
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
BOOL WimExtractFile_7z(const char* image, int index, const char* src, const char* dst, BOOL bSilent)
{
	int n;
	size_t i;
	char cmdline[MAX_PATH];
	char tmpdst[MAX_PATH];
	char index_prefix[] = "#\\";

	suprintf("Opening: %s:[%d] (7-Zip)", image, index);

	if ((image == NULL) || (src == NULL) || (dst == NULL))
		return FALSE;

	// If you shove more than 9 images in a WIM, don't come complaining
	// that this breaks!
	index_prefix[0] = '0' + index;

	suprintf("Extracting: %s (From %s)", dst, src);

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
		suprintf("  7z.exe did not extract %s", tmpdst);
		return FALSE;
	}

	// coverity[toctou]
	if (!MoveFileExU(tmpdst, dst, MOVEFILE_REPLACE_EXISTING)) {
		uprintf("  Could not rename %s to %s: %s", tmpdst, dst, WindowsErrorString());
		return FALSE;
	}

	return TRUE;
}

// Extract a file from a WIM image
BOOL WimExtractFile(const char* image, int index, const char* src, const char* dst, BOOL bSilent)
{
	if ((wim_flags == 0) && (!WIM_HAS_EXTRACT(WimExtractCheck(TRUE))))
		return FALSE;
	if ((image == NULL) || (src == NULL) || (dst == NULL))
		return FALSE;

	// Prefer 7-Zip as, unsurprisingly, it's faster than the Microsoft way,
	// but allow fallback if 7-Zip doesn't succeed
	return ( ((wim_flags & WIM_HAS_7Z_EXTRACT) && WimExtractFile_7z(image, index, src, dst, bSilent))
		  || ((wim_flags & WIM_HAS_API_EXTRACT) && WimExtractFile_API(image, index, src, dst, bSilent)) );
}

/// <summary>
/// Find if a specific index belongs to a WIM image.
/// </summary>
/// <param name="image">The path to the WIM file.</param>
/// <param name="index">The (non-zero) value of the index to check.</param>
/// <returns>TRUE if the index was found in the image, FALSE otherwise.</returns>
BOOL WimIsValidIndex(const char* image, int index)
{
	int i = 1;
	BOOL r = FALSE;
	DWORD dw = 0;
	HANDLE hWim = NULL;
	HANDLE hFile = NULL;
	char xml_file[MAX_PATH] = { 0 };
	char* str;
	wchar_t* wimage = utf8_to_wchar(image);
	wchar_t* wim_info;

	PF_INIT_OR_OUT(WIMCreateFile, Wimgapi);
	PF_INIT_OR_OUT(WIMGetImageInformation, Wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, Wimgapi);

	// Zero indexes are invalid
	if (index == 0)
		goto out;

	hWim = pfWIMCreateFile(wimage, WIM_GENERIC_READ, WIM_OPEN_EXISTING,
		(img_report.wininst_version >= SPECIAL_WIM_VERSION) ? WIM_UNDOCUMENTED_BULLSHIT : 0, 0, NULL);
	if (hWim == NULL) {
		uprintf("  Could not access image: %s", WindowsErrorString());
		goto out;
	}

	if (!pfWIMGetImageInformation(hWim, &wim_info, &dw) || (dw == 0)) {
		uprintf("  Could not access WIM info: %s", WindowsErrorString());
		goto out;
	}

	if ((GetTempFileNameU(temp_dir, APPLICATION_NAME, 0, xml_file) == 0) || (xml_file[0] == 0))
		static_strcpy(xml_file, ".\\RufVXml.tmp");
	DeleteFileU(xml_file);
	hFile = CreateFileU(xml_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if ((hFile == INVALID_HANDLE_VALUE) || (!WriteFile(hFile, wim_info, dw, &dw, NULL)))
		goto out;

	while ((str = get_token_data_file_indexed("IMAGE INDEX", xml_file, i)) != NULL) {
		if (atoi(str) == index) {
			r = TRUE;
			break;
		}
		i++;
	}

out:
	if (hWim != NULL)
		pfWIMCloseHandle(hWim);
	safe_closehandle(hFile);
	if (xml_file[0] != 0)
		DeleteFileU(xml_file);
	safe_free(wimage);
	return r;
}

// Apply a WIM image using wimgapi.dll
// https://docs.microsoft.com/en-us/previous-versions/msdn10/dd851944(v=msdn.10)
// To get progress, we must run this call within its own thread
static DWORD WINAPI WimApplyImageThread(LPVOID param)
{
	BOOL r = FALSE;
	HANDLE hWim = NULL;
	HANDLE hImage = NULL;
	wchar_t wtemp[MAX_PATH] = { 0 };
	mount_params_t* mp = (mount_params_t*)param;
	wchar_t* wimage = utf8_to_wchar(mp->image);
	wchar_t* wdst = utf8_to_wchar(mp->dst);

	PF_INIT_OR_OUT(WIMRegisterMessageCallback, Wimgapi);
	PF_INIT_OR_OUT(WIMCreateFile, Wimgapi);
	PF_INIT_OR_OUT(WIMSetTemporaryPath, Wimgapi);
	PF_INIT_OR_OUT(WIMLoadImage, Wimgapi);
	PF_INIT_OR_OUT(WIMApplyImage, Wimgapi);
	PF_INIT_OR_OUT(WIMCloseHandle, Wimgapi);
	PF_INIT_OR_OUT(WIMUnregisterMessageCallback, Wimgapi);

	uprintf("Opening: %s:[%d]", mp->image, mp->index);

	progress_report_mask = WIM_REPORT_PROCESS | WIM_REPORT_FILEINFO;
	progress_op = OP_FILE_COPY;
	progress_msg = MSG_267;
	progress_offset = 0;
	progress_total = 100;
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

	hImage = pfWIMLoadImage(hWim, (DWORD)mp->index);
	if (hImage == NULL) {
		uprintf("  Could not set index: %s", WindowsErrorString());
		goto out;
	}

	uprintf("Applying Windows image...");
	UpdateProgressWithInfoInit(NULL, TRUE);
	// Run a first pass using WIM_FLAG_NO_APPLY to count the files
	wim_nb_files = 0;
	wim_proc_files = 0;
	wim_extra_files = 0;
	count_files = TRUE;
	if (!pfWIMApplyImage(hImage, wdst, WIM_FLAG_NO_APPLY)) {
		uprintf("  Could not count the files to apply: %s", WindowsErrorString());
		goto out;
	}
	// The latest Windows 10 ISOs have a ~17.5% discrepancy between the number of
	// files and directories actually applied vs. the ones counted when not applying.
	// Therefore, we add a 'safe' 20% to our counted files to compensate for yet
	// another dismal Microsoft progress reporting API...
	wim_nb_files += wim_nb_files / 5;
	count_files = FALSE;
	// Actual apply
	if (!pfWIMApplyImage(hImage, wdst, WIM_FLAG_FILEINFO)) {
		uprintf("  Could not apply image: %s", WindowsErrorString());
		goto out;
	}
	// Ensure that we'll pick if need to readjust our 20% above from user reports
	if (wim_extra_files > 0)
		uprintf("Notice: An extra %d files and directories were applied, from the %d expected",
			wim_extra_files, wim_nb_files);
	// Re-use extra files as the final progress step
	wim_extra_files = (wim_nb_files - wim_proc_files) / 3;
	UpdateProgressWithInfo(OP_FILE_COPY, MSG_267, wim_proc_files + wim_extra_files, wim_nb_files);
	r = TRUE;

out:
	if ((hImage != NULL) || (hWim != NULL)) {
		uprintf("Closing: %s", mp->image);
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
	DWORD dw = 0;
	mount_params_t mp = { 0 };
	mp.image = image;
	mp.index = index;
	mp.dst = dst;

	wim_thread = CreateThread(NULL, 0, WimApplyImageThread, &mp, 0, NULL);
	if (wim_thread == NULL) {
		uprintf("Unable to start apply-image thread");
		return FALSE;
	}
	SetThreadPriority(wim_thread, default_thread_priority);
	WaitForSingleObject(wim_thread, INFINITE);
	if (!GetExitCodeThread(wim_thread, &dw))
		dw = 0;
	wim_thread = NULL;
	return dw;
}

// VirtDisk API Prototypes since we can't use delay-loading because of MinGW
// See https://github.com/pbatard/rufus/issues/2272#issuecomment-1615976013
PF_TYPE_DECL(WINAPI, DWORD, CreateVirtualDisk, (PVIRTUAL_STORAGE_TYPE, PCWSTR,
	VIRTUAL_DISK_ACCESS_MASK, PSECURITY_DESCRIPTOR, CREATE_VIRTUAL_DISK_FLAG, ULONG,
	PCREATE_VIRTUAL_DISK_PARAMETERS, LPOVERLAPPED, PHANDLE));
PF_TYPE_DECL(WINAPI, DWORD, OpenVirtualDisk, (PVIRTUAL_STORAGE_TYPE, PCWSTR,
	VIRTUAL_DISK_ACCESS_MASK, OPEN_VIRTUAL_DISK_FLAG, POPEN_VIRTUAL_DISK_PARAMETERS, PHANDLE));
PF_TYPE_DECL(WINAPI, DWORD, AttachVirtualDisk, (HANDLE, PSECURITY_DESCRIPTOR,
	ATTACH_VIRTUAL_DISK_FLAG, ULONG, PATTACH_VIRTUAL_DISK_PARAMETERS, LPOVERLAPPED));
PF_TYPE_DECL(WINAPI, DWORD, DetachVirtualDisk, (HANDLE, DETACH_VIRTUAL_DISK_FLAG, ULONG));
PF_TYPE_DECL(WINAPI, DWORD, GetVirtualDiskPhysicalPath, (HANDLE, PULONG, PWSTR));
PF_TYPE_DECL(WINAPI, DWORD, GetVirtualDiskOperationProgress, (HANDLE, LPOVERLAPPED, PVIRTUAL_DISK_PROGRESS));
PF_TYPE_DECL(WINAPI, DWORD, GetVirtualDiskInformation, (HANDLE, PULONG, PGET_VIRTUAL_DISK_INFO, PULONG));

static char physical_path[128] = "";
static HANDLE mounted_handle = INVALID_HANDLE_VALUE;

// Mount an ISO or a VHD/VHDX image and provide its size
// Returns the physical path of the mounted image or NULL on error.
char* VhdMountImageAndGetSize(const char* path, uint64_t* disk_size)
{
	VIRTUAL_STORAGE_TYPE vtype = { VIRTUAL_STORAGE_TYPE_DEVICE_ISO, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
	ATTACH_VIRTUAL_DISK_PARAMETERS vparams = { 0 };
	GET_VIRTUAL_DISK_INFO disk_info = { 0 };
	DWORD r;
	wchar_t wtmp[128];
	ULONG size = ARRAYSIZE(wtmp);
	wconvert(path);
	char *ret = NULL, *ext = NULL;

	PF_INIT_OR_OUT(OpenVirtualDisk, VirtDisk);
	PF_INIT_OR_OUT(AttachVirtualDisk, VirtDisk);
	PF_INIT_OR_OUT(GetVirtualDiskPhysicalPath, VirtDisk);
	if (disk_size != NULL)
		PF_INIT_OR_OUT(GetVirtualDiskInformation, VirtDisk);

	if (wpath == NULL)
		return NULL;

	if ((mounted_handle != NULL) && (mounted_handle != INVALID_HANDLE_VALUE))
		VhdUnmountImage();

	if (safe_strlen(path) > 4)
		for (ext = (char*)&path[safe_strlen(path) - 1]; (*ext != '.') && (ext != path); ext--);
	if (safe_stricmp(ext, ".vhdx") == 0)
		vtype.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
	else if (safe_stricmp(ext, ".vhd") == 0)
		vtype.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHD;

	r = pfOpenVirtualDisk(&vtype, wpath, VIRTUAL_DISK_ACCESS_READ | VIRTUAL_DISK_ACCESS_GET_INFO,
		OPEN_VIRTUAL_DISK_FLAG_NONE, NULL, &mounted_handle);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not open image '%s': %s", path, WindowsErrorString());
		goto out;
	}

	vparams.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
	r = pfAttachVirtualDisk(mounted_handle, NULL, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY |
		ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER, 0, &vparams, NULL);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not mount image '%s': %s", path, WindowsErrorString());
		goto out;
	}

	r = pfGetVirtualDiskPhysicalPath(mounted_handle, &size, wtmp);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not obtain physical path for mounted image '%s': %s", path, WindowsErrorString());
		goto out;
	}
	wchar_to_utf8_no_alloc(wtmp, physical_path, sizeof(physical_path));

	if (disk_size != NULL) {
		*disk_size = 0;
		disk_info.Version = GET_VIRTUAL_DISK_INFO_SIZE;
		size = sizeof(disk_info);
		r = pfGetVirtualDiskInformation(mounted_handle, &size, &disk_info, NULL);
		if (r != ERROR_SUCCESS) {
			SetLastError(r);
			uprintf("Could not obtain virtual size of mounted image '%s': %s", path, WindowsErrorString());
			goto out;
		}
		*disk_size = disk_info.Size.VirtualSize;
	}

	ret = physical_path;

out:
	if (ret == NULL)
		VhdUnmountImage();
	wfree(path);
	return ret;
}

void VhdUnmountImage(void)
{
	PF_INIT_OR_OUT(DetachVirtualDisk, VirtDisk);

	if ((mounted_handle == NULL) || (mounted_handle == INVALID_HANDLE_VALUE))
		goto out;

	pfDetachVirtualDisk(mounted_handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
	safe_closehandle(mounted_handle);
out:
	physical_path[0] = 0;
}

// Since we no longer have to deal with Windows 7, we can call on CreateVirtualDisk()
// to backup a physical disk to VHD/VHDX. Now if this could also be used to create an
// ISO from optical media that would be swell, but no matter what I tried, it didn't
// seem possible...
static DWORD WINAPI VhdSaveImageThread(void* param)
{
	IMG_SAVE* img_save = (IMG_SAVE*)param;
	HANDLE handle = INVALID_HANDLE_VALUE;
	WCHAR* wSrc = utf8_to_wchar(img_save->DevicePath);
	WCHAR* wDst = utf8_to_wchar(img_save->ImagePath);
	VIRTUAL_STORAGE_TYPE vtype = { img_save->Type, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
	STOPGAP_CREATE_VIRTUAL_DISK_PARAMETERS vparams = { 0 };
	VIRTUAL_DISK_PROGRESS vprogress = { 0 };
	OVERLAPPED overlapped = { 0 };
	DWORD r = ERROR_NOT_FOUND, flags;

	PF_INIT_OR_OUT(CreateVirtualDisk, VirtDisk);
	PF_INIT_OR_OUT(GetVirtualDiskOperationProgress, VirtDisk);

	assert(img_save->Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHD ||
		img_save->Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX);

	UpdateProgressWithInfoInit(NULL, FALSE);

	vparams.Version = CREATE_VIRTUAL_DISK_VERSION_2;
	vparams.Version2.UniqueId = GUID_NULL;
	vparams.Version2.BlockSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_BLOCK_SIZE;
	vparams.Version2.SectorSizeInBytes = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_SECTOR_SIZE;
	vparams.Version2.PhysicalSectorSizeInBytes = SelectedDrive.SectorSize;
	vparams.Version2.SourcePath = wSrc;

	// When CREATE_VIRTUAL_DISK_FLAG_CREATE_BACKING_STORAGE is specified with
	// a source path, CreateVirtualDisk() automatically clones the source to
	// the virtual disk.
	flags = CREATE_VIRTUAL_DISK_FLAG_CREATE_BACKING_STORAGE;
	// The following ensures that VHD images are stored uncompressed and can
	// be used as DD images.
	if (img_save->Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHD)
		flags |= CREATE_VIRTUAL_DISK_FLAG_FULL_PHYSICAL_ALLOCATION;
	// TODO: Use CREATE_VIRTUAL_DISK_FLAG_PREVENT_WRITES_TO_SOURCE_DISK?

	overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

	// CreateVirtualDisk() does not have an overwrite flag...
	DeleteFileW(wDst);

	r = pfCreateVirtualDisk(&vtype, wDst, VIRTUAL_DISK_ACCESS_NONE, NULL,
		flags, 0, (PCREATE_VIRTUAL_DISK_PARAMETERS)&vparams, &overlapped, &handle);
	if (r != ERROR_SUCCESS && r != ERROR_IO_PENDING) {
		SetLastError(r);
		uprintf("Could not create virtual disk: %s", WindowsErrorString());
		goto out;
	}

	if (r == ERROR_IO_PENDING) {
		while ((r = WaitForSingleObject(overlapped.hEvent, 100)) == WAIT_TIMEOUT) {
			if (IS_ERROR(ErrorStatus) && (SCODE_CODE(ErrorStatus) == ERROR_CANCELLED)) {
				CancelIoEx(handle, &overlapped);
				goto out;
			}
			if (pfGetVirtualDiskOperationProgress(handle, &overlapped, &vprogress) == ERROR_SUCCESS) {
				if (vprogress.OperationStatus == ERROR_IO_PENDING)
					UpdateProgressWithInfo(OP_FORMAT, MSG_261, vprogress.CurrentValue, vprogress.CompletionValue);
			}
		}
		if (r != WAIT_OBJECT_0) {
			uprintf("Could not save virtual disk: %s", WindowsErrorString());
			goto out;
		}
	}

	r = 0;
	UpdateProgressWithInfo(OP_FORMAT, MSG_261, SelectedDrive.DiskSize, SelectedDrive.DiskSize);
	uprintf("Operation complete.");

out:
	safe_closehandle(overlapped.hEvent);
	safe_closehandle(handle);
	safe_free(wSrc);
	safe_free(wDst);
	safe_free(img_save->DevicePath);
	safe_free(img_save->ImagePath);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	ExitThread(r);
}

// FfuProvider.dll has some nice FfuApplyImage()/FfuCaptureImage() calls... which
// Microsoft decided not make public!
// Considering that trying to both figure out how to use these internal function
// calls, as well as how to properly hook into the DLL for every arch/every release
// of Windows, would be a massive timesink, we just take a shortcut by calling dism
// directly, as imperfect as such a solution might be...
static DWORD WINAPI FfuSaveImageThread(void* param)
{
	DWORD r;
	IMG_SAVE* img_save = (IMG_SAVE*)param;
	char cmd[MAX_PATH + 128], letters[27], *label;

	GetDriveLabel(SelectedDrive.DeviceNumber, letters, &label, TRUE);
	static_sprintf(cmd, "dism /Capture-Ffu /CaptureDrive:%s /ImageFile:\"%s\" "
		"/Name:\"%s\" /Description:\"Created by %s (%s)\"",
		img_save->DevicePath, img_save->ImagePath, label, APPLICATION_NAME, RUFUS_URL);
	uprintf("Running command: '%s", cmd);
	r = RunCommandWithProgress(cmd, sysnative_dir, TRUE, MSG_261);
	if (r != 0 && !IS_ERROR(ErrorStatus)) {
		SetLastError(r);
		uprintf("Failed to capture FFU image: %s", WindowsErrorString());
		ErrorStatus = RUFUS_ERROR(SCODE_CODE(r));
	}
	safe_free(img_save->DevicePath);
	safe_free(img_save->ImagePath);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	ExitThread(r);
}

void VhdSaveImage(void)
{
	UINT i;
	static IMG_SAVE img_save;
	char filename[128];
	char path[MAX_PATH];
	int DriveIndex = ComboBox_GetCurSel(hDeviceList);
	enum { image_type_vhd = 1, image_type_vhdx = 2, image_type_ffu = 3 };
	static EXT_DECL(img_ext, filename, __VA_GROUP__("*.vhd", "*.vhdx", "*.ffu"),
		__VA_GROUP__(lmprintf(MSG_343), lmprintf(MSG_342), lmprintf(MSG_344)));
	ULARGE_INTEGER free_space;

	memset(&img_save, 0, sizeof(IMG_SAVE));
	if ((DriveIndex < 0) || (format_thread != NULL))
		return;

	static_sprintf(filename, "%s", rufus_drive[DriveIndex].label);
	img_save.DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, DriveIndex);
	img_save.DevicePath = GetPhysicalName(img_save.DeviceNum);
	// FFU support requires GPT
	img_ext.count = (!has_ffu_support || SelectedDrive.PartitionStyle != PARTITION_STYLE_GPT) ? 2 : 3;
	for (i = 1; i <= (UINT)img_ext.count && (safe_strcmp(save_image_type , &_img_ext_x[i - 1][2]) != 0); i++);
	if (i > (UINT)img_ext.count)
		i = image_type_vhdx;
	img_save.ImagePath = FileDialog(TRUE, NULL, &img_ext, &i);
	if (img_save.ImagePath == NULL)
		goto out;
	for (i = 1; i <= (UINT)img_ext.count && (strstr(img_save.ImagePath, &_img_ext_x[i - 1][1]) == NULL); i++);
	if (i > (UINT)img_ext.count) {
		uprintf("Warning: Can not determine image type from extension - Saving to uncompressed VHD.");
		i = image_type_vhd;
	} else {
		save_image_type = (char*)&_img_ext_x[i - 1][2];
		WriteSettingStr(SETTING_PREFERRED_SAVE_IMAGE_TYPE, save_image_type);
	}
	switch (i) {
	case image_type_vhd:
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
		break;
	case image_type_ffu:
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_FFU;
		break;
	default:
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
		break;
	}
	img_save.BufSize = DD_BUFFER_SIZE;
	img_save.DeviceSize = SelectedDrive.DiskSize;
	if (img_save.DevicePath != NULL && img_save.ImagePath != NULL) {
		// Reset all progress bars
		SendMessage(hMainDialog, UM_PROGRESS_INIT, 0, 0);
		ErrorStatus = 0;
		if (img_save.Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHD) {
			free_space.QuadPart = 0;
			if ((GetVolumePathNameA(img_save.ImagePath, path, sizeof(path)))
				&& (GetDiskFreeSpaceExA(path, &free_space, NULL, NULL))
				&& ((LONGLONG)free_space.QuadPart < (SelectedDrive.DiskSize + 512))) {
				uprintf("The VHD size is too large for the target drive");
				ErrorStatus = RUFUS_ERROR(ERROR_FILE_TOO_LARGE);
				PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
				goto out;
			}
		}
		// Disable all controls except Cancel
		EnableControls(FALSE, FALSE);
		ErrorStatus = 0;
		InitProgress(TRUE);
		format_thread = CreateThread(NULL, 0, img_save.Type == VIRTUAL_STORAGE_TYPE_DEVICE_FFU ?
			FfuSaveImageThread : VhdSaveImageThread, &img_save, 0, NULL);
		if (format_thread != NULL) {
			uprintf("\r\nSave to VHD operation started");
			PrintInfo(0, -1);
			SendMessage(hMainDialog, UM_TIMER_START, 0, 0);
		} else {
			uprintf("Unable to start VHD save thread");
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_START_THREAD));
			PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
		}
	}
out:
	if (format_thread == NULL) {
		safe_free(img_save.DevicePath);
		safe_free(img_save.ImagePath);
	}
}
