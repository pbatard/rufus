/*
 * win32_apply.c - Windows-specific code for applying files from a WIM image.
 */

/*
 * Copyright 2013-2023 Eric Biggers
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

#ifdef _WIN32

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/win32_common.h"

#include "wimlib/apply.h"
#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/error.h"
#include "wimlib/metadata.h"
#include "wimlib/object_id.h"
#include "wimlib/paths.h"
#include "wimlib/pattern.h"
#include "wimlib/reparse.h"
#include "wimlib/scan.h" /* for mangle_pat() and match_pattern_list()  */
#include "wimlib/textfile.h"
#include "wimlib/wimboot.h"
#include "wimlib/wof.h"
#include "wimlib/xattr.h"
#include "wimlib/xml.h"

struct win32_apply_ctx {

	/* Extract flags, the pointer to the WIMStruct, etc.  */
	struct apply_ctx common;

	/* WIMBoot information, only filled in if WIMLIB_EXTRACT_FLAG_WIMBOOT
	 * was provided  */
	struct {
		/* This array contains the WIM files registered with WOF on the
		 * target volume for this extraction operation.  All WIMStructs
		 * in this array are distinct and have ->filename != NULL.  */
		struct wimboot_wim {
			WIMStruct *wim;
			u64 data_source_id;
			u8 blob_table_hash[SHA1_HASH_SIZE];
		} *wims;
		size_t num_wims;
		bool wof_running;
		bool have_wrong_version_wims;
		bool have_uncompressed_wims;
		bool have_unsupported_compressed_resources;
		bool have_huge_resources;
	} wimboot;

	/* External backing information  */
	struct string_list *prepopulate_pats;
	void *mem_prepopulate_pats;
	bool tried_to_load_prepopulate_list;

	/* Open handle to the target directory  */
	HANDLE h_target;

	/* NT namespace path to the target directory (buffer allocated)  */
	UNICODE_STRING target_ntpath;

	/* Temporary buffer for building paths (buffer allocated)  */
	UNICODE_STRING pathbuf;

	/* Object attributes to reuse for opening files in the target directory.
	 * (attr.ObjectName == &pathbuf) and (attr.RootDirectory == h_target).
	 */
	OBJECT_ATTRIBUTES attr;

	/* Temporary I/O status block for system calls  */
	IO_STATUS_BLOCK iosb;

	/* Allocated buffer for creating "printable" paths from our
	 * target-relative NT paths  */
	wchar_t *print_buffer;

	/* Allocated buffer for reading blob data when it cannot be extracted
	 * directly  */
	u8 *data_buffer;

	/* Pointer to the next byte in @data_buffer to fill  */
	u8 *data_buffer_ptr;

	/* Size allocated in @data_buffer  */
	size_t data_buffer_size;

	/* Current offset in the raw encrypted file being written  */
	size_t encrypted_offset;

	/* Current size of the raw encrypted file being written  */
	size_t encrypted_size;

	/* Temporary buffer for reparse data  */
	struct reparse_buffer_disk rpbuf;

	/* Temporary buffer for reparse data of "fixed" absolute symbolic links
	 * and junctions  */
	struct reparse_buffer_disk rpfixbuf;

	/* Array of open handles to filesystem streams currently being written
	 */
	HANDLE open_handles[MAX_OPEN_FILES];

	/* Number of handles in @open_handles currently open (filled in from the
	 * beginning of the array)  */
	unsigned num_open_handles;

	/* For each currently open stream, whether we're writing to it in
	 * "sparse" mode or not.  */
	bool is_sparse_stream[MAX_OPEN_FILES];

	/* Whether is_sparse_stream[] is true for any currently open stream  */
	bool any_sparse_streams;

	/* List of dentries, joined by @d_tmp_list, that need to have reparse
	 * data extracted as soon as the whole blob has been read into
	 * @data_buffer.  */
	struct list_head reparse_dentries;

	/* List of dentries, joined by @d_tmp_list, that need to have raw
	 * encrypted data extracted as soon as the whole blob has been read into
	 * @data_buffer.  */
	struct list_head encrypted_dentries;

	/* Number of files for which we didn't have permission to set the full
	 * security descriptor.  */
	unsigned long partial_security_descriptors;

	/* Number of files for which we didn't have permission to set any part
	 * of the security descriptor.  */
	unsigned long no_security_descriptors;

	/* Number of files for which we couldn't set the short name.  */
	unsigned long num_set_short_name_failures;

	/* Number of files for which we couldn't remove the short name.  */
	unsigned long num_remove_short_name_failures;

	/* Number of files on which we couldn't set System Compression.  */
	unsigned long num_system_compression_failures;

	/* The number of files which, for compatibility with the Windows
	 * bootloader, were not compressed using the requested system
	 * compression format.  This includes matches with the hardcoded pattern
	 * list only; it does not include matches with patterns in
	 * [PrepopulateList].  */
	unsigned long num_system_compression_exclusions;

	/* Number of files for which we couldn't set the object ID.  */
	unsigned long num_object_id_failures;

	/* Number of files for which we couldn't set extended attributes.  */
	unsigned long num_xattr_failures;

	/* The Windows build number of the image being applied, or 0 if unknown.
	 */
	u64 windows_build_number;

	/* Have we tried to enable short name support on the target volume yet?
	 */
	bool tried_to_enable_short_names;
};

/* Get the drive letter from a Windows path, or return the null character if the
 * path is relative.  */
static wchar_t
get_drive_letter(const wchar_t *path)
{
	/* Skip \\?\ prefix  */
	if (!wcsncmp(path, L"\\\\?\\", 4))
		path += 4;

	/* Return drive letter if valid  */
	if (((path[0] >= L'a' && path[0] <= L'z') ||
	     (path[0] >= L'A' && path[0] <= L'Z')) && path[1] == L':')
		return path[0];

	return L'\0';
}

static void
get_vol_flags(const wchar_t *target, DWORD *vol_flags_ret,
	      bool *short_names_supported_ret)
{
	wchar_t filesystem_name[MAX_PATH + 1];
	wchar_t drive[4];
	wchar_t *volume = NULL;

	*vol_flags_ret = 0;
	*short_names_supported_ret = false;

	drive[0] = get_drive_letter(target);
	if (drive[0]) {
		drive[1] = L':';
		drive[2] = L'\\';
		drive[3] = L'\0';
		volume = drive;
	}

	if (!GetVolumeInformation(volume, NULL, 0, NULL, NULL,
				  vol_flags_ret, filesystem_name,
				  ARRAY_LEN(filesystem_name)))
	{
		win32_warning(GetLastError(),
			      L"Failed to get volume information for \"%ls\"",
			      target);
		return;
	}

	if (wcsstr(filesystem_name, L"NTFS")) {
		/*
		 * FILE_SUPPORTS_HARD_LINKS and
		 * FILE_SUPPORTS_EXTENDED_ATTRIBUTES are only supported on
		 * Windows 7 and later.  Force them on anyway if the filesystem
		 * is NTFS.
		 */
		*vol_flags_ret |= FILE_SUPPORTS_HARD_LINKS;
		*vol_flags_ret |= FILE_SUPPORTS_EXTENDED_ATTRIBUTES;

		/* There's no volume flag for short names, but according to the
		 * MS documentation they are only user-settable on NTFS.  */
		*short_names_supported_ret = true;
	}
}

static const wchar_t *
current_path(struct win32_apply_ctx *ctx);

static void
build_extraction_path(const struct wim_dentry *dentry,
		      struct win32_apply_ctx *ctx);

static int
report_dentry_apply_error(const struct wim_dentry *dentry,
			  struct win32_apply_ctx *ctx, int ret)
{
	build_extraction_path(dentry, ctx);
	return report_apply_error(&ctx->common, ret, current_path(ctx));
}

static inline int
check_apply_error(const struct wim_dentry *dentry,
		  struct win32_apply_ctx *ctx, int ret)
{
	if (unlikely(ret))
		ret = report_dentry_apply_error(dentry, ctx, ret);
	return ret;
}

static int
win32_get_supported_features(const wchar_t *target,
			     struct wim_features *supported_features)
{
	DWORD vol_flags;
	bool short_names_supported;

	/* Query the features of the target volume.  */

	get_vol_flags(target, &vol_flags, &short_names_supported);

	supported_features->readonly_files = 1;
	supported_features->hidden_files = 1;
	supported_features->system_files = 1;
	supported_features->archive_files = 1;

	if (vol_flags & FILE_FILE_COMPRESSION)
		supported_features->compressed_files = 1;

	if (vol_flags & FILE_SUPPORTS_ENCRYPTION) {
		supported_features->encrypted_files = 1;
		supported_features->encrypted_directories = 1;
	}

	supported_features->not_context_indexed_files = 1;

	if (vol_flags & FILE_SUPPORTS_SPARSE_FILES)
		supported_features->sparse_files = 1;

	if (vol_flags & FILE_NAMED_STREAMS)
		supported_features->named_data_streams = 1;

	if (vol_flags & FILE_SUPPORTS_HARD_LINKS)
		supported_features->hard_links = 1;

	if (vol_flags & FILE_SUPPORTS_REPARSE_POINTS)
		supported_features->reparse_points = 1;

	if (vol_flags & FILE_PERSISTENT_ACLS)
		supported_features->security_descriptors = 1;

	if (short_names_supported)
		supported_features->short_names = 1;

	if (vol_flags & FILE_SUPPORTS_OBJECT_IDS)
		supported_features->object_ids = 1;

	supported_features->timestamps = 1;

	if (vol_flags & FILE_CASE_SENSITIVE_SEARCH) {
		/*
		 * The filesystem supports case-sensitive filenames.  But does
		 * the operating system as well?  This normally requires the
		 * registry setting ObCaseInsensitive=0.  We can test it
		 * indirectly by attempting to open the "\SystemRoot" symbolic
		 * link using a name with the wrong case.  If we get
		 * STATUS_OBJECT_NAME_NOT_FOUND instead of STATUS_ACCESS_DENIED,
		 * then case-sensitive names must be enabled.
		 */
		UNICODE_STRING path;
		OBJECT_ATTRIBUTES attr;
		HANDLE h;
		NTSTATUS status;

		RtlInitUnicodeString(&path, L"\\systemroot");
		InitializeObjectAttributes(&attr, &path, 0, NULL, NULL);

		status = NtOpenSymbolicLinkObject(&h, 0, &attr);
		if (status == STATUS_OBJECT_NAME_NOT_FOUND)
			supported_features->case_sensitive_filenames = 1;
	}

	if (vol_flags & FILE_SUPPORTS_EXTENDED_ATTRIBUTES)
		supported_features->xattrs = 1;

	return 0;
}

#define COMPACT_FLAGS	(WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K |		\
			 WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS8K |		\
			 WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS16K |	\
			 WIMLIB_EXTRACT_FLAG_COMPACT_LZX)



/*
 * If not done already, load the patterns from the [PrepopulateList] section of
 * WimBootCompress.ini in the WIM image being extracted.
 *
 * Note: WimBootCompress.ini applies to both types of "external backing":
 *
 *	- WIM backing ("WIMBoot" - Windows 8.1 and later)
 *	- File backing ("System Compression" - Windows 10 and later)
 */
static int
load_prepopulate_pats(struct win32_apply_ctx *ctx)
{
	const wchar_t *path = L"\\Windows\\System32\\WimBootCompress.ini";
	struct wim_dentry *dentry;
	const struct blob_descriptor *blob;
	int ret;
	void *buf;
	struct string_list *strings;
	void *mem;
	struct text_file_section sec;

	if (ctx->tried_to_load_prepopulate_list)
		return 0;

	ctx->tried_to_load_prepopulate_list = true;

	dentry = get_dentry(ctx->common.wim, path, WIMLIB_CASE_INSENSITIVE);
	if (!dentry ||
	    (dentry->d_inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
					      FILE_ATTRIBUTE_REPARSE_POINT |
					      FILE_ATTRIBUTE_ENCRYPTED)) ||
	    !(blob = inode_get_blob_for_unnamed_data_stream(dentry->d_inode,
							    ctx->common.wim->blob_table)))
	{
		WARNING("%ls does not exist in the WIM image.\n"
			"          The default configuration will be used instead; it assumes that all\n"
			"          files are valid for external backing regardless of path, equivalent\n"
			"          to an empty [PrepopulateList] section.", path);
		return WIMLIB_ERR_PATH_DOES_NOT_EXIST;
	}

	ret = read_blob_into_alloc_buf(blob, &buf);
	if (ret)
		return ret;

	strings = CALLOC(1, sizeof(struct string_list));
	if (!strings) {
		FREE(buf);
		return WIMLIB_ERR_NOMEM;
	}

	sec.name = T("PrepopulateList");
	sec.strings = strings;

	ret = load_text_file(path, buf, blob->size, &mem, &sec, 1,
			     LOAD_TEXT_FILE_REMOVE_QUOTES |
			     LOAD_TEXT_FILE_NO_WARNINGS,
			     mangle_pat);
	STATIC_ASSERT(OS_PREFERRED_PATH_SEPARATOR == WIM_PATH_SEPARATOR);
	FREE(buf);
	if (ret) {
		FREE(strings);
		return ret;
	}
	ctx->prepopulate_pats = strings;
	ctx->mem_prepopulate_pats = mem;
	return 0;
}

/* Returns %true if the specified absolute path to a file in the WIM image can
 * be subject to external backing when extracted.  Otherwise returns %false.  */
static bool
can_externally_back_path(const wchar_t *path, const struct win32_apply_ctx *ctx)
{
	/* Does the path match a pattern given in the [PrepopulateList] section
	 * of WimBootCompress.ini?  */
	if (ctx->prepopulate_pats && match_pattern_list(path, ctx->prepopulate_pats,
							MATCH_RECURSIVELY))
		return false;

	/* Since we attempt to modify the SYSTEM registry after it's extracted
	 * (see end_wimboot_extraction()), it can't be extracted as externally
	 * backed.  This extends to associated files such as SYSTEM.LOG that
	 * also must be writable in order to write to the registry.  Normally,
	 * SYSTEM is in [PrepopulateList], and the SYSTEM.* files match patterns
	 * in [ExclusionList] and therefore are not captured in the WIM at all.
	 * However, a WIM that wasn't specifically captured in "WIMBoot mode"
	 * may contain SYSTEM.* files.  So to make things "just work", hard-code
	 * the pattern.  */
	if (match_path(path, L"\\Windows\\System32\\config\\SYSTEM*", 0))
		return false;

	return true;
}

/* Can the specified WIM resource be used as the source of an external backing
 * for the wof.sys WIM provider?  */
