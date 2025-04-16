#ifndef _WIMLIB_DENTRY_H
#define _WIMLIB_DENTRY_H

#include "wimlib/avl_tree.h"
#include "wimlib/case.h"
#include "wimlib/compiler.h"
#include "wimlib/inode.h"
#include "wimlib/list.h"
#include "wimlib/types.h"

struct wim_inode;
struct blob_table;

/* Base size of a WIM dentry in the on-disk format, up to and including the file
 * name length.  This does not include the variable-length file name, short
 * name, extra stream entries, and padding to 8-byte boundaries.  */
#define WIM_DENTRY_DISK_SIZE 102

/*
 * In-memory structure for a WIM directory entry (dentry).  There is a directory
 * tree for each image in the WIM.
 *
 * Note that this is a directory entry and not an inode.  Since NTFS allows hard
 * links, it's possible for an NTFS inode to correspond to multiple WIM
 * dentries.  The hard link group ID field of the on-disk WIM dentry tells us
 * the number of the NTFS inode that the dentry corresponds to (and this gets
 * placed in d_inode->i_ino).
 *
 * Unfortunately, WIM files do not have an analogue to an inode; instead certain
 * information, such as file attributes, the security descriptor, and streams is
 * replicated in each hard-linked dentry, even though this information really is
 * associated with an inode.  In-memory, we fix up this flaw by allocating a
 * `struct wim_inode' for each dentry that contains some of this duplicated
 * information, then combining the inodes for each hard link group together.
 * (See dentry_tree_fix_inodes().)
 */
struct wim_dentry {
	/* Pointer to the inode for this dentry.  This will contain some
	 * information that was factored out of the on-disk WIM dentry as common
	 * to all dentries in a hard link group.  */
	struct wim_inode *d_inode;

	/* Node for the parent's balanced binary search tree of child dentries
	 * keyed by filename (root i_children).  */
	struct avl_tree_node d_index_node;

	/* The parent of this directory entry. (The root is its own parent.)  */
	struct wim_dentry *d_parent;

	/* Linked list node that places this dentry in the list of aliases for
	 * its inode (d_inode) */
	struct hlist_node d_alias_node;

	/* Pointer to the UTF-16LE filename (malloc()ed buffer), or NULL if this
	 * dentry has no filename.  */
	utf16lechar *d_name;

	/* Pointer to the UTF-16LE short filename (malloc()ed buffer), or NULL
	 * if this dentry has no short name.  */
	utf16lechar *d_short_name;

	/* Length of 'd_name' in bytes, excluding the terminating null  */
	u16 d_name_nbytes;

	/* Length of 'd_short_name' in bytes, excluding the terminating null  */
	u16 d_short_name_nbytes;

	/* (Extraction only) Length of 'd_extraction_name' in _characters_,
	 * excluding the terminating null  */
	u16 d_extraction_name_nchars;

	/* When capturing from an NTFS volume using NTFS-3G, this flag is set on
	 * dentries that were created from a filename in the WIN32 or WIN32+DOS
	 * namespaces rather than the POSIX namespace.  Otherwise this will
	 * always be 0.  */
	u16 d_is_win32_name : 1;

	/* Temporary flag; always reset to 0 when done using.  */
	u16 d_tmp_flag : 1;

	/* Used by wimlib_update_image()  */
	u16 d_is_orphan : 1;

	union {
		/* The subdir offset is only used while reading and writing this
		 * dentry.  See the corresponding field in `struct
		 * wim_dentry_on_disk' for explanation.  */
		u64 d_subdir_offset;

		/* Temporary list field  */
		struct list_head d_tmp_list;
	};

	/* Full path to this dentry in the WIM, in platform-dependent tchars
	 * that can be printed without conversion.  By default this field will
	 * be NULL and will only be calculated on-demand by the
	 * calculate_dentry_full_path() or dentry_full_path() functions.  */
	tchar *d_full_path;

	/* (Extraction only) Actual name to extract this dentry as.  This may be
	 * either in 'tchars' or in 'utf16lechars', depending on what encoding
	 * the extraction backend needs.  This may alias 'd_name'.  If it
	 * doesn't, it is an allocated buffer which must be freed.  */
	void *d_extraction_name;

	/* (Extraction only) Linked list node that connects all dentries being
	 * extracted as part of the current extraction operation.  */
	struct list_head d_extraction_list_node;

	/* (Extraction only) Pointer to the next alias of this dentry's inode
	 * that needs to be extracted as part of the current extraction
	 * operation, or NULL if this is the last alias.  */
	struct wim_dentry *d_next_extraction_alias;

#ifdef ENABLE_TEST_SUPPORT
	struct wim_dentry *d_corresponding;
#endif
};

static inline bool
will_extract_dentry(const struct wim_dentry *dentry)
{
	return dentry->d_extraction_list_node.next != NULL;
}

size_t
dentry_out_total_length(const struct wim_dentry *dentry);

