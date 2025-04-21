/*
 * blob_table.c
 *
 * A blob table maps SHA-1 message digests to "blobs", which are nonempty
 * sequences of binary data.  Within a WIM file, blobs are single-instanced.
 *
 * This file also contains code to read and write the corresponding on-disk
 * representation of this table in the WIM file format.
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* for unlink()  */

#include "wimlib/assert.h"
#include "wimlib/bitops.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/metadata.h"
#include "wimlib/ntfs_3g.h"
#include "wimlib/resource.h"
#include "wimlib/unaligned.h"
#include "wimlib/util.h"
#include "wimlib/win32.h"
#include "wimlib/write.h"

/* A hash table mapping SHA-1 message digests to blob descriptors  */
struct blob_table {
	struct hlist_head *array;
	size_t num_blobs;
	size_t mask; /* capacity - 1; capacity is a power of 2  */
};

struct blob_table *
new_blob_table(size_t capacity)
{
	struct blob_table *table;
	struct hlist_head *array;

	capacity = roundup_pow_of_2(capacity);

	table = MALLOC(sizeof(struct blob_table));
	if (table == NULL)
		goto oom;

	array = CALLOC(capacity, sizeof(array[0]));
	if (array == NULL) {
		FREE(table);
		goto oom;
	}

	table->num_blobs = 0;
	table->mask = capacity - 1;
	table->array = array;
	return table;

oom:
	ERROR("Failed to allocate memory for blob table "
	      "with capacity %zu", capacity);
	return NULL;
}

static int
do_free_blob_descriptor(struct blob_descriptor *blob, void *_ignore)
{
	free_blob_descriptor(blob);
	return 0;
}

void
free_blob_table(struct blob_table *table)
{
	if (table) {
		for_blob_in_table(table, do_free_blob_descriptor, NULL);
		FREE(table->array);
		FREE(table);
	}
}

struct blob_descriptor *
new_blob_descriptor(void)
{
	STATIC_ASSERT(BLOB_NONEXISTENT == 0);
	return CALLOC(1, sizeof(struct blob_descriptor));
}

struct blob_descriptor *
clone_blob_descriptor(const struct blob_descriptor *old)
{
	struct blob_descriptor *new;

	new = memdup(old, sizeof(struct blob_descriptor));
	if (new == NULL)
		return NULL;

	switch (new->blob_location) {
	case BLOB_IN_WIM:
		list_add(&new->rdesc_node, &new->rdesc->blob_list);
		break;

	case BLOB_IN_FILE_ON_DISK:
#ifdef WITH_FUSE
	case BLOB_IN_STAGING_FILE:
		STATIC_ASSERT((void*)&old->file_on_disk ==
			      (void*)&old->staging_file_name);
#endif
		new->file_on_disk = TSTRDUP(old->file_on_disk);
		if (new->file_on_disk == NULL)
			goto out_free;
		break;
#ifdef _WIN32
	case BLOB_IN_WINDOWS_FILE:
		new->windows_file = clone_windows_file(old->windows_file);
		break;
#endif
	case BLOB_IN_ATTACHED_BUFFER:
		new->attached_buffer = memdup(old->attached_buffer, old->size);
		if (new->attached_buffer == NULL)
			goto out_free;
		break;
#ifdef WITH_NTFS_3G
	case BLOB_IN_NTFS_VOLUME:
		new->ntfs_loc = clone_ntfs_location(old->ntfs_loc);
		if (!new->ntfs_loc)
			goto out_free;
		break;
#endif
	}
	return new;

out_free:
	free_blob_descriptor(new);
	return NULL;
}

/* Release a blob descriptor from its location, if any, and set its new location
 * to BLOB_NONEXISTENT.  */
void
blob_release_location(struct blob_descriptor *blob)
{
	switch (blob->blob_location) {
	case BLOB_IN_WIM: {
		struct wim_resource_descriptor *rdesc = blob->rdesc;

		list_del(&blob->rdesc_node);
		if (list_empty(&rdesc->blob_list)) {
			wim_decrement_refcnt(rdesc->wim);
			FREE(rdesc);
		}
		break;
	}
	case BLOB_IN_FILE_ON_DISK:
#ifdef WITH_FUSE
	case BLOB_IN_STAGING_FILE:
		STATIC_ASSERT((void*)&blob->file_on_disk ==
			      (void*)&blob->staging_file_name);
#endif
	case BLOB_IN_ATTACHED_BUFFER:
		STATIC_ASSERT(offsetof(struct blob_descriptor, file_on_disk) ==
			offsetof(struct blob_descriptor, attached_buffer));
		FREE(blob->file_on_disk);
		break;
#ifdef _WIN32
	case BLOB_IN_WINDOWS_FILE:
		free_windows_file(blob->windows_file);
		break;
#endif
#ifdef WITH_NTFS_3G
	case BLOB_IN_NTFS_VOLUME:
		free_ntfs_location(blob->ntfs_loc);
		break;
#endif
	}
	blob->blob_location = BLOB_NONEXISTENT;
}

void
free_blob_descriptor(struct blob_descriptor *blob)
{
	if (blob) {
		blob_release_location(blob);
		FREE(blob);
	}
}

/* Should this blob be retained even if it has no references?  */
static bool
should_retain_blob(const struct blob_descriptor *blob)
{
	return blob->blob_location == BLOB_IN_WIM;
}

static void
finalize_blob(struct blob_descriptor *blob)
{
	if (!should_retain_blob(blob))
		free_blob_descriptor(blob);
}

