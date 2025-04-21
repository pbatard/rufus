/*
 * resource.c
 *
 * Code for reading blobs and resources, including compressed WIM resources.
 */

/*
 * Copyright (C) 2012, 2013, 2015 Eric Biggers
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

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "wimlib/alloca.h"
#include "wimlib/assert.h"
#include "wimlib/bitops.h"
#include "wimlib/blob_table.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/file_io.h"
#include "wimlib/ntfs_3g.h"
#include "wimlib/resource.h"
#include "wimlib/sha1.h"
#include "wimlib/wim.h"
#include "wimlib/win32.h"

/*
 *                         Compressed WIM resources
 *
 * A compressed resource in a WIM consists of a sequence of chunks.  Each chunk
 * decompresses to the same size except possibly for the last, which
 * decompresses to the remaining size.  Chunks that did not compress to less
 * than their original size are stored uncompressed.
 *
 * We support three variations on this resource format, independently of the
 * compression type and chunk size which can vary as well:
 *
 * - Original resource format: immediately before the compressed chunks, the
 *   "chunk table" provides the offset, in bytes relative to the end of the
 *   chunk table, of the start of each compressed chunk, except for the first
 *   chunk which is omitted as it always has an offset of 0.  Chunk table
 *   entries are 32-bit for resources < 4 GiB uncompressed and 64-bit for
 *   resources >= 4 GiB uncompressed.
 *
 * - Solid resource format (distinguished by the use of WIM_RESHDR_FLAG_SOLID
 *   instead of WIM_RESHDR_FLAG_COMPRESSED): similar to the original format, but
 *   the resource begins with a 16-byte header which specifies the uncompressed
 *   size of the resource, the compression type, and the chunk size.  (In the
 *   original format, these values were instead determined from outside the
 *   resource itself, from the blob table and the WIM file header.) In addition,
 *   in this format the entries in the chunk table contain compressed chunk
 *   sizes rather than offsets.  As a consequence of this, the chunk table
 *   entries are always 32-bit and there is an entry for chunk 0.
 *
 * - Pipable resource format (wimlib extension; all resources in a pipable WIM
 *   have this format): similar to the original format, but the chunk table is
 *   at the end of the resource rather than the beginning, and each compressed
 *   chunk is prefixed with its compressed size as a 32-bit integer.  This
 *   format allows a resource to be written without rewinding.
 */


struct data_range {
	u64 offset;
	u64 size;
};

static int
decompress_chunk(const void *cbuf, u32 chunk_csize, u8 *ubuf, u32 chunk_usize,
		 struct wimlib_decompressor *decompressor, bool recover_data)
{
	int res = wimlib_decompress(cbuf, chunk_csize, ubuf, chunk_usize,
				    decompressor);
	if (likely(res == 0))
		return 0;

	if (recover_data) {
		WARNING("Failed to decompress data!  Continuing anyway since data recovery mode is enabled.");

		/* Continue on with *something*.  In the worst case just use a
		 * zeroed buffer.  But, try to fill as much of it with
		 * decompressed data as we can.  This works because if the
		 * corruption isn't located right at the beginning of the
		 * compressed chunk, wimlib_decompress() may write some correct
		 * output at the beginning even if it fails later.  */
		memset(ubuf, 0, chunk_usize);
		(void)wimlib_decompress(cbuf, chunk_csize, ubuf,
					chunk_usize, decompressor);
		return 0;
	}
	ERROR("Failed to decompress data!");
	errno = EINVAL;
	return WIMLIB_ERR_DECOMPRESSION;
}

/*
 * Read data from a compressed WIM resource.
 *
 * @rdesc
 *	Description of the compressed WIM resource to read from.
 * @ranges
 *	Nonoverlapping, nonempty ranges of the uncompressed resource data to
 *	read, sorted by increasing offset.
 * @num_ranges
 *	Number of ranges in @ranges; must be at least 1.
 * @cb
 *	Structure which provides the consume_chunk callback into which to feed
 *	the data being read.  Each call provides the next chunk of the requested
 *	data, uncompressed.  Each chunk will be nonempty and will not cross
 *	range boundaries but otherwise will be of unspecified size.
 * @recover_data
 *	If a chunk can't be fully decompressed due to being corrupted, continue
 *	with whatever data can be recovered rather than return an error.
 *
 * Possible return values:
 *
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_READ			  (errno set)
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE (errno set to EINVAL)
 *	WIMLIB_ERR_NOMEM		  (errno set to ENOMEM)
 *	WIMLIB_ERR_DECOMPRESSION	  (errno set to EINVAL)
 *	WIMLIB_ERR_INVALID_CHUNK_SIZE	  (errno set to EINVAL)
 *
 *	or other error code returned by the callback function.
 */