static bool
is_resource_valid_for_external_backing(const struct wim_resource_descriptor *rdesc,
				       struct win32_apply_ctx *ctx)
{
	/* Must be the original WIM file format.  This check excludes pipable
	 * resources and solid resources.  It also excludes other resources
	 * contained in such files even if they would be otherwise compatible.
	 */
	if (rdesc->wim->hdr.magic != WIM_MAGIC ||
	    rdesc->wim->hdr.wim_version != WIM_VERSION_DEFAULT)
	{
		ctx->wimboot.have_wrong_version_wims = true;
		return false;
	}

	/*
	 * Whitelist of compression types and chunk sizes supported by
	 * Microsoft's WOF driver.
	 *
	 * Notes:
	 *    - Uncompressed WIMs result in BSOD.  However, this only applies to
	 *      the WIM file itself, not to uncompressed resources in a WIM file
	 *      that is otherwise compressed.
	 *    - XPRESS 64K sometimes appears to work, but sometimes it causes
	 *	reads to fail with STATUS_UNSUCCESSFUL.
	 */
	switch (rdesc->compression_type) {
	case WIMLIB_COMPRESSION_TYPE_NONE:
		if (rdesc->wim->compression_type == WIMLIB_COMPRESSION_TYPE_NONE) {
			ctx->wimboot.have_uncompressed_wims = true;
			return false;
		}
		break;
	case WIMLIB_COMPRESSION_TYPE_XPRESS:
		switch (rdesc->chunk_size) {
		case 4096:
		case 8192:
		case 16384:
		case 32768:
			break;
		default:
			ctx->wimboot.have_unsupported_compressed_resources = true;
			return false;
		}
		break;
	case WIMLIB_COMPRESSION_TYPE_LZX:
		switch (rdesc->chunk_size) {
		case 32768:
			break;
		default:
			ctx->wimboot.have_unsupported_compressed_resources = true;
			return false;
		}
		break;
	default:
		ctx->wimboot.have_unsupported_compressed_resources = true;
		return false;
	}

	/* Microsoft's WoF driver errors out if it tries to satisfy a read with
	 * ending offset >= 4 GiB from an externally backed file.  */
	if (rdesc->uncompressed_size > 4200000000) {
		ctx->wimboot.have_huge_resources = true;
		return false;
	}

	return true;
}

#define EXTERNAL_BACKING_NOT_ENABLED		-1
#define EXTERNAL_BACKING_NOT_POSSIBLE		-2
#define EXTERNAL_BACKING_EXCLUDED		-3

/*
 * Determines whether the specified file will be externally backed.  Returns a
 * negative status code if no, 0 if yes, or a positive wimlib error code on
 * error.  If the file is excluded from external backing based on its path, then
 * *excluded_dentry_ret is set to the dentry for the path that matched the
 * exclusion rule.
 *
 * Note that this logic applies to both types of "external backing":
 *
 *	- WIM backing ("WIMBoot" - Windows 8.1 and later)
 *	- File backing ("System Compression" - Windows 10 and later)
 *
 * However, in the case of WIM backing we also need to validate that the WIM
 * resource that would be the source of the backing is supported by the wof.sys
 * WIM provider.
 */
static int
will_externally_back_inode(struct wim_inode *inode, struct win32_apply_ctx *ctx,
			   const struct wim_dentry **excluded_dentry_ret,
			   bool wimboot_mode)
{
	struct wim_dentry *dentry;
	struct blob_descriptor *blob;
	int ret;

	if (load_prepopulate_pats(ctx) == WIMLIB_ERR_NOMEM)
		return WIMLIB_ERR_NOMEM;

	if (inode->i_can_externally_back)
		return 0;

	/* This may do redundant checks because the cached value
	 * i_can_externally_back is 2-state (as opposed to 3-state:
	 * unknown/no/yes).  But most files can be externally backed, so this
	 * way is fine.  */

	if (inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
				   FILE_ATTRIBUTE_REPARSE_POINT |
				   FILE_ATTRIBUTE_ENCRYPTED))
		return EXTERNAL_BACKING_NOT_POSSIBLE;

	blob = inode_get_blob_for_unnamed_data_stream_resolved(inode);

	if (!blob)
		return EXTERNAL_BACKING_NOT_POSSIBLE;

	if (wimboot_mode &&
	    (blob->blob_location != BLOB_IN_WIM ||
	     !is_resource_valid_for_external_backing(blob->rdesc, ctx)))
		return EXTERNAL_BACKING_NOT_POSSIBLE;

	/*
	 * We need to check the patterns in [PrepopulateList] against every name
	 * of the inode, in case any of them match.
	 */

	inode_for_each_extraction_alias(dentry, inode) {

		ret = calculate_dentry_full_path(dentry);
		if (ret)
			return ret;

		if (!can_externally_back_path(dentry->d_full_path, ctx)) {
			if (excluded_dentry_ret)
				*excluded_dentry_ret = dentry;
			return EXTERNAL_BACKING_EXCLUDED;
		}
	}

	inode->i_can_externally_back = 1;
	return 0;
}

/*
 * Determines if the unnamed data stream of a file will be created as a WIM
 * external backing (a "WIMBoot pointer file"), as opposed to a standard
 * extraction.
 */
static int
win32_will_back_from_wim(struct wim_dentry *dentry, struct apply_ctx *_ctx)
{
	struct win32_apply_ctx *ctx = (struct win32_apply_ctx *)_ctx;

	if (!(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT))
		return EXTERNAL_BACKING_NOT_ENABLED;

	return will_externally_back_inode(dentry->d_inode, ctx, NULL, true);
}

/* Find the WOF registration information for the specified WIM file.  */
static struct wimboot_wim *
find_wimboot_wim(WIMStruct *wim_to_find, struct win32_apply_ctx *ctx)
{
	for (size_t i = 0; i < ctx->wimboot.num_wims; i++)
		if (wim_to_find == ctx->wimboot.wims[i].wim)
			return &ctx->wimboot.wims[i];

	wimlib_assert(0);
	return NULL;
}

static int
set_backed_from_wim(HANDLE h, struct wim_inode *inode, struct win32_apply_ctx *ctx)
{
	int ret;
	const struct wim_dentry *excluded_dentry;
	const struct blob_descriptor *blob;
	const struct wimboot_wim *wimboot_wim;

	ret = will_externally_back_inode(inode, ctx, &excluded_dentry, true);
	if (ret > 0) /* Error.  */
		return ret;

	if (ret < 0 && ret != EXTERNAL_BACKING_EXCLUDED)
		return 0; /* Not externally backing, other than due to exclusion.  */

	if (unlikely(ret == EXTERNAL_BACKING_EXCLUDED)) {
		/* Not externally backing due to exclusion.  */
		union wimlib_progress_info info;

		build_extraction_path(excluded_dentry, ctx);

		info.wimboot_exclude.path_in_wim = excluded_dentry->d_full_path;
		info.wimboot_exclude.extraction_path = current_path(ctx);

		return call_progress(ctx->common.progfunc,
				     WIMLIB_PROGRESS_MSG_WIMBOOT_EXCLUDE,
				     &info, ctx->common.progctx);
	}

	/* Externally backing.  */

	blob = inode_get_blob_for_unnamed_data_stream_resolved(inode);
	wimboot_wim = find_wimboot_wim(blob->rdesc->wim, ctx);

	if (unlikely(!wimboot_set_pointer(h,
					  blob,
					  wimboot_wim->data_source_id,
					  wimboot_wim->blob_table_hash,
					  ctx->wimboot.wof_running)))
	{
		const DWORD err = GetLastError();

		build_extraction_path(inode_first_extraction_dentry(inode), ctx);
		win32_error(err, L"\"%ls\": Couldn't set WIMBoot pointer data",
			    current_path(ctx));
		return WIMLIB_ERR_WIMBOOT;
	}
	return 0;
}

/* Calculates the SHA-1 message digest of the WIM's blob table.  */
static int
hash_blob_table(WIMStruct *wim, u8 hash[SHA1_HASH_SIZE])
{
	return wim_reshdr_to_hash(&wim->hdr.blob_table_reshdr, wim, hash);
}

static int
register_wim_with_wof(WIMStruct *wim, struct win32_apply_ctx *ctx)
{
	struct wimboot_wim *p;
	int ret;

	/* Check if already registered  */
	for (size_t i = 0; i < ctx->wimboot.num_wims; i++)
		if (wim == ctx->wimboot.wims[i].wim)
			return 0;

	/* Not yet registered  */

	p = REALLOC(ctx->wimboot.wims,
		    (ctx->wimboot.num_wims + 1) * sizeof(ctx->wimboot.wims[0]));
	if (!p)
		return WIMLIB_ERR_NOMEM;
	ctx->wimboot.wims = p;

	ctx->wimboot.wims[ctx->wimboot.num_wims].wim = wim;

	ret = hash_blob_table(wim, ctx->wimboot.wims[ctx->wimboot.num_wims].blob_table_hash);
	if (ret)
		return ret;

	ret = wimboot_alloc_data_source_id(wim->filename,
					   wim->hdr.guid,
					   ctx->common.wim->current_image,
					   ctx->common.target,
					   &ctx->wimboot.wims[ctx->wimboot.num_wims].data_source_id,
					   &ctx->wimboot.wof_running);
	if (ret)
		return ret;

	ctx->wimboot.num_wims++;
	return 0;
}

/* Prepare for doing a "WIMBoot" extraction by registering each source WIM file
 * with WOF on the target volume.  */
static int
start_wimboot_extraction(struct list_head *dentry_list, struct win32_apply_ctx *ctx)
{
	int ret;
	struct wim_dentry *dentry;

	if (!xml_get_wimboot(ctx->common.wim->xml_info,
			     ctx->common.wim->current_image))
		WARNING("The WIM image is not marked as WIMBoot compatible.  This usually\n"
			"          means it is not intended to be used to back a Windows operating\n"
			"          system.  Proceeding anyway.");

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		struct blob_descriptor *blob;

		ret = win32_will_back_from_wim(dentry, &ctx->common);
		if (ret > 0) /* Error */
			return ret;
		if (ret < 0) /* Won't externally back */
			continue;

		blob = inode_get_blob_for_unnamed_data_stream_resolved(dentry->d_inode);
		ret = register_wim_with_wof(blob->rdesc->wim, ctx);
		if (ret)
			return ret;
	}

	if (ctx->wimboot.have_wrong_version_wims) {
  WARNING("At least one of the source WIM files uses a version of the WIM\n"
"          file format that not supported by Microsoft's wof.sys driver.\n"
"          Files whose data is contained in one of these WIM files will be\n"
"          extracted as full files rather than externally backed.");
	}

	if (ctx->wimboot.have_uncompressed_wims) {
  WARNING("At least one of the source WIM files is uncompressed.  Files whose\n"
"          data is contained in an uncompressed WIM file will be extracted as\n"
"          full files rather than externally backed, since uncompressed WIM\n"
"          files are not supported by Microsoft's wof.sys driver.");
	}

	if (ctx->wimboot.have_unsupported_compressed_resources) {
  WARNING("At least one of the source WIM files uses a compression format that\n"
"          is not supported by Microsoft's wof.sys driver.  Files whose data is\n"
"          contained in a compressed resource in one of these WIM files will be\n"
"          extracted as full files rather than externally backed.  (The\n"
"          compression formats supported by wof.sys are: XPRESS 4K, XPRESS 8K,\n"
"          XPRESS 16K, XPRESS 32K, and LZX 32K.)");
	}

	if (ctx->wimboot.have_huge_resources) {
  WARNING("Some files exceeded 4.2 GB in size.  Such files will be extracted\n"
"          as full files rather than externally backed, since very large files\n"
"          are not supported by Microsoft's wof.sys driver.");
	}

	return 0;
}

static void
build_win32_extraction_path(const struct wim_dentry *dentry,
			    struct win32_apply_ctx *ctx);

/* Sets WimBoot=1 in the extracted SYSTEM registry hive.
 *
 * WIMGAPI does this, and it's possible that it's important.
 * But I don't know exactly what this value means to Windows.  */
static int
end_wimboot_extraction(struct win32_apply_ctx *ctx)
{
	struct wim_dentry *dentry;
	wchar_t subkeyname[32];
	LONG res;
	LONG res2;
	HKEY key;
	DWORD value;

	dentry = get_dentry(ctx->common.wim, L"\\Windows\\System32\\config\\SYSTEM",
			    WIMLIB_CASE_INSENSITIVE);

	if (!dentry || !will_extract_dentry(dentry))
		goto out;

	if (!will_extract_dentry(wim_get_current_root_dentry(ctx->common.wim)))
		goto out;

	/* Not bothering to use the native routines (e.g. NtLoadKey()) for this.
	 * If this doesn't work, you probably also have many other problems.  */

	build_win32_extraction_path(dentry, ctx);

	get_random_alnum_chars(subkeyname, 20);
	subkeyname[20] = L'\0';

	res = RegLoadKey(HKEY_LOCAL_MACHINE, subkeyname, ctx->pathbuf.Buffer);
	if (res)
		goto out_check_res;

	wcscpy(&subkeyname[20], L"\\Setup");

	res = RegCreateKeyEx(HKEY_LOCAL_MACHINE, subkeyname, 0, NULL,
			     REG_OPTION_BACKUP_RESTORE, 0, NULL, &key, NULL);
	if (res)
		goto out_unload_key;

	value = 1;

	res = RegSetValueEx(key, L"WimBoot", 0, REG_DWORD,
			    (const BYTE *)&value, sizeof(DWORD));
	if (res)
		goto out_close_key;

	res = RegFlushKey(key);

out_close_key:
	res2 = RegCloseKey(key);
	if (!res)
		res = res2;
out_unload_key:
	subkeyname[20] = L'\0';
	RegUnLoadKey(HKEY_LOCAL_MACHINE, subkeyname);
out_check_res:
	if (res) {
		/* Warning only.  */
		win32_warning(res, L"Failed to set \\Setup: dword \"WimBoot\"=1 "
			      "value in registry hive \"%ls\"",
			      ctx->pathbuf.Buffer);
	}
out:
	return 0;
}

/* Returns the number of wide characters needed to represent the path to the
 * specified @dentry, relative to the target directory, when extracted.
 *
 * Does not include null terminator (not needed for NtCreateFile).  */
static size_t
dentry_extraction_path_length(const struct wim_dentry *dentry)
{
	size_t len = 0;
	const struct wim_dentry *d;

	d = dentry;
	do {
		len += d->d_extraction_name_nchars + 1;
		d = d->d_parent;
	} while (!dentry_is_root(d) && will_extract_dentry(d));

	return --len;  /* No leading slash  */
}

