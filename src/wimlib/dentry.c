/*
 * dentry.c - see description below
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

/*
 * This file contains logic to deal with WIM directory entries, or "dentries":
 *
 *  - Reading a dentry tree from a metadata resource in a WIM file
 *  - Writing a dentry tree to a metadata resource in a WIM file
 *  - Iterating through a tree of WIM dentries
 *  - Path lookup: translating a path into a WIM dentry or inode
 *  - Creating, modifying, and deleting WIM dentries
 *
 * Notes:
 *
 *  - A WIM file can contain multiple images, each of which has an independent
 *    tree of dentries.  "On disk", the dentry tree for an image is stored in
 *    the "metadata resource" for that image.
 *
 *  - Multiple dentries in an image may correspond to the same inode, or "file".
 *    When this occurs, it means that the file has multiple names, or "hard
 *    links".  A dentry is not a file, but rather the name of a file!
 *
 *  - Inodes are not represented explicitly in the WIM file format.  Instead,
 *    the metadata resource provides a "hard link group ID" for each dentry.
 *    wimlib handles pulling out actual inodes from this information, but this
 *    occurs in inode_fixup.c and not in this file.
 *
 *  - wimlib does not allow *directory* hard links, so a WIM image really does
 *    have a *tree* of dentries (and not an arbitrary graph of dentries).
 *
 *  - wimlib supports both case-sensitive and case-insensitive path lookups.
 *    The implementation uses a single in-memory index per directory, using a
 *    collation order like that used by NTFS; see collate_dentry_names().
 *
 *  - Multiple dentries in a directory might have the same case-insensitive
 *    name.  But wimlib enforces that at most one dentry in a directory can have
 *    a given case-sensitive name.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>

#include "wimlib/assert.h"
#include "wimlib/dentry.h"
#include "wimlib/inode.h"
#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/metadata.h"
#include "wimlib/paths.h"

/* On-disk format of a WIM dentry (directory entry), located in the metadata
 * resource for a WIM image.  */
PRAGMA_BEGIN_PACKED
struct wim_dentry_on_disk {

	/* Length of this directory entry in bytes, not including any extra
	 * stream entries.  Should be a multiple of 8 so that the following
	 * dentry or extra stream entry is aligned on an 8-byte boundary.  (If
	 * not, wimlib will round it up.)  It must be at least as long as the
	 * fixed-length fields of the dentry (WIM_DENTRY_DISK_SIZE), plus the
	 * lengths of the file name and/or short name if present, plus the size
	 * of any "extra" data.
	 *
	 * It is also possible for this field to be 0.  This case indicates the
	 * end of a list of sibling entries in a directory.  It also means the
	 * real length is 8, because the dentry included only the length field,
	 * but that takes up 8 bytes.  */
	le64 length;

	/* File attributes for the file or directory.  This is a bitwise OR of
	 * the FILE_ATTRIBUTE_* constants and should correspond to the value
	 * retrieved by GetFileAttributes() on Windows. */
	le32 attributes;

	/* A value that specifies the security descriptor for this file or
	 * directory.  If 0xFFFFFFFF, the file or directory has no security
	 * descriptor.  Otherwise, it is a 0-based index into the WIM image's
	 * table of security descriptors (see: `struct wim_security_data') */
	le32 security_id;

	/* Offset, in bytes, from the start of the uncompressed metadata
	 * resource of this directory's child directory entries, or 0 if this
	 * directory entry does not correspond to a directory or otherwise does
	 * not have any children. */
	le64 subdir_offset;

	/* Reserved fields */
	le64 unused_1;
	le64 unused_2;

	/* Creation time, last access time, and last write time, in
	 * 100-nanosecond intervals since 12:00 a.m UTC January 1, 1601.  They
	 * should correspond to the times gotten by calling GetFileTime() on
	 * Windows. */
	le64 creation_time;
	le64 last_access_time;
	le64 last_write_time;

	/*
	 * Usually this is the SHA-1 message digest of the file's contents, or
	 * all zeroes if the file is a directory or is empty.  However, special
	 * rules apply if the file has FILE_ATTRIBUTE_REPARSE_POINT set or has
	 * named data streams.  See assign_stream_types_unencrypted().
	 */
	u8 main_hash[SHA1_HASH_SIZE];

	/* Unknown field (maybe accidental padding)  */
	le32 unknown_0x54;

	/*
	 * The following 8-byte union contains either information about the
	 * reparse point (for files with FILE_ATTRIBUTE_REPARSE_POINT set), or
	 * the "hard link group ID" (for other files).
	 *
	 * The reparse point information contains ReparseTag and ReparseReserved
	 * from the header of the reparse point buffer.  It also contains a flag
	 * that indicates whether a reparse point fixup (for the target of an
	 * absolute symbolic link or junction) was done or not.
	 *
	 * The "hard link group ID" is like an inode number; all dentries for
	 * the same inode share the same value.  See inode_fixup.c for more
	 * information.
	 *
	 * Note that this union creates the limitation that reparse point files
	 * cannot have multiple names (hard links).
	 */
	union {
		struct {
			le32 reparse_tag;
			le16 rp_reserved;
			le16 rp_flags;
		} __attribute__((packed)) reparse;
		struct {
			le64 hard_link_group_id;
		} __attribute__((packed)) nonreparse;
	};

	/* Number of extra stream entries that directly follow this dentry
	 * on-disk.  */
	le16 num_extra_streams;

	/* If nonzero, this is the length, in bytes, of this dentry's UTF-16LE
	 * encoded short name (8.3 DOS-compatible name), excluding the null
	 * terminator.  If zero, then the long name of this dentry does not have
	 * a corresponding short name (but this does not exclude the possibility
	 * that another dentry for the same file has a short name).  */
	le16 short_name_nbytes;

	/* If nonzero, this is the length, in bytes, of this dentry's UTF-16LE
	 * encoded "long" name, excluding the null terminator.  If zero, then
	 * this file has no long name.  The root dentry should not have a long
	 * name, but all other dentries in the image should have long names.  */
	le16 name_nbytes;

	/* Beginning of optional, variable-length fields  */

	/* If name_nbytes != 0, the next field will be the UTF-16LE encoded long
	 * name.  This will be null-terminated, so the size of this field will
	 * really be name_nbytes + 2.  */
	/*utf16lechar name[];*/

	/* If short_name_nbytes != 0, the next field will be the UTF-16LE
	 * encoded short name.  This will be null-terminated, so the size of
	 * this field will really be short_name_nbytes + 2.  */
	/*utf16lechar short_name[];*/

	/* If there is still space in the dentry (according to the 'length'
	 * field) after 8-byte alignment, then the remaining space will be a
	 * variable-length list of tagged metadata items.  See tagged_items.c
	 * for more information.  */
	/* u8 tagged_items[] __attribute__((aligned(8))); */

} __attribute__((packed));
	/* If num_extra_streams != 0, then there are that many extra stream
	 * entries following the dentry, starting on the next 8-byte aligned
	 * boundary.  They are not counted in the 'length' field of the dentry.
	 */

/* On-disk format of an extra stream entry.  This represents an extra NTFS-style
 * "stream" associated with the file, such as a named data stream.  */
struct wim_extra_stream_entry_on_disk {

	/* Length of this extra stream entry, in bytes.  This includes all
	 * fixed-length fields, plus the name and null terminator if present,
	 * and any needed padding such that the length is a multiple of 8.  */
	le64 length;

	/* Reserved field  */
	le64 reserved;

	/* SHA-1 message digest of this stream's uncompressed data, or all
	 * zeroes if this stream's data is of zero length.  */
	u8 hash[SHA1_HASH_SIZE];

	/* Length of this stream's name, in bytes and excluding the null
	 * terminator; or 0 if this stream is unnamed.  */
	le16 name_nbytes;

	/* Stream name in UTF-16LE.  It is @name_nbytes bytes long, excluding
	 * the null terminator.  There is a null terminator character if
	 * @name_nbytes != 0; i.e., if this stream is named.  */
	utf16lechar name[];
} __attribute__((packed));
PRAGMA_END_PACKED

