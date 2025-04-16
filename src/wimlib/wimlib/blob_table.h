#ifndef _WIMLIB_BLOB_TABLE_H
#define _WIMLIB_BLOB_TABLE_H

#include "wimlib/list.h"
#include "wimlib/resource.h"
#include "wimlib/sha1.h"
#include "wimlib/types.h"

/* An enumerated type that identifies where a blob's data is located.  */
enum blob_location {

	/* The blob's data does not exist.  This is a temporary state only.  */
	BLOB_NONEXISTENT = 0,

	/* The blob's data is available in the WIM resource identified by the
	 * `struct wim_resource_descriptor' pointed to by @rdesc.
	 * @offset_in_res identifies the offset at which this particular blob
	 * begins in the uncompressed data of the resource.  */
	BLOB_IN_WIM,

	/* The blob's data is available as the contents of the file named by
	 * @file_on_disk.  */
	BLOB_IN_FILE_ON_DISK,

	/* The blob's data is available as the contents of the in-memory buffer
	 * pointed to by @attached_buffer.  */
	BLOB_IN_ATTACHED_BUFFER,

#ifdef WITH_FUSE
	/* The blob's data is available as the contents of the file with name
	 * @staging_file_name relative to the open directory file descriptor
	 * @staging_dir_fd.  */
	BLOB_IN_STAGING_FILE,
#endif

#ifdef WITH_NTFS_3G
	/* The blob's data is available as the contents of an NTFS attribute
	 * accessible through libntfs-3g.  @ntfs_loc points to a structure which
	 * identifies the attribute.  */
	BLOB_IN_NTFS_VOLUME,
#endif

#ifdef _WIN32
	/* Windows only: the blob's data is available in the file (or named data
	 * stream) specified by @windows_file.  The data might be only properly
	 * accessible through the Windows API.  */
	BLOB_IN_WINDOWS_FILE,
#endif
};

/* A "blob extraction target" is a stream, and the inode to which that stream
 * belongs, to which a blob needs to be extracted as part of an extraction
 * operation.  Since blobs are single-instanced, a blob may have multiple
 * extraction targets.  */
struct blob_extraction_target {
	struct wim_inode *inode;
	struct wim_inode_stream *stream;
};

/*
 * Descriptor for a "blob", which is a known length sequence of binary data.
 *
 * Within a WIM file, blobs are single instanced and are identified by SHA-1
 * message digest.
 */
struct blob_descriptor {

	/* List node for a hash bucket of the blob table  */
	struct hlist_node hash_list;

	/*
	 * Uncompressed size of this blob.
	 *
	 * In most cases we are now enforcing that this is nonzero; i.e. an
	 * empty stream will have "no blob" rather than "an empty blob".  The
	 * exceptions are:
	 *
	 *	- blob descriptors with 'blob_location == BLOB_NONEXISTENT',
	 *	  e.g. placeholder entries for new metadata resources or for
	 *	  blobs required for pipable WIM extraction.  In these cases the
	 *	  size is not meaningful information anyway.
	 *	- blob descriptors with 'blob_location == BLOB_IN_STAGING_FILE'
	 *	  can vary their size over time, including to 0.
	 */
	u64 size;

	union {
		/*
		 * For unhashed == 0: 'hash' is the SHA-1 message digest of the
		 * blob's data.  'hash_short' allows accessing just a prefix of
		 * the SHA-1 message digest, which is useful for getting a "hash
		 * code" for hash table lookup/insertion.
		 */
		u8 hash[SHA1_HASH_SIZE];
		size_t hash_short;

		/* For unhashed == 1: these variables make it possible to find
		 * the stream that references this blob.  There can be at most
		 * one such reference, since duplicate blobs can only be joined
		 * after they have been hashed.  */
		struct {
			struct wim_inode *back_inode;
			u32 back_stream_id;
		};
	};

	/* Number of times this blob is referenced by file streams in WIM
	 * images.  See blob_decrement_refcnt() for information about the
	 * limitations of this field.  */
	u32 refcnt;

	/*
	 * When a WIM file is written, this is set to the number of references
	 * (from file streams) to this blob in the output WIM file.
	 *
	 * During extraction, this is set to the number of targets to which this
	 * blob is being extracted.
	 *
	 * During image export, this is set to the number of references of this
	 * blob that originated from the source WIM.
	 *
	 * When mounting a WIM image read-write, this is set to the number of
	 * extra references to this blob preemptively taken to allow later
	 * saving the modified image as a new image and leaving the original
	 * image alone.
	 */
	u32 out_refcnt;

#ifdef WITH_FUSE
	/* Number of open file descriptors to this blob during a FUSE mount of
	 * a WIM image.  */
	u16 num_opened_fds;
#endif

