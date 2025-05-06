/*
 * wimboot.c
 *
 * Support for creating WIMBoot pointer files.
 *
 * For general information about WIMBoot, see
 * https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-8.1-and-8/dn594399(v=win.10)
 *
 * Note that WIMBoot pointer files are actually implemented on top of the
 * Windows Overlay Filesystem filter (WOF).  See wof.h for more info.
 */

/*
 * Copyright (C) 2014-2021 Eric Biggers
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option) any
 * later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this file; if not, see https://www.gnu.org/licenses/.
 */

#ifdef _WIN32

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/win32_common.h"

#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/inode.h"
#include "wimlib/error.h"
#include "wimlib/util.h"
#include "wimlib/wimboot.h"
#include "wimlib/win32.h"
#include "wimlib/wof.h"

static HANDLE
open_file(const wchar_t *device_name, DWORD desiredAccess)
{
	return CreateFile(device_name, desiredAccess,
			  FILE_SHARE_VALID_FLAGS, NULL, OPEN_EXISTING,
			  FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

static BOOL
query_device(HANDLE h, DWORD code, void *out, DWORD out_size)
{
	DWORD bytes_returned;
	return DeviceIoControl(h, code, NULL, 0, out, out_size,
			       &bytes_returned, NULL);
}

/*
 * Gets partition and drive information for the specified path.
 *
 * @path
 *	Absolute path which must begin with a drive letter.  For example, if the
 *	path is D:\install.wim, this function will query information about the
 *	D: volume.
 * @part_info_ret
 *	Partition info is returned here.
 * @drive_info_ret
 *	Drive info is returned here.  The contained partition info will not be
 *	valid.
 *
 * Returns 0 on success, or a positive error code on failure.
 */
static int
query_partition_and_disk_info(const wchar_t *path,
			      PARTITION_INFORMATION_EX *part_info,
			      DRIVE_LAYOUT_INFORMATION_EX *drive_info_ret)
{
	wchar_t vol_name[] = L"\\\\.\\X:";
	wchar_t disk_name[] = L"\\\\?\\PhysicalDriveXXXXXXXXXX";
	HANDLE h = INVALID_HANDLE_VALUE;
	VOLUME_DISK_EXTENTS *extents = NULL;
	size_t extents_size;
	DRIVE_LAYOUT_INFORMATION_EX *drive_info = NULL;
	size_t drive_info_size;
	int ret;

	wimlib_assert(path[0] != L'\0' && path[1] == L':');

	*(wcschr(vol_name, L'X')) = path[0];

	h = open_file(vol_name, GENERIC_READ);
	if (h == INVALID_HANDLE_VALUE) {
		win32_error(GetLastError(), L"\"%ls\": Can't open volume device",
			    vol_name);
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}

	if (!query_device(h, IOCTL_DISK_GET_PARTITION_INFO_EX,
			  part_info, sizeof(PARTITION_INFORMATION_EX)))
	{
		win32_error(GetLastError(),
			    L"\"%ls\": Can't get partition info", vol_name);
		ret = WIMLIB_ERR_READ;
		goto out;
	}

	extents_size = sizeof(VOLUME_DISK_EXTENTS);
	for (;;) {
		extents_size += 4 * sizeof(DISK_EXTENT);
		extents = MALLOC(extents_size);
		if (!extents) {
			ret = WIMLIB_ERR_NOMEM;
			goto out;
		}

		if (query_device(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
				 extents, extents_size))
			break;
		if (GetLastError() != ERROR_MORE_DATA) {
			win32_error(GetLastError(),
				    L"\"%ls\": Can't get volume extent info",
				    vol_name);
			ret = WIMLIB_ERR_READ;
			goto out;
		}
		FREE(extents);
	}

	CloseHandle(h);
	h = INVALID_HANDLE_VALUE;

	if (extents->NumberOfDiskExtents != 1) {
		ERROR("\"%ls\": This volume has %"PRIu32" disk extents, "
		      "but this code is untested for more than 1",
		      vol_name, (u32)extents->NumberOfDiskExtents);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out;
	}

	wsprintf(wcschr(disk_name, L'X'), L"%"PRIu32,
		 extents->Extents[0].DiskNumber);

	h = open_file(disk_name, GENERIC_READ);
	if (h == INVALID_HANDLE_VALUE) {
		win32_error(GetLastError(),
			    L"\"%ls\": Can't open disk device", disk_name);
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}

	drive_info_size = sizeof(DRIVE_LAYOUT_INFORMATION_EX);
	for (;;) {
		drive_info_size += 4 * sizeof(PARTITION_INFORMATION_EX);
		drive_info = MALLOC(drive_info_size);
		if (!drive_info) {
			ret = WIMLIB_ERR_NOMEM;
			goto out;
		}

		if (query_device(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
				 drive_info, drive_info_size))
			break;
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			win32_error(GetLastError(),
				    L"\"%ls\": Can't get disk info", disk_name);
			ret = WIMLIB_ERR_READ;
			goto out;
		}
		FREE(drive_info);
	}

	*drive_info_ret = *drive_info;  /* doesn't include partitions */
	CloseHandle(h);
	h = INVALID_HANDLE_VALUE;

	if (drive_info->PartitionStyle != part_info->PartitionStyle) {
		ERROR("\"%ls\", \"%ls\": Inconsistent partition table type!",
		      vol_name, disk_name);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out;
	}

	if (part_info->PartitionStyle == PARTITION_STYLE_GPT) {
		STATIC_ASSERT(sizeof(part_info->Gpt.PartitionId) ==
			      sizeof(drive_info->Gpt.DiskId));
		if (!memcmp(&part_info->Gpt.PartitionId,
			    &drive_info->Gpt.DiskId,
			    sizeof(drive_info->Gpt.DiskId)))
		{
			ERROR("\"%ls\", \"%ls\": Partition GUID is the "
			      "same as the disk GUID???", vol_name, disk_name);
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out;
		}
	}

	if (part_info->PartitionStyle != PARTITION_STYLE_MBR &&
	    part_info->PartitionStyle != PARTITION_STYLE_GPT)
	{
		ERROR("\"%ls\": Unknown partition style 0x%08"PRIx32,
		      vol_name, (u32)part_info->PartitionStyle);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out;
	}

	ret = 0;
out:
	FREE(extents);
	FREE(drive_info);
	if (h != INVALID_HANDLE_VALUE)
		CloseHandle(h);
	return ret;
}

/*
 * Calculate the size of WimOverlay.dat with one entry added.
 *
 * @old_hdr
 *	Previous WimOverlay.dat contents, or NULL if file did not exist.
 * @new_entry_2_size
 *	Size of entry_2 being added.
 * @size_ret
 *	Size will be returned here.
 *
 * Returns 0 on success, or WIMLIB_ERR_UNSUPPORTED if size overflows 32 bits.
 */
static int
calculate_wimoverlay_dat_size(const struct WimOverlay_dat_header *old_hdr,
			      u32 new_entry_2_size, u32 *size_ret)
{
	u64 size_64;
	u32 size;

	size_64 = sizeof(struct WimOverlay_dat_header);
	if (old_hdr) {
		for (u32 i = 0; i < old_hdr->num_entries; i++) {
			size_64 += sizeof(struct WimOverlay_dat_entry_1);
			size_64 += old_hdr->entry_1s[i].entry_2_length;
		}
	}
	size_64 += sizeof(struct WimOverlay_dat_entry_1);
	size_64 += new_entry_2_size;

	size = size_64;
	if (size_64 != size)
		return WIMLIB_ERR_UNSUPPORTED;

	*size_ret = size;
	return 0;
}

/*
 * Writes @size bytes of @contents to the named file @path.
 *
 * Returns 0 on success; WIMLIB_ERR_OPEN or WIMLIB_ERR_WRITE on failure.
 */
static int
write_wimoverlay_dat(const wchar_t *path, const void *contents, u32 size)
{
	HANDLE h;
	DWORD bytes_written;

	h = CreateFile(path, GENERIC_WRITE, 0, NULL,
		       CREATE_ALWAYS, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		win32_error(GetLastError(),
			    L"\"%ls\": Can't open file for writing", path);
		return WIMLIB_ERR_OPEN;
	}

	SetLastError(0);
	if (!WriteFile(h, contents, size, &bytes_written, NULL) ||
	    bytes_written != size)
	{
		win32_error(GetLastError(),
			    L"\"%ls\": Can't write file", path);
		CloseHandle(h);
		return WIMLIB_ERR_WRITE;
	}

	if (!CloseHandle(h)) {
		win32_error(GetLastError(),
			    L"\"%ls\": Can't close handle", path);
		return WIMLIB_ERR_WRITE;
	}

	return 0;
}

/*
 * Generates the contents of WimOverlay.dat in memory, with one entry added.
 *
 * @buf
 *	Buffer large enough to hold the new contents.
 * @old_hdr
 *	Old contents of WimOverlay.dat, or NULL if it did not exist.
 * @wim_path
 *	Absolute path to the WIM file.  It must begin with a drive letter; for
 *	example, D:\install.wim.
 * @wim_guid
 *	GUID of the WIM, from the WIM header.
 * @image
 *	Number of the image in the WIM to specify in the new entry.
 * @new_data_source_id
 *	Data source ID to use for the new entry.
 * @part_info
 *	Partition information for the WIM file.
 * @disk_info
 *	Disk information for the WIM file.
 * @new_entry_2_size
 *	Size, in bytes, of the new location information structure ('struct
 *	WimOverlay_dat_entry_2').
 *
 * Returns a pointer one past the last byte of @buf filled in.
 */
static u8 *
fill_in_wimoverlay_dat(u8 *buf,
		       const struct WimOverlay_dat_header *old_hdr,
		       const wchar_t *wim_path,
		       const u8 wim_guid[GUID_SIZE],
		       int image,
		       u64 new_data_source_id,
		       const PARTITION_INFORMATION_EX *part_info,
		       const DRIVE_LAYOUT_INFORMATION_EX *disk_info,
		       u32 new_entry_2_size)
{
	struct WimOverlay_dat_header *new_hdr;
	struct WimOverlay_dat_entry_1 *new_entry_1;
	struct WimOverlay_dat_entry_2 *new_entry_2;
	u32 entry_2_offset;
	u8 *p = buf;

	new_hdr = (struct WimOverlay_dat_header *)p;

	/* Fill in new header  */
	new_hdr->magic = WIMOVERLAY_DAT_MAGIC;
	new_hdr->wim_provider_version = WIM_PROVIDER_CURRENT_VERSION;
	new_hdr->unknown_0x08 = 0x00000028;
	new_hdr->num_entries = (old_hdr ? old_hdr->num_entries : 0) + 1;
	new_hdr->next_data_source_id = (old_hdr ? old_hdr->next_data_source_id : 0) + 1;

	p += sizeof(struct WimOverlay_dat_header);

	/* Copy WIM-specific information for old entries  */
	entry_2_offset = sizeof(struct WimOverlay_dat_header) +
			(new_hdr->num_entries * sizeof(struct WimOverlay_dat_entry_1));
	if (old_hdr) {
		for (u32 i = 0; i < old_hdr->num_entries; i++) {
			new_entry_1 = (struct WimOverlay_dat_entry_1 *)p;

			p = mempcpy(p, &old_hdr->entry_1s[i],
				    sizeof(struct WimOverlay_dat_entry_1));

			new_entry_1->entry_2_offset = entry_2_offset;
			entry_2_offset += new_entry_1->entry_2_length;
		}
	}

	/* Generate WIM-specific information for new entry  */
	new_entry_1 = (struct WimOverlay_dat_entry_1 *)p;

	new_entry_1->data_source_id = new_data_source_id;
	new_entry_1->entry_2_offset = entry_2_offset;
	new_entry_1->entry_2_length = new_entry_2_size;
	new_entry_1->wim_type = WIM_BOOT_NOT_OS_WIM;
	new_entry_1->wim_index = image;
	STATIC_ASSERT(sizeof(new_entry_1->guid) == GUID_SIZE);
	copy_guid(new_entry_1->guid, wim_guid);

	p += sizeof(struct WimOverlay_dat_entry_1);

	/* Copy WIM location information for old entries  */
	if (old_hdr) {
		for (u32 i = 0; i < old_hdr->num_entries; i++) {
			wimlib_assert(new_hdr->entry_1s[i].entry_2_offset == p - buf);
			wimlib_assert(old_hdr->entry_1s[i].entry_2_length ==
				      new_hdr->entry_1s[i].entry_2_length);
			p = mempcpy(p,
				    ((const u8 *)old_hdr + old_hdr->entry_1s[i].entry_2_offset),
				    old_hdr->entry_1s[i].entry_2_length);
		}
	}

	/* Generate WIM location information for new entry  */
	new_entry_2 = (struct WimOverlay_dat_entry_2 *)p;

	new_entry_2->unknown_0x00 = 0x00000000;
	new_entry_2->unknown_0x04 = 0x00000000;
	new_entry_2->entry_2_length = new_entry_2_size;
	new_entry_2->unknown_0x0C = 0x00000000;
	new_entry_2->unknown_0x10 = 0x00000005;
	new_entry_2->unknown_0x14 = 0x00000001;
	new_entry_2->inner_struct_size = new_entry_2_size - 0x14;
	new_entry_2->unknown_0x1C = 0x00000005;
	new_entry_2->unknown_0x20 = 0x00000006;
	new_entry_2->unknown_0x24 = 0x00000000;
	new_entry_2->unknown_0x28 = 0x00000048;
	new_entry_2->unknown_0x2C = 0x00000000;
	new_entry_2->unknown_0x40 = 0x00000000;

	if (part_info->PartitionStyle == PARTITION_STYLE_MBR) {
		new_entry_2->partition.mbr.part_start_offset = part_info->StartingOffset.QuadPart;
		new_entry_2->partition.mbr.padding = 0;
		new_entry_2->partition_table_type = WIMOVERLAY_PARTITION_TYPE_MBR;
		new_entry_2->disk.mbr.disk_id = disk_info->Mbr.Signature;
		new_entry_2->disk.mbr.padding[0] = 0x00000000;
		new_entry_2->disk.mbr.padding[1] = 0x00000000;
		new_entry_2->disk.mbr.padding[2] = 0x00000000;
	} else {
		STATIC_ASSERT(sizeof(new_entry_2->partition.gpt.part_unique_guid) ==
			      sizeof(part_info->Gpt.PartitionId));
		memcpy(new_entry_2->partition.gpt.part_unique_guid,
		       &part_info->Gpt.PartitionId,
		       sizeof(part_info->Gpt.PartitionId));
		new_entry_2->partition_table_type = WIMOVERLAY_PARTITION_TYPE_GPT;

		STATIC_ASSERT(sizeof(new_entry_2->disk.gpt.disk_guid) ==
			      sizeof(disk_info->Gpt.DiskId));
		memcpy(new_entry_2->disk.gpt.disk_guid,
		       &disk_info->Gpt.DiskId,
		       sizeof(disk_info->Gpt.DiskId));

		STATIC_ASSERT(sizeof(new_entry_2->disk.gpt.disk_guid) ==
			      sizeof(new_entry_2->partition.gpt.part_unique_guid));
	}
	new_entry_2->unknown_0x58[0] = 0x00000000;
	new_entry_2->unknown_0x58[1] = 0x00000000;
	new_entry_2->unknown_0x58[2] = 0x00000000;
	new_entry_2->unknown_0x58[3] = 0x00000000;

	wimlib_assert(wim_path[2] == L'\\');
	return mempcpy(new_entry_2->wim_file_name,
		       wim_path + 2,
		       new_entry_2_size - sizeof(struct WimOverlay_dat_entry_2));
}

/*
 * Prepares the new contents of WimOverlay.dat in memory, with one entry added.
 *
 * @old_hdr
 *	Old contents of WimOverlay.dat, or NULL if it did not exist.
 * @wim_path
 *	Absolute path to the WIM file.  It must begin with a drive letter; for
 *	example, D:\install.wim.
 * @wim_guid
 *	GUID of the WIM, from the WIM header.
 * @image
 *	Number of the image in the WIM to specify in the new entry.
 * @new_contents_ret
 *	Location into which to return the new contents as a malloc()ed buffer on
 *	success.
 * @new_contents_size_ret
 *	Location into which to return the size, in bytes, of the new contents on
 *	success.
 * @new_data_source_id_ret
 *	Location into which to return the data source ID of the new entry on
 *	success.
 *
 * Returns 0 on success, or a positive error code on failure.
 */
static int
prepare_wimoverlay_dat(const struct WimOverlay_dat_header *old_hdr,
		       const wchar_t *wim_path,
		       const u8 wim_guid[GUID_SIZE],
		       int image,
		       void **new_contents_ret,
		       u32 *new_contents_size_ret,
		       u64 *new_data_source_id_ret)
{
	int ret;
	PARTITION_INFORMATION_EX part_info;
	DRIVE_LAYOUT_INFORMATION_EX disk_info;
	u64 new_data_source_id;
	u32 new_entry_2_size;
	u32 new_contents_size;
	u8 *buf;
	u8 *end;

	ret = query_partition_and_disk_info(wim_path, &part_info, &disk_info);
	if (ret)
		return ret;

	new_data_source_id = old_hdr ? old_hdr->next_data_source_id : 0;

	new_entry_2_size = sizeof(struct WimOverlay_dat_entry_2) +
				((wcslen(wim_path) - 2 + 1) * sizeof(wchar_t));
	ret = calculate_wimoverlay_dat_size(old_hdr, new_entry_2_size,
					    &new_contents_size);
	if (ret)
		return ret;

	buf = MALLOC(new_contents_size);
	if (!buf)
		return WIMLIB_ERR_NOMEM;

	end = fill_in_wimoverlay_dat(buf, old_hdr, wim_path, wim_guid, image,
				     new_data_source_id,
				     &part_info, &disk_info, new_entry_2_size);

	wimlib_assert(end - buf == new_contents_size);

	*new_contents_ret = buf;
	*new_contents_size_ret = new_contents_size;
	*new_data_source_id_ret = new_data_source_id;
	return 0;
}

static bool
valid_wim_filename(const struct WimOverlay_dat_entry_2 *entry, size_t name_len)
{
	size_t i;

	if (name_len % sizeof(wchar_t))
		return false;
	name_len /= sizeof(wchar_t);
	if (name_len < 2)
		return false;
	for (i = 0; i < name_len && entry->wim_file_name[i] != 0; i++)
		;
	return i == name_len - 1;
}

/*
 * Reads and validates a WimOverlay.dat file.
 *
 * @path
 *	Path to the WimOverlay.dat file, such as
 *	C:\System Volume Information\WimOverlay.dat
 * @contents_ret
 *	Location into which to return the contents as a malloc()ed buffer on
 *	success.  This can be cast to 'struct WimOverlay_dat_header', and its
 *	contents are guaranteed to be valid.  Alternatively, if the file does
 *	not exist, NULL will be returned here.
 *
 * Returns 0 on success, or a positive error code on failure.
 */
static int
read_wimoverlay_dat(const wchar_t *path, void **contents_ret)
{
	HANDLE h;
	BY_HANDLE_FILE_INFORMATION info;
	int ret;
	void *contents;
	const struct WimOverlay_dat_header *hdr;
	DWORD bytes_read;
	bool already_retried = false;

retry:
	h = open_file(path, GENERIC_READ);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		if (err == ERROR_FILE_NOT_FOUND) {
			*contents_ret = NULL;
			return 0;
		}
		if (err == ERROR_PATH_NOT_FOUND &&
		    func_RtlCreateSystemVolumeInformationFolder)
		{
			wchar_t volume_root_path[] = L"\\??\\X:\\";

			*(wcschr(volume_root_path, L'X')) = path[0];

			UNICODE_STRING str = {
				.Length = sizeof(volume_root_path) - sizeof(wchar_t),
				.MaximumLength = sizeof(volume_root_path),
				.Buffer = volume_root_path,
			};
			NTSTATUS status;
			DWORD err2;

			status = (*func_RtlCreateSystemVolumeInformationFolder)(&str);

			err2 = RtlNtStatusToDosError(status);
			if (err2 == ERROR_SUCCESS) {
				if (!already_retried) {
					already_retried = true;
					goto retry;
				}
			} else {
				err = err2;
			}
		}
		win32_error(err, L"\"%ls\": Can't open for reading", path);
		return WIMLIB_ERR_OPEN;
	}
	if (!GetFileInformationByHandle(h, &info)) {
		win32_error(GetLastError(), L"\"%ls\": Can't query metadata", path);
		CloseHandle(h);
		return WIMLIB_ERR_STAT;
	}

	contents = NULL;
	if (!info.nFileSizeHigh)
		contents = MALLOC(info.nFileSizeLow);
	if (!contents) {
		ERROR("\"%ls\": File is too large to fit into memory", path);
		CloseHandle(h);
		return WIMLIB_ERR_NOMEM;
	}

	SetLastError(0);
	if (!ReadFile(h, contents, info.nFileSizeLow, &bytes_read, NULL) ||
	    bytes_read != info.nFileSizeLow)
	{
		win32_error(GetLastError(), L"\"%ls\": Can't read data", path);
		CloseHandle(h);
		ret = WIMLIB_ERR_READ;
		goto out_free_contents;
	}

	CloseHandle(h);

	if (info.nFileSizeLow < sizeof(struct WimOverlay_dat_header)) {
		ERROR("\"%ls\": File is unexpectedly small (only %"PRIu32" bytes)",
		      path, (u32)info.nFileSizeLow);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out_free_contents;
	}

	hdr = (const struct WimOverlay_dat_header *)contents;

	if (hdr->magic != WIMOVERLAY_DAT_MAGIC ||
	    hdr->wim_provider_version != WIM_PROVIDER_CURRENT_VERSION ||
	    hdr->unknown_0x08 != 0x00000028)
	{
		ERROR("\"%ls\": Header contains unexpected data:", path);
		if (wimlib_print_errors) {
			print_byte_field((const u8 *)hdr,
					 sizeof(struct WimOverlay_dat_header),
					 wimlib_error_file);
			fputc('\n', wimlib_error_file);
		}
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out_free_contents;
	}

	if ((u64)hdr->num_entries * sizeof(struct WimOverlay_dat_entry_1) >
	    info.nFileSizeLow - sizeof(struct WimOverlay_dat_header))
	{
		ERROR("\"%ls\": File is unexpectedly small "
		      "(only %"PRIu32" bytes, but has %"PRIu32" entries)",
		      path, (u32)info.nFileSizeLow, hdr->num_entries);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out_free_contents;
	}

	for (u32 i = 0; i < hdr->num_entries; i++) {
		const struct WimOverlay_dat_entry_1 *entry_1;
		const struct WimOverlay_dat_entry_2 *entry_2;
		u32 wim_file_name_length;

		entry_1 = &hdr->entry_1s[i];

		if (entry_1->data_source_id >= hdr->next_data_source_id) {
			ERROR("\"%ls\": value of next_data_source_id "
			      "(0x%016"PRIx64") is unexpected, since entry "
			      "%"PRIu32" has data source ID 0x%016"PRIx64,
			      path, hdr->next_data_source_id,
			      i, entry_1->data_source_id);
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}

		if (((u64)entry_1->entry_2_offset +
		     (u64)entry_1->entry_2_length) >
		    info.nFileSizeLow)
		{
			ERROR("\"%ls\": entry %"PRIu32" (2) "
			      "(data source ID 0x%016"PRIx64") "
			      "overflows file",
			      path, i, entry_1->data_source_id);
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}
		if (entry_1->entry_2_length < sizeof(struct WimOverlay_dat_entry_2)) {
			ERROR("\"%ls\": entry %"PRIu32" (2) "
			      "(data source ID 0x%016"PRIx64") "
			      "is too short",
			      path, i, entry_1->data_source_id);
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}

		if (entry_1->entry_2_offset % 2 != 0) {
			ERROR("\"%ls\": entry %"PRIu32" (2) "
			      "(data source ID 0x%016"PRIx64") "
			      "is misaligned",
			      path, i, entry_1->data_source_id);
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}

		entry_2 = (const struct WimOverlay_dat_entry_2 *)
				((const u8 *)hdr + entry_1->entry_2_offset);

		wim_file_name_length = entry_1->entry_2_length -
					sizeof(struct WimOverlay_dat_entry_2);
		if (!valid_wim_filename(entry_2, wim_file_name_length)) {
			ERROR("\"%ls\": entry %"PRIu32" (2) "
			      "(data source ID 0x%016"PRIx64") "
			      "has invalid WIM file name",
			      path, i, entry_1->data_source_id);
			if (wimlib_print_errors) {
				print_byte_field((const u8 *)entry_2->wim_file_name,
						 wim_file_name_length,
						 wimlib_error_file);
				fputc('\n', wimlib_error_file);
			}
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}

		if (entry_2->unknown_0x00 != 0x00000000 ||
		    entry_2->unknown_0x04 != 0x00000000 ||
		    entry_2->unknown_0x0C != 0x00000000 ||
		    entry_2->entry_2_length != entry_1->entry_2_length ||
		    entry_2->unknown_0x10 != 0x00000005 ||
		    entry_2->unknown_0x14 != 0x00000001 ||
		    entry_2->inner_struct_size != entry_1->entry_2_length - 0x14 ||
		    entry_2->unknown_0x1C != 0x00000005 ||
		    entry_2->unknown_0x20 != 0x00000006 ||
		    entry_2->unknown_0x24 != 0x00000000 ||
		    entry_2->unknown_0x28 != 0x00000048 ||
		    entry_2->unknown_0x2C != 0x00000000 ||
		    entry_2->unknown_0x40 != 0x00000000 ||
		    (entry_2->partition_table_type != WIMOVERLAY_PARTITION_TYPE_GPT &&
		     entry_2->partition_table_type != WIMOVERLAY_PARTITION_TYPE_MBR) ||
		    (entry_2->partition_table_type == WIMOVERLAY_PARTITION_TYPE_MBR &&
		     entry_2->partition.mbr.padding != 0) ||
		    (entry_2->partition_table_type == WIMOVERLAY_PARTITION_TYPE_GPT &&
		     entry_2->partition.mbr.padding == 0) ||
		    entry_2->unknown_0x58[0] != 0x00000000 ||
		    entry_2->unknown_0x58[1] != 0x00000000 ||
		    entry_2->unknown_0x58[2] != 0x00000000 ||
		    entry_2->unknown_0x58[3] != 0x00000000)
		{
			ERROR("\"%ls\": entry %"PRIu32" (2) "
			      "(data source ID 0x%016"PRIx64") "
			      "contains unexpected data!",
			      path, i, entry_1->data_source_id);
			if (wimlib_print_errors) {
				print_byte_field((const u8 *)entry_2,
						 entry_1->entry_2_length,
						 wimlib_error_file);
				fputc('\n', wimlib_error_file);
			}
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_free_contents;
		}
	}

	*contents_ret = contents;
	return 0;

out_free_contents:
	FREE(contents);
	return ret;
}

