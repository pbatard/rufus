/*
 * inode.c
 *
 * Functions that operate on WIM inodes.
 *
 * See dentry.c for a description of the relationship between WIM dentries and
 * WIM inodes.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>

#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/error.h"
#include "wimlib/inode.h"
#include "wimlib/timestamp.h"

/*
 * The 'stream_name' field of unnamed streams always points to this array, which
 * is an empty UTF-16 string.
 */
const utf16lechar NO_STREAM_NAME[1];

/* Allocate a new inode and associate the specified dentry with it.  */
struct wim_inode *
new_inode(struct wim_dentry *dentry, bool set_timestamps)
{
	struct wim_inode *inode;

	inode = CALLOC(1, sizeof(struct wim_inode));
	if (!inode)
		return NULL;

	inode->i_security_id = -1;
	/*inode->i_nlink = 0;*/
	inode->i_rp_flags = WIM_RP_FLAG_NOT_FIXED;
	INIT_HLIST_HEAD(&inode->i_alias_list);
	inode->i_streams = inode->i_embedded_streams;
	if (set_timestamps) {
		u64 now = now_as_wim_timestamp();
		inode->i_creation_time = now;
		inode->i_last_access_time = now;
		inode->i_last_write_time = now;
	}
	d_associate(dentry, inode);
	return inode;
}

static inline void
destroy_stream(struct wim_inode_stream *strm)
{
	if (strm->stream_name != NO_STREAM_NAME)
		FREE(strm->stream_name);
}

static void
free_inode(struct wim_inode *inode)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++)
		destroy_stream(&inode->i_streams[i]);
	if (inode->i_streams != inode->i_embedded_streams)
		FREE(inode->i_streams);
	if (inode->i_extra)
		FREE(inode->i_extra);
	if (!hlist_unhashed(&inode->i_hlist_node))
		hlist_del(&inode->i_hlist_node);
	FREE(inode);
}

static inline void
free_inode_if_unneeded(struct wim_inode *inode)
{
	if (inode->i_nlink)
		return;
#ifdef WITH_FUSE
	if (inode->i_num_opened_fds)
		return;
#endif
	free_inode(inode);
}

/* Associate a dentry with the specified inode.  */
void
d_associate(struct wim_dentry *dentry, struct wim_inode *inode)
{
	wimlib_assert(!dentry->d_inode);

	hlist_add_head(&dentry->d_alias_node, &inode->i_alias_list);
	dentry->d_inode = inode;
	inode->i_nlink++;
}

/* Disassociate a dentry from its inode, if any.  Following this, free the inode
 * if it is no longer in use.  */
void
d_disassociate(struct wim_dentry *dentry)
{
	struct wim_inode *inode = dentry->d_inode;

	if (unlikely(!inode))
		return;

	wimlib_assert(inode->i_nlink > 0);

	hlist_del(&dentry->d_alias_node);
	dentry->d_inode = NULL;
	inode->i_nlink--;

	free_inode_if_unneeded(inode);
}

#ifdef WITH_FUSE
void
inode_dec_num_opened_fds(struct wim_inode *inode)
{
	wimlib_assert(inode->i_num_opened_fds > 0);

	if (--inode->i_num_opened_fds == 0) {
		/* The last file descriptor to this inode was closed.  */
		FREE(inode->i_fds);
		inode->i_fds = NULL;
		inode->i_num_allocated_fds = 0;

		free_inode_if_unneeded(inode);
	}
}
#endif

/*
 * Retrieve a stream of an inode.
 *
 * @inode
 *	The inode from which the stream is desired
 * @stream_type
 *	The type of the stream desired
 * @stream_name
 *	The name of the stream desired as a null-terminated UTF-16LE string, or
 *	NO_STREAM_NAME if an unnamed stream is desired
 *
 * Returns a pointer to the stream if found, otherwise NULL.
 */
struct wim_inode_stream *
inode_get_stream(const struct wim_inode *inode, int stream_type,
		 const utf16lechar *stream_name)
{
	if (stream_name == NO_STREAM_NAME)  /* Optimization  */
		return inode_get_unnamed_stream(inode, stream_type);

	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];
		if (strm->stream_type == stream_type &&
		    !cmp_utf16le_strings_z(strm->stream_name, stream_name,
					   default_ignore_case))
		{
			return strm;
		}
	}
	return NULL;
}

/*
 * This is equivalent to inode_get_stream(inode, stream_type, NO_STREAM_NAME),
 * but this optimizes for the unnamed case by not doing full string comparisons.
 */