/* Returns the length of the longest string that might need to be appended to
 * the path to an alias of an inode to open or create a named data stream.
 *
 * If the inode has no named data streams, this will be 0.  Otherwise, this will
 * be 1 plus the length of the longest-named data stream, since the data stream
 * name must be separated from the path by the ':' character.  */
static size_t
inode_longest_named_data_stream_spec(const struct wim_inode *inode)
{
	size_t max = 0;
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		const struct wim_inode_stream *strm = &inode->i_streams[i];
		if (!stream_is_named_data_stream(strm))
			continue;
		size_t len = utf16le_len_chars(strm->stream_name);
		if (len > max)
			max = len;
	}
	if (max)
		max += 1;
	return max;
}

/* Find the length, in wide characters, of the longest path needed for
 * extraction of any file in @dentry_list relative to the target directory.
 *
 * Accounts for named data streams, but does not include null terminator (not
 * needed for NtCreateFile).  */
static size_t
compute_path_max(struct list_head *dentry_list)
{
	size_t max = 0;
	const struct wim_dentry *dentry;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		size_t len;

		len = dentry_extraction_path_length(dentry);

		/* Account for named data streams  */
		len += inode_longest_named_data_stream_spec(dentry->d_inode);

		if (len > max)
			max = len;
	}

	return max;
}

/* Build the path at which to extract the @dentry, relative to the target
 * directory.
 *
 * The path is saved in ctx->pathbuf.  */
static void
build_extraction_path(const struct wim_dentry *dentry,
		      struct win32_apply_ctx *ctx)
{
	size_t len;
	wchar_t *p;
	const struct wim_dentry *d;

	len = dentry_extraction_path_length(dentry);

	ctx->pathbuf.Length = len * sizeof(wchar_t);
	p = ctx->pathbuf.Buffer + len;
	for (d = dentry;
	     !dentry_is_root(d->d_parent) && will_extract_dentry(d->d_parent);
	     d = d->d_parent)
	{
		p -= d->d_extraction_name_nchars;
		if (d->d_extraction_name_nchars)
			wmemcpy(p, d->d_extraction_name,
				d->d_extraction_name_nchars);
		*--p = '\\';
	}
	/* No leading slash  */
	p -= d->d_extraction_name_nchars;
	wmemcpy(p, d->d_extraction_name, d->d_extraction_name_nchars);
}

/* Build the path at which to extract the @dentry, relative to the target
 * directory, adding the suffix for a named data stream.
 *
 * The path is saved in ctx->pathbuf.  */
static void
build_extraction_path_with_ads(const struct wim_dentry *dentry,
			       struct win32_apply_ctx *ctx,
			       const wchar_t *stream_name,
			       size_t stream_name_nchars)
{
	wchar_t *p;

	build_extraction_path(dentry, ctx);

	/* Add :NAME for named data stream  */
	p = ctx->pathbuf.Buffer + (ctx->pathbuf.Length / sizeof(wchar_t));
	*p++ = L':';
	wmemcpy(p, stream_name, stream_name_nchars);
	ctx->pathbuf.Length += (1 + stream_name_nchars) * sizeof(wchar_t);
}

/* Build the Win32 namespace path to the specified @dentry when extracted.
 *
 * The path is saved in ctx->pathbuf and will be null terminated.
 *
 * XXX: We could get rid of this if it wasn't needed for the file encryption
 * APIs, and the registry manipulation in WIMBoot mode.  */
static void
build_win32_extraction_path(const struct wim_dentry *dentry,
			    struct win32_apply_ctx *ctx)
{
	build_extraction_path(dentry, ctx);

	/* Prepend target_ntpath to our relative path, then change \??\ into \\?\  */

	memmove(ctx->pathbuf.Buffer +
			(ctx->target_ntpath.Length / sizeof(wchar_t)) + 1,
		ctx->pathbuf.Buffer, ctx->pathbuf.Length);
	memcpy(ctx->pathbuf.Buffer, ctx->target_ntpath.Buffer,
		ctx->target_ntpath.Length);
	ctx->pathbuf.Buffer[ctx->target_ntpath.Length / sizeof(wchar_t)] = L'\\';
	ctx->pathbuf.Length += ctx->target_ntpath.Length + sizeof(wchar_t);
	ctx->pathbuf.Buffer[ctx->pathbuf.Length / sizeof(wchar_t)] = L'\0';

	wimlib_assert(ctx->pathbuf.Length >= 4 * sizeof(wchar_t) &&
		      !wmemcmp(ctx->pathbuf.Buffer, L"\\??\\", 4));

	ctx->pathbuf.Buffer[1] = L'\\';

}

/* Returns a "printable" representation of the last relative NT path that was
 * constructed with build_extraction_path() or build_extraction_path_with_ads().
 *
 * This will be overwritten by the next call to this function.  */
static const wchar_t *
current_path(struct win32_apply_ctx *ctx)
{
	wchar_t *p = ctx->print_buffer;

	p = wmempcpy(p, ctx->common.target, ctx->common.target_nchars);
	*p++ = L'\\';
	p = wmempcpy(p, ctx->pathbuf.Buffer, ctx->pathbuf.Length / sizeof(wchar_t));
	*p = L'\0';
	return ctx->print_buffer;
}

/* Open handle to the target directory if it is not already open.  If the target
 * directory does not exist, this creates it.  */
static int
open_target_directory(struct win32_apply_ctx *ctx)
{
	NTSTATUS status;

	if (ctx->h_target)
		return 0;

	ctx->attr.Length = sizeof(ctx->attr);
	ctx->attr.RootDirectory = NULL;
	ctx->attr.ObjectName = &ctx->target_ntpath;

	/* Don't use FILE_OPEN_REPARSE_POINT here; we want the extraction to
	 * happen at the directory "pointed to" by the reparse point. */
	status = NtCreateFile(&ctx->h_target,
			      FILE_TRAVERSE,
			      &ctx->attr,
			      &ctx->iosb,
			      NULL,
			      0,
			      FILE_SHARE_VALID_FLAGS,
			      FILE_OPEN_IF,
			      FILE_DIRECTORY_FILE | FILE_OPEN_FOR_BACKUP_INTENT,
			      NULL,
			      0);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"Can't open or create directory \"%ls\"",
			    ctx->common.target);
		return WIMLIB_ERR_OPENDIR;
	}
	ctx->attr.RootDirectory = ctx->h_target;
	ctx->attr.ObjectName = &ctx->pathbuf;
	return 0;
}

static void
close_target_directory(struct win32_apply_ctx *ctx)
{
	if (ctx->h_target) {
		NtClose(ctx->h_target);
		ctx->h_target = NULL;
		ctx->attr.RootDirectory = NULL;
	}
}

/*
 * Ensures the target directory exists and opens a handle to it, in preparation
 * of using paths relative to it.
 */
static int
prepare_target(struct list_head *dentry_list, struct win32_apply_ctx *ctx)
{
	int ret;
	size_t path_max;

	ret = win32_path_to_nt_path(ctx->common.target, &ctx->target_ntpath);
	if (ret)
		return ret;

	ret = open_target_directory(ctx);
	if (ret)
		return ret;

	path_max = compute_path_max(dentry_list);
	/* Add some extra for building Win32 paths for the file encryption APIs,
	 * and ensure we have at least enough to potentially use an 8.3 name for
	 * the last component.  */
	path_max += max(2 + (ctx->target_ntpath.Length / sizeof(wchar_t)),
			8 + 1 + 3);

	ctx->pathbuf.MaximumLength = path_max * sizeof(wchar_t);
	if (ctx->pathbuf.MaximumLength != path_max * sizeof(wchar_t)) {
		/* Paths are too long for a UNICODE_STRING! */
		ERROR("Some paths are too long to extract (> 32768 characters)!");
		return WIMLIB_ERR_UNSUPPORTED;
	}

	ctx->pathbuf.Buffer = MALLOC(ctx->pathbuf.MaximumLength);
	if (!ctx->pathbuf.Buffer)
		return WIMLIB_ERR_NOMEM;

	ctx->print_buffer = MALLOC((ctx->common.target_nchars + 1 + path_max + 1) *
				   sizeof(wchar_t));
	if (!ctx->print_buffer)
		return WIMLIB_ERR_NOMEM;

	return 0;
}

/* When creating an inode that will have a short (DOS) name, we create it using
 * the long name associated with the short name.  This ensures that the short
 * name gets associated with the correct long name.  */
static struct wim_dentry *
first_extraction_alias(const struct wim_inode *inode)
{
	struct wim_dentry *dentry;

	inode_for_each_extraction_alias(dentry, inode)
		if (dentry_has_short_name(dentry))
			return dentry;
	return inode_first_extraction_dentry(inode);
}

/*
 * Set or clear FILE_ATTRIBUTE_COMPRESSED if the inherited value is different
 * from the desired value.
 *
 * Note that you can NOT override the inherited value of
 * FILE_ATTRIBUTE_COMPRESSED directly with NtCreateFile().
 */
static int
adjust_compression_attribute(HANDLE h, const struct wim_dentry *dentry,
			     struct win32_apply_ctx *ctx)
{
	const bool compressed = (dentry->d_inode->i_attributes &
				 FILE_ATTRIBUTE_COMPRESSED);
	FILE_BASIC_INFORMATION info = { 0 };
	USHORT compression_state;
	NTSTATUS status;

	if (ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_NO_ATTRIBUTES)
		return 0;

	if (!ctx->common.supported_features.compressed_files)
		return 0;


	/* Get current attributes  */
	status = NtQueryInformationFile(h, &ctx->iosb, &info, sizeof(info),
					FileBasicInformation);
	if (NT_SUCCESS(status) &&
	    compressed == !!(info.FileAttributes & FILE_ATTRIBUTE_COMPRESSED))
	{
		/* Nothing needs to be done.  */
		return 0;
	}

	/* Set the new compression state  */

	if (compressed)
		compression_state = COMPRESSION_FORMAT_DEFAULT;
	else
		compression_state = COMPRESSION_FORMAT_NONE;

	status = winnt_fsctl(h, FSCTL_SET_COMPRESSION,
			     &compression_state, sizeof(USHORT), NULL, 0, NULL);
	if (NT_SUCCESS(status))
		return 0;

	winnt_error(status, L"Can't %s compression attribute on \"%ls\"",
		    (compressed ? "set" : "clear"), current_path(ctx));
	return WIMLIB_ERR_SET_ATTRIBUTES;
}

static bool
need_sparse_flag(const struct wim_inode *inode,
		 const struct win32_apply_ctx *ctx)
{
	return (inode->i_attributes & FILE_ATTRIBUTE_SPARSE_FILE) &&
		ctx->common.supported_features.sparse_files;
}

static int
set_sparse_flag(HANDLE h, struct win32_apply_ctx *ctx)
{
	NTSTATUS status;

	status = winnt_fsctl(h, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, NULL);
	if (NT_SUCCESS(status))
		return 0;

	winnt_error(status, L"Can't set sparse flag on \"%ls\"",
		    current_path(ctx));
	return WIMLIB_ERR_SET_ATTRIBUTES;
}

/* Try to enable short name support on the target volume.  If successful, return
 * true.  If unsuccessful, issue a warning and return false.  */
static bool
try_to_enable_short_names(const wchar_t *volume)
{
	HANDLE h;
	FILE_FS_PERSISTENT_VOLUME_INFORMATION info;
	BOOL bret;
	DWORD bytesReturned;

	h = CreateFile(volume, GENERIC_WRITE,
		       FILE_SHARE_VALID_FLAGS, NULL, OPEN_EXISTING,
		       FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE)
		goto fail;

	info.VolumeFlags = 0;
	info.FlagMask = PERSISTENT_VOLUME_STATE_SHORT_NAME_CREATION_DISABLED;
	info.Version = 1;
	info.Reserved = 0;

	bret = DeviceIoControl(h, FSCTL_SET_PERSISTENT_VOLUME_STATE,
			       &info, sizeof(info), NULL, 0,
			       &bytesReturned, NULL);

	CloseHandle(h);

	if (!bret)
		goto fail;
	return true;

fail:
	win32_warning(GetLastError(),
		      L"Failed to enable short name support on %ls",
		      volume + 4);
	return false;
}

static NTSTATUS
remove_conflicting_short_name(const struct wim_dentry *dentry, struct win32_apply_ctx *ctx)
{
	wchar_t *name;
	wchar_t *end;
	NTSTATUS status;
	HANDLE h;
	size_t bufsize = offsetof(FILE_NAME_INFORMATION, FileName) +
			 (13 * sizeof(wchar_t));
	u8* buf = wimlib_aligned_malloc(bufsize, 8);
	bool retried = false;
	FILE_NAME_INFORMATION *info = (FILE_NAME_INFORMATION *)buf;

	memset(buf, 0, bufsize);

	/* Build the path with the short name.  */
	name = &ctx->pathbuf.Buffer[ctx->pathbuf.Length / sizeof(wchar_t)];
	while (name != ctx->pathbuf.Buffer && *(name - 1) != L'\\')
		name--;
	end = mempcpy(name, dentry->d_short_name, dentry->d_short_name_nbytes);
	ctx->pathbuf.Length = ((u8 *)end - (u8 *)ctx->pathbuf.Buffer);

	/* Open the conflicting file (by short name).  */
	status = NtOpenFile(&h, GENERIC_WRITE | DELETE,
			    &ctx->attr, &ctx->iosb,
			    FILE_SHARE_VALID_FLAGS,
			    FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT);
	if (!NT_SUCCESS(status)) {
		winnt_warning(status, L"Can't open \"%ls\"", current_path(ctx));
		goto out;
	}

#if 0
	WARNING("Overriding conflicting short name; path=\"%ls\"",
		current_path(ctx));
#endif

	/* Try to remove the short name on the conflicting file.  */

retry:
	status = NtSetInformationFile(h, &ctx->iosb, info, bufsize,
				      FileShortNameInformation);

	if (status == STATUS_INVALID_PARAMETER && !retried) {
		/* Microsoft forgot to make it possible to remove short names
		 * until Windows 7.  Oops.  Use a random short name instead.  */
		get_random_alnum_chars(info->FileName, 8);
		wcscpy(&info->FileName[8], L".WLB");
		info->FileNameLength = 12 * sizeof(wchar_t);
		retried = true;
		goto retry;
	}
	NtClose(h);
out:
	wimlib_aligned_free(buf);
	build_extraction_path(dentry, ctx);
	return status;
}

