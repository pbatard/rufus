/*
 * extract.c
 *
 * Support for extracting WIM images, or files or directories contained in a WIM
 * image.
 */

/*
 * Copyright (C) 2012-2018 Eric Biggers
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

/*
 * This file provides the API functions wimlib_extract_image(),
 * wimlib_extract_image_from_pipe(), wimlib_extract_paths(), and
 * wimlib_extract_pathlist().  Internally, all end up calling
 * do_wimlib_extract_paths() and extract_trees().
 *
 * Although wimlib supports multiple extraction modes/backends (NTFS-3G, UNIX,
 * Win32), this file does not itself have code to extract files or directories
 * to any specific target; instead, it handles generic functionality and relies
 * on lower-level callback functions declared in `struct apply_operations' to do
 * the actual extraction.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wimlib/apply.h"
#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/metadata.h"
#include "wimlib/object_id.h"
#include "wimlib/pathlist.h"
#include "wimlib/paths.h"
#include "wimlib/pattern.h"
#include "wimlib/reparse.h"
#include "wimlib/resource.h"
#include "wimlib/security.h"
#include "wimlib/unix_data.h"
#include "wimlib/wim.h"
#include "wimlib/win32.h" /* for realpath() equivalent */
#include "wimlib/xattr.h"
#include "wimlib/xml.h"

#define WIMLIB_EXTRACT_FLAG_FROM_PIPE   0x80000000
#define WIMLIB_EXTRACT_FLAG_IMAGEMODE   0x40000000

/* Keep in sync with wimlib.h  */
#define WIMLIB_EXTRACT_MASK_PUBLIC				\
	(WIMLIB_EXTRACT_FLAG_NTFS			|	\
	 WIMLIB_EXTRACT_FLAG_RECOVER_DATA		|	\
	 WIMLIB_EXTRACT_FLAG_UNIX_DATA			|	\
	 WIMLIB_EXTRACT_FLAG_NO_ACLS			|	\
	 WIMLIB_EXTRACT_FLAG_STRICT_ACLS		|	\
	 WIMLIB_EXTRACT_FLAG_RPFIX			|	\
	 WIMLIB_EXTRACT_FLAG_NORPFIX			|	\
	 WIMLIB_EXTRACT_FLAG_TO_STDOUT			|	\
	 WIMLIB_EXTRACT_FLAG_REPLACE_INVALID_FILENAMES	|	\
	 WIMLIB_EXTRACT_FLAG_ALL_CASE_CONFLICTS		|	\
	 WIMLIB_EXTRACT_FLAG_STRICT_TIMESTAMPS		|	\
	 WIMLIB_EXTRACT_FLAG_STRICT_SHORT_NAMES		|	\
	 WIMLIB_EXTRACT_FLAG_STRICT_SYMLINKS		|	\
	 WIMLIB_EXTRACT_FLAG_GLOB_PATHS			|	\
	 WIMLIB_EXTRACT_FLAG_STRICT_GLOB		|	\
	 WIMLIB_EXTRACT_FLAG_NO_ATTRIBUTES		|	\
	 WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE  |	\
	 WIMLIB_EXTRACT_FLAG_WIMBOOT			|	\
	 WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K		|	\
	 WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS8K		|	\
	 WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS16K		|	\
	 WIMLIB_EXTRACT_FLAG_COMPACT_LZX			\
	 )

/* Send WIMLIB_PROGRESS_MSG_EXTRACT_FILE_STRUCTURE or
 * WIMLIB_PROGRESS_MSG_EXTRACT_METADATA.  */
int
do_file_extract_progress(struct apply_ctx *ctx, enum wimlib_progress_msg msg)
{
	ctx->count_until_file_progress = 500;  /* Arbitrary value to limit calls  */
	return extract_progress(ctx, msg);
}

static int
start_file_phase(struct apply_ctx *ctx, u64 end_file_count, enum wimlib_progress_msg msg)
{
	ctx->progress.extract.current_file_count = 0;
	ctx->progress.extract.end_file_count = end_file_count;
	return do_file_extract_progress(ctx, msg);
}

int
start_file_structure_phase(struct apply_ctx *ctx, u64 end_file_count)
{
	return start_file_phase(ctx, end_file_count, WIMLIB_PROGRESS_MSG_EXTRACT_FILE_STRUCTURE);
}

int
start_file_metadata_phase(struct apply_ctx *ctx, u64 end_file_count)
{
	return start_file_phase(ctx, end_file_count, WIMLIB_PROGRESS_MSG_EXTRACT_METADATA);
}

static int
end_file_phase(struct apply_ctx *ctx, enum wimlib_progress_msg msg)
{
	ctx->progress.extract.current_file_count = ctx->progress.extract.end_file_count;
	return do_file_extract_progress(ctx, msg);
}

int
end_file_structure_phase(struct apply_ctx *ctx)
{
	return end_file_phase(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_FILE_STRUCTURE);
}

int
end_file_metadata_phase(struct apply_ctx *ctx)
{
	return end_file_phase(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_METADATA);
}

/* Are all bytes in the specified buffer zero? */
static bool
is_all_zeroes(const u8 *buf, const size_t size)
{
	uintptr_t p = (uintptr_t)buf;
	const uintptr_t end = p + size;

	for (; p % WORDBYTES && p != end; p++)
		if (*((const u8 *)p))
			return false;

	for (; end - p >= WORDBYTES; p += WORDBYTES)
		if (*(const machine_word_t *)p)
			return false;

	for (; p != end; p++)
		if (*((const u8*)p))
			return false;

	return true;
}

/*
 * Sparse regions should be detected at the granularity of the filesystem block
 * size.  For now just assume 4096 bytes, which is the default block size on
 * NTFS and most Linux filesystems.
 */
#define SPARSE_UNIT 4096

/*
 * Detect whether the specified buffer begins with a region of all zero bytes.
 * Return %true if a zero region was found or %false if a nonzero region was
 * found, and sets *len_ret to the length of the region.  This operates at a
 * granularity of SPARSE_UNIT bytes, meaning that to extend a zero region, there
 * must be SPARSE_UNIT zero bytes with no interruption, but to extend a nonzero
 * region, just one nonzero byte in the next SPARSE_UNIT bytes is sufficient.
 *
 * Note: besides compression, the WIM format doesn't yet have a way to
 * efficiently represent zero regions, so that's why we need to detect them
 * ourselves.  Things will still fall apart badly on extremely large sparse
 * files, but this is a start...
 */
bool
detect_sparse_region(const void *data, size_t size, size_t *len_ret)
{
	uintptr_t p = (uintptr_t)data;
	const uintptr_t end = p + size;
	size_t len = 0;
	bool zeroes = false;

	while (p != end) {
		size_t n = min(end - p, SPARSE_UNIT);
		bool z = is_all_zeroes((const u8 *)p, n);

		if (len != 0 && z != zeroes)
			break;
		zeroes = z;
		len += n;
		p += n;
	}

	*len_ret = len;
	return zeroes;
}

#define PWM_FOUND_WIM_HDR (-1)

/* Read the header for a blob in a pipable WIM.  If @pwm_hdr_ret is not NULL,
 * also look for a pipable WIM header and return PWM_FOUND_WIM_HDR if found.  */
static int
read_pwm_blob_header(WIMStruct *pwm, u8 hash_ret[SHA1_HASH_SIZE],
		     struct wim_reshdr *reshdr_ret,
		     struct wim_header_disk *pwm_hdr_ret)
{
	int ret;
	struct pwm_blob_hdr blob_hdr;
	u64 magic;

	ret = full_read(&pwm->in_fd, &blob_hdr, sizeof(blob_hdr));
	if (unlikely(ret))
		goto read_error;

	magic = le64_to_cpu(blob_hdr.magic);

	if (magic == PWM_MAGIC && pwm_hdr_ret != NULL) {
		memcpy(pwm_hdr_ret, &blob_hdr, sizeof(blob_hdr));
		ret = full_read(&pwm->in_fd,
				(u8 *)pwm_hdr_ret + sizeof(blob_hdr),
				sizeof(*pwm_hdr_ret) - sizeof(blob_hdr));
		if (unlikely(ret))
			goto read_error;
		return PWM_FOUND_WIM_HDR;
	}

	if (unlikely(magic != PWM_BLOB_MAGIC)) {
		ERROR("Data read on pipe is invalid (expected blob header)");
		return WIMLIB_ERR_INVALID_PIPABLE_WIM;
	}

	copy_hash(hash_ret, blob_hdr.hash);

	reshdr_ret->size_in_wim = 0; /* Not available  */
	reshdr_ret->flags = le32_to_cpu(blob_hdr.flags);
	reshdr_ret->offset_in_wim = pwm->in_fd.offset;
	reshdr_ret->uncompressed_size = le64_to_cpu(blob_hdr.uncompressed_size);

	if (unlikely(reshdr_ret->uncompressed_size == 0)) {
		ERROR("Data read on pipe is invalid (resource is of 0 size)");
		return WIMLIB_ERR_INVALID_PIPABLE_WIM;
	}