struct wim_inode_stream *
inode_get_unnamed_stream(const struct wim_inode *inode, int stream_type)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];
		if (strm->stream_type == stream_type &&
		    strm->stream_name == NO_STREAM_NAME)
		{
			return strm;
		}
	}
	return NULL;
}


static void
inode_set_stream_blob(struct wim_inode *inode, struct wim_inode_stream *strm,
		      struct blob_descriptor *new_blob)
{
	strm->_stream_blob = new_blob;
	strm->stream_resolved = 1;
	if (new_blob)
		new_blob->refcnt += inode->i_nlink;
}

static void
inode_unset_stream_blob(struct wim_inode *inode, struct wim_inode_stream *strm,
			struct blob_table *blob_table)
{
	struct blob_descriptor *old_blob;

	old_blob = stream_blob(strm, blob_table);
	if (old_blob)
		blob_subtract_refcnt(old_blob, blob_table, inode->i_nlink);
	strm->_stream_blob = NULL;
	strm->stream_resolved = 1;
}

/*
 * Replace the blob associated with the specified stream.
 *
 * @inode
 *	The inode containing @strm
 * @strm
 *	The stream whose data needs to be replaced
 * @new_blob
 *	The new blob descriptor to assign
 * @blob_table
 *	Pointer to the blob table in which data blobs are being indexed
 */
void
inode_replace_stream_blob(struct wim_inode *inode,
			  struct wim_inode_stream *strm,
			  struct blob_descriptor *new_blob,
			  struct blob_table *blob_table)
{
	inode_unset_stream_blob(inode, strm, blob_table);
	inode_set_stream_blob(inode, strm, new_blob);
}

/*
 * Add a new stream to the specified inode.
 *
 * @inode
 *	The inode to which to add the stream
 * @stream_type
 *	The type of the stream being added
 * @stream_name
 *	The name of the stream being added as a null-terminated UTF-16LE string,
 *	or NO_STREAM_NAME if the stream is unnamed
 * @blob
 *	The blob that the new stream will initially reference, or NULL
 *
 * Returns a pointer to the new stream, or NULL with errno set if it could not
 * be added.
 */
struct wim_inode_stream *
inode_add_stream(struct wim_inode *inode, int stream_type,
		 const utf16lechar *stream_name, struct blob_descriptor *blob)
{
	if (inode->i_num_streams >= 0xFFFF) {
		ERROR("Inode has too many streams! Path=\"%"TS"\"",
		      inode_any_full_path(inode));
		errno = EFBIG;
		return NULL;
	}

	struct wim_inode_stream *streams;
	struct wim_inode_stream *new_strm;

	if (inode->i_streams == inode->i_embedded_streams) {
		if (inode->i_num_streams < ARRAY_LEN(inode->i_embedded_streams)) {
			streams = inode->i_embedded_streams;
		} else {
			streams = MALLOC((inode->i_num_streams + 1) *
						sizeof(inode->i_streams[0]));
			if (!streams)
				return NULL;
			memcpy(streams, inode->i_streams,
			       (inode->i_num_streams *
					sizeof(inode->i_streams[0])));
			inode->i_streams = streams;
		}
	} else {
		streams = REALLOC(inode->i_streams,
				  (inode->i_num_streams + 1) *
					sizeof(inode->i_streams[0]));
		if (!streams)
			return NULL;
		inode->i_streams = streams;
	}
	new_strm = &streams[inode->i_num_streams];

	memset(new_strm, 0, sizeof(*new_strm));

	new_strm->stream_type = stream_type;
	if (!*stream_name) {
		/* Unnamed stream  */
		new_strm->stream_name = (utf16lechar *)NO_STREAM_NAME;
	} else {
		/* Named stream  */
		new_strm->stream_name = utf16le_dup(stream_name);
		if (!new_strm->stream_name)
			return NULL;
	}

	new_strm->stream_id = inode->i_next_stream_id++;

	inode_set_stream_blob(inode, new_strm, blob);

	inode->i_num_streams++;

	return new_strm;
}

/*
 * Replace the data of the specified stream.
 *
 * @inode
 *	The inode containing @strm
 * @strm
 *	The stream whose data needs to be replaced
 * @data
 *	The buffer of data to assign to the stream
 * @size
 *	Size of the @data buffer, in bytes
 * @blob_table
 *	Pointer to the blob table in which data blobs are being indexed
 *
 * Returns true if successful; false with errno set if unsuccessful.
 */