/* Set the short name on the open file @h which has been created at the location
 * indicated by @dentry.
 *
 * Note that this may add, change, or remove the short name.
 *
 * @h must be opened with DELETE access.
 *
 * Returns 0 or WIMLIB_ERR_SET_SHORT_NAME.  The latter only happens in
 * STRICT_SHORT_NAMES mode.
 */
static int
set_short_name(HANDLE h, const struct wim_dentry *dentry,
	       struct win32_apply_ctx *ctx)
{

	if (!ctx->common.supported_features.short_names)
		return 0;

	/*
	 * Note: The size of the FILE_NAME_INFORMATION buffer must be such that
	 * FileName contains at least 2 wide characters (4 bytes).  Otherwise,
	 * NtSetInformationFile() will return STATUS_INFO_LENGTH_MISMATCH.  This
	 * is despite the fact that FileNameLength can validly be 0 or 2 bytes,
	 * with the former case being removing the existing short name if
	 * present, rather than setting one.
	 *
	 * The null terminator is seemingly optional, but to be safe we include
	 * space for it and zero all unused space.
	 */

	size_t bufsize = offsetof(FILE_NAME_INFORMATION, FileName) +
			 max(dentry->d_short_name_nbytes, sizeof(wchar_t)) +
			 sizeof(wchar_t);
	u8* buf = wimlib_aligned_malloc(bufsize, 8);
	FILE_NAME_INFORMATION *info = (FILE_NAME_INFORMATION *)buf;
	NTSTATUS status;
	bool tried_to_remove_existing = false;

	memset(buf, 0, bufsize);

	info->FileNameLength = dentry->d_short_name_nbytes;
	memcpy(info->FileName, dentry->d_short_name, dentry->d_short_name_nbytes);

retry:
	status = NtSetInformationFile(h, &ctx->iosb, info, bufsize,
				      FileShortNameInformation);
	if (NT_SUCCESS(status)) {
		wimlib_aligned_free(buf);
		return 0;
	}

	if (status == STATUS_SHORT_NAMES_NOT_ENABLED_ON_VOLUME) {
		if (dentry->d_short_name_nbytes == 0)
			return 0;
		if (!ctx->tried_to_enable_short_names) {
			wchar_t volume[7];
			int ret;

			ctx->tried_to_enable_short_names = true;

			ret = win32_get_drive_path(ctx->common.target,
						   volume);
			if (ret) {
				wimlib_aligned_free(buf);
				return ret;
			}
			if (try_to_enable_short_names(volume))
				goto retry;
		}
	}

	/*
	 * Short names can conflict in several cases:
	 *
	 * - a file being extracted has a short name conflicting with an
	 *   existing file
	 *
	 * - a file being extracted has a short name conflicting with another
	 *   file being extracted (possible, but shouldn't happen)
	 *
	 * - a file being extracted has a short name that conflicts with the
	 *   automatically generated short name of a file we previously
	 *   extracted, but failed to set the short name for.  Sounds unlikely,
	 *   but this actually does happen fairly often on versions of Windows
	 *   prior to Windows 7 because they do not support removing short names
	 *   from files.
	 */
	if (unlikely(status == STATUS_OBJECT_NAME_COLLISION) &&
	    dentry->d_short_name_nbytes && !tried_to_remove_existing)
	{
		tried_to_remove_existing = true;
		status = remove_conflicting_short_name(dentry, ctx);
		if (NT_SUCCESS(status))
			goto retry;
	}

	/* By default, failure to set short names is not an error (since short
	 * names aren't too important anymore...).  */
	if (!(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_SHORT_NAMES)) {
		if (dentry->d_short_name_nbytes)
			ctx->num_set_short_name_failures++;
		else
			ctx->num_remove_short_name_failures++;
		wimlib_aligned_free(buf);
		return 0;
	}

	winnt_error(status, L"Can't set short name on \"%ls\"", current_path(ctx));
	wimlib_aligned_free(buf);
	return WIMLIB_ERR_SET_SHORT_NAME;
}

/*
 * A wrapper around NtCreateFile() to make it slightly more usable...
 * This uses the path currently constructed in ctx->pathbuf.
 *
 * Also, we always specify SYNCHRONIZE access, FILE_OPEN_FOR_BACKUP_INTENT, and
 * FILE_OPEN_REPARSE_POINT.
 */
static NTSTATUS
do_create_file(PHANDLE FileHandle,
	       ACCESS_MASK DesiredAccess,
	       PLARGE_INTEGER AllocationSize,
	       ULONG FileAttributes,
	       ULONG CreateDisposition,
	       ULONG CreateOptions,
	       struct win32_apply_ctx *ctx)
{
	return NtCreateFile(FileHandle,
			    DesiredAccess | SYNCHRONIZE,
			    &ctx->attr,
			    &ctx->iosb,
			    AllocationSize,
			    FileAttributes,
			    FILE_SHARE_VALID_FLAGS,
			    CreateDisposition,
			    CreateOptions |
				FILE_OPEN_FOR_BACKUP_INTENT |
				FILE_OPEN_REPARSE_POINT,
			    NULL,
			    0);
}

/* Like do_create_file(), but builds the extraction path of the @dentry first.
 */
static NTSTATUS
create_file(PHANDLE FileHandle,
	    ACCESS_MASK DesiredAccess,
	    PLARGE_INTEGER AllocationSize,
	    ULONG FileAttributes,
	    ULONG CreateDisposition,
	    ULONG CreateOptions,
	    const struct wim_dentry *dentry,
	    struct win32_apply_ctx *ctx)
{
	build_extraction_path(dentry, ctx);
	return do_create_file(FileHandle,
			      DesiredAccess,
			      AllocationSize,
			      FileAttributes,
			      CreateDisposition,
			      CreateOptions,
			      ctx);
}

static int
delete_file_or_stream(struct win32_apply_ctx *ctx)
{
	NTSTATUS status;
	HANDLE h;
	ULONG perms = DELETE;
	ULONG flags = FILE_NON_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE;

	/* First try opening the file with FILE_DELETE_ON_CLOSE.  In most cases,
	 * all we have to do is that plus close the file handle.  */
retry:
	status = do_create_file(&h, perms, NULL, 0, FILE_OPEN, flags, ctx);

	if (unlikely(status == STATUS_CANNOT_DELETE)) {
		/* This error occurs for files with FILE_ATTRIBUTE_READONLY set.
		 * Try an alternate approach: first open the file without
		 * FILE_DELETE_ON_CLOSE, then reset the file attributes, then
		 * set the "delete" disposition on the handle.  */
		if (flags & FILE_DELETE_ON_CLOSE) {
			flags &= ~FILE_DELETE_ON_CLOSE;
			perms |= FILE_WRITE_ATTRIBUTES;
			goto retry;
		}
	}

	if (unlikely(!NT_SUCCESS(status))) {
		winnt_error(status, L"Can't open \"%ls\" for deletion "
			    "(perms=%x, flags=%x)",
			    current_path(ctx), (u32)perms, (u32)flags);
		return WIMLIB_ERR_OPEN;
	}

	if (unlikely(!(flags & FILE_DELETE_ON_CLOSE))) {

		FILE_BASIC_INFORMATION basic_info =
			{ .FileAttributes = FILE_ATTRIBUTE_NORMAL };
		status = NtSetInformationFile(h, &ctx->iosb, &basic_info,
					      sizeof(basic_info),
					      FileBasicInformation);

		if (!NT_SUCCESS(status)) {
			winnt_error(status, L"Can't reset attributes of \"%ls\" "
				    "to prepare for deletion", current_path(ctx));
			NtClose(h);
			return WIMLIB_ERR_SET_ATTRIBUTES;
		}

		FILE_DISPOSITION_INFORMATION disp_info =
			{ .DoDeleteFile = TRUE };
		status = NtSetInformationFile(h, &ctx->iosb, &disp_info,
					      sizeof(disp_info),
					      FileDispositionInformation);
		if (!NT_SUCCESS(status)) {
			winnt_error(status, L"Can't set delete-on-close "
				    "disposition on \"%ls\"", current_path(ctx));
			NtClose(h);
			return WIMLIB_ERR_SET_ATTRIBUTES;
		}
	}

	status = NtClose(h);
	if (unlikely(!NT_SUCCESS(status))) {
		winnt_error(status, L"Error closing \"%ls\" after setting "
			    "delete-on-close disposition", current_path(ctx));
		return WIMLIB_ERR_OPEN;
	}

	return 0;
}

/*
 * Create a nondirectory file or named data stream at the current path,
 * superseding any that already exists at that path.  If successful, return an
 * open handle to the file or named data stream with the requested permissions.
 */
static int
supersede_file_or_stream(struct win32_apply_ctx *ctx, DWORD perms,
			 HANDLE *h_ret)
{
	NTSTATUS status;
	bool retried = false;

	/* FILE_ATTRIBUTE_SYSTEM is needed to ensure that
	 * FILE_ATTRIBUTE_ENCRYPTED doesn't get set before we want it to be.  */
retry:
	status = do_create_file(h_ret,
				perms,
				NULL,
				FILE_ATTRIBUTE_SYSTEM,
				FILE_CREATE,
				FILE_NON_DIRECTORY_FILE,
				ctx);
	if (likely(NT_SUCCESS(status)))
		return 0;

	/* STATUS_OBJECT_NAME_COLLISION means that the file or stream already
	 * exists.  Delete the existing file or stream, then try again.
	 *
	 * Note: we don't use FILE_OVERWRITE_IF or FILE_SUPERSEDE because of
	 * problems with certain file attributes, especially
	 * FILE_ATTRIBUTE_ENCRYPTED.  FILE_SUPERSEDE is also broken in the
	 * Windows PE ramdisk.  */
	if (status == STATUS_OBJECT_NAME_COLLISION && !retried) {
		int ret = delete_file_or_stream(ctx);
		if (ret)
			return ret;
		retried = true;
		goto retry;
	}
	winnt_error(status, L"Can't create \"%ls\"", current_path(ctx));
	return WIMLIB_ERR_OPEN;
}

/* Set the reparse point @rpbuf of length @rpbuflen on the extracted file
 * corresponding to the WIM dentry @dentry.  */
static int
do_set_reparse_point(const struct wim_dentry *dentry,
		     const struct reparse_buffer_disk *rpbuf, u16 rpbuflen,
		     struct win32_apply_ctx *ctx)
{
	NTSTATUS status;
	HANDLE h;

	status = create_file(&h, GENERIC_WRITE, NULL,
			     0, FILE_OPEN, 0, dentry, ctx);
	if (!NT_SUCCESS(status))
		goto fail;

	status = winnt_fsctl(h, FSCTL_SET_REPARSE_POINT,
			     rpbuf, rpbuflen, NULL, 0, NULL);
	NtClose(h);

	if (NT_SUCCESS(status))
		return 0;

	/* On Windows, by default only the Administrator can create symbolic
	 * links for some reason.  By default we just issue a warning if this
	 * appears to be the problem.  Use WIMLIB_EXTRACT_FLAG_STRICT_SYMLINKS
	 * to get a hard error.  */
	if (!(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_SYMLINKS)
	    && (status == STATUS_PRIVILEGE_NOT_HELD ||
		status == STATUS_ACCESS_DENIED)
	    && (dentry->d_inode->i_reparse_tag == WIM_IO_REPARSE_TAG_SYMLINK ||
		dentry->d_inode->i_reparse_tag == WIM_IO_REPARSE_TAG_MOUNT_POINT))
	{
		WARNING("Can't create symbolic link \"%ls\"!              \n"
			"          (Need Administrator rights, or at least "
			"the\n"
			"          SeCreateSymbolicLink privilege.)",
			current_path(ctx));
		return 0;
	}

fail:
	winnt_error(status, L"Can't set reparse data on \"%ls\"",
		    current_path(ctx));
	return WIMLIB_ERR_SET_REPARSE_DATA;
}

/*
 * Create empty named data streams and potentially a reparse point for the
 * specified file, if any.
 *
 * Since these won't have blob descriptors, they won't show up in the call to
 * extract_blob_list().  Hence the need for the special case.
 */
static int
create_empty_streams(const struct wim_dentry *dentry,
		     struct win32_apply_ctx *ctx)
{
	const struct wim_inode *inode = dentry->d_inode;
	int ret;

	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		const struct wim_inode_stream *strm = &inode->i_streams[i];

		if (stream_blob_resolved(strm) != NULL)
			continue;

		if (strm->stream_type == STREAM_TYPE_REPARSE_POINT &&
		    ctx->common.supported_features.reparse_points)
		{
			PRAGMA_ALIGN(u8 buf[REPARSE_DATA_OFFSET], 8);
			struct reparse_buffer_disk *rpbuf =
				(struct reparse_buffer_disk *)buf;
			complete_reparse_point(rpbuf, inode, 0);
			ret = do_set_reparse_point(dentry, rpbuf,
						   REPARSE_DATA_OFFSET, ctx);
			if (ret)
				return ret;
		} else if (stream_is_named_data_stream(strm) &&
			   ctx->common.supported_features.named_data_streams)
		{
			HANDLE h;

			build_extraction_path_with_ads(dentry, ctx,
						       strm->stream_name,
						       utf16le_len_chars(strm->stream_name));
			/*
			 * Note: do not request any permissions on the handle.
			 * Otherwise, we may encounter a Windows bug where the
			 * parent directory DACL denies read access to the new
			 * named data stream, even when using backup semantics!
			 */
			ret = supersede_file_or_stream(ctx, 0, &h);

			build_extraction_path(dentry, ctx);

			if (ret)
				return ret;
			NtClose(h);
		}
	}

	return 0;
}

/*
 * Creates the directory named by @dentry, or uses an existing directory at that
 * location.  If necessary, sets the short name and/or fixes compression and
 * encryption attributes.
 *
 * Returns 0, WIMLIB_ERR_MKDIR, or WIMLIB_ERR_SET_SHORT_NAME.
 */