	return 0;

read_error:
	if (ret == WIMLIB_ERR_UNEXPECTED_END_OF_FILE)
		ERROR("The pipe ended before all needed data was sent!");
	else
		ERROR_WITH_ERRNO("Error reading pipable WIM from pipe");
	return ret;
}

static int
read_blobs_from_pipe(struct apply_ctx *ctx, const struct read_blob_callbacks *cbs)
{
	int ret;
	u8 hash[SHA1_HASH_SIZE];
	struct wim_reshdr reshdr;
	struct wim_header_disk pwm_hdr;
	struct wim_resource_descriptor rdesc;
	struct blob_descriptor *blob;

	copy_guid(ctx->progress.extract.guid, ctx->wim->hdr.guid);
	ctx->progress.extract.part_number = ctx->wim->hdr.part_number;
	ctx->progress.extract.total_parts = ctx->wim->hdr.total_parts;
	ret = extract_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_SPWM_PART_BEGIN);
	if (ret)
		return ret;

	while (ctx->num_blobs_remaining) {

		ret = read_pwm_blob_header(ctx->wim, hash, &reshdr, &pwm_hdr);

		if (ret == PWM_FOUND_WIM_HDR) {
			u16 part_number = le16_to_cpu(pwm_hdr.part_number);
			u16 total_parts = le16_to_cpu(pwm_hdr.total_parts);

			if (part_number == ctx->progress.extract.part_number &&
			    total_parts == ctx->progress.extract.total_parts &&
			    guids_equal(pwm_hdr.guid, ctx->progress.extract.guid))
				continue;

			copy_guid(ctx->progress.extract.guid, pwm_hdr.guid);
			ctx->progress.extract.part_number = part_number;
			ctx->progress.extract.total_parts = total_parts;
			ret = extract_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_SPWM_PART_BEGIN);
			if (ret)
				return ret;

			continue;
		}

		if (ret)
			return ret;

		if (!(reshdr.flags & WIM_RESHDR_FLAG_METADATA)
		    && (blob = lookup_blob(ctx->wim->blob_table, hash))
		    && (blob->out_refcnt))
		{
			wim_reshdr_to_desc_and_blob(&reshdr, ctx->wim, &rdesc, blob);
			ret = read_blob_with_sha1(blob, cbs,
						  ctx->extract_flags &
						  WIMLIB_EXTRACT_FLAG_RECOVER_DATA);
			blob_unset_is_located_in_wim_resource(blob);
			if (ret)
				return ret;
			ctx->num_blobs_remaining--;
		} else {
			wim_reshdr_to_desc(&reshdr, ctx->wim, &rdesc);
			ret = skip_wim_resource(&rdesc);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int
handle_pwm_metadata_resource(WIMStruct *pwm, int image, bool is_needed)
{
	struct blob_descriptor *blob;
	struct wim_reshdr reshdr;
	struct wim_resource_descriptor *rdesc;
	int ret;

	ret = WIMLIB_ERR_NOMEM;
	blob = new_blob_descriptor();
	if (!blob)
		goto out;

	ret = read_pwm_blob_header(pwm, blob->hash, &reshdr, NULL);
	if (ret)
		goto out;

	ret = WIMLIB_ERR_INVALID_PIPABLE_WIM;
	if (!(reshdr.flags & WIM_RESHDR_FLAG_METADATA)) {
		ERROR("Expected metadata resource, but found non-metadata "
		      "resource");
		goto out;
	}

	ret = WIMLIB_ERR_NOMEM;
	rdesc = MALLOC(sizeof(*rdesc));
	if (!rdesc)
		goto out;

	wim_reshdr_to_desc_and_blob(&reshdr, pwm, rdesc, blob);
	pwm->refcnt++;

	ret = WIMLIB_ERR_NOMEM;
	pwm->image_metadata[image - 1] = new_unloaded_image_metadata(blob);
	if (!pwm->image_metadata[image - 1])
		goto out;
	blob = NULL;

	/* If the metadata resource is for the image being extracted, then parse
	 * it and save the metadata in memory.  Otherwise, skip over it.  */
	if (is_needed)
		ret = select_wim_image(pwm, image);
	else
		ret = skip_wim_resource(rdesc);
out:
	free_blob_descriptor(blob);
	return ret;
}

/* Creates a temporary file opened for writing.  The open file descriptor is
 * returned in @fd_ret and its name is returned in @name_ret (dynamically
 * allocated).  */
static int
create_temporary_file(struct filedes *fd_ret, tchar **name_ret)
{
	tchar *name;
	int raw_fd;

#ifdef _WIN32
retry:
	// coverity[secure_temp]
	name = _wtempnam(NULL, L"wimlib");
	if (!name) {
		ERROR_WITH_ERRNO("Failed to create temporary filename");
		return WIMLIB_ERR_NOMEM;
	}
	raw_fd = _wopen(name, O_WRONLY | O_CREAT | O_EXCL | O_BINARY |
			_O_SHORT_LIVED, 0600);
	if (raw_fd < 0 && errno == EEXIST) {
		FREE(name);
		goto retry;
	}
#else /* _WIN32 */
	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir)
		tmpdir = P_tmpdir;
	name = MALLOC(strlen(tmpdir) + 1 + 6 + 6 + 1);
	if (!name)
		return WIMLIB_ERR_NOMEM;
	sprintf(name, "%s/wimlibXXXXXX", tmpdir);
	raw_fd = mkstemp(name);
#endif /* !_WIN32 */

	if (raw_fd < 0) {
		ERROR_WITH_ERRNO("Failed to create temporary file "
				 "\"%"TS"\"", name);
		FREE(name);
		return WIMLIB_ERR_OPEN;
	}

	filedes_init(fd_ret, raw_fd);
	*name_ret = name;
	return 0;
}

static int
begin_extract_blob(struct blob_descriptor *blob, void *_ctx)
{
	struct apply_ctx *ctx = _ctx;

	if (unlikely(blob->out_refcnt > MAX_OPEN_FILES))
		return create_temporary_file(&ctx->tmpfile_fd, &ctx->tmpfile_name);

	return call_begin_blob(blob, ctx->saved_cbs);
}

static int
extract_chunk(const struct blob_descriptor *blob, u64 offset,
	      const void *chunk, size_t size, void *_ctx)
{
	struct apply_ctx *ctx = _ctx;
	union wimlib_progress_info *progress = &ctx->progress;
	bool last = (offset + size == blob->size);
	int ret;

	if (likely(ctx->supported_features.hard_links)) {
		progress->extract.completed_bytes +=
			(u64)size * blob->out_refcnt;
		if (last)
			progress->extract.completed_streams += blob->out_refcnt;
	} else {
		const struct blob_extraction_target *targets =
			blob_extraction_targets(blob);
		for (u32 i = 0; i < blob->out_refcnt; i++) {
			const struct wim_inode *inode = targets[i].inode;
			const struct wim_dentry *dentry;

			inode_for_each_extraction_alias(dentry, inode) {
				progress->extract.completed_bytes += size;
				if (last)
					progress->extract.completed_streams++;
			}
		}
	}
	if (progress->extract.completed_bytes >= ctx->next_progress) {

		ret = extract_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS);
		if (ret)
			return ret;

		set_next_progress(progress->extract.completed_bytes,
				  progress->extract.total_bytes,
				  &ctx->next_progress);
	}

	if (unlikely(filedes_valid(&ctx->tmpfile_fd))) {
		/* Just extracting to temporary file for now.  */
		ret = full_write(&ctx->tmpfile_fd, chunk, size);
		if (ret) {
			ERROR_WITH_ERRNO("Error writing data to "
					 "temporary file \"%"TS"\"",
					 ctx->tmpfile_name);
		}
		return ret;
	}

	return call_continue_blob(blob, offset, chunk, size, ctx->saved_cbs);
}

/* Copy the blob's data from the temporary file to each of its targets.
 *
 * This is executed only in the very uncommon case that a blob is being
 * extracted to more than MAX_OPEN_FILES targets!  */
static int
extract_from_tmpfile(const tchar *tmpfile_name,
		     const struct blob_descriptor *orig_blob,
		     const struct read_blob_callbacks *cbs)
{
	struct blob_descriptor tmpfile_blob;
	const struct blob_extraction_target *targets = blob_extraction_targets(orig_blob);
	int ret;

	memcpy(&tmpfile_blob, orig_blob, sizeof(struct blob_descriptor));
	tmpfile_blob.blob_location = BLOB_IN_FILE_ON_DISK;
	tmpfile_blob.file_on_disk = (tchar *)tmpfile_name;
	tmpfile_blob.out_refcnt = 1;

	for (u32 i = 0; i < orig_blob->out_refcnt; i++) {
		tmpfile_blob.inline_blob_extraction_targets[0] = targets[i];
		ret = read_blob_with_cbs(&tmpfile_blob, cbs, false);
		if (ret)
			return ret;
	}
	return 0;
}