bool
inode_replace_stream_data(struct wim_inode *inode,
			  struct wim_inode_stream *strm,
			  const void *data, size_t size,
			  struct blob_table *blob_table)
{
	struct blob_descriptor *new_blob = NULL;

	if (size) {
		new_blob = new_blob_from_data_buffer(data, size, blob_table);
		if (!new_blob)
			return false;
	}

	inode_replace_stream_blob(inode, strm, new_blob, blob_table);
	return true;
}

/*
 * Add a new stream to the specified inode and assign it the specified data.
 *
 * @inode
 *	The inode to which to add the stream
 * @stream_type
 *	The type of the stream being added
 * @stream_name
 *	The name of the stream being added as a null-terminated UTF-16LE string,
 *	or NO_STREAM_NAME if the stream is unnamed
 * @data
 *	The buffer of data to assign to the new stream
 * @size
 *	Size of the @data buffer, in bytes
 * @blob_table
 *	Pointer to the blob table in which data blobs are being indexed
 *
 * Returns true if successful; false with errno set if unsuccessful.
 */
bool
inode_add_stream_with_data(struct wim_inode *inode,
			   int stream_type, const utf16lechar *stream_name,
			   const void *data, size_t size,
			   struct blob_table *blob_table)
{
	struct wim_inode_stream *strm;
	struct blob_descriptor *blob = NULL;

	strm = inode_add_stream(inode, stream_type, stream_name, NULL);
	if (!strm)
		return false;

	if (size) {
		blob = new_blob_from_data_buffer(data, size, blob_table);
		if (unlikely(!blob)) {
			inode_remove_stream(inode, strm, blob_table);
			return false;
		}
	}

	inode_set_stream_blob(inode, strm, blob);
	return true;
}

/*
 * Remove a stream from the specified inode.
 *
 * This handles releasing the references to the blob descriptor, if any.
 */
void
inode_remove_stream(struct wim_inode *inode, struct wim_inode_stream *strm,
		    struct blob_table *blob_table)
{
	unsigned idx = strm - inode->i_streams;

	wimlib_assert(idx < inode->i_num_streams);

	inode_unset_stream_blob(inode, strm, blob_table);

	destroy_stream(strm);

	memmove(strm, strm + 1,
		(inode->i_num_streams - idx - 1) * sizeof(inode->i_streams[0]));
	inode->i_num_streams--;
}

/* Returns true iff the specified inode has at least one named data stream.  */
bool
inode_has_named_data_stream(const struct wim_inode *inode)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++)
		if (stream_is_named_data_stream(&inode->i_streams[i]))
			return true;
	return false;
}

/*
 * Resolve an inode's streams.
 *
 * For each stream, this replaces the SHA-1 message digest of the blob data with
 * a pointer to the 'struct blob_descriptor' for the blob.  Blob descriptors are
 * looked up in @table.
 *
 * If @force is %false:
 *	If any of the needed blobs do not exist in @table, return
 *	WIMLIB_ERR_RESOURCE_NOT_FOUND.
 * If @force is %true:
 *	If any of the needed blobs do not exist in @table, allocate new blob
 *	descriptors for them and insert them into @table.  This does not, of
 *	course, cause the data of these blobs to magically exist, but this is
 *	needed by the code for extraction from a pipe.
 *
 * Returns 0 on success; WIMLIB_ERR_NOMEM if out of memory; or
 * WIMLIB_ERR_RESOURCE_NOT_FOUND if @force is %false and at least one blob
 * referenced by the inode was missing.
 */
int
inode_resolve_streams(struct wim_inode *inode, struct blob_table *table,
		      bool force)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];

		if (strm->stream_resolved)
			continue;

		const u8 *hash = stream_hash(strm);
		struct blob_descriptor *blob = NULL;

		if (!is_zero_hash(hash)) {
			blob = lookup_blob(table, hash);
			if (!blob) {
				if (!force)
					return blob_not_found_error(inode, hash);
				blob = new_blob_descriptor();
				if (!blob)
					return WIMLIB_ERR_NOMEM;
				copy_hash(blob->hash, hash);
				blob_table_insert(table, blob);
			}
		}
		strm->_stream_blob = blob;
		strm->stream_resolved = 1;
	}
	return 0;
}

