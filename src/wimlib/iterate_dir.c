/*
 * iterate_dir.c
 *
 * Iterate through files in a WIM image.
 * This is the stable API; internal code can just use for_dentry_in_tree().
 */

/*
 * Copyright (C) 2013-2016 Eric Biggers
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

#include "wimlib.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/metadata.h"
#include "wimlib/object_id.h"
#include "wimlib/paths.h"
#include "wimlib/security.h"
#include "wimlib/timestamp.h"
#include "wimlib/unix_data.h"
#include "wimlib/util.h"
#include "wimlib/wim.h"

static int
stream_to_wimlib_stream_entry(const struct wim_inode *inode,
			      const struct wim_inode_stream *strm,
			      struct wimlib_stream_entry *wstream,
			      const struct blob_table *blob_table,
			      int flags)
{
	const struct blob_descriptor *blob;
	const u8 *hash;

	if (stream_is_named(strm)) {
		int ret;

		ret = utf16le_get_tstr(strm->stream_name,
				       utf16le_len_bytes(strm->stream_name),
				       &wstream->stream_name, NULL);
		if (ret)
			return ret;
	}

	blob = stream_blob(strm, blob_table);
	if (blob) {
		blob_to_wimlib_resource_entry(blob, &wstream->resource);
	} else if (!is_zero_hash((hash = stream_hash(strm)))) {
		if (flags & WIMLIB_ITERATE_DIR_TREE_FLAG_RESOURCES_NEEDED)
			return blob_not_found_error(inode, hash);
		copy_hash(wstream->resource.sha1_hash, hash);
		wstream->resource.is_missing = 1;
	}
	return 0;
}

static int
get_default_stream_type(const struct wim_inode *inode)
{
	if (inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED)
		return STREAM_TYPE_EFSRPC_RAW_DATA;
	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		return STREAM_TYPE_REPARSE_POINT;
	return STREAM_TYPE_DATA;
}

static int
init_wimlib_dentry(struct wimlib_dir_entry *wdentry, struct wim_dentry *dentry,
		   WIMStruct *wim, int flags)
{
	int ret;
	const struct wim_inode *inode = dentry->d_inode;
	const struct wim_inode_stream *strm;
	struct wimlib_unix_data unix_data;
	const void *object_id;
	u32 object_id_len;

	ret = utf16le_get_tstr(dentry->d_name, dentry->d_name_nbytes,
			       &wdentry->filename, NULL);
	if (ret)
		return ret;

	ret = utf16le_get_tstr(dentry->d_short_name, dentry->d_short_name_nbytes,
			       &wdentry->dos_name, NULL);
	if (ret)
		return ret;

	ret = calculate_dentry_full_path(dentry);
	if (ret)
		return ret;
	wdentry->full_path = dentry->d_full_path;

	for (struct wim_dentry *d = dentry; !dentry_is_root(d); d = d->d_parent)
		wdentry->depth++;

	if (inode_has_security_descriptor(inode)) {
		struct wim_security_data *sd;

		sd = wim_get_current_security_data(wim);
		wdentry->security_descriptor = sd->descriptors[inode->i_security_id];
		wdentry->security_descriptor_size = sd->sizes[inode->i_security_id];
	}
	wdentry->reparse_tag = inode->i_reparse_tag;
	wdentry->num_links = inode->i_nlink;
	wdentry->attributes = inode->i_attributes;
	wdentry->hard_link_group_id = inode->i_ino;

	wim_timestamp_to_wimlib_timespec(inode->i_creation_time,
					 &wdentry->creation_time,
					 &wdentry->creation_time_high);

	wim_timestamp_to_wimlib_timespec(inode->i_last_write_time,
					 &wdentry->last_write_time,
					 &wdentry->last_write_time_high);

	wim_timestamp_to_wimlib_timespec(inode->i_last_access_time,
					 &wdentry->last_access_time,
					 &wdentry->last_access_time_high);

	if (inode_get_unix_data(inode, &unix_data)) {
		wdentry->unix_uid = unix_data.uid;
		wdentry->unix_gid = unix_data.gid;
		wdentry->unix_mode = unix_data.mode;
		wdentry->unix_rdev = unix_data.rdev;
	}
	object_id = inode_get_object_id(inode, &object_id_len);
	if (unlikely(object_id != NULL)) {
		memcpy(&wdentry->object_id, object_id,
		       min(object_id_len, sizeof(wdentry->object_id)));
	}

	strm = inode_get_unnamed_stream(inode, get_default_stream_type(inode));
	if (strm) {
		ret = stream_to_wimlib_stream_entry(inode, strm,
						    &wdentry->streams[0],
						    wim->blob_table, flags);
		if (ret)
			return ret;
	}

	for (unsigned i = 0; i < inode->i_num_streams; i++) {

		strm = &inode->i_streams[i];

		if (!stream_is_named_data_stream(strm))
			continue;

		wdentry->num_named_streams++;

		ret = stream_to_wimlib_stream_entry(inode, strm,
						    &wdentry->streams[
							wdentry->num_named_streams],
						    wim->blob_table, flags);
		if (ret)
			return ret;
	}
	return 0;
}

static void
free_wimlib_dentry(struct wimlib_dir_entry *wdentry)
{
	utf16le_put_tstr(wdentry->filename);
	utf16le_put_tstr(wdentry->dos_name);
	for (unsigned i = 1; i <= wdentry->num_named_streams; i++)
		utf16le_put_tstr(wdentry->streams[i].stream_name);
	FREE(wdentry);
}

static int
do_iterate_dir_tree(WIMStruct *wim,
		    struct wim_dentry *dentry, int flags,
		    wimlib_iterate_dir_tree_callback_t cb,
		    void *user_ctx)
{
	struct wimlib_dir_entry *wdentry;
	int ret = WIMLIB_ERR_NOMEM;


	wdentry = CALLOC(1, sizeof(struct wimlib_dir_entry) +
				  (1 + dentry->d_inode->i_num_streams) *
					sizeof(struct wimlib_stream_entry));
	if (wdentry == NULL)
		goto out;

	ret = init_wimlib_dentry(wdentry, dentry, wim, flags);
	if (ret)
		goto out_free_wimlib_dentry;

	if (!(flags & WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN)) {
		ret = (*cb)(wdentry, user_ctx);
		if (ret)
			goto out_free_wimlib_dentry;
	}

	if (flags & (WIMLIB_ITERATE_DIR_TREE_FLAG_RECURSIVE |
		     WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN))
	{
		struct wim_dentry *child;

		ret = 0;
		for_dentry_child(child, dentry) {
			ret = do_iterate_dir_tree(wim, child,
						  flags & ~WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN,
						  cb, user_ctx);
			if (ret)
				break;
		}
	}
out_free_wimlib_dentry:
	FREE(dentry->d_full_path);
	dentry->d_full_path = NULL;
	free_wimlib_dentry(wdentry);
out:
	return ret;
}

struct image_iterate_dir_tree_ctx {
	const tchar *path;
	int flags;
	wimlib_iterate_dir_tree_callback_t cb;
	void *user_ctx;
};


static int
image_do_iterate_dir_tree(WIMStruct *wim)
{
	struct image_iterate_dir_tree_ctx *ctx = wim->private;
	struct wim_dentry *dentry;

	dentry = get_dentry(wim, ctx->path, WIMLIB_CASE_PLATFORM_DEFAULT);
	if (dentry == NULL)
		return WIMLIB_ERR_PATH_DOES_NOT_EXIST;
	return do_iterate_dir_tree(wim, dentry, ctx->flags, ctx->cb, ctx->user_ctx);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_iterate_dir_tree(WIMStruct *wim, int image, const tchar *_path,
			int flags,
			wimlib_iterate_dir_tree_callback_t cb, void *user_ctx)
{
	tchar *path;
	int ret;

	if (flags & ~(WIMLIB_ITERATE_DIR_TREE_FLAG_RECURSIVE |
		      WIMLIB_ITERATE_DIR_TREE_FLAG_CHILDREN |
		      WIMLIB_ITERATE_DIR_TREE_FLAG_RESOURCES_NEEDED))
		return WIMLIB_ERR_INVALID_PARAM;

	path = canonicalize_wim_path(_path);
	if (path == NULL)
		return WIMLIB_ERR_NOMEM;
	struct image_iterate_dir_tree_ctx ctx = {
		.path = path,
		.flags = flags,
		.cb = cb,
		.user_ctx = user_ctx,
	};
	wim->private = &ctx;
	ret = for_image(wim, image, image_do_iterate_dir_tree);
	FREE(path);
	return ret;
}