static int
read_compressed_wim_resource(const struct wim_resource_descriptor * const rdesc,
			     const struct data_range * const ranges,
			     const size_t num_ranges,
			     const struct consume_chunk_callback *cb,
			     bool recover_data)
{
	int ret;
	u64 *chunk_offsets = NULL;
	u8 *ubuf = NULL;
	void *cbuf = NULL;
	bool chunk_offsets_malloced = false;
	bool ubuf_malloced = false;
	bool cbuf_malloced = false;
	struct wimlib_decompressor *decompressor = NULL;

	/* Sanity checks  */
	wimlib_assert(num_ranges != 0);
	for (size_t i = 0; i < num_ranges; i++) {
		wimlib_assert(ranges[i].offset + ranges[i].size > ranges[i].offset &&
			      ranges[i].offset + ranges[i].size <= rdesc->uncompressed_size);
	}
	for (size_t i = 0; i < num_ranges - 1; i++)
		wimlib_assert(ranges[i].offset + ranges[i].size <= ranges[i + 1].offset);

	/* Get the offsets of the first and last bytes of the read.  */
	const u64 first_offset = ranges[0].offset;
	const u64 last_offset = ranges[num_ranges - 1].offset + ranges[num_ranges - 1].size - 1;

	/* Get the file descriptor for the WIM.  */
	struct filedes * const in_fd = &rdesc->wim->in_fd;

	/* Determine if we're reading a pipable resource from a pipe or not.  */
	const bool is_pipe_read = (rdesc->is_pipable && !filedes_is_seekable(in_fd));

	/* Determine if the chunk table is in an alternate format.  */
	const bool alt_chunk_table = (rdesc->flags & WIM_RESHDR_FLAG_SOLID)
					&& !is_pipe_read;

	/* Get the maximum size of uncompressed chunks in this resource, which
	 * we require be a power of 2.  */
	u64 cur_read_offset = rdesc->offset_in_wim;
	int ctype = rdesc->compression_type;
	u32 chunk_size = rdesc->chunk_size;
	if (alt_chunk_table) {
		/* Alternate chunk table format.  Its header specifies the chunk
		 * size and compression format.  Note: it could be read here;
		 * however, the relevant data was already loaded into @rdesc by
		 * read_blob_table().  */
		cur_read_offset += sizeof(struct alt_chunk_table_header_disk);
	}

	if (unlikely(!is_power_of_2(chunk_size))) {
		ERROR("Invalid compressed resource: "
		      "expected power-of-2 chunk size (got %"PRIu32")",
		      chunk_size);
		ret = WIMLIB_ERR_INVALID_CHUNK_SIZE;
		errno = EINVAL;
		goto out_cleanup;
	}

	/* Get valid decompressor.  */
	if (likely(ctype == rdesc->wim->decompressor_ctype &&
		   chunk_size == rdesc->wim->decompressor_max_block_size))
	{
		/* Cached decompressor.  */
		decompressor = rdesc->wim->decompressor;
		rdesc->wim->decompressor_ctype = WIMLIB_COMPRESSION_TYPE_NONE;
		rdesc->wim->decompressor = NULL;
	} else {
		ret = wimlib_create_decompressor(ctype, chunk_size,
						 &decompressor);
		if (unlikely(ret)) {
			if (ret != WIMLIB_ERR_NOMEM)
				errno = EINVAL;
			goto out_cleanup;
		}
	}

	const u32 chunk_order = bsr32(chunk_size);

	/* Calculate the total number of chunks the resource is divided into.  */
	const u64 num_chunks = (rdesc->uncompressed_size + chunk_size - 1) >> chunk_order;

	/* Calculate the 0-based indices of the first and last chunks containing
	 * data that needs to be passed to the callback.  */
	const u64 first_needed_chunk = first_offset >> chunk_order;
	const u64 last_needed_chunk = last_offset >> chunk_order;

	/* Calculate the 0-based index of the first chunk that actually needs to
	 * be read.  This is normally first_needed_chunk, but for pipe reads we
	 * must always start from the 0th chunk.  */
	const u64 read_start_chunk = (is_pipe_read ? 0 : first_needed_chunk);

	/* Calculate the number of chunk offsets that are needed for the chunks
	 * being read.  */
	const u64 num_needed_chunk_offsets =
		last_needed_chunk - read_start_chunk + 1 +
		(last_needed_chunk < num_chunks - 1);

	/* Calculate the number of entries in the chunk table.  Normally, it's
	 * one less than the number of chunks, since the first chunk has no
	 * entry.  But in the alternate chunk table format, the chunk entries
	 * contain chunk sizes, not offsets, and there is one per chunk.  */
	const u64 num_chunk_entries = (alt_chunk_table ? num_chunks : num_chunks - 1);

	/* Set the size of each chunk table entry based on the resource's
	 * uncompressed size.  */
	const u64 chunk_entry_size = get_chunk_entry_size(rdesc->uncompressed_size,
							  alt_chunk_table);

	/* Calculate the size of the chunk table in bytes.  */
	const u64 chunk_table_size = num_chunk_entries * chunk_entry_size;

	/* Calculate the size of the chunk table in bytes, including the header
	 * in the case of the alternate chunk table format.  */
	const u64 chunk_table_full_size =
		(alt_chunk_table) ? chunk_table_size + sizeof(struct alt_chunk_table_header_disk)
				  : chunk_table_size;

	if (!is_pipe_read) {
		/* Read the needed chunk table entries into memory and use them
		 * to initialize the chunk_offsets array.  */

		u64 first_chunk_entry_to_read;
		u64 num_chunk_entries_to_read;

		if (alt_chunk_table) {
			/* The alternate chunk table contains chunk sizes, not
			 * offsets, so we always must read all preceding entries
			 * in order to determine offsets.  */
			first_chunk_entry_to_read = 0;
			num_chunk_entries_to_read = last_needed_chunk + 1;
		} else {

			num_chunk_entries_to_read = last_needed_chunk - read_start_chunk + 1;

			/* The first chunk has no explicit chunk table entry.  */
			if (read_start_chunk == 0) {
				num_chunk_entries_to_read--;
				first_chunk_entry_to_read = 0;
			} else {
				first_chunk_entry_to_read = read_start_chunk - 1;
			}

			/* Unless we're reading the final chunk of the resource,
			 * we need the offset of the chunk following the last
			 * needed chunk so that the compressed size of the last
			 * needed chunk can be computed.  */
			if (last_needed_chunk < num_chunks - 1)
				num_chunk_entries_to_read++;
		}

		const u64 chunk_offsets_alloc_size =
			max(num_chunk_entries_to_read,
			    num_needed_chunk_offsets) * sizeof(chunk_offsets[0]);

		if (unlikely((size_t)chunk_offsets_alloc_size != chunk_offsets_alloc_size)) {
			errno = ENOMEM;
			goto oom;
		}

		if (likely(chunk_offsets_alloc_size <= STACK_MAX)) {
			chunk_offsets = alloca(chunk_offsets_alloc_size);
		} else {
			chunk_offsets = MALLOC(chunk_offsets_alloc_size);
			if (unlikely(!chunk_offsets))
				goto oom;
			chunk_offsets_malloced = true;
		}

		const size_t chunk_table_size_to_read =
			num_chunk_entries_to_read * chunk_entry_size;

		const u64 file_offset_of_needed_chunk_entries =
			cur_read_offset
			+ (first_chunk_entry_to_read * chunk_entry_size)
			+ (rdesc->is_pipable ? (rdesc->size_in_wim - chunk_table_size) : 0);

		void * const chunk_table_data =
			(u8*)chunk_offsets +
			chunk_offsets_alloc_size -
			chunk_table_size_to_read;

		ret = full_pread(in_fd, chunk_table_data, chunk_table_size_to_read,
				 file_offset_of_needed_chunk_entries);
		if (unlikely(ret))
			goto read_error;

		/* Now fill in chunk_offsets from the entries we have read in
		 * chunk_tab_data.  We break aliasing rules here to avoid having
		 * to allocate yet another array.  */
		typedef le64 __attribute__((may_alias)) aliased_le64_t;
		typedef le32 __attribute__((may_alias)) aliased_le32_t;
		u64 * chunk_offsets_p = chunk_offsets;

		if (alt_chunk_table) {
			u64 cur_offset = 0;
			aliased_le32_t *raw_entries = chunk_table_data;

			for (size_t i = 0; i < num_chunk_entries_to_read; i++) {
				u32 entry = le32_to_cpu(raw_entries[i]);
				if (i >= read_start_chunk)
					*chunk_offsets_p++ = cur_offset;
				cur_offset += entry;
			}
			if (last_needed_chunk < num_chunks - 1)
				*chunk_offsets_p = cur_offset;
		} else {
			if (read_start_chunk == 0)
				*chunk_offsets_p++ = 0;

			if (chunk_entry_size == 4) {
				aliased_le32_t *raw_entries = chunk_table_data;
				for (size_t i = 0; i < num_chunk_entries_to_read; i++)
					*chunk_offsets_p++ = le32_to_cpu(raw_entries[i]);
			} else {
				aliased_le64_t *raw_entries = chunk_table_data;
				for (size_t i = 0; i < num_chunk_entries_to_read; i++)
					*chunk_offsets_p++ = le64_to_cpu(raw_entries[i]);
			}
		}

		/* Set offset to beginning of first chunk to read.  */
		cur_read_offset += chunk_offsets[0];
		if (rdesc->is_pipable)
			cur_read_offset += read_start_chunk * sizeof(struct pwm_chunk_hdr);
		else
			cur_read_offset += chunk_table_size;
	}

	/* Allocate buffer for holding the uncompressed data of each chunk.  */
	if (chunk_size <= STACK_MAX) {
		ubuf = alloca(chunk_size);
	} else {
		ubuf = MALLOC(chunk_size);
		if (unlikely(!ubuf))
			goto oom;
		ubuf_malloced = true;
	}

	/* Allocate a temporary buffer for reading compressed chunks, each of
	 * which can be at most @chunk_size - 1 bytes.  This excludes compressed
	 * chunks that are a full @chunk_size bytes, which are actually stored
	 * uncompressed.  */
	if (chunk_size - 1 <= STACK_MAX) {
		cbuf = alloca(chunk_size - 1);
	} else {
		cbuf = MALLOC(chunk_size - 1);
		if (unlikely(!cbuf))
			goto oom;
		cbuf_malloced = true;
	}

	/* Set current data range.  */
	const struct data_range *cur_range = ranges;
	const struct data_range * const end_range = &ranges[num_ranges];
	u64 cur_range_pos = cur_range->offset;
	u64 cur_range_end = cur_range->offset + cur_range->size;

	/* Read and process each needed chunk.  */
	for (u64 i = read_start_chunk; i <= last_needed_chunk; i++) {

		/* Calculate uncompressed size of next chunk.  */
		u32 chunk_usize;
		if ((i == num_chunks - 1) && (rdesc->uncompressed_size & (chunk_size - 1)))
			chunk_usize = (rdesc->uncompressed_size & (chunk_size - 1));
		else
			chunk_usize = chunk_size;

		/* Calculate compressed size of next chunk.  */
		u32 chunk_csize;
		if (is_pipe_read) {
			struct pwm_chunk_hdr chunk_hdr;

			ret = full_pread(in_fd, &chunk_hdr,
					 sizeof(chunk_hdr), cur_read_offset);
			if (unlikely(ret))
				goto read_error;
			chunk_csize = le32_to_cpu(chunk_hdr.compressed_size);
		} else {
			if (i == num_chunks - 1) {
				chunk_csize = rdesc->size_in_wim -
					      chunk_table_full_size -
					      chunk_offsets[i - read_start_chunk];
				if (rdesc->is_pipable)
					chunk_csize -= num_chunks * sizeof(struct pwm_chunk_hdr);
			} else {
				chunk_csize = chunk_offsets[i + 1 - read_start_chunk] -
					      chunk_offsets[i - read_start_chunk];
			}
		}
		if (unlikely(chunk_csize == 0 || chunk_csize > chunk_usize)) {
			ERROR("Invalid chunk size in compressed resource!");
			errno = EINVAL;
			ret = WIMLIB_ERR_DECOMPRESSION;
			goto out_cleanup;
		}
		if (rdesc->is_pipable)
			cur_read_offset += sizeof(struct pwm_chunk_hdr);

		/* Offsets in the uncompressed resource at which this chunk
		 * starts and ends.  */
		const u64 chunk_start_offset = i << chunk_order;
		const u64 chunk_end_offset = chunk_start_offset + chunk_usize;

		if (chunk_end_offset <= cur_range_pos) {

			/* The next range does not require data in this chunk,
			 * so skip it.  */
			cur_read_offset += chunk_csize;
			if (is_pipe_read) {
				u8 dummy;

				ret = full_pread(in_fd, &dummy, 1, cur_read_offset - 1);
				if (unlikely(ret))
					goto read_error;
			}
		} else {

			/* Read the chunk and feed data to the callback
			 * function.  */
			u8 *read_buf;

			if (chunk_csize == chunk_usize)
				read_buf = ubuf;
			else
				read_buf = cbuf;

			ret = full_pread(in_fd,
					 read_buf,
					 chunk_csize,
					 cur_read_offset);
			if (unlikely(ret))
				goto read_error;

			if (read_buf == cbuf) {
				ret = decompress_chunk(cbuf, chunk_csize,
						       ubuf, chunk_usize,
						       decompressor,
						       recover_data);
				if (unlikely(ret))
					goto out_cleanup;
			}
			cur_read_offset += chunk_csize;

			/* At least one range requires data in this chunk.  */
			do {
				size_t start, end, size;

				/* Calculate how many bytes of data should be
				 * sent to the callback function, taking into
				 * account that data sent to the callback
				 * function must not overlap range boundaries.
				 */
				start = cur_range_pos - chunk_start_offset;
				end = min(cur_range_end, chunk_end_offset) - chunk_start_offset;
				size = end - start;

				ret = consume_chunk(cb, &ubuf[start], size);
				if (unlikely(ret))
					goto out_cleanup;

				cur_range_pos += size;
				if (cur_range_pos == cur_range_end) {
					/* Advance to next range.  */
					if (++cur_range == end_range) {
						cur_range_pos = ~0ULL;
					} else {
						cur_range_pos = cur_range->offset;
						cur_range_end = cur_range->offset + cur_range->size;
					}
				}
			} while (cur_range_pos < chunk_end_offset);
		}
	}

	if (is_pipe_read &&
	    last_offset == rdesc->uncompressed_size - 1 &&
	    chunk_table_size)
	{
		u8 dummy;
		/* If reading a pipable resource from a pipe and the full data
		 * was requested, skip the chunk table at the end so that the
		 * file descriptor is fully clear of the resource after this
		 * returns.  */
		cur_read_offset += chunk_table_size;
		ret = full_pread(in_fd, &dummy, 1, cur_read_offset - 1);
		if (unlikely(ret))
			goto read_error;
	}
	ret = 0;

out_cleanup:
	if (decompressor) {
		wimlib_free_decompressor(rdesc->wim->decompressor);
		rdesc->wim->decompressor = decompressor;
		rdesc->wim->decompressor_ctype = ctype;
		rdesc->wim->decompressor_max_block_size = chunk_size;
	}
	if (chunk_offsets_malloced)
		FREE(chunk_offsets);
	if (ubuf_malloced)
		FREE(ubuf);
	if (cbuf_malloced)
		FREE(cbuf);
	return ret;

oom:
	ERROR("Out of memory while reading compressed WIM resource");
	ret = WIMLIB_ERR_NOMEM;
	goto out_cleanup;

read_error:
	ERROR_WITH_ERRNO("Error reading data from WIM file");
	goto out_cleanup;
}

