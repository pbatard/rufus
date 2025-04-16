/*
 * solid.c
 *
 * Heuristic sorting of blobs to optimize solid compression.
 */

/*
 * Copyright (C) 2015 Eric Biggers
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

#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/metadata.h"
#include "wimlib/paths.h"
#include "wimlib/solid.h"
#include "wimlib/unaligned.h"

static const utf16lechar *
get_extension(const utf16lechar *name, size_t nbytes)
{
	const utf16lechar *p = name + (nbytes / sizeof(utf16lechar));
	for (;;) {
		if (p == name)
			return NULL;
		if (*(p - 1) == cpu_to_le16('/') || *(p - 1) == cpu_to_le16('\\'))
			return NULL;
		if (*(p - 1) == cpu_to_le16('.'))
			return p;
		p--;
	}
}

/*
 * Sort order for solid compression:
 *
 * 1. Blobs without sort names
 *	- sorted by sequential order
 * 2. Blobs with sort names:
 *    a. Blobs whose sort name does not have an extension
 *	  - sorted by sort name
 *    b. Blobs whose sort name has an extension
 *        - sorted primarily by extension (case insensitive),
 *	    secondarily by sort name (case insensitive)
 */
static int
cmp_blobs_by_solid_sort_name(const void *p1, const void *p2)
{
	const struct blob_descriptor *blob1, *blob2;

	blob1 = *(const struct blob_descriptor **)p1;
	blob2 = *(const struct blob_descriptor **)p2;

	if (blob1->solid_sort_name) {
		if (!blob2->solid_sort_name)
			return 1;
		const utf16lechar *extension1 = get_extension(blob1->solid_sort_name,
							      blob1->solid_sort_name_nbytes);
		const utf16lechar *extension2 = get_extension(blob2->solid_sort_name,
							      blob2->solid_sort_name_nbytes);
		if (extension1) {
			if (!extension2)
				return 1;
			int res = cmp_utf16le_strings_z(extension1,
							extension2,
							true); /* case insensitive */
			if (res)
				return res;
		} else {
			if (extension2)
				return -1;
		}
		int res = cmp_utf16le_strings(blob1->solid_sort_name,
					      blob1->solid_sort_name_nbytes / sizeof(utf16lechar),
					      blob2->solid_sort_name,
					      blob2->solid_sort_name_nbytes / sizeof(utf16lechar),
					      true); /* case insensitive */
		if (res)
			return res;
	} else {
		if (blob2->solid_sort_name)
			return -1;
	}
	return cmp_blobs_by_sequential_order(p1, p2);
}

static void
blob_set_solid_sort_name_from_inode(struct blob_descriptor *blob,
				    const struct wim_inode *inode)
{
	const struct wim_dentry *dentry;
	const utf16lechar *best_name = NULL;
	size_t best_name_nbytes = SIZE_MAX;

	if (blob->solid_sort_name) /* Sort name already set?  */
		return;

	/* If this file has multiple names, choose the shortest one.  */
	inode_for_each_dentry(dentry, inode) {
		if (dentry->d_name_nbytes < best_name_nbytes) {
			best_name = dentry->d_name;
			best_name_nbytes = dentry->d_name_nbytes;
		}
	}
	blob->solid_sort_name = utf16le_dupz(best_name, best_name_nbytes);
	blob->solid_sort_name_nbytes = best_name_nbytes;
}

struct temp_blob_table {
	struct hlist_head *table;
	size_t capacity;
};

static int
dentry_fill_in_solid_sort_names(struct wim_dentry *dentry, void *_blob_table)
{
	const struct temp_blob_table *blob_table = _blob_table;
	const struct wim_inode *inode = dentry->d_inode;
	const u8 *hash;
	struct hlist_head *head;
	struct blob_descriptor *blob;

	hash = inode_get_hash_of_unnamed_data_stream(inode);
	if (!hash) /* unhashed? */
		return 0;
	head = &blob_table->table[load_size_t_unaligned(hash) %
				  blob_table->capacity];
	hlist_for_each_entry(blob, head, hash_list_2) {
		if (hashes_equal(hash, blob->hash)) {
			blob_set_solid_sort_name_from_inode(blob, inode);
			break;
		}
	}
	return 0;
}

static int
image_fill_in_solid_sort_names(WIMStruct *wim)
{
	return for_dentry_in_tree(wim_get_current_root_dentry(wim),
				  dentry_fill_in_solid_sort_names,
				  wim->private);
}

int
sort_blob_list_for_solid_compression(struct list_head *blob_list)
{
	size_t num_blobs = 0;
	struct temp_blob_table blob_table;
	WIMStruct *wims[128];
	int num_wims = 0;
	struct blob_descriptor *blob;
	int ret;

	/* Count the number of blobs to be written.  */
	list_for_each_entry(blob, blob_list, write_blobs_list)
		num_blobs++;

	/* Allocate a temporary hash table for mapping blob hash => blob  */
	blob_table.capacity = num_blobs;
	blob_table.table = CALLOC(blob_table.capacity,
				  sizeof(blob_table.table[0]));
	if (!blob_table.table)
		return WIMLIB_ERR_NOMEM;

	/*
	 * For each blob to be written:
	 * - Reset the sort name
	 * - If it's in non-solid WIM resource, then save the WIMStruct.
	 * - If it's in a file on disk, then set its sort name from that.
	 */
	list_for_each_entry(blob, blob_list, write_blobs_list) {
		blob->solid_sort_name = NULL;
		blob->solid_sort_name_nbytes = 0;
		switch (blob->blob_location) {
		case BLOB_IN_WIM:
			if (blob->size != blob->rdesc->uncompressed_size)
				continue;
			for (int i = 0; i < num_wims; i++)
				if (blob->rdesc->wim == wims[i])
					goto found_wim;
			if (num_wims >= ARRAY_LEN(wims))
				continue;
			wims[num_wims++] = blob->rdesc->wim;
		found_wim:
			hlist_add_head(&blob->hash_list_2,
				       &blob_table.table[load_size_t_unaligned(blob->hash) %
							 blob_table.capacity]);
			break;
		case BLOB_IN_FILE_ON_DISK:
	#ifdef _WIN32
		case BLOB_IN_WINDOWS_FILE:
	#endif
			blob_set_solid_sort_name_from_inode(blob, blob->file_inode);
			break;
		default:
			break;
		}
	}

	/* For each WIMStruct that was found, search for dentry references to
	 * each blob and fill in the sort name this way.  This is useful e.g.
	 * when exporting a solid WIM file from a non-solid WIM file.  */
	for (int i = 0; i < num_wims; i++) {
		if (!wim_has_metadata(wims[i]))
			continue;
		wims[i]->private = &blob_table;
		ret = for_image(wims[i], WIMLIB_ALL_IMAGES,
				image_fill_in_solid_sort_names);
		if (ret)
			goto out;
		deselect_current_wim_image(wims[i]);
	}

	ret = sort_blob_list(blob_list,
			     offsetof(struct blob_descriptor, write_blobs_list),
			     cmp_blobs_by_solid_sort_name);

out:
	list_for_each_entry(blob, blob_list, write_blobs_list)
		FREE(blob->solid_sort_name);
	FREE(blob_table.table);
	return ret;
}
