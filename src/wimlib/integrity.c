/*
 * integrity.c
 *
 * WIM files can optionally contain a table of SHA1 message digests at the end,
 * one digest for each chunk of the file of some specified size (often 10 MB).
 * This file implements the checking and writing of this table.
 */

/*
 * Copyright (C) 2012-2016 Eric Biggers
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

#include "wimlib/assert.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/file_io.h"
#include "wimlib/integrity.h"
#include "wimlib/progress.h"
#include "wimlib/resource.h"
#include "wimlib/sha1.h"
#include "wimlib/wim.h"
#include "wimlib/write.h"

/* Size, in bytes, of each SHA1-summed chunk, when wimlib writes integrity
 * information. */
#define INTEGRITY_CHUNK_SIZE 10485760

/* Only use a different chunk size for compatibility with an existing integrity
 * table if the chunk size is between these two numbers. */
#define INTEGRITY_MIN_CHUNK_SIZE 4096
#define INTEGRITY_MAX_CHUNK_SIZE 134217728

PRAGMA_BEGIN_PACKED
struct integrity_table {
	u32 size;
	u32 num_entries;
	u32 chunk_size;
	u8  sha1sums[][20];
} __attribute__((packed));
PRAGMA_END_PACKED

static int
calculate_chunk_sha1(struct filedes *in_fd, size_t this_chunk_size,
		     off_t offset, u8 sha1_md[])
{
	u8 buf[BUFFER_SIZE];
	struct sha1_ctx ctx;
	size_t bytes_remaining;
	size_t bytes_to_read;
	int ret;

	bytes_remaining = this_chunk_size;
	sha1_init(&ctx);
	do {
		bytes_to_read = min(bytes_remaining, sizeof(buf));
		ret = full_pread(in_fd, buf, bytes_to_read, offset);
		if (ret) {
			ERROR_WITH_ERRNO("Read error while calculating "
					 "integrity checksums");
			return ret;
		}
		// I have no idea what Coverity is hallucinating with here...
		// coverity[overrun-call]
		sha1_update(&ctx, buf, bytes_to_read);
		bytes_remaining -= bytes_to_read;
		offset += bytes_to_read;
	} while (bytes_remaining);
	sha1_final(&ctx, sha1_md);
	return 0;
}


/*
 * read_integrity_table: -  Reads the integrity table from a WIM file.
 *
 * @wim:
 *	WIMStruct for the WIM file; @wim->hdr.integrity_table_reshdr specifies
 *	the location of the integrity table.  @wim->in_fd is expected to be a
 *	seekable file descriptor to the WIM file opened for reading.
 *
 * @num_checked_bytes:
 *	Number of bytes of data that should be checked by the integrity table.
 *
 * @table_ret:
 *	On success, a pointer to an in-memory structure containing the integrity
 *	information is written to this location.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_INVALID_INTEGRITY_TABLE
 *	WIMLIB_ERR_NOMEM
 *	WIMLIB_ERR_READ
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE
 */
int
read_integrity_table(WIMStruct *wim, u64 num_checked_bytes,
		     struct integrity_table **table_ret)
{
	void *buf;
	struct integrity_table *table;
	int ret;

	STATIC_ASSERT(sizeof(struct integrity_table) == 12);
	if (wim->hdr.integrity_table_reshdr.uncompressed_size < 12)
		return WIMLIB_ERR_INVALID_INTEGRITY_TABLE;

	ret = wim_reshdr_to_data(&wim->hdr.integrity_table_reshdr, wim, &buf);
	if (ret)
		return ret;
	table = buf;

	table->size        = le32_to_cpu((_force_attr le32)table->size);
	table->num_entries = le32_to_cpu((_force_attr le32)table->num_entries);
	table->chunk_size  = le32_to_cpu((_force_attr le32)table->chunk_size);