/* Read raw data from a file descriptor at the specified offset, feeding the
 * data in nonempty chunks into the specified callback function.  */
static int
read_raw_file_data(struct filedes *in_fd, u64 offset, u64 size,
		   const struct consume_chunk_callback *cb,
		   const tchar *filename)
{
	u8 buf[BUFFER_SIZE];
	size_t bytes_to_read;
	int ret;

	while (size) {
		bytes_to_read = min(sizeof(buf), size);
		ret = full_pread(in_fd, buf, bytes_to_read, offset);
		if (unlikely(ret))
			goto read_error;
		ret = consume_chunk(cb, buf, bytes_to_read);
		if (unlikely(ret))
			return ret;
		size -= bytes_to_read;
		offset += bytes_to_read;
	}
	return 0;

read_error:
	if (!filename) {
		ERROR_WITH_ERRNO("Error reading data from WIM file");
	} else if (ret == WIMLIB_ERR_UNEXPECTED_END_OF_FILE) {
		ERROR("\"%"TS"\": File was concurrently truncated", filename);
		ret = WIMLIB_ERR_CONCURRENT_MODIFICATION_DETECTED;
	} else {
		ERROR_WITH_ERRNO("\"%"TS"\": Error reading data", filename);
	}
	return ret;
}

/* A consume_chunk implementation which simply concatenates all chunks into an
 * in-memory buffer.  */
