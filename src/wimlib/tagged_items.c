/*
 * tagged_items.c
 *
 * Support for tagged metadata items that can be appended to WIM directory
 * entries.
 */

/*
 * Copyright (C) 2014-2016 Eric Biggers
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
#include "wimlib/inode.h"
#include "wimlib/tagged_items.h"
#include "wimlib/unix_data.h"

/*
 * Header that begins each tagged metadata item associated with a file in a WIM
 * metadata resource
 */
PRAGMA_BEGIN_ALIGN(8)
struct tagged_item_header {

	/* identifies the type of metadata item (see TAG_* constants) */
	le32 tag;

	/* size of this item's data in bytes, excluding this header */
	le32 length;

	/* followed by the item's data */
	u8 data[0];

	/* then zero-padded to an 8-byte boundary */
} PRAGMA_END_ALIGN(8);

/*
 * Retrieve from @inode the first metadata item that is tagged with @tag and
 * contains at least @min_len bytes of data.  If found, return a pointer to the
 * item's data and write its actual length to @actual_len_ret if not NULL.  If
 * not found, return NULL.
 */
void *
inode_get_tagged_item(const struct wim_inode *inode, u32 tag, u32 min_len,
		      u32 *actual_len_ret)
{
	struct tagged_item_header *hdr;
	size_t len_remaining;

	STATIC_ASSERT(sizeof(*hdr) == 8);

	if (!inode->i_extra)
		return NULL;

	hdr = (struct tagged_item_header *)inode->i_extra->data;
	len_remaining = inode->i_extra->size;

	/* Iterate through the tagged items. */
	while (len_remaining >= sizeof(*hdr) + min_len) {
		u32 len = le32_to_cpu(hdr->length);
		u32 full_len = sizeof(*hdr) + ALIGN(len, 8);

		/* Length overflow (corrupted item list)? */
		if (unlikely(full_len < len || full_len > len_remaining))
			return NULL;

		/* Matches the item we wanted? */
		if (le32_to_cpu(hdr->tag) == tag && len >= min_len) {
			if (actual_len_ret)
				*actual_len_ret = len;
			return hdr->data;
		}

		len_remaining -= full_len;
		hdr = (struct tagged_item_header *)((u8 *)hdr + full_len);
	}
	return NULL;
}

/*
 * Add a tagged item to the specified inode and return a pointer to its
 * uninitialized data, which the caller must initialize.  No check is made for
 * whether the inode already has item(s) with the specified tag.
 */
static void *
inode_add_tagged_item(struct wim_inode *inode, u32 tag, u32 len)
{
	struct wim_inode_extra *extra;
	struct tagged_item_header *hdr;
	size_t oldsize = (inode->i_extra ? inode->i_extra->size : 0);
	size_t newsize = oldsize + sizeof(*hdr) + ALIGN(len, 8);

	wimlib_assert(oldsize % 8 == 0);

	extra = REALLOC(inode->i_extra, sizeof(*extra) + newsize);
	if (!extra)
		return NULL;
	inode->i_extra = extra;
	extra->size = newsize;
	hdr = (struct tagged_item_header *)&extra->data[oldsize];
	hdr->tag = cpu_to_le32(tag);
	hdr->length = cpu_to_le32(len);
	memset(hdr->data + len, 0, -len & 7); /* pad to next 8-byte boundary */
	return hdr->data;
}

/*
 * Add a tagged item containing the specified data to the specified inode, first
 * removing any existing items with the same tag.  Returns %true if successful,
 * %false if failed (out of memory).
 */
bool
inode_set_tagged_item(struct wim_inode *inode, u32 tag,
		      const void *data, u32 len)
{
	u8 *p;
	u32 old_len;

	/* Remove any existing items with the same tag */
	while ((p = inode_get_tagged_item(inode, tag, 0, &old_len)) != NULL) {
		p -= sizeof(struct tagged_item_header);
		old_len += sizeof(struct tagged_item_header);
		old_len = ALIGN(old_len, 8);
		memmove(p, p + old_len, (inode->i_extra->data +
					 inode->i_extra->size) - (p + old_len));
		inode->i_extra->size -= old_len;
	}

	/* Add the new item */
	p = inode_add_tagged_item(inode, tag, len);
	if (!p)
		return false;
	memcpy(p, data, len);
	return true;
}

struct wimlib_unix_data_disk {
	le32 uid;
	le32 gid;
	le32 mode;
	le32 rdev;
};

static inline struct wimlib_unix_data_disk *
inode_get_unix_data_disk(const struct wim_inode *inode)
{
	return inode_get_tagged_item(inode, TAG_WIMLIB_UNIX_DATA,
				     sizeof(struct wimlib_unix_data_disk),
				     NULL);
}

/* Return %true iff the specified inode has standard UNIX metadata. */
bool
inode_has_unix_data(const struct wim_inode *inode)
{
	return inode_get_unix_data_disk(inode) != NULL;
}

/*
 * Get an inode's standard UNIX metadata.
 *
 * If the inode has standard UNIX metadata, returns %true and fills @unix_data.
 * Otherwise returns %false.
 */
bool
inode_get_unix_data(const struct wim_inode *inode,
		    struct wimlib_unix_data *unix_data)
{
	const struct wimlib_unix_data_disk *p;

	p = inode_get_unix_data_disk(inode);
	if (!p)
		return false;

	unix_data->uid = le32_to_cpu(p->uid);
	unix_data->gid = le32_to_cpu(p->gid);
	unix_data->mode = le32_to_cpu(p->mode);
	unix_data->rdev = le32_to_cpu(p->rdev);
	return true;
}

/*
 * Set an inode's standard UNIX metadata.
 *
 * Callers must specify all members in @unix_data.  If the inode does not yet
 * have standard UNIX metadata, it is given these values.  Otherwise, only the
 * values that also have the corresponding flags in @which set are changed.
 *
 * Returns %true if successful, %false if failed (out of memory).
 */
bool
inode_set_unix_data(struct wim_inode *inode, struct wimlib_unix_data *unix_data,
		    int which)
{
	struct wimlib_unix_data_disk *p;

	p = inode_get_unix_data_disk(inode);
	if (!p) {
		p = inode_add_tagged_item(inode, TAG_WIMLIB_UNIX_DATA,
					  sizeof(*p));
		if (!p)
			return false;
		which = UNIX_DATA_ALL;
	}
	if (which & UNIX_DATA_UID)
		p->uid = cpu_to_le32(unix_data->uid);
	if (which & UNIX_DATA_GID)
		p->gid = cpu_to_le32(unix_data->gid);
	if (which & UNIX_DATA_MODE)
		p->mode = cpu_to_le32(unix_data->mode);
	if (which & UNIX_DATA_RDEV)
		p->rdev = cpu_to_le32(unix_data->rdev);
	return true;
}
