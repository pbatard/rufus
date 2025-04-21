/*
 * security.c
 *
 * Read and write the per-WIM-image table of security descriptors.
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

#include "wimlib/assert.h"
#include "wimlib/avl_tree.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/security.h"
#include "wimlib/sha1.h"
#include "wimlib/util.h"

PRAGMA_BEGIN_PACKED
struct wim_security_data_disk {
	le32 total_length;
	le32 num_entries;
	le64 sizes[];
} __attribute__((packed));
PRAGMA_END_PACKED

struct wim_security_data *
new_wim_security_data(void)
{
	return CALLOC(1, sizeof(struct wim_security_data));
}

/*
 * Reads the security data from the metadata resource of a WIM image.
 *
 * @buf
 *	Buffer containing an uncompressed WIM metadata resource.
 * @buf_len
 *	Length of the uncompressed metadata resource, in bytes.
 * @sd_ret
 *	On success, a pointer to the resulting security data structure will be
 *	returned here.
 *
 * Note: There is no `offset' argument because the security data is located at
 * the beginning of the metadata resource.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_INVALID_METADATA_RESOURCE
 *	WIMLIB_ERR_NOMEM
 */
int
read_wim_security_data(const u8 *buf, size_t buf_len,
		       struct wim_security_data **sd_ret)
{
	struct wim_security_data *sd;
	int ret;
	u64 total_len;
	u64 sizes_size;
	u64 size_no_descriptors;
	const struct wim_security_data_disk *sd_disk;
	const u8 *p;

	if (buf_len < 8)
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

	sd = new_wim_security_data();
	if (!sd)
		goto out_of_memory;

	sd_disk = (const struct wim_security_data_disk *)buf;
	sd->total_length = ALIGN(le32_to_cpu(sd_disk->total_length), 8);
	sd->num_entries = le32_to_cpu(sd_disk->num_entries);

	/* Length field of 0 is a special case that really means length
	 * of 8. */
	if (sd->total_length == 0)
		sd->total_length = 8;

	/* The security_id field of each dentry is a signed 32-bit integer, so
	 * the possible indices into the security descriptors table are 0
	 * through 0x7fffffff.  Which means 0x80000000 security descriptors
	 * maximum.  Not like you should ever have anywhere close to that many
	 * security descriptors! */
	if (sd->num_entries > 0x80000000)
		goto out_invalid_sd;

	/* Verify the listed total length of the security data is big enough to
	 * include the sizes array, verify that the file data is big enough to
	 * include it as well, then allocate the array of sizes.
	 *
	 * Note: The total length of the security data must fit in a 32-bit
	 * integer, even though each security descriptor size is a 64-bit
	 * integer.  This is stupid, and we need to be careful not to actually
	 * let the security descriptor sizes be over 0xffffffff.  */
	if (sd->total_length > buf_len)
		goto out_invalid_sd;

	sizes_size = (u64)sd->num_entries * sizeof(u64);
	size_no_descriptors = 8 + sizes_size;
	if (size_no_descriptors > sd->total_length)
		goto out_invalid_sd;

	total_len = size_no_descriptors;

	/* Return immediately if no security descriptors. */
	if (sd->num_entries == 0)
		goto out_descriptors_ready;

	/* Allocate a new buffer for the sizes array */
	sd->sizes = MALLOC(sizes_size);
	if (!sd->sizes)
		goto out_of_memory;

	/* Copy the sizes array into the new buffer */
	for (u32 i = 0; i < sd->num_entries; i++) {
		sd->sizes[i] = le64_to_cpu(sd_disk->sizes[i]);
		if (sd->sizes[i] > 0xffffffff)
			goto out_invalid_sd;
	}

	p = (const u8*)sd_disk + size_no_descriptors;

	/* Allocate the array of pointers to the security descriptors, then read
	 * them into separate buffers. */
	sd->descriptors = CALLOC(sd->num_entries, sizeof(sd->descriptors[0]));
	if (!sd->descriptors)
		goto out_of_memory;