static void
do_dentry_set_name(struct wim_dentry *dentry, utf16lechar *name,
		   size_t name_nbytes)
{
	FREE(dentry->d_name);
	dentry->d_name = name;
	dentry->d_name_nbytes = name_nbytes;

	if (dentry_has_short_name(dentry)) {
		FREE(dentry->d_short_name);
		dentry->d_short_name = NULL;
		dentry->d_short_name_nbytes = 0;
	}
}

/*
 * Set the name of a WIM dentry from a UTF-16LE string.
 *
 * This sets the long name of the dentry.  The short name will automatically be
 * removed, since it may not be appropriate for the new long name.
 *
 * The @name string need not be null-terminated, since its length is specified
 * in @name_nbytes.
 *
 * If @name_nbytes is 0, both the long and short names of the dentry will be
 * removed.
 *
 * Only use this function on unlinked dentries, since it doesn't update the name
 * indices.  For dentries that are currently linked into the tree, use
 * rename_wim_path().
 *
 * Returns 0 or WIMLIB_ERR_NOMEM.
 */
int
dentry_set_name_utf16le(struct wim_dentry *dentry, const utf16lechar *name,
			size_t name_nbytes)
{
	utf16lechar *dup = NULL;

	if (name_nbytes) {
		dup = utf16le_dupz(name, name_nbytes);
		if (!dup)
			return WIMLIB_ERR_NOMEM;
	}
	do_dentry_set_name(dentry, dup, name_nbytes);
	return 0;
}


/*
 * Set the name of a WIM dentry from a 'tchar' string.
 *
 * This sets the long name of the dentry.  The short name will automatically be
 * removed, since it may not be appropriate for the new long name.
 *
 * If @name is NULL or empty, both the long and short names of the dentry will
 * be removed.
 *
 * Only use this function on unlinked dentries, since it doesn't update the name
 * indices.  For dentries that are currently linked into the tree, use
 * rename_wim_path().
 *
 * Returns 0 or an error code resulting from a failed string conversion.
 */
int
dentry_set_name(struct wim_dentry *dentry, const tchar *name)
{
	utf16lechar *name_utf16le = NULL;
	size_t name_utf16le_nbytes = 0;
	int ret;

	if (name && *name) {
		ret = tstr_to_utf16le(name, tstrlen(name) * sizeof(tchar),
				      &name_utf16le, &name_utf16le_nbytes);
		if (ret)
			return ret;
	}

	do_dentry_set_name(dentry, name_utf16le, name_utf16le_nbytes);
	return 0;
}

/* Calculate the minimum unaligned length, in bytes, of an on-disk WIM dentry
 * that has names of the specified lengths.  (Zero length means the
 * corresponding name actually does not exist.)  The returned value excludes
 * tagged metadata items as well as any extra stream entries that may need to
 * follow the dentry.  */
static size_t
dentry_min_len_with_names(u16 name_nbytes, u16 short_name_nbytes)
{
	size_t length = sizeof(struct wim_dentry_on_disk);
	if (name_nbytes)
		length += (u32)name_nbytes + 2;
	if (short_name_nbytes)
		length += (u32)short_name_nbytes + 2;
	return length;
}


/* Return the length, in bytes, required for the specified stream on-disk, when
 * represented as an extra stream entry.  */
static size_t
stream_out_total_length(const struct wim_inode_stream *strm)
{
	/* Account for the fixed length portion  */
	size_t len = sizeof(struct wim_extra_stream_entry_on_disk);

	/* For named streams, account for the variable-length name.  */
	if (stream_is_named(strm))
		len += utf16le_len_bytes(strm->stream_name) + 2;

	/* Account for any necessary padding to the next 8-byte boundary.  */
	return ALIGN(len, 8);
}

/*
 * Calculate the total number of bytes that will be consumed when a dentry is
 * written.  This includes the fixed-length portion of the dentry, the name
 * fields, any tagged metadata items, and any extra stream entries.  This also
 * includes all alignment bytes.
 */
size_t
dentry_out_total_length(const struct wim_dentry *dentry)
{
	const struct wim_inode *inode = dentry->d_inode;
	size_t len;
	unsigned num_unnamed_streams = 0;
	bool have_named_data_stream = false;

	len = dentry_min_len_with_names(dentry->d_name_nbytes,
					dentry->d_short_name_nbytes);
	len = ALIGN(len, 8);

	if (inode->i_extra)
		len += ALIGN(inode->i_extra->size, 8);

	/*
	 * Calculate the total length of the extra stream entries that will be
	 * written.  To match DISM, some odd rules need to be followed here.
	 * See write_dentry_streams() for explanation.  Keep this in sync with
	 * write_dentry_streams()!
	 */
	if (inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED) {
		num_unnamed_streams++;
	} else {
		for (unsigned i = 0; i < inode->i_num_streams; i++) {
			const struct wim_inode_stream *strm = &inode->i_streams[i];

			if (stream_is_named_data_stream(strm)) {
				len += stream_out_total_length(strm);
				have_named_data_stream = true;
			}
		}
		if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
			num_unnamed_streams++;
		if (!(inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY))
			num_unnamed_streams++;
	}
	if (num_unnamed_streams > 1 || have_named_data_stream)
		len += num_unnamed_streams *
		       ALIGN(sizeof(struct wim_extra_stream_entry_on_disk), 8);
	return len;
}

/* Internal version of for_dentry_in_tree() that omits the NULL check  */
static int
do_for_dentry_in_tree(struct wim_dentry *dentry,
		      int (*visitor)(struct wim_dentry *, void *), void *arg)
{
	int ret;
	struct wim_dentry *child;

	ret = (*visitor)(dentry, arg);
	if (unlikely(ret))
		return ret;

	for_dentry_child(child, dentry) {
		ret = do_for_dentry_in_tree(child, visitor, arg);
		if (unlikely(ret))
			return ret;
	}
	return 0;
}

/* Internal version of for_dentry_in_tree_depth() that omits the NULL check  */
static int
do_for_dentry_in_tree_depth(struct wim_dentry *dentry,
			    int (*visitor)(struct wim_dentry *, void *), void *arg)
{
	int ret;
	struct wim_dentry *child;

	for_dentry_child_postorder(child, dentry) {
		ret = do_for_dentry_in_tree_depth(child, visitor, arg);
		if (unlikely(ret))
			return ret;
	}
	return unlikely((*visitor)(dentry, arg));
}

/*
 * Call a function on all dentries in a tree.
 *
 * @arg will be passed as the second argument to each invocation of @visitor.
 *
 * This function does a pre-order traversal --- that is, a parent will be
 * visited before its children.  Furthermore, siblings will be visited in their
 * collation order.
 *
 * It is safe to pass NULL for @root, which means that the dentry tree is empty.
 * In this case, this function does nothing.
 *
 * @visitor must not modify the structure of the dentry tree during the
 * traversal.
 *
 * The return value will be 0 if all calls to @visitor returned 0.  Otherwise,
 * the return value will be the first nonzero value returned by @visitor.
 */
int
for_dentry_in_tree(struct wim_dentry *root,
		   int (*visitor)(struct wim_dentry *, void *), void *arg)
{
	if (unlikely(!root))
		return 0;
	return do_for_dentry_in_tree(root, visitor, arg);
}

/* Like for_dentry_in_tree(), but do a depth-first traversal of the dentry tree.
 * That is, the visitor function will be called on a dentry's children before
 * itself.  It will be safe to free a dentry when visiting it.  */
static int
for_dentry_in_tree_depth(struct wim_dentry *root,
			 int (*visitor)(struct wim_dentry *, void *), void *arg)
{
	if (unlikely(!root))
		return 0;
	return do_for_dentry_in_tree_depth(root, visitor, arg);
}

/*
 * Calculate the full path to @dentry within the WIM image, if not already done.
 *
 * The full name will be saved in the cached value 'dentry->d_full_path'.
 *
 * Whenever possible, use dentry_full_path() instead of calling this and
 * accessing d_full_path directly.
 *
 * Returns 0 or an error code resulting from a failed string conversion.
 */
