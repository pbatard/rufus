/*
 * wim.c - High-level code dealing with WIMStructs and images.
 */

/*
 * Copyright 2012-2023 Eric Biggers
 * Copyright 2025 Pete Batard <pete@akeo.ie>
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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wimlib.h"
#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/cpu_features.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/file_io.h"
#include "wimlib/integrity.h"
#include "wimlib/metadata.h"
#include "wimlib/security.h"
#include "wimlib/threads.h"
#include "wimlib/wim.h"
#include "wimlib/xml.h"
#include "wimlib/win32.h"

/* Information about the available compression types for the WIM format.  */
static const struct {
	const tchar *name;
	u32 min_chunk_size;
	u32 max_chunk_size;
	u32 default_nonsolid_chunk_size;
	u32 default_solid_chunk_size;
} wim_ctype_info[] = {
	[WIMLIB_COMPRESSION_TYPE_NONE] = {
		.name = T("None"),
		.min_chunk_size = 0,
		.max_chunk_size = 0,
		.default_nonsolid_chunk_size = 0,
		.default_solid_chunk_size = 0,
	},
	[WIMLIB_COMPRESSION_TYPE_XPRESS] = {
		.name = T("XPRESS"),
		.min_chunk_size = 4096,
		.max_chunk_size = 65536,
		.default_nonsolid_chunk_size = 32768,
		.default_solid_chunk_size = 32768,
	},
	[WIMLIB_COMPRESSION_TYPE_LZX] = {
		.name = T("LZX"),
		.min_chunk_size = 32768,
		.max_chunk_size = 2097152,
		.default_nonsolid_chunk_size = 32768,
		.default_solid_chunk_size = 32768,
	},
	[WIMLIB_COMPRESSION_TYPE_LZMS] = {
		.name = T("LZMS"),
		.min_chunk_size = 32768,
		.max_chunk_size = 1073741824,
		.default_nonsolid_chunk_size = 131072,
		.default_solid_chunk_size = 67108864,
	},
};

/* Is the specified compression type valid?  */
static bool
wim_compression_type_valid(enum wimlib_compression_type ctype)
{
	return (unsigned)ctype < ARRAY_LEN(wim_ctype_info) &&
	       wim_ctype_info[(unsigned)ctype].name != NULL;
}

/* Is the specified chunk size valid for the compression type?  */
static bool
wim_chunk_size_valid(u32 chunk_size, enum wimlib_compression_type ctype)
{
	if (!(chunk_size == 0 || is_power_of_2(chunk_size)))
		return false;

	return chunk_size >= wim_ctype_info[(unsigned)ctype].min_chunk_size &&
	       chunk_size <= wim_ctype_info[(unsigned)ctype].max_chunk_size;
}

/* Return the default chunk size to use for the specified compression type in
 * non-solid resources.  */
static u32
wim_default_nonsolid_chunk_size(enum wimlib_compression_type ctype)
{
	return wim_ctype_info[(unsigned)ctype].default_nonsolid_chunk_size;
}

/* Return the default chunk size to use for the specified compression type in
 * solid resources.  */
static u32
wim_default_solid_chunk_size(enum wimlib_compression_type ctype)
{
	return wim_ctype_info[(unsigned)ctype].default_solid_chunk_size;
}

/* Return the default compression type to use in solid resources.  */
static enum wimlib_compression_type
wim_default_solid_compression_type(void)
{
	return WIMLIB_COMPRESSION_TYPE_LZMS;
}

static int
is_blob_in_solid_resource(struct blob_descriptor *blob, void *_ignore)
{
	return blob->blob_location == BLOB_IN_WIM &&
		(blob->rdesc->flags & WIM_RESHDR_FLAG_SOLID);
}

bool
wim_has_solid_resources(const WIMStruct *wim)
{
	return for_blob_in_table(wim->blob_table, is_blob_in_solid_resource, NULL);
}

