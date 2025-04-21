/*
 * xml.c - deals with the XML information in WIM files
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

#include <stdlib.h>
#include <string.h>

#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/error.h"
#include "wimlib/file_io.h"
#include "wimlib/metadata.h"
#include "wimlib/resource.h"
#include "wimlib/timestamp.h"
#include "wimlib/xml.h"
#include "wimlib/xmlproc.h"
#include "wimlib/write.h"

/*
 * A wrapper around a WIM file's XML document.  The XML document contains
 * metadata about each image in the WIM file as well as metadata about the WIM
 * file itself.
 */
struct wim_xml_info {

	/* The XML document in tree form */
	struct xml_node *root;

	/* A malloc()ed array containing a pointer to the IMAGE element for each
	 * WIM image.  The image with 1-based index 'i' is at index 'i - 1' in
	 * this array.  Note: these pointers are cached values, since they could
	 * also be found by searching the document.  */
	struct xml_node **images;

	/* The number of WIM images (the length of 'images')  */
	int image_count;
};

static u64
parse_number(const tchar *str, int base)
{
	tchar *end;
	unsigned long long v;

	if (!str)
		return 0;
	v = tstrtoull(str, &end, base);
	if (end == str || *end || v >= UINT64_MAX)
		return 0;
	return v;
}

/*
 * Retrieve an unsigned integer from the contents of the specified element,
 * decoding it using the specified base.  If the element has no contents or does
 * not contain a valid number, returns 0.
 */
static u64
xml_element_get_number(const struct xml_node *element, int base)
{
	return parse_number(xml_element_get_text(element), base);
}

/*
 * Retrieve the timestamp from a time element.  This element should have child
 * elements HIGHPART and LOWPART; these elements will be used to construct a
 * Windows-style timestamp.
 */
static u64
xml_element_get_timestamp(const struct xml_node *element)
{
	u64 timestamp = 0;
	const struct xml_node *child;

	xml_node_for_each_child(element, child) {
		if (xml_node_is_element(child, T("HIGHPART")))
			timestamp |= xml_element_get_number(child, 16) << 32;
		else if (xml_node_is_element(child, T("LOWPART")))
			timestamp |= xml_element_get_number(child, 16);
	}
	return timestamp;
}

/* Create a new timestamp element and optionally link it into a tree.  */
static struct xml_node *
xml_new_element_with_timestamp(struct xml_node *parent, const tchar *name,
			       u64 timestamp)
{
	struct xml_node *element;
	tchar buf[32];

	element = xml_new_element(NULL, name);
	if (!element)
		goto err;

	tsprintf(buf, T("0x%08"PRIX32), (u32)(timestamp >> 32));
	if (!xml_new_element_with_text(element, T("HIGHPART"), buf))
		goto err;

	tsprintf(buf, T("0x%08"PRIX32), (u32)timestamp);
	if (!xml_new_element_with_text(element, T("LOWPART"), buf))
		goto err;

	if (parent)
		xml_add_child(parent, element);
	return element;

err:
	xml_free_node(element);
	return NULL;
}

/* Create a new number element and optionally link it into a tree.  */
static struct xml_node *
xml_new_element_with_u64(struct xml_node *parent, const tchar *name, u64 value)
{
	tchar buf[32];

	tsprintf(buf, T("%"PRIu64), value);
	return xml_new_element_with_text(parent, name, buf);
}

static bool
parse_index(tchar **pp, u32 *index_ret)
{
	tchar *p = *pp;
	u32 index = 0;

	*p++ = '\0'; /* overwrite '[' */
	while (*p >= '0' && *p <= '9') {
		u32 n = (index * 10) + (*p++ - '0');
		if (n < index)
			return false;
		index = n;
	}
	if (index == 0)
		return false;
	if (*p != ']')
		return false;
	p++;
	if (*p != '/' && *p != '\0')
		return false;

	*pp = p;
	*index_ret = index;
	return true;
}

static int
do_xml_path_walk(struct xml_node *element, const tchar *path, bool create,
		 struct xml_node **result_ret)
{
	size_t n = tstrlen(path) + 1;
#ifdef _RUFUS
	wimlib_assert(n < MAX_PATH);
	tchar buf[MAX_PATH];
#else
	tchar buf[n];
#endif
	tchar *p;
	tchar c;

	*result_ret = NULL;

	if (!element)
		return 0;

	/* Copy the path to a temporary buffer.  */
	tmemcpy(buf, path, n);
	p = buf;

	if (*p == '/')
		goto bad_syntax;
	c = *p;

