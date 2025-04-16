#ifndef _WIMLIB_INODE_TABLE_H
#define _WIMLIB_INODE_TABLE_H

#include "wimlib/list.h"
#include "wimlib/types.h"
#include "wimlib/util.h"

struct wim_dentry;

/* Hash table to find inodes for hard link detection, given an inode number (in
 * the case of reading a WIM image), or both an inode number and a device number
 * (in the case of adding files to a WIM image).  Also contains an extra list to
 * hold inodes for which no additional hard link detection is desired.  In both
 * cases the inodes are linked by i_hlist_node.  */
struct wim_inode_table {
	struct hlist_head *array;
	size_t filled;
	size_t capacity;
	struct hlist_head extra_inodes;
};


/* Compute the index of the hash bucket to use for the given inode number and
 * device number.  */
static inline size_t
hash_inode(const struct wim_inode_table *table, u64 ino, u64 devno)
{
	return (hash_u64(ino) + devno) & (table->capacity - 1);
}

int
init_inode_table(struct wim_inode_table *table, size_t capacity);

int
inode_table_new_dentry(struct wim_inode_table *table, const tchar *name,
		       u64 ino, u64 devno, bool noshare,
		       struct wim_dentry **dentry_ret);

void
enlarge_inode_table(struct wim_inode_table *table);

void
inode_table_prepare_inode_list(struct wim_inode_table *table,
			       struct hlist_head *head);

void
destroy_inode_table(struct wim_inode_table *table);

#endif /* _WIMLIB_INODE_TABLE_H  */