static int
bufferer_cb(const void *chunk, size_t size, void *_ctx)
{
	void **buf_p = _ctx;

	*buf_p = mempcpy(*buf_p, chunk, size);
	return 0;
}

/*
 * Read @size bytes at @offset in the WIM resource described by @rdesc and feed
 * the data into the @cb callback function.
 *
 * @offset and @size are assumed to have already been validated against the
 * resource's uncompressed size.
 *
 * Returns 0 on success; or the first nonzero value returned by the callback
 * function; or a nonzero wimlib error code with errno set as well.
 */
static int
read_partial_wim_resource(const struct wim_resource_descriptor *rdesc,
			  const u64 offset, const u64 size,
			  const struct consume_chunk_callback *cb,
			  bool recover_data)
{
	if (rdesc->flags & (WIM_RESHDR_FLAG_COMPRESSED |
			    WIM_RESHDR_FLAG_SOLID))
	{
		/* Compressed resource  */
		if (unlikely(!size))
			return 0;
		struct data_range range = {
			.offset = offset,
			.size = size,
		};
		return read_compressed_wim_resource(rdesc, &range, 1, cb,
						    recover_data);
	}

	/* Uncompressed resource  */
	return read_raw_file_data(&rdesc->wim->in_fd,
				  rdesc->offset_in_wim + offset,
				  size, cb, NULL);
}

/* Read the specified range of uncompressed data from the specified blob, which
 * must be located in a WIM file, into the specified buffer.  */
int
read_partial_wim_blob_into_buf(const struct blob_descriptor *blob,
			       u64 offset, size_t size, void *buf)
{
	struct consume_chunk_callback cb = {
		.func	= bufferer_cb,
		.ctx	= &buf,
	};
	return read_partial_wim_resource(blob->rdesc,
					 blob->offset_in_res + offset,
					 size,
					 &cb, false);
}

static int
noop_cb(const void *chunk, size_t size, void *_ctx)
{
	return 0;
}

/* Skip over the data of the specified WIM resource.  */
int
skip_wim_resource(const struct wim_resource_descriptor *rdesc)
{
	static const struct consume_chunk_callback cb = {
		.func = noop_cb,
	};
	return read_partial_wim_resource(rdesc, 0,
					 rdesc->uncompressed_size, &cb, false);
}

static int
read_wim_blob_prefix(const struct blob_descriptor *blob, u64 size,
		     const struct consume_chunk_callback *cb, bool recover_data)
{
	return read_partial_wim_resource(blob->rdesc, blob->offset_in_res,
					 size, cb, recover_data);
}

/* This function handles reading blob data that is located in an external file,
 * such as a file that has been added to the WIM image through execution of a
 * wimlib_add_command.
 *
 * This assumes the file can be accessed using the standard POSIX open(),
 * read(), and close().  On Windows this will not necessarily be the case (since
 * the file may need FILE_FLAG_BACKUP_SEMANTICS to be opened, or the file may be
 * encrypted), so Windows uses its own code for its equivalent case.  */
static int
read_file_on_disk_prefix(const struct blob_descriptor *blob, u64 size,
			 const struct consume_chunk_callback *cb,
			 bool recover_data)
{
	int ret;
	int raw_fd;
	struct filedes fd;

	raw_fd = topen(blob->file_on_disk, O_BINARY | O_RDONLY);
	if (unlikely(raw_fd < 0)) {
		ERROR_WITH_ERRNO("Can't open \"%"TS"\"", blob->file_on_disk);
		return WIMLIB_ERR_OPEN;
	}
	filedes_init(&fd, raw_fd);
	ret = read_raw_file_data(&fd, 0, size, cb, blob->file_on_disk);
	filedes_close(&fd);
	return ret;
}

#ifdef WITH_FUSE
static int
read_staging_file_prefix(const struct blob_descriptor *blob, u64 size,
			 const struct consume_chunk_callback *cb,
			 bool recover_data)
{
	int raw_fd;
	struct filedes fd;
	int ret;

	raw_fd = openat(blob->staging_dir_fd, blob->staging_file_name,
			O_RDONLY | O_NOFOLLOW);
	if (unlikely(raw_fd < 0)) {
		ERROR_WITH_ERRNO("Can't open staging file \"%s\"",
				 blob->staging_file_name);
		return WIMLIB_ERR_OPEN;
	}
	filedes_init(&fd, raw_fd);
	ret = read_raw_file_data(&fd, 0, size, cb, blob->staging_file_name);
	filedes_close(&fd);
	return ret;
}
#endif

/* This function handles the trivial case of reading blob data that is, in fact,
 * already located in an in-memory buffer.  */
