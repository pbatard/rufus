#ifndef _WIMLIB_INODE_H
#define _WIMLIB_INODE_H

#include "wimlib/assert.h"
#include "wimlib/list.h"
#include "wimlib/sha1.h"
#include "wimlib/types.h"

struct avl_tree_node;
struct blob_descriptor;
struct blob_table;
struct wim_dentry;
struct wim_inode_extra;
struct wim_security_data;
struct wimfs_fd;

/* Valid values for the 'stream_type' field of a 'struct wim_inode_stream'  */
enum wim_inode_stream_type {

	/* Data stream, may be unnamed (usual case) or named  */
	STREAM_TYPE_DATA,

	/* Reparse point stream.  This is the same as the data of the on-disk
	 * reparse point attribute, except that the first 8 bytes of the on-disk
	 * attribute are omitted.  The omitted bytes contain the reparse tag
	 * (which is instead stored in the on-disk WIM dentry), the reparse data
	 * size (which is redundant with the stream size), and a reserved field
	 * that is always zero.  */
	STREAM_TYPE_REPARSE_POINT,

	/* Encrypted data in the "EFSRPC raw data format" specified by [MS-EFSR]
	 * section 2.2.3.  This contains metadata for the Encrypting File System
	 * as well as the encrypted data of all the file's data streams.  */
	STREAM_TYPE_EFSRPC_RAW_DATA,

	/* Stream type could not be determined  */
	STREAM_TYPE_UNKNOWN,
};

extern const utf16lechar NO_STREAM_NAME[1];

/*
 * 'struct wim_inode_stream' describes a "stream", which associates a blob of
 * data with an inode.  Each stream has a type and optionally a name.
 *
 * The most frequently seen kind of stream is the "unnamed data stream"
 * (stream_type == STREAM_TYPE_DATA && stream_name == NO_STREAM_NAME), which is
 * the "default file contents".  Many inodes just have an unnamed data stream
 * and no other streams.  However, files on NTFS filesystems may have
 * additional, "named" data streams, and this is supported by the WIM format.
 *
 * A "reparse point" is an inode with reparse data set.  The reparse data is
 * stored in a stream of type STREAM_TYPE_REPARSE_POINT.  There should be only
 * one such stream, and it should be unnamed.  However, it is possible for an
 * inode to have both a reparse point stream and an unnamed data stream, and
 * even named data streams as well.
 */
struct wim_inode_stream {

	/* The name of the stream or NO_STREAM_NAME.  */
	utf16lechar *stream_name;

	/*
	 * If 'stream_resolved' = 0, then 'stream_hash' is the SHA-1 message
	 * digest of the uncompressed data of this stream, or all zeroes if this
	 * stream is empty.
	 *
	 * If 'stream_resolved' = 1, then 'stream_blob' is a pointer directly to
	 * a descriptor for this stream's blob, or NULL if this stream is empty.
	 */
	union {
		u8 _stream_hash[SHA1_HASH_SIZE];
		struct blob_descriptor *_stream_blob;
	};

	/* 'stream_resolved' determines whether 'stream_hash' or 'stream_blob'
	 * is valid as described above.  */
	u32 stream_resolved : 1;

	/* A unique identifier for this stream within the context of its inode.
	 * This stays constant even if the streams array is reallocated.  */
	u32 stream_id : 28;

	/* The type of this stream as one of the STREAM_TYPE_* values  */
	u32 stream_type : 3;
};

/*
 * WIM inode - a "file" in an image which may be accessible via multiple paths
 *
 * Note: in WIM files there is no true on-disk analogue of an inode; there are
 * only directory entries, and some fields are duplicated among all links to a
 * file.  However, wimlib uses inode structures internally to simplify handling
 * of hard links.
 */
struct wim_inode {

	/*
	 * The collection of streams for this inode.  'i_streams' points to
	 * either 'i_embedded_streams' or an allocated array.
	 */
	struct wim_inode_stream *i_streams;
	struct wim_inode_stream i_embedded_streams[1];
	unsigned i_num_streams;

