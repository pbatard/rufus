/*
 * wim.h - WIMStruct definition and helper functions
 */

#ifndef _WIMLIB_WIM_H
#define _WIMLIB_WIM_H

#include "wimlib.h"
#include "wimlib/file_io.h"
#include "wimlib/header.h"
#include "wimlib/list.h"

struct wim_image_metadata;
struct wim_xml_info;
struct blob_table;

/*
 * WIMStruct - represents a WIM, or a part of a non-standalone WIM
 *
 * Note 1: there are three ways in which a WIMStruct can be created:
 *
 *	1. open an on-disk WIM file
 *	2. start to extract a pipable WIM from a file descriptor
 *	3. create a new WIMStruct directly
 *
 * For (1) and (2), the WIMStruct has a backing file; for (3) it does not.  For
 * (1), the backing file is a real "on-disk" file from the filesystem, whereas
 * for (2) the backing file is a file descriptor which may be a pipe.
 *
 * Note 2: although this is the top-level data structure in wimlib, there do
 * exist cases in which a WIMStruct is not standalone:
 *	- blobs have been referenced from another WIMStruct
 *	- an image has been imported into this WIMStruct from another
 *	  (as this references the metadata rather than copies it)
 *
 * Note 3: It is unsafe for multiple threads to operate on the same WIMStruct at
 * the same time.  This extends to references to other WIMStructs as noted
 * above.  But besides this, it is safe to operate on *different* WIMStructs in
 * different threads concurrently.
 */
struct WIMStruct {

	/* Information from the header of the WIM file.
	 *
	 * This is also maintained for a WIMStruct not backed by a file, but in
	 * that case the 'reshdr' fields are left zeroed.  */
	struct wim_header hdr;

	/* If the library is currently writing this WIMStruct out to a file,
	 * then this is the header being created for that file.  */
	struct wim_header out_hdr;

	/* Array of image metadata, one for each image in the WIM (array length
	 * hdr.image_count).  Or, this will be NULL if this WIM does not contain
	 * metadata, which implies that this WIMStruct either represents part of
	 * a non-standalone WIM, or represents a standalone WIM that, oddly
	 * enough, actually contains 0 images.  */
	struct wim_image_metadata **image_metadata;

	/* Information from the XML data of the WIM file.  This information is
	 * also maintained for a WIMStruct not backed by a file.  */
	struct wim_xml_info *xml_info;

	/* The blob table for this WIMStruct.  If this WIMStruct has a backing
	 * file, then this table will index the blobs contained in that file.
	 * In addition, this table may index blobs that were added by updates or
	 * referenced from other WIMStructs.  */
	struct blob_table *blob_table;

	/* The number of references to this WIMStruct.  This is equal to the
	 * number of resource descriptors that reference this WIMStruct, plus 1
	 * if wimlib_free() still needs to be called.  */
	ssize_t refcnt;

	/*
	 * The 1-based index of the currently selected image in this WIMStruct,
	 * or WIMLIB_NO_IMAGE if no image is currently selected.
	 *
	 * The metadata for the current image is image_metadata[current_image -
	 * 1].  Since we load image metadata lazily, only the metadata for the
	 * current image is guaranteed to actually be present in memory.
	 */
	int current_image;

	/* The absolute path to the on-disk file backing this WIMStruct, or NULL
	 * if this WIMStruct is not backed by an on-disk file.  */
	tchar *filename;

	/* If this WIMStruct has a backing file, then this is a file descriptor
	 * open to that file with read access.  Otherwise, this field is invalid
	 * (!filedes_valid(&in_fd)).  */
	struct filedes in_fd;

	/* If the library is currently writing this WIMStruct out to a file,
	 * then this is a file descriptor open to that file with write access.
	 * Otherwise, this field is invalid (!filedes_valid(&out_fd)).  */
	struct filedes out_fd;

	/* The size of the backing file, or 0 if unknown */
	u64 file_size;

	/*
	 * This is the cached decompressor for this WIM file, or NULL if no
	 * decompressor is cached yet.  Normally, all the compressed data in a
	 * WIM file has the same compression type and chunk size, so the same
	 * decompressor can be used for all data --- and that decompressor will
	 * be cached here.  However, if we do encounter any data with a
	 * different compression type or chunk size (this is possible in solid
	 * resources), then this cached decompressor will be replaced with a new
	 * one.
	 */
	struct wimlib_decompressor *decompressor;
	u8 decompressor_ctype;
	u32 decompressor_max_block_size;