static WIMStruct *
new_wim_struct(void)
{
	WIMStruct *wim = CALLOC(1, sizeof(WIMStruct));
	if (!wim)
		return NULL;

	wim->refcnt = 1;
	filedes_invalidate(&wim->in_fd);
	filedes_invalidate(&wim->out_fd);
	wim->out_solid_compression_type = wim_default_solid_compression_type();
	wim->out_solid_chunk_size = wim_default_solid_chunk_size(
					wim->out_solid_compression_type);
	return wim;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_create_new_wim(enum wimlib_compression_type ctype, WIMStruct **wim_ret)
{
	int ret;
	WIMStruct *wim;

	ret = wimlib_global_init(0);
	if (ret)
		return ret;

	if (!wim_ret)
		return WIMLIB_ERR_INVALID_PARAM;

	if (!wim_compression_type_valid(ctype))
		return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

	wim = new_wim_struct();
	if (!wim)
		return WIMLIB_ERR_NOMEM;

	/* Fill in wim->hdr with default values */
	wim->hdr.magic = WIM_MAGIC;
	wim->hdr.wim_version = WIM_VERSION_DEFAULT;
	wim->hdr.part_number = 1;
	wim->hdr.total_parts = 1;
	wim->compression_type = WIMLIB_COMPRESSION_TYPE_NONE;

	/* Set the output compression type */
	wim->out_compression_type = ctype;
	wim->out_chunk_size = wim_default_nonsolid_chunk_size(ctype);

	/* Allocate an empty XML info and blob table */
	wim->xml_info = xml_new_info_struct();
	wim->blob_table = new_blob_table(64);
	if (!wim->xml_info || !wim->blob_table) {
		wimlib_free(wim);
		return WIMLIB_ERR_NOMEM;
	}

	*wim_ret = wim;
	return 0;
}

static void
unload_image_metadata(struct wim_image_metadata *imd)
{
	free_dentry_tree(imd->root_dentry, NULL);
	imd->root_dentry = NULL;
	free_wim_security_data(imd->security_data);
	imd->security_data = NULL;
	INIT_HLIST_HEAD(&imd->inode_list);
}

/* Release a reference to the specified image metadata.  This assumes that no
 * WIMStruct has the image selected.  */
void
put_image_metadata(struct wim_image_metadata *imd)
{
	struct blob_descriptor *blob, *tmp;

	if (!imd)
		return;
	wimlib_assert(imd->refcnt > 0);
	if (--imd->refcnt != 0)
		return;
	wimlib_assert(imd->selected_refcnt == 0);
	unload_image_metadata(imd);
	list_for_each_entry_safe(blob, tmp, &imd->unhashed_blobs, unhashed_list)
		free_blob_descriptor(blob);
	free_blob_descriptor(imd->metadata_blob);
	FREE(imd);
}

/* Appends the specified image metadata structure to the array of image metadata
 * for a WIM, and increments the image count. */
int
append_image_metadata(WIMStruct *wim, struct wim_image_metadata *imd)
{
	struct wim_image_metadata **imd_array;

	if (!wim_has_metadata(wim))
		return WIMLIB_ERR_METADATA_NOT_FOUND;

	if (wim->hdr.image_count >= MAX_IMAGES)
		return WIMLIB_ERR_IMAGE_COUNT;

	imd_array = REALLOC(wim->image_metadata,
			    sizeof(wim->image_metadata[0]) * (wim->hdr.image_count + 1));

	if (!imd_array)
		return WIMLIB_ERR_NOMEM;
	wim->image_metadata = imd_array;
	imd_array[wim->hdr.image_count++] = imd;
	return 0;
}

static struct wim_image_metadata *
new_image_metadata(struct blob_descriptor *metadata_blob,
		   struct wim_security_data *security_data)
{
	struct wim_image_metadata *imd;

	imd = CALLOC(1, sizeof(*imd));
	if (!imd)
		return NULL;

	metadata_blob->is_metadata = 1;
	imd->refcnt = 1;
	imd->selected_refcnt = 0;
	imd->root_dentry = NULL;
	imd->security_data = security_data;
	imd->metadata_blob = metadata_blob;
	INIT_HLIST_HEAD(&imd->inode_list);
	INIT_LIST_HEAD(&imd->unhashed_blobs);
	imd->stats_outdated = false;
	return imd;
}

/* Create an image metadata structure for a new empty image.  */
struct wim_image_metadata *
new_empty_image_metadata(void)
{
	struct blob_descriptor *metadata_blob;
	struct wim_security_data *security_data;
	struct wim_image_metadata *imd;

	metadata_blob = new_blob_descriptor();
	security_data = new_wim_security_data();
	if (metadata_blob && security_data) {
		metadata_blob->refcnt = 1;
		imd = new_image_metadata(metadata_blob, security_data);
		if (imd)
			return imd;
	}
	free_blob_descriptor(metadata_blob);
	FREE(security_data);
	return NULL;
}

/* Create an image metadata structure that refers to the specified metadata
 * resource and is initially not loaded.  */
struct wim_image_metadata *
new_unloaded_image_metadata(struct blob_descriptor *metadata_blob)
{
	wimlib_assert(metadata_blob->blob_location == BLOB_IN_WIM);
	return new_image_metadata(metadata_blob, NULL);
}

/*
 * Load the metadata for the specified WIM image into memory and set it
 * as the WIMStruct's currently selected image.
 *
 * @wim
 *	The WIMStruct for the WIM.
 * @image
 *	The 1-based index of the image in the WIM to select.
 *
 * On success, 0 will be returned, wim->current_image will be set to
 * @image, and wim_get_current_image_metadata() can be used to retrieve
 * metadata information for the image.
 *
 * On failure, WIMLIB_ERR_INVALID_IMAGE, WIMLIB_ERR_METADATA_NOT_FOUND,
 * or another error code will be returned.
 */
int
select_wim_image(WIMStruct *wim, int image)
{
	struct wim_image_metadata *imd;
	int ret;

	if (image == WIMLIB_NO_IMAGE)
		return WIMLIB_ERR_INVALID_IMAGE;

	if (image == wim->current_image)
		return 0;

	if (image < 1 || image > wim->hdr.image_count)
		return WIMLIB_ERR_INVALID_IMAGE;

	if (!wim_has_metadata(wim))
		return WIMLIB_ERR_METADATA_NOT_FOUND;

	deselect_current_wim_image(wim);

	imd = wim->image_metadata[image - 1];
	if (!is_image_loaded(imd)) {
		ret = read_metadata_resource(imd);
		if (ret)
			return ret;
	}
	wim->current_image = image;
	imd->selected_refcnt++;
	return 0;
}

/*
 * Deselect the WIMStruct's currently selected image, if any.  To reduce memory
 * usage, possibly unload the newly deselected image's metadata from memory.
 */
void
deselect_current_wim_image(WIMStruct *wim)
{
	struct wim_image_metadata *imd;

	if (wim->current_image == WIMLIB_NO_IMAGE)
		return;
	imd = wim_get_current_image_metadata(wim);
	wimlib_assert(imd->selected_refcnt > 0);
	imd->selected_refcnt--;
	wim->current_image = WIMLIB_NO_IMAGE;

	if (can_unload_image(imd)) {
		wimlib_assert(list_empty(&imd->unhashed_blobs));
		unload_image_metadata(imd);
	}
}

/*
 * Calls a function on images in the WIM.  If @image is WIMLIB_ALL_IMAGES,
 * @visitor is called on the WIM once for each image, with each image selected
 * as the current image in turn.  If @image is a certain image, @visitor is
 * called on the WIM only once, with that image selected.
 */
int
for_image(WIMStruct *wim, int image, int (*visitor)(WIMStruct *))
{
	int ret;
	int start;
	int end;
	int i;

	if (image == WIMLIB_ALL_IMAGES) {
		start = 1;
		end = wim->hdr.image_count;
	} else if (image >= 1 && image <= wim->hdr.image_count) {
		start = image;
		end = image;
	} else {
		return WIMLIB_ERR_INVALID_IMAGE;
	}
	for (i = start; i <= end; i++) {
		ret = select_wim_image(wim, i);
		if (ret != 0)
			return ret;
		ret = visitor(wim);
		if (ret != 0)
			return ret;
	}
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_resolve_image(WIMStruct *wim, const tchar *image_name_or_num)
{
	tchar *p;
	long image;
	int i;

	if (!image_name_or_num || !*image_name_or_num)
		return WIMLIB_NO_IMAGE;

	if (!tstrcasecmp(image_name_or_num, T("all"))
	    || !tstrcasecmp(image_name_or_num, T("*")))
		return WIMLIB_ALL_IMAGES;
	image = tstrtol(image_name_or_num, &p, 10);
	if (p != image_name_or_num && *p == T('\0') && image > 0) {
		if (image > wim->hdr.image_count)
			return WIMLIB_NO_IMAGE;
		return image;
	} else {
		for (i = 1; i <= wim->hdr.image_count; i++) {
			if (!tstrcmp(image_name_or_num,
				     wimlib_get_image_name(wim, i)))
				return i;
		}
		return WIMLIB_NO_IMAGE;
	}
}

/* API function documented in wimlib.h  */
WIMLIBAPI void
wimlib_print_available_images(const WIMStruct *wim, int image)
{
	int first;
	int last;
	int i;
	int n = 80;
	if (image == WIMLIB_ALL_IMAGES) {
		tprintf(T("Available Images:\n"));
		first = 1;
		last = wim->hdr.image_count;
	} else if (image >= 1 && image <= wim->hdr.image_count) {
		tprintf(T("Information for Image %d\n"), image);
		first = image;
		last = image;
	} else {
		tprintf(T("wimlib_print_available_images(): Invalid image %d"),
			image);
		return;
	}
	for (i = 0; i < n - 1; i++)
		tputchar(T('-'));
	tputchar(T('\n'));
	for (i = first; i <= last; i++)
		xml_print_image_info(wim->xml_info, i);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_get_wim_info(WIMStruct *wim, struct wimlib_wim_info *info)
{
	memset(info, 0, sizeof(struct wimlib_wim_info));
	copy_guid(info->guid, wim->hdr.guid);
	info->image_count = wim->hdr.image_count;
	info->boot_index = wim->hdr.boot_idx;
	info->wim_version = wim->hdr.wim_version;
	info->chunk_size = wim->chunk_size;
	info->part_number = wim->hdr.part_number;
	info->total_parts = wim->hdr.total_parts;
	info->compression_type = wim->compression_type;
	info->total_bytes = xml_get_total_bytes(wim->xml_info);
	info->has_integrity_table = wim_has_integrity_table(wim);
	info->opened_from_file = (wim->filename != NULL);
	info->is_readonly = (wim->hdr.flags & WIM_HDR_FLAG_READONLY) ||
			     (wim->hdr.total_parts != 1) ||
			     (wim->filename && taccess(wim->filename, W_OK));
	info->has_rpfix = (wim->hdr.flags & WIM_HDR_FLAG_RP_FIX) != 0;
	info->is_marked_readonly = (wim->hdr.flags & WIM_HDR_FLAG_READONLY) != 0;
	info->write_in_progress = (wim->hdr.flags & WIM_HDR_FLAG_WRITE_IN_PROGRESS) != 0;
	info->metadata_only = (wim->hdr.flags & WIM_HDR_FLAG_METADATA_ONLY) != 0;
	info->resource_only = (wim->hdr.flags & WIM_HDR_FLAG_RESOURCE_ONLY) != 0;
	info->spanned = (wim->hdr.flags & WIM_HDR_FLAG_SPANNED) != 0;
	info->pipable = wim_is_pipable(wim);
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_wim_info(WIMStruct *wim, const struct wimlib_wim_info *info, int which)
{
	if (which & ~(WIMLIB_CHANGE_READONLY_FLAG |
		      WIMLIB_CHANGE_GUID |
		      WIMLIB_CHANGE_BOOT_INDEX |
		      WIMLIB_CHANGE_RPFIX_FLAG))
		return WIMLIB_ERR_INVALID_PARAM;

	if ((which & WIMLIB_CHANGE_BOOT_INDEX) &&
	    info->boot_index > wim->hdr.image_count)
		return WIMLIB_ERR_INVALID_IMAGE;

	if (which & WIMLIB_CHANGE_READONLY_FLAG) {
		if (info->is_marked_readonly)
			wim->hdr.flags |= WIM_HDR_FLAG_READONLY;
		else
			wim->hdr.flags &= ~WIM_HDR_FLAG_READONLY;
	}

	if (which & WIMLIB_CHANGE_GUID)
		copy_guid(wim->hdr.guid, info->guid);

	if (which & WIMLIB_CHANGE_BOOT_INDEX)
		wim->hdr.boot_idx = info->boot_index;

	if (which & WIMLIB_CHANGE_RPFIX_FLAG) {
		if (info->has_rpfix)
			wim->hdr.flags |= WIM_HDR_FLAG_RP_FIX;
		else
			wim->hdr.flags &= ~WIM_HDR_FLAG_RP_FIX;
	}
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_output_compression_type(WIMStruct *wim,
				   enum wimlib_compression_type ctype)
{
	if (!wim_compression_type_valid(ctype))
		return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

	wim->out_compression_type = ctype;

	/* Reset the chunk size if it's no longer valid.  */
	if (!wim_chunk_size_valid(wim->out_chunk_size, ctype))
		wim->out_chunk_size = wim_default_nonsolid_chunk_size(ctype);
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_output_pack_compression_type(WIMStruct *wim,
					enum wimlib_compression_type ctype)
{
	if (!wim_compression_type_valid(ctype))
		return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

	/* Solid resources can't be uncompressed.  */
	if (ctype == WIMLIB_COMPRESSION_TYPE_NONE)
		return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;

	wim->out_solid_compression_type = ctype;

	/* Reset the chunk size if it's no longer valid.  */
	if (!wim_chunk_size_valid(wim->out_solid_chunk_size, ctype))
		wim->out_solid_chunk_size = wim_default_solid_chunk_size(ctype);
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_output_chunk_size(WIMStruct *wim, u32 chunk_size)
{
	if (chunk_size == 0) {
		wim->out_chunk_size =
			wim_default_nonsolid_chunk_size(wim->out_compression_type);
		return 0;
	}

	if (!wim_chunk_size_valid(chunk_size, wim->out_compression_type))
		return WIMLIB_ERR_INVALID_CHUNK_SIZE;

	wim->out_chunk_size = chunk_size;
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_set_output_pack_chunk_size(WIMStruct *wim, u32 chunk_size)
{
	if (chunk_size == 0) {
		wim->out_solid_chunk_size =
			wim_default_solid_chunk_size(wim->out_solid_compression_type);
		return 0;
	}

	if (!wim_chunk_size_valid(chunk_size, wim->out_solid_compression_type))
		return WIMLIB_ERR_INVALID_CHUNK_SIZE;

	wim->out_solid_chunk_size = chunk_size;
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI const tchar *
wimlib_get_compression_type_string(enum wimlib_compression_type ctype)
{
	if (!wim_compression_type_valid(ctype))
		return T("Invalid");

	return wim_ctype_info[(unsigned)ctype].name;
}

WIMLIBAPI void
wimlib_register_progress_function(WIMStruct *wim,
				  wimlib_progress_func_t progfunc,
				  void *progctx)
{
	wim->progfunc = progfunc;
	wim->progctx = progctx;
}

#ifdef WITH_LIBCDIO
static int 
open_iso_wim_file(const tchar* filename, struct filedes* fd_ret)
{
	int ret = 0;
	size_t n;
	char *iso_filename, *iso_path = NULL;
	udf_dirent_t *p_udf_root;

	/* If the wim path contains a pipe separator, look it up inside an ISO */
	if (tstr_to_utf8(filename, (tstrlen(filename) + 1) * sizeof(tchar), &iso_path, &n))
		return WIMLIB_ERR_NOMEM;
	iso_filename = strchr(iso_path, '|');
	if (iso_filename == NULL) {
		ret = WIMLIB_ERR_NO_FILENAME;
		goto out;
	}

	*iso_filename++ = '\0';
	filedes_init(fd_ret, 0);

	/* Try to open as UDF image */
	fd_ret->p_udf = udf_open(iso_path);
	if (!fd_ret->p_udf)
		goto try_iso;
	p_udf_root = udf_get_root(fd_ret->p_udf, true, 0);
	if (!p_udf_root) {
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}
	fd_ret->p_udf_file = udf_fopen(p_udf_root, iso_filename);
	udf_dirent_free(p_udf_root);
	if (!fd_ret->p_udf_file) {
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}
	fd_ret->is_udf = 1;
	goto out;

try_iso:
	/* Try to open as ISO9660 image */
	fd_ret->p_iso = iso9660_open_ext(iso_path, ISO_EXTENSION_ALL);
	if (!fd_ret->p_iso) {
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}
	fd_ret->p_iso_file = iso9660_ifs_stat_translate(fd_ret->p_iso, iso_filename);
	if (!fd_ret->p_iso_file) {
		ret = WIMLIB_ERR_OPEN;
		goto out;
	}
	fd_ret->is_iso = 1;

out:
	FREE(iso_path);
	/* Because we use an union, make sure fd is cleared on error */
	if (ret)
		fd_ret->fd = 0;
	return ret;
}
#endif

static int
open_wim_file(const tchar *filename, struct filedes *fd_ret)
{
	int raw_fd;

#ifdef WITH_LIBCDIO
	if (open_iso_wim_file(filename, fd_ret) == 0)
		return 0;
#endif
	raw_fd = topen(filename, O_RDONLY | O_BINARY);
	if (raw_fd < 0) {
		ERROR_WITH_ERRNO("Can't open \"%"TS"\" read-only", filename);
		return WIMLIB_ERR_OPEN;
	}
	filedes_init(fd_ret, raw_fd);
	return 0;
}

/*
 * Begins the reading of a WIM file; opens the file and reads its header and
 * blob table, and optionally checks the integrity.
 */
static int
begin_read(WIMStruct *wim, const void *wim_filename_or_fd, int open_flags)
{
	int ret;
	const tchar *wimfile;

	if (open_flags & WIMLIB_OPEN_FLAG_FROM_PIPE) {
		wimfile = NULL;
		filedes_init(&wim->in_fd, *(const int*)wim_filename_or_fd);
		wim->in_fd.is_pipe = 1;
	} else {
		struct stat stbuf;

		wimfile = wim_filename_or_fd;
		ret = open_wim_file(wimfile, &wim->in_fd);
		if (ret)
			return ret;

		/* The file size is needed for enforcing some limits later. */
#ifdef WITH_LIBCDIO
		if ((wim->in_fd.is_udf | wim->in_fd.is_iso) &&
			(open_flags & WIMLIB_OPEN_FLAG_WRITE_ACCESS))
			return WIMLIB_ERR_WIM_IS_READONLY;
		if (wim->in_fd.is_udf)
			wim->file_size = udf_get_file_length(wim->in_fd.p_udf_file);
		else if (wim->in_fd.is_iso)
			wim->file_size = wim->in_fd.p_iso_file->total_size;
		else
#endif
		if (fstat(wim->in_fd.fd, &stbuf) == 0)
			wim->file_size = stbuf.st_size;

		/* The absolute path to the WIM is requested so that
		 * wimlib_overwrite() still works even if the process changes
		 * its working directory.  This actually happens if a WIM is
		 * mounted read-write, since the FUSE thread changes directory
		 * to "/", and it needs to be able to find the WIM file again.
		 *
		 * This will break if the full path to the WIM changes in the
		 * intervening time...
		 *
		 * Warning: in Windows native builds, realpath() calls the
		 * replacement function in win32_replacements.c.
		 */
#ifdef WITH_LIBCDIO
		/* No overwriting for libcdio, so simply duplicate */
		if (tstrchr(wimfile, T('|')))
			wim->filename = tstrdup(wimfile);
		else
#endif
		wim->filename = realpath(wimfile, NULL);
		if (!wim->filename) {
			ERROR_WITH_ERRNO("Failed to get full path to file "
					 "\"%"TS"\"", wimfile);
			if (errno == ENOMEM)
				return WIMLIB_ERR_NOMEM;
			else
				return WIMLIB_ERR_NO_FILENAME;
		}
	}

	ret = read_wim_header(wim, &wim->hdr);
	if (ret)
		return ret;

	if (wim->hdr.flags & WIM_HDR_FLAG_WRITE_IN_PROGRESS) {
		WARNING("The WIM_HDR_FLAG_WRITE_IN_PROGRESS flag is set in the header of\n"
			"          \"%"TS"\".  It may be being changed by another process,\n"
			"          or a process may have crashed while writing the WIM.",
			wimfile);
	}

	if (open_flags & WIMLIB_OPEN_FLAG_WRITE_ACCESS) {
		ret = can_modify_wim(wim);
		if (ret)
			return ret;
	}

	if ((open_flags & WIMLIB_OPEN_FLAG_ERROR_IF_SPLIT) &&
	    (wim->hdr.total_parts != 1))
		return WIMLIB_ERR_IS_SPLIT_WIM;

	/* If the boot index is invalid, print a warning and set it to 0 */
	if (wim->hdr.boot_idx > wim->hdr.image_count) {
		WARNING("Ignoring invalid boot index.");
		wim->hdr.boot_idx = 0;
	}

	/* Check and cache the compression type */
	if (wim->hdr.flags & WIM_HDR_FLAG_COMPRESSION) {
		if (wim->hdr.flags & WIM_HDR_FLAG_COMPRESS_LZX) {
			wim->compression_type = WIMLIB_COMPRESSION_TYPE_LZX;
		} else if (wim->hdr.flags & (WIM_HDR_FLAG_COMPRESS_XPRESS |
					     WIM_HDR_FLAG_COMPRESS_XPRESS_2)) {
			wim->compression_type = WIMLIB_COMPRESSION_TYPE_XPRESS;
		} else if (wim->hdr.flags & WIM_HDR_FLAG_COMPRESS_LZMS) {
			wim->compression_type = WIMLIB_COMPRESSION_TYPE_LZMS;
		} else {
			return WIMLIB_ERR_INVALID_COMPRESSION_TYPE;
		}
	} else {
		wim->compression_type = WIMLIB_COMPRESSION_TYPE_NONE;
	}
	wim->out_compression_type = wim->compression_type;

	/* Check and cache the chunk size.  */
	wim->chunk_size = wim->hdr.chunk_size;
	wim->out_chunk_size = wim->chunk_size;
	if (!wim_chunk_size_valid(wim->chunk_size, wim->compression_type)) {
		ERROR("Invalid chunk size (%"PRIu32" bytes) "
		      "for compression type %"TS"!", wim->chunk_size,
		      wimlib_get_compression_type_string(wim->compression_type));
		return WIMLIB_ERR_INVALID_CHUNK_SIZE;
	}

	if (open_flags & WIMLIB_OPEN_FLAG_CHECK_INTEGRITY) {
		ret = check_wim_integrity(wim);
		if (ret == WIM_INTEGRITY_NONEXISTENT) {
			WARNING("\"%"TS"\" does not contain integrity "
				"information.  Skipping integrity check.",
				wimfile);
		} else if (ret == WIM_INTEGRITY_NOT_OK) {
			return WIMLIB_ERR_INTEGRITY;
		} else if (ret != WIM_INTEGRITY_OK) {
			return ret;
		}
	}

	if (wim->hdr.image_count != 0 && wim->hdr.part_number == 1) {
		wim->image_metadata = CALLOC(wim->hdr.image_count,
					     sizeof(wim->image_metadata[0]));
		if (!wim->image_metadata)
			return WIMLIB_ERR_NOMEM;
	}

	if (open_flags & WIMLIB_OPEN_FLAG_FROM_PIPE) {
		wim->blob_table = new_blob_table(64);
		if (!wim->blob_table)
			return WIMLIB_ERR_NOMEM;
	} else {
		if (wim->hdr.blob_table_reshdr.uncompressed_size == 0 &&
		    wim->hdr.xml_data_reshdr.uncompressed_size == 0)
			return WIMLIB_ERR_WIM_IS_INCOMPLETE;

		ret = read_wim_xml_data(wim);
		if (ret)
			return ret;

		if (xml_get_image_count(wim->xml_info) != wim->hdr.image_count) {
			ERROR("The WIM's header is inconsistent with its XML data.\n"
			      "        Please submit a bug report if you believe this "
			      "WIM file should be considered valid.");
			return WIMLIB_ERR_IMAGE_COUNT;
		}

		ret = read_blob_table(wim);
		if (ret)
			return ret;
	}
	return 0;
}

int
open_wim_as_WIMStruct(const void *wim_filename_or_fd, int open_flags,
		      WIMStruct **wim_ret,
		      wimlib_progress_func_t progfunc, void *progctx)
{
	WIMStruct *wim;
	int ret;

	ret = wimlib_global_init(0);
	if (ret)
		return ret;

	wim = new_wim_struct();
	if (!wim)
		return WIMLIB_ERR_NOMEM;

	wim->progfunc = progfunc;
	wim->progctx = progctx;

	ret = begin_read(wim, wim_filename_or_fd, open_flags);
	if (ret) {
		wimlib_free(wim);
		return ret;
	}

	*wim_ret = wim;
	return 0;
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_open_wim_with_progress(const tchar *wimfile, int open_flags,
			      WIMStruct **wim_ret,
			      wimlib_progress_func_t progfunc, void *progctx)
{
	if (open_flags & ~(WIMLIB_OPEN_FLAG_CHECK_INTEGRITY |
			   WIMLIB_OPEN_FLAG_ERROR_IF_SPLIT |
			   WIMLIB_OPEN_FLAG_WRITE_ACCESS))
		return WIMLIB_ERR_INVALID_PARAM;

	if (!wimfile || !*wimfile || !wim_ret)
		return WIMLIB_ERR_INVALID_PARAM;

	return open_wim_as_WIMStruct(wimfile, open_flags, wim_ret,
				     progfunc, progctx);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_open_wim(const tchar *wimfile, int open_flags, WIMStruct **wim_ret)
{
	return wimlib_open_wim_with_progress(wimfile, open_flags, wim_ret,
					     NULL, NULL);
}

/* Checksum all blobs that are unhashed (other than the metadata blobs), merging
 * them into the blob table as needed.  This is a no-op unless files have been
 * added to an image in the same WIMStruct.  */
int
wim_checksum_unhashed_blobs(WIMStruct *wim)
{
	int ret;

	if (!wim_has_metadata(wim))
		return 0;
	for (int i = 0; i < wim->hdr.image_count; i++) {
		struct blob_descriptor *blob, *tmp;
		struct wim_image_metadata *imd = wim->image_metadata[i];
		image_for_each_unhashed_blob_safe(blob, tmp, imd) {
			struct blob_descriptor *new_blob;
			ret = hash_unhashed_blob(blob, wim->blob_table, &new_blob);
			if (ret)
				return ret;
			if (new_blob != blob)
				free_blob_descriptor(blob);
		}
	}
	return 0;
}

/*
 * can_modify_wim - Check if a given WIM is writeable.  This is only the case if
 * it meets the following three conditions:
 *
 * 1. Write access is allowed to the underlying file (if any) at the filesystem level.
 * 2. The WIM is not part of a spanned set.
 * 3. The WIM_HDR_FLAG_READONLY flag is not set in the WIM header.
 *
 * Return value is 0 if writable; WIMLIB_ERR_WIM_IS_READONLY otherwise.
 */
int
can_modify_wim(WIMStruct *wim)
{
	if (wim->filename) {
		if (taccess(wim->filename, W_OK)) {
			ERROR_WITH_ERRNO("Can't modify \"%"TS"\"", wim->filename);
			return WIMLIB_ERR_WIM_IS_READONLY;
		}
	}
	if (wim->hdr.total_parts != 1) {
		ERROR("Cannot modify \"%"TS"\": is part of a split WIM",
		      wim->filename);
		return WIMLIB_ERR_WIM_IS_READONLY;
	}
	if (wim->hdr.flags & WIM_HDR_FLAG_READONLY) {
		ERROR("Cannot modify \"%"TS"\": is marked read-only",
		      wim->filename);
		return WIMLIB_ERR_WIM_IS_READONLY;
	}
	return 0;
}

/* Release a reference to a WIMStruct.  If the reference count reaches 0, the
 * WIMStruct is freed.  */
void
wim_decrement_refcnt(WIMStruct *wim)
{
	wimlib_assert(wim->refcnt > 0);
	if (--wim->refcnt != 0)
		return;
#ifdef WITH_LIBCDIO
	if (wim->in_fd.is_udf) {
		udf_dirent_free(wim->in_fd.p_udf_file);
		udf_close(wim->in_fd.p_udf);
	} else if (wim->in_fd.is_iso) {
		iso9660_stat_free(wim->in_fd.p_iso_file);
		iso9660_close(wim->in_fd.p_iso);
	} else {
#endif
		if (filedes_valid(&wim->in_fd))
			filedes_close(&wim->in_fd);
		if (filedes_valid(&wim->out_fd))
			filedes_close(&wim->out_fd);
#ifdef WITH_LIBCDIO
	}
#endif
	wimlib_free_decompressor(wim->decompressor);
	xml_free_info_struct(wim->xml_info);
	FREE(wim->filename);
	FREE(wim);
}

/* API function documented in wimlib.h  */
WIMLIBAPI void
wimlib_free(WIMStruct *wim)
{
	if (!wim)
		return;

	/* The blob table and image metadata are freed immediately, but other
	 * members of the WIMStruct such as the input file descriptor are
	 * retained until no more exported resources reference the WIMStruct. */

	free_blob_table(wim->blob_table);
	wim->blob_table = NULL;
	if (wim->image_metadata != NULL) {
		deselect_current_wim_image(wim);
		for (int i = 0; i < wim->hdr.image_count; i++)
			put_image_metadata(wim->image_metadata[i]);
		FREE(wim->image_metadata);
		wim->image_metadata = NULL;
	}

	wim_decrement_refcnt(wim);
}

/* API function documented in wimlib.h  */
WIMLIBAPI u32
wimlib_get_version(void)
{
	return (WIMLIB_MAJOR_VERSION << 20) |
	       (WIMLIB_MINOR_VERSION << 10) |
		WIMLIB_PATCH_VERSION;
}

WIMLIBAPI const tchar *
wimlib_get_version_string(void)
{
	return T(PACKAGE_VERSION);
}

static volatile uint16_t lib_initialization_mutex = 0;
static bool lib_initialized = false;

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_global_init(int init_flags)
{
	int ret = 0;

	// Non problematic init/cleanup mutex (Windows only) to keep static
	// analysers happy. Only one thread at a time can run the code between
	// initial InterlockedIncrement and ending InterlockedDecrement.
	while (InterlockedIncrement16(&lib_initialization_mutex) >= 2) {
		InterlockedDecrement16(&lib_initialization_mutex);
		Sleep(100);
	}

	if (lib_initialized)
		goto out_unlock;

	if (!wimlib_error_file)
		wimlib_error_file = stderr;

	ret = WIMLIB_ERR_INVALID_PARAM;
	if (init_flags & ~(WIMLIB_INIT_FLAG_ASSUME_UTF8 |
			   WIMLIB_INIT_FLAG_DONT_ACQUIRE_PRIVILEGES |
			   WIMLIB_INIT_FLAG_STRICT_CAPTURE_PRIVILEGES |
			   WIMLIB_INIT_FLAG_STRICT_APPLY_PRIVILEGES |
			   WIMLIB_INIT_FLAG_DEFAULT_CASE_SENSITIVE |
			   WIMLIB_INIT_FLAG_DEFAULT_CASE_INSENSITIVE))
		goto out_unlock;

	ret = WIMLIB_ERR_INVALID_PARAM;
	if ((init_flags & (WIMLIB_INIT_FLAG_DEFAULT_CASE_SENSITIVE |
			   WIMLIB_INIT_FLAG_DEFAULT_CASE_INSENSITIVE))
			== (WIMLIB_INIT_FLAG_DEFAULT_CASE_SENSITIVE |
			    WIMLIB_INIT_FLAG_DEFAULT_CASE_INSENSITIVE))
		goto out_unlock;

	init_cpu_features();
#ifdef _WIN32
	ret = win32_global_init(init_flags);
	if (ret)
		goto out_unlock;
#endif
	init_upcase();
	if (init_flags & WIMLIB_INIT_FLAG_DEFAULT_CASE_SENSITIVE)
		default_ignore_case = false;
	else if (init_flags & WIMLIB_INIT_FLAG_DEFAULT_CASE_INSENSITIVE)
		default_ignore_case = true;
	lib_initialized = true;
	ret = 0;
out_unlock:
	InterlockedDecrement16(&lib_initialization_mutex);
	return ret;
}

/* API function documented in wimlib.h  */
WIMLIBAPI void
wimlib_global_cleanup(void)
{
	while (InterlockedIncrement16(&lib_initialization_mutex) >= 2) {
		InterlockedDecrement16(&lib_initialization_mutex);
		Sleep(100);
	}

	if (!lib_initialized)
		goto out_unlock;

#ifdef _WIN32
	win32_global_cleanup();
#endif

	wimlib_set_error_file(NULL);
	lib_initialized = false;

out_unlock:
	InterlockedDecrement16(&lib_initialization_mutex);
}