	while (c != '\0') {
		const tchar *name;
		struct xml_node *child;
		u32 index = 1;

		/* We have another path component.  */

		/* Parse the element name.  */
		name = p;
		while (*p != '/' && *p != '\0' && *p != '[')
			p++;
		if (p == name) /* empty name?  */
			goto bad_syntax;

		/* Handle a bracketed index, if one was specified.  */
		if (*p == '[' && !parse_index(&p, &index))
			goto bad_syntax;

		c = *p;
		*p = '\0';

		/* Look for a matching child.  */
		xml_node_for_each_child(element, child)
			if (xml_node_is_element(child, name) && !--index)
				goto next_step;

		/* No child matched the path.  If create=false, the lookup
		 * failed.  If create=true, create the needed element.  */
		if (!create)
			return 0;

		/* We can't create an element at index 'n' if indices 1...n-1
		 * didn't already exist.  */
		if (index != 1)
			return WIMLIB_ERR_INVALID_PARAM;

		child = xml_new_element(element, name);
		if (!child)
			return WIMLIB_ERR_NOMEM;
	next_step:
		/* Continue to the next path component, if there is one.  */
		element = child;
		p++;
	}

	*result_ret = element;
	return 0;

bad_syntax:
	ERROR("The XML path \"%"TS"\" has invalid syntax.", path);
	return WIMLIB_ERR_INVALID_PARAM;
}

/* Retrieve the XML element, if any, at the specified 'path'.  This supports a
 * simple filesystem-like syntax.  If the element was found, returns a pointer
 * to it; otherwise returns NULL.  */
static struct xml_node *
xml_get_element_by_path(struct xml_node *root, const tchar *path)
{
	struct xml_node *element;

	do_xml_path_walk(root, path, false, &element);
	return element;
}

/*
 * Similar to xml_get_element_by_path(), but creates the element and any
 * requisite ancestor elements as needed.   If successful, 0 is returned and
 * *element_ret is set to a pointer to the resulting element.  If unsuccessful,
 * an error code is returned and *element_ret is set to NULL.
 */
static int
xml_ensure_element_by_path(struct xml_node *root, const tchar *path,
			   struct xml_node **element_ret)
{
	return do_xml_path_walk(root, path, true, element_ret);
}

static u64
xml_get_number_by_path(struct xml_node *root, const tchar *path)
{
	return xml_element_get_number(xml_get_element_by_path(root, path), 10);
}

static u64
xml_get_timestamp_by_path(struct xml_node *root, const tchar *path)
{
	return xml_element_get_timestamp(xml_get_element_by_path(root, path));
}

static const tchar *
xml_get_text_by_path(struct xml_node *root, const tchar *path)
{
	return xml_element_get_text(xml_get_element_by_path(root, path));
}

/*
 * Create/replace (if text is not NULL and not empty) or remove (if text is NULL
 * or empty) an element containing text.
 */
static int
xml_set_text_by_path(struct xml_node *root, const tchar *path,
		     const tchar *text)
{
	int ret;
	struct xml_node *element;

	if (text && *text) {
		/* Create or replace  */
		ret = xml_ensure_element_by_path(root, path, &element);
		if (ret)
			return ret;
		return xml_element_set_text(element, text);
	} else {
		/* Remove  */
		xml_free_node(xml_get_element_by_path(root, path));
		return 0;
	}
}

/* Unlink and return the node which represents the INDEX attribute of the
 * specified IMAGE element.  */
static struct xml_node *
unlink_index_attribute(struct xml_node *image_node)
{
	struct xml_node *attr = xml_get_attrib(image_node, T("INDEX"));

	xml_unlink_node(attr);
	return attr;
}

/* Compute the total uncompressed size of the streams of the specified inode. */
static u64
inode_sum_stream_sizes(const struct wim_inode *inode,
		       const struct blob_table *blob_table)
{
	u64 total_size = 0;

	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		const struct blob_descriptor *blob;

		blob = stream_blob(&inode->i_streams[i], blob_table);
		if (blob)
			total_size += blob->size;
	}
	return total_size;
}

static int
append_image_node(struct wim_xml_info *info, struct xml_node *image_node)
{
	tchar buf[32];
	struct xml_node **images;
	int ret;

	/* Limit exceeded?  */
	if (unlikely(info->image_count >= MAX_IMAGES))
		return WIMLIB_ERR_IMAGE_COUNT;

	/* Set the INDEX attribute. */
	tsprintf(buf, T("%d"), info->image_count + 1);
	ret = xml_set_attrib(image_node, T("INDEX"), buf);
	if (ret)
		return ret;

	/* Append the IMAGE element to the 'images' array.  */
	images = REALLOC(info->images,
			 (info->image_count + 1) * sizeof(info->images[0]));
	if (unlikely(!images))
		return WIMLIB_ERR_NOMEM;
	info->images = images;
	images[info->image_count++] = image_node;

	/* Add the IMAGE element to the document.  */
	xml_add_child(info->root, image_node);
	return 0;
}

/*----------------------------------------------------------------------------*
 *                     Functions for internal library use                     *
 *----------------------------------------------------------------------------*/

/* Allocate an empty 'struct wim_xml_info', containing no images.  */
struct wim_xml_info *
xml_new_info_struct(void)
{
	struct wim_xml_info *info = CALLOC(1, sizeof(*info));

	if (!info)
		return NULL;

	info->root = xml_new_element(NULL, T("WIM"));
	if (!info->root) {
		FREE(info);
		return NULL;
	}
	return info;
}