/*
 * Update WimOverlay.dat manually in order to add a WIM data source to the
 * target volume.
 *
 * THIS CODE IS EXPERIMENTAL AS I HAD TO REVERSE ENGINEER THE FILE FORMAT!
 *
 * @path
 *	Target drive.  Must be a letter followed by a colon (e.g. D:).
 * @wim_path
 *	Absolute path to the WIM file.  It must begin with a drive letter; for
 *	example, D:\install.wim.
 * @wim_guid
 *	GUID of the WIM, from the WIM header.
 * @image
 *	Number of the image in the WIM to specify in the new entry.
 * @data_source_id_ret
 *	On success, the allocated data source ID is returned here.
 */
static int
update_wimoverlay_manually(const wchar_t *drive, const wchar_t *wim_path,
			   const u8 wim_guid[GUID_SIZE],
			   int image, u64 *data_source_id_ret)
{
	wchar_t path_main[] = L"A:\\System Volume Information\\WimOverlay.dat";
	wchar_t path_backup[] = L"A:\\System Volume Information\\WimOverlay.backup";
	wchar_t path_wimlib_backup[] = L"A:\\System Volume Information\\WimOverlay.wimlib_backup";
	wchar_t path_new[] = L"A:\\System Volume Information\\WimOverlay.wimlib_new";
	void *old_contents = NULL;
	void *new_contents = NULL;
	u32 new_contents_size = 0;
	u64 new_data_source_id = -1;
	int ret;

	wimlib_assert(drive[0] != L'\0' &&
		      drive[1] == L':' &&
		      drive[2] == L'\0');

	path_main[0]          = drive[0];
	path_backup[0]        = drive[0];
	path_wimlib_backup[0] = drive[0];
	path_new[0]           = drive[0];

	ret = read_wimoverlay_dat(path_main, &old_contents);
	if (ret)
		goto out;

	// coverity[tainted_data]
	ret = prepare_wimoverlay_dat(old_contents, wim_path, wim_guid, image,
				     &new_contents, &new_contents_size,
				     &new_data_source_id);
	FREE(old_contents);
	if (ret)
		goto out;

	/* Write WimOverlay.wimlib_new  */
	ret = write_wimoverlay_dat(path_new,
				   new_contents, new_contents_size);
	if (ret)
		goto out_free_new_contents;

	/* Write WimOverlay.backup  */
	ret = write_wimoverlay_dat(path_backup,
				   new_contents, new_contents_size);
	if (ret)
		goto out_free_new_contents;

	if (old_contents) {
		/* Rename WimOverlay.dat => WimOverlay.wimlib_backup  */
		ret = win32_rename_replacement(path_main, path_wimlib_backup);
		if (ret) {
			ERROR_WITH_ERRNO("Can't rename \"%ls\" => \"%ls\"",
					 path_main, path_wimlib_backup);
			ret = WIMLIB_ERR_RENAME;
			goto out_free_new_contents;
		}
	}

	/* Rename WimOverlay.wimlib_new => WimOverlay.dat  */
	ret = win32_rename_replacement(path_new, path_main);
	if (ret) {
		ERROR_WITH_ERRNO("Can't rename \"%ls\" => \"%ls\"",
				 path_new, path_main);
		ret = WIMLIB_ERR_RENAME;
	}
out_free_new_contents:
	FREE(new_contents);
out:
	if (ret == WIMLIB_ERR_UNSUPPORTED) {
		ERROR("Please report to developer ("PACKAGE_BUGREPORT").\n"
		      "        If possible send the file \"%ls\".\n\n", path_main);
	}
	if (ret == 0)
		*data_source_id_ret = new_data_source_id;
	return ret;
}