static void
warn_about_corrupted_file(struct wim_dentry *dentry,
			  const struct wim_inode_stream *stream)
{
	WARNING("Corruption in %s\"%"TS"\"!  Extracting anyway since data recovery mode is enabled.",
		stream_is_unnamed_data_stream(stream) ? "" : "alternate stream of ",
		dentry_full_path(dentry));
}

static int
end_extract_blob(struct blob_descriptor *blob, int status, void *_ctx)
{
	struct apply_ctx *ctx = _ctx;

	if ((ctx->extract_flags & WIMLIB_EXTRACT_FLAG_RECOVER_DATA) &&
	    !status && blob->corrupted) {
		const struct blob_extraction_target *targets =
			blob_extraction_targets(blob);
		for (u32 i = 0; i < blob->out_refcnt; i++) {
			struct wim_dentry *dentry =
				inode_first_extraction_dentry(targets[i].inode);

			warn_about_corrupted_file(dentry, targets[i].stream);
		}
	}

	if (unlikely(filedes_valid(&ctx->tmpfile_fd))) {
		filedes_close(&ctx->tmpfile_fd);
		if (!status)
			status = extract_from_tmpfile(ctx->tmpfile_name, blob,
						      ctx->saved_cbs);
		filedes_invalidate(&ctx->tmpfile_fd);
		tunlink(ctx->tmpfile_name);
		FREE(ctx->tmpfile_name);
		return status;
	}

	return call_end_blob(blob, status, ctx->saved_cbs);
}

/*
 * Read the list of blobs to extract and feed their data into the specified
 * callback functions.
 *
 * This handles checksumming each blob.
 *
 * This also handles sending WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS.
 *
 * This also works if the WIM is being read from a pipe.
 *
 * This also will split up blobs that will need to be extracted to more than
 * MAX_OPEN_FILES locations, as measured by the 'out_refcnt' of each blob.
 * Therefore, the apply_operations implementation need not worry about running
 * out of file descriptors, unless it might open more than one file descriptor
 * per 'blob_extraction_target' (e.g. Win32 currently might because the
 * destination file system might not support hard links).
 */
int
extract_blob_list(struct apply_ctx *ctx, const struct read_blob_callbacks *cbs)
{
	struct read_blob_callbacks wrapper_cbs = {
		.begin_blob	= begin_extract_blob,
		.continue_blob	= extract_chunk,
		.end_blob	= end_extract_blob,
		.ctx		= ctx,
	};
	ctx->saved_cbs = cbs;
	if (ctx->extract_flags & WIMLIB_EXTRACT_FLAG_FROM_PIPE) {
		return read_blobs_from_pipe(ctx, &wrapper_cbs);
	} else {
		int flags = VERIFY_BLOB_HASHES;

		if (ctx->extract_flags & WIMLIB_EXTRACT_FLAG_RECOVER_DATA)
			flags |= RECOVER_DATA;

		return read_blob_list(&ctx->blob_list,
				      offsetof(struct blob_descriptor,
					       extraction_list),
				      &wrapper_cbs, flags);
	}
}

/* Extract a WIM dentry to standard output.
 *
 * This obviously doesn't make sense in all cases.  We return an error if the
 * dentry does not correspond to a regular file.  Otherwise we extract the
 * unnamed data stream only.  */
static int
extract_dentry_to_stdout(struct wim_dentry *dentry,
			 const struct blob_table *blob_table, int extract_flags)
{
	struct wim_inode *inode = dentry->d_inode;
	struct blob_descriptor *blob;
	struct filedes _stdout;
	bool recover = (extract_flags & WIMLIB_EXTRACT_FLAG_RECOVER_DATA);
	int ret;

	if (inode->i_attributes & (FILE_ATTRIBUTE_REPARSE_POINT |
				   FILE_ATTRIBUTE_DIRECTORY |
				   FILE_ATTRIBUTE_ENCRYPTED))
	{
		ERROR("\"%"TS"\" is not a regular file and therefore cannot be "
		      "extracted to standard output", dentry_full_path(dentry));
		return WIMLIB_ERR_NOT_A_REGULAR_FILE;
	}

	blob = inode_get_blob_for_unnamed_data_stream(inode, blob_table);
	if (!blob) {
		const u8 *hash = inode_get_hash_of_unnamed_data_stream(inode);
		if (!is_zero_hash(hash))
			return blob_not_found_error(inode, hash);
		return 0;
	}

	filedes_init(&_stdout, STDOUT_FILENO);
	ret = extract_blob_to_fd(blob, &_stdout, recover);
	if (ret)
		return ret;
	if (recover && blob->corrupted)
		warn_about_corrupted_file(dentry,
					  inode_get_unnamed_data_stream(inode));
	return 0;
}

static int
extract_dentries_to_stdout(struct wim_dentry **dentries, size_t num_dentries,
			   const struct blob_table *blob_table,
			   int extract_flags)
{
	for (size_t i = 0; i < num_dentries; i++) {
		int ret = extract_dentry_to_stdout(dentries[i], blob_table,
						   extract_flags);
		if (ret)
			return ret;
	}
	return 0;
}

/**********************************************************************/

/*
 * Removes duplicate dentries from the array.
 *
 * Returns the new number of dentries, packed at the front of the array.
 */
static size_t
remove_duplicate_trees(struct wim_dentry **trees, size_t num_trees)
{
	size_t i, j = 0;
	for (i = 0; i < num_trees; i++) {
		if (!trees[i]->d_tmp_flag) {
			/* Found distinct dentry.  */
			trees[i]->d_tmp_flag = 1;
			trees[j++] = trees[i];
		}
	}
	for (i = 0; i < j; i++)
		trees[i]->d_tmp_flag = 0;
	return j;
}

/*
 * Remove dentries that are descendants of other dentries in the array.
 *
 * Returns the new number of dentries, packed at the front of the array.
 */
static size_t
remove_contained_trees(struct wim_dentry **trees, size_t num_trees)
{
	size_t i, j = 0;
	for (i = 0; i < num_trees; i++)
		trees[i]->d_tmp_flag = 1;
	for (i = 0; i < num_trees; i++) {
		struct wim_dentry *d = trees[i];
		while (!dentry_is_root(d)) {
			d = d->d_parent;
			if (d->d_tmp_flag)
				goto tree_contained;
		}
		trees[j++] = trees[i];
		continue;

	tree_contained:
		trees[i]->d_tmp_flag = 0;
	}

	for (i = 0; i < j; i++)
		trees[i]->d_tmp_flag = 0;
	return j;
}

static int
dentry_append_to_list(struct wim_dentry *dentry, void *_dentry_list)
{
	struct list_head *dentry_list = _dentry_list;
	list_add_tail(&dentry->d_extraction_list_node, dentry_list);
	return 0;
}

static void
dentry_reset_extraction_list_node(struct wim_dentry *dentry)
{
	dentry->d_extraction_list_node = (struct list_head){NULL, NULL};
}

static int
dentry_delete_from_list(struct wim_dentry *dentry, void *_ignore)
{
	if (will_extract_dentry(dentry)) {
		list_del(&dentry->d_extraction_list_node);
		dentry_reset_extraction_list_node(dentry);
	}
	return 0;
}

/*
 * Build the preliminary list of dentries to be extracted.
 *
 * The list maintains the invariant that if d1 and d2 are in the list and d1 is
 * an ancestor of d2, then d1 appears before d2 in the list.
 */
static void
build_dentry_list(struct list_head *dentry_list, struct wim_dentry **trees,
		  size_t num_trees, bool add_ancestors)
{
	INIT_LIST_HEAD(dentry_list);

	/* Add the trees recursively.  */
	for (size_t i = 0; i < num_trees; i++)
		for_dentry_in_tree(trees[i], dentry_append_to_list, dentry_list);

	/* If requested, add ancestors of the trees.  */
	if (add_ancestors) {
		for (size_t i = 0; i < num_trees; i++) {
			struct wim_dentry *dentry = trees[i];
			struct wim_dentry *ancestor;
			struct list_head *place_after;

			if (dentry_is_root(dentry))
				continue;

			place_after = dentry_list;
			ancestor = dentry;
			do {
				ancestor = ancestor->d_parent;
				if (will_extract_dentry(ancestor)) {
					place_after = &ancestor->d_extraction_list_node;
					break;
				}
			} while (!dentry_is_root(ancestor));

			ancestor = dentry;
			do {
				ancestor = ancestor->d_parent;
				if (will_extract_dentry(ancestor))
					break;
				list_add(&ancestor->d_extraction_list_node, place_after);
			} while (!dentry_is_root(ancestor));
		}
	}
}

static void
destroy_dentry_list(struct list_head *dentry_list)
{
	struct wim_dentry *dentry, *tmp;
	struct wim_inode *inode;

	list_for_each_entry_safe(dentry, tmp, dentry_list, d_extraction_list_node) {
		inode = dentry->d_inode;
		dentry_reset_extraction_list_node(dentry);
		inode->i_visited = 0;
		inode->i_can_externally_back = 0;
		if ((void *)dentry->d_extraction_name != (void *)dentry->d_name)
			FREE(dentry->d_extraction_name);
		dentry->d_extraction_name = NULL;
		dentry->d_extraction_name_nchars = 0;
	}
}