/* Free a 'struct wim_xml_info'.  */
void
xml_free_info_struct(struct wim_xml_info *info)
{
	if (info) {
		xml_free_node(info->root);
		FREE(info->images);
		FREE(info);
	}
}

/* Retrieve the number of images for which there exist IMAGE elements in the XML
 * document.  */
int
xml_get_image_count(const struct wim_xml_info *info)
{
	return info->image_count;
}

/* Retrieve the TOTALBYTES value for the WIM file, or 0 if this value is
 * unavailable.  */
u64
xml_get_total_bytes(const struct wim_xml_info *info)
{
	return xml_get_number_by_path(info->root, T("TOTALBYTES"));
}

/* Retrieve the TOTALBYTES value for the specified image, or 0 if this value is
 * unavailable.  */
u64
xml_get_image_total_bytes(const struct wim_xml_info *info, int image)
{
	return xml_get_number_by_path(info->images[image - 1], T("TOTALBYTES"));
}

/* Retrieve the HARDLINKBYTES value for the specified image, or 0 if this value
 * is unavailable.  */
u64
xml_get_image_hard_link_bytes(const struct wim_xml_info *info, int image)
{
	return xml_get_number_by_path(info->images[image - 1],
				      T("HARDLINKBYTES"));
}

/* Retrieve the WIMBOOT value for the specified image, or false if this value is
 * unavailable.  */
bool
xml_get_wimboot(const struct wim_xml_info *info, int image)
{
	return xml_get_number_by_path(info->images[image - 1], T("WIMBOOT"));
}

/* Retrieve the Windows build number for the specified image, or 0 if this
 * information is not available.  */
u64
xml_get_windows_build_number(const struct wim_xml_info *info, int image)
{
	return xml_get_number_by_path(info->images[image - 1],
				      T("WINDOWS/VERSION/BUILD"));
}

/* Set the WIMBOOT value for the specified image.  */
int
xml_set_wimboot(struct wim_xml_info *info, int image)
{
	return xml_set_text_by_path(info->images[image - 1],
				    T("WIMBOOT"), T("1"));
}

/*
 * Update the DIRCOUNT, FILECOUNT, TOTALBYTES, HARDLINKBYTES, and
 * LASTMODIFICATIONTIME elements for the specified WIM image.
 *
 * Note: since these stats are likely to be used for display purposes only, we
 * no longer attempt to duplicate WIMGAPI's weird bugs when calculating them.
 */
int
xml_update_image_info(WIMStruct *wim, int image)
{
	const struct wim_image_metadata *imd = wim->image_metadata[image - 1];
	struct xml_node *image_node = wim->xml_info->images[image - 1];
	const struct wim_inode *inode;
	u64 dir_count = 0;
	u64 file_count = 0;
	u64 total_bytes = 0;
	u64 hard_link_bytes = 0;
	u64 size;
	struct xml_node *dircount_node;
	struct xml_node *filecount_node;
	struct xml_node *totalbytes_node;
	struct xml_node *hardlinkbytes_node;
	struct xml_node *lastmodificationtime_node;

	image_for_each_inode(inode, imd) {
		if (inode_is_directory(inode))
			dir_count += inode->i_nlink;
		else
			file_count += inode->i_nlink;
		size = inode_sum_stream_sizes(inode, wim->blob_table);
		total_bytes += size * inode->i_nlink;
		hard_link_bytes += size * (inode->i_nlink - 1);
	}

	dircount_node = xml_new_element_with_u64(NULL, T("DIRCOUNT"),
						 dir_count);
	filecount_node = xml_new_element_with_u64(NULL, T("FILECOUNT"),
						  file_count);
	totalbytes_node = xml_new_element_with_u64(NULL, T("TOTALBYTES"),
						   total_bytes);
	hardlinkbytes_node = xml_new_element_with_u64(NULL, T("HARDLINKBYTES"),
						      hard_link_bytes);
	lastmodificationtime_node = xml_new_element_with_timestamp(NULL,
			T("LASTMODIFICATIONTIME"), now_as_wim_timestamp());

	if (unlikely(!dircount_node || !filecount_node || !totalbytes_node ||
		     !hardlinkbytes_node || !lastmodificationtime_node)) {
		xml_free_node(dircount_node);
		xml_free_node(filecount_node);
		xml_free_node(totalbytes_node);
		xml_free_node(hardlinkbytes_node);
		xml_free_node(lastmodificationtime_node);
		return WIMLIB_ERR_NOMEM;
	}

	xml_replace_child(image_node, dircount_node);
	xml_replace_child(image_node, filecount_node);
	xml_replace_child(image_node, totalbytes_node);
	xml_replace_child(image_node, hardlinkbytes_node);
	xml_replace_child(image_node, lastmodificationtime_node);
	return 0;
}

