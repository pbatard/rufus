#ifndef _WIMLIB_UNIX_DATA_H
#define _WIMLIB_UNIX_DATA_H

#include "wimlib/types.h"

struct wimlib_unix_data {
	u32 uid;
	u32 gid;
	u32 mode;
	u32 rdev;
};

struct wim_inode;

bool
inode_has_unix_data(const struct wim_inode *inode);

bool
inode_get_unix_data(const struct wim_inode *inode,
		    struct wimlib_unix_data *unix_data);

#define UNIX_DATA_UID	0x1
#define UNIX_DATA_GID	0x2
#define UNIX_DATA_MODE	0x4
#define UNIX_DATA_RDEV	0x8

#define UNIX_DATA_ALL	0xF

bool
inode_set_unix_data(struct wim_inode *inode,
		    struct wimlib_unix_data *unix_data, int which);

#endif /* _WIMLIB_UNIX_DATA_H  */