static void
destroy_blob_list(struct list_head *blob_list)
{
	struct blob_descriptor *blob;

	list_for_each_entry(blob, blob_list, extraction_list)
		if (blob->out_refcnt > ARRAY_LEN(blob->inline_blob_extraction_targets))
			FREE(blob->blob_extraction_targets);
}

#ifdef _WIN32
static const utf16lechar replacement_char = cpu_to_le16(0xfffd);
#else
static const utf16lechar replacement_char = cpu_to_le16('?');
#endif

static bool
file_name_valid(utf16lechar *name, size_t num_chars, bool fix)
{
	size_t i;

	if (num_chars == 0)
		return true;
	for (i = 0; i < num_chars; i++) {
		u16 c = le16_to_cpu(name[i]);
		if (c == '/' || c == '\0'
#ifdef _WIN32
			|| (c >= '\x01' && c <= '\x1F') || c == ':' || c == '?'
			|| c == '"' || c == '<' || c == '>' || c == '|'
#endif
		) {
			if (fix)
				name[i] = replacement_char;
			else
				return false;
		}
	}

	return true;
}

static int
dentry_calculate_extraction_name(struct wim_dentry *dentry,
				 struct apply_ctx *ctx)
{
	int ret;

	if (dentry_is_root(dentry))
		return 0;

#ifdef WITH_NTFS_3G
	if (ctx->extract_flags & WIMLIB_EXTRACT_FLAG_NTFS) {
		dentry->d_extraction_name = dentry->d_name;
		dentry->d_extraction_name_nchars = dentry->d_name_nbytes /
						   sizeof(utf16lechar);
		return 0;
	}
#endif

	if (!ctx->supported_features.case_sensitive_filenames) {
		struct wim_dentry *other;
		dentry_for_each_ci_match(other, dentry) {
			if (will_extract_dentry(other)) {
				if (ctx->extract_flags &
				    WIMLIB_EXTRACT_FLAG_ALL_CASE_CONFLICTS) {
					WARNING("\"%"TS"\" has the same "
						"case-insensitive name as "
						"\"%"TS"\"; extracting "
						"dummy name instead",
						dentry_full_path(dentry),
						dentry_full_path(other));
					goto out_replace;
				} else {
					WARNING("Not extracting \"%"TS"\": "
						"has same case-insensitive "
						"name as \"%"TS"\"",
						dentry_full_path(dentry),
						dentry_full_path(other));
					goto skip_dentry;
				}
			}
		}
	}

	if (file_name_valid(dentry->d_name, dentry->d_name_nbytes / 2, false)) {
		size_t nbytes = 0;
		ret = utf16le_get_tstr(dentry->d_name,
				       dentry->d_name_nbytes,
				       (const tchar **)&dentry->d_extraction_name,
				       &nbytes);
		dentry->d_extraction_name_nchars = nbytes / sizeof(tchar);
		return ret;
	} else {
		if (ctx->extract_flags & WIMLIB_EXTRACT_FLAG_REPLACE_INVALID_FILENAMES)
		{
			WARNING("\"%"TS"\" has an invalid filename "
				"that is not supported on this platform; "
				"extracting dummy name instead",
				dentry_full_path(dentry));
			goto out_replace;
		} else {
			WARNING("Not extracting \"%"TS"\": has an invalid filename "
				"that is not supported on this platform",
				dentry_full_path(dentry));
			goto skip_dentry;
		}
	}

out_replace:
	{
		utf16lechar* utf16_name_copy = alloca(dentry->d_name_nbytes);

		memcpy(utf16_name_copy, dentry->d_name, dentry->d_name_nbytes);
		file_name_valid(utf16_name_copy, dentry->d_name_nbytes / 2, true);

		const tchar *tchar_name;
		size_t tchar_nchars;

		ret = utf16le_get_tstr(utf16_name_copy,
				       dentry->d_name_nbytes,
				       &tchar_name, &tchar_nchars);
		if (ret)
			return ret;

		tchar_nchars /= sizeof(tchar);

		size_t fixed_name_num_chars = tchar_nchars;
		tchar* fixed_name = alloca((tchar_nchars + 50) * sizeof(tchar));

		tmemcpy(fixed_name, tchar_name, tchar_nchars);
		fixed_name_num_chars += tsnprintf(fixed_name + tchar_nchars,
						 tchar_nchars + 50,
						 T(" (invalid filename #%lu)"),
						 ++ctx->invalid_sequence);

		utf16le_put_tstr(tchar_name);

		dentry->d_extraction_name = TSTRDUP(fixed_name);
		if (!dentry->d_extraction_name)
			return WIMLIB_ERR_NOMEM;
		dentry->d_extraction_name_nchars = fixed_name_num_chars;
	}
	return 0;

skip_dentry:
	for_dentry_in_tree(dentry, dentry_delete_from_list, NULL);
	return 0;
}

/*
 * Calculate the actual filename component at which each WIM dentry will be
 * extracted, with special handling for dentries that are unsupported by the
 * extraction backend or have invalid names.
 *
 * ctx->supported_features must be filled in.
 *
 * Possible error codes: WIMLIB_ERR_NOMEM, WIMLIB_ERR_INVALID_UTF16_STRING
 */
static int
dentry_list_calculate_extraction_names(struct list_head *dentry_list,
				       struct apply_ctx *ctx)
{
	struct list_head *prev, *cur;

	/* Can't use list_for_each_entry() because a call to
	 * dentry_calculate_extraction_name() may delete the current dentry and
	 * its children from the list.  */

	prev = dentry_list;
	for (;;) {
		struct wim_dentry *dentry;
		int ret;

		cur = prev->next;
		if (cur == dentry_list)
			break;

		dentry = list_entry(cur, struct wim_dentry, d_extraction_list_node);

		ret = dentry_calculate_extraction_name(dentry, ctx);
		if (ret)
			return ret;

		if (prev->next == cur)
			prev = cur;
		else
			; /* Current dentry and its children (which follow in
			     the list) were deleted.  prev stays the same.  */
	}
	return 0;
}

static int
dentry_resolve_streams(struct wim_dentry *dentry, int extract_flags,
		       struct blob_table *blob_table)
{
	struct wim_inode *inode = dentry->d_inode;
	struct blob_descriptor *blob;
	int ret;
	bool force = false;

	/* Special case:  when extracting from a pipe, the WIM blob table is
	 * initially empty, so "resolving" an inode's streams is initially not
	 * possible.  However, we still need to keep track of which blobs,
	 * identified by SHA-1 message digests, need to be extracted, so we
	 * "resolve" the inode's streams anyway by allocating a 'struct
	 * blob_descriptor' for each one.  */
	if (extract_flags & WIMLIB_EXTRACT_FLAG_FROM_PIPE)
		force = true;
	ret = inode_resolve_streams(inode, blob_table, force);
	if (ret)
		return ret;
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		blob = stream_blob_resolved(&inode->i_streams[i]);
		if (blob)
			blob->out_refcnt = 0;
	}
	return 0;
}

/*
 * For each dentry to be extracted, resolve all streams in the corresponding
 * inode and set 'out_refcnt' in all referenced blob_descriptors to 0.
 *
 * Possible error codes: WIMLIB_ERR_RESOURCE_NOT_FOUND, WIMLIB_ERR_NOMEM.
 */
static int
dentry_list_resolve_streams(struct list_head *dentry_list,
			    struct apply_ctx *ctx)
{
	struct wim_dentry *dentry;
	int ret;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		ret = dentry_resolve_streams(dentry,
					     ctx->extract_flags,
					     ctx->wim->blob_table);
		if (ret)
			return ret;
	}
	return 0;
}

static int
ref_stream(struct wim_inode_stream *strm, struct wim_dentry *dentry,
	   struct apply_ctx *ctx)
{
	struct wim_inode *inode = dentry->d_inode;
	struct blob_descriptor *blob = stream_blob_resolved(strm);
	struct blob_extraction_target *targets;

	if (!blob)
		return 0;

	/* Tally the size only for each actual extraction of the stream (not
	 * additional hard links to the inode).  */
	if (inode->i_visited && ctx->supported_features.hard_links)
		return 0;

	ctx->progress.extract.total_bytes += blob->size;
	ctx->progress.extract.total_streams++;

	if (inode->i_visited)
		return 0;

	/* Add each blob to 'ctx->blob_list' only one time, regardless of how
	 * many extraction targets it will have.  */
	if (blob->out_refcnt == 0) {
		list_add_tail(&blob->extraction_list, &ctx->blob_list);
		ctx->num_blobs_remaining++;
	}

	/* Set this stream as an extraction target of 'blob'.  */