/* Add an image to the XML information. */
int
xml_add_image(struct wim_xml_info *info, const tchar *name)
{
	const u64 now = now_as_wim_timestamp();
	struct xml_node *image_node;
	int ret;

	if (name && !xml_legal_value(name)) {
		ERROR("Name of new image contains illegal characters");
		return WIMLIB_ERR_INVALID_PARAM;
	}

	ret = WIMLIB_ERR_NOMEM;
	image_node = xml_new_element(NULL, T("IMAGE"));
	if (!image_node)
		goto err;
	if (name && *name &&
	    !xml_new_element_with_text(image_node, T("NAME"), name))
		goto err;
	if (!xml_new_element_with_u64(image_node, T("DIRCOUNT"), 0))
		goto err;
	if (!xml_new_element_with_u64(image_node, T("FILECOUNT"), 0))
		goto err;
	if (!xml_new_element_with_u64(image_node, T("TOTALBYTES"), 0))
		goto err;
	if (!xml_new_element_with_u64(image_node, T("HARDLINKBYTES"), 0))
		goto err;
	if (!xml_new_element_with_timestamp(image_node, T("CREATIONTIME"), now))
		goto err;
	if (!xml_new_element_with_timestamp(image_node,
					    T("LASTMODIFICATIONTIME"), now))
		goto err;
	ret = append_image_node(info, image_node);
	if (ret)
		goto err;
	return 0;

err:
	xml_free_node(image_node);
	return ret;
}

/*
 * Make a copy of the XML information for the image with index @src_image in the
 * @src_info XML document and append it to the @dest_info XML document.
 *
 * In the process, change the image's name and description to the values
 * specified by @dest_image_name and @dest_image_description.  Either or both
 * may be NULL, which indicates that the corresponding element will not be
 * included in the destination image.
 */
int
xml_export_image(const struct wim_xml_info *src_info, int src_image,
		 struct wim_xml_info *dest_info, const tchar *dest_image_name,
		 const tchar *dest_image_description, bool wimboot)
{
	struct xml_node *dest_node;
	int ret;

	if (dest_image_name && !xml_legal_value(dest_image_name)) {
		ERROR("Destination image name contains illegal characters");
		return WIMLIB_ERR_INVALID_PARAM;
	}
	if (dest_image_description &&
	    !xml_legal_value(dest_image_description)) {
		ERROR("Destination image description contains illegal characters");
		return WIMLIB_ERR_INVALID_PARAM;
	}

	ret = WIMLIB_ERR_NOMEM;
	dest_node = xml_clone_tree(src_info->images[src_image - 1]);
	if (!dest_node)
		goto err;

	ret = xml_set_text_by_path(dest_node, T("NAME"), dest_image_name);
	if (ret)
		goto err;

	ret = xml_set_text_by_path(dest_node, T("DESCRIPTION"),
				   dest_image_description);
	if (ret)
		goto err;

	if (wimboot) {
		ret = xml_set_text_by_path(dest_node, T("WIMBOOT"), T("1"));
		if (ret)
			goto err;
	}

	ret = append_image_node(dest_info, dest_node);
	if (ret)
		goto err;
	return 0;

err:
	xml_free_node(dest_node);
	return ret;
}

/* Remove the specified image from the XML document.  */
void
xml_delete_image(struct wim_xml_info *info, int image)
{
	struct xml_node *next_image;
	struct xml_node *index_attr, *next_index_attr;

	/* Free the IMAGE element for the deleted image.  Then, shift all
	 * higher-indexed IMAGE elements down by 1, in the process re-assigning
	 * their INDEX attributes.  */

	next_image = info->images[image - 1];
	next_index_attr = unlink_index_attribute(next_image);
	xml_free_node(next_image);

	while (image < info->image_count) {
		index_attr = next_index_attr;
		next_image = info->images[image];
		next_index_attr = unlink_index_attribute(next_image);
		xml_add_child(next_image, index_attr);
		info->images[image - 1] = next_image;
		image++;
	}

	xml_free_node(next_index_attr);
	info->image_count--;
}

/* Architecture constants are from w64 mingw winnt.h  */
#define PROCESSOR_ARCHITECTURE_INTEL		0
#define PROCESSOR_ARCHITECTURE_MIPS		1
#define PROCESSOR_ARCHITECTURE_ALPHA		2
#define PROCESSOR_ARCHITECTURE_PPC		3
#define PROCESSOR_ARCHITECTURE_SHX		4
#define PROCESSOR_ARCHITECTURE_ARM		5
#define PROCESSOR_ARCHITECTURE_IA64		6
#define PROCESSOR_ARCHITECTURE_ALPHA64		7
#define PROCESSOR_ARCHITECTURE_MSIL		8
#define PROCESSOR_ARCHITECTURE_AMD64		9
#define PROCESSOR_ARCHITECTURE_IA32_ON_WIN64	10
#define PROCESSOR_ARCHITECTURE_ARM64		12