/*
 * Decrements the reference count of the specified blob, which must be either
 * (a) unhashed, or (b) inserted in the specified blob table.
 *
 * If the blob's reference count reaches 0, we may unlink it from @table and
 * free it.  However, we retain blobs with 0 reference count that originated
 * from WIM files (BLOB_IN_WIM).  We do this for two reasons:
 *
 * 1. This prevents information about valid blobs in a WIM file --- blobs which
 *    will continue to be present after appending to the WIM file --- from being
 *    lost merely because we dropped all references to them.
 *
 * 2. Blob reference counts we read from WIM files can't be trusted.  It's
 *    possible that a WIM has reference counts that are too low; WIMGAPI
 *    sometimes creates WIMs where this is the case.  It's also possible that
 *    blobs have been referenced from an external WIM; those blobs can
 *    potentially have any reference count at all, either lower or higher than
 *    would be expected for this WIM ("this WIM" meaning the owner of @table) if
 *    it were a standalone WIM.
 *
 * So we can't take the reference counts too seriously.  But at least, we do
 * recalculate by default when writing a new WIM file.
 */
void
blob_decrement_refcnt(struct blob_descriptor *blob, struct blob_table *table)
{
	blob_subtract_refcnt(blob, table, 1);
}

void
blob_subtract_refcnt(struct blob_descriptor *blob, struct blob_table *table,
		     u32 count)
{
	if (unlikely(blob->refcnt < count)) {
		blob->refcnt = 0; /* See comment above  */
		return;
	}

	blob->refcnt -= count;

	if (blob->refcnt != 0)
		return;

	if (blob->unhashed) {
		list_del(&blob->unhashed_list);
	#ifdef WITH_FUSE
		/* If the blob has been extracted to a staging file for a FUSE
		 * mount, unlink the staging file.  (Note that there still may
		 * be open file descriptors to it.)  */
		if (blob->blob_location == BLOB_IN_STAGING_FILE)
			unlinkat(blob->staging_dir_fd,
				 blob->staging_file_name, 0);
	#endif
	} else {
		if (!should_retain_blob(blob))
			blob_table_unlink(table, blob);
	}

	/* If FUSE mounts are enabled, then don't actually free the blob
	 * descriptor until the last file descriptor to it has been closed.  */
#ifdef WITH_FUSE
	if (blob->num_opened_fds == 0)
#endif
		finalize_blob(blob);
}

#ifdef WITH_FUSE
void
blob_decrement_num_opened_fds(struct blob_descriptor *blob)
{
	wimlib_assert(blob->num_opened_fds != 0);

	if (--blob->num_opened_fds == 0 && blob->refcnt == 0)
		finalize_blob(blob);
}
#endif

static void
blob_table_insert_raw(struct blob_table *table, struct blob_descriptor *blob)
{
	size_t i = blob->hash_short & table->mask;

	hlist_add_head(&blob->hash_list, &table->array[i]);
}

static void
enlarge_blob_table(struct blob_table *table)
{
	size_t old_capacity, new_capacity;
	struct hlist_head *old_array, *new_array;
	struct blob_descriptor *blob;
	struct hlist_node *tmp;
	size_t i;

	old_capacity = table->mask + 1;
	new_capacity = old_capacity * 2;
	new_array = CALLOC(new_capacity, sizeof(struct hlist_head));
	if (new_array == NULL)
		return;
	old_array = table->array;
	table->array = new_array;
	table->mask = new_capacity - 1;

	for (i = 0; i < old_capacity; i++)
		hlist_for_each_entry_safe(blob, tmp, &old_array[i], hash_list)
			blob_table_insert_raw(table, blob);
	FREE(old_array);
}

/* Insert a blob descriptor into the blob table.  */
void
blob_table_insert(struct blob_table *table, struct blob_descriptor *blob)
{
	blob_table_insert_raw(table, blob);
	if (table->num_blobs++ > table->mask)
		enlarge_blob_table(table);
}

/* Unlinks a blob descriptor from the blob table; does not free it.  */
void
blob_table_unlink(struct blob_table *table, struct blob_descriptor *blob)
{
	wimlib_assert(!blob->unhashed);
	wimlib_assert(table->num_blobs != 0);

	hlist_del(&blob->hash_list);
	table->num_blobs--;
}

/* Given a SHA-1 message digest, return the corresponding blob descriptor from
 * the specified blob table, or NULL if there is none.  */
struct blob_descriptor *
lookup_blob(const struct blob_table *table, const u8 *hash)
{
	size_t i;
	struct blob_descriptor *blob;

	i = load_size_t_unaligned(hash) & table->mask;
	hlist_for_each_entry(blob, &table->array[i], hash_list)
		if (hashes_equal(hash, blob->hash))
			return blob;
	return NULL;
}

/* Call a function on all blob descriptors in the specified blob table.  Stop
 * early and return nonzero if any call to the function returns nonzero.  */
int
for_blob_in_table(struct blob_table *table,
		  int (*visitor)(struct blob_descriptor *, void *), void *arg)
{
	struct blob_descriptor *blob;
	struct hlist_node *tmp;
	int ret;

	for (size_t i = 0; i <= table->mask; i++) {
		hlist_for_each_entry_safe(blob, tmp, &table->array[i],
					  hash_list)
		{
			ret = visitor(blob, arg);
			if (ret)
				return ret;
		}
	}
	return 0;
}

/*
 * This is a qsort() callback that sorts blobs into an order optimized for
 * reading.  Sorting is done primarily by blob location, then secondarily by a
 * location-dependent order.  For example, blobs in WIM resources are sorted
 * such that the underlying WIM files will be read sequentially.  This is
 * especially important for WIM files containing solid resources.
 */
