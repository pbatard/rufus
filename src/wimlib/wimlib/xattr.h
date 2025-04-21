#ifndef _WIMLIB_XATTR_H
#define _WIMLIB_XATTR_H

#include <string.h>

#include "wimlib/endianness.h"
#include "wimlib/tagged_items.h"
#include "wimlib/util.h"

#undef HAVE_LINUX_XATTR_SUPPORT
#if defined(HAVE_SYS_XATTR_H) && \
	defined(HAVE_LLISTXATTR) && defined(HAVE_LGETXATTR) && \
	defined(HAVE_FSETXATTR) && defined(HAVE_LSETXATTR)
#  define HAVE_LINUX_XATTR_SUPPORT 1
#endif

#define WIM_XATTR_NAME_MAX 255
#define WIM_XATTR_SIZE_MAX 65535

/*
 * On-disk format of each extended attribute (xattr, or EA) entry in a metadata
 * item tagged with TAG_XATTRS.  This is the preferred xattr format, since it is
 * also used by WIMGAPI and DISM starting in Windows 10 version 1607.
 */
PRAGMA_BEGIN_PACKED
struct wim_xattr_entry {

	/* length of xattr value in bytes */
	le16 value_len;

	/* length of xattr name in bytes, excluding the null terminator */
	u8 name_len;

	/* flags: 0 or 0x80 (FILE_NEED_EA) */
	u8 flags;

	/* followed by the xattr name *with* a null terminator */
	char name[0];

	/* followed by the xattr value */
	/* u8 value[0]; */

	/* no padding at end! */
} __attribute__((packed));
PRAGMA_END_PACKED

static inline size_t
xattr_entry_size(const struct wim_xattr_entry *entry)
{
	STATIC_ASSERT(sizeof(*entry) == 4);

	return sizeof(*entry) + entry->name_len + 1 +
		le16_to_cpu(entry->value_len);
}

/* minimum is a 1-byte name (plus null terminator) and an empty value */
#define XATTR_ENTRY_MIN_SIZE (sizeof(struct wim_xattr_entry) + 2)

static inline struct wim_xattr_entry *
xattr_entry_next(const struct wim_xattr_entry *entry)
{
	return (void *)_PTR(entry + xattr_entry_size(entry));
}

static inline bool
valid_xattr_entry(const struct wim_xattr_entry *entry, size_t avail)
{
	if (avail < sizeof(*entry))
		return false;

	return entry->name_len > 0 && entry->name_len <= WIM_XATTR_NAME_MAX &&
		le16_to_cpu(entry->value_len) <= WIM_XATTR_SIZE_MAX &&
		avail >= xattr_entry_size(entry) &&
		memchr(entry->name, '\0', entry->name_len) == NULL &&
		entry->name[entry->name_len] == '\0';
}

/*
 * On-disk format of each extended attribute entry in a metadata item tagged
 * with TAG_WIMLIB_LINUX_XATTRS.  This is a deprecated format which wimlib
 * v1.11-v1.12 used to store extended attributes on Linux (predating the Windows
 * xattr support in both WIMGAPI and wimlib).  Now we use TAG_XATTRS for both
 * Windows and Linux xattrs.
 */
PRAGMA_BEGIN_ALIGN(4)
struct wimlib_xattr_entry_old {

	/* length of xattr name in bytes, excluding a null terminator */
	le16 name_len;

	/* reserved, must be 0 */
	le16 reserved;

	/* length of xattr value in bytes */
	le32 value_len;

	/* followed by the xattr name *without* a null terminator */
	char name[0];

	/* followed by the xattr value */
	/* u8 value[0]; */

	/* then zero-padded to a 4-byte boundary */
} PRAGMA_END_ALIGN(4);

static inline size_t
old_xattr_entry_size(const struct wimlib_xattr_entry_old *entry)
{
	STATIC_ASSERT(sizeof(*entry) == 8);

	return ALIGN(sizeof(*entry) + le16_to_cpu(entry->name_len) +
		     le32_to_cpu(entry->value_len), 4);
}

/* minimum is a 1-byte name and an empty value */
#define OLD_XATTR_ENTRY_MIN_SIZE \
	(ALIGN(sizeof(struct wimlib_xattr_entry_old) + 1, 4))

static inline struct wimlib_xattr_entry_old *
old_xattr_entry_next(const struct wimlib_xattr_entry_old *entry)
{
	return (void *)_PTR(entry + old_xattr_entry_size(entry));
}

static inline bool
old_valid_xattr_entry(const struct wimlib_xattr_entry_old *entry, size_t avail)
{
	u16 name_len;

	if (avail < sizeof(*entry))
		return false;

	name_len = le16_to_cpu(entry->name_len);
	return name_len > 0 && name_len <= WIM_XATTR_NAME_MAX &&
		le32_to_cpu(entry->value_len) <= WIM_XATTR_SIZE_MAX &&
		avail >= old_xattr_entry_size(entry) &&
		memchr(entry->name, '\0', name_len) == NULL;
}

/* Is the xattr of the specified name security-related on Linux? */
static inline bool
is_linux_security_xattr(const char *name)
{
#define XATTR_SECURITY_PREFIX "security."
#define XATTR_SYSTEM_PREFIX "system."
#define XATTR_POSIX_ACL_ACCESS  "posix_acl_access"
#define XATTR_NAME_POSIX_ACL_ACCESS XATTR_SYSTEM_PREFIX XATTR_POSIX_ACL_ACCESS
#define XATTR_POSIX_ACL_DEFAULT  "posix_acl_default"
#define XATTR_NAME_POSIX_ACL_DEFAULT XATTR_SYSTEM_PREFIX XATTR_POSIX_ACL_DEFAULT

	return !strncmp(name, XATTR_SECURITY_PREFIX,
			sizeof(XATTR_SECURITY_PREFIX) - 1) ||
	       !strcmp(name, XATTR_NAME_POSIX_ACL_ACCESS) ||
	       !strcmp(name, XATTR_NAME_POSIX_ACL_DEFAULT);
}

static inline const void *
inode_get_xattrs(const struct wim_inode *inode, u32 *len_ret)
{
	return inode_get_tagged_item(inode, TAG_XATTRS,
				     XATTR_ENTRY_MIN_SIZE, len_ret);
}

static inline const void *
inode_get_xattrs_old(const struct wim_inode *inode, u32 *len_ret)
{
	return inode_get_tagged_item(inode, TAG_WIMLIB_LINUX_XATTRS,
				     OLD_XATTR_ENTRY_MIN_SIZE, len_ret);
}

static inline const void *
inode_get_linux_xattrs(const struct wim_inode *inode, u32 *len_ret,
		       bool *is_old_format_ret)
{
	const void *entries;

	entries = inode_get_xattrs(inode, len_ret);
	if (entries) {
		*is_old_format_ret = false;
		return entries;
	}
	entries = inode_get_xattrs_old(inode, len_ret);
	if (entries) {
		*is_old_format_ret = true;
		return entries;
	}
	return NULL;
}

static inline bool
inode_has_xattrs(const struct wim_inode *inode)
{
	return inode_get_xattrs(inode, NULL) != NULL ||
	       inode_get_xattrs_old(inode, NULL) != NULL;
}

static inline bool
inode_set_xattrs(struct wim_inode *inode, const void *entries, u32 len)
{
	return inode_set_tagged_item(inode, TAG_XATTRS, entries, len);
}

#endif /* _WIMLIB_XATTR_H  */