static const tchar *
describe_arch(u64 arch)
{
	static const tchar * const descriptions[] = {
		[PROCESSOR_ARCHITECTURE_INTEL] = T("x86"),
		[PROCESSOR_ARCHITECTURE_MIPS]  = T("MIPS"),
		[PROCESSOR_ARCHITECTURE_ARM]   = T("ARM"),
		[PROCESSOR_ARCHITECTURE_IA64]  = T("ia64"),
		[PROCESSOR_ARCHITECTURE_AMD64] = T("x86_64"),
		[PROCESSOR_ARCHITECTURE_ARM64] = T("ARM64"),
	};

	if (arch < ARRAY_LEN(descriptions) && descriptions[arch] != NULL)
		return descriptions[arch];

	return T("unknown");
}

/* Print information from the WINDOWS element, if present.  */
static void
print_windows_info(struct xml_node *image_node)
{
	struct xml_node *windows_node;
	struct xml_node *langs_node;
	struct xml_node *version_node;
	const tchar *text;

	windows_node = xml_get_element_by_path(image_node, T("WINDOWS"));
	if (!windows_node)
		return;

	tprintf(T("Architecture:           %"TS"\n"),
		describe_arch(xml_get_number_by_path(windows_node, T("ARCH"))));

	text = xml_get_text_by_path(windows_node, T("PRODUCTNAME"));
	if (text)
		tprintf(T("Product Name:           %"TS"\n"), text);

	text = xml_get_text_by_path(windows_node, T("EDITIONID"));
	if (text)
		tprintf(T("Edition ID:             %"TS"\n"), text);

	text = xml_get_text_by_path(windows_node, T("INSTALLATIONTYPE"));
	if (text)
		tprintf(T("Installation Type:      %"TS"\n"), text);

	text = xml_get_text_by_path(windows_node, T("HAL"));
	if (text)
		tprintf(T("HAL:                    %"TS"\n"), text);

	text = xml_get_text_by_path(windows_node, T("PRODUCTTYPE"));
	if (text)
		tprintf(T("Product Type:           %"TS"\n"), text);

	text = xml_get_text_by_path(windows_node, T("PRODUCTSUITE"));
	if (text)
		tprintf(T("Product Suite:          %"TS"\n"), text);

	langs_node = xml_get_element_by_path(windows_node, T("LANGUAGES"));
	if (langs_node) {
		struct xml_node *lang_node;

		tprintf(T("Languages:              "));
		xml_node_for_each_child(langs_node, lang_node) {
			if (!xml_node_is_element(lang_node, T("LANGUAGE")))
				continue;
			text = xml_element_get_text(lang_node);
			if (!text)
				continue;
			tprintf(T("%"TS" "), text);
		}
		tputchar(T('\n'));

		text = xml_get_text_by_path(langs_node, T("DEFAULT"));
		if (text)
			tprintf(T("Default Language:       %"TS"\n"), text);
	}

	text = xml_get_text_by_path(windows_node, T("SYSTEMROOT"));
	if (text)
		tprintf(T("System Root:            %"TS"\n"), text);

	version_node = xml_get_element_by_path(windows_node, T("VERSION"));
	if (version_node) {
		tprintf(T("Major Version:          %"PRIu64"\n"),
			xml_get_number_by_path(version_node, T("MAJOR")));
		tprintf(T("Minor Version:          %"PRIu64"\n"),
			xml_get_number_by_path(version_node, T("MINOR")));
		tprintf(T("Build:                  %"PRIu64"\n"),
			xml_get_number_by_path(version_node, T("BUILD")));
		tprintf(T("Service Pack Build:     %"PRIu64"\n"),
			xml_get_number_by_path(version_node, T("SPBUILD")));
		tprintf(T("Service Pack Level:     %"PRIu64"\n"),
			xml_get_number_by_path(version_node, T("SPLEVEL")));
	}
}

/* Prints information about the specified image.  */
void
xml_print_image_info(struct wim_xml_info *info, int image)
{
	struct xml_node * const image_node = info->images[image - 1];
	const tchar *text;
	tchar timebuf[64];

	tprintf(T("Index:                  %d\n"), image);

	/* Always print the Name and Description, even if the corresponding XML
	 * elements are not present.  */
	text = xml_get_text_by_path(image_node, T("NAME"));
	tprintf(T("Name:                   %"TS"\n"), text ? text : T(""));
	text = xml_get_text_by_path(image_node, T("DESCRIPTION"));
	tprintf(T("Description:            %"TS"\n"), text ? text : T(""));

	text = xml_get_text_by_path(image_node, T("DISPLAYNAME"));
	if (text)
		tprintf(T("Display Name:           %"TS"\n"), text);

	text = xml_get_text_by_path(image_node, T("DISPLAYDESCRIPTION"));
	if (text)
		tprintf(T("Display Description:    %"TS"\n"), text);

	tprintf(T("Directory Count:        %"PRIu64"\n"),
		xml_get_number_by_path(image_node, T("DIRCOUNT")));

	tprintf(T("File Count:             %"PRIu64"\n"),
		xml_get_number_by_path(image_node, T("FILECOUNT")));

	tprintf(T("Total Bytes:            %"PRIu64"\n"),
		xml_get_number_by_path(image_node, T("TOTALBYTES")));

	tprintf(T("Hard Link Bytes:        %"PRIu64"\n"),
		xml_get_number_by_path(image_node, T("HARDLINKBYTES")));

	wim_timestamp_to_str(xml_get_timestamp_by_path(image_node,
						       T("CREATIONTIME")),
			     timebuf, ARRAY_LEN(timebuf));
	tprintf(T("Creation Time:          %"TS"\n"), timebuf);

	wim_timestamp_to_str(xml_get_timestamp_by_path(image_node,
					T("LASTMODIFICATIONTIME")),
					timebuf, ARRAY_LEN(timebuf));
	tprintf(T("Last Modification Time: %"TS"\n"), timebuf);

	print_windows_info(image_node);

	text = xml_get_text_by_path(image_node, T("FLAGS"));
	if (text)
		tprintf(T("Flags:                  %"TS"\n"), text);

	tprintf(T("WIMBoot compatible:     %"TS"\n"),
		xml_get_number_by_path(image_node, T("WIMBOOT")) ?
			T("yes") : T("no"));

	tputchar('\n');
}