int
calculate_dentry_full_path(struct wim_dentry *dentry)
{
	size_t ulen;
	const struct wim_dentry *d;

	if (dentry->d_full_path)
		return 0;

	ulen = 0;
	d = dentry;
	do {
		ulen += d->d_name_nbytes / sizeof(utf16lechar);
		ulen++;
		d = d->d_parent;  /* assumes d == d->d_parent for root  */
	} while (!dentry_is_root(d));

	// +1 to keep Coverity happy
	utf16lechar *ubuf = alloca((ulen + 1) * sizeof(utf16lechar));
	if (!ubuf)
		return WIMLIB_ERR_NOMEM;
	utf16lechar *p = &ubuf[ulen];

	d = dentry;
	do {
		p -= d->d_name_nbytes / sizeof(utf16lechar);
		if (d->d_name_nbytes)
			memcpy(p, d->d_name, d->d_name_nbytes);
		*--p = cpu_to_le16(WIM_PATH_SEPARATOR);
		d = d->d_parent;  /* assumes d == d->d_parent for root  */
	} while (!dentry_is_root(d));

	wimlib_assert(p == ubuf);

	return utf16le_to_tstr(ubuf, ulen * sizeof(utf16lechar),
			       &dentry->d_full_path, NULL);
}

/*
 * Return the full path to the @dentry within the WIM image, or NULL if the full
 * path could not be determined due to a string conversion error.
 *
 * The returned memory will be cached in the dentry, so the caller is not
 * responsible for freeing it.
 */
tchar *
dentry_full_path(struct wim_dentry *dentry)
{
	if (calculate_dentry_full_path(dentry))
		return NULL;
	return dentry->d_full_path;
}

static int
dentry_calculate_subdir_offset(struct wim_dentry *dentry, void *_subdir_offset_p)
{
	if (dentry_is_directory(dentry)) {
		u64 *subdir_offset_p = _subdir_offset_p;
		struct wim_dentry *child;

		/* Set offset of directory's child dentries  */
		dentry->d_subdir_offset = *subdir_offset_p;

		/* Account for child dentries  */
		for_dentry_child(child, dentry)
			*subdir_offset_p += dentry_out_total_length(child);

		/* Account for end-of-directory entry  */
		*subdir_offset_p += 8;
	} else {
		/* Not a directory; set the subdir offset to 0  */
		dentry->d_subdir_offset = 0;
	}
	return 0;
}

/*
 * Calculate the subdir offsets for a dentry tree, in preparation of writing
 * that dentry tree to a metadata resource.
 *
 * The subdir offset of each dentry is the offset in the uncompressed metadata
 * resource at which its child dentries begin, or 0 if that dentry has no
 * children.
 *
 * The caller must initialize *subdir_offset_p to the first subdir offset that
 * is available to use after the root dentry is written.
 *
 * When this function returns, *subdir_offset_p will have been advanced past the
 * size needed for the dentry tree within the uncompressed metadata resource.
 */
void
calculate_subdir_offsets(struct wim_dentry *root, u64 *subdir_offset_p)
{
	for_dentry_in_tree(root, dentry_calculate_subdir_offset, subdir_offset_p);
}

static int
dentry_compare_names(const struct wim_dentry *d1, const struct wim_dentry *d2,
		     bool ignore_case)
{
	return cmp_utf16le_strings(d1->d_name, d1->d_name_nbytes / 2,
				   d2->d_name, d2->d_name_nbytes / 2,
				   ignore_case);
}

/*
 * Collate (compare) the long filenames of two dentries.  This first compares
 * the names ignoring case, then falls back to a case-sensitive comparison if
 * the names are the same ignoring case.
 */
static int
collate_dentry_names(const struct avl_tree_node *n1,
		     const struct avl_tree_node *n2)
{
	const struct wim_dentry *d1, *d2;
	int res;

	d1 = avl_tree_entry(n1, struct wim_dentry, d_index_node);
	d2 = avl_tree_entry(n2, struct wim_dentry, d_index_node);

	res = dentry_compare_names(d1, d2, true);
	if (res)
		return res;
	return dentry_compare_names(d1, d2, false);
}

/* Default case sensitivity behavior for searches with
 * WIMLIB_CASE_PLATFORM_DEFAULT specified.  This can be modified by passing
 * WIMLIB_INIT_FLAG_DEFAULT_CASE_SENSITIVE or
 * WIMLIB_INIT_FLAG_DEFAULT_CASE_INSENSITIVE to wimlib_global_init().  */
bool default_ignore_case =
#ifdef _WIN32
	true
#else
	false
#endif
;

/*
 * Find the dentry within the given directory that has the given UTF-16LE
 * filename.  Return it if found, otherwise return NULL.  This has configurable
 * case sensitivity, and @name need not be null-terminated.
 */
struct wim_dentry *
get_dentry_child_with_utf16le_name(const struct wim_dentry *dir,
				   const utf16lechar *name,
				   size_t name_nbytes,
				   CASE_SENSITIVITY_TYPE case_type)
{
	struct wim_dentry wanted;
	struct avl_tree_node *cur = dir->d_inode->i_children;
	struct wim_dentry *ci_match = NULL;

	wanted.d_name = (utf16lechar *)name;
	wanted.d_name_nbytes = name_nbytes;

	if (unlikely(wanted.d_name_nbytes != name_nbytes))
		return NULL; /* overflow */

	/* Note: we can't use avl_tree_lookup_node() here because we need to
	 * save case-insensitive matches. */
	while (cur) {
		struct wim_dentry *child;
		int res;

		child = avl_tree_entry(cur, struct wim_dentry, d_index_node);

		res = dentry_compare_names(&wanted, child, true);
		if (!res) {
			/* case-insensitive match found */
			ci_match = child;

			res = dentry_compare_names(&wanted, child, false);
			if (!res)
				return child; /* case-sensitive match found */
		}

		if (res < 0)
			cur = cur->left;
		else
			cur = cur->right;
	}

	/* No case-sensitive match; use a case-insensitive match if possible. */

	if (!will_ignore_case(case_type))
		return NULL;

	if (ci_match) {
		size_t num_other_ci_matches = 0;
		struct wim_dentry *other_ci_match, *d;

		dentry_for_each_ci_match(d, ci_match) {
			num_other_ci_matches++;
			other_ci_match = d;
		}

		if (num_other_ci_matches != 0) {
			WARNING("Result of case-insensitive lookup is ambiguous\n"
				"          (returning \"%"TS"\" of %zu "
				"possible files, including \"%"TS"\")",
				dentry_full_path(ci_match), num_other_ci_matches,
				dentry_full_path(other_ci_match));
		}
	}

	return ci_match;
}

/*
 * Find the dentry within the given directory that has the given 'tstr'
 * filename.  If the filename was successfully converted to UTF-16LE and the
 * dentry was found, return it; otherwise return NULL.  This has configurable
 * case sensitivity.
 */
struct wim_dentry *
get_dentry_child_with_name(const struct wim_dentry *dir, const tchar *name,
			   CASE_SENSITIVITY_TYPE case_type)
{
	int ret;
	const utf16lechar *name_utf16le;
	size_t name_utf16le_nbytes;
	struct wim_dentry *child;

	ret = tstr_get_utf16le_and_len(name, &name_utf16le,
				       &name_utf16le_nbytes);
	if (ret)
		return NULL;

	child = get_dentry_child_with_utf16le_name(dir,
						   name_utf16le,
						   name_utf16le_nbytes,
						   case_type);
	tstr_put_utf16le(name_utf16le);
	return child;
}

/* This is the UTF-16LE version of get_dentry(), currently private to this file
 * because no one needs it besides get_dentry().  */