	if (blob->out_refcnt < ARRAY_LEN(blob->inline_blob_extraction_targets)) {
		targets = blob->inline_blob_extraction_targets;
	} else {
		struct blob_extraction_target *prev_targets;
		size_t alloc_blob_extraction_targets;

		if (blob->out_refcnt == ARRAY_LEN(blob->inline_blob_extraction_targets)) {
			prev_targets = NULL;
			alloc_blob_extraction_targets = ARRAY_LEN(blob->inline_blob_extraction_targets);
		} else {
			prev_targets = blob->blob_extraction_targets;
			alloc_blob_extraction_targets = blob->alloc_blob_extraction_targets;
		}

		if (blob->out_refcnt == alloc_blob_extraction_targets) {
			alloc_blob_extraction_targets *= 2;
			targets = REALLOC(prev_targets,
					  alloc_blob_extraction_targets *
					  sizeof(targets[0]));
			if (!targets)
				return WIMLIB_ERR_NOMEM;
			if (!prev_targets) {
				memcpy(targets,
				       blob->inline_blob_extraction_targets,
				       sizeof(blob->inline_blob_extraction_targets));
			}
			blob->blob_extraction_targets = targets;
			blob->alloc_blob_extraction_targets = alloc_blob_extraction_targets;
		}
		targets = blob->blob_extraction_targets;
	}
	targets[blob->out_refcnt].inode = inode;
	targets[blob->out_refcnt].stream = strm;
	blob->out_refcnt++;
	return 0;
}

static int
ref_stream_if_needed(struct wim_dentry *dentry, struct wim_inode *inode,
		     struct wim_inode_stream *strm, struct apply_ctx *ctx)
{
	bool need_stream = false;
	switch (strm->stream_type) {
	case STREAM_TYPE_DATA:
		if (stream_is_named(strm)) {
			/* Named data stream  */
			if (ctx->supported_features.named_data_streams)
				need_stream = true;
		} else if (!(inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
						    FILE_ATTRIBUTE_ENCRYPTED))
			   && !(inode_is_symlink(inode)
				&& !ctx->supported_features.reparse_points
				&& ctx->supported_features.symlink_reparse_points))
		{
			/*
			 * Unnamed data stream.  Skip if any of the following is true:
			 *
			 * - file is a directory
			 * - file is encrypted
			 * - backend needs to create the file as UNIX symlink
			 * - backend will extract the stream as externally
			 *   backed from the WIM archive itself
			 */
			if (ctx->apply_ops->will_back_from_wim) {
				int ret = (*ctx->apply_ops->will_back_from_wim)(dentry, ctx);
				if (ret > 0) /* Error?  */
					return ret;
				if (ret < 0) /* Won't externally back?  */
					need_stream = true;
			} else {
				need_stream = true;
			}
		}
		break;
	case STREAM_TYPE_REPARSE_POINT:
		wimlib_assert(inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT);
		if (ctx->supported_features.reparse_points ||
		    (inode_is_symlink(inode) &&
		     ctx->supported_features.symlink_reparse_points))
			need_stream = true;
		break;
	case STREAM_TYPE_EFSRPC_RAW_DATA:
		wimlib_assert(inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED);
		if (inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (ctx->supported_features.encrypted_directories)
				need_stream = true;
		} else {
			if (ctx->supported_features.encrypted_files)
				need_stream = true;
		}
		break;
	}
	if (need_stream)
		return ref_stream(strm, dentry, ctx);
	return 0;
}

static int
dentry_ref_streams(struct wim_dentry *dentry, struct apply_ctx *ctx)
{
	struct wim_inode *inode = dentry->d_inode;
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		int ret = ref_stream_if_needed(dentry, inode,
					       &inode->i_streams[i], ctx);
		if (ret)
			return ret;
	}
	inode->i_visited = 1;
	return 0;
}

/*
 * Given a list of dentries to be extracted, build the list of blobs that need
 * to be extracted, and for each blob determine the streams to which that blob
 * will be extracted.
 *
 * This also initializes the extract progress info with byte and blob
 * information.
 *
 * ctx->supported_features must be filled in.
 */
static int
dentry_list_ref_streams(struct list_head *dentry_list, struct apply_ctx *ctx)
{
	struct wim_dentry *dentry;
	int ret;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		ret = dentry_ref_streams(dentry, ctx);
		if (ret)
			return ret;
	}
	list_for_each_entry(dentry, dentry_list, d_extraction_list_node)
		dentry->d_inode->i_visited = 0;
	return 0;
}

static void
dentry_list_build_inode_alias_lists(struct list_head *dentry_list)
{
	struct wim_dentry *dentry;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node)
		dentry->d_inode->i_first_extraction_alias = NULL;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		dentry->d_next_extraction_alias = dentry->d_inode->i_first_extraction_alias;
		dentry->d_inode->i_first_extraction_alias = dentry;
	}
}

static void
inode_tally_features(const struct wim_inode *inode,
		     struct wim_features *features)
{
	if (inode->i_attributes & FILE_ATTRIBUTE_READONLY)
		features->readonly_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_HIDDEN)
		features->hidden_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_SYSTEM)
		features->system_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_ARCHIVE)
		features->archive_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_COMPRESSED)
		features->compressed_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED) {
		if (inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY)
			features->encrypted_directories++;
		else
			features->encrypted_files++;
	}
	if (inode->i_attributes & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
		features->not_context_indexed_files++;
	if (inode->i_attributes & FILE_ATTRIBUTE_SPARSE_FILE)
		features->sparse_files++;
	if (inode_has_named_data_stream(inode))
		features->named_data_streams++;
	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		features->reparse_points++;
		if (inode_is_symlink(inode))
			features->symlink_reparse_points++;
		else
			features->other_reparse_points++;
	}
	if (inode_has_security_descriptor(inode))
		features->security_descriptors++;
	if (inode_has_unix_data(inode))
		features->unix_data++;
	if (inode_has_object_id(inode))
		features->object_ids++;
	if (inode_has_xattrs(inode))
		features->xattrs++;
}

/* Tally features necessary to extract a dentry and the corresponding inode.  */
static void
dentry_tally_features(struct wim_dentry *dentry, struct wim_features *features)
{
	struct wim_inode *inode = dentry->d_inode;

	if (dentry_has_short_name(dentry))
		features->short_names++;

	if (inode->i_visited) {
		features->hard_links++;
	} else {
		inode_tally_features(inode, features);
		inode->i_visited = 1;
	}
}

/* Tally the features necessary to extract the specified dentries.  */
static void
dentry_list_get_features(struct list_head *dentry_list,
			 struct wim_features *features)
{
	struct wim_dentry *dentry;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node)
		dentry_tally_features(dentry, features);

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node)
		dentry->d_inode->i_visited = 0;
}