	if (table->size != wim->hdr.integrity_table_reshdr.uncompressed_size ||
	    table->size != (u64)table->num_entries * SHA1_HASH_SIZE + 12 ||
	    table->chunk_size == 0 ||
	    table->num_entries != DIV_ROUND_UP(num_checked_bytes, table->chunk_size))
	{
		FREE(table);
		return WIMLIB_ERR_INVALID_INTEGRITY_TABLE;
	}

	*table_ret = table;
	return 0;
}

/*
 * calculate_integrity_table():
 *
 * Calculates an integrity table for the data in a file beginning at offset 208
 * (WIM_HEADER_DISK_SIZE).
 *
 * @in_fd:
 *	File descriptor for the file to be checked, opened for reading.  Does
 *	not need to be at any specific location in the file.
 *
 * @new_check_end:
 *	Offset of byte after the last byte to be checked.
 *
 * @old_table:
 *	If non-NULL, a pointer to the table containing the previously calculated
 *	integrity data for a prefix of this file.
 *
 * @old_check_end:
 *	If @old_table is non-NULL, the byte after the last byte that was checked
 *	in the old table.  Must be less than or equal to new_check_end.
 *
 * @integrity_table_ret:
 *	On success, a pointer to the calculated integrity table is written into
 *	this location.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_NOMEM
 *	WIMLIB_ERR_READ
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE
 */
static int
calculate_integrity_table(struct filedes *in_fd,
			  off_t new_check_end,
			  const struct integrity_table *old_table,
			  off_t old_check_end,
			  struct integrity_table **integrity_table_ret,
			  wimlib_progress_func_t progfunc,
			  void *progctx)
{
	int ret;
	size_t chunk_size = INTEGRITY_CHUNK_SIZE;

	/* If an old table is provided, set the chunk size to be compatible with
	 * the old chunk size, unless the old chunk size was weird. */
	if (old_table != NULL) {
		if (old_table->num_entries == 0 ||
		    old_table->chunk_size < INTEGRITY_MIN_CHUNK_SIZE ||
		    old_table->chunk_size > INTEGRITY_MAX_CHUNK_SIZE)
			old_table = NULL;
		else
			chunk_size = old_table->chunk_size;
	}


	u64 old_check_bytes = old_check_end - WIM_HEADER_DISK_SIZE;
	u64 new_check_bytes = new_check_end - WIM_HEADER_DISK_SIZE;

	u32 old_num_chunks = DIV_ROUND_UP(old_check_bytes, chunk_size);
	u32 new_num_chunks = DIV_ROUND_UP(new_check_bytes, chunk_size);

	size_t old_last_chunk_size = MODULO_NONZERO(old_check_bytes, chunk_size);
	size_t new_last_chunk_size = MODULO_NONZERO(new_check_bytes, chunk_size);

	size_t new_table_size = 12 + new_num_chunks * SHA1_HASH_SIZE;

	struct integrity_table *new_table = MALLOC(new_table_size);
	if (!new_table)
		return WIMLIB_ERR_NOMEM;
	new_table->num_entries = new_num_chunks;
	new_table->size = new_table_size;
	new_table->chunk_size = chunk_size;

	u64 offset = WIM_HEADER_DISK_SIZE;
	union wimlib_progress_info progress;

	progress.integrity.total_bytes      = new_check_bytes;
	progress.integrity.total_chunks     = new_num_chunks;
	progress.integrity.completed_chunks = 0;
	progress.integrity.completed_bytes  = 0;
	progress.integrity.chunk_size       = chunk_size;
	progress.integrity.filename         = NULL;

	ret = call_progress(progfunc, WIMLIB_PROGRESS_MSG_CALC_INTEGRITY,
			    &progress, progctx);
	if (ret)
		goto out_free_new_table;

	for (u32 i = 0; i < new_num_chunks; i++) {
		size_t this_chunk_size;
		if (i == new_num_chunks - 1)
			this_chunk_size = new_last_chunk_size;
		else
			this_chunk_size = chunk_size;
		if (old_table &&
		    ((this_chunk_size == chunk_size && i < old_num_chunks - 1) ||
		      (i == old_num_chunks - 1 && this_chunk_size == old_last_chunk_size)))
		{
			/* Can use SHA1 message digest from old integrity table
			 * */
			copy_hash(new_table->sha1sums[i], old_table->sha1sums[i]);
		} else {
			/* Calculate the SHA1 message digest of this chunk */
			ret = calculate_chunk_sha1(in_fd, this_chunk_size,
						   offset, new_table->sha1sums[i]);
			if (ret)
				goto out_free_new_table;
		}
		offset += this_chunk_size;

		progress.integrity.completed_chunks++;
		progress.integrity.completed_bytes += this_chunk_size;
		ret = call_progress(progfunc, WIMLIB_PROGRESS_MSG_CALC_INTEGRITY,
				    &progress, progctx);
		if (ret)
			goto out_free_new_table;
	}
	*integrity_table_ret = new_table;
	return 0;

out_free_new_table:
	FREE(new_table);
	return ret;
}