/*
 * Allocate a WOF data source ID for a WIM file.
 *
 * @wim_path
 *	Absolute path to the WIM file.  This must include a drive letter and use
 *	backslash path separators.
 * @wim_guid
 *	GUID of the WIM, from the WIM header.
 * @image
 *	Number of the image in the WIM being applied.
 * @target
 *	Path to the target directory.
 * @data_source_id_ret
 *	On success, an identifier for the backing WIM file will be returned
 *	here.
 *
 * Returns 0 on success, or a positive error code on failure.
 */
int
wimboot_alloc_data_source_id(const wchar_t *wim_path,
			     const u8 wim_guid[GUID_SIZE],
			     int image, const wchar_t *target,
			     u64 *data_source_id_ret, bool *wof_running_ret)
{
	tchar drive_path[7];
	size_t wim_path_nchars;
	size_t wim_file_name_length;
	void *in;
	size_t insize;
	WOF_EXTERNAL_INFO *wof_info;
	WIM_PROVIDER_ADD_OVERLAY_INPUT *wim_info;
	wchar_t *WimFileName;
	HANDLE h;
	u64 data_source_id;
	DWORD bytes_returned;
	int ret;
	const wchar_t *prefix = L"\\??\\";
	const size_t prefix_nchars = 4;
	bool tried_to_attach_wof = false;

	ret = win32_get_drive_path(target, drive_path);
	if (ret)
		return ret;

	wimlib_assert(!wcschr(wim_path, L'/'));
	wimlib_assert(wim_path[0] != L'\0' && wim_path[1] == L':');

	wim_path_nchars = wcslen(wim_path);
	wim_file_name_length = sizeof(wchar_t) *
			       (wim_path_nchars + prefix_nchars);

	insize = sizeof(*wof_info) + sizeof(*wim_info) + wim_file_name_length;
	in = CALLOC(1, insize);
	if (!in) {
		ret = WIMLIB_ERR_NOMEM;
		goto out;
	}

	wof_info = (WOF_EXTERNAL_INFO *)in;
	wof_info->Version = WOF_CURRENT_VERSION;
	wof_info->Provider = WOF_PROVIDER_WIM;

	wim_info = (WIM_PROVIDER_ADD_OVERLAY_INPUT *)(wof_info + 1);
	wim_info->WimType = WIM_BOOT_NOT_OS_WIM;
	wim_info->WimIndex = image;
	wim_info->WimFileNameOffset = sizeof(*wim_info);
	wim_info->WimFileNameLength = wim_file_name_length;
	WimFileName = (wchar_t *)(wim_info + 1);
	wmemcpy(WimFileName, prefix, prefix_nchars);
	wmemcpy(&WimFileName[prefix_nchars], wim_path, wim_path_nchars);

retry_ioctl:
	h = open_file(drive_path, GENERIC_WRITE);

	if (h == INVALID_HANDLE_VALUE) {
		win32_error(GetLastError(),
			    L"Failed to open \"%ls\"", drive_path + 4);
		ret = WIMLIB_ERR_OPEN;
		goto out_free_in;
	}

	if (!DeviceIoControl(h, FSCTL_ADD_OVERLAY,
			     in, insize,
			     &data_source_id, sizeof(data_source_id),
			     &bytes_returned, NULL))
	{
		DWORD err = GetLastError();
		if (err == ERROR_INVALID_FUNCTION) {
			if (!tried_to_attach_wof) {
				CloseHandle(h);
				h = INVALID_HANDLE_VALUE;
				tried_to_attach_wof = true;
				if (win32_try_to_attach_wof(drive_path + 4))
					goto retry_ioctl;
			}
			ret = WIMLIB_ERR_UNSUPPORTED;
			goto out_close_handle;
		} else {
			win32_error(err, L"Failed to add overlay source \"%ls\" "
				    "to volume \"%ls\"", wim_path, drive_path + 4);
			ret = WIMLIB_ERR_WIMBOOT;
			goto out_close_handle;
		}
	}

	if (bytes_returned != sizeof(data_source_id)) {
		ret = WIMLIB_ERR_WIMBOOT;
		ERROR("Unexpected result size when adding "
		      "overlay source \"%ls\" to volume \"%ls\"",
		      wim_path, drive_path + 4);
		goto out_close_handle;
	}

	*wof_running_ret = true;
	*data_source_id_ret = data_source_id;
	ret = 0;

out_close_handle:
	CloseHandle(h);
out_free_in:
	FREE(in);
out:
	if (ret == WIMLIB_ERR_UNSUPPORTED) {
		WARNING("WOF driver is not available; updating WimOverlay.dat directly.");
		ret = update_wimoverlay_manually(drive_path + 4, wim_path,
						 wim_guid, image,
						 data_source_id_ret);
		*wof_running_ret = false;
	}
	return ret;
}


