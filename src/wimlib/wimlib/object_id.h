#ifndef _WIMLIB_OBJECT_ID_H
#define _WIMLIB_OBJECT_ID_H

#include "wimlib/tagged_items.h"

/* Unconfirmed: are all 64 bytes of the object ID always present?  Since NTFS-3G
 * permits shorter object IDs, we'll do the same for now.  */
#define OBJECT_ID_MIN_LENGTH	16

static inline const void *
inode_get_object_id(const struct wim_inode *inode, u32 *len_ret)
{
	return inode_get_tagged_item(inode, TAG_OBJECT_ID, OBJECT_ID_MIN_LENGTH,
				     len_ret);
}

static inline bool
inode_has_object_id(const struct wim_inode *inode)
{
	return inode_get_object_id(inode, NULL) != NULL;
}

static inline bool
inode_set_object_id(struct wim_inode *inode, const void *object_id, u32 len)
{
	return inode_set_tagged_item(inode, TAG_OBJECT_ID, object_id, len);
}

#endif /* _WIMLIB_OBJECT_ID_H  */