int
blob_not_found_error(const struct wim_inode *inode, const u8 *hash)
{
	if (wimlib_print_errors) {
		tchar hashstr[SHA1_HASH_STRING_LEN];

		sprint_hash(hash, hashstr);

		ERROR("\"%"TS"\": blob not found\n"
		      "        SHA-1 message digest of missing blob:\n"
		      "        %"TS"",
		      inode_any_full_path(inode), hashstr);
	}
	return WIMLIB_ERR_RESOURCE_NOT_FOUND;
}

/*
 * Return the blob descriptor for the specified stream, or NULL if the stream is
 * empty or its blob is not available in @table.
 */
struct blob_descriptor *
stream_blob(const struct wim_inode_stream *strm, const struct blob_table *table)
{
	if (strm->stream_resolved)
		return strm->_stream_blob;
	else
		return lookup_blob(table, strm->_stream_hash);
}

/*
 * Return the SHA-1 message digest of the data of the specified stream, or a
 * void SHA-1 of all zeroes if the specified stream is empty, or NULL if the
 * specified stream is unhashed.  (Most callers ensure the stream cannot be
 * unhashed.)
 */
const u8 *
stream_hash(const struct wim_inode_stream *strm)
{
	if (!strm->stream_resolved)
		return strm->_stream_hash;

	if (!strm->_stream_blob)
		return zero_hash;

	if (strm->_stream_blob->unhashed)
		return NULL;

	return strm->_stream_blob->hash;
}

/*
 * Return the blob descriptor for the unnamed data stream of the inode, or NULL
 * if the inode does not have an unnamed data stream, the inode's unnamed data
 * stream is empty, or the blob for the inode's unnamed data stream is not
 * available in @blob_table.
 */
struct blob_descriptor *
inode_get_blob_for_unnamed_data_stream(const struct wim_inode *inode,
				       const struct blob_table *blob_table)
{
	const struct wim_inode_stream *strm;

	strm = inode_get_unnamed_data_stream(inode);
	if (!strm)
		return NULL;

	return stream_blob(strm, blob_table);
}

/* Like inode_get_blob_for_unnamed_data_stream(), but assumes the unnamed data
 * stream is resolved.  */
struct blob_descriptor *
inode_get_blob_for_unnamed_data_stream_resolved(const struct wim_inode *inode)
{
	const struct wim_inode_stream *strm;

	strm = inode_get_unnamed_data_stream(inode);
	if (!strm)
		return NULL;

	return stream_blob_resolved(strm);
}

/*
 * Return the SHA-1 message digest of the unnamed data stream of the inode, or a
 * void SHA-1 of all zeroes if the inode does not have an unnamed data stream or
 * if the inode's unnamed data stream is empty, or NULL if the inode's unnamed
 * data stream is unhashed.  (Most callers ensure the stream cannot be
 * unhashed.)
 */
const u8 *
inode_get_hash_of_unnamed_data_stream(const struct wim_inode *inode)
{
	const struct wim_inode_stream *strm;

	strm = inode_get_unnamed_data_stream(inode);
	if (!strm)
		return zero_hash;

	return stream_hash(strm);
}

/* Acquire another reference to each blob referenced by this inode.  This is
 * necessary when creating a hard link to this inode.
 *
 * All streams of the inode must be resolved.  */
void
inode_ref_blobs(struct wim_inode *inode)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct blob_descriptor *blob;

		blob = stream_blob_resolved(&inode->i_streams[i]);
		if (blob)
			blob->refcnt++;
	}
}

/* Release a reference to each blob referenced by this inode.  This is necessary
 * when deleting a hard link to this inode.  */
void
inode_unref_blobs(struct wim_inode *inode, struct blob_table *blob_table)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct blob_descriptor *blob;

		blob = stream_blob(&inode->i_streams[i], blob_table);
		if (blob)
			blob_decrement_refcnt(blob, blob_table);
	}
}

/*
 * Given a blob descriptor, return a pointer to the pointer contained in the
 * stream that references it.
 *
 * This is only possible for "unhashed" blobs, which are guaranteed to have only
 * one referencing stream, and that reference is guaranteed to be in a resolved
 * stream.  (It can't be in an unresolved stream, since that would imply the
 * hash is known!)
 */
struct blob_descriptor **
retrieve_pointer_to_unhashed_blob(struct blob_descriptor *blob)
{
	wimlib_assert(blob->unhashed);

	struct wim_inode *inode = blob->back_inode;
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		if (inode->i_streams[i].stream_id == blob->back_stream_id) {
			wimlib_assert(inode->i_streams[i]._stream_blob == blob);
			return &inode->i_streams[i]._stream_blob;
		}
	}

	wimlib_assert(0);
	return NULL;
}
