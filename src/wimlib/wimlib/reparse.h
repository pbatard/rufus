#ifndef _WIMLIB_REPARSE_H
#define _WIMLIB_REPARSE_H

#include "wimlib/inode.h" /* for reparse tag definitions */
#include "wimlib/types.h"

struct blob_descriptor;
struct blob_table;

/* Windows enforces this limit on the size of a reparse point buffer.  */
#define REPARSE_POINT_MAX_SIZE	16384

/*
 * On-disk format of a reparse point buffer.  See:
 *	https://msdn.microsoft.com/en-us/library/dd541671.aspx
 *
 * Note: we are not using __attribute__((packed)) for this structure, so only
 * cast to this if properly aligned!
 */
struct reparse_buffer_disk {
	le32 rptag;
	le16 rpdatalen;
	le16 rpreserved;
	union {
		u8 rpdata[REPARSE_POINT_MAX_SIZE - 8];

		struct {
			le16 substitute_name_offset;
			le16 substitute_name_nbytes;
			le16 print_name_offset;
			le16 print_name_nbytes;

			union {
				struct {
					u8 data[REPARSE_POINT_MAX_SIZE - 16];
				} junction;

				struct {
					le32 flags;
			#define SYMBOLIC_LINK_RELATIVE 0x00000001
					u8 data[REPARSE_POINT_MAX_SIZE - 20];
				} symlink;
			};
		} link;
	};
};

#define REPARSE_DATA_OFFSET ((unsigned)offsetof(struct reparse_buffer_disk, rpdata))

#define REPARSE_DATA_MAX_SIZE (REPARSE_POINT_MAX_SIZE - REPARSE_DATA_OFFSET)

static void __attribute__((unused))
check_reparse_buffer_disk(void)
{
	STATIC_ASSERT(offsetof(struct reparse_buffer_disk, rpdata) == 8);
	STATIC_ASSERT(offsetof(struct reparse_buffer_disk, link.junction.data) == 16);
	STATIC_ASSERT(offsetof(struct reparse_buffer_disk, link.symlink.data) == 20);
	STATIC_ASSERT(sizeof(struct reparse_buffer_disk) == REPARSE_POINT_MAX_SIZE);
}

/* Wrapper around a symbolic link or junction reparse point
 * (WIM_IO_REPARSE_TAG_SYMLINK or WIM_IO_REPARSE_TAG_MOUNT_POINT)  */
struct link_reparse_point {

	u32 rptag;
	u16 rpreserved;

	/* Flags, valid for symbolic links only  */
	u32 symlink_flags;

	/* Pointers to the substitute name and print name of the link,
	 * potentially not null terminated  */
	utf16lechar *substitute_name;
	utf16lechar *print_name;

	/* Lengths of the substitute and print names in bytes, not including
	 * their null terminators if present  */
	size_t substitute_name_nbytes;
	size_t print_name_nbytes;
};

static inline bool
link_is_relative_symlink(const struct link_reparse_point *link)
{
	return link->rptag == WIM_IO_REPARSE_TAG_SYMLINK &&
	       (link->symlink_flags & SYMBOLIC_LINK_RELATIVE);
}

void
complete_reparse_point(struct reparse_buffer_disk *rpbuf,
		       const struct wim_inode *inode, u16 blob_size);

int
parse_link_reparse_point(const struct reparse_buffer_disk *rpbuf, u16 rpbuflen,
			 struct link_reparse_point *link);

int
make_link_reparse_point(const struct link_reparse_point *link,
			struct reparse_buffer_disk *rpbuf, u16 *rpbuflen_ret);

#ifndef _WIN32
int
wim_inode_readlink(const struct wim_inode *inode, char *buf, size_t bufsize,
		   const struct blob_descriptor *blob,
		   const char *altroot, size_t altroot_len);

int
wim_inode_set_symlink(struct wim_inode *inode, const char *target,
		      struct blob_table *blob_table);
#endif

#endif /* _WIMLIB_REPARSE_H */