int
for_dentry_in_tree(struct wim_dentry *root,
		   int (*visitor)(struct wim_dentry *, void *), void *args);

/* Iterate through each @child dentry of the @dir directory inode in
 * collation order.  */
#define for_inode_child(child, dir)					\
	avl_tree_for_each_in_order((child), (dir)->i_children,		\
				   struct wim_dentry, d_index_node)

/* Iterate through each @child dentry of the @parent dentry in
 * collation order.  */
#define for_dentry_child(child, parent) \
	for_inode_child((child), (parent)->d_inode)

/* Iterate through each @child dentry of the @dir directory inode in
 * postorder (safe for freeing the child dentries).  */
#define for_inode_child_postorder(child, dir)				\
	avl_tree_for_each_in_postorder((child), (dir)->i_children,	\
				       struct wim_dentry, d_index_node)

/* Iterate through each @child dentry of the @parent dentry in
 * postorder (safe for freeing the child dentries).  */
#define for_dentry_child_postorder(child, parent) \
	for_inode_child_postorder((child), (parent)->d_inode)

/* Get any child dentry of the @dir directory inode.  Requires
 * inode_has_children(@dir) == true.  */
#define inode_any_child(dir)	\
	avl_tree_entry((dir)->i_children, struct wim_dentry, d_index_node)

/* Get any child dentry of the @parent dentry.  Requires
 * dentry_has_children(@parent) == true.  */
#define dentry_any_child(parent) \
	inode_any_child((parent)->d_inode)

struct wim_dentry *
dentry_get_first_ci_match(struct wim_dentry *dentry);

struct wim_dentry *
dentry_get_next_ci_match(struct wim_dentry *dentry,
			 struct wim_dentry *ci_match);

/* Iterate through all other dentries which have the same case insensitive name
 * as the one given.  */
#define dentry_for_each_ci_match(ci_match, dentry)			\
	for ((ci_match) = dentry_get_first_ci_match((dentry));		\
	     (ci_match);						\
	     (ci_match) = dentry_get_next_ci_match((dentry), (ci_match)))

void
calculate_subdir_offsets(struct wim_dentry *root, u64 *subdir_offset_p);

int
dentry_set_name(struct wim_dentry *dentry, const tchar *name);

int
dentry_set_name_utf16le(struct wim_dentry *dentry, const utf16lechar *name,
			size_t name_nbytes);

struct wim_dentry *
get_dentry(WIMStruct *wim, const tchar *path, CASE_SENSITIVITY_TYPE case_type);

struct wim_dentry *
get_dentry_child_with_name(const struct wim_dentry *dentry, const tchar *name,
			   CASE_SENSITIVITY_TYPE case_type);

struct wim_dentry *
get_dentry_child_with_utf16le_name(const struct wim_dentry *dentry,
				   const utf16lechar *name,
				   size_t name_nbytes,
				   CASE_SENSITIVITY_TYPE case_type);

struct wim_dentry *
get_parent_dentry(WIMStruct *wim, const tchar *path,
		  CASE_SENSITIVITY_TYPE case_type);

int
calculate_dentry_full_path(struct wim_dentry *dentry);

tchar *
dentry_full_path(struct wim_dentry *dentry);

int
new_dentry_with_new_inode(const tchar *name, bool set_timestamps,
			  struct wim_dentry **dentry_ret);

int
new_dentry_with_existing_inode(const tchar *name, struct wim_inode *inode,
			       struct wim_dentry **dentry_ret);

int
new_filler_directory(struct wim_dentry **dentry_ret);

void
free_dentry(struct wim_dentry *dentry);

void
free_dentry_tree(struct wim_dentry *root, struct blob_table *blob_table);

void
unlink_dentry(struct wim_dentry *dentry);

struct wim_dentry *
dentry_add_child(struct wim_dentry *parent, struct wim_dentry *child);

struct update_command_journal;

int
rename_wim_path(WIMStruct *wim, const tchar *from, const tchar *to,
		CASE_SENSITIVITY_TYPE case_type, bool noreplace,
		struct update_command_journal *j);


int
read_dentry_tree(const u8 *buf, size_t buf_len,
		 u64 root_offset, struct wim_dentry **root_ret);

u8 *
write_dentry_tree(struct wim_dentry *root, u8 *p);

static inline bool
dentry_is_root(const struct wim_dentry *dentry)
{
	return dentry->d_parent == dentry;
}

static inline bool
dentry_is_directory(const struct wim_dentry *dentry)
{
	return inode_is_directory(dentry->d_inode);
}

static inline bool
dentry_has_children(const struct wim_dentry *dentry)
{
	return inode_has_children(dentry->d_inode);
}

static inline bool
dentry_has_long_name(const struct wim_dentry *dentry)
{
	return dentry->d_name_nbytes != 0;
}

static inline bool
dentry_has_short_name(const struct wim_dentry *dentry)
{
	return dentry->d_short_name_nbytes != 0;
}

#endif /* _WIMLIB_DENTRY_H */