static int
do_feature_check(const struct wim_features *required_features,
		 const struct wim_features *supported_features,
		 int extract_flags)
{
	/* Encrypted files.  */
	if (required_features->encrypted_files &&
	    !supported_features->encrypted_files)
		WARNING("Ignoring EFS-encrypted data of %lu files",
			required_features->encrypted_files);

	/* Named data streams.  */
	if (required_features->named_data_streams &&
	    !supported_features->named_data_streams)
		WARNING("Ignoring named data streams of %lu files",
			required_features->named_data_streams);

	/* File attributes.  */
	if (!(extract_flags & WIMLIB_EXTRACT_FLAG_NO_ATTRIBUTES)) {

		if (required_features->readonly_files &&
		    !supported_features->readonly_files)
			WARNING("Ignoring FILE_ATTRIBUTE_READONLY of %lu files",
				required_features->readonly_files);

		if (required_features->hidden_files &&
		    !supported_features->hidden_files)
			WARNING("Ignoring FILE_ATTRIBUTE_HIDDEN of %lu files",
				required_features->hidden_files);

		if (required_features->system_files &&
		    !supported_features->system_files)
			WARNING("Ignoring FILE_ATTRIBUTE_SYSTEM of %lu files",
				required_features->system_files);

		/* Note: Don't bother the user about FILE_ATTRIBUTE_ARCHIVE.
		 * We're an archive program, so theoretically we can do what we
		 * want with it.  */

		if (required_features->compressed_files &&
		    !supported_features->compressed_files)
			WARNING("Ignoring FILE_ATTRIBUTE_COMPRESSED of %lu files",
				required_features->compressed_files);

		if (required_features->not_context_indexed_files &&
		    !supported_features->not_context_indexed_files)
			WARNING("Ignoring FILE_ATTRIBUTE_NOT_CONTENT_INDEXED of %lu files",
				required_features->not_context_indexed_files);

		if (required_features->sparse_files &&
		    !supported_features->sparse_files)
			WARNING("Ignoring FILE_ATTRIBUTE_SPARSE_FILE of %lu files",
				required_features->sparse_files);

		if (required_features->encrypted_directories &&
		    !supported_features->encrypted_directories)
			WARNING("Ignoring FILE_ATTRIBUTE_ENCRYPTED of %lu directories",
				required_features->encrypted_directories);
	}

	/* Hard links.  */
	if (required_features->hard_links && !supported_features->hard_links)
		WARNING("Extracting %lu hard links as independent files",
			required_features->hard_links);

	/* Symbolic links and reparse points.  */
	if ((extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_SYMLINKS) &&
	    required_features->symlink_reparse_points &&
	    !supported_features->symlink_reparse_points &&
	    !supported_features->reparse_points)
	{
		ERROR("Extraction backend does not support symbolic links!");
		return WIMLIB_ERR_UNSUPPORTED;
	}
	if (required_features->reparse_points &&
	    !supported_features->reparse_points)
	{
		if (supported_features->symlink_reparse_points) {
			if (required_features->other_reparse_points) {
				WARNING("Ignoring reparse data of %lu non-symlink/junction files",
					required_features->other_reparse_points);
			}
		} else {
			WARNING("Ignoring reparse data of %lu files",
				required_features->reparse_points);
		}
	}

	/* Security descriptors.  */
	if (((extract_flags & (WIMLIB_EXTRACT_FLAG_STRICT_ACLS |
			       WIMLIB_EXTRACT_FLAG_UNIX_DATA))
	     == WIMLIB_EXTRACT_FLAG_STRICT_ACLS) &&
	    required_features->security_descriptors &&
	    !supported_features->security_descriptors)
	{
		ERROR("Extraction backend does not support security descriptors!");
		return WIMLIB_ERR_UNSUPPORTED;
	}
	if (!(extract_flags & WIMLIB_EXTRACT_FLAG_NO_ACLS) &&
	    required_features->security_descriptors &&
	    !supported_features->security_descriptors)
		WARNING("Ignoring Windows NT security descriptors of %lu files",
			required_features->security_descriptors);

	/* Standard UNIX metadata */
	if (required_features->unix_data &&
	    (!supported_features->unix_data ||
	     !(extract_flags & WIMLIB_EXTRACT_FLAG_UNIX_DATA)))
	{
		if (extract_flags & WIMLIB_EXTRACT_FLAG_UNIX_DATA) {
			ERROR("Requested UNIX metadata extraction, but "
			      "extraction backend does not support it!");
			return WIMLIB_ERR_UNSUPPORTED;
		}
		WARNING("Ignoring UNIX metadata (uid/gid/mode/rdev) of %lu files%"TS,
			required_features->unix_data,
			(supported_features->unix_data ?
			 T("\n          (use --unix-data mode to extract these)") : T("")));
	}

	/* Extended attributes */
	if (required_features->xattrs &&
	    (!supported_features->xattrs ||
	     (supported_features->unix_data &&
	      !(extract_flags & WIMLIB_EXTRACT_FLAG_UNIX_DATA))))
	{
		WARNING("Ignoring extended attributes of %lu files%"TS,
			required_features->xattrs,
			(supported_features->xattrs ?
			 T("\n          (use --unix-data mode to extract these)") : T("")));
	}

	/* Object IDs.  */
	if (required_features->object_ids && !supported_features->object_ids) {
		WARNING("Ignoring object IDs of %lu files",
			required_features->object_ids);
	}

	/* DOS Names.  */
	if (required_features->short_names &&
	    !supported_features->short_names)
	{
		if (extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_SHORT_NAMES) {
			ERROR("Extraction backend does not support DOS names!");
			return WIMLIB_ERR_UNSUPPORTED;
		}
		WARNING("Ignoring DOS names of %lu files",
			required_features->short_names);
	}

	/* Timestamps.  */
	if ((extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_TIMESTAMPS) &&
	    !supported_features->timestamps)
	{
		ERROR("Extraction backend does not support timestamps!");
		return WIMLIB_ERR_UNSUPPORTED;
	}

	return 0;
}

static const struct apply_operations *
select_apply_operations(int extract_flags)
{
#ifdef WITH_NTFS_3G
	if (extract_flags & WIMLIB_EXTRACT_FLAG_NTFS)
		return &ntfs_3g_apply_ops;
#endif
#ifdef _WIN32
	return &win32_apply_ops;
#else
	return &unix_apply_ops;
#endif
}

static int
extract_trees(WIMStruct *wim, struct wim_dentry **trees, size_t num_trees,
	      const tchar *target, int extract_flags)
{
	const struct apply_operations *ops;
	struct apply_ctx *ctx;
	int ret;
	LIST_HEAD(dentry_list);

	if (extract_flags & WIMLIB_EXTRACT_FLAG_TO_STDOUT) {
		ret = extract_dentries_to_stdout(trees, num_trees,
						 wim->blob_table,
						 extract_flags);
		goto out;
	}

	num_trees = remove_duplicate_trees(trees, num_trees);
	num_trees = remove_contained_trees(trees, num_trees);

	ops = select_apply_operations(extract_flags);

	if (num_trees > 1 && ops->single_tree_only) {
		ERROR("Extracting multiple directory trees "
		      "at once is not supported in %s extraction mode!",
		      ops->name);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out;
	}

	ctx = CALLOC(1, ops->context_size);
	if (!ctx) {
		ret = WIMLIB_ERR_NOMEM;
		goto out;
	}

	ctx->wim = wim;
	ctx->target = target;
	ctx->target_nchars = tstrlen(target);
	ctx->extract_flags = extract_flags;
	if (ctx->wim->progfunc) {
		ctx->progfunc = ctx->wim->progfunc;
		ctx->progctx = ctx->wim->progctx;
		ctx->progress.extract.image = wim->current_image;
		ctx->progress.extract.extract_flags = (extract_flags &
						       WIMLIB_EXTRACT_MASK_PUBLIC);
		ctx->progress.extract.wimfile_name = wim->filename;
		ctx->progress.extract.image_name = wimlib_get_image_name(wim,
									 wim->current_image);
		ctx->progress.extract.target = target;
	}
	INIT_LIST_HEAD(&ctx->blob_list);
	filedes_invalidate(&ctx->tmpfile_fd);
	ctx->apply_ops = ops;

	ret = (*ops->get_supported_features)(target, &ctx->supported_features);
	if (ret)
		goto out_cleanup;

	build_dentry_list(&dentry_list, trees, num_trees,
			  !(extract_flags &
			    WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE));

	dentry_list_get_features(&dentry_list, &ctx->required_features);

	ret = do_feature_check(&ctx->required_features, &ctx->supported_features,
			       ctx->extract_flags);
	if (ret)
		goto out_cleanup;

	ret = dentry_list_calculate_extraction_names(&dentry_list, ctx);
	if (ret)
		goto out_cleanup;

	if (unlikely(list_empty(&dentry_list))) {
		WARNING("There is nothing to extract!");
		goto out_cleanup;
	}

	ret = dentry_list_resolve_streams(&dentry_list, ctx);
	if (ret)
		goto out_cleanup;

	dentry_list_build_inode_alias_lists(&dentry_list);

	ret = dentry_list_ref_streams(&dentry_list, ctx);
	if (ret)
		goto out_cleanup;

	if (extract_flags & WIMLIB_EXTRACT_FLAG_FROM_PIPE) {
		/* When extracting from a pipe, the number of bytes of data to
		 * extract can't be determined in the normal way (examining the
		 * blob table), since at this point all we have is a set of
		 * SHA-1 message digests of blobs that need to be extracted.
		 * However, we can get a reasonably accurate estimate by taking
		 * <TOTALBYTES> from the corresponding <IMAGE> in the WIM XML
		 * data.  This does assume that a full image is being extracted,
		 * but currently there is no API for doing otherwise.  (Also,
		 * subtract <HARDLINKBYTES> from this if hard links are
		 * supported by the extraction mode.)  */
		ctx->progress.extract.total_bytes =
			xml_get_image_total_bytes(wim->xml_info,
						  wim->current_image);
		if (ctx->supported_features.hard_links) {
			ctx->progress.extract.total_bytes -=
				xml_get_image_hard_link_bytes(wim->xml_info,
							      wim->current_image);
		}
	}

	ret = extract_progress(ctx,
			       ((extract_flags & WIMLIB_EXTRACT_FLAG_IMAGEMODE) ?
				       WIMLIB_PROGRESS_MSG_EXTRACT_IMAGE_BEGIN :
				       WIMLIB_PROGRESS_MSG_EXTRACT_TREE_BEGIN));
	if (ret)
		goto out_cleanup;

	ret = (*ops->extract)(&dentry_list, ctx);
	if (ret)
		goto out_cleanup;

	if (ctx->progress.extract.completed_bytes <
	    ctx->progress.extract.total_bytes)
	{
		ctx->progress.extract.completed_bytes =
			ctx->progress.extract.total_bytes;
		ret = extract_progress(ctx, WIMLIB_PROGRESS_MSG_EXTRACT_STREAMS);
		if (ret)
			goto out_cleanup;
	}

	ret = extract_progress(ctx,
			       ((extract_flags & WIMLIB_EXTRACT_FLAG_IMAGEMODE) ?
				       WIMLIB_PROGRESS_MSG_EXTRACT_IMAGE_END :
				       WIMLIB_PROGRESS_MSG_EXTRACT_TREE_END));
out_cleanup:
	destroy_blob_list(&ctx->blob_list);
	destroy_dentry_list(&dentry_list);
	FREE(ctx);
out:
	return ret;
}