/*
 * write_integrity_table():
 *
 * Writes a WIM integrity table (a list of SHA1 message digests of raw 10 MiB
 * chunks of the file).
 *
 * This function can optionally re-use entries from an older integrity table.
 * To do this, specify old_blob_table_end and old_table.
 *
 * On success, @wim->out_hdr.integrity_table_reshdr will be filled in with
 * information about the integrity table that was written.
 *
 * @wim:
 *	WIMStruct for the WIM file.  @wim->out_fd must be a seekable descriptor
 *	to the new WIM file, opened read-write, positioned at the location at
 *	which the integrity table is to be written.
 *
 * @new_blob_table_end:
 *	The offset of the byte directly following the blob table in the WIM
 *	being written.
 *
 * @old_blob_table_end:
 *	If nonzero, the offset of the byte directly following the old blob table
 *	in the WIM.
 *
 * @old_table
 *	Pointer to the old integrity table read into memory, or NULL if not
 *	specified.
 */
int
write_integrity_table(WIMStruct *wim,
		      off_t new_blob_table_end,
		      off_t old_blob_table_end,
		      struct integrity_table *old_table)
{
	struct integrity_table *new_table;
	int ret;
	u32 new_table_size;

	wimlib_assert(old_blob_table_end <= new_blob_table_end);

	ret = calculate_integrity_table(&wim->out_fd, new_blob_table_end,
					old_table, old_blob_table_end,
					&new_table, wim->progfunc, wim->progctx);
	if (ret)
		return ret;

	new_table_size = new_table->size;

	new_table->size        = (_force_attr u32)cpu_to_le32(new_table->size);
	new_table->num_entries = (_force_attr u32)cpu_to_le32(new_table->num_entries);
	new_table->chunk_size  = (_force_attr u32)cpu_to_le32(new_table->chunk_size);

	ret = write_wim_resource_from_buffer(new_table,
					     new_table_size,
					     false,
					     &wim->out_fd,
					     WIMLIB_COMPRESSION_TYPE_NONE,
					     0,
					     &wim->out_hdr.integrity_table_reshdr,
					     NULL,
					     0);
	FREE(new_table);
	return ret;
}

/*
 * verify_integrity():
 *
 * Checks a WIM for consistency with the integrity table.
 *
 * @in_fd:
 *	File descriptor to the WIM file, opened for reading.
 *
 * @table:
 *	The integrity table for the WIM, read into memory.
 *
 * @bytes_to_check:
 *	Number of bytes in the WIM that need to be checked (offset of end of the
 *	blob table minus offset of end of the header).
 *
 * Returns:
 *	> 0 (WIMLIB_ERR_READ, WIMLIB_ERR_UNEXPECTED_END_OF_FILE) on error
 *	0 (WIM_INTEGRITY_OK) if the integrity was checked successfully and there
 *	were no inconsistencies.
 *	-1 (WIM_INTEGRITY_NOT_OK) if the WIM failed the integrity check.
 */