static struct wim_dentry *
get_dentry_utf16le(WIMStruct *wim, const utf16lechar *path,
		   CASE_SENSITIVITY_TYPE case_type)
{
	struct wim_dentry *cur_dentry;
	const utf16lechar *name_start, *name_end;

	/* Start with the root directory of the image.  Note: this will be NULL
	 * if an image has been added directly with wimlib_add_empty_image() but
	 * no files have been added yet; in that case we fail with ENOENT.  */
	cur_dentry = wim_get_current_root_dentry(wim);

	name_start = path;
	for (;;) {
		if (cur_dentry == NULL) {
			errno = ENOENT;
			return NULL;
		}

		if (*name_start && !dentry_is_directory(cur_dentry)) {
			errno = ENOTDIR;
			return NULL;
		}

		while (*name_start == cpu_to_le16(WIM_PATH_SEPARATOR))
			name_start++;

		if (!*name_start)
			return cur_dentry;

		name_end = name_start;
		do {
			++name_end;
		} while (*name_end != cpu_to_le16(WIM_PATH_SEPARATOR) && *name_end);

		cur_dentry = get_dentry_child_with_utf16le_name(cur_dentry,
								name_start,
								(u8*)name_end - (u8*)name_start,
								case_type);
		name_start = name_end;
	}
}

/*
 * WIM path lookup: translate a path in the currently selected WIM image to the
 * corresponding dentry, if it exists.
 *
 * @wim
 *	The WIMStruct for the WIM.  The search takes place in the currently
 *	selected image.
 *
 * @path
 *	The path to look up, given relative to the root of the WIM image.
 *	Characters with value WIM_PATH_SEPARATOR are taken to be path
 *	separators.  Leading path separators are ignored, whereas one or more
 *	trailing path separators cause the path to only match a directory.
 *
 * @case_type
 *	The case-sensitivity behavior of this function, as one of the following
 *	constants:
 *
 *    - WIMLIB_CASE_SENSITIVE:  Perform the search case sensitively.  This means
 *	that names must match exactly.
 *
 *    - WIMLIB_CASE_INSENSITIVE:  Perform the search case insensitively.  This
 *	means that names are considered to match if they are equal when
 *	transformed to upper case.  If a path component matches multiple names
 *	case-insensitively, the name that matches the path component
 *	case-sensitively is chosen, if existent; otherwise one
 *	case-insensitively matching name is chosen arbitrarily.
 *
 *    - WIMLIB_CASE_PLATFORM_DEFAULT:  Perform either case-sensitive or
 *	case-insensitive search, depending on the value of the global variable
 *	default_ignore_case.
 *
 *    In any case, no Unicode normalization is done before comparing strings.
 *
 * Returns a pointer to the dentry that is the result of the lookup, or NULL if
 * no such dentry exists.  If NULL is returned, errno is set to one of the
 * following values:
 *
 *	ENOTDIR if one of the path components used as a directory existed but
 *	was not, in fact, a directory.
 *
 *	ENOENT otherwise.
 *
 * Additional notes:
 *
 *    - This function does not consider a reparse point to be a directory, even
 *	if it has FILE_ATTRIBUTE_DIRECTORY set.
 *
 *    - This function does not dereference symbolic links or junction points
 *	when performing the search.
 *
 *    - Since this function ignores leading slashes, the empty path is valid and
 *	names the root directory of the WIM image.
 *
 *    - An image added with wimlib_add_empty_image() does not have a root
 *	directory yet, and this function will fail with ENOENT for any path on
 *	such an image.
 */
struct wim_dentry *
get_dentry(WIMStruct *wim, const tchar *path, CASE_SENSITIVITY_TYPE case_type)
{
	int ret;
	const utf16lechar *path_utf16le;
	struct wim_dentry *dentry;

	ret = tstr_get_utf16le(path, &path_utf16le);
	if (ret)
		return NULL;
	dentry = get_dentry_utf16le(wim, path_utf16le, case_type);
	tstr_put_utf16le(path_utf16le);
	return dentry;
}

/* Modify @path, which is a null-terminated string @len 'tchars' in length,
 * in-place to produce the path to its parent directory.  */
static void
to_parent_name(tchar *path, size_t len)
{
	ssize_t i = (ssize_t)len - 1;
	while (i >= 0 && path[i] == WIM_PATH_SEPARATOR)
		i--;
	while (i >= 0 && path[i] != WIM_PATH_SEPARATOR)
		i--;
	while (i >= 0 && path[i] == WIM_PATH_SEPARATOR)
		i--;
	path[i + 1] = T('\0');
}

/* Similar to get_dentry(), but returns the dentry named by @path with the last
 * component stripped off.
 *
 * Note: The returned dentry is NOT guaranteed to be a directory.  */
struct wim_dentry *
get_parent_dentry(WIMStruct *wim, const tchar *path,
		  CASE_SENSITIVITY_TYPE case_type)
{
	size_t path_len = tstrlen(path);
	tchar* buf = alloca((path_len + 1) * sizeof(tchar));

	tmemcpy(buf, path, path_len + 1);
	to_parent_name(buf, path_len);
	return get_dentry(wim, buf, case_type);
}

/*
 * Create an unlinked dentry.
 *
 * @name specifies the long name to give the new dentry.  If NULL or empty, the
 * new dentry will be given no long name.
 *
 * The new dentry will have no short name and no associated inode.
 *
 * On success, returns 0 and a pointer to the new, allocated dentry is stored in
 * *dentry_ret.  On failure, returns WIMLIB_ERR_NOMEM or an error code resulting
 * from a failed string conversion.
 */
static int
new_dentry(const tchar *name, struct wim_dentry **dentry_ret)
{
	struct wim_dentry *dentry;
	int ret;

	dentry = CALLOC(1, sizeof(struct wim_dentry));
	if (!dentry)
		return WIMLIB_ERR_NOMEM;

	if (name && *name) {
		ret = dentry_set_name(dentry, name);
		if (ret) {
			FREE(dentry);
			return ret;
		}
	}
	dentry->d_parent = dentry;
	*dentry_ret = dentry;
	return 0;
}

/* Like new_dentry(), but also allocate an inode and associate it with the
 * dentry.  If set_timestamps=true, the timestamps for the inode will be set to
 * the current time; otherwise, they will be left 0.  */
int
new_dentry_with_new_inode(const tchar *name, bool set_timestamps,
			  struct wim_dentry **dentry_ret)
{
	struct wim_dentry *dentry;
	struct wim_inode *inode;
	int ret;

	ret = new_dentry(name, &dentry);
	if (ret)
		return ret;

	inode = new_inode(dentry, set_timestamps);
	if (!inode) {
		free_dentry(dentry);
		return WIMLIB_ERR_NOMEM;
	}

	*dentry_ret = dentry;
	return 0;
}

/* Like new_dentry(), but also associate the new dentry with the specified inode
 * and acquire a reference to each of the inode's blobs.  */
int
new_dentry_with_existing_inode(const tchar *name, struct wim_inode *inode,
			       struct wim_dentry **dentry_ret)
{
	int ret = new_dentry(name, dentry_ret);
	if (ret)
		return ret;
	d_associate(*dentry_ret, inode);
	inode_ref_blobs(inode);
	return 0;
}

/* Create an unnamed dentry with a new inode for a directory with the default
 * metadata.  */
int
new_filler_directory(struct wim_dentry **dentry_ret)
{
	int ret;
	struct wim_dentry *dentry;

	ret = new_dentry_with_new_inode(NULL, true, &dentry);
	if (ret)
		return ret;
	/* Leave the inode number as 0; this is allowed for non
	 * hard-linked files. */
	dentry->d_inode->i_attributes = FILE_ATTRIBUTE_DIRECTORY;
	*dentry_ret = dentry;
	return 0;
}

/*
 * Free a WIM dentry.
 *
 * In addition to freeing the dentry itself, this disassociates the dentry from
 * its inode.  If the inode is no longer in use, it will be freed as well.
 */
void
free_dentry(struct wim_dentry *dentry)
{
	if (dentry) {
		d_disassociate(dentry);
		FREE(dentry->d_name);
		FREE(dentry->d_short_name);
		FREE(dentry->d_full_path);
		FREE(dentry);
	}
}

static int
do_free_dentry(struct wim_dentry *dentry, void *_ignore)
{
	free_dentry(dentry);
	return 0;
}

static int
do_free_dentry_and_unref_blobs(struct wim_dentry *dentry, void *blob_table)
{
	inode_unref_blobs(dentry->d_inode, blob_table);
	free_dentry(dentry);
	return 0;
}