int
cmp_blobs_by_sequential_order(const void *p1, const void *p2)
{
	const struct blob_descriptor *blob1, *blob2;
	int v;
	WIMStruct *wim1, *wim2;

	blob1 = *(const struct blob_descriptor**)p1;
	blob2 = *(const struct blob_descriptor**)p2;

	v = (int)blob1->blob_location - (int)blob2->blob_location;

	/* Different locations?  Note: "unsafe compaction mode" requires that
	 * blobs in WIMs sort before all others.  For the logic here to ensure
	 * this, BLOB_IN_WIM must have the lowest value among all defined
	 * blob_locations.  Statically verify that the enum values haven't
	 * changed.  */
	STATIC_ASSERT(BLOB_NONEXISTENT == 0 && BLOB_IN_WIM == 1);
	if (v)
		return v;

	switch (blob1->blob_location) {
	case BLOB_IN_WIM:
		wim1 = blob1->rdesc->wim;
		wim2 = blob2->rdesc->wim;

		/* Different WIM files?  */
		if (wim1 != wim2) {

			/* Resources from the WIM file currently being compacted
			 * (if any) must always sort first.  */
			v = (int)wim2->being_compacted - (int)wim1->being_compacted;
			if (v)
				return v;

			/* Different split WIMs?  */
			v = cmp_guids(wim1->hdr.guid, wim2->hdr.guid);
			if (v)
				return v;

			/* Different part numbers in the same split WIM?  */
			v = (int)wim1->hdr.part_number - (int)wim2->hdr.part_number;
			if (v)
				return v;

			/* Probably two WIMStructs for the same on-disk file.
			 * Just sort by pointer.  */
			return wim1 < wim2 ? -1 : 1;
		}

		/* Same WIM file  */

		/* Sort by increasing resource offset  */
		if (blob1->rdesc->offset_in_wim != blob2->rdesc->offset_in_wim)
			return cmp_u64(blob1->rdesc->offset_in_wim,
				       blob2->rdesc->offset_in_wim);

		/* The blobs are in the same solid resource.  Sort by increasing
		 * offset in the resource.  */
		return cmp_u64(blob1->offset_in_res, blob2->offset_in_res);

	case BLOB_IN_FILE_ON_DISK:
#ifdef WITH_FUSE
	case BLOB_IN_STAGING_FILE:
#endif
		/* Compare files by path: just a heuristic that will place files
		 * in the same directory next to each other.  */
		return tstrcmp(blob1->file_on_disk, blob2->file_on_disk);
#ifdef _WIN32
	case BLOB_IN_WINDOWS_FILE:
		return cmp_windows_files(blob1->windows_file, blob2->windows_file);
#endif
#ifdef WITH_NTFS_3G
	case BLOB_IN_NTFS_VOLUME:
		return cmp_ntfs_locations(blob1->ntfs_loc, blob2->ntfs_loc);
#endif
	default:
		/* No additional sorting order defined for this resource
		 * location (e.g. BLOB_IN_ATTACHED_BUFFER); simply compare
		 * everything equal to each other.  */
		return 0;
	}
}

int
sort_blob_list(struct list_head *blob_list, size_t list_head_offset,
	       int (*compar)(const void *, const void*))
{
	struct list_head *cur;
	struct blob_descriptor **array;
	size_t i;
	size_t array_size;
	size_t num_blobs = 0;

	list_for_each(cur, blob_list)
		num_blobs++;

	if (num_blobs <= 1)
		return 0;

	array_size = num_blobs * sizeof(array[0]);
	array = MALLOC(array_size);
	if (array == NULL)
		return WIMLIB_ERR_NOMEM;

	cur = blob_list->next;
	for (i = 0; i < num_blobs; i++) {
		array[i] = (struct blob_descriptor*)((u8*)cur - list_head_offset);
		cur = cur->next;
	}

	qsort(array, num_blobs, sizeof(array[0]), compar);

	INIT_LIST_HEAD(blob_list);
	for (i = 0; i < num_blobs; i++) {
		list_add_tail((struct list_head*)
			       ((u8*)array[i] + list_head_offset), blob_list);
	}
	FREE(array);
	return 0;
}

/* Sort the specified list of blobs in an order optimized for sequential
 * reading.  */
int
sort_blob_list_by_sequential_order(struct list_head *blob_list,
				   size_t list_head_offset)
{
	return sort_blob_list(blob_list, list_head_offset,
			      cmp_blobs_by_sequential_order);
}

static int
add_blob_to_array(struct blob_descriptor *blob, void *_pp)
{
	struct blob_descriptor ***pp = _pp;
	*(*pp)++ = blob;
	return 0;
}

/* Iterate through the blob descriptors in the specified blob table in an order
 * optimized for sequential reading.  */
int
for_blob_in_table_sorted_by_sequential_order(struct blob_table *table,
					     int (*visitor)(struct blob_descriptor *, void *),
					     void *arg)
{
	struct blob_descriptor **blob_array, **p;
	size_t num_blobs = table->num_blobs;
	int ret;

	blob_array = MALLOC(num_blobs * sizeof(blob_array[0]));
	if (!blob_array)
		return WIMLIB_ERR_NOMEM;
	p = blob_array;
	for_blob_in_table(table, add_blob_to_array, &p);

	wimlib_assert(p == blob_array + num_blobs);

	qsort(blob_array, num_blobs, sizeof(blob_array[0]),
	      cmp_blobs_by_sequential_order);
	ret = 0;
	for (size_t i = 0; i < num_blobs; i++) {
		ret = visitor(blob_array[i], arg);
		if (ret)
			break;
	}
	FREE(blob_array);
	return ret;
}

/* On-disk format of a blob descriptor in a WIM file.
 *
 * Note: if the WIM file contains solid resource(s), then this structure is
 * sometimes overloaded to describe a "resource" rather than a "blob".  See the
 * code for details.  */
PRAGMA_BEGIN_PACKED
struct blob_descriptor_disk {

	/* Size, offset, and flags of the blob.  */
	struct wim_reshdr_disk reshdr;

	/* Which part of the split WIM this blob is in; indexed from 1. */
	le16 part_number;

	/* Reference count of this blob over all WIM images.  (But see comment
	 * above blob_decrement_refcnt().)  */
	le32 refcnt;

	/* SHA-1 message digest of the uncompressed data of this blob, or all
	 * zeroes if this blob is of zero length.  */
	u8 hash[SHA1_HASH_SIZE];
} __attribute__((packed));
PRAGMA_END_PACKED