/*
 * Set WIMBoot information on the specified file.
 *
 * This turns it into a reparse point that redirects accesses to it, to the
 * corresponding resource in the WIM archive.
 *
 * @h
 *	Open handle to the file, with GENERIC_WRITE access.
 * @blob
 *	The blob for the unnamed data stream of the file.
 * @data_source_id
 *	Allocated identifier for the WIM data source on the destination volume.
 * @blob_table_hash
 *	SHA-1 message digest of the WIM's blob table.
 * @wof_running
 *	%true if the WOF driver appears to be available and working; %false if
 *	not.
 *
 * Returns %true on success, or %false on failure with GetLastError() set.
 */
bool
wimboot_set_pointer(HANDLE h,
		    const struct blob_descriptor *blob,
		    u64 data_source_id,
		    const u8 blob_table_hash[SHA1_HASH_SIZE],
		    bool wof_running)
{
	DWORD bytes_returned;

	if (wof_running) {
		/* The WOF driver is running.  We can create the reparse point
		 * using FSCTL_SET_EXTERNAL_BACKING.  */
		unsigned int max_retries = 4;
		struct {
			WOF_EXTERNAL_INFO wof_info;
			WIM_PROVIDER_EXTERNAL_INFO wim_info;
		} in;

	retry:
		memset(&in, 0, sizeof(in));

		in.wof_info.Version = WOF_CURRENT_VERSION;
		in.wof_info.Provider = WOF_PROVIDER_WIM;

		in.wim_info.Version = WIM_PROVIDER_CURRENT_VERSION;
		in.wim_info.Flags = 0;
		in.wim_info.DataSourceId.QuadPart = data_source_id;
		copy_hash(in.wim_info.ResourceHash, blob->hash);

		/* blob_table_hash is not necessary  */

		if (!DeviceIoControl(h, FSCTL_SET_EXTERNAL_BACKING,
				     &in, sizeof(in), NULL, 0,
				     &bytes_returned, NULL))
		{
			/* Try to track down sporadic errors  */
			if (wimlib_print_errors) {
				WARNING("FSCTL_SET_EXTERNAL_BACKING failed (err=%u); data was %zu bytes:",
					(u32)GetLastError(), sizeof(in));
				print_byte_field((const u8 *)&in, sizeof(in), wimlib_error_file);
				putc('\n', wimlib_error_file);
			}
			if (--max_retries) {
				WARNING("Retrying after 100ms...");
				Sleep(100);
				goto retry;
			}
			WARNING("Too many retries; returning failure");
			return false;
		}
	} else {

		/* The WOF driver is running.  We need to create the reparse
		 * point manually.  */

		struct {
			struct {
				le32 rptag;
				le16 rpdatalen;
				le16 rpreserved;
			} hdr;
			WOF_EXTERNAL_INFO wof_info;
			struct wim_provider_rpdata wim_info;
		} in;

		STATIC_ASSERT(sizeof(in) == 8 +
			      sizeof(WOF_EXTERNAL_INFO) +
			      sizeof(struct wim_provider_rpdata));

		in.hdr.rptag = WIM_IO_REPARSE_TAG_WOF;
		in.hdr.rpdatalen = sizeof(in) - sizeof(in.hdr);
		in.hdr.rpreserved = 0;

		in.wof_info.Version = WOF_CURRENT_VERSION;
		in.wof_info.Provider = WOF_PROVIDER_WIM;

		in.wim_info.version = 2;
		in.wim_info.flags = 0;
		in.wim_info.data_source_id = data_source_id;
		copy_hash(in.wim_info.unnamed_data_stream_hash, blob->hash);
		copy_hash(in.wim_info.blob_table_hash, blob_table_hash);
		in.wim_info.unnamed_data_stream_size = blob->size;
		in.wim_info.unnamed_data_stream_size_in_wim = blob->rdesc->size_in_wim;
		in.wim_info.unnamed_data_stream_offset_in_wim = blob->rdesc->offset_in_wim;

		if (!DeviceIoControl(h, FSCTL_SET_REPARSE_POINT,
				     &in, sizeof(in), NULL, 0, &bytes_returned, NULL))
			return false;

		/* We also need to create an unnamed data stream of the correct
		 * size.  Otherwise the file shows up as zero length.  It can be
		 * a sparse stream containing all zeroes; its contents
		 * are unimportant.  */
		if (!DeviceIoControl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0,
				     &bytes_returned, NULL))
			return false;

		if (!SetFilePointerEx(h,
				      (LARGE_INTEGER){ .QuadPart = blob->size},
				      NULL, FILE_BEGIN))
			return false;

		if (!SetEndOfFile(h))
			return false;
	}

	return true;
}

#endif /* _WIN32 */