	for (u32 i = 0; i < sd->num_entries; i++) {
		if (sd->sizes[i] == 0)
			continue;
		total_len += sd->sizes[i];
		if (total_len > (u64)sd->total_length)
			goto out_invalid_sd;
		sd->descriptors[i] = memdup(p, sd->sizes[i]);
		if (!sd->descriptors[i])
			goto out_of_memory;
		p += sd->sizes[i];
	}
out_descriptors_ready:
	if (ALIGN(total_len, 8) != sd->total_length) {
		WARNING("Stored WIM security data total length was "
			"%"PRIu32" bytes, but calculated %"PRIu32" bytes",
			sd->total_length, (u32)total_len);
	}
	*sd_ret = sd;
	ret = 0;
	goto out;
out_invalid_sd:
	ERROR("WIM security data is invalid!");
	ret = WIMLIB_ERR_INVALID_METADATA_RESOURCE;
	goto out_free_sd;
out_of_memory:
	ERROR("Out of memory while reading WIM security data!");
	ret = WIMLIB_ERR_NOMEM;
out_free_sd:
	free_wim_security_data(sd);
out:
	return ret;
}

/*
 * Writes the security data for a WIM image to an in-memory buffer.
 */
u8 *
write_wim_security_data(const struct wim_security_data * restrict sd,
			u8 * restrict p)
{
	u8 *orig_p = p;
	struct wim_security_data_disk *sd_disk = (struct wim_security_data_disk*)p;
	u32 num_entries = sd->num_entries;

	sd_disk->total_length = cpu_to_le32(sd->total_length);
	sd_disk->num_entries = cpu_to_le32(num_entries);

	for (u32 i = 0; i < num_entries; i++)
		sd_disk->sizes[i] = cpu_to_le64(sd->sizes[i]);

	p = (u8*)&sd_disk->sizes[num_entries];

	for (u32 i = 0; i < num_entries; i++)
		p = mempcpy(p, sd->descriptors[i], sd->sizes[i]);

	while ((uintptr_t)p & 7)
		*p++ = 0;

	wimlib_assert(p - orig_p == sd->total_length);
	return p;
}

void
free_wim_security_data(struct wim_security_data *sd)
{
	if (sd) {
		u8 **descriptors = sd->descriptors;
		u32 num_entries  = sd->num_entries;
		if (descriptors)
			while (num_entries--)
				FREE(*descriptors++);
		FREE(sd->sizes);
		FREE(sd->descriptors);
		FREE(sd);
	}
}

struct sd_node {
	s32 security_id;
	u8 hash[SHA1_HASH_SIZE];
	struct avl_tree_node index_node;
};

#define SD_NODE(avl_node) \
	avl_tree_entry(avl_node, struct sd_node, index_node)

static void
free_sd_tree(struct avl_tree_node *node)
{
	if (node) {
		free_sd_tree(node->left);
		free_sd_tree(node->right);
		FREE(SD_NODE(node));
	}
}

void
rollback_new_security_descriptors(struct wim_sd_set *sd_set)
{
	struct wim_security_data *sd = sd_set->sd;
	u32 i;

	for (i = sd_set->orig_num_entries; i < sd->num_entries; i++)
		FREE(sd->descriptors[i]);
	sd->num_entries = sd_set->orig_num_entries;
}

/* Frees a security descriptor index set. */
void
destroy_sd_set(struct wim_sd_set *sd_set)
{
	free_sd_tree(sd_set->root);
}

static int
_avl_cmp_nodes_by_hash(const struct avl_tree_node *n1,
		       const struct avl_tree_node *n2)
{
	return hashes_cmp(SD_NODE(n1)->hash, SD_NODE(n2)->hash);
}

/* Inserts a new node into the security descriptor index tree.  Returns true
 * if successful (not a duplicate).  */