/*----------------------------------------------------------------------------*
 *                      Reading and writing the XML data                      *
 *----------------------------------------------------------------------------*/

static int
image_element_get_index(struct xml_node *element)
{
	struct xml_node *attrib = xml_get_attrib(element, T("INDEX"));

	if (!attrib)
		return 0;
	return min(INT_MAX, parse_number(attrib->value, 10));
}

/* Prepare the 'images' array from the XML document tree.  */
static int
setup_images(struct wim_xml_info *info, struct xml_node *root)
{
	struct xml_node *child;
	int index;
	int max_index = 0;
	int ret;

	xml_node_for_each_child(root, child) {
		if (!xml_node_is_element(child, T("IMAGE")))
			continue;
		index = image_element_get_index(child);
		if (unlikely(index < 1 || info->image_count >= MAX_IMAGES))
			goto err_indices;
		max_index = max(max_index, index);
		info->image_count++;
	}
	if (unlikely(max_index != info->image_count))
		goto err_indices;
	ret = WIMLIB_ERR_NOMEM;
	info->images = CALLOC(info->image_count, sizeof(info->images[0]));
	if (unlikely(!info->images))
		goto err;
	xml_node_for_each_child(root, child) {
		if (!xml_node_is_element(child, T("IMAGE")))
			continue;
		index = image_element_get_index(child);
		if (unlikely(info->images[index - 1]))
			goto err_indices;
		info->images[index - 1] = child;
	}
	return 0;

err_indices:
	ERROR("The WIM file's XML document does not contain exactly one IMAGE "
	      "element per image!");
	ret = WIMLIB_ERR_XML;
err:
	FREE(info->images);
	return ret;
}

static int
parse_wim_xml_document(const utf16lechar *raw_doc, size_t raw_doc_size,
		       struct xml_node **root_ret)
{
	tchar *doc;
	int ret;

	ret = utf16le_to_tstr(raw_doc, raw_doc_size, &doc, NULL);
	if (ret)
		return ret;
	ret = xml_parse_document(doc, root_ret);
	FREE(doc);
	return ret;
}

/* Reads the XML data from a WIM file.  */
int
read_wim_xml_data(WIMStruct *wim)
{
	struct wim_xml_info *info;
	void *raw_doc;
	size_t raw_doc_size;
	struct xml_node *root;
	int ret;

	/* Allocate the 'struct wim_xml_info'.  */
	ret = WIMLIB_ERR_NOMEM;
	info = CALLOC(1, sizeof(*info));
	if (!info)
		goto err;

	/* Read the raw UTF-16LE XML document.  */
	ret = wimlib_get_xml_data(wim, &raw_doc, &raw_doc_size);
	if (ret)
		goto err;

	/* Parse the document, creating the document tree.  */
	ret = parse_wim_xml_document(raw_doc, raw_doc_size, &info->root);
	FREE(raw_doc);
	raw_doc = NULL;
	if (ret) {
		if (ret != WIMLIB_ERR_NOMEM)
			ret = WIMLIB_ERR_XML;
		ERROR("Unable to parse the WIM file's XML document!");
		goto err;
	}
	root = info->root;

	/* Verify the root element.  */
	if (!xml_node_is_element(root, T("WIM"))) {
		ERROR("The WIM file's XML document has an unexpected format!");
		ret = WIMLIB_ERR_XML;
		goto err;
	}

	/* Verify the WIM file is not encrypted.  */
	if (xml_get_element_by_path(root, T("ESD/ENCRYPTED"))) {
		ret = WIMLIB_ERR_WIM_IS_ENCRYPTED;
		goto err;
	}

	/* Validate the image elements and set up the images[] array.  */
	ret = setup_images(info, root);
	if (ret)
		goto err;

	/* Success!  */
	wim->xml_info = info;
	return 0;

err:
	xml_free_info_struct(info);
	return ret;
}