	/* Windows file attribute flags (FILE_ATTRIBUTE_*).  */
	u32 i_attributes;

	/* Root of a balanced binary search tree storing the child directory
	 * entries of this inode, if any, indexed by filename.  If this inode is
	 * not a directory or if it has no children then this will be an empty
	 * tree (NULL).  */
	struct avl_tree_node *i_children;

	/* List of dentries that are aliases for this inode.  There will be
	 * i_nlink dentries in this list.  */
	struct hlist_head i_alias_list;

	/* Field to place this inode into a list.  While reading a WIM image or
	 * adding files to a WIM image this is owned by the inode table;
	 * otherwise this links the inodes for the WIM image.  */
	struct hlist_node i_hlist_node;

	/* Number of dentries that are aliases for this inode.  */
	u32 i_nlink : 30;

	/* Flag used by some code to mark this inode as visited.  It will be 0
	 * by default, and it always must be cleared after use.  */
	u32 i_visited : 1;

	/* Cached value  */
	u32 i_can_externally_back : 1;

	/* If not NULL, a pointer to the extra data that was read from the
	 * dentry.  This should be a series of tagged items, each of which
	 * represents a bit of extra metadata, such as the file's object ID.
	 * See tagged_items.c for more information.  */
	struct wim_inode_extra *i_extra;

	/* Creation time, last access time, and last write time for this inode,
	 * in 100-nanosecond intervals since 12:00 a.m UTC January 1, 1601.
	 * They should correspond to the times gotten by calling GetFileTime()
	 * on Windows. */
	u64 i_creation_time;
	u64 i_last_access_time;
	u64 i_last_write_time;

	/* Corresponds to 'security_id' in `struct wim_dentry_on_disk':  The
	 * index of this inode's security descriptor in the WIM image's table of
	 * security descriptors, or -1 if this inode does not have a security
	 * descriptor.  */
	s32 i_security_id;

	/* Unknown field that we only read into memory so we can re-write it
	 * unchanged.  Probably it's actually just padding...  */
	u32 i_unknown_0x54;

	/* The following fields correspond to 'reparse_tag', 'rp_reserved', and
	 * 'rp_flags' in `struct wim_dentry_on_disk'.  They are only meaningful
	 * for reparse point files.  */
	u32 i_reparse_tag;
	u16 i_rp_reserved;
	u16 i_rp_flags;

	/* Inode number; corresponds to hard_link_group_id in the `struct
	 * wim_dentry_on_disk'.  */
	u64 i_ino;

	union {
		/* Device number, used only during image capture, so we can
		 * identify hard linked files by the combination of inode number
		 * and device number (rather than just inode number, which could
		 * be ambiguous if the captured tree spans a mountpoint).  Set
		 * to 0 otherwise.  */
		u64 i_devno;

		/* Fields used only during extraction  */
		struct {
			/* A singly linked list of aliases of this inode that
			 * are being extracted in the current extraction
			 * operation.  This list may be shorter than the inode's
			 * full alias list.  This list will be constructed
			 * regardless of whether the extraction backend supports
			 * hard links or not.  */
			struct wim_dentry *i_first_extraction_alias;

		#ifdef WITH_NTFS_3G
			/* In NTFS-3G extraction mode, this is set to the Master
			 * File Table (MFT) number of the NTFS file that was
			 * created for this inode.  */
			u64 i_mft_no;
		#endif
		};

		/* Used during WIM writing with
		 * WIMLIB_WRITE_FLAG_SEND_DONE_WITH_FILE_MESSAGES:  the number
		 * of streams this inode has that have not yet been fully read.
		 * */
		u32 i_num_remaining_streams;

#ifdef WITH_FUSE
		struct {
			/* Used only during image mount:  Table of file
			 * descriptors that have been opened to this inode.
			 * This table is freed when the last file descriptor is
			 * closed.  */
			struct wimfs_fd **i_fds;