	/* Temporary field; use sparingly  */
	void *private;

	/* 1 if any images have been deleted from this WIMStruct, otherwise 0 */
	u8 image_deletion_occurred : 1;

	/* 1 if the WIM file has been locked for appending, otherwise 0  */
	u8 locked_for_append : 1;

	/* 1 if the WIM file is currently being compacted by wimlib_overwrite()
	 * with WIMLIB_WRITE_FLAG_UNSAFE_COMPACT  */
	u8 being_compacted : 1;

	/* If this WIM is backed by a file, then this is the compression type
	 * for non-solid resources in that file.  */
	u8 compression_type;

	/* Overridden compression type for wimlib_overwrite() or wimlib_write().
	 * Can be changed by wimlib_set_output_compression_type(); otherwise is
	 * the same as compression_type.  */
	u8 out_compression_type;

	/* Compression type for writing solid resources; can be set with
	 * wimlib_set_output_pack_compression_type().  */
	u8 out_solid_compression_type;

	/* If this WIM is backed by a file, then this is the compression chunk
	 * size for non-solid resources in that file.  */
	u32 chunk_size;

	/* Overridden chunk size for wimlib_overwrite() or wimlib_write().  Can
	 * be changed by wimlib_set_output_chunk_size(); otherwise is the same
	 * as chunk_size.  */
	u32 out_chunk_size;

	/* Chunk size for writing solid resources; can be set with
	 * wimlib_set_output_pack_chunk_size().  */
	u32 out_solid_chunk_size;

	/* Currently registered progress function for this WIMStruct, or NULL if
	 * no progress function is currently registered for this WIMStruct.  */
	wimlib_progress_func_t progfunc;
	void *progctx;
};

/*
 * Return true if and only if the WIM contains image metadata (actual directory
 * trees, not just a collection of blobs and their checksums).
 *
 * See the description of the 'image_metadata' field.  Note that we return true
 * when the image count is 0 because it could be a WIM with 0 images.  It's only
 * when the WIM does not contain the metadata described by its image count that
 * we return false.
 */
static inline bool wim_has_metadata(const WIMStruct *wim)
{
	return (wim->image_metadata != NULL || wim->hdr.image_count == 0);
}

/* Return true if and only if the WIM has an integrity table.
 *
 * If the WIM is not backed by a file, then this always returns false.  */
static inline bool wim_has_integrity_table(const WIMStruct *wim)
{
	return (wim->hdr.integrity_table_reshdr.offset_in_wim != 0);
}

/* Return true if and only if the WIM is in pipable format.
 *
 * If the WIM is not backed by a file, then this always returns false.  */
static inline bool wim_is_pipable(const WIMStruct *wim)
{
	return (wim->hdr.magic == PWM_MAGIC);
}

void
wim_decrement_refcnt(WIMStruct *wim);

bool
wim_has_solid_resources(const WIMStruct *wim);

int
read_wim_header(WIMStruct *wim, struct wim_header *hdr);

int
write_wim_header(const struct wim_header *hdr, struct filedes *out_fd,
		 off_t offset);

int
write_wim_header_flags(u32 hdr_flags, struct filedes *out_fd);

int
select_wim_image(WIMStruct *wim, int image);

void
deselect_current_wim_image(WIMStruct *wim);

int
for_image(WIMStruct *wim, int image, int (*visitor)(WIMStruct *));

int
wim_checksum_unhashed_blobs(WIMStruct *wim);

int
delete_wim_image(WIMStruct *wim, int image);

/* Internal open flags (pass to open_wim_as_WIMStruct(), not wimlib_open_wim())
 */
#define WIMLIB_OPEN_FLAG_FROM_PIPE	0x80000000

int
open_wim_as_WIMStruct(const void *wim_filename_or_fd, int open_flags,
		      WIMStruct **wim_ret,
		      wimlib_progress_func_t progfunc, void *progctx);

int
can_modify_wim(WIMStruct *wim);

#endif /* _WIMLIB_WIM_H */