static int
verify_integrity(struct filedes *in_fd, const tchar *filename,
		 const struct integrity_table *table,
		 u64 bytes_to_check,
		 wimlib_progress_func_t progfunc, void *progctx)
{
	int ret;
	u64 offset = WIM_HEADER_DISK_SIZE;
	u8 sha1_md[SHA1_HASH_SIZE];
	union wimlib_progress_info progress;

	progress.integrity.total_bytes      = bytes_to_check;
	progress.integrity.total_chunks     = table->num_entries;
	progress.integrity.completed_chunks = 0;
	progress.integrity.completed_bytes  = 0;
	progress.integrity.chunk_size       = table->chunk_size;
	progress.integrity.filename         = filename;

	ret = call_progress(progfunc, WIMLIB_PROGRESS_MSG_VERIFY_INTEGRITY,
			    &progress, progctx);
	if (ret)
		return ret;

	for (u32 i = 0; i < table->num_entries; i++) {
		size_t this_chunk_size;
		if (i == table->num_entries - 1)
			this_chunk_size = MODULO_NONZERO(bytes_to_check,
							 table->chunk_size);
		else
			this_chunk_size = table->chunk_size;

		ret = calculate_chunk_sha1(in_fd, this_chunk_size, offset, sha1_md);
		if (ret)
			return ret;

		if (!hashes_equal(sha1_md, table->sha1sums[i]))
			return WIM_INTEGRITY_NOT_OK;

		offset += this_chunk_size;
		progress.integrity.completed_chunks++;
		progress.integrity.completed_bytes += this_chunk_size;

		ret = call_progress(progfunc, WIMLIB_PROGRESS_MSG_VERIFY_INTEGRITY,
				    &progress, progctx);
		if (ret)
			return ret;
	}
	return WIM_INTEGRITY_OK;
}


/*
 * check_wim_integrity():
 *
 * Verifies the integrity of the WIM by making sure the SHA1 message digests of
 * ~10 MiB chunks of the WIM match up with the values given in the integrity
 * table.
 *
 * @wim:
 *	The WIM, opened for reading.
 *
 * Returns:
 *	> 0 (WIMLIB_ERR_INVALID_INTEGRITY_TABLE, WIMLIB_ERR_READ,
 *	     WIMLIB_ERR_UNEXPECTED_END_OF_FILE) on error
 *	0 (WIM_INTEGRITY_OK) if the integrity was checked successfully and there
 *	were no inconsistencies.
 *	-1 (WIM_INTEGRITY_NOT_OK) if the WIM failed the integrity check.
 *	-2 (WIM_INTEGRITY_NONEXISTENT) if the WIM contains no integrity
 *	information.
 */
int
check_wim_integrity(WIMStruct *wim)
{
	int ret;
	u64 bytes_to_check;
	struct integrity_table *table;
	u64 end_blob_table_offset;

	if (!wim_has_integrity_table(wim))
		return WIM_INTEGRITY_NONEXISTENT;

	end_blob_table_offset = wim->hdr.blob_table_reshdr.offset_in_wim +
				wim->hdr.blob_table_reshdr.size_in_wim;

	if (end_blob_table_offset < WIM_HEADER_DISK_SIZE) {
		ERROR("WIM blob table ends before WIM header ends!");
		return WIMLIB_ERR_INVALID_INTEGRITY_TABLE;
	}

	bytes_to_check = end_blob_table_offset - WIM_HEADER_DISK_SIZE;

	ret = read_integrity_table(wim, bytes_to_check, &table);
	if (ret)
		return ret;
	ret = verify_integrity(&wim->in_fd, wim->filename, table,
			       bytes_to_check, wim->progfunc, wim->progctx);
	FREE(table);
	return ret;
}
