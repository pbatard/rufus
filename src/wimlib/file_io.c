/*
 * file_io.c - Helper functions for reading and writing to file descriptors.
 */

/*
 * Copyright (C) 2013 Eric Biggers
 * Copyright (C) 2025 Pete Batard <pete@akeo.ie>
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
#include <unistd.h>

#include "wimlib/error.h"
#include "wimlib/file_io.h"
#include "wimlib/util.h"

#ifdef _WIN32
#  include "wimlib/win32.h"
#  define read win32_read
#  define write win32_write
#  define pread win32_pread
#  define pwrite win32_pwrite
#endif

#ifdef WITH_LIBCDIO
static int
udf_pread(struct filedes* fd, void* buf, size_t count, off_t offset)
{
	ssize_t ret;
	size_t partial_size;
	uint64_t file_length;
	char _buf[UDF_BLOCKSIZE];

	if (count == 0)
		return 0;

	if (!udf_setpos(fd->p_udf_file, (offset / UDF_BLOCKSIZE) * UDF_BLOCKSIZE)) {
		errno = ERANGE;
		return WIMLIB_ERR_READ;
	}
	fd->offset = (offset / UDF_BLOCKSIZE) * UDF_BLOCKSIZE;

	file_length = udf_get_file_length(fd->p_udf_file);
	if (offset + count > file_length)
		count = file_length - offset;

	if (offset % UDF_BLOCKSIZE) {
		partial_size = min(UDF_BLOCKSIZE - offset % UDF_BLOCKSIZE, count);
		ret = udf_read_block(fd->p_udf_file, _buf, 1);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		memcpy(buf, &_buf[offset % UDF_BLOCKSIZE], partial_size);
		buf = _PTR(buf + partial_size);
		fd->offset += partial_size;
		count -= partial_size;
	}

	while (count >= UDF_BLOCKSIZE) {
		ret = udf_read_block(fd->p_udf_file, buf, count / UDF_BLOCKSIZE);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		buf = _PTR(buf + ret);
		fd->offset += ret;
		count -= ret;
	}

	partial_size = count % UDF_BLOCKSIZE;
	if (partial_size) {
		ret = udf_read_block(fd->p_udf_file, _buf, 1);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		memcpy(buf, _buf, partial_size);
		buf = _PTR(buf + partial_size);
		fd->offset += partial_size;
	}

	return 0;
}

static int
iso_pread(struct filedes* fd, void* buf, size_t count, off_t offset)
{
	ssize_t ret;
	size_t partial_size;
	char _buf[ISO_BLOCKSIZE];
	lsn_t lsn_offset = fd->p_iso_file->lsn + offset / ISO_BLOCKSIZE;

	if (count == 0)
		return 0;

	if (offset >= fd->p_iso_file->total_size) {
		errno = ERANGE;
		return WIMLIB_ERR_READ;
	}

	if (offset + count > fd->p_iso_file->total_size)
		count = fd->p_iso_file->total_size - offset;

	if (offset % ISO_BLOCKSIZE) {
		partial_size = min(ISO_BLOCKSIZE - offset % ISO_BLOCKSIZE, count);
		ret = iso9660_iso_seek_read(fd->p_iso, _buf, lsn_offset, 1);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		lsn_offset += 1;
		memcpy(buf, &_buf[offset % ISO_BLOCKSIZE], partial_size);
		buf = _PTR(buf + partial_size);
		fd->offset += partial_size;
		count -= partial_size;
	}

	while (count >= ISO_BLOCKSIZE) {
		ret = iso9660_iso_seek_read(fd->p_iso, _buf,
			lsn_offset % ISO_BLOCKSIZE, 1);
		ret = iso9660_iso_seek_read(fd->p_iso, buf, lsn_offset, count / ISO_BLOCKSIZE);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		lsn_offset += ret / ISO_BLOCKSIZE;
		buf = _PTR(buf + ret);
		fd->offset += ret;
		count -= ret;
	}

	partial_size = count % ISO_BLOCKSIZE;
	if (partial_size) {
		ret = iso9660_iso_seek_read(fd->p_iso, _buf, lsn_offset, 1);
		if (unlikely(ret <= 0)) {
			errno = EINVAL;
			return WIMLIB_ERR_READ;
		}
		memcpy(buf, _buf, partial_size);
		buf = _PTR(buf + partial_size);
		fd->offset += partial_size;
	}

	return 0;
}
#endif

/*
 * Wrapper around read() that checks for errors and keeps retrying until all
 * requested bytes have been read or until end-of file has occurred.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS			(0)
 *	WIMLIB_ERR_READ				(errno set)
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE	(errno set to EINVAL)
 */