/*
 * Free all dentries in a tree.
 *
 * @root:
 *	The root of the dentry tree to free.  If NULL, this function has no
 *	effect.
 *
 * @blob_table:
 *	A pointer to the blob table for the WIM, or NULL if not specified.  If
 *	specified, this function will decrement the reference counts of the
 *	blobs referenced by the dentries.
 *
 * This function also releases references to the corresponding inodes.
 *
 * This function does *not* unlink @root from its parent directory, if it has
 * one.  If @root has a parent, the caller must unlink @root before calling this
 * function.
 */
void
free_dentry_tree(struct wim_dentry *root, struct blob_table *blob_table)
{
	int (*f)(struct wim_dentry *, void *);

	if (blob_table)
		f = do_free_dentry_and_unref_blobs;
	else
		f = do_free_dentry;

	for_dentry_in_tree_depth(root, f, blob_table);
}

/*
 * Return the first dentry in the list of dentries which have the same
 * case-insensitive name as the one given.
 */
struct wim_dentry *
dentry_get_first_ci_match(struct wim_dentry *dentry)
{
	struct wim_dentry *ci_match = dentry;

	for (;;) {
		struct avl_tree_node *node;
		struct wim_dentry *prev;

		node = avl_tree_prev_in_order(&ci_match->d_index_node);
		if (!node)
			break;
		prev = avl_tree_entry(node, struct wim_dentry, d_index_node);
		if (dentry_compare_names(prev, dentry, true))
			break;
		ci_match = prev;
	}

	if (ci_match == dentry)
		return dentry_get_next_ci_match(dentry, dentry);

	return ci_match;
}

/*
 * Return the next dentry in the list of dentries which have the same
 * case-insensitive name as the one given.
 */
struct wim_dentry *
dentry_get_next_ci_match(struct wim_dentry *dentry, struct wim_dentry *ci_match)
{
	do {
		struct avl_tree_node *node;

		node = avl_tree_next_in_order(&ci_match->d_index_node);
		if (!node)
			return NULL;
		ci_match = avl_tree_entry(node, struct wim_dentry, d_index_node);
	} while (ci_match == dentry);

	if (dentry_compare_names(ci_match, dentry, true))
		return NULL;

	return ci_match;
}

/*
 * Link a dentry into a directory.
 *
 * @parent:
 *	The directory into which to link the dentry.
 *
 * @child:
 *	The dentry to link into the directory.  It must be currently unlinked.
 *
 * Returns NULL if successful; or, if @parent already contains a dentry with the
 * same case-sensitive name as @child, then a pointer to this duplicate dentry
 * is returned.
 */
struct wim_dentry *
dentry_add_child(struct wim_dentry *parent, struct wim_dentry *child)
{
	struct wim_inode *dir = parent->d_inode;
	struct avl_tree_node *duplicate;

	wimlib_assert(parent != child);
	wimlib_assert(inode_is_directory(dir));

	duplicate = avl_tree_insert(&dir->i_children, &child->d_index_node,
				    collate_dentry_names);
	if (duplicate)
		return avl_tree_entry(duplicate, struct wim_dentry, d_index_node);

	child->d_parent = parent;
	return NULL;
}

/* Unlink a dentry from its parent directory. */
void
unlink_dentry(struct wim_dentry *dentry)
{
	/* Do nothing if the dentry is root or it's already unlinked.  Not
	 * actually necessary based on the current callers, but we do the check
	 * here to be safe.  */
	if (unlikely(dentry->d_parent == dentry))
		return;

	avl_tree_remove(&dentry->d_parent->d_inode->i_children,
			&dentry->d_index_node);

	/* Not actually necessary, but to be safe don't retain the now-obsolete
	 * parent pointer.  */
	dentry->d_parent = dentry;
}

static int
read_extra_data(const u8 *p, const u8 *end, struct wim_inode *inode)
{
	while (((uintptr_t)p & 7) && p < end)
		p++;

	if (unlikely(p < end)) {
		inode->i_extra = MALLOC(sizeof(struct wim_inode_extra) +
					end - p);
		if (!inode->i_extra)
			return WIMLIB_ERR_NOMEM;
		inode->i_extra->size = end - p;
		memcpy(inode->i_extra->data, p, end - p);
	}
	return 0;
}

/*
 * Set the type of each stream for an encrypted file.
 *
 * All data streams of the encrypted file should have been packed into a single
 * stream in the format provided by ReadEncryptedFileRaw() on Windows.  We
 * assign this stream type STREAM_TYPE_EFSRPC_RAW_DATA.
 *
 * Encrypted files can't have a reparse point stream.  In the on-disk NTFS
 * format they can, but as far as I know the reparse point stream of an
 * encrypted file can't be stored in the WIM format in a way that's compatible
 * with WIMGAPI, nor is there even any way for it to be read or written on
 * Windows when the process does not have access to the file encryption key.
 */
static void
assign_stream_types_encrypted(struct wim_inode *inode)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];
		if (!stream_is_named(strm) && !is_zero_hash(strm->_stream_hash))
		{
			strm->stream_type = STREAM_TYPE_EFSRPC_RAW_DATA;
			return;
		}
	}
}

/*
 * Set the type of each stream for an unencrypted file.
 *
 * To specify the streams of each file, the WIM provides a main_hash and an
 * optional list of "extra stream entries".  Each extra stream entry is a
 * (name, hash) pair where the name is optional.  Hashes can be the special
 * value of zero_hash, which means the stream is empty (zero-length).
 *
 * While extra stream entries with names always refer to "named data streams",
 * the main hash and any extra unnamed hashes can be hard to interpret.  This is
 * because the WIM file format unfortunately doesn't make it very clear which is
 * the unnamed data stream (i.e. standard file contents) and which is the
 * reparse stream.  The way this ambiguity is resolved (based on what MS
 * software seems to do) is by (1) a file can have at most one unnamed data
 * stream and at most one reparse stream, (2) a reparse stream is present if and
 * only if the file has FILE_ATTRIBUTE_REPARSE_POINT, and (3) the reparse
 * stream, if present, is stored before the unnamed data stream if present
 * (considering main_hash to come before any extra hashes).  Note: directories
 * need not have an unnamed data stream stored, even with a zero hash, as
 * "unnamed data stream" isn't meaningful for a directory in the first place.
 *
 * With those rules in mind, one would expect that the first unnamed stream
 * would use main_hash, and the second (if present) would use an extra stream
 * entry.  However, there is another quirk that we must be compatible with:
 * sometimes main_hash isn't used and only extra stream entries are used.  To
 * handle this, we ignore main_hash if it is zero and there is at least one
 * unnamed extra stream entry.  This works correctly as long as a zero main_hash
 * and an unnamed extra stream entry is never used to represent an empty reparse
 * stream and an unnamed data stream.  (It's not, as the reparse stream always
 * goes in the extra stream entries in this case.  See write_dentry_streams().)
 */
static void
assign_stream_types_unencrypted(struct wim_inode *inode)
{
	bool found_reparse_stream = false;
	bool found_unnamed_data_stream = false;

	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];

		if (stream_is_named(strm)) {
			/* Named extra stream entry */
			strm->stream_type = STREAM_TYPE_DATA;
		} else if (i != 0 || !is_zero_hash(strm->_stream_hash)) {
			/* Unnamed extra stream entry or a nonzero main_hash */
			if ((inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
			    !found_reparse_stream) {
				found_reparse_stream = true;
				strm->stream_type = STREAM_TYPE_REPARSE_POINT;
			} else if (!found_unnamed_data_stream) {
				found_unnamed_data_stream = true;
				strm->stream_type = STREAM_TYPE_DATA;
			} /* Else, too many unnamed streams were found. */

		} /* Else, it's a zero main_hash. */
	}

	/* If needed, use the zero main_hash. */
	if (!found_reparse_stream && !found_unnamed_data_stream) {
		inode->i_streams[0].stream_type =
			(inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) ?
			STREAM_TYPE_REPARSE_POINT : STREAM_TYPE_DATA;
	}
}

/*
 * Read and interpret the collection of streams for the specified inode.
 */
