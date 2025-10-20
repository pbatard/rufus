/*
 * header.c
 *
 * Read, write, or print a WIM header.
 */

/*
 * Copyright 2012-2023 Eric Biggers
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wimlib.h"
#include "wimlib/alloca.h"
#include "wimlib/assert.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/file_io.h"
#include "wimlib/header.h"
#include "wimlib/util.h"
#include "wimlib/wim.h"

/*
 * Reads the header from a WIM file.
 *
 * @wim
 *	WIM to read the header from.  @wim->in_fd must be positioned at the
 *	beginning of the file.
 *
 * @hdr
 *	Structure to read the header into.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_IMAGE_COUNT
 *	WIMLIB_ERR_INVALID_PART_NUMBER
 *	WIMLIB_ERR_NOT_A_WIM_FILE
 *	WIMLIB_ERR_READ
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE
 *	WIMLIB_ERR_UNKNOWN_VERSION
 */
int
read_wim_header(WIMStruct *wim, struct wim_header *hdr)
{
	PRAGMA_ALIGN(struct wim_header_disk disk_hdr, 8);
	struct filedes *in_fd = &wim->in_fd;
	const tchar *filename = wim->filename;
	int ret;
	tchar *pipe_str;

	wimlib_assert(in_fd->offset == 0);

	if (filename == NULL) {
		pipe_str = alloca(40);
		tsprintf(pipe_str, T("[fd %d]"), in_fd->fd);
		filename = pipe_str;
	}

	STATIC_ASSERT(sizeof(struct wim_header_disk) == WIM_HEADER_DISK_SIZE);

	ret = full_read(in_fd, &disk_hdr, sizeof(disk_hdr));
	if (ret)
		goto read_error;

	hdr->magic = le64_to_cpu(disk_hdr.magic);

	if (hdr->magic != WIM_MAGIC) {
		if (hdr->magic == PWM_MAGIC) {
			/* Pipable WIM:  Use header at end instead, unless
			 * actually reading from a pipe.  */
			if (!in_fd->is_pipe) {
				ret = WIMLIB_ERR_READ;
				if (-1 == _lseeki64(in_fd->fd, -WIM_HEADER_DISK_SIZE, SEEK_END))
					goto read_error;
				ret = full_read(in_fd, &disk_hdr, sizeof(disk_hdr));
				if (ret)
					goto read_error;
			}
		} else {
			ERROR("\"%"TS"\": Invalid magic characters in header", filename);
			return WIMLIB_ERR_NOT_A_WIM_FILE;
		}
	}

	if (le32_to_cpu(disk_hdr.hdr_size) != sizeof(struct wim_header_disk)) {
		ERROR("\"%"TS"\": Header size is invalid (%u bytes)",
		      filename, le32_to_cpu(disk_hdr.hdr_size));
		return WIMLIB_ERR_INVALID_HEADER;
	}

	hdr->wim_version = le32_to_cpu(disk_hdr.wim_version);
	if (hdr->wim_version != WIM_VERSION_DEFAULT &&
	    hdr->wim_version != WIM_VERSION_SOLID)
	{
		ERROR("\"%"TS"\": Unknown WIM version: %u",
		      filename, hdr->wim_version);
		return WIMLIB_ERR_UNKNOWN_VERSION;
	}

	hdr->flags = le32_to_cpu(disk_hdr.wim_flags);
	hdr->chunk_size = le32_to_cpu(disk_hdr.chunk_size);
	copy_guid(hdr->guid, disk_hdr.guid);
	hdr->part_number = le16_to_cpu(disk_hdr.part_number);
	hdr->total_parts = le16_to_cpu(disk_hdr.total_parts);

	if (hdr->total_parts == 0 || hdr->part_number == 0 ||
	    hdr->part_number > hdr->total_parts)
	{
		ERROR("\"%"TS"\": Invalid WIM part number: %hu of %hu",
		      filename, hdr->part_number, hdr->total_parts);
		return WIMLIB_ERR_INVALID_PART_NUMBER;
	}

	hdr->image_count = le32_to_cpu(disk_hdr.image_count);

	if (unlikely(hdr->image_count > MAX_IMAGES)) {
		ERROR("\"%"TS"\": Invalid image count (%u)",
		      filename, hdr->image_count);
		return WIMLIB_ERR_IMAGE_COUNT;
	}

	get_wim_reshdr(&disk_hdr.blob_table_reshdr, &hdr->blob_table_reshdr);
	get_wim_reshdr(&disk_hdr.xml_data_reshdr, &hdr->xml_data_reshdr);
	get_wim_reshdr(&disk_hdr.boot_metadata_reshdr, &hdr->boot_metadata_reshdr);
	hdr->boot_idx = le32_to_cpu(disk_hdr.boot_idx);
	get_wim_reshdr(&disk_hdr.integrity_table_reshdr, &hdr->integrity_table_reshdr);

	/*
	 * Prevent huge memory allocations when processing fuzzed files.  The
	 * blob table, XML data, and integrity table are all uncompressed, so
	 * they should never be larger than the WIM file itself.
	 */
	if (wim->file_size > 0 &&
	    (hdr->blob_table_reshdr.uncompressed_size > wim->file_size ||
	     hdr->xml_data_reshdr.uncompressed_size > wim->file_size ||
	     hdr->integrity_table_reshdr.uncompressed_size > wim->file_size))
		return WIMLIB_ERR_INVALID_HEADER;

	return 0;

read_error:
	ERROR_WITH_ERRNO("\"%"TS"\": Error reading header", filename);
	return ret;
}

