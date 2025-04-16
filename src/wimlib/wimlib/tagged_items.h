#ifndef _WIMLIB_TAGGED_ITEMS_H
#define _WIMLIB_TAGGED_ITEMS_H

#include "wimlib/types.h"

struct wim_inode;

/* Windows-style object ID */
#define TAG_OBJECT_ID			0x00000001

/* Extended attributes */
#define TAG_XATTRS			0x00000002

/* [wimlib extension] Standard UNIX metadata: uid, gid, mode, and rdev */
#define TAG_WIMLIB_UNIX_DATA		0x337DD873

/*
 * [wimlib extension] Linux-style extended attributes
 * (deprecated in favor of TAG_XATTRS)
 */
#define TAG_WIMLIB_LINUX_XATTRS		0x337DD874

void *
inode_get_tagged_item(const struct wim_inode *inode, u32 tag, u32 min_len,
		      u32 *actual_len_ret);

bool
inode_set_tagged_item(struct wim_inode *inode, u32 tag,
		      const void *data, u32 len);

#endif /* _WIMLIB_TAGGED_ITEMS_H */
