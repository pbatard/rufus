/*
 * Rufus: The Reliable USB Formatting Utility
 * Virtual Disk Handling functions
 * Copyright © 2013-2025 Pete Batard <pete@akeo.ie>
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

// MinGW includes virdisk.h in windows.h, but we we don't want that
// because we must apply a delay-loading workaround, and that workaround
// has to apply between the winnt.h include and the virdisk.h include.
// So we define _INC_VIRTDISK, to prevent the virdisk.h include in
// windows.h, and then take care of the workaround (and virtdisk.h
// include) in vhd.h.
#define _INC_VIRTDISK
#include <windows.h>
#undef _INC_VIRTDISK
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
#include "wimlib.h"
#include "registry.h"
#include "bled/bled.h"

extern char* save_image_type;
extern BOOL ignore_boot_marker, has_ffu_support;
extern RUFUS_DRIVE rufus_drive[MAX_DRIVES];
extern HANDLE format_thread;
extern FILE* fd_md5sum;
extern uint64_t total_blocks, extra_blocks, nb_blocks, last_nb_blocks;

static char physical_path[128] = "";
static int progress_op = OP_FILE_COPY, progress_msg = MSG_267;
static HANDLE mounted_handle = INVALID_HANDLE_VALUE;
static struct wimlib_progress_info_split last_split_progress;

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
	{ ".zst", BLED_COMPRESSION_ZSTD },
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
				physical_disk = VhdMountImageAndGetSize(path, &img_report.projected_size);
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
	if (img_report.projected_size == 0)
		img_report.projected_size = img_report.image_size;
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
static enum wimlib_progress_status WimProgressFunc(enum wimlib_progress_msg msg_type,
	union wimlib_progress_info* info, void* progctx)
{
	static BOOL init[3] = { 0 };

	if IS_ERROR(ErrorStatus)
		return WIMLIB_PROGRESS_STATUS_ABORT;

	switch (msg_type) {
	case WIMLIB_PROGRESS_MSG_EXTRACT_IMAGE_BEGIN:
		memset(init, 0, sizeof(init));
		uprintf("Applying image %d (\"%S\") from '%S' to '%S'",
			info->extract.image, info->extract.image_name,
			info->extract.wimfile_name, info->extract.target);
		break;
	case WIMLIB_PROGRESS_MSG_EXTRACT_FILE_STRUCTURE:
		if (!init[0]) {
			uprintf("Creating file structure...");
			init[0] = TRUE;
			uprint_progress(0, 0);
		}
		UpdateProgressWithInfoUpTo(98, progress_op, progress_msg, info->extract.current_file_count, info->extract.end_file_count * 6);
		uprint_progress(info->extract.current_file_count, info->extract.end_file_count);
		break;
	case WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS:
		if (!init[1]) {
			uprintf("\nExtracting file data...");
			init[1] = TRUE;
			uprint_progress(0, 0);
		}
		UpdateProgressWithInfoUpTo(98, progress_op, progress_msg, info->extract.total_bytes + (4 * info->extract.completed_bytes), info->extract.total_bytes * 6);
		uprint_progress(info->extract.completed_bytes, info->extract.total_bytes);
		break;
	case WIMLIB_PROGRESS_MSG_EXTRACT_METADATA:
		if (!init[2]) {
			uprintf("\nApplying metadata to files...");
			init[2] = TRUE;
			uprint_progress(0, 0);
		}
		UpdateProgressWithInfoUpTo(98, progress_op, progress_msg, info->extract.current_file_count + (5 * info->extract.end_file_count), info->extract.end_file_count * 6);
		uprint_progress(info->extract.current_file_count, info->extract.end_file_count);
		if (info->extract.current_file_count >= info->extract.end_file_count)
			uprintf("\n");
		break;
	case WIMLIB_PROGRESS_MSG_SPLIT_BEGIN_PART:
		last_split_progress = info->split;
		uprintf("● %S", info->split.part_name);
		break;
	case WIMLIB_PROGRESS_MSG_SPLIT_END_PART:
		if (fd_md5sum != NULL) {
			// Don't bother computing the hash at write time - just do it post creation
			uint8_t sum[MD5_HASHSIZE];
			char* filename = wchar_to_utf8(info->split.part_name);
			if (filename != NULL) {
				HashFile(HASH_MD5, filename, sum);
				for (int j = 0; j < MD5_HASHSIZE; j++)
					fprintf(fd_md5sum, "%02x", sum[j]);
				fprintf(fd_md5sum, "  ./%s\n", &filename[3]);
				free(filename);
			}
		}
		break;
	case WIMLIB_PROGRESS_MSG_WRITE_STREAMS:
		uint64_t completed_bytes = last_split_progress.completed_bytes + info->write_streams.completed_compressed_bytes;
		nb_blocks = last_nb_blocks + completed_bytes / 2048;
		UpdateProgressWithInfo(OP_FILE_COPY, MSG_231, nb_blocks, total_blocks + extra_blocks);
		break;
	default:
		break;
	}

	return WIMLIB_PROGRESS_STATUS_CONTINUE;
}

// Return the WIM version of an image
uint32_t GetWimVersion(const char* image)
{
	int r;
	WIMStruct* wim;
	struct wimlib_wim_info info;

	if (image == NULL)
		return 0;

	r = wimlib_open_wimU(image, 0, &wim);
	if (r == 0) {
		r = wimlib_get_wim_info(wim, &info);
		wimlib_free(wim);
		if (r == 0)
			return info.wim_version;
	}
	uprintf("WARNING: Could not get WIM version: Error %d", r);
	return 0;
}

// Extract a file from a WIM image. Returns the allocated path of the extracted file or NULL on error.
BOOL WimExtractFile(const char* image, int index, const char* src, const char* dst)
{
	int r = 1;
	WIMStruct* wim;
	char tmp[MAX_PATH] = "", *p;

	if ((image == NULL) || (src == NULL) || (dst == NULL))
		goto out;

	assert(strrchr(src, '\\') != NULL);
	assert(strrchr(dst, '\\') != NULL);
	if (strrchr(src, '\\') == NULL || strrchr(dst, '\\') == NULL)
		goto out;
	p = strrchr(dst, '\\');
	*p = '\0';

	wimlib_global_init(0);
	wimlib_set_print_errors(true);
	r = wimlib_open_wimU(image, 0, &wim);
	if (r == 0) {
		r = wimlib_extract_pathsU(wim, index, dst, &src, 1, WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE);
		wimlib_free(wim);
		static_strcpy(tmp, dst);
		static_strcat(tmp, strrchr(src, '\\'));
		*p = '\\';
		if (!MoveFileExU(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
			uprintf("  Could not rename %s to %s: %s", tmp, dst, WindowsErrorString());
			r = 1;
		}
	}
	wimlib_global_cleanup();

out:
	return (r == 0);
}

// Split an install.wim for FAT32 limits
BOOL WimSplitFile(const char* src, const char* dst)
{
	int r = 1;
	WIMStruct* wim;

	if ((src == NULL) || (dst == NULL))
		goto out;

	wimlib_global_init(0);
	wimlib_set_print_errors(true);
	r = wimlib_open_wimU(src, 0, &wim);
	if (r == 0) {
		wimlib_register_progress_function(wim, WimProgressFunc, NULL);
		r = wimlib_splitU(wim, dst, 4094ULL * MB, WIMLIB_WRITE_FLAG_FSYNC);
		wimlib_free(wim);
	}
	wimlib_global_cleanup();

out:
	return (r == 0);
}

BOOL WimApplyImage(const char* image, int index, const char* dst)
{
	int r = 1;
	WIMStruct* wim;

	wimlib_global_init(0);
	wimlib_set_print_errors(true);

	uprintf("Opening: %s:[%d]", image, index);
	r = wimlib_open_wimU(image, 0, &wim);
	if (r == 0) {
		progress_op = OP_FILE_COPY;
		progress_msg = MSG_267;
		wimlib_register_progress_function(wim, WimProgressFunc, NULL);
		r = wimlib_extract_imageU(wim, index, dst, 0);
		wimlib_free(wim);
	} else {
		uprintf("Failed to open '%s': Wimlib error %d", image, r);
	}
	wimlib_global_cleanup();
	return (r == 0);
}

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

	r = OpenVirtualDisk(&vtype, wpath, VIRTUAL_DISK_ACCESS_READ | VIRTUAL_DISK_ACCESS_GET_INFO,
		OPEN_VIRTUAL_DISK_FLAG_NONE, NULL, &mounted_handle);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not open image '%s': %s", path, WindowsErrorString());
		goto out;
	}

	vparams.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
	r = AttachVirtualDisk(mounted_handle, NULL, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY |
		ATTACH_VIRTUAL_DISK_FLAG_NO_DRIVE_LETTER, 0, &vparams, NULL);
	if (r != ERROR_SUCCESS) {
		SetLastError(r);
		uprintf("Could not mount image '%s': %s", path, WindowsErrorString());
		goto out;
	}

	r = GetVirtualDiskPhysicalPath(mounted_handle, &size, wtmp);
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
		r = GetVirtualDiskInformation(mounted_handle, &size, &disk_info, NULL);
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
	if ((mounted_handle == NULL) || (mounted_handle == INVALID_HANDLE_VALUE))
		goto out;

	DetachVirtualDisk(mounted_handle, DETACH_VIRTUAL_DISK_FLAG_NONE, 0);
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
	DWORD r = ERROR_NOT_FOUND, flags, bytes_read = 0;

	if_assert_fails(img_save->Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHD ||
		img_save->Type == VIRTUAL_STORAGE_TYPE_DEVICE_VHDX)
		return ERROR_INVALID_PARAMETER;

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

	r = CreateVirtualDisk(&vtype, wDst, VIRTUAL_DISK_ACCESS_NONE, NULL,
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
			if (GetVirtualDiskOperationProgress(handle, &overlapped, &vprogress) == ERROR_SUCCESS) {
				if (vprogress.OperationStatus == ERROR_IO_PENDING)
					UpdateProgressWithInfo(OP_FORMAT, MSG_261, vprogress.CurrentValue, vprogress.CompletionValue);
			}
		}
		if (r != WAIT_OBJECT_0) {
			uprintf("Could not save virtual disk: %s", WindowsErrorString());
			goto out;
		}
	}

	if (!GetOverlappedResult(handle, &overlapped, &bytes_read, FALSE)) {
		r = GetLastError();
		uprintf("Could not save virtual disk: %s", WindowsErrorString());
		goto out;
	}

	r = 0;
	UpdateProgressWithInfo(OP_FORMAT, MSG_261, SelectedDrive.DiskSize, SelectedDrive.DiskSize);
	uprintf("Saved '%s'", img_save->ImagePath);

out:
	safe_closehandle(overlapped.hEvent);
	safe_closehandle(handle);
	safe_free(wSrc);
	safe_free(wDst);
	safe_free(img_save->DevicePath);
	safe_free(img_save->ImagePath);
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	if (r != 0 && !IS_ERROR(ErrorStatus))
		ErrorStatus = RUFUS_ERROR(SCODE_CODE(r));
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
	uprintf("Running command: '%s'", cmd);
	// For detecting typical dism.exe commandline progress report of type:
	// "\r[====                       8.0%                           ]"
	r = RunCommandWithProgress(cmd, sysnative_dir, TRUE, MSG_261, ".*\r\\[[= ]+([0-9\\.]+)%[= ]+\\].*");
	if (r != 0 && !IS_ERROR(ErrorStatus)) {
		SetLastError(r);
		uprintf("Failed to capture FFU image: %s", WindowsErrorString());
		ErrorStatus = RUFUS_ERROR(SCODE_CODE(r));
	}
	PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)TRUE, 0);
	if (!IS_ERROR(ErrorStatus))
		uprintf("Saved '%s'", img_save->ImagePath);
	safe_free(img_save->DevicePath);
	safe_free(img_save->ImagePath);
	ExitThread(r);
}

BOOL SaveImage(void)
{
	UINT i;
	static IMG_SAVE img_save;
	char filename[128], letters[27], path[MAX_PATH];
	int DriveIndex = ComboBox_GetCurSel(hDeviceList);
	enum { image_type_vhd = 1, image_type_vhdx = 2, image_type_ffu = 3, image_type_iso = 4 };
	static EXT_DECL(img_ext, filename, __VA_GROUP__("*.vhd", "*.vhdx", "*.ffu", "*.iso"),
		__VA_GROUP__(lmprintf(MSG_343), lmprintf(MSG_342), lmprintf(MSG_344), lmprintf(MSG_036)));
	ULARGE_INTEGER free_space;

	memset(&img_save, 0, sizeof(IMG_SAVE));
	if ((DriveIndex < 0) || (format_thread != NULL))
		return FALSE;

	static_sprintf(filename, "%s", rufus_drive[DriveIndex].label);
	img_save.DeviceNum = (DWORD)ComboBox_GetItemData(hDeviceList, DriveIndex);
	img_save.DevicePath = GetPhysicalName(img_save.DeviceNum);
	img_ext.count = 2;
	// FFU support requires GPT
	if (has_ffu_support && SelectedDrive.PartitionStyle == PARTITION_STYLE_GPT) {
		img_ext.count += 1;
	} else {
		// Move the ISO extension one place down
		img_ext.extension[2] = img_ext.extension[3];
		img_ext.description[2] = img_ext.description[3];
	}
	// ISO support requires a mounted file system
	if (GetDriveLetters(SelectedDrive.DeviceNumber, letters) && letters[0] != '\0')
		img_ext.count += 1;
	for (i = 1; i <= (UINT)img_ext.count && (safe_strcmp(save_image_type, &img_ext.extension[i - 1][2]) != '\0'); i++);
	if (i > (UINT)img_ext.count)
		i = image_type_vhdx;
	img_save.ImagePath = FileDialog(TRUE, NULL, &img_ext, &i);
	if (img_save.ImagePath == NULL)
		goto out;
	// Start from the end of our extension array, since '.vhd' would match for '.vhdx' otherwise
	for (i = (UINT)img_ext.count; (i > 0) && (strstr(img_save.ImagePath, &img_ext.extension[i - 1][1]) == NULL); i--);
	if (i == 0) {
		uprintf("WARNING: Can not determine image type from extension - Saving to uncompressed VHD.");
		i = image_type_vhd;
	} else {
		save_image_type = (char*)&img_ext.extension[i - 1][2];
		WriteSettingStr(SETTING_PREFERRED_SAVE_IMAGE_TYPE, save_image_type);
	}
	switch (i) {
	case image_type_vhd:
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
		break;
	case image_type_ffu:
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_FFU;
		break;
	case image_type_iso:
		// ISO requires oscdimg.exe. If not already present, attempt to download it.
		static_sprintf(path, "%s\\%s\\oscdimg.exe", app_data_dir, FILES_DIR);
		if (!PathFileExistsU(path)) {
			if (Notification(MB_YESNO | MB_ICONWARNING, lmprintf(MSG_115), lmprintf(MSG_337, "oscdimg.exe")) != IDYES)
				goto out;
			IGNORE_RETVAL(_chdirU(app_data_dir));
			IGNORE_RETVAL(_mkdir(FILES_DIR));
			if (DownloadToFileOrBufferEx(OSCDIMG_URL, path, SYMBOL_SERVER_USER_AGENT, NULL, hMainDialog, FALSE) < 64 * KB)
				goto out;
		}
		img_save.Type = VIRTUAL_STORAGE_TYPE_DEVICE_ISO;
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
			FfuSaveImageThread : (img_save.Type == VIRTUAL_STORAGE_TYPE_DEVICE_ISO ? IsoSaveImageThread : VhdSaveImageThread),
			&img_save, 0, NULL);
		if (format_thread != NULL) {
			uprintf("\r\nSave to image operation started");
			PrintInfo(0, -1);
			SendMessage(hMainDialog, UM_TIMER_START, 0, 0);
		} else {
			uprintf("Unable to start image save thread");
			ErrorStatus = RUFUS_ERROR(APPERR(ERROR_CANT_START_THREAD));
			PostMessage(hMainDialog, UM_FORMAT_COMPLETED, (WPARAM)FALSE, 0);
		}
	}
out:
	if (format_thread == NULL) {
		safe_free(img_save.DevicePath);
		safe_free(img_save.ImagePath);
	}
	return (format_thread != NULL);
}