static int
mkdir_if_needed(const tchar *target)
{
	if (!tmkdir(target, 0755))
		return 0;

	if (errno == EEXIST)
		return 0;

#ifdef _WIN32
	/* _wmkdir() fails with EACCES if called on a drive root directory.  */
	if (errno == EACCES)
		return 0;
#endif

	ERROR_WITH_ERRNO("Failed to create directory \"%"TS"\"", target);
	return WIMLIB_ERR_MKDIR;
}

/* Make sure the extraction flags make sense, and update them if needed.  */
static int
check_extract_flags(const WIMStruct *wim, int *extract_flags_p)
{
	int extract_flags = *extract_flags_p;

	/* Check for invalid flag combinations  */

	if ((extract_flags &
	     (WIMLIB_EXTRACT_FLAG_NO_ACLS |
	      WIMLIB_EXTRACT_FLAG_STRICT_ACLS)) == (WIMLIB_EXTRACT_FLAG_NO_ACLS |
						    WIMLIB_EXTRACT_FLAG_STRICT_ACLS))
		return WIMLIB_ERR_INVALID_PARAM;

	if ((extract_flags &
	     (WIMLIB_EXTRACT_FLAG_RPFIX |
	      WIMLIB_EXTRACT_FLAG_NORPFIX)) == (WIMLIB_EXTRACT_FLAG_RPFIX |
						WIMLIB_EXTRACT_FLAG_NORPFIX))
		return WIMLIB_ERR_INVALID_PARAM;

#ifndef WITH_NTFS_3G
	if (extract_flags & WIMLIB_EXTRACT_FLAG_NTFS) {
		ERROR("wimlib was compiled without support for NTFS-3G, so\n"
		      "        it cannot apply a WIM image directly to an NTFS volume.");
		return WIMLIB_ERR_UNSUPPORTED;
	}
#endif

	if (extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT) {
#ifdef _WIN32
		if (!wim->filename)
			return WIMLIB_ERR_NO_FILENAME;
#else
		ERROR("WIMBoot extraction is only supported on Windows!");
		return WIMLIB_ERR_UNSUPPORTED;
#endif
	}

	if (extract_flags & (WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K |
			     WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS8K |
			     WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS16K |
			     WIMLIB_EXTRACT_FLAG_COMPACT_LZX))
	{
	#ifdef _WIN32
		int count = 0;
		count += ((extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K) != 0);
		count += ((extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS8K) != 0);
		count += ((extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS16K) != 0);
		count += ((extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_LZX) != 0);
		if (count != 1) {
			ERROR("Only one compression format can be specified "
			      "for compact-mode extraction!");
			return WIMLIB_ERR_INVALID_PARAM;
		}
		if (extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT) {
			ERROR("Compact-mode extraction and WIMBoot-mode "
			      "extraction are mutually exclusive!");
			return WIMLIB_ERR_INVALID_PARAM;
		}
	#else
		ERROR("Compact-mode extraction (System Compression) "
		      "is only supported on Windows!");
		return WIMLIB_ERR_UNSUPPORTED;
	#endif
	}


	if ((extract_flags & (WIMLIB_EXTRACT_FLAG_RPFIX |
			      WIMLIB_EXTRACT_FLAG_NORPFIX |
			      WIMLIB_EXTRACT_FLAG_IMAGEMODE)) ==
					WIMLIB_EXTRACT_FLAG_IMAGEMODE)
	{
		/* For full-image extraction, do reparse point fixups by default
		 * if the WIM header says they are enabled.  */
		if (wim->hdr.flags & WIM_HDR_FLAG_RP_FIX)
			extract_flags |= WIMLIB_EXTRACT_FLAG_RPFIX;
	}

	*extract_flags_p = extract_flags;
	return 0;
}

struct append_dentry_ctx {
	struct wim_dentry **dentries;
	size_t num_dentries;
	size_t num_alloc_dentries;
};

static int
append_dentry_cb(struct wim_dentry *dentry, void *_ctx)
{
	struct append_dentry_ctx *ctx = _ctx;

	if (ctx->num_dentries == ctx->num_alloc_dentries) {
		struct wim_dentry **new_dentries;
		size_t new_length;

		new_length = max(ctx->num_alloc_dentries + 8,
				 ctx->num_alloc_dentries * 3 / 2);
		new_dentries = REALLOC(ctx->dentries,
				       new_length * sizeof(ctx->dentries[0]));
		if (new_dentries == NULL)
			return WIMLIB_ERR_NOMEM;
		ctx->dentries = new_dentries;
		ctx->num_alloc_dentries = new_length;
	}
	ctx->dentries[ctx->num_dentries++] = dentry;
	return 0;
}

/* Append dentries matched by a path which can contain wildcard characters.  */
static int
append_matched_dentries(WIMStruct *wim, const tchar *orig_pattern,
			int extract_flags, struct append_dentry_ctx *ctx)
{
	const size_t count_before = ctx->num_dentries;
	tchar *pattern;
	int ret;

	pattern = canonicalize_wim_path(orig_pattern);
	if (!pattern)
		return WIMLIB_ERR_NOMEM;
	ret = expand_path_pattern(wim_get_current_root_dentry(wim), pattern,
				  append_dentry_cb, ctx);
	FREE(pattern);
	if (ret || ctx->num_dentries > count_before)
		return ret;
	if (extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_GLOB) {
		ERROR("No matches for path pattern \"%"TS"\"", orig_pattern);
		return WIMLIB_ERR_PATH_DOES_NOT_EXIST;
	}
	WARNING("No matches for path pattern \"%"TS"\"", orig_pattern);
	return 0;
}

static int
do_wimlib_extract_paths(WIMStruct *wim, int image, const tchar *target,
			const tchar * const *paths, size_t num_paths,
			int extract_flags)
{
	int ret;
	struct wim_dentry **trees;
	size_t num_trees;

	if (wim == NULL || target == NULL || target[0] == T('\0') ||
	    (num_paths != 0 && paths == NULL))
		return WIMLIB_ERR_INVALID_PARAM;

	ret = check_extract_flags(wim, &extract_flags);
	if (ret)
		return ret;

	ret = select_wim_image(wim, image);
	if (ret)
		return ret;

	ret = wim_checksum_unhashed_blobs(wim);
	if (ret)
		return ret;

	if ((extract_flags & (WIMLIB_EXTRACT_FLAG_NTFS |
			      WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE)) ==
	    (WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE))
	{
		ret = mkdir_if_needed(target);
		if (ret)
			return ret;
	}

	if (extract_flags & WIMLIB_EXTRACT_FLAG_GLOB_PATHS) {

		struct append_dentry_ctx append_dentry_ctx = {
			.dentries = NULL,
			.num_dentries = 0,
			.num_alloc_dentries = 0,
		};

		for (size_t i = 0; i < num_paths; i++) {
			ret = append_matched_dentries(wim, paths[i],
						      extract_flags,
						      &append_dentry_ctx);
			if (ret) {
				trees = append_dentry_ctx.dentries;
				goto out_free_trees;
			}
		}
		trees = append_dentry_ctx.dentries;
		num_trees = append_dentry_ctx.num_dentries;
	} else {
		trees = MALLOC(num_paths * sizeof(trees[0]));
		if (trees == NULL)
			return WIMLIB_ERR_NOMEM;

		for (size_t i = 0; i < num_paths; i++) {

			tchar *path = canonicalize_wim_path(paths[i]);
			if (path == NULL) {
				ret = WIMLIB_ERR_NOMEM;
				goto out_free_trees;
			}

			trees[i] = get_dentry(wim, path,
					      WIMLIB_CASE_PLATFORM_DEFAULT);
			FREE(path);
			if (trees[i] == NULL) {
				  ERROR("Path \"%"TS"\" does not exist "
					"in WIM image %d",
					paths[i], wim->current_image);
				  ret = WIMLIB_ERR_PATH_DOES_NOT_EXIST;
				  goto out_free_trees;
			}
		}
		num_trees = num_paths;
	}

	if (num_trees == 0) {
		ret = 0;
		goto out_free_trees;
	}

	ret = extract_trees(wim, trees, num_trees, target, extract_flags);
out_free_trees:
	FREE(trees);
	return ret;
}

static int
extract_single_image(WIMStruct *wim, int image,
		     const tchar *target, int extract_flags)
{
	const tchar *path = WIMLIB_WIM_ROOT_PATH;
	extract_flags |= WIMLIB_EXTRACT_FLAG_IMAGEMODE;
	return do_wimlib_extract_paths(wim, image, target, &path, 1, extract_flags);
}

static const tchar * const filename_forbidden_chars =
#ifdef _WIN32
T("<>:\"/\\|?*");
#else
T("/");
#endif