			/* Lower bound on the index of the next available entry
			 * in 'i_fds'.  */
			u16 i_next_fd;
		};
#endif
	};

#ifdef WITH_FUSE
	u16 i_num_opened_fds;
	u16 i_num_allocated_fds;
#endif

	/* Next stream ID to be assigned  */
	u32 i_next_stream_id;

#ifdef ENABLE_TEST_SUPPORT
	struct wim_inode *i_corresponding;
#endif
};

/* Optional extra data for a WIM inode  */
struct wim_inode_extra {
	size_t size;	/* Size of the extra data in bytes  */
	PRAGMA_ALIGN(u8 data[], 8); /* The extra data  */
};

/*
 * The available reparse tags are documented at
 * https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-fscc/c8e77b37-3909-4fe6-a4ea-2b9d423b1ee4
 * Here we only define the ones of interest to us.
 */
#define WIM_IO_REPARSE_TAG_MOUNT_POINT		0xA0000003
#define WIM_IO_REPARSE_TAG_SYMLINK		0xA000000C
#define WIM_IO_REPARSE_TAG_DEDUP		0x80000013
#define WIM_IO_REPARSE_TAG_WOF			0x80000017

/* Flags for the rp_flags field.  Currently the only known flag is NOT_FIXED,
 * which indicates that the target of the absolute symbolic link or junction was
 * not changed when it was stored.  */
#define WIM_RP_FLAG_NOT_FIXED		   0x0001

/* Windows file attribute flags  */
#define FILE_ATTRIBUTE_READONLY            0x00000001
#define FILE_ATTRIBUTE_HIDDEN              0x00000002
#define FILE_ATTRIBUTE_SYSTEM              0x00000004
#define FILE_ATTRIBUTE_DIRECTORY           0x00000010
#define FILE_ATTRIBUTE_ARCHIVE             0x00000020
#define FILE_ATTRIBUTE_DEVICE              0x00000040
#define FILE_ATTRIBUTE_NORMAL              0x00000080
#define FILE_ATTRIBUTE_TEMPORARY           0x00000100
#define FILE_ATTRIBUTE_SPARSE_FILE         0x00000200
#define FILE_ATTRIBUTE_REPARSE_POINT       0x00000400
#define FILE_ATTRIBUTE_COMPRESSED          0x00000800
#define FILE_ATTRIBUTE_OFFLINE             0x00001000
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000
#define FILE_ATTRIBUTE_ENCRYPTED           0x00004000
#define FILE_ATTRIBUTE_VIRTUAL             0x00010000

struct wim_inode *
new_inode(struct wim_dentry *dentry, bool set_timestamps);

/* Iterate through each alias of the specified inode.  */
#define inode_for_each_dentry(dentry, inode) \
	hlist_for_each_entry((dentry), &(inode)->i_alias_list, d_alias_node)

/* Return an alias of the specified inode.  */
#define inode_any_dentry(inode) \
	hlist_entry(inode->i_alias_list.first, struct wim_dentry, d_alias_node)

/* Return the full path of an alias of the specified inode, or NULL if a full
 * path could not be determined.  */
#define inode_any_full_path(inode) \
	dentry_full_path(inode_any_dentry(inode))

void
d_associate(struct wim_dentry *dentry, struct wim_inode *inode);

void
d_disassociate(struct wim_dentry *dentry);

#ifdef WITH_FUSE
void
inode_dec_num_opened_fds(struct wim_inode *inode);
#endif

/* Is the inode a directory?
 * This doesn't count directories with reparse data.
 * wimlib only allows inodes of this type to have children.
 */
static inline bool
inode_is_directory(const struct wim_inode *inode)
{
	return (inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
				       FILE_ATTRIBUTE_REPARSE_POINT))
			== FILE_ATTRIBUTE_DIRECTORY;
}

/* Is the inode a symbolic link?
 * This returns true iff the inode is a reparse point that is either a "real"
 * symbolic link or a junction point.  */