/* Writes the header for a WIM file at the specified offset.  If the offset
 * specified is the current one, the position is advanced by the size of the
 * header.  */
int
write_wim_header(const struct wim_header *hdr, struct filedes *out_fd,
		 off_t offset)
{
	PRAGMA_ALIGN(struct wim_header_disk disk_hdr, 8);
	int ret;

	disk_hdr.magic = cpu_to_le64(hdr->magic);
	disk_hdr.hdr_size = cpu_to_le32(sizeof(struct wim_header_disk));
	disk_hdr.wim_version = cpu_to_le32(hdr->wim_version);
	disk_hdr.wim_flags = cpu_to_le32(hdr->flags);
	disk_hdr.chunk_size = cpu_to_le32(hdr->chunk_size);
	copy_guid(disk_hdr.guid, hdr->guid);
	disk_hdr.part_number = cpu_to_le16(hdr->part_number);
	disk_hdr.total_parts = cpu_to_le16(hdr->total_parts);
	disk_hdr.image_count = cpu_to_le32(hdr->image_count);
	put_wim_reshdr(&hdr->blob_table_reshdr, &disk_hdr.blob_table_reshdr);
	put_wim_reshdr(&hdr->xml_data_reshdr, &disk_hdr.xml_data_reshdr);
	put_wim_reshdr(&hdr->boot_metadata_reshdr, &disk_hdr.boot_metadata_reshdr);
	disk_hdr.boot_idx = cpu_to_le32(hdr->boot_idx);
	put_wim_reshdr(&hdr->integrity_table_reshdr, &disk_hdr.integrity_table_reshdr);
	memset(disk_hdr.unused, 0, sizeof(disk_hdr.unused));

	if (offset == out_fd->offset)
		ret = full_write(out_fd, &disk_hdr, sizeof(disk_hdr));
	else
		ret = full_pwrite(out_fd, &disk_hdr, sizeof(disk_hdr), offset);
	if (ret)
		ERROR_WITH_ERRNO("Failed to write WIM header");
	return ret;
}

/* Update just the wim_flags field. */
int
write_wim_header_flags(u32 hdr_flags, struct filedes *out_fd)
{
	le32 flags = cpu_to_le32(hdr_flags);

	return full_pwrite(out_fd, &flags, sizeof(flags),
			   offsetof(struct wim_header_disk, wim_flags));
}

static const struct {
	u32 flag;
	const tchar *name;
} hdr_flags[] = {
	{WIM_HDR_FLAG_RESERVED,		T("RESERVED")},
	{WIM_HDR_FLAG_COMPRESSION,	T("COMPRESSION")},
	{WIM_HDR_FLAG_READONLY,		T("READONLY")},
	{WIM_HDR_FLAG_SPANNED,		T("SPANNED")},
	{WIM_HDR_FLAG_RESOURCE_ONLY,	T("RESOURCE_ONLY")},
	{WIM_HDR_FLAG_METADATA_ONLY,	T("METADATA_ONLY")},
	{WIM_HDR_FLAG_WRITE_IN_PROGRESS,T("WRITE_IN_PROGRESS")},
	{WIM_HDR_FLAG_RP_FIX,		T("RP_FIX")},
	{WIM_HDR_FLAG_COMPRESS_RESERVED,T("COMPRESS_RESERVED")},
	{WIM_HDR_FLAG_COMPRESS_LZX,	T("COMPRESS_LZX")},
	{WIM_HDR_FLAG_COMPRESS_XPRESS,	T("COMPRESS_XPRESS")},
	{WIM_HDR_FLAG_COMPRESS_LZMS,	T("COMPRESS_LZMS")},
	{WIM_HDR_FLAG_COMPRESS_XPRESS_2,T("COMPRESS_XPRESS_2")},
};