static int
create_directory(const struct wim_dentry *dentry, struct win32_apply_ctx *ctx)
{
	DWORD perms;
	NTSTATUS status;
	HANDLE h;
	int ret;

	/* DELETE is needed for set_short_name(); GENERIC_READ and GENERIC_WRITE
	 * are needed for adjust_compression_attribute().  */
	perms = GENERIC_READ | GENERIC_WRITE;
	if (!dentry_is_root(dentry))
		perms |= DELETE;

	/* FILE_ATTRIBUTE_SYSTEM is needed to ensure that
	 * FILE_ATTRIBUTE_ENCRYPTED doesn't get set before we want it to be.  */
	status = create_file(&h, perms, NULL, FILE_ATTRIBUTE_SYSTEM,
			     FILE_OPEN_IF, FILE_DIRECTORY_FILE, dentry, ctx);
	if (unlikely(!NT_SUCCESS(status))) {
		const wchar_t *path = current_path(ctx);
		winnt_error(status, L"Can't create directory \"%ls\"", path);

		/* Check for known issue with WindowsApps directory.  */
		if (status == STATUS_ACCESS_DENIED &&
		    (wcsstr(path, L"\\WindowsApps\\") ||
		     wcsstr(path, L"\\InfusedApps\\"))) {
			ERROR(
"You seem to be trying to extract files to the WindowsApps directory.\n"
"        Windows 8.1 and later use new file permissions in this directory that\n"
"        cannot be overridden, even by backup/restore programs.  To extract your\n"
"        files anyway, you need to choose a different target directory, delete\n"
"        the WindowsApps directory entirely, reformat the volume, do the\n"
"        extraction from a non-broken operating system such as Windows 7 or\n"
"        Linux, or wait for Microsoft to fix the design flaw in their operating\n"
"        system.  This is *not* a bug in wimlib.  See this thread for more\n"
"        information: https://wimlib.net/forums/viewtopic.php?f=1&t=261");
		}
		return WIMLIB_ERR_MKDIR;
	}

	if (ctx->iosb.Information == FILE_OPENED) {
		/* If we opened an existing directory, try to clear its file
		 * attributes.  As far as I know, this only actually makes a
		 * difference in the case where a FILE_ATTRIBUTE_READONLY
		 * directory has a named data stream which needs to be
		 * extracted.  You cannot create a named data stream of such a
		 * directory, even though this contradicts Microsoft's
		 * documentation for FILE_ATTRIBUTE_READONLY which states it is
		 * not honored for directories!  */
		if (!(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_NO_ATTRIBUTES)) {
			FILE_BASIC_INFORMATION basic_info =
				{ .FileAttributes = FILE_ATTRIBUTE_NORMAL };
			NtSetInformationFile(h, &ctx->iosb, &basic_info,
					     sizeof(basic_info),
					     FileBasicInformation);
		}
	}

	if (!dentry_is_root(dentry)) {
		ret = set_short_name(h, dentry, ctx);
		if (ret)
			goto out;
	}

	ret = adjust_compression_attribute(h, dentry, ctx);
out:
	NtClose(h);
	return ret;
}

/*
 * Create all the directories being extracted, other than the target directory
 * itself.
 *
 * Note: we don't honor directory hard links.  However, we don't allow them to
 * exist in WIM images anyway (see inode_fixup.c).
 */