static inline bool
inode_is_symlink(const struct wim_inode *inode)
{
	return (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		&& (inode->i_reparse_tag == WIM_IO_REPARSE_TAG_SYMLINK ||
		    inode->i_reparse_tag == WIM_IO_REPARSE_TAG_MOUNT_POINT);
}

/* Does the inode have children?  Currently (based on read_dentry_tree() as well
 * as the various build-dentry-tree implementations), this can only return true
 * for inodes for which inode_is_directory() returns true.  */
static inline bool
inode_has_children(const struct wim_inode *inode)
{
	return inode->i_children != NULL;
}

/* Does the inode have a security descriptor?  */
static inline bool
inode_has_security_descriptor(const struct wim_inode *inode)
{
	return inode->i_security_id >= 0;
}

struct wim_inode_stream *
inode_get_stream(const struct wim_inode *inode, int stream_type,
		 const utf16lechar *stream_name);

struct wim_inode_stream *
inode_get_unnamed_stream(const struct wim_inode *inode, int stream_type);

static inline struct wim_inode_stream *
inode_get_unnamed_data_stream(const struct wim_inode *inode)
{
	return inode_get_unnamed_stream(inode, STREAM_TYPE_DATA);
}

struct wim_inode_stream *
inode_add_stream(struct wim_inode *inode, int stream_type,
		 const utf16lechar *stream_name, struct blob_descriptor *blob);

void
inode_replace_stream_blob(struct wim_inode *inode,
			  struct wim_inode_stream *strm,
			  struct blob_descriptor *new_blob,
			  struct blob_table *blob_table);

bool
inode_replace_stream_data(struct wim_inode *inode,
			  struct wim_inode_stream *strm,
			  const void *data, size_t size,
			  struct blob_table *blob_table);

bool
inode_add_stream_with_data(struct wim_inode *inode,
			   int stream_type, const utf16lechar *stream_name,
			   const void *data, size_t size,
			   struct blob_table *blob_table);

void
inode_remove_stream(struct wim_inode *inode, struct wim_inode_stream *strm,
		    struct blob_table *blob_table);

static inline struct blob_descriptor *
stream_blob_resolved(const struct wim_inode_stream *strm)
{
	wimlib_assert(strm->stream_resolved);
	return strm->_stream_blob;
}

static inline bool
stream_is_named(const struct wim_inode_stream *strm)
{
	return strm->stream_name != NO_STREAM_NAME;
}

static inline bool
stream_is_unnamed_data_stream(const struct wim_inode_stream *strm)
{
	return strm->stream_type == STREAM_TYPE_DATA && !stream_is_named(strm);
}

static inline bool
stream_is_named_data_stream(const struct wim_inode_stream *strm)
{
	return strm->stream_type == STREAM_TYPE_DATA && stream_is_named(strm);
}

bool
inode_has_named_data_stream(const struct wim_inode *inode);

int
inode_resolve_streams(struct wim_inode *inode, struct blob_table *table,
		      bool force);

int
blob_not_found_error(const struct wim_inode *inode, const u8 *hash);

struct blob_descriptor *
stream_blob(const struct wim_inode_stream *strm, const struct blob_table *table);

struct blob_descriptor *
inode_get_blob_for_unnamed_data_stream(const struct wim_inode *inode,
				       const struct blob_table *blob_table);

struct blob_descriptor *
inode_get_blob_for_unnamed_data_stream_resolved(const struct wim_inode *inode);

const u8 *
stream_hash(const struct wim_inode_stream *strm);

const u8 *
inode_get_hash_of_unnamed_data_stream(const struct wim_inode *inode);

void
inode_ref_blobs(struct wim_inode *inode);

void
inode_unref_blobs(struct wim_inode *inode, struct blob_table *blob_table);

/* inode_fixup.c  */
int
dentry_tree_fix_inodes(struct wim_dentry *root, struct hlist_head *inode_list);

#endif /* _WIMLIB_INODE_H  */