static int
setup_inode_streams(const u8 *p, const u8 *end, struct wim_inode *inode,
		    unsigned num_extra_streams, const u8 *main_hash,
		    u64 *offset_p)
{
	const u8 *orig_p = p;

	inode->i_num_streams = 1 + num_extra_streams;

	if (unlikely(inode->i_num_streams > ARRAY_LEN(inode->i_embedded_streams))) {
		inode->i_streams = CALLOC(inode->i_num_streams,
					  sizeof(inode->i_streams[0]));
		if (!inode->i_streams)
			return WIMLIB_ERR_NOMEM;
	}

	/* Use main_hash for the first stream. */
	inode->i_streams[0].stream_name = (utf16lechar *)NO_STREAM_NAME;
	copy_hash(inode->i_streams[0]._stream_hash, main_hash);
	inode->i_streams[0].stream_type = STREAM_TYPE_UNKNOWN;
	inode->i_streams[0].stream_id = 0;

	/* Read the extra stream entries. */
	for (unsigned i = 1; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm;
		const struct wim_extra_stream_entry_on_disk *disk_strm;
		u64 length;
		u16 name_nbytes;

		strm = &inode->i_streams[i];

		strm->stream_id = i;

		/* Do we have at least the size of the fixed-length data we know
		 * need?  */
		if ((end - p) < sizeof(struct wim_extra_stream_entry_on_disk))
			return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

		disk_strm = (const struct wim_extra_stream_entry_on_disk *)p;

		/* Read the length field  */
		length = ALIGN(le64_to_cpu(disk_strm->length), 8);

		/* Make sure the length field is neither so small it doesn't
		 * include all the fixed-length data nor so large it overflows
		 * the metadata resource buffer. */
		if (length < sizeof(struct wim_extra_stream_entry_on_disk) ||
		    length > (end - p))
			return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

		/* Read the rest of the fixed-length data. */

		copy_hash(strm->_stream_hash, disk_strm->hash);
		name_nbytes = le16_to_cpu(disk_strm->name_nbytes);

		/* If stream_name_nbytes != 0, the stream is named.  */
		if (name_nbytes != 0) {
			/* The name is encoded in UTF16-LE, which uses 2-byte
			 * coding units, so the length of the name had better be
			 * an even number of bytes.  */
			if (name_nbytes & 1)
				return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

			/* Add the length of the stream name to get the length
			 * we actually need to read.  Make sure this isn't more
			 * than the specified length of the entry.  */
			if (sizeof(struct wim_extra_stream_entry_on_disk) +
			    name_nbytes > length)
				return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

			strm->stream_name = utf16le_dupz(disk_strm->name,
							 name_nbytes);
			if (!strm->stream_name)
				return WIMLIB_ERR_NOMEM;
		} else {
			strm->stream_name = (utf16lechar *)NO_STREAM_NAME;
		}

		strm->stream_type = STREAM_TYPE_UNKNOWN;

		p += length;
	}

	inode->i_next_stream_id = inode->i_num_streams;

	/* Now, assign a type to each stream.  Unfortunately this requires
	 * various hacks because stream types aren't explicitly provided in the
	 * WIM on-disk format.  */

	if (unlikely(inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED))
		assign_stream_types_encrypted(inode);
	else
		assign_stream_types_unencrypted(inode);

	*offset_p += p - orig_p;
	return 0;
}

/* Read a dentry, including all extra stream entries that follow it, from an
 * uncompressed metadata resource buffer.  */
static int
read_dentry(const u8 * restrict buf, size_t buf_len,
	    u64 *offset_p, struct wim_dentry **dentry_ret)
{
	u64 offset = *offset_p;
	u64 length;
	const u8 *p;
	const struct wim_dentry_on_disk *disk_dentry;
	struct wim_dentry *dentry;
	struct wim_inode *inode;
	u16 short_name_nbytes;
	u16 name_nbytes;
	u64 calculated_size;
	int ret;

	STATIC_ASSERT(sizeof(struct wim_dentry_on_disk) == WIM_DENTRY_DISK_SIZE);

	/* Before reading the whole dentry, we need to read just the length.
	 * This is because a dentry of length 8 (that is, just the length field)
	 * terminates the list of sibling directory entries. */

	/* Check for buffer overrun.  */
	if (unlikely(offset + sizeof(u64) > buf_len ||
		     offset + sizeof(u64) < offset))
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

	/* Get pointer to the dentry data.  */
	p = &buf[offset];
	disk_dentry = (const struct wim_dentry_on_disk*)p;

	/* Get dentry length.  */
	length = ALIGN(le64_to_cpu(disk_dentry->length), 8);

	/* Check for end-of-directory.  */
	if (length <= 8) {
		*dentry_ret = NULL;
		return 0;
	}

	/* Validate dentry length.  */
	if (unlikely(length < sizeof(struct wim_dentry_on_disk)))
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

	/* Check for buffer overrun.  */
	if (unlikely(offset + length > buf_len ||
		     offset + length < offset))
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

	/* Allocate new dentry structure, along with a preliminary inode.  */
	ret = new_dentry_with_new_inode(NULL, false, &dentry);
	if (ret)
		return ret;

	inode = dentry->d_inode;

	/* Read more fields: some into the dentry, and some into the inode.  */
	inode->i_attributes = le32_to_cpu(disk_dentry->attributes);
	inode->i_security_id = le32_to_cpu(disk_dentry->security_id);
	dentry->d_subdir_offset = le64_to_cpu(disk_dentry->subdir_offset);
	inode->i_creation_time = le64_to_cpu(disk_dentry->creation_time);
	inode->i_last_access_time = le64_to_cpu(disk_dentry->last_access_time);
	inode->i_last_write_time = le64_to_cpu(disk_dentry->last_write_time);
	inode->i_unknown_0x54 = le32_to_cpu(disk_dentry->unknown_0x54);

	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		inode->i_reparse_tag = le32_to_cpu(disk_dentry->reparse.reparse_tag);
		inode->i_rp_reserved = le16_to_cpu(disk_dentry->reparse.rp_reserved);
		inode->i_rp_flags = le16_to_cpu(disk_dentry->reparse.rp_flags);
		/* Leave inode->i_ino at 0.  Note: this means that WIM cannot
		 * represent multiple hard links to a reparse point file.  */
	} else {
		inode->i_ino = le64_to_cpu(disk_dentry->nonreparse.hard_link_group_id);
	}

	/* Now onto reading the names.  There are two of them: the (long) file
	 * name, and the short name.  */

	short_name_nbytes = le16_to_cpu(disk_dentry->short_name_nbytes);
	name_nbytes = le16_to_cpu(disk_dentry->name_nbytes);

	if (unlikely((short_name_nbytes & 1) | (name_nbytes & 1))) {
		ret = WIMLIB_ERR_INVALID_METADATA_RESOURCE;
		goto err_free_dentry;
	}

	/* We now know the length of the file name and short name.  Make sure
	 * the length of the dentry is large enough to actually hold them.  */
	calculated_size = dentry_min_len_with_names(name_nbytes,
						    short_name_nbytes);

	if (unlikely(length < calculated_size)) {
		ret = WIMLIB_ERR_INVALID_METADATA_RESOURCE;
		goto err_free_dentry;
	}

	/* Advance p to point past the base dentry, to the first name.  */
	p += sizeof(struct wim_dentry_on_disk);

	/* Read the filename if present.  Note: if the filename is empty, there
	 * is no null terminator following it.  */
	if (name_nbytes) {
		dentry->d_name = utf16le_dupz(p, name_nbytes);
		if (unlikely(!dentry->d_name)) {
			ret = WIMLIB_ERR_NOMEM;
			goto err_free_dentry;
		}
		dentry->d_name_nbytes = name_nbytes;
		p += (u32)name_nbytes + 2;
	}

	/* Read the short filename if present.  Note: if there is no short
	 * filename, there is no null terminator following it. */
	if (short_name_nbytes) {
		dentry->d_short_name = utf16le_dupz(p, short_name_nbytes);
		if (unlikely(!dentry->d_short_name)) {
			ret = WIMLIB_ERR_NOMEM;
			goto err_free_dentry;
		}
		dentry->d_short_name_nbytes = short_name_nbytes;
		p += (u32)short_name_nbytes + 2;
	}

	/* Read extra data at end of dentry (but before extra stream entries).
	 * This may contain tagged metadata items.  */
	ret = read_extra_data(p, &buf[offset + length], inode);
	if (ret)
		goto err_free_dentry;

	offset += length;

	/* Set up the inode's collection of streams.  */
	ret = setup_inode_streams(&buf[offset],
				  &buf[buf_len],
				  inode,
				  le16_to_cpu(disk_dentry->num_extra_streams),
				  disk_dentry->main_hash,
				  &offset);
	if (ret)
		goto err_free_dentry;

	*offset_p = offset;  /* Sets offset of next dentry in directory  */
	*dentry_ret = dentry;
	return 0;