static int
read_buffer_prefix(const struct blob_descriptor *blob,
		   u64 size, const struct consume_chunk_callback *cb,
		   bool recover_data)
{
	if (unlikely(!size))
		return 0;
	return consume_chunk(cb, blob->attached_buffer, size);
}

typedef int (*read_blob_prefix_handler_t)(const struct blob_descriptor *blob,
					  u64 size,
					  const struct consume_chunk_callback *cb,
					  bool recover_data);

/*
 * Read the first @size bytes from a generic "blob", which may be located in any
 * one of several locations, such as in a WIM resource (possibly compressed), in
 * an external file, or directly in an in-memory buffer.  The blob data will be
 * fed to @cb in chunks that are nonempty but otherwise are of unspecified size.
 *
 * Returns 0 on success; nonzero on error.  A nonzero value will be returned if
 * the blob data cannot be successfully read (for a number of different reasons,
 * depending on the blob location), or if @cb returned nonzero in which case
 * that error code will be returned.  If @recover_data is true, then errors
 * decompressing chunks in WIM resources will be ignored.
 */
static int
read_blob_prefix(const struct blob_descriptor *blob, u64 size,
		 const struct consume_chunk_callback *cb, bool recover_data)
{
	static const read_blob_prefix_handler_t handlers[] = {
		[BLOB_IN_WIM] = read_wim_blob_prefix,
		[BLOB_IN_FILE_ON_DISK] = read_file_on_disk_prefix,
		[BLOB_IN_ATTACHED_BUFFER] = read_buffer_prefix,
	#ifdef WITH_FUSE
		[BLOB_IN_STAGING_FILE] = read_staging_file_prefix,
	#endif
	#ifdef WITH_NTFS_3G
		[BLOB_IN_NTFS_VOLUME] = read_ntfs_attribute_prefix,
	#endif
	#ifdef _WIN32
		[BLOB_IN_WINDOWS_FILE] = read_windows_file_prefix,
	#endif
	};
	wimlib_assert(blob->blob_location < ARRAY_LEN(handlers)
		      && handlers[blob->blob_location] != NULL);
	wimlib_assert(size <= blob->size);
	return handlers[blob->blob_location](blob, size, cb, recover_data);
}

struct blob_chunk_ctx {
	const struct blob_descriptor *blob;
	const struct read_blob_callbacks *cbs;
	u64 offset;
};

static int
consume_blob_chunk(const void *chunk, size_t size, void *_ctx)
{
	struct blob_chunk_ctx *ctx = _ctx;
	int ret;

	ret = call_continue_blob(ctx->blob, ctx->offset, chunk, size, ctx->cbs);
	ctx->offset += size;
	return ret;
}

/* Read the full data of the specified blob, passing the data into the specified
 * callbacks (all of which are optional).  */
int
read_blob_with_cbs(struct blob_descriptor *blob,
		   const struct read_blob_callbacks *cbs, bool recover_data)
{
	int ret;
	struct blob_chunk_ctx ctx = {
		.blob = blob,
		.offset = 0,
		.cbs = cbs,
	};
	struct consume_chunk_callback cb = {
		.func = consume_blob_chunk,
		.ctx = &ctx,
	};

	ret = call_begin_blob(blob, cbs);
	if (unlikely(ret))
		return ret;

	ret = read_blob_prefix(blob, blob->size, &cb, recover_data);

	return call_end_blob(blob, ret, cbs);
}

/* Read the full uncompressed data of the specified blob into the specified
 * buffer, which must have space for at least blob->size bytes.  The SHA-1
 * message digest is *not* checked.  */
int
read_blob_into_buf(const struct blob_descriptor *blob, void *buf)
{
	struct consume_chunk_callback cb = {
		.func	= bufferer_cb,
		.ctx	= &buf,
	};
	return read_blob_prefix(blob, blob->size, &cb, false);
}

/* Retrieve the full uncompressed data of the specified blob.  A buffer large
 * enough hold the data is allocated and returned in @buf_ret.  The SHA-1
 * message digest is *not* checked.  */
int
read_blob_into_alloc_buf(const struct blob_descriptor *blob, void **buf_ret)
{
	int ret;
	void *buf;

	if (unlikely((size_t)blob->size != blob->size)) {
		ERROR("Can't read %"PRIu64" byte blob into memory", blob->size);
		return WIMLIB_ERR_NOMEM;
	}

	buf = MALLOC(blob->size);
	if (unlikely(!buf))
		return WIMLIB_ERR_NOMEM;

	ret = read_blob_into_buf(blob, buf);
	if (unlikely(ret)) {
		FREE(buf);
		return ret;
	}

	*buf_ret = buf;
	return 0;
}

/* Retrieve the full uncompressed data of a WIM resource specified as a raw
 * `wim_reshdr' and the corresponding WIM file.  A buffer large enough hold the
 * data is allocated and returned in @buf_ret.  */
int
wim_reshdr_to_data(const struct wim_reshdr *reshdr, WIMStruct *wim,
		   void **buf_ret)
{
	struct wim_resource_descriptor rdesc;
	struct blob_descriptor blob;

	wim_reshdr_to_desc_and_blob(reshdr, wim, &rdesc, &blob);

	return read_blob_into_alloc_buf(&blob, buf_ret);
}

/* Calculate the SHA-1 message digest of the uncompressed data of the specified
 * WIM resource.  */
int
wim_reshdr_to_hash(const struct wim_reshdr *reshdr, WIMStruct *wim,
		   u8 hash[SHA1_HASH_SIZE])
{
	struct wim_resource_descriptor rdesc;
	struct blob_descriptor blob;
	int ret;

	wim_reshdr_to_desc_and_blob(reshdr, wim, &rdesc, &blob);
	blob.unhashed = 1;

	ret = sha1_blob(&blob);
	if (unlikely(ret))
		return ret;

	copy_hash(hash, blob.hash);
	return 0;
}

struct blobifier_context {
	struct read_blob_callbacks cbs;
	struct blob_descriptor *cur_blob;
	struct blob_descriptor *next_blob;
	u64 cur_blob_offset;
	struct blob_descriptor *final_blob;
	size_t list_head_offset;
};

static struct blob_descriptor *
next_blob(struct blob_descriptor *blob, size_t list_head_offset)
{
	struct list_head *cur;

	cur = (struct list_head*)((u8*)blob + list_head_offset);

	return (struct blob_descriptor*)((u8*)cur->next - list_head_offset);
}

/*
 * A consume_chunk implementation that translates raw resource data into blobs,
 * calling the begin_blob, continue_blob, and end_blob callbacks as appropriate.
 */