int
full_read(struct filedes *fd, void *buf, size_t count)
{
#ifdef WITH_LIBCDIO
	if (fd->is_udf)
		return udf_pread(fd, buf, count, 0);
	else if (fd->is_iso)
		return iso_pread(fd, buf, count, 0);
#endif

	while (count) {
		ssize_t ret = read(fd->fd, buf, count);
		if (unlikely(ret <= 0)) {
			if (ret == 0) {
				errno = EINVAL;
				return WIMLIB_ERR_UNEXPECTED_END_OF_FILE;
			}
			if (errno == EINTR)
				continue;
			return WIMLIB_ERR_READ;
		}
		buf = _PTR(buf + ret);
		count -= ret;
		fd->offset += ret;
	}
	return 0;
}

static int
pipe_read(struct filedes *fd, void *buf, size_t count, off_t offset)
{
	int ret;

	/* Verify the offset.  */
	if (offset < fd->offset) {
		ERROR("Can't seek backwards in pipe "
		      "(offset %"PRIu64" => %"PRIu64").\n"
		      "        Make sure the WIM was captured as "
		      "pipable.", fd->offset, offset);
		errno = ESPIPE;
		return WIMLIB_ERR_RESOURCE_ORDER;
	}

	/* Manually seek to the requested position.  */
	while (fd->offset != offset) {
		size_t bytes_to_read = min(offset - fd->offset, BUFFER_SIZE);
		u8* dummy = MALLOC(bytes_to_read);

		ret = full_read(fd, dummy, bytes_to_read);
		FREE(dummy);
		if (ret)
			return ret;
	}

	/* Do the actual read.  */
	return full_read(fd, buf, count);
}

/*
 * Wrapper around pread() that checks for errors and keeps retrying until all
 * requested bytes have been read or until end-of file has occurred.  This also
 * transparently handle reading from pipe files, but the caller needs to be sure
 * the requested offset is greater than or equal to the current offset, or else
 * WIMLIB_ERR_RESOURCE_ORDER will be returned.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS			(0)
 *	WIMLIB_ERR_READ				(errno set)
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE	(errno set to EINVAL)
 *	WIMLIB_ERR_RESOURCE_ORDER		(errno set to ESPIPE)
 */
int
full_pread(struct filedes *fd, void *buf, size_t count, off_t offset)
{
	if (fd->is_pipe)
		goto is_pipe;

#ifdef WITH_LIBCDIO
	if (fd->is_udf)
		return udf_pread(fd, buf, count, offset);
	else if (fd->is_iso)
		return iso_pread(fd, buf, count, offset);
#endif

	while (count) {
		ssize_t ret = pread(fd->fd, buf, count, offset);
		if (unlikely(ret <= 0)) {
			if (ret == 0) {
				errno = EINVAL;
				return WIMLIB_ERR_UNEXPECTED_END_OF_FILE;
			}
			if (errno == EINTR)
				continue;
			if (errno == ESPIPE) {
				fd->is_pipe = 1;
				goto is_pipe;
			}
			return WIMLIB_ERR_READ;
		}
		buf = _PTR(buf + ret);
		count -= ret;
		offset += ret;
	}
	return 0;

is_pipe:
	return pipe_read(fd, buf, count, offset);
}

/*
 * Wrapper around write() that checks for errors and keeps retrying until all
 * requested bytes have been written.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS			(0)
 *	WIMLIB_ERR_WRITE			(errno set)
 */
int
full_write(struct filedes *fd, const void *buf, size_t count)
{
	while (count) {
		ssize_t ret = write(fd->fd, buf, count);
		if (unlikely(ret < 0)) {
			if (errno == EINTR)
				continue;
			return WIMLIB_ERR_WRITE;
		}
		buf = _PTR(buf + ret);
		count -= ret;
		fd->offset += ret;
	}
	return 0;
}


/*
 * Wrapper around pwrite() that checks for errors and keeps retrying until all
 * requested bytes have been written.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS	(0)
 *	WIMLIB_ERR_WRITE	(errno set)
 */
int
full_pwrite(struct filedes *fd, const void *buf, size_t count, off_t offset)
{
	while (count) {
		ssize_t ret = pwrite(fd->fd, buf, count, offset);
		if (unlikely(ret < 0)) {
			if (errno == EINTR)
				continue;
			return WIMLIB_ERR_WRITE;
		}
		buf = _PTR(buf + ret);
		count -= ret;
		offset += ret;
	}
	return 0;
}

off_t filedes_seek(struct filedes *fd, off_t offset)
{
#ifdef WITH_LIBCDIO
	/* No arbitrary seek for ISO files */
	if (fd->is_udf || fd->is_iso) {
		errno = ENFILE;
		return -1;
	}
#endif
	if (fd->is_pipe) {
		errno = ESPIPE;
		return -1;
	}
	if (fd->offset != offset) {
		if (_lseeki64(fd->fd, offset, SEEK_SET) == -1)
			return -1;
		fd->offset = offset;
	}
	return offset;
}

bool filedes_is_seekable(struct filedes *fd)
{
#ifdef WITH_LIBCDIO
	/* No arbitrary seek for ISO files */
	if (fd->is_udf || fd->is_iso)
		return false;
#endif
	return !fd->is_pipe && _lseeki64(fd->fd, 0, SEEK_CUR) != -1;
}