static int
create_directories(struct list_head *dentry_list,
		   struct win32_apply_ctx *ctx)
{
	const struct wim_dentry *dentry;
	int ret;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {

		if (!(dentry->d_inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;

		/* Note: Here we include files with
		 * FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT, but we
		 * wait until later to actually set the reparse data.  */

		ret = create_directory(dentry, ctx);

		if (!ret)
			ret = create_empty_streams(dentry, ctx);

		ret = check_apply_error(dentry, ctx, ret);
		if (ret)
			return ret;

		ret = report_file_created(&ctx->common);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Creates the nondirectory file named by @dentry.
 *
 * On success, returns an open handle to the file in @h_ret, with GENERIC_READ,
 * GENERIC_WRITE, and DELETE access.  Also, the path to the file will be saved
 * in ctx->pathbuf.  On failure, returns an error code.
 */
static int
create_nondirectory_inode(HANDLE *h_ret, const struct wim_dentry *dentry,
			  struct win32_apply_ctx *ctx)
{
	int ret;
	HANDLE h;

	build_extraction_path(dentry, ctx);

	ret = supersede_file_or_stream(ctx,
				       GENERIC_READ | GENERIC_WRITE | DELETE,
				       &h);
	if (ret)
		goto out;

	ret = adjust_compression_attribute(h, dentry, ctx);
	if (ret)
		goto out_close;

	if (need_sparse_flag(dentry->d_inode, ctx)) {
		ret = set_sparse_flag(h, ctx);
		if (ret)
			goto out_close;
	}

	ret = create_empty_streams(dentry, ctx);
	if (ret)
		goto out_close;

	*h_ret = h;
	return 0;

out_close:
	NtClose(h);
out:
	return ret;
}

/* Creates a hard link at the location named by @dentry to the file represented
 * by the open handle @h.  Or, if the target volume does not support hard links,
 * create a separate file instead.  */
static int
create_link(HANDLE h, const struct wim_dentry *dentry,
	    struct win32_apply_ctx *ctx)
{
	if (ctx->common.supported_features.hard_links) {

		build_extraction_path(dentry, ctx);

		size_t bufsize = offsetof(FILE_LINK_INFORMATION, FileName) +
				 ctx->pathbuf.Length + sizeof(wchar_t);
		u8* buf = wimlib_aligned_malloc(bufsize, 8);
		FILE_LINK_INFORMATION *info = (FILE_LINK_INFORMATION *)buf;
		NTSTATUS status;

		info->ReplaceIfExists = TRUE;
		info->RootDirectory = ctx->attr.RootDirectory;
		info->FileNameLength = ctx->pathbuf.Length;
		memcpy(info->FileName, ctx->pathbuf.Buffer, ctx->pathbuf.Length);
		info->FileName[info->FileNameLength / 2] = L'\0';
		/*
		 * Note: the null terminator isn't actually necessary, but if
		 * you don't add the extra character, you get
		 * STATUS_INFO_LENGTH_MISMATCH when FileNameLength is 2.
		 */

		/*
		 * When fuzzing with wlfuzz.exe, creating a hard link sometimes
		 * fails with STATUS_ACCESS_DENIED.  However, it eventually
		 * succeeds when re-attempted...
		 */
		int i = 0;
		do {
			status = NtSetInformationFile(h, &ctx->iosb, info,
						      bufsize,
						      FileLinkInformation);
			if (NT_SUCCESS(status)) {
				wimlib_aligned_free(buf);
				return 0;
			}
		} while (++i < 32);
		winnt_error(status, L"Failed to create link \"%ls\"",
			    current_path(ctx));
		wimlib_aligned_free(buf);
		return WIMLIB_ERR_LINK;
	} else {
		HANDLE h2;
		int ret;

		ret = create_nondirectory_inode(&h2, dentry, ctx);
		if (ret)
			return ret;

		NtClose(h2);
		return 0;
	}
}

/* Given an inode (represented by the open handle @h) for which one link has
 * been created (named by @first_dentry), create the other links.
 *
 * Or, if the target volume does not support hard links, create separate files.
 *
 * Note: This uses ctx->pathbuf and does not reset it.
 */
static int
create_links(HANDLE h, const struct wim_dentry *first_dentry,
	     struct win32_apply_ctx *ctx)
{
	const struct wim_inode *inode = first_dentry->d_inode;
	const struct wim_dentry *dentry;
	int ret;

	inode_for_each_extraction_alias(dentry, inode) {
		if (dentry != first_dentry) {
			ret = create_link(h, dentry, ctx);
			if (ret)
				return ret;
		}
	}
	return 0;
}

/* Create a nondirectory file, including all links.  */
static int
create_nondirectory(struct wim_inode *inode, struct win32_apply_ctx *ctx)
{
	struct wim_dentry *first_dentry;
	HANDLE h;
	int ret;

	first_dentry = first_extraction_alias(inode);

	/* Create first link.  */
	ret = create_nondirectory_inode(&h, first_dentry, ctx);
	if (ret)
		return ret;

	/* Set short name.  */
	ret = set_short_name(h, first_dentry, ctx);

	/* Create additional links, OR if hard links are not supported just
	 * create more files.  */
	if (!ret)
		ret = create_links(h, first_dentry, ctx);

	/* "WIMBoot" extraction: set external backing by the WIM file if needed.  */
	if (!ret && unlikely(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT))
		ret = set_backed_from_wim(h, inode, ctx);

	NtClose(h);
	return ret;
}

/* Create all the nondirectory files being extracted, including all aliases
 * (hard links).  */
static int
create_nondirectories(struct list_head *dentry_list, struct win32_apply_ctx *ctx)
{
	struct wim_dentry *dentry;
	struct wim_inode *inode;
	int ret;

	list_for_each_entry(dentry, dentry_list, d_extraction_list_node) {
		inode = dentry->d_inode;
		if (inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		/* Call create_nondirectory() only once per inode  */
		if (dentry == inode_first_extraction_dentry(inode)) {
			ret = create_nondirectory(inode, ctx);
			ret = check_apply_error(dentry, ctx, ret);
			if (ret)
				return ret;
		}
		ret = report_file_created(&ctx->common);
		if (ret)
			return ret;
	}
	return 0;
}

static void
close_handles(struct win32_apply_ctx *ctx)
{
	for (unsigned i = 0; i < ctx->num_open_handles; i++)
		NtClose(ctx->open_handles[i]);
}

/* Prepare to read the next blob, which has size @blob_size, into an in-memory
 * buffer.  */
static bool
prepare_data_buffer(struct win32_apply_ctx *ctx, u64 blob_size)
{
	if (blob_size > ctx->data_buffer_size) {
		/* Larger buffer needed.  */
		void *new_buffer;
		if ((size_t)blob_size != blob_size)
			return false;
		new_buffer = REALLOC(ctx->data_buffer, blob_size);
		if (!new_buffer)
			return false;
		ctx->data_buffer = new_buffer;
		ctx->data_buffer_size = blob_size;
	}
	/* On the first call this changes data_buffer_ptr from NULL, which tells
	 * extract_chunk() that the data buffer needs to be filled while reading
	 * the stream data.  */
	ctx->data_buffer_ptr = ctx->data_buffer;
	return true;
}

static int
begin_extract_blob_instance(const struct blob_descriptor *blob,
			    struct wim_dentry *dentry,
			    const struct wim_inode_stream *strm,
			    struct win32_apply_ctx *ctx)
{
	HANDLE h;
	NTSTATUS status;

	if (unlikely(strm->stream_type == STREAM_TYPE_REPARSE_POINT)) {
		/* We can't write the reparse point stream directly; we must set
		 * it with FSCTL_SET_REPARSE_POINT, which requires that all the
		 * data be available.  So, stage the data in a buffer.  */
		if (!prepare_data_buffer(ctx, blob->size))
			return WIMLIB_ERR_NOMEM;
		list_add_tail(&dentry->d_tmp_list, &ctx->reparse_dentries);
		return 0;
	}

	if (unlikely(strm->stream_type == STREAM_TYPE_EFSRPC_RAW_DATA)) {
		/* We can't write encrypted files directly; we must use
		 * WriteEncryptedFileRaw(), which requires providing the data
		 * through a callback function.  This can't easily be combined
		 * with our own callback-based approach.
		 *
		 * The current workaround is to simply read the blob into memory
		 * and write the encrypted file from that.
		 *
		 * TODO: This isn't sufficient for extremely large encrypted
		 * files.  Perhaps we should create an extra thread to write
		 * such files...  */
		if (!prepare_data_buffer(ctx, blob->size))
			return WIMLIB_ERR_NOMEM;
		list_add_tail(&dentry->d_tmp_list, &ctx->encrypted_dentries);
		return 0;
	}

	/* It's a data stream (may be unnamed or named).  */
	wimlib_assert(strm->stream_type == STREAM_TYPE_DATA);

	if (ctx->num_open_handles == MAX_OPEN_FILES) {
		/* XXX: Fix this.  But because of the checks in
		 * extract_blob_list(), this can now only happen on a filesystem
		 * that does not support hard links.  */
		ERROR("Can't extract data: too many open files!");
		return WIMLIB_ERR_UNSUPPORTED;
	}


	if (unlikely(stream_is_named(strm))) {
		build_extraction_path_with_ads(dentry, ctx,
					       strm->stream_name,
					       utf16le_len_chars(strm->stream_name));
	} else {
		build_extraction_path(dentry, ctx);
	}


	/* Open a new handle  */
	status = do_create_file(&h,
				FILE_WRITE_DATA | SYNCHRONIZE,
				NULL, 0, FILE_OPEN_IF,
				FILE_SEQUENTIAL_ONLY |
					FILE_SYNCHRONOUS_IO_NONALERT,
				ctx);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"Can't open \"%ls\" for writing",
			    current_path(ctx));
		return WIMLIB_ERR_OPEN;
	}

	ctx->is_sparse_stream[ctx->num_open_handles] = false;
	if (need_sparse_flag(dentry->d_inode, ctx)) {
		/* If the stream is unnamed, then the sparse flag was already
		 * set when the file was created.  But if the stream is named,
		 * then we need to set the sparse flag here. */
		if (unlikely(stream_is_named(strm))) {
			int ret = set_sparse_flag(h, ctx);
			if (ret) {
				NtClose(h);
				return ret;
			}
		}
		ctx->is_sparse_stream[ctx->num_open_handles] = true;
		ctx->any_sparse_streams = true;
	} else {
		/* Allocate space for the data.  */
		FILE_ALLOCATION_INFORMATION info =
			{ .AllocationSize = { .QuadPart = blob->size }};
		NtSetInformationFile(h, &ctx->iosb, &info, sizeof(info),
				     FileAllocationInformation);
	}
	ctx->open_handles[ctx->num_open_handles++] = h;
	return 0;
}

/* Given a Windows NT namespace path, such as \??\e:\Windows\System32, return a
 * pointer to the suffix of the path that begins with the device directly, such
 * as e:\Windows\System32.  */
static const wchar_t *
skip_nt_toplevel_component(const wchar_t *path, size_t path_nchars)
{
	static const wchar_t * const dirs[] = {
		L"\\??\\",
		L"\\DosDevices\\",
		L"\\Device\\",
	};
	const wchar_t * const end = path + path_nchars;

	for (size_t i = 0; i < ARRAY_LEN(dirs); i++) {
		size_t len = wcslen(dirs[i]);
		if (len <= (end - path) && !wmemcmp(path, dirs[i], len)) {
			path += len;
			while (path != end && *path == L'\\')
				path++;
			return path;
		}
	}
	return path;
}

/*
 * Given a Windows NT namespace path, such as \??\e:\Windows\System32, return a
 * pointer to the suffix of the path that is device-relative but possibly with
 * leading slashes, such as \Windows\System32.
 *
 * The path has an explicit length and is not necessarily null terminated.
 */
static const wchar_t *
get_device_relative_path(const wchar_t *path, size_t path_nchars)
{
	const wchar_t * const orig_path = path;
	const wchar_t * const end = path + path_nchars;

	path = skip_nt_toplevel_component(path, path_nchars);
	if (path == orig_path)
		return orig_path;

	while (path != end && *path != L'\\')
		path++;

	return path;
}

/*
 * Given a reparse point buffer for an inode for which the absolute link target
 * was relativized when it was archived, de-relative the link target to be
 * consistent with the actual extraction location.
 */
static void
try_rpfix(struct reparse_buffer_disk *rpbuf, u16 *rpbuflen_p,
	  struct win32_apply_ctx *ctx)
{
	struct link_reparse_point link;
	size_t orig_subst_name_nchars;
	const wchar_t *relpath;
	size_t relpath_nchars;
	size_t target_ntpath_nchars;
	size_t fixed_subst_name_nchars;
	const wchar_t *fixed_print_name;
	size_t fixed_print_name_nchars;

	/* Do nothing if the reparse data is invalid.  */
	if (parse_link_reparse_point(rpbuf, *rpbuflen_p, &link))
		return;

	/* Do nothing if the reparse point is a relative symbolic link.  */
	if (link_is_relative_symlink(&link))
		return;

	/* Build the new substitute name from the NT namespace path to the
	 * target directory, then a path separator, then the "device relative"
	 * part of the old substitute name.  */

	orig_subst_name_nchars = link.substitute_name_nbytes / sizeof(wchar_t);

	relpath = get_device_relative_path(link.substitute_name,
					   orig_subst_name_nchars);
	relpath_nchars = orig_subst_name_nchars -
			 (relpath - link.substitute_name);

	target_ntpath_nchars = ctx->target_ntpath.Length / sizeof(wchar_t);

	/* If the target directory is a filesystem root, such as \??\C:\, then
	 * it already will have a trailing slash.  Don't include this slash if
	 * we are already adding slashes via 'relpath'.  This prevents an extra
	 * slash from being generated each time the link is extracted.  And
	 * unlike on UNIX, the number of slashes in paths on Windows can be
	 * significant; Windows won't understand the link target if it contains
	 * too many slashes.  */
	if (target_ntpath_nchars > 0 && relpath_nchars > 0 &&
	    ctx->target_ntpath.Buffer[target_ntpath_nchars - 1] == L'\\')
		target_ntpath_nchars--;

	/* Also remove extra slashes from the beginning of 'relpath'.  Normally
	 * this isn't needed, but this is here to make the extra slash(es) added
	 * by wimlib pre-v1.9.1 get removed automatically.  */
	while (relpath_nchars >= 2 &&
	       relpath[0] == L'\\' && relpath[1] == L'\\') {
		relpath++;
		relpath_nchars--;
	}

	fixed_subst_name_nchars = target_ntpath_nchars + relpath_nchars;

	wchar_t* fixed_subst_name = MALLOC(fixed_subst_name_nchars * sizeof(wchar_t));

	wmemcpy(fixed_subst_name, ctx->target_ntpath.Buffer, target_ntpath_nchars);
	wmemcpy(&fixed_subst_name[target_ntpath_nchars], relpath, relpath_nchars);
	/* Doesn't need to be null-terminated.  */

	/* Print name should be Win32, but not all NT names can even be
	 * translated to Win32 names.  But we can at least delete the top-level
	 * directory, such as \??\, and this will have the expected result in
	 * the usual case.  */
	fixed_print_name = skip_nt_toplevel_component(fixed_subst_name,
						      fixed_subst_name_nchars);
	fixed_print_name_nchars = fixed_subst_name_nchars - (fixed_print_name -
							     fixed_subst_name);

	link.substitute_name = fixed_subst_name;
	link.substitute_name_nbytes = fixed_subst_name_nchars * sizeof(wchar_t);
	link.print_name = (wchar_t *)fixed_print_name;
	link.print_name_nbytes = fixed_print_name_nchars * sizeof(wchar_t);
	make_link_reparse_point(&link, rpbuf, rpbuflen_p);
	FREE(fixed_subst_name);
}

/* Sets the reparse point on the specified file.  This handles "fixing" the
 * targets of absolute symbolic links and junctions if WIMLIB_EXTRACT_FLAG_RPFIX
 * was specified.  */
static int
set_reparse_point(const struct wim_dentry *dentry,
		  const struct reparse_buffer_disk *rpbuf, u16 rpbuflen,
		  struct win32_apply_ctx *ctx)
{
	if ((ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_RPFIX)
	    && !(dentry->d_inode->i_rp_flags & WIM_RP_FLAG_NOT_FIXED))
	{
		memcpy(&ctx->rpfixbuf, rpbuf, rpbuflen);
		try_rpfix(&ctx->rpfixbuf, &rpbuflen, ctx);
		rpbuf = &ctx->rpfixbuf;
	}
	return do_set_reparse_point(dentry, rpbuf, rpbuflen, ctx);

}

/* Import the next block of raw encrypted data  */
static DWORD WINAPI
import_encrypted_data(PBYTE pbData, PVOID pvCallbackContext, PULONG Length)
{
	struct win32_apply_ctx *ctx = pvCallbackContext;
	ULONG copy_len;

	copy_len = min(ctx->encrypted_size - ctx->encrypted_offset, *Length);
	memcpy(pbData, &ctx->data_buffer[ctx->encrypted_offset], copy_len);
	ctx->encrypted_offset += copy_len;
	*Length = copy_len;
	return ERROR_SUCCESS;
}

/*
 * Write the raw encrypted data to the already-created file (or directory)
 * corresponding to @dentry.
 *
 * The raw encrypted data is provided in ctx->data_buffer, and its size is
 * ctx->encrypted_size.
 *
 * This function may close the target directory, in which case the caller needs
 * to re-open it if needed.
 */
static int
extract_encrypted_file(const struct wim_dentry *dentry,
		       struct win32_apply_ctx *ctx)
{
	void *rawctx;
	DWORD err;
	ULONG flags;
	bool retried;

	/* Temporarily build a Win32 path for OpenEncryptedFileRaw()  */
	build_win32_extraction_path(dentry, ctx);

	flags = CREATE_FOR_IMPORT | OVERWRITE_HIDDEN;
	if (dentry->d_inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY)
		flags |= CREATE_FOR_DIR;

	retried = false;
retry:
	err = OpenEncryptedFileRaw(ctx->pathbuf.Buffer, flags, &rawctx);
	if (err == ERROR_SHARING_VIOLATION && !retried) {
		/* This can be caused by the handle we have open to the target
		 * directory.  Try closing it temporarily.  */
		close_target_directory(ctx);
		retried = true;
		goto retry;
	}

	/* Restore the NT namespace path  */
	build_extraction_path(dentry, ctx);

	if (err != ERROR_SUCCESS) {
		win32_error(err, L"Can't open \"%ls\" for encrypted import",
			    current_path(ctx));
		return WIMLIB_ERR_OPEN;
	}

	ctx->encrypted_offset = 0;

	err = WriteEncryptedFileRaw(import_encrypted_data, ctx, rawctx);

	CloseEncryptedFileRaw(rawctx);

	if (err != ERROR_SUCCESS) {
		win32_error(err, L"Can't import encrypted file \"%ls\"",
			    current_path(ctx));
		return WIMLIB_ERR_WRITE;
	}

	return 0;
}

/* Called when starting to read a blob for extraction */
static int
win32_begin_extract_blob(struct blob_descriptor *blob, void *_ctx)
{
	struct win32_apply_ctx *ctx = _ctx;
	const struct blob_extraction_target *targets = blob_extraction_targets(blob);
	int ret;

	ctx->num_open_handles = 0;
	ctx->data_buffer_ptr = NULL;
	ctx->any_sparse_streams = false;
	INIT_LIST_HEAD(&ctx->reparse_dentries);
	INIT_LIST_HEAD(&ctx->encrypted_dentries);

	for (u32 i = 0; i < blob->out_refcnt; i++) {
		const struct wim_inode *inode = targets[i].inode;
		const struct wim_inode_stream *strm = targets[i].stream;
		struct wim_dentry *dentry;

		/* A copy of the blob needs to be extracted to @inode.  */

		if (ctx->common.supported_features.hard_links) {
			dentry = inode_first_extraction_dentry(inode);
			ret = begin_extract_blob_instance(blob, dentry, strm, ctx);
			ret = check_apply_error(dentry, ctx, ret);
			if (ret)
				goto fail;
		} else {
			/* Hard links not supported.  Extract the blob
			 * separately to each alias of the inode.  */
			inode_for_each_extraction_alias(dentry, inode) {
				ret = begin_extract_blob_instance(blob, dentry, strm, ctx);
				ret = check_apply_error(dentry, ctx, ret);
				if (ret)
					goto fail;
			}
		}
	}

	return 0;

fail:
	close_handles(ctx);
	return ret;
}

static int
pwrite_to_handle(HANDLE h, const void *data, size_t size, u64 offset)
{
	const uintptr_t end = (uintptr_t)data + size;
	uintptr_t p;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;

	for (p = (uintptr_t)data; p != end; p += iosb.Information,
				 offset += iosb.Information)
	{
		LARGE_INTEGER offs = { .QuadPart = offset };

		status = NtWriteFile(h, NULL, NULL, NULL, &iosb,
				     (void *)p, min(INT32_MAX, end - p),
				     &offs, NULL);
		if (!NT_SUCCESS(status)) {
			winnt_error(status,
				    L"Error writing data to target volume");
			return WIMLIB_ERR_WRITE;
		}
	}
	return 0;
}

/* Called when the next chunk of a blob has been read for extraction */
static int
win32_extract_chunk(const struct blob_descriptor *blob, u64 offset,
		    const void *chunk, size_t size, void *_ctx)
{
	struct win32_apply_ctx *ctx = _ctx;
	const uintptr_t end = (uintptr_t)chunk + size;
	uintptr_t p;
	bool zeroes;
	size_t len;
	unsigned i;
	int ret;

	/*
	 * For sparse streams, only write nonzero regions.  This lets the
	 * filesystem use holes to represent zero regions.
	 */
	for (p = (uintptr_t)chunk; p != end; p += len, offset += len) {
		zeroes = maybe_detect_sparse_region((const void*)p, end - p, &len,
						    ctx->any_sparse_streams);
		for (i = 0; i < ctx->num_open_handles; i++) {
			if (!zeroes || !ctx->is_sparse_stream[i]) {
				ret = pwrite_to_handle(ctx->open_handles[i],
						       (void*)p, len, offset);
				if (ret)
					return ret;
			}
		}
	}

	/* Copy the data chunk into the buffer (if needed)  */
	if (ctx->data_buffer_ptr)
		ctx->data_buffer_ptr = mempcpy(ctx->data_buffer_ptr,
					       chunk, size);
	return 0;
}

static int
get_system_compression_format(int extract_flags)
{
	if (extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS4K)
		return FILE_PROVIDER_COMPRESSION_XPRESS4K;

	if (extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS8K)
		return FILE_PROVIDER_COMPRESSION_XPRESS8K;

	if (extract_flags & WIMLIB_EXTRACT_FLAG_COMPACT_XPRESS16K)
		return FILE_PROVIDER_COMPRESSION_XPRESS16K;

	return FILE_PROVIDER_COMPRESSION_LZX;
}


static const wchar_t *
get_system_compression_format_string(int format)
{
	switch (format) {
	case FILE_PROVIDER_COMPRESSION_XPRESS4K:
		return L"XPRESS4K";
	case FILE_PROVIDER_COMPRESSION_XPRESS8K:
		return L"XPRESS8K";
	case FILE_PROVIDER_COMPRESSION_XPRESS16K:
		return L"XPRESS16K";
	default:
		return L"LZX";
	}
}

static NTSTATUS
set_system_compression(HANDLE h, int format)
{
	NTSTATUS status;
	struct {
		WOF_EXTERNAL_INFO wof_info;
		FILE_PROVIDER_EXTERNAL_INFO_V1 file_info;
	} in = {
		.wof_info = {
			.Version = WOF_CURRENT_VERSION,
			.Provider = WOF_PROVIDER_FILE,
		},
		.file_info = {
			.Version = FILE_PROVIDER_CURRENT_VERSION,
			.Algorithm = format,
		},
	};

	/* We intentionally use NtFsControlFile() rather than DeviceIoControl()
	 * here because the "compressing this object would not save space"
	 * status code does not map to a valid Win32 error code on older
	 * versions of Windows (before Windows 10?).  This can be a problem if
	 * the WOFADK driver is being used rather than the regular WOF, since
	 * WOFADK can be used on older versions of Windows.  */
	status = winnt_fsctl(h, FSCTL_SET_EXTERNAL_BACKING,
			     &in, sizeof(in), NULL, 0, NULL);

	if (status == 0xC000046F) /* "Compressing this object would not save space."  */
		return STATUS_SUCCESS;

	return status;
}

/* Hard-coded list of files which the Windows bootloader may need to access
 * before the WOF driver has been loaded.  */
static const wchar_t * const bootloader_pattern_strings[] = {
	L"*winload.*",
	L"*winresume.*",
	L"\\Windows\\AppPatch\\drvmain.sdb",
	L"\\Windows\\Boot\\DVD\\*",
	L"\\Windows\\Boot\\EFI\\*",
	L"\\Windows\\bootstat.dat",
	L"\\Windows\\Fonts\\vgaoem.fon",
	L"\\Windows\\Fonts\\vgasys.fon",
	L"\\Windows\\INF\\errata.inf",
	L"\\Windows\\System32\\config\\*",
	L"\\Windows\\System32\\ntkrnlpa.exe",
	L"\\Windows\\System32\\ntoskrnl.exe",
	L"\\Windows\\System32\\bootvid.dll",
	L"\\Windows\\System32\\ci.dll",
	L"\\Windows\\System32\\hal*.dll",
	L"\\Windows\\System32\\mcupdate_AuthenticAMD.dll",
	L"\\Windows\\System32\\mcupdate_GenuineIntel.dll",
	L"\\Windows\\System32\\pshed.dll",
	L"\\Windows\\System32\\apisetschema.dll",
	L"\\Windows\\System32\\api-ms-win*.dll",
	L"\\Windows\\System32\\ext-ms-win*.dll",
	L"\\Windows\\System32\\KernelBase.dll",
	L"\\Windows\\System32\\drivers\\*.sys",
	L"\\Windows\\System32\\*.nls",
	L"\\Windows\\System32\\kbd*.dll",
	L"\\Windows\\System32\\kd*.dll",
	L"\\Windows\\System32\\clfs.sys",
	L"\\Windows\\System32\\CodeIntegrity\\driver.stl",
};

static const struct string_list bootloader_patterns = {
	.strings = (wchar_t **)bootloader_pattern_strings,
	.num_strings = ARRAY_LEN(bootloader_pattern_strings),
};

/* Returns true if the specified system compression format is supported by the
 * bootloader of the image being applied.  */
static bool
bootloader_supports_compression_format(struct win32_apply_ctx *ctx, int format)
{
	/* Windows 10 and later support XPRESS4K */
	if (format == FILE_PROVIDER_COMPRESSION_XPRESS4K)
		return ctx->windows_build_number >= 10240;

	/*
	 * Windows 10 version 1903 and later support the other formats;
	 * see https://wimlib.net/forums/viewtopic.php?f=1&t=444
	 */
	return ctx->windows_build_number >= 18362;
}

static NTSTATUS
set_system_compression_on_inode(struct wim_inode *inode, int format,
				struct win32_apply_ctx *ctx)
{
	bool retried = false;
	NTSTATUS status;
	HANDLE h;

	/* If it may be needed for compatibility with the Windows bootloader,
	 * force this file to XPRESS4K or uncompressed format.  */
	if (!bootloader_supports_compression_format(ctx, format)) {
		/* We need to check the patterns against every name of the
		 * inode, in case any of them match.  */
		struct wim_dentry *dentry;
		inode_for_each_extraction_alias(dentry, inode) {
			bool incompatible;
			bool warned;

			if (calculate_dentry_full_path(dentry)) {
				ERROR("Unable to compute file path!");
				return STATUS_NO_MEMORY;
			}

			incompatible = match_pattern_list(dentry->d_full_path,
							  &bootloader_patterns,
							  MATCH_RECURSIVELY);
			FREE(dentry->d_full_path);
			dentry->d_full_path = NULL;

			if (!incompatible)
				continue;

			warned = (ctx->num_system_compression_exclusions++ > 0);

			if (bootloader_supports_compression_format(ctx,
				   FILE_PROVIDER_COMPRESSION_XPRESS4K))
			{
				/* Force to XPRESS4K  */
				if (!warned) {
					WARNING("For compatibility with the "
						"Windows bootloader, some "
						"files are being\n"
						"          compacted "
						"using the XPRESS4K format "
						"instead of the %"TS" format\n"
						"          you requested.",
						get_system_compression_format_string(format));
				}
				format = FILE_PROVIDER_COMPRESSION_XPRESS4K;
				break;
			} else {
				/* Force to uncompressed  */
				if (!warned) {
					WARNING("For compatibility with the "
						"Windows bootloader, some "
						"files will not\n"
						"          be compressed with"
						" system compression "
						"(\"compacted\").");
				}
				return STATUS_SUCCESS;
			}

		}
	}

	/* Open the extracted file.  */
	status = create_file(&h, GENERIC_READ | GENERIC_WRITE, NULL,
			     0, FILE_OPEN, 0,
			     inode_first_extraction_dentry(inode), ctx);

	if (!NT_SUCCESS(status))
		return status;
retry:
	/* Compress the file.  If the attempt fails with "invalid device
	 * request", then attach wof.sys (or wofadk.sys) and retry.  */
	status = set_system_compression(h, format);
	if (unlikely(status == STATUS_INVALID_DEVICE_REQUEST && !retried)) {
		wchar_t drive_path[7];
		if (!win32_get_drive_path(ctx->common.target, drive_path) &&
		    win32_try_to_attach_wof(drive_path + 4)) {
			retried = true;
			goto retry;
		}
	}

	NtClose(h);
	return status;
}

/*
 * This function is called when doing a "compact-mode" extraction and we just
 * finished extracting a blob to one or more locations.  For each location that
 * was the unnamed data stream of a file, this function compresses the
 * corresponding file using System Compression, if allowed.
 *
 * Note: we're doing the compression immediately after extracting the data
 * rather than during a separate compression pass.  This way should be faster
 * since the operating system should still have the file's data cached.
 *
 * Note: we're having the operating system do the compression, which is not
 * ideal because wimlib could create the compressed data faster and more
 * efficiently (the compressed data format is identical to a WIM resource).  But
 * we seemingly don't have a choice because WOF prevents applications from
 * creating its reparse points.
 */
static void
handle_system_compression(struct blob_descriptor *blob, struct win32_apply_ctx *ctx)
{
	const struct blob_extraction_target *targets = blob_extraction_targets(blob);

	const int format = get_system_compression_format(ctx->common.extract_flags);

	for (u32 i = 0; i < blob->out_refcnt; i++) {
		struct wim_inode *inode = targets[i].inode;
		struct wim_inode_stream *strm = targets[i].stream;
		NTSTATUS status;

		if (!stream_is_unnamed_data_stream(strm))
			continue;

		if (will_externally_back_inode(inode, ctx, NULL, false) != 0)
			continue;

		status = set_system_compression_on_inode(inode, format, ctx);
		if (likely(NT_SUCCESS(status)))
			continue;

		if (status == STATUS_INVALID_DEVICE_REQUEST) {
			WARNING(
	  "The request to compress the extracted files using System Compression\n"
"          will not be honored because the operating system or target volume\n"
"          does not support it.  System Compression is only supported on\n"
"          Windows 10 and later, and only on NTFS volumes.");
			ctx->common.extract_flags &= ~COMPACT_FLAGS;
			return;
		}

		ctx->num_system_compression_failures++;
		if (ctx->num_system_compression_failures < 10) {
			winnt_warning(status, L"\"%ls\": Failed to compress "
				      "extracted file using System Compression",
				      current_path(ctx));
		} else if (ctx->num_system_compression_failures == 10) {
			WARNING("Suppressing further warnings about "
				"System Compression failures.");
		}
	}
}

/* Called when a blob has been fully read for extraction */
static int
win32_end_extract_blob(struct blob_descriptor *blob, int status, void *_ctx)
{
	struct win32_apply_ctx *ctx = _ctx;
	int ret;
	const struct wim_dentry *dentry;

	/* Extend sparse streams to their final size. */
	if (ctx->any_sparse_streams && !status) {
		for (unsigned i = 0; i < ctx->num_open_handles; i++) {
			FILE_END_OF_FILE_INFORMATION info =
				{ .EndOfFile = { .QuadPart = blob->size } };
			NTSTATUS ntstatus;

			if (!ctx->is_sparse_stream[i])
				continue;

			ntstatus = NtSetInformationFile(ctx->open_handles[i],
							&ctx->iosb,
							&info, sizeof(info),
							FileEndOfFileInformation);
			if (!NT_SUCCESS(ntstatus)) {
				winnt_error(ntstatus, L"Error writing data to "
					    "target volume (while extending)");
				status = WIMLIB_ERR_WRITE;
				break;
			}
		}
	}

	close_handles(ctx);

	if (status)
		return status;

	if (unlikely(ctx->common.extract_flags & COMPACT_FLAGS))
		handle_system_compression(blob, ctx);

	if (likely(!ctx->data_buffer_ptr))
		return 0;

	if (!list_empty(&ctx->reparse_dentries)) {
		if (blob->size > REPARSE_DATA_MAX_SIZE) {
			dentry = list_first_entry(&ctx->reparse_dentries,
						  struct wim_dentry, d_tmp_list);
			build_extraction_path(dentry, ctx);
			ERROR("Reparse data of \"%ls\" has size "
			      "%"PRIu64" bytes (exceeds %u bytes)",
			      current_path(ctx), blob->size,
			      REPARSE_DATA_MAX_SIZE);
			ret = WIMLIB_ERR_INVALID_REPARSE_DATA;
			return check_apply_error(dentry, ctx, ret);
		}
		/* Reparse data  */
		memcpy(ctx->rpbuf.rpdata, ctx->data_buffer, blob->size);

		list_for_each_entry(dentry, &ctx->reparse_dentries, d_tmp_list) {

			/* Reparse point header  */
			complete_reparse_point(&ctx->rpbuf, dentry->d_inode,
					       blob->size);

			ret = set_reparse_point(dentry, &ctx->rpbuf,
						REPARSE_DATA_OFFSET + blob->size,
						ctx);
			ret = check_apply_error(dentry, ctx, ret);
			if (ret)
				return ret;
		}
	}

	if (!list_empty(&ctx->encrypted_dentries)) {
		ctx->encrypted_size = blob->size;
		list_for_each_entry(dentry, &ctx->encrypted_dentries, d_tmp_list) {
			ret = extract_encrypted_file(dentry, ctx);
			ret = check_apply_error(dentry, ctx, ret);
			if (ret)
				return ret;
			/* Re-open the target directory if needed.  */
			ret = open_target_directory(ctx);
			if (ret)
				return ret;
		}
	}

	return 0;
}

/* Attributes that can't be set directly  */
#define SPECIAL_ATTRIBUTES			\
	(FILE_ATTRIBUTE_REPARSE_POINT	|	\
	 FILE_ATTRIBUTE_DIRECTORY	|	\
	 FILE_ATTRIBUTE_ENCRYPTED	|	\
	 FILE_ATTRIBUTE_SPARSE_FILE	|	\
	 FILE_ATTRIBUTE_COMPRESSED)

static void
set_object_id(HANDLE h, const struct wim_inode *inode,
	      struct win32_apply_ctx *ctx)
{
	const void *object_id;
	u32 len;
	NTSTATUS status;

	if (!ctx->common.supported_features.object_ids)
		return;

	object_id = inode_get_object_id(inode, &len);
	if (likely(object_id == NULL))  /* No object ID?  */
		return;

	status = winnt_fsctl(h, FSCTL_SET_OBJECT_ID,
			     object_id, len, NULL, 0, NULL);
	if (NT_SUCCESS(status))
		return;

	/* Object IDs must be unique within the filesystem.  A duplicate might
	 * occur if an image containing object IDs is applied twice to the same
	 * filesystem.  Arguably, the user should be warned in this case; but
	 * the reality seems to be that nothing important cares about object IDs
	 * except the Distributed Link Tracking Service... so for now these
	 * failures are just ignored.  */
	if (status == STATUS_DUPLICATE_NAME ||
	    status == STATUS_OBJECT_NAME_COLLISION)
		return;

	ctx->num_object_id_failures++;
	if (ctx->num_object_id_failures < 10) {
		winnt_warning(status, L"Can't set object ID on \"%ls\"",
			      current_path(ctx));
	} else if (ctx->num_object_id_failures == 10) {
		WARNING("Suppressing further warnings about failure to set "
			"object IDs.");
	}
}

static int
set_xattrs(HANDLE h, const struct wim_inode *inode, struct win32_apply_ctx *ctx)
{
	uintptr_t entries, entries_end;
	u32 len;
	const struct wim_xattr_entry *entry;
	size_t bufsize = 0;
	PRAGMA_ALIGN(u8 _buf[1024], 4);
	u8 *buf = _buf;
	FILE_FULL_EA_INFORMATION *ea, *ea_prev;
	NTSTATUS status;
	int ret;

	if (!ctx->common.supported_features.xattrs)
		return 0;

	entries = (uintptr_t)inode_get_xattrs(inode, &len);
	if (likely(entries == 0 || len == 0))  /* No extended attributes? */
		return 0;
	entries_end = entries + len;

	for (entry = (const struct wim_xattr_entry*)entries; (uintptr_t)entry < entries_end;
	     entry = xattr_entry_next(entry)) {
		if (!valid_xattr_entry(entry, (size_t)(entries_end - (uintptr_t)entry))) {
			ERROR("\"%"TS"\": extended attribute is corrupt or unsupported",
			      inode_any_full_path(inode));
			return WIMLIB_ERR_INVALID_XATTR;
		}

		bufsize += ALIGN(offsetof(FILE_FULL_EA_INFORMATION, EaName) +
				 entry->name_len + 1 +
				 le16_to_cpu(entry->value_len), 4);
	}

	if (unlikely(bufsize != (u32)bufsize)) {
		ERROR("\"%"TS"\": too many extended attributes to extract!",
		      inode_any_full_path(inode));
		return WIMLIB_ERR_INVALID_XATTR;
	}

	if (unlikely(bufsize > sizeof(_buf))) {
		buf = MALLOC(bufsize);
		if (!buf)
			return WIMLIB_ERR_NOMEM;
	}

	ea_prev = NULL;
	ea = (FILE_FULL_EA_INFORMATION *)buf;
	for (entry = (const struct wim_xattr_entry*)entries; (uintptr_t)entry < entries_end;
	     entry = xattr_entry_next(entry)) {
		u8 *p;

		if (ea_prev)
			ea_prev->NextEntryOffset = (u8 *)ea - (u8 *)ea_prev;
		ea->Flags = entry->flags;
		ea->EaNameLength = entry->name_len;
		ea->EaValueLength = le16_to_cpu(entry->value_len);
		p = mempcpy(ea->EaName, entry->name,
			    ea->EaNameLength + 1 + ea->EaValueLength);
		while ((uintptr_t)p & 3)
			*p++ = 0;
		ea_prev = ea;
		ea = (FILE_FULL_EA_INFORMATION *)p;
	}
	if (ea_prev == NULL) {
		ret = WIMLIB_ERR_INVALID_PARAM;
		goto out;
	}
	ea_prev->NextEntryOffset = 0;
	wimlib_assert((u8 *)ea - buf == bufsize);

	status = NtSetEaFile(h, &ctx->iosb, buf, bufsize);
	if (unlikely(!NT_SUCCESS(status))) {
		if (status == STATUS_EAS_NOT_SUPPORTED) {
			/* This happens with Samba. */
			WARNING("Filesystem advertised extended attribute (EA) support, but it doesn't\n"
				"          work.  EAs will not be extracted.");
			ctx->common.supported_features.xattrs = 0;
		} else if (status == STATUS_INVALID_EA_NAME) {
			ctx->num_xattr_failures++;
			if (ctx->num_xattr_failures < 5) {
				winnt_warning(status,
					      L"Can't set extended attributes on \"%ls\"",
					      current_path(ctx));
			} else if (ctx->num_xattr_failures == 5) {
				WARNING("Suppressing further warnings about "
					"failure to set extended attributes.");
			}
		} else {
			winnt_error(status, L"Can't set extended attributes on \"%ls\"",
				    current_path(ctx));
			ret = WIMLIB_ERR_SET_XATTR;
			goto out;
		}
	}
	ret = 0;
out:
	if (buf != _buf)
		FREE(buf);
	return ret;
}

/* Set the security descriptor @desc, of @desc_size bytes, on the file with open
 * handle @h.  */
static NTSTATUS
set_security_descriptor(HANDLE h, const void *_desc,
			size_t desc_size, struct win32_apply_ctx *ctx)
{
	SECURITY_INFORMATION info;
	NTSTATUS status;
	SECURITY_DESCRIPTOR_RELATIVE *desc;

	/*
	 * Ideally, we would just pass in the security descriptor buffer as-is.
	 * But it turns out that Windows can mess up the security descriptor
	 * even when using the low-level NtSetSecurityObject() function:
	 *
	 * - Windows will clear SE_DACL_AUTO_INHERITED if it is set in the
	 *   passed buffer.  To actually get Windows to set
	 *   SE_DACL_AUTO_INHERITED, the application must set the non-persistent
	 *   flag SE_DACL_AUTO_INHERIT_REQ.  As usual, Microsoft didn't bother
	 *   to properly document either of these flags.  It's unclear how
	 *   important SE_DACL_AUTO_INHERITED actually is, but to be safe we use
	 *   the SE_DACL_AUTO_INHERIT_REQ workaround to set it if needed.
	 *
	 * - The above also applies to the equivalent SACL flags,
	 *   SE_SACL_AUTO_INHERITED and SE_SACL_AUTO_INHERIT_REQ.
	 *
	 * - If the application says that it's setting
	 *   DACL_SECURITY_INFORMATION, then Windows sets SE_DACL_PRESENT in the
	 *   resulting security descriptor, even if the security descriptor the
	 *   application provided did not have a DACL.  This seems to be
	 *   unavoidable, since omitting DACL_SECURITY_INFORMATION would cause a
	 *   default DACL to remain.  Fortunately, this behavior seems harmless,
	 *   since the resulting DACL will still be "null" --- but it will be
	 *   "the other representation of null".
	 *
	 * - The above also applies to SACL_SECURITY_INFORMATION and
	 *   SE_SACL_PRESENT.  Again, it's seemingly unavoidable but "harmless"
	 *   that Windows changes the representation of a "null SACL".
	 */
	if (likely(desc_size <= STACK_MAX)) {
		desc = alloca(desc_size);
	} else {
		desc = MALLOC(desc_size);
		if (!desc)
			return STATUS_NO_MEMORY;
	}

	memcpy(desc, _desc, desc_size);

	if (likely(desc_size >= 4)) {

		if (desc->Control & SE_DACL_AUTO_INHERITED)
			desc->Control |= SE_DACL_AUTO_INHERIT_REQ;

		if (desc->Control & SE_SACL_AUTO_INHERITED)
			desc->Control |= SE_SACL_AUTO_INHERIT_REQ;
	}

	/*
	 * More API insanity.  We want to set the entire security descriptor
	 * as-is.  But all available APIs require specifying the specific parts
	 * of the security descriptor being set.  Especially annoying is that
	 * mandatory integrity labels are part of the SACL, but they aren't set
	 * with SACL_SECURITY_INFORMATION.  Instead, applications must also
	 * specify LABEL_SECURITY_INFORMATION (Windows Vista, Windows 7) or
	 * BACKUP_SECURITY_INFORMATION (Windows 8).  But at least older versions
	 * of Windows don't error out if you provide these newer flags...
	 *
	 * Also, if the process isn't running as Administrator, then it probably
	 * doesn't have SE_RESTORE_PRIVILEGE.  In this case, it will always get
	 * the STATUS_PRIVILEGE_NOT_HELD error by trying to set the SACL, even
	 * if the security descriptor it provided did not have a SACL.  By
	 * default, in this case we try to recover and set as much of the
	 * security descriptor as possible --- potentially excluding the DACL, and
	 * even the owner, as well as the SACL.
	 */

	info = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
	       DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION |
	       LABEL_SECURITY_INFORMATION | BACKUP_SECURITY_INFORMATION;


	/*
	 * It's also worth noting that SetFileSecurity() is unusable because it
	 * doesn't request "backup semantics" when it opens the file internally.
	 * NtSetSecurityObject() seems to be the best function to use in backup
	 * applications.  (SetSecurityInfo() should also work, but it's harder
	 * to use and must call NtSetSecurityObject() internally anyway.
	 * BackupWrite() is theoretically usable as well, but it's inflexible
	 * and poorly documented.)
	 */

retry:
	status = NtSetSecurityObject(h, info, desc);
	if (NT_SUCCESS(status))
		goto out_maybe_free_desc;

	/* Failed to set the requested parts of the security descriptor.  If the
	 * error was permissions-related, try to set fewer parts of the security
	 * descriptor, unless WIMLIB_EXTRACT_FLAG_STRICT_ACLS is enabled.  */
	if ((status == STATUS_PRIVILEGE_NOT_HELD ||
	     status == STATUS_ACCESS_DENIED) &&
	    !(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_ACLS))
	{
		if (info & SACL_SECURITY_INFORMATION) {
			info &= ~(SACL_SECURITY_INFORMATION |
				  LABEL_SECURITY_INFORMATION |
				  BACKUP_SECURITY_INFORMATION);
			ctx->partial_security_descriptors++;
			goto retry;
		}
		if (info & DACL_SECURITY_INFORMATION) {
			info &= ~DACL_SECURITY_INFORMATION;
			goto retry;
		}
		if (info & OWNER_SECURITY_INFORMATION) {
			info &= ~OWNER_SECURITY_INFORMATION;
			goto retry;
		}
		/* Nothing left except GROUP, and if we removed it we
		 * wouldn't have anything at all.  */
	}

	/* No part of the security descriptor could be set, or
	 * WIMLIB_EXTRACT_FLAG_STRICT_ACLS is enabled and the full security
	 * descriptor could not be set.  */
	if (!(info & SACL_SECURITY_INFORMATION))
		ctx->partial_security_descriptors--;
	ctx->no_security_descriptors++;

out_maybe_free_desc:
	if (unlikely(desc_size > STACK_MAX))
		FREE(desc);
	return status;
}

/* Set metadata on the open file @h from the WIM inode @inode.  */
static int
do_apply_metadata_to_file(HANDLE h, const struct wim_inode *inode,
			  struct win32_apply_ctx *ctx)
{
	FILE_BASIC_INFORMATION info;
	NTSTATUS status;
	int ret;

	/* Set the file's object ID if present and object IDs are supported by
	 * the filesystem.  */
	set_object_id(h, inode, ctx);

	/* Set the file's extended attributes (EAs) if present and EAs are
	 * supported by the filesystem.  */
	ret = set_xattrs(h, inode, ctx);
	if (ret)
		return ret;

	/* Set the file's security descriptor if present and we're not in
	 * NO_ACLS mode  */
	if (inode_has_security_descriptor(inode) &&
	    !(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_NO_ACLS))
	{
		const struct wim_security_data *sd;
		const void *desc;
		size_t desc_size;

		sd = wim_get_current_security_data(ctx->common.wim);
		desc = sd->descriptors[inode->i_security_id];
		desc_size = sd->sizes[inode->i_security_id];

		status = set_security_descriptor(h, desc, desc_size, ctx);
		if (!NT_SUCCESS(status) &&
		    (ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_STRICT_ACLS))
		{
			winnt_error(status,
				    L"Can't set security descriptor on \"%ls\"",
				    current_path(ctx));
			return WIMLIB_ERR_SET_SECURITY;
		}
	}

	/* Set attributes and timestamps  */
	info.CreationTime.QuadPart = inode->i_creation_time;
	info.LastAccessTime.QuadPart = inode->i_last_access_time;
	info.LastWriteTime.QuadPart = inode->i_last_write_time;
	info.ChangeTime.QuadPart = 0;
	if (ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_NO_ATTRIBUTES) {
		info.FileAttributes = FILE_ATTRIBUTE_NORMAL;
	} else {
		info.FileAttributes = inode->i_attributes & ~SPECIAL_ATTRIBUTES;
		if (info.FileAttributes == 0)
			info.FileAttributes = FILE_ATTRIBUTE_NORMAL;
	}

	status = NtSetInformationFile(h, &ctx->iosb, &info, sizeof(info),
				      FileBasicInformation);
	/* On FAT volumes we get STATUS_INVALID_PARAMETER if we try to set
	 * attributes on the root directory.  (Apparently because FAT doesn't
	 * actually have a place to store those attributes!)  */
	if (!NT_SUCCESS(status)
	    && !(status == STATUS_INVALID_PARAMETER &&
		 dentry_is_root(inode_first_extraction_dentry(inode))))
	{
		winnt_error(status, L"Can't set basic metadata on \"%ls\"",
			    current_path(ctx));
		return WIMLIB_ERR_SET_ATTRIBUTES;
	}

	return 0;
}

static int
apply_metadata_to_file(const struct wim_dentry *dentry,
		       struct win32_apply_ctx *ctx)
{
	const struct wim_inode *inode = dentry->d_inode;
	DWORD perms;
	HANDLE h;
	NTSTATUS status;
	int ret;

	perms = FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA | WRITE_DAC |
		WRITE_OWNER | ACCESS_SYSTEM_SECURITY;

	build_extraction_path(dentry, ctx);

	/* Open a handle with as many relevant permissions as possible.  */
	while (!NT_SUCCESS(status = do_create_file(&h, perms, NULL,
						   0, FILE_OPEN, 0, ctx)))
	{
		if (status == STATUS_PRIVILEGE_NOT_HELD ||
		    status == STATUS_ACCESS_DENIED)
		{
			if (perms & ACCESS_SYSTEM_SECURITY) {
				perms &= ~ACCESS_SYSTEM_SECURITY;
				continue;
			}
			if (perms & WRITE_DAC) {
				perms &= ~WRITE_DAC;
				continue;
			}
			if (perms & WRITE_OWNER) {
				perms &= ~WRITE_OWNER;
				continue;
			}
		}
		winnt_error(status, L"Can't open \"%ls\" to set metadata",
			    current_path(ctx));
		return WIMLIB_ERR_OPEN;
	}

	ret = do_apply_metadata_to_file(h, inode, ctx);

	NtClose(h);

	return ret;
}

static int
apply_metadata(struct list_head *dentry_list, struct win32_apply_ctx *ctx)
{
	const struct wim_dentry *dentry;
	int ret;

	/* We go in reverse so that metadata is set on all a directory's
	 * children before the directory itself.  This avoids any potential
	 * problems with attributes, timestamps, or security descriptors.  */
	list_for_each_entry_reverse(dentry, dentry_list, d_extraction_list_node)
	{
		ret = apply_metadata_to_file(dentry, ctx);
		ret = check_apply_error(dentry, ctx, ret);
		if (ret)
			return ret;
		ret = report_file_metadata_applied(&ctx->common);
		if (ret)
			return ret;
	}
	return 0;
}

/* Issue warnings about problems during the extraction for which warnings were
 * not already issued (due to the high number of potential warnings if we issued
 * them per-file).  */
static void
do_warnings(const struct win32_apply_ctx *ctx)
{
	if (ctx->partial_security_descriptors == 0
	    && ctx->no_security_descriptors == 0
	    && ctx->num_set_short_name_failures == 0
	#if 0
	    && ctx->num_remove_short_name_failures == 0
	#endif
	    )
		return;

	WARNING("Extraction to \"%ls\" complete, but with one or more warnings:",
		ctx->common.target);
	if (ctx->num_set_short_name_failures) {
		WARNING("- Could not set short names on %lu files or directories",
			ctx->num_set_short_name_failures);
	}
#if 0
	if (ctx->num_remove_short_name_failures) {
		WARNING("- Could not remove short names on %lu files or directories"
			"          (This is expected on Vista and earlier)",
			ctx->num_remove_short_name_failures);
	}
#endif
	if (ctx->partial_security_descriptors) {
		WARNING("- Could only partially set the security descriptor\n"
			"            on %lu files or directories.",
			ctx->partial_security_descriptors);
	}
	if (ctx->no_security_descriptors) {
		WARNING("- Could not set security descriptor at all\n"
			"            on %lu files or directories.",
			ctx->no_security_descriptors);
	}
	if (ctx->partial_security_descriptors || ctx->no_security_descriptors) {
		WARNING("To fully restore all security descriptors, run the program\n"
			"          with Administrator rights.");
	}
}

static u64
count_dentries(const struct list_head *dentry_list)
{
	const struct list_head *cur;
	u64 count = 0;

	list_for_each(cur, dentry_list)
		count++;

	return count;
}

/* Extract files from a WIM image to a directory on Windows  */
static int
win32_extract(struct list_head *dentry_list, struct apply_ctx *_ctx)
{
	int ret;
	struct win32_apply_ctx *ctx = (struct win32_apply_ctx *)_ctx;
	u64 dentry_count;

	ret = prepare_target(dentry_list, ctx);
	if (ret)
		goto out;

	if (unlikely(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT)) {
		ret = start_wimboot_extraction(dentry_list, ctx);
		if (ret)
			goto out;
	}

	ctx->windows_build_number = xml_get_windows_build_number(ctx->common.wim->xml_info,
								 ctx->common.wim->current_image);

	dentry_count = count_dentries(dentry_list);

	ret = start_file_structure_phase(&ctx->common, dentry_count);
	if (ret)
		goto out;

	ret = create_directories(dentry_list, ctx);
	if (ret)
		goto out;

	ret = create_nondirectories(dentry_list, ctx);
	if (ret)
		goto out;

	ret = end_file_structure_phase(&ctx->common);
	if (ret)
		goto out;

	struct read_blob_callbacks cbs = {
		.begin_blob	= win32_begin_extract_blob,
		.continue_blob	= win32_extract_chunk,
		.end_blob	= win32_end_extract_blob,
		.ctx		= ctx,
	};
	ret = extract_blob_list(&ctx->common, &cbs);
	if (ret)
		goto out;

	ret = start_file_metadata_phase(&ctx->common, dentry_count);
	if (ret)
		goto out;

	ret = apply_metadata(dentry_list, ctx);
	if (ret)
		goto out;

	ret = end_file_metadata_phase(&ctx->common);
	if (ret)
		goto out;

	if (unlikely(ctx->common.extract_flags & WIMLIB_EXTRACT_FLAG_WIMBOOT)) {
		ret = end_wimboot_extraction(ctx);
		if (ret)
			goto out;
	}

	do_warnings(ctx);
out:
	close_target_directory(ctx);
	if (ctx->target_ntpath.Buffer)
		HeapFree(GetProcessHeap(), 0, ctx->target_ntpath.Buffer);
	FREE(ctx->pathbuf.Buffer);
	FREE(ctx->print_buffer);
	FREE(ctx->wimboot.wims);
	if (ctx->prepopulate_pats) {
		FREE(ctx->prepopulate_pats->strings);
		FREE(ctx->prepopulate_pats);
	}
	FREE(ctx->mem_prepopulate_pats);
	FREE(ctx->data_buffer);
	return ret;
}

const struct apply_operations win32_apply_ops = {
	.name			= "Windows",
	.get_supported_features = win32_get_supported_features,
	.extract                = win32_extract,
	.will_back_from_wim     = win32_will_back_from_wim,
	.context_size           = sizeof(struct win32_apply_ctx),
};

#endif /* _WIN32 */