	/* One of the `enum blob_location' values documented above.  */
	u16 blob_location : 4;

	/* 1 iff this blob contains "metadata" as opposed to data.  */
	u16 is_metadata : 1;

	/* 1 iff the SHA-1 message digest of this blob is unknown.  */
	u16 unhashed : 1;

	/* 1 iff this blob has failed its checksum.  */
	u16 corrupted : 1;

	/* Temporary fields used when writing blobs; set as documented for
	 * prepare_blob_list_for_write().  */
	u16 unique_size : 1;
	u16 will_be_in_output_wim : 1;

	u16 may_send_done_with_file : 1;

	/* Only used by wimlib_export_image() */
	u16 was_exported : 1;

	/* Specification of where this blob's data is located.  Which member of
	 * this union is valid is determined by the @blob_location field.  */
	union {
		/* BLOB_IN_WIM  */
		struct {
			struct wim_resource_descriptor *rdesc;
			u64 offset_in_res;

			/* Links together blobs that share the same underlying
			 * WIM resource.  The head is rdesc->blob_list.  */
			struct list_head rdesc_node;
		};

		struct {

			union {

				/* BLOB_IN_FILE_ON_DISK
				 * BLOB_IN_WINDOWS_FILE  */
				struct {
					union {
						tchar *file_on_disk;
						struct windows_file *windows_file;
					};
					struct wim_inode *file_inode;
				};

				/* BLOB_IN_ATTACHED_BUFFER */
				void *attached_buffer;

			#ifdef WITH_FUSE
				/* BLOB_IN_STAGING_FILE  */
				struct {
					char *staging_file_name;
					int staging_dir_fd;
				};
			#endif

			#ifdef WITH_NTFS_3G
				/* BLOB_IN_NTFS_VOLUME  */
				struct ntfs_location *ntfs_loc;
			#endif
			};

			/* List link for per-WIM-image list of unhashed blobs */
			struct list_head unhashed_list;
		};
	};

	/* Temporary fields  */
	union {
		/* Fields used temporarily during WIM file writing.  */
		struct {
			union {
				/* List node used for blob size table.  */
				struct hlist_node hash_list_2;

				/* Metadata for the underlying solid resource in
				 * the WIM being written (only valid if
				 * WIM_RESHDR_FLAG_SOLID set in
				 * out_reshdr.flags).  */
				struct {
					u64 out_res_offset_in_wim;
					u64 out_res_size_in_wim;
					u64 out_res_uncompressed_size;
				};
			};

			/* Links blobs being written to the WIM.  */
			struct list_head write_blobs_list;

			union {
				/* Metadata for this blob in the WIM being
				 * written.  */
				struct wim_reshdr out_reshdr;

				struct {
					/* Name under which this blob is being
					 * sorted; used only when sorting blobs
					 * for solid compression.  */
					utf16lechar *solid_sort_name;
					size_t solid_sort_name_nbytes;
				};
			};
		};

		/* Used temporarily during extraction.  This is an array of
		 * references to the streams being extracted that use this blob.
		 * out_refcnt tracks the number of slots filled.  */
		union {
			struct blob_extraction_target inline_blob_extraction_targets[3];
			struct {
				struct blob_extraction_target *blob_extraction_targets;
				u32 alloc_blob_extraction_targets;
			};
		};
	};

	/* Temporary list fields.  */
	union {
		/* Links blobs for writing blob table.  */
		struct list_head blob_table_list;

		/* Links blobs being extracted.  */
		struct list_head extraction_list;

		/* Links blobs being exported.  */
		struct list_head export_blob_list;
	};
};

struct blob_table *
new_blob_table(size_t capacity);

void
free_blob_table(struct blob_table *table);

int
read_blob_table(WIMStruct *wim);

int
write_blob_table_from_blob_list(struct list_head *blob_list,
				struct filedes *out_fd,
				u16 part_number,
				struct wim_reshdr *out_reshdr,
				int write_resource_flags);

struct blob_descriptor *
new_blob_descriptor(void);

struct blob_descriptor *
clone_blob_descriptor(const struct blob_descriptor *blob);

void
blob_decrement_refcnt(struct blob_descriptor *blob, struct blob_table *table);

