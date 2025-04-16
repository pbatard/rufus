/*
 * metadata_resource.c
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/error.h"
#include "wimlib/metadata.h"
#include "wimlib/resource.h"
#include "wimlib/security.h"
#include "wimlib/write.h"

/* Fix the security ID for every inode to be either -1 or in bounds.  */
static void
fix_security_ids(struct wim_image_metadata *imd, const u32 num_entries)
{
	struct wim_inode *inode;
	unsigned long invalid_count = 0;

	image_for_each_inode(inode, imd) {
		if ((u32)inode->i_security_id >= num_entries) {
			if (inode->i_security_id >= 0)
				invalid_count++;
			inode->i_security_id = -1;
		}
	}
	if (invalid_count)
		WARNING("%lu inodes had invalid security IDs", invalid_count);
}

/*
 * Reads and parses a metadata resource for an image in the WIM file.
 *
 * @imd:
 *	Pointer to the image metadata structure for the image whose metadata
 *	resource we are reading.  Its `metadata_blob' member specifies the blob
 *	table entry for the metadata resource.  The rest of the image metadata
 *	entry will be filled in by this function.
 *
 * Return values:
 *	WIMLIB_ERR_SUCCESS (0)
 *	WIMLIB_ERR_INVALID_METADATA_RESOURCE
 *	WIMLIB_ERR_NOMEM
 *	WIMLIB_ERR_READ
 *	WIMLIB_ERR_UNEXPECTED_END_OF_FILE
 *	WIMLIB_ERR_DECOMPRESSION
 */
int
read_metadata_resource(struct wim_image_metadata *imd)
{
	const struct blob_descriptor *metadata_blob;
	void *buf;
	int ret;
	u8 hash[SHA1_HASH_SIZE];
	struct wim_security_data *sd;
	struct wim_dentry *root;

	metadata_blob = imd->metadata_blob;

	/*
	 * Prevent huge memory allocations when processing fuzzed files.  The
	 * case of metadata resources is tough, since a metadata resource can
	 * legitimately decompress to many times the size of the WIM file
	 * itself, e.g. in the case of an image containing many empty files with
	 * similar long filenames.  Arbitrarily choose 512x as a generous limit.
	 */
	if (metadata_blob->blob_location == BLOB_IN_WIM &&
	    metadata_blob->rdesc->wim->file_size > 0 &&
	    metadata_blob->size / 512 > metadata_blob->rdesc->wim->file_size)
		return WIMLIB_ERR_INVALID_METADATA_RESOURCE;

	/* Read the metadata resource into memory.  (It may be compressed.)  */
	ret = read_blob_into_alloc_buf(metadata_blob, &buf);
	if (ret)
		return ret;

	/* Checksum the metadata resource.  */
	sha1(buf, metadata_blob->size, hash);
	if (!hashes_equal(metadata_blob->hash, hash)) {
		ERROR("Metadata resource is corrupted "
		      "(invalid SHA-1 message digest)!");
		ret = WIMLIB_ERR_INVALID_METADATA_RESOURCE;
		goto out_free_buf;
	}

	/* Parse the metadata resource.
	 *
	 * Notes: The metadata resource consists of the security data, followed
	 * by the directory entry for the root directory, followed by all the
	 * other directory entries in the filesystem.  The subdir offset field
	 * of each directory entry gives the start of its child entries from the
	 * beginning of the metadata resource.  An end-of-directory is signaled
	 * by a directory entry of length '0', really of length 8, because
	 * that's how long the 'length' field is.  */

	ret = read_wim_security_data(buf, metadata_blob->size, &sd);
	if (ret)
		goto out_free_buf;

	ret = read_dentry_tree(buf, metadata_blob->size, sd->total_length, &root);
	if (ret)
		goto out_free_security_data;

	/* We have everything we need from the buffer now.  */
	FREE(buf);
	buf = NULL;

	/* Calculate and validate inodes.  */

	ret = dentry_tree_fix_inodes(root, &imd->inode_list);
	if (ret)
		goto out_free_dentry_tree;

	fix_security_ids(imd, sd->num_entries);

	/* Success; fill in the image_metadata structure.  */
	imd->root_dentry = root;
	imd->security_data = sd;
	INIT_LIST_HEAD(&imd->unhashed_blobs);
	return 0;

out_free_dentry_tree:
	free_dentry_tree(root, NULL);
out_free_security_data:
	free_wim_security_data(sd);
out_free_buf:
	FREE(buf);
	return ret;
}

static void
recalculate_security_data_length(struct wim_security_data *sd)
{
	u32 total_length = sizeof(u64) * sd->num_entries + 2 * sizeof(u32);
	for (u32 i = 0; i < sd->num_entries; i++)
		total_length += sd->sizes[i];
	sd->total_length = ALIGN(total_length, 8);
}

static int
prepare_metadata_resource(WIMStruct *wim, int image,
			  u8 **buf_ret, size_t *len_ret)
{
	u8 *buf;
	u8 *p;
	int ret;
	u64 subdir_offset;
	struct wim_dentry *root;
	size_t len;
	struct wim_security_data *sd;
	struct wim_image_metadata *imd;

	ret = select_wim_image(wim, image);
	if (ret)
		return ret;

	imd = wim->image_metadata[image - 1];

	root = imd->root_dentry;
	sd = imd->security_data;

	if (!root) {
		/* Empty image; create a dummy root.  */
		ret = new_filler_directory(&root);
		if (ret)
			return ret;
		imd->root_dentry = root;
	}

	/* The offset of the first child of the root dentry is equal to the
	 * total length of the security data, plus the total length of the root
	 * dentry, plus 8 bytes for an end-of-directory entry following the root
	 * dentry (shouldn't really be needed, but just in case...)  */
	recalculate_security_data_length(sd);
	subdir_offset = sd->total_length + dentry_out_total_length(root) + 8;

	/* Calculate the subdirectory offsets for the entire dentry tree.  */
	calculate_subdir_offsets(root, &subdir_offset);

	/* Total length of the metadata resource (uncompressed).  */
	len = subdir_offset;

	/* Allocate a buffer to contain the uncompressed metadata resource.  */
	buf = NULL;
	if (likely(len == subdir_offset))
		buf = MALLOC(len);
	if (!buf) {
		ERROR("Failed to allocate %"PRIu64" bytes for "
		      "metadata resource", subdir_offset);
		return WIMLIB_ERR_NOMEM;
	}

	/* Write the security data into the resource buffer.  */
	p = write_wim_security_data(sd, buf);

	/* Write the dentry tree into the resource buffer.  */
	p = write_dentry_tree(root, p);

	/* We MUST have exactly filled the buffer; otherwise we calculated its
	 * size incorrectly or wrote the data incorrectly.  */
	wimlib_assert(p - buf == len);

	*buf_ret = buf;
	*len_ret = len;
	return 0;
}

int
write_metadata_resource(WIMStruct *wim, int image, int write_resource_flags)
{
	int ret;
	u8 *buf;
	size_t len;
	struct wim_image_metadata *imd;

	ret = prepare_metadata_resource(wim, image, &buf, &len);
	if (ret)
		return ret;

	imd = wim->image_metadata[image - 1];

	/* Write the metadata resource to the output WIM using the proper
	 * compression type, in the process updating the blob descriptor for the
	 * metadata resource.  */
	ret = write_wim_resource_from_buffer(buf,
					     len,
					     true,
					     &wim->out_fd,
					     wim->out_compression_type,
					     wim->out_chunk_size,
					     &imd->metadata_blob->out_reshdr,
					     imd->metadata_blob->hash,
					     write_resource_flags);

	FREE(buf);
	return ret;
}