/* Given a nonempty run of consecutive blob descriptors with the SOLID flag set,
 * count how many specify resources (as opposed to blobs within those
 * resources).
 *
 * Returns the resulting count.  */
static size_t
count_solid_resources(const struct blob_descriptor_disk *entries, size_t max)
{
	size_t count = 0;
	do {
		struct wim_reshdr reshdr;

		get_wim_reshdr(&(entries++)->reshdr, &reshdr);

		if (!(reshdr.flags & WIM_RESHDR_FLAG_SOLID)) {
			/* Run was terminated by a stand-alone blob entry.  */
			break;
		}

		if (reshdr.uncompressed_size == SOLID_RESOURCE_MAGIC_NUMBER) {
			/* This is a resource entry.  */
			count++;
		}
	} while (--max);
	return count;
}

/*
 * Given a run of consecutive blob descriptors with the SOLID flag set and
 * having @num_rdescs resource entries, load resource information from them into
 * the resource descriptors in the @rdescs array.
 *
 * Returns 0 on success, or a nonzero error code on failure.
 */
static int
do_load_solid_info(WIMStruct *wim, struct wim_resource_descriptor **rdescs,
		   size_t num_rdescs,
		   const struct blob_descriptor_disk *entries)
{
	for (size_t i = 0; i < num_rdescs; i++) {
		struct wim_reshdr reshdr;
		struct alt_chunk_table_header_disk hdr;
		struct wim_resource_descriptor *rdesc;
		int ret;

		/* Advance to next resource entry.  */

		do {
			get_wim_reshdr(&(entries++)->reshdr, &reshdr);
		} while (reshdr.uncompressed_size != SOLID_RESOURCE_MAGIC_NUMBER);

		rdesc = rdescs[i];

		wim_reshdr_to_desc(&reshdr, wim, rdesc);

		/* For solid resources, the uncompressed size, compression type,
		 * and chunk size are stored in the resource itself, not in the
		 * blob table.  */

		ret = full_pread(&wim->in_fd, &hdr,
				 sizeof(hdr), reshdr.offset_in_wim);
		if (ret) {
			ERROR("Failed to read header of solid resource "
			      "(offset_in_wim=%"PRIu64")",
			      reshdr.offset_in_wim);
			return ret;
		}

		rdesc->uncompressed_size = le64_to_cpu(hdr.res_usize);

		/* Compression format numbers must be the same as in
		 * WIMGAPI to be compatible here.  */
		STATIC_ASSERT(WIMLIB_COMPRESSION_TYPE_NONE == 0);
		STATIC_ASSERT(WIMLIB_COMPRESSION_TYPE_XPRESS == 1);
		STATIC_ASSERT(WIMLIB_COMPRESSION_TYPE_LZX == 2);
		STATIC_ASSERT(WIMLIB_COMPRESSION_TYPE_LZMS == 3);
		rdesc->compression_type = le32_to_cpu(hdr.compression_format);
		rdesc->chunk_size = le32_to_cpu(hdr.chunk_size);
	}
	return 0;
}

/*
 * Given a nonempty run of consecutive blob descriptors with the SOLID flag set,
 * allocate a 'struct wim_resource_descriptor' for each resource within that
 * run.
 *
 * Returns 0 on success, or a nonzero error code on failure.
 * Returns the pointers and count in *rdescs_ret and *num_rdescs_ret.
 */
static int
load_solid_info(WIMStruct *wim,
		const struct blob_descriptor_disk *entries,
		size_t num_remaining_entries,
		struct wim_resource_descriptor ***rdescs_ret,
		size_t *num_rdescs_ret)
{
	size_t num_rdescs;
	struct wim_resource_descriptor **rdescs;
	size_t i;
	int ret;

	num_rdescs = count_solid_resources(entries, num_remaining_entries);
	rdescs = CALLOC(num_rdescs, sizeof(rdescs[0]));
	if (!rdescs)
		return WIMLIB_ERR_NOMEM;

	for (i = 0; i < num_rdescs; i++) {
		rdescs[i] = MALLOC(sizeof(struct wim_resource_descriptor));
		if (!rdescs[i]) {
			ret = WIMLIB_ERR_NOMEM;
			goto out_free_rdescs;
		}
	}

	ret = do_load_solid_info(wim, rdescs, num_rdescs, entries);
	if (ret)
		goto out_free_rdescs;

	wim->refcnt += num_rdescs;

	*rdescs_ret = rdescs;
	*num_rdescs_ret = num_rdescs;
	return 0;

out_free_rdescs:
	for (i = 0; i < num_rdescs; i++)
		FREE(rdescs[i]);
	FREE(rdescs);
	return ret;
}

/* Given a 'struct blob_descriptor' allocated for an on-disk blob descriptor
 * with the SOLID flag set, try to assign it to resource in the current solid
 * run.  */
static int
assign_blob_to_solid_resource(const struct wim_reshdr *reshdr,
			      struct blob_descriptor *blob,
			      struct wim_resource_descriptor **rdescs,
			      size_t num_rdescs)
{
	u64 offset = reshdr->offset_in_wim;

	/* XXX: This linear search will be slow in the degenerate case where the
	 * number of solid resources in the run is huge.  */
	blob->size = reshdr->size_in_wim;
	for (size_t i = 0; i < num_rdescs; i++) {
		if (offset + blob->size <= rdescs[i]->uncompressed_size) {
			blob_set_is_located_in_wim_resource(blob, rdescs[i], offset);
			return 0;
		}
		offset -= rdescs[i]->uncompressed_size;
	}
	ERROR("blob could not be assigned to a solid resource");
	return WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;
}

static void
free_solid_rdescs(struct wim_resource_descriptor **rdescs, size_t num_rdescs)
{
	if (rdescs) {
		for (size_t i = 0; i < num_rdescs; i++) {
			if (list_empty(&rdescs[i]->blob_list)) {
				rdescs[i]->wim->refcnt--;
				FREE(rdescs[i]);
			}
		}
		FREE(rdescs);
	}
}