void
blob_subtract_refcnt(struct blob_descriptor *blob, struct blob_table *table,
		     u32 count);

#ifdef WITH_FUSE
void
blob_decrement_num_opened_fds(struct blob_descriptor *blob);
#endif

void
blob_release_location(struct blob_descriptor *blob);

void
free_blob_descriptor(struct blob_descriptor *blob);

void
blob_table_insert(struct blob_table *table, struct blob_descriptor *blob);

void
blob_table_unlink(struct blob_table *table, struct blob_descriptor *blob);

struct blob_descriptor *
lookup_blob(const struct blob_table *table, const u8 *hash);

int
for_blob_in_table(struct blob_table *table,
		  int (*visitor)(struct blob_descriptor *, void *), void *arg);

int
for_blob_in_table_sorted_by_sequential_order(struct blob_table *table,
					     int (*visitor)(struct blob_descriptor *, void *),
					     void *arg);

struct wimlib_resource_entry;

void
blob_to_wimlib_resource_entry(const struct blob_descriptor *blob,
			      struct wimlib_resource_entry *wentry);

int
sort_blob_list(struct list_head *blob_list, size_t list_head_offset,
	       int (*compar)(const void *, const void*));

int
sort_blob_list_by_sequential_order(struct list_head *blob_list,
				   size_t list_head_offset);

int
cmp_blobs_by_sequential_order(const void *p1, const void *p2);

static inline const struct blob_extraction_target *
blob_extraction_targets(const struct blob_descriptor *blob)
{
	if (blob->out_refcnt <= ARRAY_LEN(blob->inline_blob_extraction_targets))
		return blob->inline_blob_extraction_targets;
	else
		return blob->blob_extraction_targets;
}

/*
 * Declare that the specified blob is located in the specified WIM resource at
 * the specified offset.  The caller is expected to set blob->size if required.
 */
static inline void
blob_set_is_located_in_wim_resource(struct blob_descriptor *blob,
				    struct wim_resource_descriptor *rdesc,
				    u64 offset_in_res)
{
	blob->blob_location = BLOB_IN_WIM;
	blob->rdesc = rdesc;
	list_add_tail(&blob->rdesc_node, &rdesc->blob_list);
	blob->offset_in_res = offset_in_res;
}

static inline void
blob_unset_is_located_in_wim_resource(struct blob_descriptor *blob)
{
	list_del(&blob->rdesc_node);
	blob->blob_location = BLOB_NONEXISTENT;
}

static inline void
blob_set_is_located_in_attached_buffer(struct blob_descriptor *blob,
				       void *buffer, size_t size)
{
	blob->blob_location = BLOB_IN_ATTACHED_BUFFER;
	blob->attached_buffer = buffer;
	blob->size = size;
}

static inline bool
blob_is_in_file(const struct blob_descriptor *blob)
{
	return blob->blob_location == BLOB_IN_FILE_ON_DISK
#ifdef _WIN32
	    || blob->blob_location == BLOB_IN_WINDOWS_FILE
#endif
	   ;
}

#ifdef _WIN32
const wchar_t *
get_windows_file_path(const struct windows_file *file);
#endif

static inline const tchar *
blob_file_path(const struct blob_descriptor *blob)
{
#ifdef _WIN32
	if (blob->blob_location == BLOB_IN_WINDOWS_FILE)
		return get_windows_file_path(blob->windows_file);
#endif
	return blob->file_on_disk;
}

struct blob_descriptor *
new_blob_from_data_buffer(const void *buffer, size_t size,
			  struct blob_table *blob_table);

struct blob_descriptor *
after_blob_hashed(struct blob_descriptor *blob,
		  struct blob_descriptor **back_ptr,
		  struct blob_table *blob_table, struct wim_inode *inode);

int
hash_unhashed_blob(struct blob_descriptor *blob, struct blob_table *blob_table,
		   struct blob_descriptor **blob_ret);

struct blob_descriptor **
retrieve_pointer_to_unhashed_blob(struct blob_descriptor *blob);

static inline void
prepare_unhashed_blob(struct blob_descriptor *blob,
		      struct wim_inode *back_inode, u32 stream_id,
		      struct list_head *unhashed_blobs)
{
	if (!blob)
		return;
	blob->unhashed = 1;
	blob->back_inode = back_inode;
	blob->back_stream_id = stream_id;
	list_add_tail(&blob->unhashed_list, unhashed_blobs);
}

#endif /* _WIMLIB_BLOB_TABLE_H */