/* Swap the INDEX attributes of two IMAGE elements.  */
static void
swap_index_attributes(struct xml_node *image_element_1,
		      struct xml_node *image_element_2)
{
	struct xml_node *attr_1, *attr_2;

	if (image_element_1 != image_element_2) {
		attr_1 = unlink_index_attribute(image_element_1);
		attr_2 = unlink_index_attribute(image_element_2);
		xml_add_child(image_element_1, attr_2);
		xml_add_child(image_element_2, attr_1);
	}
}

static int
prepare_document_for_write(struct wim_xml_info *info, int image, u64 total_bytes,
			   struct xml_node **orig_totalbytes_element_ret)
{
	struct xml_node *totalbytes_element = NULL;

	/* Allocate the new TOTALBYTES element if needed.  */
	if (total_bytes != WIM_TOTALBYTES_USE_EXISTING &&
	    total_bytes != WIM_TOTALBYTES_OMIT) {
		totalbytes_element = xml_new_element_with_u64(
					NULL, T("TOTALBYTES"), total_bytes);
		if (!totalbytes_element)
			return WIMLIB_ERR_NOMEM;
	}

	/* Adjust the IMAGE elements if needed.  */
	if (image != WIMLIB_ALL_IMAGES) {
		/* We're writing a single image only.  Temporarily unlink all
		 * other IMAGE elements from the document.  */
		for (int i = 0; i < info->image_count; i++)
			if (i + 1 != image)
				xml_unlink_node(info->images[i]);

		/* Temporarily set the INDEX attribute of the needed IMAGE
		 * element to 1.  */
		swap_index_attributes(info->images[0], info->images[image - 1]);
	}

	/* Adjust (add, change, or remove) the TOTALBYTES element if needed.  */
	*orig_totalbytes_element_ret = NULL;
	if (total_bytes != WIM_TOTALBYTES_USE_EXISTING) {
		/* Unlink the previous TOTALBYTES element, if any.  */
		*orig_totalbytes_element_ret = xml_get_element_by_path(
						info->root, T("TOTALBYTES"));
		if (*orig_totalbytes_element_ret)
			xml_unlink_node(*orig_totalbytes_element_ret);

		/* Link in the new TOTALBYTES element, if any.  */
		if (totalbytes_element)
			xml_add_child(info->root, totalbytes_element);
	}
	return 0;
}

static void
restore_document_after_write(struct wim_xml_info *info, int image,
			     struct xml_node *orig_totalbytes_element)
{
	/* Restore the IMAGE elements if needed.  */
	if (image != WIMLIB_ALL_IMAGES) {
		/* We wrote a single image only.  Re-link all other IMAGE
		 * elements to the document.  */
		for (int i = 0; i < info->image_count; i++)
			if (i + 1 != image)
				xml_add_child(info->root, info->images[i]);

		/* Restore the original INDEX attributes.  */
		swap_index_attributes(info->images[0], info->images[image - 1]);
	}

	/* Restore the original TOTALBYTES element if needed.  */
	if (orig_totalbytes_element)
		xml_replace_child(info->root, orig_totalbytes_element);
}

/*
 * Writes the XML data to a WIM file.
 *
 * 'image' specifies the image(s) to include in the XML data.  Normally it is
 * WIMLIB_ALL_IMAGES, but it can also be a 1-based image index.
 *
 * 'total_bytes' is the number to use in the top-level TOTALBYTES element, or
 * WIM_TOTALBYTES_USE_EXISTING to use the existing value from the XML document
 * (if any), or WIM_TOTALBYTES_OMIT to omit the TOTALBYTES element entirely.
 */
int
write_wim_xml_data(WIMStruct *wim, int image, u64 total_bytes,
		   struct wim_reshdr *out_reshdr, int write_resource_flags)
{
	struct wim_xml_info *info = wim->xml_info;
	int ret;
	struct xml_node *orig_totalbytes_element;
	struct xml_out_buf buf = { 0 };
	const utf16lechar *raw_doc;
	size_t raw_doc_size;

	/* Make any needed temporary changes to the document.  */
	ret = prepare_document_for_write(info, image, total_bytes,
					 &orig_totalbytes_element);
	if (ret)
		goto out;

	ret = xml_write_document(info->root, &buf);
	if (ret)
		goto out_restore_document;

	ret = tstr_get_utf16le_and_len(buf.buf, &raw_doc, &raw_doc_size);
	if (ret)
		goto out_restore_document;

	/* Write the XML data uncompressed.  Although wimlib can handle
	 * compressed XML data, some other WIM software cannot.  */
	ret = write_wim_resource_from_buffer(raw_doc, raw_doc_size,
					     true,
					     &wim->out_fd,
					     WIMLIB_COMPRESSION_TYPE_NONE,
					     0,
					     out_reshdr,
					     NULL,
					     write_resource_flags);
	tstr_put_utf16le(raw_doc);
out_restore_document:
	/* Revert any temporary changes we made to the document.  */
	restore_document_after_write(info, image, orig_totalbytes_element);
	FREE(buf.buf);
out:
	return ret;
}