static int
cmp_blobs_by_offset_in_res(const void *p1, const void *p2)
{
	const struct blob_descriptor *blob1, *blob2;

	blob1 = *(const struct blob_descriptor**)p1;
	blob2 = *(const struct blob_descriptor**)p2;

	return cmp_u64(blob1->offset_in_res, blob2->offset_in_res);
}

/* Validate the size and location of a WIM resource.  */
static int
validate_resource(struct wim_resource_descriptor *rdesc)
{
	struct blob_descriptor *blob;
	bool out_of_order;
	u64 expected_next_offset;
	int ret;

	/* Verify that the resource itself has a valid offset and size.  */
	if (rdesc->offset_in_wim + rdesc->size_in_wim < rdesc->size_in_wim)
		goto invalid_due_to_overflow;

	/* Verify that each blob in the resource has a valid offset and size.
	 */
	expected_next_offset = 0;
	out_of_order = false;
	list_for_each_entry(blob, &rdesc->blob_list, rdesc_node) {
		if (blob->offset_in_res + blob->size < blob->size ||
		    blob->offset_in_res + blob->size > rdesc->uncompressed_size)
			goto invalid_due_to_overflow;

		if (blob->offset_in_res >= expected_next_offset)
			expected_next_offset = blob->offset_in_res + blob->size;
		else
			out_of_order = true;
	}

	/* If the blobs were not located at strictly increasing positions (not
	 * allowing for overlap), sort them.  Then make sure that none overlap.
	 */
	if (out_of_order) {
		ret = sort_blob_list(&rdesc->blob_list,
				     offsetof(struct blob_descriptor,
					      rdesc_node),
				     cmp_blobs_by_offset_in_res);
		if (ret)
			return ret;

		expected_next_offset = 0;
		list_for_each_entry(blob, &rdesc->blob_list, rdesc_node) {
			if (blob->offset_in_res >= expected_next_offset)
				expected_next_offset = blob->offset_in_res + blob->size;
			else
				goto invalid_due_to_overlap;
		}
	}

	return 0;

invalid_due_to_overflow:
	ERROR("Invalid blob table (offset overflow)");
	return WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;

invalid_due_to_overlap:
	ERROR("Invalid blob table (blobs in solid resource overlap)");
	return WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;
}

static int
finish_solid_rdescs(struct wim_resource_descriptor **rdescs, size_t num_rdescs)
{
	int ret = 0;
	for (size_t i = 0; i < num_rdescs; i++) {
		ret = validate_resource(rdescs[i]);
		if (ret)
			break;
	}
	free_solid_rdescs(rdescs, num_rdescs);
	return ret;
}

/*
 * read_blob_table() -
 *
 * Read the blob table from a WIM file.  Usually, each entry in this table
 * describes a "blob", or equivalently a "resource", that the WIM file contains,
 * along with its location and SHA-1 message digest.  Descriptors for
 * non-metadata blobs will be saved in the in-memory blob table
 * (wim->blob_table), whereas descriptors for metadata blobs will be saved in a
 * special location per-image (the wim->image_metadata array).
 *
 * However, in WIM_VERSION_SOLID (3584) WIMs, a resource may contain multiple
 * blobs that are compressed together.  Such a resource is called a "solid
 * resource".  Solid resources are still described in the on-disk "blob table",
 * although the format is not the most logical.  A consecutive sequence of
 * entries that all have flag WIM_RESHDR_FLAG_SOLID (0x10) set is a "solid run".
 * A solid run describes a set of solid resources, each of which contains a set
 * of blobs.  In a solid run, a 'struct wim_reshdr_disk' with 'uncompressed_size
 * = SOLID_RESOURCE_MAGIC_NUMBER (0x100000000)' specifies a solid resource,
 * whereas any other 'struct wim_reshdr_disk' specifies a blob within a solid
 * resource.  There are some oddities in how we need to determine which solid
 * resource a blob is actually in; see the code for details.
 *
 * Possible return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY
 *	WIMLIB_ERR_NOMEM
 *
 *	Or an error code caused by failure to read the blob table from the WIM
 *	file.
 */