err_free_dentry:
	free_dentry(dentry);
	return ret;
}

static bool
dentry_is_dot_or_dotdot(const struct wim_dentry *dentry)
{
	if (dentry->d_name_nbytes <= 4) {
		if (dentry->d_name_nbytes == 4) {
			if (dentry->d_name[0] == cpu_to_le16('.') &&
			    dentry->d_name[1] == cpu_to_le16('.'))
				return true;
		} else if (dentry->d_name_nbytes == 2) {
			if (dentry->d_name[0] == cpu_to_le16('.'))
				return true;
		}
	}
	return false;
}

static bool
dentry_contains_embedded_null(const struct wim_dentry *dentry)
{
	for (unsigned i = 0; i < dentry->d_name_nbytes / 2; i++)
		if (dentry->d_name[i] == cpu_to_le16('\0'))
			return true;
	return false;
}

static bool
should_ignore_dentry(struct wim_dentry *dir, const struct wim_dentry *dentry)
{
	/* All dentries except the root must be named. */
	if (!dentry_has_long_name(dentry)) {
		WARNING("Ignoring unnamed file in directory \"%"TS"\"",
			dentry_full_path(dir));
		return true;
	}

	/* Don't allow files named "." or "..".  Such filenames could be used in
	 * path traversal attacks. */
	if (dentry_is_dot_or_dotdot(dentry)) {
		WARNING("Ignoring file named \".\" or \"..\" in directory "
			"\"%"TS"\"", dentry_full_path(dir));
		return true;
	}

	/* Don't allow filenames containing embedded null characters.  Although
	 * the null character is already considered an unsupported character for
	 * extraction by all targets, it is probably a good idea to just forbid
	 * such names entirely. */
	if (dentry_contains_embedded_null(dentry)) {
		WARNING("Ignoring filename with embedded null character in "
			"directory \"%"TS"\"", dentry_full_path(dir));
		return true;
	}

	return false;
}

static int
read_dentry_tree_recursive(const u8 * restrict buf, size_t buf_len,
			   struct wim_dentry * restrict dir, unsigned depth)
{
	u64 cur_offset = dir->d_subdir_offset;

	/* Disallow extremely deep or cyclic directory structures  */
	if (unlikely(depth >= 16384)) {
		ERROR("Directory structure too deep!");
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;
	}

	for (;;) {
		struct wim_dentry *child;
		struct wim_dentry *duplicate;
		int ret;

		/* Read next child of @dir.  */
		ret = read_dentry(buf, buf_len, &cur_offset, &child);
		if (ret)
			return ret;

		/* Check for end of directory.  */
		if (child == NULL)
			return 0;

		/* Ignore dentries with bad names.  */
		if (unlikely(should_ignore_dentry(dir, child))) {
			free_dentry(child);
			continue;
		}

		/* Link the child into the directory.  */
		duplicate = dentry_add_child(dir, child);
		if (unlikely(duplicate)) {
			/* We already found a dentry with this same
			 * case-sensitive long name.  Only keep the first one.
			 */
			WARNING("Ignoring duplicate file \"%"TS"\" "
				"(the WIM image already contains a file "
				"at that path with the exact same name)",
				dentry_full_path(duplicate));
			free_dentry(child);
			continue;
		}

		/* If this child is a directory that itself has children, call
		 * this procedure recursively.  */
		if (child->d_subdir_offset != 0) {
			if (likely(dentry_is_directory(child))) {
				ret = read_dentry_tree_recursive(buf,
								 buf_len,
								 child,
								 depth + 1);
				if (ret)
					return ret;
			} else {
				WARNING("Ignoring children of "
					"non-directory file \"%"TS"\"",
					dentry_full_path(child));
			}
		}
	}
}

/*
 * Read a tree of dentries from a WIM metadata resource.
 *
 * @buf:
 *	Buffer containing an uncompressed WIM metadata resource.
 *
 * @buf_len:
 *	Length of the uncompressed metadata resource, in bytes.
 *
 * @root_offset
 *	Offset in the metadata resource of the root of the dentry tree.
 *
 * @root_ret:
 *	On success, either NULL or a pointer to the root dentry is written to
 *	this location.  The former case only occurs in the unexpected case that
 *	the tree began with an end-of-directory entry.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_INVALID_METADATA_RESOURCE
 *	WIMLIB_ERR_NOMEM
 */
int
read_dentry_tree(const u8 *buf, size_t buf_len,
		 u64 root_offset, struct wim_dentry **root_ret)
{
	int ret;
	struct wim_dentry *root;

	ret = read_dentry(buf, buf_len, &root_offset, &root);
	if (ret)
		return ret;

	if (likely(root != NULL)) {
		if (unlikely(dentry_has_long_name(root) ||
			     dentry_has_short_name(root)))
		{
			WARNING("The root directory has a nonempty name; "
				"removing it.");
			dentry_set_name(root, NULL);
		}

		if (unlikely(!dentry_is_directory(root))) {
			ERROR("The root of the WIM image is not a directory!");
			ret = WIMLIB_ERR_INVALID_METADATA_RESOURCE;
			goto err_free_dentry_tree;
		}

		if (likely(root->d_subdir_offset != 0)) {
			ret = read_dentry_tree_recursive(buf, buf_len, root, 0);
			if (ret)
				goto err_free_dentry_tree;
		}
	} else {
		WARNING("The metadata resource has no directory entries; "
			"treating as an empty image.");
	}
	*root_ret = root;
	return 0;

err_free_dentry_tree:
	free_dentry_tree(root, NULL);
	return ret;
}

static u8 *
write_extra_stream_entry(u8 * restrict p, const utf16lechar * restrict name,
			 const u8 * restrict hash)
{
	struct wim_extra_stream_entry_on_disk *disk_strm =
			(struct wim_extra_stream_entry_on_disk *)p;
	u8 *orig_p = p;
	size_t name_nbytes;

	if (name == NO_STREAM_NAME)
		name_nbytes = 0;
	else
		name_nbytes = utf16le_len_bytes(name);

	disk_strm->reserved = 0;
	copy_hash(disk_strm->hash, hash);
	disk_strm->name_nbytes = cpu_to_le16(name_nbytes);
	p += sizeof(struct wim_extra_stream_entry_on_disk);
	if (name_nbytes != 0)
		p = mempcpy(p, name, name_nbytes + 2);
	/* Align to 8-byte boundary */
	while ((uintptr_t)p & 7)
		*p++ = 0;
	disk_strm->length = cpu_to_le64(p - orig_p);
	return p;
}