/* API function documented in wimlib.h  */
WIMLIBAPI void
wimlib_print_header(const WIMStruct *wim)
{
	const struct wim_header *hdr = &wim->hdr;

	tprintf(T("Magic Characters            = "));
	for (int i = 0; i < sizeof(hdr->magic); i++) {
		tchar c = (u8)(hdr->magic >> ((8 * i)));
		if (istalpha(c))
			tputchar(c);
		else
			tprintf(T("\\%o"), c);
	}
	tputchar(T('\n'));
	tprintf(T("Header Size                 = %u\n"), WIM_HEADER_DISK_SIZE);
	tprintf(T("Version                     = 0x%x\n"), hdr->wim_version);

	tprintf(T("Flags                       = 0x%x\n"), hdr->flags);
	for (size_t i = 0; i < ARRAY_LEN(hdr_flags); i++)
		if (hdr_flags[i].flag & hdr->flags)
			tprintf(T("    WIM_HDR_FLAG_%s is set\n"), hdr_flags[i].name);

	tprintf(T("Chunk Size                  = %u\n"), hdr->chunk_size);
	tfputs (T("GUID                        = "), stdout);
	print_byte_field(hdr->guid, GUID_SIZE, stdout);
	tputchar(T('\n'));
	tprintf(T("Part Number                 = %hu\n"), hdr->part_number);
	tprintf(T("Total Parts                 = %hu\n"), hdr->total_parts);
	tprintf(T("Image Count                 = %u\n"), hdr->image_count);
	tprintf(T("Blob Table Size             = %"PRIu64"\n"),
				(u64)hdr->blob_table_reshdr.size_in_wim);
	tprintf(T("Blob Table Flags            = 0x%hhx\n"),
				(u8)hdr->blob_table_reshdr.flags);
	tprintf(T("Blob Table Offset           = %"PRIu64"\n"),
				hdr->blob_table_reshdr.offset_in_wim);
	tprintf(T("Blob Table Original_size    = %"PRIu64"\n"),
				hdr->blob_table_reshdr.uncompressed_size);
	tprintf(T("XML Data Size               = %"PRIu64"\n"),
				(u64)hdr->xml_data_reshdr.size_in_wim);
	tprintf(T("XML Data Flags              = 0x%hhx\n"),
				(u8)hdr->xml_data_reshdr.flags);
	tprintf(T("XML Data Offset             = %"PRIu64"\n"),
				hdr->xml_data_reshdr.offset_in_wim);
	tprintf(T("XML Data Original Size      = %"PRIu64"\n"),
				hdr->xml_data_reshdr.uncompressed_size);
	tprintf(T("Boot Metadata Size          = %"PRIu64"\n"),
				(u64)hdr->boot_metadata_reshdr.size_in_wim);
	tprintf(T("Boot Metadata Flags         = 0x%hhx\n"),
				(u8)hdr->boot_metadata_reshdr.flags);
	tprintf(T("Boot Metadata Offset        = %"PRIu64"\n"),
				hdr->boot_metadata_reshdr.offset_in_wim);
	tprintf(T("Boot Metadata Original Size = %"PRIu64"\n"),
				hdr->boot_metadata_reshdr.uncompressed_size);
	tprintf(T("Boot Index                  = %u\n"), hdr->boot_idx);
	tprintf(T("Integrity Size              = %"PRIu64"\n"),
				(u64)hdr->integrity_table_reshdr.size_in_wim);
	tprintf(T("Integrity Flags             = 0x%hhx\n"),
				(u8)hdr->integrity_table_reshdr.flags);
	tprintf(T("Integrity Offset            = %"PRIu64"\n"),
				hdr->integrity_table_reshdr.offset_in_wim);
	tprintf(T("Integrity Original_size     = %"PRIu64"\n"),
				hdr->integrity_table_reshdr.uncompressed_size);
}
