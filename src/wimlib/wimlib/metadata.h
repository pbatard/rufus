#ifndef _WIMLIB_METADATA_H
#define _WIMLIB_METADATA_H

#include "wimlib/blob_table.h"
#include "wimlib/list.h"
#include "wimlib/types.h"
#include "wimlib/wim.h"

/*
 * This structure holds the directory tree that comprises a WIM image, along
 * with other information maintained at the image level.  It is populated either
 * by reading and parsing a metadata resource or by scanning new files.
 *
 * An image that hasn't been modified from its on-disk copy is considered
 * "clean" and is loaded from its metadata resource on demand by
 * select_wim_image().  Such an image may be unloaded later to save memory when
 * a different image is selected.  An image that has been modified or has been
 * created from scratch, on the other hand, is considered "dirty" and is never
 * automatically unloaded.
 *
 * To implement exports, it's allowed that multiple WIMStructs reference the
 * same wim_image_metadata.
 */
struct wim_image_metadata {

	/* Number of WIMStructs that reference this image.  This will always be
	 * >= 1.  It may be > 1 if this image has been exported.  */
	u32 refcnt;

	/* Number of WIMStructs that have this image selected as their
	 * current_image.  This will always be <= 'refcnt' and may be 0.  */
	u32 selected_refcnt;

	/* Pointer to the root dentry of this image, or NULL if this image is
	 * completely empty or is not currently loaded.  */
	struct wim_dentry *root_dentry;

	/* Pointer to the security data of this image, or NULL if this image is
	 * not currently loaded.  */
	struct wim_security_data *security_data;

	/* Pointer to the blob descriptor for this image's metadata resource.
	 * If this image metadata is sourced from a WIM file (as opposed to
	 * being created from scratch) and hasn't been modified from the version
	 * in that WIM file, then this blob descriptor's data corresponds to the
	 * WIM backing source.  Otherwise, this blob descriptor is a dummy entry
	 * with blob_location==BLOB_NONEXISTENT.  */
	struct blob_descriptor *metadata_blob;

	/* Linked list of 'struct wim_inode's for this image, or an empty list
	 * if this image is completely empty or is not currently loaded.  */
	struct hlist_head inode_list;

	/* Linked list of 'struct blob_descriptor's for blobs that are
	 * referenced by this image's dentry tree, but have not had their SHA-1
	 * message digests calculated yet and therefore have not been inserted
	 * into the WIMStruct's blob table.  This list is appended to when files
	 * are scanned for inclusion in this WIM image.  */
	struct list_head unhashed_blobs;

	/* Are the filecount/bytecount stats (in the XML info) out of date for
	 * this image?  */
	bool stats_outdated;
};

/* Retrieve the metadata of the image in @wim currently selected with
 * select_wim_image().  */
static inline struct wim_image_metadata *
wim_get_current_image_metadata(WIMStruct *wim)
{
	return wim->image_metadata[wim->current_image - 1];
}

/* Retrieve the root dentry of the image in @wim currently selected with
 * select_wim_image().  */
static inline struct wim_dentry *
wim_get_current_root_dentry(WIMStruct *wim)
{
	return wim_get_current_image_metadata(wim)->root_dentry;
}

/* Retrieve the security data of the image in @wim currently selected with
 * select_wim_image().  */
static inline struct wim_security_data *
wim_get_current_security_data(WIMStruct *wim)
{
	return wim_get_current_image_metadata(wim)->security_data;
}

/* Return true iff the specified image has been changed since being read from
 * its backing file or has been created from scratch.  */
static inline bool
is_image_dirty(const struct wim_image_metadata *imd)
{
	/* The only possible values here are BLOB_NONEXISTENT and BLOB_IN_WIM */
	return imd->metadata_blob->blob_location == BLOB_NONEXISTENT;
}

/* Return true iff the specified image is unchanged since being read from the
 * specified backing WIM file.  */
static inline bool
is_image_unchanged_from_wim(const struct wim_image_metadata *imd,
			    const WIMStruct *wim)
{
	return !is_image_dirty(imd) && imd->metadata_blob->rdesc->wim == wim;
}

/* Mark the metadata for the specified WIM image "dirty" following changes to
 * the image's directory tree.  This records that the metadata no longer matches
 * the version in the WIM file (if any) and that its stats are out of date.  */
static inline void
mark_image_dirty(struct wim_image_metadata *imd)
{
	blob_release_location(imd->metadata_blob);
	imd->stats_outdated = true;
}

/* Return true iff the specified image is currently loaded into memory.  */
static inline bool
is_image_loaded(const struct wim_image_metadata *imd)
{
	/* Check security_data rather than root_dentry, since root_dentry will
	 * be NULL for a completely empty image whereas security_data will still
	 * be non-NULL in that case.  */
	return imd->security_data != NULL;
}

/* Return true iff it is okay to unload the specified image.  The image can be
 * unloaded if no WIMStructs have it selected and it is not dirty.  */
static inline bool
can_unload_image(const struct wim_image_metadata *imd)
{
	return imd->selected_refcnt == 0 && !is_image_dirty(imd);
}

/* Iterate over each inode in a WIM image  */
#define image_for_each_inode(inode, imd) \
	hlist_for_each_entry(inode, &(imd)->inode_list, i_hlist_node)

/* Iterate over each inode in a WIM image (safe against inode removal)  */
#define image_for_each_inode_safe(inode, tmp, imd) \
	hlist_for_each_entry_safe(inode, tmp, &(imd)->inode_list, i_hlist_node)

/* Iterate over each blob in a WIM image that has not yet been hashed */
#define image_for_each_unhashed_blob(blob, imd) \
	list_for_each_entry(blob, &(imd)->unhashed_blobs, unhashed_list)

/* Iterate over each blob in a WIM image that has not yet been hashed (safe
 * against blob removal) */
#define image_for_each_unhashed_blob_safe(blob, tmp, imd) \
	list_for_each_entry_safe(blob, tmp, &(imd)->unhashed_blobs, unhashed_list)

void
put_image_metadata(struct wim_image_metadata *imd);

int
append_image_metadata(WIMStruct *wim, struct wim_image_metadata *imd);

struct wim_image_metadata *
new_empty_image_metadata(void);

struct wim_image_metadata *
new_unloaded_image_metadata(struct blob_descriptor *metadata_blob);

#endif /* _WIMLIB_METADATA_H */