static int
blobifier_cb(const void *chunk, size_t size, void *_ctx)
{
	struct blobifier_context *ctx = _ctx;
	int ret;

	wimlib_assert(ctx->cur_blob != NULL);
	wimlib_assert(size <= ctx->cur_blob->size - ctx->cur_blob_offset);

	if (ctx->cur_blob_offset == 0) {
		/* Starting a new blob.  */
		ret = call_begin_blob(ctx->cur_blob, &ctx->cbs);
		if (ret)
			return ret;
	}

	ret = call_continue_blob(ctx->cur_blob, ctx->cur_blob_offset,
				 chunk, size, &ctx->cbs);
	ctx->cur_blob_offset += size;
	if (ret)
		return ret;

	if (ctx->cur_blob_offset == ctx->cur_blob->size) {
		/* Finished reading all the data for a blob.  */

		ctx->cur_blob_offset = 0;

		ret = call_end_blob(ctx->cur_blob, 0, &ctx->cbs);
		if (ret)
			return ret;

		/* Advance to next blob.  */
		ctx->cur_blob = ctx->next_blob;
		if (ctx->cur_blob != NULL) {
			if (ctx->cur_blob != ctx->final_blob)
				ctx->next_blob = next_blob(ctx->cur_blob,
							   ctx->list_head_offset);
			else
				ctx->next_blob = NULL;
		}
	}
	return 0;
}

struct hasher_context {
	struct sha1_ctx sha_ctx;
	int flags;
	struct read_blob_callbacks cbs;
};

/* Callback for starting to read a blob while calculating its SHA-1 message
 * digest.  */
static int
hasher_begin_blob(struct blob_descriptor *blob, void *_ctx)
{
	struct hasher_context *ctx = _ctx;

	sha1_init(&ctx->sha_ctx);
	blob->corrupted = 0;

	return call_begin_blob(blob, &ctx->cbs);
}

/*
 * A continue_blob() implementation that continues calculating the SHA-1 message
 * digest of the blob being read, then optionally passes the data on to another
 * continue_blob() implementation.  This allows checking the SHA-1 message
 * digest of a blob being extracted, for example.
 */
static int
hasher_continue_blob(const struct blob_descriptor *blob, u64 offset,
		     const void *chunk, size_t size, void *_ctx)
{
	struct hasher_context *ctx = _ctx;

	sha1_update(&ctx->sha_ctx, chunk, size);

	return call_continue_blob(blob, offset, chunk, size, &ctx->cbs);
}

static int
report_sha1_mismatch(struct blob_descriptor *blob,
		     const u8 actual_hash[SHA1_HASH_SIZE], bool recover_data)
{
	tchar expected_hashstr[SHA1_HASH_STRING_LEN];
	tchar actual_hashstr[SHA1_HASH_STRING_LEN];

	wimlib_assert(blob->blob_location != BLOB_NONEXISTENT);
	wimlib_assert(blob->blob_location != BLOB_IN_ATTACHED_BUFFER);

	sprint_hash(blob->hash, expected_hashstr);
	sprint_hash(actual_hash, actual_hashstr);

	blob->corrupted = 1;

	if (blob_is_in_file(blob)) {
		ERROR("A file was concurrently modified!\n"
		      "        Path: \"%"TS"\"\n"
		      "        Expected SHA-1: %"TS"\n"
		      "        Actual SHA-1: %"TS"\n",
		      blob_file_path(blob), expected_hashstr, actual_hashstr);
		return WIMLIB_ERR_CONCURRENT_MODIFICATION_DETECTED;
	} else if (blob->blob_location == BLOB_IN_WIM) {
		const struct wim_resource_descriptor *rdesc = blob->rdesc;

		(recover_data ? wimlib_warning : wimlib_error)(
		      T("A WIM resource is corrupted!\n"
			"        WIM file: \"%"TS"\"\n"
			"        Blob uncompressed size: %"PRIu64"\n"
			"        Resource offset in WIM: %"PRIu64"\n"
			"        Resource uncompressed size: %"PRIu64"\n"
			"        Resource size in WIM: %"PRIu64"\n"
			"        Resource flags: 0x%x%"TS"\n"
			"        Resource compression type: %"TS"\n"
			"        Resource compression chunk size: %"PRIu32"\n"
			"        Expected SHA-1: %"TS"\n"
			"        Actual SHA-1: %"TS"\n"),
		      rdesc->wim->filename,
		      blob->size,
		      rdesc->offset_in_wim,
		      rdesc->uncompressed_size,
		      rdesc->size_in_wim,
		      (unsigned int)rdesc->flags,
		      (rdesc->is_pipable ? T(", pipable") : T("")),
		      wimlib_get_compression_type_string(
						rdesc->compression_type),
		      rdesc->chunk_size,
		      expected_hashstr, actual_hashstr);
		if (recover_data)
			return 0;
		return WIMLIB_ERR_INVALID_RESOURCE_HASH;
	} else {
		ERROR("File data was concurrently modified!\n"
		      "        Location ID: %d\n"
		      "        Expected SHA-1: %"TS"\n"
		      "        Actual SHA-1: %"TS"\n",
		      (int)blob->blob_location,
		      expected_hashstr, actual_hashstr);
		return WIMLIB_ERR_CONCURRENT_MODIFICATION_DETECTED;
	}
}

/* Callback for finishing reading a blob while calculating its SHA-1 message
 * digest.  */
static int
hasher_end_blob(struct blob_descriptor *blob, int status, void *_ctx)
{
	struct hasher_context *ctx = _ctx;
	u8 hash[SHA1_HASH_SIZE];
	int ret;

	if (unlikely(status)) {
		/* Error occurred; the full blob may not have been read.  */
		ret = status;
		goto out_next_cb;
	}

	/* Retrieve the final SHA-1 message digest.  */
	sha1_final(&ctx->sha_ctx, hash);

	/* Set the SHA-1 message digest of the blob, or compare the calculated
	 * value with stored value.  */
	if (blob->unhashed) {
		if (ctx->flags & COMPUTE_MISSING_BLOB_HASHES)
			copy_hash(blob->hash, hash);
	} else if ((ctx->flags & VERIFY_BLOB_HASHES) &&
		   unlikely(!hashes_equal(hash, blob->hash)))
	{
		ret = report_sha1_mismatch(blob, hash,
					   ctx->flags & RECOVER_DATA);
		goto out_next_cb;
	}
	ret = 0;
out_next_cb:
	return call_end_blob(blob, ret, &ctx->cbs);
}

/* Read the full data of the specified blob, passing the data into the specified
 * callbacks (all of which are optional) and either checking or computing the
 * SHA-1 message digest of the blob.  */