int
read_blob_table(WIMStruct *wim)
{
	int ret;
	size_t num_entries;
	void *buf = NULL;
	struct blob_table *table = NULL;
	struct blob_descriptor *cur_blob = NULL;
	size_t num_duplicate_blobs = 0;
	size_t num_empty_blobs = 0;
	size_t num_wrong_part_blobs = 0;
	u32 image_index = 0;
	struct wim_resource_descriptor **cur_solid_rdescs = NULL;
	size_t cur_num_solid_rdescs = 0;

	/* Calculate the number of entries in the blob table.  */
	num_entries = wim->hdr.blob_table_reshdr.uncompressed_size /
		      sizeof(struct blob_descriptor_disk);

	/* Read the blob table into a buffer.  */
	ret = wim_reshdr_to_data(&wim->hdr.blob_table_reshdr, wim, &buf);
	if (ret)
		goto out;

	/* Allocate a hash table to map SHA-1 message digests into blob
	 * descriptors.  This is the in-memory "blob table".  */
	table = new_blob_table(num_entries);
	if (!table)
		goto oom;

	/* Allocate and initialize blob descriptors from the raw blob table
	 * buffer.  */
	for (size_t i = 0; i < num_entries; i++) {
		const struct blob_descriptor_disk *disk_entry =
			&((const struct blob_descriptor_disk*)buf)[i];
		struct wim_reshdr reshdr;
		u16 part_number;

		/* Get the resource header  */
		get_wim_reshdr(&disk_entry->reshdr, &reshdr);

		/* Ignore SOLID flag if it isn't supposed to be used in this WIM
		 * version.  */
		if (wim->hdr.wim_version == WIM_VERSION_DEFAULT)
			reshdr.flags &= ~WIM_RESHDR_FLAG_SOLID;

		/* Allocate a new 'struct blob_descriptor'.  */
		cur_blob = new_blob_descriptor();
		if (!cur_blob)
			goto oom;

		/* Get the part number, reference count, and hash.  */
		part_number = le16_to_cpu(disk_entry->part_number);
		cur_blob->refcnt = le32_to_cpu(disk_entry->refcnt);
		copy_hash(cur_blob->hash, disk_entry->hash);

		if (reshdr.flags & WIM_RESHDR_FLAG_SOLID) {

			/* SOLID entry  */

			if (!cur_solid_rdescs) {
				/* Starting new run  */
				ret = load_solid_info(wim, disk_entry,
						      num_entries - i,
						      &cur_solid_rdescs,
						      &cur_num_solid_rdescs);
				if (ret)
					goto out;
			}

			if (reshdr.uncompressed_size == SOLID_RESOURCE_MAGIC_NUMBER) {
				/* Resource entry, not blob entry  */
				goto free_cur_blob_and_continue;
			}

			/* Blob entry  */

			ret = assign_blob_to_solid_resource(&reshdr,
							    cur_blob,
							    cur_solid_rdescs,
							    cur_num_solid_rdescs);
			if (ret)
				goto out;

		} else {
			/* Normal blob/resource entry; SOLID not set.  */

			struct wim_resource_descriptor *rdesc;

			if (unlikely(cur_solid_rdescs)) {
				/* This entry terminated a solid run.  */
				ret = finish_solid_rdescs(cur_solid_rdescs,
							  cur_num_solid_rdescs);
				cur_solid_rdescs = NULL;
				if (ret)
					goto out;
			}

			if (unlikely(!(reshdr.flags & WIM_RESHDR_FLAG_COMPRESSED) &&
				     (reshdr.size_in_wim != reshdr.uncompressed_size)))
			{
				ERROR("Uncompressed resource has "
				      "size_in_wim != uncompressed_size");
				ret = WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;
				goto out;
			}

			/* Set up a resource descriptor for this blob.  */

			rdesc = MALLOC(sizeof(struct wim_resource_descriptor));
			if (!rdesc)
				goto oom;

			wim_reshdr_to_desc_and_blob(&reshdr, wim, rdesc, cur_blob);
			wim->refcnt++;
		}

		/* cur_blob is now a blob bound to a resource.  */

		/* Ignore entries with all zeroes in the hash field.  */
		if (unlikely(is_zero_hash(cur_blob->hash)))
			goto free_cur_blob_and_continue;

		/* Verify that the blob has nonzero size.  */
		if (unlikely(cur_blob->size == 0)) {
			num_empty_blobs++;
			goto free_cur_blob_and_continue;
		}

		/* Verify that the part number matches that of the underlying
		 * WIM file.  */
		if (unlikely(part_number != wim->hdr.part_number)) {
			num_wrong_part_blobs++;
			goto free_cur_blob_and_continue;
		}

		if (reshdr.flags & WIM_RESHDR_FLAG_METADATA) {
			/* Blob table entry for a metadata resource.  */

			/* Metadata entries with no references must be ignored.
			 * See, for example, the WinPE WIMs from the WAIK v2.1.
			 */
			if (cur_blob->refcnt == 0)
				goto free_cur_blob_and_continue;

			if (cur_blob->refcnt != 1) {
				/* We don't currently support this case due to
				 * the complications of multiple images sharing
				 * the same metadata resource or a metadata
				 * resource also being referenced by files.  */
				ERROR("Found metadata resource with refcnt != 1");
				ret = WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;
				goto out;
			}

			if (reshdr.flags & WIM_RESHDR_FLAG_SOLID) {
				ERROR("Image metadata in solid resources "
				      "is unsupported.");
				ret = WIMLIB_ERR_INVALID_LOOKUP_TABLE_ENTRY;
				goto out;
			}

			if (wim->hdr.part_number != 1) {
				WARNING("Ignoring metadata resource found in a "
					"non-first part of the split WIM");
				goto free_cur_blob_and_continue;
			}

			/* The number of entries in the blob table with
			 * WIM_RESHDR_FLAG_METADATA set should be the same as
			 * the image_count field in the WIM header.  */
			if (image_index == wim->hdr.image_count) {
				WARNING("Found more metadata resources than images");
				goto free_cur_blob_and_continue;
			}

			/* Notice very carefully:  We are assigning the metadata
			 * resources to images in the same order in which their
			 * blob table entries occur on disk.  (This is also the
			 * behavior of Microsoft's software.)  In particular,
			 * this overrides the actual locations of the metadata
			 * resources themselves in the WIM file as well as any
			 * information written in the XML data.  */
			wim->image_metadata[image_index] = new_unloaded_image_metadata(cur_blob);
			if (!wim->image_metadata[image_index])
				goto oom;
			image_index++;
		} else {
			/* Blob table entry for a non-metadata blob.  */

			/* Ignore this blob if it's a duplicate.  */
			if (lookup_blob(table, cur_blob->hash)) {
				num_duplicate_blobs++;
				goto free_cur_blob_and_continue;
			}

			/* Insert the blob into the in-memory blob table, keyed
			 * by its SHA-1 message digest.  */
			blob_table_insert(table, cur_blob);
		}

		continue;

	free_cur_blob_and_continue:
		if (cur_solid_rdescs &&
		    cur_blob->blob_location == BLOB_IN_WIM)
			blob_unset_is_located_in_wim_resource(cur_blob);
		free_blob_descriptor(cur_blob);
	}
	cur_blob = NULL;

	if (cur_solid_rdescs) {
		/* End of blob table terminated a solid run.  */
		ret = finish_solid_rdescs(cur_solid_rdescs, cur_num_solid_rdescs);
		cur_solid_rdescs = NULL;
		if (ret)
			goto out;
	}

	if (wim->hdr.part_number == 1 && image_index != wim->hdr.image_count) {
		WARNING("Could not find metadata resources for all images");
		wim->hdr.image_count = image_index;
	}

	if (num_duplicate_blobs > 0)
		WARNING("Ignoring %zu duplicate blobs", num_duplicate_blobs);

	if (num_empty_blobs > 0)
		WARNING("Ignoring %zu empty blobs", num_empty_blobs);

	if (num_wrong_part_blobs > 0) {
		WARNING("Ignoring %zu blobs with wrong part number",
			num_wrong_part_blobs);
	}

	wim->blob_table = table;
	ret = 0;
	goto out_free_buf;

oom:
	ERROR("Not enough memory to read blob table!");
	ret = WIMLIB_ERR_NOMEM;
out:
	free_solid_rdescs(cur_solid_rdescs, cur_num_solid_rdescs);
	free_blob_descriptor(cur_blob);
	free_blob_table(table);
out_free_buf:
	FREE(buf);
	return ret;
}