/*----------------------------------------------------------------------------*
 *                           Library API functions                            *
 *----------------------------------------------------------------------------*/

WIMLIBAPI int
wimlib_get_xml_data(WIMStruct *wim, void **buf_ret, size_t *bufsize_ret)
{
	const struct wim_reshdr *xml_reshdr;

	if (wim->filename == NULL && filedes_is_seekable(&wim->in_fd))
		return WIMLIB_ERR_NO_FILENAME;

	if (buf_ret == NULL || bufsize_ret == NULL)
		return WIMLIB_ERR_INVALID_PARAM;

	xml_reshdr = &wim->hdr.xml_data_reshdr;

	*bufsize_ret = xml_reshdr->uncompressed_size;
	return wim_reshdr_to_data(xml_reshdr, wim, buf_ret);
}

WIMLIBAPI int
wimlib_extract_xml_data(WIMStruct *wim, FILE *fp)
{
	int ret;
	void *buf;
	size_t bufsize;

	ret = wimlib_get_xml_data(wim, &buf, &bufsize);
	if (ret)
		return ret;

	if (fwrite(buf, 1, bufsize, fp) != bufsize) {
		ERROR_WITH_ERRNO("Failed to extract XML data");
		ret = WIMLIB_ERR_WRITE;
	}
	FREE(buf);
	return ret;
}

static bool
image_name_in_use(const WIMStruct *wim, const tchar *name, int excluded_image)
{
	const struct wim_xml_info *info = wim->xml_info;
	const tchar *existing_name;

	/* Any number of images can have "no name".  */
	if (!name || !*name)
		return false;

	/* Check for images that have the specified name.  */
	for (int i = 0; i < info->image_count; i++) {
		if (i + 1 == excluded_image)
			continue;
		existing_name = xml_get_text_by_path(info->images[i],
						     T("NAME"));
		if (existing_name && !tstrcmp(existing_name, name))
			return true;
	}
	return false;
}

WIMLIBAPI bool
wimlib_image_name_in_use(const WIMStruct *wim, const tchar *name)
{
	return image_name_in_use(wim, name, WIMLIB_NO_IMAGE);
}

WIMLIBAPI const tchar *
wimlib_get_image_name(const WIMStruct *wim, int image)
{
	const struct wim_xml_info *info = wim->xml_info;
	const tchar *name;

	if (image < 1 || image > info->image_count)
		return NULL;
	name = wimlib_get_image_property(wim, image, T("NAME"));
	return name ? name : T("");
}

WIMLIBAPI const tchar *
wimlib_get_image_description(const WIMStruct *wim, int image)
{
	return wimlib_get_image_property(wim, image, T("DESCRIPTION"));
}

WIMLIBAPI const tchar *
wimlib_get_image_property(const WIMStruct *wim, int image,
			  const tchar *property_name)
{
	const struct wim_xml_info *info = wim->xml_info;

	if (!property_name || !*property_name)
		return NULL;
	if (image < 1 || image > info->image_count)
		return NULL;
	return xml_get_text_by_path(info->images[image - 1], property_name);
}

WIMLIBAPI int
wimlib_set_image_name(WIMStruct *wim, int image, const tchar *name)
{
	return wimlib_set_image_property(wim, image, T("NAME"), name);
}

WIMLIBAPI int
wimlib_set_image_descripton(WIMStruct *wim, int image, const tchar *description)
{
	return wimlib_set_image_property(wim, image, T("DESCRIPTION"),
					 description);
}

WIMLIBAPI int
wimlib_set_image_flags(WIMStruct *wim, int image, const tchar *flags)
{
	return wimlib_set_image_property(wim, image, T("FLAGS"), flags);
}

WIMLIBAPI int
wimlib_set_image_property(WIMStruct *wim, int image, const tchar *property_name,
			  const tchar *property_value)
{
	struct wim_xml_info *info = wim->xml_info;

	if (!property_name || !*property_name)
		return WIMLIB_ERR_INVALID_PARAM;

	if (!xml_legal_path(property_name)) {
		ERROR("Property name '%"TS"' is illegal in XML", property_name);
		return WIMLIB_ERR_INVALID_PARAM;
	}

	if (property_value && !xml_legal_value(property_value)) {
		WARNING("Value of property '%"TS"' contains illegal characters",
			property_name);
		return WIMLIB_ERR_INVALID_PARAM;
	}

	if (image < 1 || image > info->image_count)
		return WIMLIB_ERR_INVALID_IMAGE;

	if (!tstrcmp(property_name, T("NAME")) &&
	    image_name_in_use(wim, property_value, image))
		return WIMLIB_ERR_IMAGE_NAME_COLLISION;

	return xml_set_text_by_path(info->images[image - 1], property_name,
				    property_value);
}