/* This function checks if it is okay to use a WIM image's name as a directory
 * name.  */
static bool
image_name_ok_as_dir(const tchar *image_name)
{
	return image_name && *image_name &&
		!tstrpbrk(image_name, filename_forbidden_chars) &&
		tstrcmp(image_name, T(".")) &&
		tstrcmp(image_name, T("..")) &&
		tstrlen(image_name) <= 128;
}

/* Extracts all images from the WIM to the directory @target, with the images
 * placed in subdirectories named by their image names. */
static int
extract_all_images(WIMStruct *wim, const tchar *target, int extract_flags)
{
	size_t output_path_len = tstrlen(target);
	tchar* buf = alloca((output_path_len + 1 + 128 + 1) * sizeof(tchar));
	int ret;
	int image;
	const tchar *image_name;

	if (extract_flags & WIMLIB_EXTRACT_FLAG_NTFS) {
		ERROR("Cannot extract multiple images in NTFS extraction mode.");
		return WIMLIB_ERR_INVALID_PARAM;
	}

	ret = mkdir_if_needed(target);
	if (ret)
		return ret;
	tmemcpy(buf, target, output_path_len);
	buf[output_path_len] = OS_PREFERRED_PATH_SEPARATOR;
	for (image = 1; image <= wim->hdr.image_count; image++) {
		image_name = wimlib_get_image_name(wim, image);
		if (image_name_ok_as_dir(image_name)) {
			tstrcpy(buf + output_path_len + 1, image_name);
		} else {
			/* Image name is empty or contains forbidden characters.
			 * Use image number instead. */
			tsnprintf(buf + output_path_len + 1, output_path_len + 1 + 128 + 1, T("%d"), image);
		}
		ret = extract_single_image(wim, image, buf, extract_flags);
		if (ret)
			return ret;
	}
	return 0;
}

static int
do_wimlib_extract_image(WIMStruct *wim, int image, const tchar *target,
			int extract_flags)
{
	if (extract_flags & (WIMLIB_EXTRACT_FLAG_NO_PRESERVE_DIR_STRUCTURE |
			     WIMLIB_EXTRACT_FLAG_TO_STDOUT |
			     WIMLIB_EXTRACT_FLAG_GLOB_PATHS))
		return WIMLIB_ERR_INVALID_PARAM;

	if (image == WIMLIB_ALL_IMAGES)
		return extract_all_images(wim, target, extract_flags);
	else
		return extract_single_image(wim, image, target, extract_flags);
}


/****************************************************************************
 *                          Extraction API                                  *
 ****************************************************************************/

WIMLIBAPI int
wimlib_extract_paths(WIMStruct *wim, int image, const tchar *target,
		     const tchar * const *paths, size_t num_paths,
		     int extract_flags)
{
	if (extract_flags & ~WIMLIB_EXTRACT_MASK_PUBLIC)
		return WIMLIB_ERR_INVALID_PARAM;

	return do_wimlib_extract_paths(wim, image, target, paths, num_paths,
				       extract_flags);
}

WIMLIBAPI int
wimlib_extract_pathlist(WIMStruct *wim, int image, const tchar *target,
			const tchar *path_list_file, int extract_flags)
{
	int ret;
	tchar **paths;
	size_t num_paths;
	void *mem;

	ret = read_path_list_file(path_list_file, &paths, &num_paths, &mem);
	if (ret) {
		ERROR("Failed to read path list file \"%"TS"\"",
		      path_list_file ? path_list_file : T("<stdin>"));
		return ret;
	}

	ret = wimlib_extract_paths(wim, image, target,
				   (const tchar * const *)paths, num_paths,
				   extract_flags);
	FREE(paths);
	FREE(mem);
	return ret;
}

WIMLIBAPI int
wimlib_extract_image_from_pipe_with_progress(int pipe_fd,
					     const tchar *image_num_or_name,
					     const tchar *target,
					     int extract_flags,
					     wimlib_progress_func_t progfunc,
					     void *progctx)
{
	int ret;
	WIMStruct *pwm;
	struct filedes *in_fd;
	int image;
	unsigned i;

	if (extract_flags & ~WIMLIB_EXTRACT_MASK_PUBLIC)
		return WIMLIB_ERR_INVALID_PARAM;

	/* Read the WIM header from the pipe and get a WIMStruct to represent
	 * the pipable WIM.  Caveats:  Unlike getting a WIMStruct with
	 * wimlib_open_wim(), getting a WIMStruct in this way will result in an
	 * empty blob table, no XML data read, and no filename set.  */
	ret = open_wim_as_WIMStruct(&pipe_fd, WIMLIB_OPEN_FLAG_FROM_PIPE, &pwm,
				    progfunc, progctx);
	if (ret)
		return ret;

	/* Sanity check to make sure this is a pipable WIM.  */
	if (pwm->hdr.magic != PWM_MAGIC) {
		ERROR("The WIM being read from file descriptor %d "
		      "is not pipable!", pipe_fd);
		ret = WIMLIB_ERR_NOT_PIPABLE;
		goto out_wimlib_free;
	}

	/* Sanity check to make sure the first part of a pipable split WIM is
	 * sent over the pipe first.  */
	if (pwm->hdr.part_number != 1) {
		ERROR("The first part of the split WIM must be "
		      "sent over the pipe first.");
		ret = WIMLIB_ERR_INVALID_PIPABLE_WIM;
		goto out_wimlib_free;
	}

	in_fd = &pwm->in_fd;
	wimlib_assert(in_fd->offset == WIM_HEADER_DISK_SIZE);

	/* As mentioned, the WIMStruct we created from the pipe does not have
	 * XML data yet.  Fix this by reading the extra copy of the XML data
	 * that directly follows the header in pipable WIMs.  (Note: see
	 * write_pipable_wim() for more details about the format of pipable
	 * WIMs.)  */
	{
		u8 hash[SHA1_HASH_SIZE];

		ret = read_pwm_blob_header(pwm, hash,
					   &pwm->hdr.xml_data_reshdr, NULL);
		if (ret)
			goto out_wimlib_free;

		if (!(pwm->hdr.xml_data_reshdr.flags & WIM_RESHDR_FLAG_METADATA)) {
			ERROR("Expected XML data, but found non-metadata resource.");
			ret = WIMLIB_ERR_INVALID_PIPABLE_WIM;
			goto out_wimlib_free;
		}

		ret = read_wim_xml_data(pwm);
		if (ret)
			goto out_wimlib_free;

		if (xml_get_image_count(pwm->xml_info) != pwm->hdr.image_count) {
			ERROR("Image count in XML data is not the same as in WIM header.");
			ret = WIMLIB_ERR_IMAGE_COUNT;
			goto out_wimlib_free;
		}
	}

	/* Get image index (this may use the XML data that was just read to
	 * resolve an image name).  */
	if (image_num_or_name) {
		image = wimlib_resolve_image(pwm, image_num_or_name);
		if (image == WIMLIB_NO_IMAGE) {
			ERROR("\"%"TS"\" is not a valid image in the pipable WIM!",
			      image_num_or_name);
			ret = WIMLIB_ERR_INVALID_IMAGE;
			goto out_wimlib_free;
		} else if (image == WIMLIB_ALL_IMAGES) {
			ERROR("Applying all images from a pipe is not supported!");
			ret = WIMLIB_ERR_INVALID_IMAGE;
			goto out_wimlib_free;
		}
	} else {
		if (pwm->hdr.image_count != 1) {
			ERROR("No image was specified, but the pipable WIM "
			      "did not contain exactly 1 image");
			ret = WIMLIB_ERR_INVALID_IMAGE;
			goto out_wimlib_free;
		}
		image = 1;
	}

	/* Load the needed metadata resource.  */
	for (i = 1; i <= pwm->hdr.image_count; i++) {
		ret = handle_pwm_metadata_resource(pwm, i, i == image);
		if (ret)
			goto out_wimlib_free;
	}
	/* Extract the image.  */
	extract_flags |= WIMLIB_EXTRACT_FLAG_FROM_PIPE;
	ret = do_wimlib_extract_image(pwm, image, target, extract_flags);
	/* Clean up and return.  */
out_wimlib_free:
	wimlib_free(pwm);
	return ret;
}


WIMLIBAPI int
wimlib_extract_image_from_pipe(int pipe_fd, const tchar *image_num_or_name,
			       const tchar *target, int extract_flags)
{
	return wimlib_extract_image_from_pipe_with_progress(pipe_fd,
							    image_num_or_name,
							    target,
							    extract_flags,
							    NULL,
							    NULL);
}

WIMLIBAPI int
wimlib_extract_image(WIMStruct *wim, int image, const tchar *target,
		     int extract_flags)
{
	if (extract_flags & ~WIMLIB_EXTRACT_MASK_PUBLIC)
		return WIMLIB_ERR_INVALID_PARAM;
	return do_wimlib_extract_image(wim, image, target, extract_flags);
}