static void
write_blob_descriptor(struct blob_descriptor_disk *disk_entry,
		      const struct wim_reshdr *out_reshdr,
		      u16 part_number, u32 refcnt, const u8 *hash)
{
	put_wim_reshdr(out_reshdr, &disk_entry->reshdr);
	disk_entry->part_number = cpu_to_le16(part_number);
	disk_entry->refcnt = cpu_to_le32(refcnt);
	copy_hash(disk_entry->hash, hash);
}

/* Note: the list of blob descriptors must be sorted so that all entries for the
 * same solid resource are consecutive.  In addition, blob descriptors for
 * metadata resources must be in the same order as the indices of the underlying
 * images.  */
int
write_blob_table_from_blob_list(struct list_head *blob_list,
				struct filedes *out_fd,
				u16 part_number,
				struct wim_reshdr *out_reshdr,
				int write_resource_flags)
{
	size_t table_size;
	struct blob_descriptor *blob;
	struct blob_descriptor_disk *table_buf;
	struct blob_descriptor_disk *table_buf_ptr;
	int ret;
	u64 prev_res_offset_in_wim = ~0ULL;
	u64 prev_uncompressed_size;
	u64 logical_offset;

	table_size = 0;
	list_for_each_entry(blob, blob_list, blob_table_list) {
		table_size += sizeof(struct blob_descriptor_disk);

		if (blob->out_reshdr.flags & WIM_RESHDR_FLAG_SOLID &&
		    blob->out_res_offset_in_wim != prev_res_offset_in_wim)
		{
			table_size += sizeof(struct blob_descriptor_disk);
			prev_res_offset_in_wim = blob->out_res_offset_in_wim;
		}
	}

	table_buf = MALLOC(table_size);
	if (table_buf == NULL) {
		ERROR("Failed to allocate %zu bytes for temporary blob table",
		      table_size);
		return WIMLIB_ERR_NOMEM;
	}
	table_buf_ptr = table_buf;

	prev_res_offset_in_wim = ~0ULL;
	prev_uncompressed_size = 0;
	logical_offset = 0;
	list_for_each_entry(blob, blob_list, blob_table_list) {
		if (blob->out_reshdr.flags & WIM_RESHDR_FLAG_SOLID) {
			struct wim_reshdr tmp_reshdr = { 0 };

			/* Eww.  When WIMGAPI sees multiple solid resources, it
			 * expects the offsets to be adjusted as if there were
			 * really only one solid resource.  */

			if (blob->out_res_offset_in_wim != prev_res_offset_in_wim) {
				/* Put the resource entry for solid resource  */
				tmp_reshdr.offset_in_wim = blob->out_res_offset_in_wim;
				tmp_reshdr.size_in_wim = blob->out_res_size_in_wim;
				tmp_reshdr.uncompressed_size = SOLID_RESOURCE_MAGIC_NUMBER;
				tmp_reshdr.flags = WIM_RESHDR_FLAG_SOLID;

				write_blob_descriptor(table_buf_ptr++, &tmp_reshdr,
						      part_number, 1, zero_hash);

				logical_offset += prev_uncompressed_size;

				prev_res_offset_in_wim = blob->out_res_offset_in_wim;
				prev_uncompressed_size = blob->out_res_uncompressed_size;
			}
			tmp_reshdr = blob->out_reshdr;
			tmp_reshdr.offset_in_wim += logical_offset;
			write_blob_descriptor(table_buf_ptr++, &tmp_reshdr,
					      part_number, blob->out_refcnt, blob->hash);
		} else {
			write_blob_descriptor(table_buf_ptr++, &blob->out_reshdr,
					      part_number, blob->out_refcnt, blob->hash);
		}

	}
	wimlib_assert((u8*)table_buf_ptr - (u8*)table_buf == table_size);

	/* Write the blob table uncompressed.  Although wimlib can handle a
	 * compressed blob table, MS software cannot.  */
	ret = write_wim_resource_from_buffer(table_buf,
					     table_size,
					     true,
					     out_fd,
					     WIMLIB_COMPRESSION_TYPE_NONE,
					     0,
					     out_reshdr,
					     NULL,
					     write_resource_flags);
	FREE(table_buf);
	return ret;
}

/* Allocate a blob descriptor for the contents of the buffer, or re-use an
 * existing descriptor in @blob_table for an identical blob.  */
struct blob_descriptor *
new_blob_from_data_buffer(const void *buffer, size_t size,
			  struct blob_table *blob_table)
{
	u8 hash[SHA1_HASH_SIZE];
	struct blob_descriptor *blob;
	void *buffer_copy;

	sha1(buffer, size, hash);

	blob = lookup_blob(blob_table, hash);
	if (blob)
		return blob;

	blob = new_blob_descriptor();
	if (!blob)
		return NULL;