/*
 * Write the stream references for a WIM dentry.  To be compatible with DISM, we
 * follow the below rules:
 *
 * 1. If the file has FILE_ATTRIBUTE_ENCRYPTED, then only the EFSRPC_RAW_DATA
 *    stream is stored.  Otherwise, the streams that are stored are:
 *    - Reparse stream if the file has FILE_ATTRIBUTE_REPARSE_POINT
 *    - Unnamed data stream if the file doesn't have FILE_ATTRIBUTE_DIRECTORY
 *    - Named data streams
 *
 * 2. If only one stream is being stored and it is the EFSRPC_RAW_DATA, unnamed
 *    data, or reparse stream, then its hash goes in main_hash, and no extra
 *    stream entries are stored.  Otherwise, *all* streams go in the extra
 *    stream entries, and main_hash is left zeroed!
 *
 * 3. If both the reparse stream and unnamed data stream are being stored, then
 *    the reparse stream comes first.
 *
 * 4. The unnamed stream(s) come before the named stream(s).  (Actually, DISM
 *    puts the named streams between the first and second unnamed streams, but
 *    this is incompatible with itself...  Tested with DISM 10.0.20348.681.)
 *
 * wimlib v1.14.1 and earlier behaved slightly differently for directories.
 * First, wimlib always put the hash of the reparse stream in an extra stream
 * entry, never in main_hash.  This difference vs. DISM went unnoticed for a
 * long time, but eventually it was found that it broke the Windows 8 setup
 * wizard.  Second, when a directory had any extra streams, wimlib created an
 * extra stream entry to represent the (empty) unnamed data stream.  However,
 * DISM now rejects that (though I think it used to accept it).  There isn't
 * really any such thing as "unnamed data stream" for a directory.
 *
 * Keep this in sync with dentry_out_total_length()!
 */
static u8 *
write_dentry_streams(const struct wim_inode *inode,
		     struct wim_dentry_on_disk *disk_dentry, u8 *p)
{
	const u8 *unnamed_data_stream_hash = zero_hash;
	const u8 *reparse_stream_hash = zero_hash;
	const u8 *efsrpc_stream_hash = zero_hash;
	const u8 *unnamed_stream_hashes[2] = { zero_hash };
	unsigned num_unnamed_streams = 0;
	unsigned num_named_streams = 0;

	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		const struct wim_inode_stream *strm = &inode->i_streams[i];

		switch (strm->stream_type) {
		case STREAM_TYPE_DATA:
			if (stream_is_named(strm))
				num_named_streams++;
			else
				unnamed_data_stream_hash = stream_hash(strm);
			break;
		case STREAM_TYPE_REPARSE_POINT:
			reparse_stream_hash = stream_hash(strm);
			break;
		case STREAM_TYPE_EFSRPC_RAW_DATA:
			efsrpc_stream_hash = stream_hash(strm);
			break;
		}
	}

	if (inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED) {
		unnamed_stream_hashes[num_unnamed_streams++] = efsrpc_stream_hash;
		num_named_streams = 0;
	} else {
		if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
			unnamed_stream_hashes[num_unnamed_streams++] = reparse_stream_hash;
		if (!(inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY))
			unnamed_stream_hashes[num_unnamed_streams++] = unnamed_data_stream_hash;
	}

	if (num_unnamed_streams <= 1 && num_named_streams == 0) {
		/* No extra stream entries are needed. */
		copy_hash(disk_dentry->main_hash, unnamed_stream_hashes[0]);
		disk_dentry->num_extra_streams = 0;
		return p;
	}

	/* Else, all streams go in extra stream entries. */
	copy_hash(disk_dentry->main_hash, zero_hash);
	wimlib_assert(num_unnamed_streams + num_named_streams <= 0xFFFF);
	disk_dentry->num_extra_streams = cpu_to_le16(num_unnamed_streams +
						     num_named_streams);
	for (unsigned i = 0; i < num_unnamed_streams; i++)
		p = write_extra_stream_entry(p, NO_STREAM_NAME,
					     unnamed_stream_hashes[i]);
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		const struct wim_inode_stream *strm = &inode->i_streams[i];

		if (stream_is_named_data_stream(strm)) {
			p = write_extra_stream_entry(p, strm->stream_name,
						     stream_hash(strm));
		}
	}
	return p;
}

/*
 * Write a WIM dentry to an output buffer.
 *
 * This includes any extra stream entries that may follow the dentry itself.
 *
 * @dentry:
 *	The dentry to write.
 *
 * @p:
 *	The memory location to which to write the data.
 *
 * Returns a pointer to the byte following the last written.
 */
static u8 *
write_dentry(const struct wim_dentry * restrict dentry, u8 * restrict p)
{
	const struct wim_inode *inode;
	struct wim_dentry_on_disk *disk_dentry;
	const u8 *orig_p;

	wimlib_assert(((uintptr_t)p & 7) == 0); /* 8 byte aligned */
	orig_p = p;

	inode = dentry->d_inode;
	disk_dentry = (struct wim_dentry_on_disk*)p;

	disk_dentry->attributes = cpu_to_le32(inode->i_attributes);
	disk_dentry->security_id = cpu_to_le32(inode->i_security_id);
	disk_dentry->subdir_offset = cpu_to_le64(dentry->d_subdir_offset);

	disk_dentry->unused_1 = cpu_to_le64(0);
	disk_dentry->unused_2 = cpu_to_le64(0);

	disk_dentry->creation_time = cpu_to_le64(inode->i_creation_time);
	disk_dentry->last_access_time = cpu_to_le64(inode->i_last_access_time);
	disk_dentry->last_write_time = cpu_to_le64(inode->i_last_write_time);
	disk_dentry->unknown_0x54 = cpu_to_le32(inode->i_unknown_0x54);
	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		disk_dentry->reparse.reparse_tag = cpu_to_le32(inode->i_reparse_tag);
		disk_dentry->reparse.rp_reserved = cpu_to_le16(inode->i_rp_reserved);
		disk_dentry->reparse.rp_flags = cpu_to_le16(inode->i_rp_flags);
	} else {
		disk_dentry->nonreparse.hard_link_group_id =
			cpu_to_le64((inode->i_nlink == 1) ? 0 : inode->i_ino);
	}

	disk_dentry->short_name_nbytes = cpu_to_le16(dentry->d_short_name_nbytes);
	disk_dentry->name_nbytes = cpu_to_le16(dentry->d_name_nbytes);
	p += sizeof(struct wim_dentry_on_disk);

	wimlib_assert(dentry_is_root(dentry) != dentry_has_long_name(dentry));

	if (dentry_has_long_name(dentry))
		p = mempcpy(p, dentry->d_name, (u32)dentry->d_name_nbytes + 2);

	if (dentry_has_short_name(dentry))
		p = mempcpy(p, dentry->d_short_name, (u32)dentry->d_short_name_nbytes + 2);

	/* Align to 8-byte boundary */
	while ((uintptr_t)p & 7)
		*p++ = 0;

	if (inode->i_extra) {
		/* Extra tagged items --- not usually present.  */
		p = mempcpy(p, inode->i_extra->data, inode->i_extra->size);

		/* Align to 8-byte boundary */
		while ((uintptr_t)p & 7)
			*p++ = 0;
	}

	disk_dentry->length = cpu_to_le64(p - orig_p);

	/*
	 * Set disk_dentry->main_hash and disk_dentry->num_extra_streams,
	 * and write any extra stream entries that are needed.
	 */
	return write_dentry_streams(inode, disk_dentry, p);
}

static int
write_dir_dentries(struct wim_dentry *dir, void *_pp)
{
	if (dir->d_subdir_offset != 0) {
		u8 **pp = _pp;
		u8 *p = *pp;
		struct wim_dentry *child;

		/* write child dentries */
		for_dentry_child(child, dir)
			p = write_dentry(child, p);

		/* write end of directory entry */
		*(u64*)p = 0;
		p += 8;
		*pp = p;
	}
	return 0;
}

/*
 * Write a directory tree to the metadata resource.
 *
 * @root:
 *	The root of a dentry tree on which calculate_subdir_offsets() has been
 *	called.  This cannot be NULL; if the dentry tree is empty, the caller is
 *	expected to first generate a dummy root directory.
 *
 * @p:
 *	Pointer to a buffer with enough space for the dentry tree.  This size
 *	must have been obtained by calculate_subdir_offsets().
 *
 * Returns a pointer to the byte following the last written.
 */
u8 *
write_dentry_tree(struct wim_dentry *root, u8 *p)
{
	/* write root dentry and end-of-directory entry following it */
	p = write_dentry(root, p);
	*(u64*)p = 0;
	p += 8;

	/* write the rest of the dentry tree */
	for_dentry_in_tree(root, write_dir_dentries, &p);

	return p;
}