int
read_blob_with_sha1(struct blob_descriptor *blob,
		    const struct read_blob_callbacks *cbs, bool recover_data)
{
	struct hasher_context hasher_ctx = {
		.flags = VERIFY_BLOB_HASHES | COMPUTE_MISSING_BLOB_HASHES |
			 (recover_data ? RECOVER_DATA : 0),
		.cbs = *cbs,
	};
	struct read_blob_callbacks hasher_cbs = {
		.begin_blob	= hasher_begin_blob,
		.continue_blob	= hasher_continue_blob,
		.end_blob	= hasher_end_blob,
		.ctx		= &hasher_ctx,
	};
	return read_blob_with_cbs(blob, &hasher_cbs, recover_data);
}

static int
read_blobs_in_solid_resource(struct blob_descriptor *first_blob,
			     struct blob_descriptor *last_blob,
			     size_t blob_count,
			     size_t list_head_offset,
			     const struct read_blob_callbacks *sink_cbs,
			     bool recover_data)
{
	struct data_range *ranges;
	bool ranges_malloced;
	struct blob_descriptor *cur_blob;
	size_t i;
	int ret;
	u64 ranges_alloc_size;

	/* Setup data ranges array (one range per blob to read); this way
	 * read_compressed_wim_resource() does not need to be aware of blobs.
	 */

	ranges_alloc_size = (u64)blob_count * sizeof(ranges[0]);

	if (unlikely((size_t)ranges_alloc_size != ranges_alloc_size))
		goto oom;

	if (ranges_alloc_size <= STACK_MAX) {
		ranges = alloca(ranges_alloc_size);
		ranges_malloced = false;
	} else {
		ranges = MALLOC(ranges_alloc_size);
		if (unlikely(!ranges))
			goto oom;
		ranges_malloced = true;
	}

	for (i = 0, cur_blob = first_blob;
	     i < blob_count;
	     i++, cur_blob = next_blob(cur_blob, list_head_offset))
	{
		ranges[i].offset = cur_blob->offset_in_res;
		ranges[i].size = cur_blob->size;
	}

	struct blobifier_context blobifier_ctx = {
		.cbs			= *sink_cbs,
		.cur_blob		= first_blob,
		.next_blob		= next_blob(first_blob, list_head_offset),
		.cur_blob_offset	= 0,
		.final_blob		= last_blob,
		.list_head_offset	= list_head_offset,
	};
	struct consume_chunk_callback cb = {
		.func	= blobifier_cb,
		.ctx	= &blobifier_ctx,
	};

	ret = read_compressed_wim_resource(first_blob->rdesc, ranges,
					   blob_count, &cb, recover_data);

	if (ranges_malloced)
		FREE(ranges);

	if (unlikely(ret && blobifier_ctx.cur_blob_offset != 0)) {
		ret = call_end_blob(blobifier_ctx.cur_blob, ret,
				    &blobifier_ctx.cbs);
	}
	return ret;

oom:
	ERROR("Too many blobs in one resource!");
	return WIMLIB_ERR_NOMEM;
}

/*
 * Read a list of blobs, each of which may be in any supported location (e.g.
 * in a WIM or in an external file).  This function optimizes the case where
 * multiple blobs are combined into a single solid compressed WIM resource by
 * reading the blobs in sequential order, only decompressing the solid resource
 * one time.
 *
 * @blob_list
 *	List of blobs to read.
 * @list_head_offset
 *	Offset of the `struct list_head' within each `struct blob_descriptor'
 *	that makes up the @blob_list.
 * @cbs
 *	Callback functions to accept the blob data.
 * @flags
 *	Bitwise OR of zero or more of the following flags:
 *
 *	VERIFY_BLOB_HASHES:
 *		For all blobs being read that have already had SHA-1 message
 *		digests computed, calculate the SHA-1 message digest of the read
 *		data and compare it with the previously computed value.  If they
 *		do not match, return WIMLIB_ERR_INVALID_RESOURCE_HASH (unless
 *		RECOVER_DATA is also set, in which case just issue a warning).
 *
 *	COMPUTE_MISSING_BLOB_HASHES
 *		For all blobs being read that have not yet had their SHA-1
 *		message digests computed, calculate and save their SHA-1 message
 *		digests.
 *
 *	BLOB_LIST_ALREADY_SORTED
 *		@blob_list is already sorted in sequential order for reading.
 *
 *	RECOVER_DATA
 *		Don't consider corrupted blob data to be an error.
 *
 * The callback functions are allowed to delete the current blob from the list
 * if necessary.
 *
 * Returns 0 on success; a nonzero error code on failure.  Failure can occur due
 * to an error reading the data or due to an error status being returned by any
 * of the callback functions.
 */
int
read_blob_list(struct list_head *blob_list, size_t list_head_offset,
	       const struct read_blob_callbacks *cbs, int flags)
{
	int ret;
	struct list_head *cur, *next;
	struct blob_descriptor *blob;
	struct hasher_context *hasher_ctx;
	struct read_blob_callbacks *sink_cbs;

	if (!(flags & BLOB_LIST_ALREADY_SORTED)) {
		ret = sort_blob_list_by_sequential_order(blob_list,
							 list_head_offset);
		if (ret)
			return ret;
	}

	if (flags & (VERIFY_BLOB_HASHES | COMPUTE_MISSING_BLOB_HASHES)) {
		hasher_ctx = alloca(sizeof(*hasher_ctx));
		*hasher_ctx = (struct hasher_context) {
			.flags	= flags,
			.cbs	= *cbs,
		};
		sink_cbs = alloca(sizeof(*sink_cbs));
		*sink_cbs = (struct read_blob_callbacks) {
			.begin_blob	= hasher_begin_blob,
			.continue_blob	= hasher_continue_blob,
			.end_blob	= hasher_end_blob,
			.ctx		= hasher_ctx,
		};
	} else {
		sink_cbs = (struct read_blob_callbacks *)cbs;
	}

	for (cur = blob_list->next, next = cur->next;
	     cur != blob_list;
	     cur = next, next = cur->next)
	{
		blob = (struct blob_descriptor*)((u8*)cur - list_head_offset);

		if (blob->blob_location == BLOB_IN_WIM &&
		    blob->size != blob->rdesc->uncompressed_size)
		{
			struct blob_descriptor *blob_next, *blob_last;
			struct list_head *next2;
			size_t blob_count;

			/* The next blob is a proper sub-sequence of a WIM
			 * resource.  See if there are other blobs in the same
			 * resource that need to be read.  Since
			 * sort_blob_list_by_sequential_order() sorted the blobs
			 * by offset in the WIM, this can be determined by
			 * simply scanning forward in the list.  */

			blob_last = blob;
			blob_count = 1;
			for (next2 = next;
			     next2 != blob_list
			     && (blob_next = (struct blob_descriptor*)
						((u8*)next2 - list_head_offset),
				 blob_next->blob_location == BLOB_IN_WIM
				 && blob_next->rdesc == blob->rdesc);
			     next2 = next2->next)
			{
				blob_last = blob_next;
				blob_count++;
			}
			if (blob_count > 1) {
				/* Reading multiple blobs combined into a single
				 * WIM resource.  They are in the blob list,
				 * sorted by offset; @blob specifies the first
				 * blob in the resource that needs to be read
				 * and @blob_last specifies the last blob in the
				 * resource that needs to be read.  */
				next = next2;
				ret = read_blobs_in_solid_resource(blob, blob_last,
								   blob_count,
								   list_head_offset,
								   sink_cbs,
								   flags & RECOVER_DATA);
				if (ret)
					return ret;
				continue;
			}
		}

		ret = read_blob_with_cbs(blob, sink_cbs, flags & RECOVER_DATA);
		if (unlikely(ret && ret != BEGIN_BLOB_STATUS_SKIP_BLOB))
			return ret;
	}
	return 0;
}