static bool
insert_sd_node(struct wim_sd_set *set, struct sd_node *new)
{
	return NULL == avl_tree_insert(&set->root, &new->index_node,
				       _avl_cmp_nodes_by_hash);
}

/* Returns the index of the security descriptor having a SHA1 message digest of
 * @hash.  If not found, return -1. */
static s32
lookup_sd(struct wim_sd_set *set, const u8 hash[SHA1_HASH_SIZE])
{
	struct avl_tree_node *res;
	struct sd_node dummy;

	copy_hash(dummy.hash, hash);
	res = avl_tree_lookup_node(set->root, &dummy.index_node,
				   _avl_cmp_nodes_by_hash);
	if (!res)
		return -1;
	return SD_NODE(res)->security_id;
}

/*
 * Adds a security descriptor to the indexed security descriptor set as well as
 * the corresponding `struct wim_security_data', and returns the new security
 * ID; or, if there is an existing security descriptor that is the same, return
 * the security ID for it.  If a new security descriptor cannot be allocated,
 * return -1.
 */
s32
sd_set_add_sd(struct wim_sd_set *sd_set, const char *descriptor, size_t size)
{
	u8 hash[SHA1_HASH_SIZE];
	s32 security_id;
	struct sd_node *new;
	u8 **descriptors;
	u64 *sizes;
	u8 *descr_copy;
	struct wim_security_data *sd;
	bool bret;

	sha1(descriptor, size, hash);

	security_id = lookup_sd(sd_set, hash);
	if (security_id >= 0) /* Identical descriptor already exists */
		goto out;

	/* Need to add a new security descriptor */
	security_id = -1;

	new = MALLOC(sizeof(*new));
	if (!new)
		goto out;

	descr_copy = memdup(descriptor, size);
	if (!descr_copy)
		goto out_free_node;

	sd = sd_set->sd;
	new->security_id = sd->num_entries;
	copy_hash(new->hash, hash);

	/* There typically are only a few dozen security descriptors in a
	 * directory tree, so expanding the array of security descriptors by
	 * only 1 extra space each time should not be a problem. */
	descriptors = REALLOC(sd->descriptors,
			      (sd->num_entries + 1) * sizeof(sd->descriptors[0]));
	if (!descriptors)
		goto out_free_descr;
	sd->descriptors = descriptors;
	sizes = REALLOC(sd->sizes,
			(sd->num_entries + 1) * sizeof(sd->sizes[0]));
	if (!sizes)
		goto out_free_descr;
	sd->sizes = sizes;
	sd->descriptors[sd->num_entries] = descr_copy;
	sd->sizes[sd->num_entries] = size;
	sd->num_entries++;
	bret = insert_sd_node(sd_set, new);
	wimlib_assert(bret);
	security_id = new->security_id;
	goto out;
out_free_descr:
	FREE(descr_copy);
out_free_node:
	FREE(new);
out:
	return security_id;
}

/* Initialize a `struct sd_set' mapping from SHA1 message digests of security
 * descriptors to indices into the security descriptors table of the WIM image
 * (security IDs).  */
int
init_sd_set(struct wim_sd_set *sd_set, struct wim_security_data *sd)
{
	int ret;

	sd_set->sd = sd;
	sd_set->root = NULL;

	/* Remember the original number of security descriptors so that newly
	 * added ones can be rolled back if needed. */
	sd_set->orig_num_entries = sd->num_entries;
	for (u32 i = 0; i < sd->num_entries; i++) {
		struct sd_node *new;

		new = MALLOC(sizeof(struct sd_node));
		if (!new) {
			ret = WIMLIB_ERR_NOMEM;
			goto out_destroy_sd_set;
		}
		sha1(sd->descriptors[i], sd->sizes[i], new->hash);
		new->security_id = i;
		if (!insert_sd_node(sd_set, new))
			FREE(new); /* Ignore duplicate security descriptor */
	}
	ret = 0;
	goto out;
out_destroy_sd_set:
	destroy_sd_set(sd_set);
out:
	return ret;
}