	buffer_copy = memdup(buffer, size);
	if (!buffer_copy) {
		free_blob_descriptor(blob);
		return NULL;
	}
	blob_set_is_located_in_attached_buffer(blob, buffer_copy, size);
	copy_hash(blob->hash, hash);
	blob_table_insert(blob_table, blob);
	return blob;
}

struct blob_descriptor *
after_blob_hashed(struct blob_descriptor *blob,
		  struct blob_descriptor **back_ptr,
		  struct blob_table *blob_table, struct wim_inode *inode)
{
	struct blob_descriptor *duplicate_blob;

	list_del(&blob->unhashed_list);
	blob->unhashed = 0;

	/* Look for a duplicate blob  */
	duplicate_blob = lookup_blob(blob_table, blob->hash);
	if (duplicate_blob) {
		/* We have a duplicate blob.  Transfer the reference counts from
		 * this blob to the duplicate and update the reference to this
		 * blob (from a stream) to point to the duplicate.  The caller
		 * is responsible for freeing @blob if needed.  */
		if (duplicate_blob->size != blob->size) {
			tchar hash_str[SHA1_HASH_STRING_LEN];

			sprint_hash(blob->hash, hash_str);
			WARNING("SHA-1 collision at \"%"TS"\"\n"
				"          (hash=%"TS", size=%"PRIu64", other_size=%"PRIu64").\n"
				"          File will be corrupted!",
				inode_any_full_path(inode), hash_str,
				blob->size, duplicate_blob->size);
		}
		duplicate_blob->refcnt += blob->refcnt;
		blob->refcnt = 0;
		*back_ptr = duplicate_blob;
		return duplicate_blob;
	} else {
		/* No duplicate blob, so we need to insert this blob into the
		 * blob table and treat it as a hashed blob.  */
		blob_table_insert(blob_table, blob);
		return blob;
	}
}

/*
 * Calculate the SHA-1 message digest of a blob and move its descriptor from the
 * list of unhashed blobs to the blob table, possibly joining it with an
 * identical blob.
 *
 * @blob:
 *	The blob to hash
 * @blob_table:
 *	The blob table in which the blob needs to be indexed
 * @blob_ret:
 *	On success, a pointer to the resulting blob descriptor is written to
 *	this location.  This will be the same as @blob if it was inserted into
 *	the blob table, or different if a duplicate blob was found.
 *
 * Returns 0 on success; nonzero if there is an error reading the blob data.
 */
int
hash_unhashed_blob(struct blob_descriptor *blob, struct blob_table *blob_table,
		   struct blob_descriptor **blob_ret)
{
	struct blob_descriptor **back_ptr;
	struct wim_inode *inode;
	int ret;

	back_ptr = retrieve_pointer_to_unhashed_blob(blob);
	inode = blob->back_inode;

	ret = sha1_blob(blob);
	if (ret)
		return ret;

	*blob_ret = after_blob_hashed(blob, back_ptr, blob_table, inode);
	return 0;
}

void
blob_to_wimlib_resource_entry(const struct blob_descriptor *blob,
			      struct wimlib_resource_entry *wentry)
{
	memset(wentry, 0, sizeof(*wentry));

	wentry->uncompressed_size = blob->size;
	if (blob->blob_location == BLOB_IN_WIM) {
		unsigned res_flags = blob->rdesc->flags;

		wentry->part_number = blob->rdesc->wim->hdr.part_number;
		if (res_flags & WIM_RESHDR_FLAG_SOLID) {
			wentry->offset = blob->offset_in_res;
		} else {
			wentry->compressed_size = blob->rdesc->size_in_wim;
			wentry->offset = blob->rdesc->offset_in_wim;
		}
		wentry->raw_resource_offset_in_wim = blob->rdesc->offset_in_wim;
		wentry->raw_resource_compressed_size = blob->rdesc->size_in_wim;
		wentry->raw_resource_uncompressed_size = blob->rdesc->uncompressed_size;

		wentry->is_compressed = (res_flags & WIM_RESHDR_FLAG_COMPRESSED) != 0;
		wentry->is_free = (res_flags & WIM_RESHDR_FLAG_FREE) != 0;
		wentry->is_spanned = (res_flags & WIM_RESHDR_FLAG_SPANNED) != 0;
		wentry->packed = (res_flags & WIM_RESHDR_FLAG_SOLID) != 0;
	}
	if (!blob->unhashed)
		copy_hash(wentry->sha1_hash, blob->hash);
	wentry->reference_count = blob->refcnt;
	wentry->is_metadata = blob->is_metadata;
}

struct iterate_blob_context {
	wimlib_iterate_lookup_table_callback_t cb;
	void *user_ctx;
};

static int
do_iterate_blob(struct blob_descriptor *blob, void *_ctx)
{
	struct iterate_blob_context *ctx = _ctx;
	struct wimlib_resource_entry entry;

	blob_to_wimlib_resource_entry(blob, &entry);
	return (*ctx->cb)(&entry, ctx->user_ctx);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_iterate_lookup_table(WIMStruct *wim, int flags,
			    wimlib_iterate_lookup_table_callback_t cb,
			    void *user_ctx)
{
	if (flags != 0)
		return WIMLIB_ERR_INVALID_PARAM;

	struct iterate_blob_context ctx = {
		.cb = cb,
		.user_ctx = user_ctx,
	};
	if (wim_has_metadata(wim)) {
		int ret;
		for (int i = 0; i < wim->hdr.image_count; i++) {
			struct blob_descriptor *blob;
			struct wim_image_metadata *imd = wim->image_metadata[i];

			ret = do_iterate_blob(imd->metadata_blob, &ctx);
			if (ret)
				return ret;
			image_for_each_unhashed_blob(blob, imd) {
				ret = do_iterate_blob(blob, &ctx);
				if (ret)
					return ret;
			}
		}
	}
	return for_blob_in_table(wim->blob_table, do_iterate_blob, &ctx);
}