static int
extract_chunk_to_fd(const void *chunk, size_t size, void *_fd)
{
	struct filedes *fd = _fd;
	int ret = full_write(fd, chunk, size);
	if (unlikely(ret))
		ERROR_WITH_ERRNO("Error writing to file descriptor");
	return ret;
}

static int
extract_blob_chunk_to_fd(const struct blob_descriptor *blob, u64 offset,
			 const void *chunk, size_t size, void *_fd)
{
	return extract_chunk_to_fd(chunk, size, _fd);
}

/* Extract the first @size bytes of the specified blob to the specified file
 * descriptor.  This does *not* check the SHA-1 message digest.  */
int
extract_blob_prefix_to_fd(struct blob_descriptor *blob, u64 size,
			  struct filedes *fd)
{
	struct consume_chunk_callback cb = {
		.func	= extract_chunk_to_fd,
		.ctx	= fd,
	};
	return read_blob_prefix(blob, size, &cb, false);
}

/* Extract the full uncompressed contents of the specified blob to the specified
 * file descriptor.  This checks the SHA-1 message digest.  */
int
extract_blob_to_fd(struct blob_descriptor *blob, struct filedes *fd,
		   bool recover_data)
{
	struct read_blob_callbacks cbs = {
		.continue_blob	= extract_blob_chunk_to_fd,
		.ctx		= fd,
	};
	return read_blob_with_sha1(blob, &cbs, recover_data);
}

/* Calculate the SHA-1 message digest of a blob and store it in @blob->hash.  */
int
sha1_blob(struct blob_descriptor *blob)
{
	static const struct read_blob_callbacks cbs = { 0 };
	return read_blob_with_sha1(blob, &cbs, false);
}

/*
 * Convert a short WIM resource header to a stand-alone WIM resource descriptor.
 *
 * Note: for solid resources some fields still need to be overridden.
 */
void
wim_reshdr_to_desc(const struct wim_reshdr *reshdr, WIMStruct *wim,
		   struct wim_resource_descriptor *rdesc)
{
	rdesc->wim = wim;
	rdesc->offset_in_wim = reshdr->offset_in_wim;
	rdesc->size_in_wim = reshdr->size_in_wim;
	rdesc->uncompressed_size = reshdr->uncompressed_size;
	INIT_LIST_HEAD(&rdesc->blob_list);
	rdesc->flags = reshdr->flags;
	rdesc->is_pipable = wim_is_pipable(wim);
	if (rdesc->flags & WIM_RESHDR_FLAG_COMPRESSED) {
		rdesc->compression_type = wim->compression_type;
		rdesc->chunk_size = wim->chunk_size;
	} else {
		rdesc->compression_type = WIMLIB_COMPRESSION_TYPE_NONE;
		rdesc->chunk_size = 0;
	}
}

/*
 * Convert the short WIM resource header @reshdr to a stand-alone WIM resource
 * descriptor @rdesc, then set @blob to consist of that entire resource.  This
 * should only be used for non-solid resources!
 */
void
wim_reshdr_to_desc_and_blob(const struct wim_reshdr *reshdr, WIMStruct *wim,
			    struct wim_resource_descriptor *rdesc,
			    struct blob_descriptor *blob)
{
	wim_reshdr_to_desc(reshdr, wim, rdesc);
	blob->size = rdesc->uncompressed_size;
	blob_set_is_located_in_wim_resource(blob, rdesc, 0);
}

/* Import a WIM resource header from the on-disk format.  */
void
get_wim_reshdr(const struct wim_reshdr_disk *disk_reshdr,
	       struct wim_reshdr *reshdr)
{
	reshdr->offset_in_wim = le64_to_cpu(disk_reshdr->offset_in_wim);
	reshdr->size_in_wim = (((u64)disk_reshdr->size_in_wim[0] <<  0) |
			       ((u64)disk_reshdr->size_in_wim[1] <<  8) |
			       ((u64)disk_reshdr->size_in_wim[2] << 16) |
			       ((u64)disk_reshdr->size_in_wim[3] << 24) |
			       ((u64)disk_reshdr->size_in_wim[4] << 32) |
			       ((u64)disk_reshdr->size_in_wim[5] << 40) |
			       ((u64)disk_reshdr->size_in_wim[6] << 48));
	reshdr->uncompressed_size = le64_to_cpu(disk_reshdr->uncompressed_size);
	reshdr->flags = disk_reshdr->flags;
}

/* Export a WIM resource header to the on-disk format.  */
void
put_wim_reshdr(const struct wim_reshdr *reshdr,
	       struct wim_reshdr_disk *disk_reshdr)
{
	disk_reshdr->size_in_wim[0] = reshdr->size_in_wim  >>  0;
	disk_reshdr->size_in_wim[1] = reshdr->size_in_wim  >>  8;
	disk_reshdr->size_in_wim[2] = reshdr->size_in_wim  >> 16;
	disk_reshdr->size_in_wim[3] = reshdr->size_in_wim  >> 24;
	disk_reshdr->size_in_wim[4] = reshdr->size_in_wim  >> 32;
	disk_reshdr->size_in_wim[5] = reshdr->size_in_wim  >> 40;
	disk_reshdr->size_in_wim[6] = reshdr->size_in_wim  >> 48;
	disk_reshdr->flags = reshdr->flags;
	disk_reshdr->offset_in_wim = cpu_to_le64(reshdr->offset_in_wim);
	disk_reshdr->uncompressed_size = cpu_to_le64(reshdr->uncompressed_size);
}
