/*
 * win32_capture.c - Windows-specific code for capturing files into a WIM image.
 *
 * This now uses the native Windows NT API a lot and not just Win32.
 */

/*
 * Copyright (C) 2013-2021 Eric Biggers
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

#include "wimlib/assert.h"
#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/endianness.h"
#include "wimlib/error.h"
#include "wimlib/object_id.h"
#include "wimlib/paths.h"
#include "wimlib/reparse.h"
#include "wimlib/scan.h"
#include "wimlib/win32_vss.h"
#include "wimlib/wof.h"
#include "wimlib/xattr.h"

struct winnt_scan_ctx {
	struct scan_params *params;
	bool is_ntfs;
	u32 vol_flags;
	unsigned long num_get_sd_access_denied;
	unsigned long num_get_sacl_priv_notheld;

	/* True if WOF is definitely not attached to the volume being scanned;
	 * false if it may be  */
	bool wof_not_attached;

	/* A reference to the VSS snapshot being used, or NULL if none  */
	struct vss_snapshot *snapshot;
};

static inline const wchar_t *
printable_path(const struct winnt_scan_ctx *ctx)
{
	/* Skip over \\?\ or \??\  */
	return ctx->params->cur_path + 4;
}

/* Description of where data is located on a Windows filesystem  */
struct windows_file {

	/* Is the data the raw encrypted data of an EFS-encrypted file?  */
	u64 is_encrypted : 1;

	/* Is this file "open by file ID" rather than the regular "open by
	 * path"?  "Open by file ID" uses resources more efficiently.  */
	u64 is_file_id : 1;

	/* The file's LCN (logical cluster number) for sorting, or 0 if unknown.
	 */
	u64 sort_key : 62;

	/* Length of the path in bytes, excluding the null terminator if
	 * present.  */
	size_t path_nbytes;

	/* A reference to the VSS snapshot containing the file, or NULL if none.
	 */
	struct vss_snapshot *snapshot;

	/* The path to the file.  If 'is_encrypted=0' this is an NT namespace
	 * path; if 'is_encrypted=1' this is a Win32 namespace path.  If
	 * 'is_file_id=0', then the path is null-terminated.  If 'is_file_id=1'
	 * (only allowed with 'is_encrypted=0') the path ends with a binary file
	 * ID and may not be null-terminated.  */
	wchar_t path[0];
};

/* Allocate a structure to describe the location of a data stream by path.  */
static struct windows_file *
alloc_windows_file(const wchar_t *path, size_t path_nchars,
		   const wchar_t *stream_name, size_t stream_name_nchars,
		   struct vss_snapshot *snapshot, bool is_encrypted)
{
	size_t full_path_nbytes;
	struct windows_file *file;
	wchar_t *p;

	full_path_nbytes = path_nchars * sizeof(wchar_t);
	if (stream_name_nchars)
		full_path_nbytes += (1 + stream_name_nchars) * sizeof(wchar_t);

	file = MALLOC(sizeof(struct windows_file) + full_path_nbytes +
		      sizeof(wchar_t));
	if (!file)
		return NULL;

	file->is_encrypted = is_encrypted;
	file->is_file_id = 0;
	file->sort_key = 0;
	file->path_nbytes = full_path_nbytes;
	file->snapshot = vss_get_snapshot(snapshot);
	p = wmempcpy(file->path, path, path_nchars);
	if (stream_name_nchars) {
		/* Named data stream  */
		*p++ = L':';
		p = wmempcpy(p, stream_name, stream_name_nchars);
	}
	*p = L'\0';
	return file;
}

/* Allocate a structure to describe the location of a file by ID.  */
static struct windows_file *
alloc_windows_file_for_file_id(u64 file_id, const wchar_t *root_path,
			       size_t root_path_nchars,
			       struct vss_snapshot *snapshot)
{
	size_t full_path_nbytes;
	struct windows_file *file;
	wchar_t *p;

	full_path_nbytes = (root_path_nchars * sizeof(wchar_t)) +
			   sizeof(file_id);
	file = MALLOC(sizeof(struct windows_file) + full_path_nbytes +
		      sizeof(wchar_t));
	if (!file)
		return NULL;

	file->is_encrypted = 0;
	file->is_file_id = 1;
	file->sort_key = 0;
	file->path_nbytes = full_path_nbytes;
	file->snapshot = vss_get_snapshot(snapshot);
	p = wmempcpy(file->path, root_path, root_path_nchars);
	p = mempcpy(p, &file_id, sizeof(file_id));
	*p = L'\0';
	return file;
}

/* Add a stream, located on a Windows filesystem, to the specified WIM inode. */
static int
add_stream(struct wim_inode *inode, struct windows_file *windows_file,
	   u64 stream_size, int stream_type, const utf16lechar *stream_name,
	   struct list_head *unhashed_blobs)
{
	struct blob_descriptor *blob = NULL;
	struct wim_inode_stream *strm;
	int ret;

	if (!windows_file)
		goto err_nomem;

	/* If the stream is nonempty, create a blob descriptor for it.  */
	if (stream_size) {
		blob = new_blob_descriptor();
		if (!blob)
			goto err_nomem;
		blob->windows_file = windows_file;
		blob->blob_location = BLOB_IN_WINDOWS_FILE;
		blob->file_inode = inode;
		blob->size = stream_size;
		windows_file = NULL;
	}

	strm = inode_add_stream(inode, stream_type, stream_name, blob);
	if (!strm)
		goto err_nomem;

	prepare_unhashed_blob(blob, inode, strm->stream_id, unhashed_blobs);
	ret = 0;
out:
	if (windows_file)
		free_windows_file(windows_file);
	return ret;

err_nomem:
	free_blob_descriptor(blob);
	ret = WIMLIB_ERR_NOMEM;
	goto out;
}

struct windows_file *
clone_windows_file(const struct windows_file *file)
{
	struct windows_file *new;

	new = memdup(file, sizeof(*file) + file->path_nbytes + sizeof(wchar_t));
	if (new)
		vss_get_snapshot(new->snapshot);
	return new;
}

void
free_windows_file(struct windows_file *file)
{
	vss_put_snapshot(file->snapshot);
	FREE(file);
}

int
cmp_windows_files(const struct windows_file *file1,
		  const struct windows_file *file2)
{
	/* Compare by starting LCN (logical cluster number)  */
	int v = cmp_u64(file1->sort_key, file2->sort_key);
	if (v)
		return v;

	/* Fall back to comparing files by path (arbitrary heuristic).  */
	v = memcmp(file1->path, file2->path,
		   min(file1->path_nbytes, file2->path_nbytes));
	if (v)
		return v;

	return cmp_u32(file1->path_nbytes, file2->path_nbytes);
}

const wchar_t *
get_windows_file_path(const struct windows_file *file)
{
	return file->path;
}

/*
 * Open the file named by the NT namespace path @path of length @path_nchars
 * characters.  If @cur_dir is not NULL then the path is given relative to
 * @cur_dir; otherwise the path is absolute.  @perms is the access mask of
 * permissions to request on the handle.  SYNCHRONIZE permision is always added.
 */
static NTSTATUS
winnt_openat(HANDLE cur_dir, const wchar_t *path, size_t path_nchars,
	     ACCESS_MASK perms, HANDLE *h_ret)
{
	UNICODE_STRING name = {
		.Length = path_nchars * sizeof(wchar_t),
		.MaximumLength = path_nchars * sizeof(wchar_t),
		.Buffer = (wchar_t *)path,
	};
	OBJECT_ATTRIBUTES attr = {
		.Length = sizeof(attr),
		.RootDirectory = cur_dir,
		.ObjectName = &name,
	};
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	ULONG options = FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT;

	perms |= SYNCHRONIZE;
	if (perms & (FILE_READ_DATA | FILE_LIST_DIRECTORY)) {
		options |= FILE_SYNCHRONOUS_IO_NONALERT;
		options |= FILE_SEQUENTIAL_ONLY;
	}
retry:
	status = NtOpenFile(h_ret, perms, &attr, &iosb,
			    FILE_SHARE_VALID_FLAGS, options);
	if (!NT_SUCCESS(status)) {
		/* Try requesting fewer permissions  */
		if (status == STATUS_ACCESS_DENIED ||
		    status == STATUS_PRIVILEGE_NOT_HELD) {
			if (perms & ACCESS_SYSTEM_SECURITY) {
				perms &= ~ACCESS_SYSTEM_SECURITY;
				goto retry;
			}
			if (perms & READ_CONTROL) {
				perms &= ~READ_CONTROL;
				goto retry;
			}
		}
	}
	return status;
}

static NTSTATUS
winnt_open(const wchar_t *path, size_t path_nchars, ACCESS_MASK perms,
	   HANDLE *h_ret)
{
	return winnt_openat(NULL, path, path_nchars, perms, h_ret);
}

static const wchar_t *
windows_file_to_string(const struct windows_file *file, u8 *buf, size_t bufsize)
{
	if (file->is_file_id) {
		u64 file_id;
		memcpy(&file_id,
		       (u8 *)file->path + file->path_nbytes - sizeof(file_id),
		       sizeof(file_id));
		swprintf((wchar_t *)buf, bufsize, L"NTFS inode 0x%016"PRIx64, file_id);
	} else if (file->path_nbytes + 3 * sizeof(wchar_t) <= bufsize) {
		swprintf((wchar_t *)buf, bufsize, L"\"%ls\"", file->path);
	} else {
		return L"(name too long)";
	}
	return (wchar_t *)buf;
}

static int
read_winnt_stream_prefix(const struct windows_file *file,
			 u64 size, const struct consume_chunk_callback *cb)
{
	IO_STATUS_BLOCK iosb;
	UNICODE_STRING name = {
		.Buffer = (wchar_t *)file->path,
		.Length = file->path_nbytes,
		.MaximumLength = file->path_nbytes,
	};
	OBJECT_ATTRIBUTES attr = {
		.Length = sizeof(attr),
		.ObjectName = &name,
	};
	HANDLE h;
	NTSTATUS status;
	PRAGMA_ALIGN(u8 buf[BUFFER_SIZE], 8);
	u64 bytes_remaining;
	int ret;

	status = NtOpenFile(&h, FILE_READ_DATA | SYNCHRONIZE,
			    &attr, &iosb,
			    FILE_SHARE_VALID_FLAGS,
			    FILE_OPEN_REPARSE_POINT |
				FILE_OPEN_FOR_BACKUP_INTENT |
				FILE_SYNCHRONOUS_IO_NONALERT |
				FILE_SEQUENTIAL_ONLY |
				(file->is_file_id ? FILE_OPEN_BY_FILE_ID : 0));
	if (unlikely(!NT_SUCCESS(status))) {
		if (status == STATUS_SHARING_VIOLATION) {
			ERROR("Can't open %ls for reading:\n"
			      "        File is in use by another process! "
			      "Consider using snapshot (VSS) mode.",
			      windows_file_to_string(file, buf, sizeof(buf)));
		} else {
			winnt_error(status, L"Can't open %ls for reading",
				    windows_file_to_string(file, buf, sizeof(buf)));
		}
		return WIMLIB_ERR_OPEN;
	}

	ret = 0;
	bytes_remaining = size;
	while (bytes_remaining) {
		IO_STATUS_BLOCK iosb;
		ULONG count;
		ULONG bytes_read;
		const unsigned max_tries = 5;
		unsigned tries_remaining = max_tries;

		count = min(sizeof(buf), bytes_remaining);

	retry_read:
		status = NtReadFile(h, NULL, NULL, NULL,
				    &iosb, buf, count, NULL, NULL);
		if (unlikely(!NT_SUCCESS(status))) {
			if (status == STATUS_END_OF_FILE) {
				ERROR("%ls: File was concurrently truncated",
				      windows_file_to_string(file, buf, sizeof(buf)));
				ret = WIMLIB_ERR_CONCURRENT_MODIFICATION_DETECTED;
			} else {
				winnt_warning(status, L"Error reading data from %ls",
					      windows_file_to_string(file, buf, sizeof(buf)));

				/* Currently these retries are purely a guess;
				 * there is no reproducible problem that they solve.  */
				if (--tries_remaining) {
					int delay = 100;
					if (status == STATUS_INSUFFICIENT_RESOURCES ||
					    status == STATUS_NO_MEMORY) {
						delay *= 25;
					}
					WARNING("Retrying after %dms...", delay);
					Sleep(delay);
					goto retry_read;
				}
				ERROR("Too many retries; returning failure");
				ret = WIMLIB_ERR_READ;
			}
			break;
		} else if (unlikely(tries_remaining != max_tries)) {
			WARNING("A read request had to be retried multiple times "
				"before it succeeded!");
		}

		bytes_read = iosb.Information;

		bytes_remaining -= bytes_read;
		ret = consume_chunk(cb, buf, bytes_read);
		if (ret)
			break;
	}
	NtClose(h);
	return ret;
}

struct win32_encrypted_read_ctx {
	const struct consume_chunk_callback *cb;
	int wimlib_err_code;
	u64 bytes_remaining;
};

static DWORD WINAPI
win32_encrypted_export_cb(unsigned char *data, void *_ctx, unsigned long len)
{
	struct win32_encrypted_read_ctx *ctx = _ctx;
	int ret;
	size_t bytes_to_consume = min(len, ctx->bytes_remaining);

	if (bytes_to_consume == 0)
		return ERROR_SUCCESS;

	ret = consume_chunk(ctx->cb, data, bytes_to_consume);
	if (ret) {
		ctx->wimlib_err_code = ret;
		/* It doesn't matter what error code is returned here, as long
		 * as it isn't ERROR_SUCCESS.  */
		return ERROR_READ_FAULT;
	}
	ctx->bytes_remaining -= bytes_to_consume;
	return ERROR_SUCCESS;
}

static int
read_win32_encrypted_file_prefix(const wchar_t *path, bool is_dir, u64 size,
				 const struct consume_chunk_callback *cb)
{
	struct win32_encrypted_read_ctx export_ctx;
	DWORD err;
	void *file_ctx;
	int ret;
	DWORD flags = 0;

	if (is_dir)
		flags |= CREATE_FOR_DIR;

	export_ctx.cb = cb;
	export_ctx.wimlib_err_code = 0;
	export_ctx.bytes_remaining = size;

	err = OpenEncryptedFileRaw(path, flags, &file_ctx);
	if (err != ERROR_SUCCESS) {
		win32_error(err,
			    L"Failed to open encrypted file \"%ls\" for raw read",
			    path);
		return WIMLIB_ERR_OPEN;
	}
	err = ReadEncryptedFileRaw(win32_encrypted_export_cb,
				   &export_ctx, file_ctx);
	if (err != ERROR_SUCCESS) {
		ret = export_ctx.wimlib_err_code;
		if (ret == 0) {
			win32_error(err,
				    L"Failed to read encrypted file \"%ls\"",
				    path);
			ret = WIMLIB_ERR_READ;
		}
	} else if (export_ctx.bytes_remaining != 0) {
		ERROR("Only could read %"PRIu64" of %"PRIu64" bytes from "
		      "encrypted file \"%ls\"",
		      size - export_ctx.bytes_remaining, size,
		      path);
		ret = WIMLIB_ERR_READ;
	} else {
		ret = 0;
	}
	CloseEncryptedFileRaw(file_ctx);
	return ret;
}

/* Read the first @size bytes from the file, or named data stream of a file,
 * described by @blob.  */
int
read_windows_file_prefix(const struct blob_descriptor *blob, u64 size,
			 const struct consume_chunk_callback *cb,
			 bool recover_data)
{
	const struct windows_file *file = blob->windows_file;

	if (unlikely(file->is_encrypted)) {
		bool is_dir = (blob->file_inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY);
		return read_win32_encrypted_file_prefix(file->path, is_dir, size, cb);
	}

	return read_winnt_stream_prefix(file, size, cb);
}

/*
 * Load the short name of a file into a WIM dentry.
 */
static noinline_for_stack NTSTATUS
winnt_get_short_name(HANDLE h, struct wim_dentry *dentry)
{
	/* It's not any harder to just make the NtQueryInformationFile() system
	 * call ourselves, and it saves a dumb call to FindFirstFile() which of
	 * course has to create its own handle.  */
	NTSTATUS status;
	IO_STATUS_BLOCK iosb;
	PRAGMA_ALIGN(u8 buf[128], 8);
	const FILE_NAME_INFORMATION *info;

	status = NtQueryInformationFile(h, &iosb, buf, sizeof(buf),
					FileAlternateNameInformation);
	info = (const FILE_NAME_INFORMATION *)buf;
	if (NT_SUCCESS(status) && info->FileNameLength != 0) {
		dentry->d_short_name = utf16le_dupz(info->FileName,
						    info->FileNameLength);
		if (!dentry->d_short_name)
			return STATUS_NO_MEMORY;
		dentry->d_short_name_nbytes = info->FileNameLength;
	}
	return status;
}

/*
 * Load the security descriptor of a file into the corresponding inode and the
 * WIM image's security descriptor set.
 */
static noinline_for_stack int
winnt_load_security_descriptor(HANDLE h, struct wim_inode *inode,
			       struct winnt_scan_ctx *ctx)
{
	SECURITY_INFORMATION requestedInformation;
	PRAGMA_ALIGN(u8 _buf[4096], 8);
	u8 *buf;
	ULONG bufsize;
	ULONG len_needed;
	NTSTATUS status;

	/*
	 * LABEL_SECURITY_INFORMATION is needed on Windows Vista and 7 because
	 * Microsoft decided to add mandatory integrity labels to the SACL but
	 * not have them returned by SACL_SECURITY_INFORMATION.
	 *
	 * BACKUP_SECURITY_INFORMATION is needed on Windows 8 because Microsoft
	 * decided to add even more stuff to the SACL and still not have it
	 * returned by SACL_SECURITY_INFORMATION; but they did remember that
	 * backup applications exist and simply want to read the stupid thing
	 * once and for all, so they added a flag to read the entire security
	 * descriptor.
	 *
	 * Older versions of Windows tolerate these new flags being passed in.
	 */
	requestedInformation = OWNER_SECURITY_INFORMATION |
			       GROUP_SECURITY_INFORMATION |
			       DACL_SECURITY_INFORMATION |
			       SACL_SECURITY_INFORMATION |
			       LABEL_SECURITY_INFORMATION |
			       BACKUP_SECURITY_INFORMATION;

	buf = _buf;
	bufsize = sizeof(_buf);

	/*
	 * We need the file's security descriptor in
	 * SECURITY_DESCRIPTOR_RELATIVE format, and we currently have a handle
	 * opened with as many relevant permissions as possible.  At this point,
	 * on Windows there are a number of options for reading a file's
	 * security descriptor:
	 *
	 * GetFileSecurity():  This takes in a path and returns the
	 * SECURITY_DESCRIPTOR_RELATIVE.  Problem: this uses an internal handle,
	 * not ours, and the handle created internally doesn't specify
	 * FILE_FLAG_BACKUP_SEMANTICS.  Therefore there can be access denied
	 * errors on some files and directories, even when running as the
	 * Administrator.
	 *
	 * GetSecurityInfo():  This takes in a handle and returns the security
	 * descriptor split into a bunch of different parts.  This should work,
	 * but it's dumb because we have to put the security descriptor back
	 * together again.
	 *
	 * BackupRead():  This can read the security descriptor, but this is a
	 * difficult-to-use API, probably only works as the Administrator, and
	 * the format of the returned data is not well documented.
	 *
	 * NtQuerySecurityObject():  This is exactly what we need, as it takes
	 * in a handle and returns the security descriptor in
	 * SECURITY_DESCRIPTOR_RELATIVE format.  Only problem is that it's a
	 * ntdll function and therefore not officially part of the Win32 API.
	 * Oh well.
	 */
	while (!NT_SUCCESS(status = NtQuerySecurityObject(h,
							  requestedInformation,
							  (PSECURITY_DESCRIPTOR)buf,
							  bufsize,
							  &len_needed)))
	{
		switch (status) {
		case STATUS_BUFFER_TOO_SMALL:
			wimlib_assert(buf == _buf);
			buf = MALLOC(len_needed);
			if (!buf) {
				status = STATUS_NO_MEMORY;
				goto out;
			}
			bufsize = len_needed;
			break;
		case STATUS_PRIVILEGE_NOT_HELD:
		case STATUS_ACCESS_DENIED:
			if (ctx->params->add_flags & WIMLIB_ADD_FLAG_STRICT_ACLS) {
		default:
				/* Permission denied in STRICT_ACLS mode, or
				 * unknown error.  */
				goto out;
			}
			if (requestedInformation & SACL_SECURITY_INFORMATION) {
				/* Try again without the SACL.  */
				ctx->num_get_sacl_priv_notheld++;
				requestedInformation &= ~(SACL_SECURITY_INFORMATION |
							  LABEL_SECURITY_INFORMATION |
							  BACKUP_SECURITY_INFORMATION);
				break;
			}
			/* Fake success (useful when capturing as
			 * non-Administrator).  */
			ctx->num_get_sd_access_denied++;
			status = STATUS_SUCCESS;
			goto out;
		}
	}

	/* We can get a length of 0 with Samba.  Assume that means "no security
	 * descriptor".  */
	if (len_needed == 0)
		goto out;

	/* Add the security descriptor to the WIM image, and save its ID in
	 * the file's inode.  */
	inode->i_security_id = sd_set_add_sd(ctx->params->sd_set, buf, len_needed);
	if (unlikely(inode->i_security_id < 0))
		status = STATUS_NO_MEMORY;
out:
	if (unlikely(buf != _buf))
		FREE(buf);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"\"%ls\": Can't read security descriptor",
			    printable_path(ctx));
		return WIMLIB_ERR_STAT;
	}
	return 0;
}

/* Load a file's object ID into the corresponding WIM inode.  */
static noinline_for_stack int
winnt_load_object_id(HANDLE h, struct wim_inode *inode,
		     struct winnt_scan_ctx *ctx)
{
	FILE_OBJECTID_BUFFER buffer;
	NTSTATUS status;
	u32 len;

	if (!(ctx->vol_flags & FILE_SUPPORTS_OBJECT_IDS))
		return 0;

	status = winnt_fsctl(h, FSCTL_GET_OBJECT_ID, NULL, 0,
			     &buffer, sizeof(buffer), &len);

	if (status == STATUS_OBJECTID_NOT_FOUND) /* No object ID  */
		return 0;

	if (status == STATUS_INVALID_DEVICE_REQUEST ||
	    status == STATUS_NOT_SUPPORTED /* Samba volume, WinXP */) {
		/* The filesystem claimed to support object IDs, but we can't
		 * actually read them.  This happens with Samba.  */
		ctx->vol_flags &= ~FILE_SUPPORTS_OBJECT_IDS;
		return 0;
	}

	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"\"%ls\": Can't read object ID",
			    printable_path(ctx));
		return WIMLIB_ERR_STAT;
	}

	if (len == 0) /* No object ID (for directories)  */
		return 0;

	if (!inode_set_object_id(inode, &buffer, len))
		return WIMLIB_ERR_NOMEM;

	return 0;
}

/* Load a file's extended attributes into the corresponding WIM inode.  */
static noinline_for_stack int
winnt_load_xattrs(HANDLE h, struct wim_inode *inode,
		  struct winnt_scan_ctx *ctx, u32 ea_size)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	PRAGMA_ALIGN(u8 _buf[1024], 4);
	u8 *buf = _buf;
	const FILE_FULL_EA_INFORMATION *ea;
	struct wim_xattr_entry *entry;
	int ret;


	/*
	 * EaSize from FILE_EA_INFORMATION is apparently supposed to give the
	 * size of the buffer required for NtQueryEaFile(), but it doesn't
	 * actually work correctly; it can be off by about 4 bytes per xattr.
	 *
	 * So just start out by doubling the advertised size, and also handle
	 * STATUS_BUFFER_OVERFLOW just in case.
	 */
retry:
	if (unlikely(ea_size * 2 < ea_size))
		ea_size = UINT32_MAX;
	else
		ea_size *= 2;
	if (unlikely(ea_size > sizeof(_buf))) {
		buf = MALLOC(ea_size);
		if (!buf) {
			if (ea_size >= (1 << 20)) {
				WARNING("\"%ls\": EaSize was extremely large (%u)",
					printable_path(ctx), ea_size);
			}
			return WIMLIB_ERR_NOMEM;
		}
	}

	status = NtQueryEaFile(h, &iosb, buf, ea_size,
			       FALSE, NULL, 0, NULL, TRUE);

	if (unlikely(!NT_SUCCESS(status))) {
		if (status == STATUS_BUFFER_OVERFLOW) {
			if (buf != _buf) {
				FREE(buf);
				buf = NULL;
			}
			goto retry;
		}
		if (status == STATUS_NO_EAS_ON_FILE) {
			/*
			 * FILE_EA_INFORMATION.EaSize was nonzero so this
			 * shouldn't happen, but just in case...
			 */
			ret = 0;
			goto out;
		}
		winnt_error(status, L"\"%ls\": Can't read extended attributes",
			    printable_path(ctx));
		ret = WIMLIB_ERR_STAT;
		goto out;
	}

	ea = (const FILE_FULL_EA_INFORMATION *)buf;
	entry = (struct wim_xattr_entry *)buf;
	for (;;) {
		/*
		 * wim_xattr_entry is not larger than FILE_FULL_EA_INFORMATION,
		 * so we can reuse the same buffer by overwriting the
		 * FILE_FULL_EA_INFORMATION with the wim_xattr_entry in-place.
		 */
		FILE_FULL_EA_INFORMATION _ea;

		STATIC_ASSERT(offsetof(struct wim_xattr_entry, name) <=
			      offsetof(FILE_FULL_EA_INFORMATION, EaName));
		wimlib_assert((u8 *)entry <= (const u8 *)ea);

		memcpy(&_ea, ea, sizeof(_ea));

		entry->value_len = cpu_to_le16(_ea.EaValueLength);
		entry->name_len = _ea.EaNameLength;
		entry->flags = _ea.Flags;
		memmove(entry->name, ea->EaName, _ea.EaNameLength);
		entry->name[_ea.EaNameLength] = '\0';
		memmove(&entry->name[_ea.EaNameLength + 1],
			&ea->EaName[_ea.EaNameLength + 1], _ea.EaValueLength);
		entry = (struct wim_xattr_entry *)
			 &entry->name[_ea.EaNameLength + 1 + _ea.EaValueLength];
		if (_ea.NextEntryOffset == 0)
			break;
		ea = (const FILE_FULL_EA_INFORMATION *)
			((const u8 *)ea + _ea.NextEntryOffset);
	}
	wimlib_assert((u8 *)entry - buf <= ea_size);

	ret = WIMLIB_ERR_NOMEM;
	if (!inode_set_xattrs(inode, buf, (u8 *)entry - buf))
		goto out;
	ret = 0;
out:
	if (unlikely(buf != _buf))
		FREE(buf);
	return ret;
}

static int
winnt_build_dentry_tree(struct wim_dentry **root_ret,
			HANDLE cur_dir,
			const wchar_t *relative_path,
			size_t relative_path_nchars,
			const wchar_t *filename,
			struct winnt_scan_ctx *ctx,
			bool recursive);

static int
winnt_recurse_directory(HANDLE h,
			struct wim_dentry *parent,
			struct winnt_scan_ctx *ctx)
{
	void *buf;
	const size_t bufsize = 8192;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	int ret = WIMLIB_ERR_SUCCESS;

	buf = MALLOC(bufsize);
	if (!buf)
		return WIMLIB_ERR_NOMEM;

	/* Using NtQueryDirectoryFile() we can re-use the same open handle,
	 * which we opened with FILE_FLAG_BACKUP_SEMANTICS.  */

	while (NT_SUCCESS(status = NtQueryDirectoryFile(h, NULL, NULL, NULL,
							&iosb, buf, bufsize,
							FileNamesInformation,
							FALSE, NULL, FALSE)))
	{
		const FILE_NAMES_INFORMATION *info = buf;
		for (;;) {
			if (!should_ignore_filename(info->FileName,
						    info->FileNameLength / 2))
			{
				struct wim_dentry *child;
				size_t orig_path_nchars;
				const wchar_t *filename;

				ret = WIMLIB_ERR_NOMEM;
				filename = pathbuf_append_name(ctx->params,
							       info->FileName,
							       info->FileNameLength / 2,
							       &orig_path_nchars);
				if (!filename)
					goto out_free_buf;

				ret = winnt_build_dentry_tree(
							&child,
							h,
							filename,
							info->FileNameLength / 2,
							filename,
							ctx,
							true);

				pathbuf_truncate(ctx->params, orig_path_nchars);

				if (ret)
					goto out_free_buf;
				attach_scanned_tree(parent, child,
						    ctx->params->blob_table);
			}
			if (info->NextEntryOffset == 0)
				break;
			info = (const FILE_NAMES_INFORMATION *)
					((const u8 *)info + info->NextEntryOffset);
		}
	}

	if (unlikely(status != STATUS_NO_MORE_FILES)) {
		winnt_error(status, L"\"%ls\": Can't read directory",
			    printable_path(ctx));
		ret = WIMLIB_ERR_READ;
	}
out_free_buf:
	FREE(buf);
	return ret;
}

/* Reparse point fixup status code  */
#define RP_FIXED	(-1)

static bool
file_has_ino_and_dev(HANDLE h, u64 ino, u64 dev)
{
	NTSTATUS status;
	IO_STATUS_BLOCK iosb;
	FILE_INTERNAL_INFORMATION int_info;
	FILE_FS_VOLUME_INFORMATION vol_info = { 0 };

	status = NtQueryInformationFile(h, &iosb, &int_info, sizeof(int_info),
					FileInternalInformation);
	if (!NT_SUCCESS(status))
		return false;

	if (int_info.IndexNumber.QuadPart != ino)
		return false;

	status = NtQueryVolumeInformationFile(h, &iosb,
					      &vol_info, sizeof(vol_info),
					      FileFsVolumeInformation);
	if (!(NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW))
		return false;

	if (iosb.Information <
	     offsetof(FILE_FS_VOLUME_INFORMATION, VolumeSerialNumber) +
	     sizeof(vol_info.VolumeSerialNumber))
		return false;

	return (vol_info.VolumeSerialNumber == dev);
}

/*
 * This is the Windows equivalent of unix_relativize_link_target(); see there
 * for general details.  This version works with an "absolute" Windows link
 * target, specified from the root of the Windows kernel object namespace.  Note
 * that we have to open directories with a trailing slash when present because
 * \??\E: opens the E: device itself and not the filesystem root directory.
 */
static const wchar_t *
winnt_relativize_link_target(const wchar_t *target, size_t target_nbytes,
			     u64 ino, u64 dev)
{
	UNICODE_STRING name;
	OBJECT_ATTRIBUTES attr;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	const wchar_t *target_end;
	const wchar_t *p;

	target_end = target + (target_nbytes / sizeof(wchar_t));

	/* Empty path??? */
	if (target_end == target)
		return target;

	/* No leading slash???  */
	if (target[0] != L'\\')
		return target;

	/* UNC path???  */
	if ((target_end - target) >= 2 &&
	    target[0] == L'\\' && target[1] == L'\\')
		return target;

	attr.Length = sizeof(attr);
	attr.RootDirectory = NULL;
	attr.ObjectName = &name;
	attr.Attributes = 0;
	attr.SecurityDescriptor = NULL;
	attr.SecurityQualityOfService = NULL;

	name.Buffer = (wchar_t *)target;
	name.Length = 0;
	p = target;
	do {
		HANDLE h;
		const wchar_t *orig_p = p;

		/* Skip non-backslashes  */
		while (p != target_end && *p != L'\\')
			p++;

		/* Skip backslashes  */
		while (p != target_end && *p == L'\\')
			p++;

		/* Append path component  */
		name.Length += (p - orig_p) * sizeof(wchar_t);
		name.MaximumLength = name.Length;

		/* Try opening the file  */
		status = NtOpenFile(&h,
				    FILE_READ_ATTRIBUTES | FILE_TRAVERSE,
				    &attr,
				    &iosb,
				    FILE_SHARE_VALID_FLAGS,
				    FILE_OPEN_FOR_BACKUP_INTENT);

		if (NT_SUCCESS(status)) {
			/* Reset root directory  */
			if (attr.RootDirectory)
				NtClose(attr.RootDirectory);
			attr.RootDirectory = h;
			name.Buffer = (wchar_t *)p;
			name.Length = 0;

			if (file_has_ino_and_dev(h, ino, dev))
				goto out_close_root_dir;
		}
	} while (p != target_end);

	p = target;

out_close_root_dir:
	if (attr.RootDirectory)
		NtClose(attr.RootDirectory);
	while (p > target && *(p - 1) == L'\\')
		p--;
	return p;
}

static int
winnt_rpfix_progress(struct scan_params *params,
		     const struct link_reparse_point *link, int scan_status)
{
	size_t print_name_nchars = link->print_name_nbytes / sizeof(wchar_t);
	wchar_t* print_name0 = alloca((print_name_nchars + 1) * sizeof(wchar_t));

	wmemcpy(print_name0, link->print_name, print_name_nchars);
	print_name0[print_name_nchars] = L'\0';

	params->progress.scan.symlink_target = print_name0;
	return do_scan_progress(params, scan_status, NULL);
}

static int
winnt_try_rpfix(struct reparse_buffer_disk *rpbuf, u16 *rpbuflen_p,
		struct scan_params *params)
{
	struct link_reparse_point link;
	const wchar_t *rel_target;
	int ret;

	if (parse_link_reparse_point(rpbuf, *rpbuflen_p, &link)) {
		/* Couldn't understand the reparse data; don't do the fixup.  */
		return 0;
	}

	/*
	 * Don't do reparse point fixups on relative symbolic links.
	 *
	 * On Windows, a relative symbolic link is supposed to be identifiable
	 * by having reparse tag WIM_IO_REPARSE_TAG_SYMLINK and flags
	 * SYMBOLIC_LINK_RELATIVE.  We will use this information, although this
	 * may not always do what the user expects, since drive-relative
	 * symbolic links such as "\Users\Public" have SYMBOLIC_LINK_RELATIVE
	 * set, in addition to truly relative symbolic links such as "Users" or
	 * "Users\Public".  However, WIMGAPI (as of Windows 8.1) has this same
	 * behavior.
	 *
	 * Otherwise, as far as I can tell, the targets of symbolic links that
	 * are NOT relative, as well as junctions (note: a mountpoint is the
	 * sames thing as a junction), must be NT namespace paths, for example:
	 *
	 *     - \??\e:\Users\Public
	 *     - \DosDevices\e:\Users\Public
	 *     - \Device\HardDiskVolume4\Users\Public
	 *     - \??\Volume{c47cb07c-946e-4155-b8f7-052e9cec7628}\Users\Public
	 *     - \DosDevices\Volume{c47cb07c-946e-4155-b8f7-052e9cec7628}\Users\Public
	 */
	if (link_is_relative_symlink(&link))
		return 0;

	rel_target = winnt_relativize_link_target(link.substitute_name,
						  link.substitute_name_nbytes,
						  params->capture_root_ino,
						  params->capture_root_dev);

	if (rel_target == link.substitute_name) {
		/* Target points outside of the tree being captured or had an
		 * unrecognized path format.  Don't adjust it.  */
		return winnt_rpfix_progress(params, &link,
					    WIMLIB_SCAN_DENTRY_NOT_FIXED_SYMLINK);
	}

	/* We have an absolute target pointing within the directory being
	 * captured. @rel_target is the suffix of the link target that is the
	 * part relative to the directory being captured.
	 *
	 * We will cut off the prefix before this part (which is the path to the
	 * directory being captured) and add a dummy prefix.  Since the process
	 * will need to be reversed when applying the image, it doesn't matter
	 * what exactly the prefix is, as long as it looks like an absolute
	 * path.  */

	static const wchar_t prefix[] = L"\\??\\X:";
	static const size_t num_unprintable_chars = 4;

	size_t rel_target_nbytes =
		link.substitute_name_nbytes - ((const u8 *)rel_target -
					       (const u8 *)link.substitute_name);

	wchar_t* tmp = alloca(sizeof(prefix) + rel_target_nbytes);
	if (!tmp)
		return 0;

	memcpy(tmp, prefix, sizeof(prefix));
	memcpy(tmp + ARRAY_LEN(prefix), rel_target, rel_target_nbytes);

	link.substitute_name = tmp;
	link.substitute_name_nbytes = sizeof(tmp);

	link.print_name = link.substitute_name + num_unprintable_chars;
	link.print_name_nbytes = link.substitute_name_nbytes -
				 (num_unprintable_chars * sizeof(wchar_t));

	if (make_link_reparse_point(&link, rpbuf, rpbuflen_p))
		return 0;

	ret = winnt_rpfix_progress(params, &link,
				   WIMLIB_SCAN_DENTRY_FIXED_SYMLINK);
	if (ret)
		return ret;
	return RP_FIXED;
}

/* Load the reparse data of a file into the corresponding WIM inode.  If the
 * reparse point is a symbolic link or junction with an absolute target and
 * RPFIX mode is enabled, then also rewrite its target to be relative to the
 * capture root.  */
static noinline_for_stack int
winnt_load_reparse_data(HANDLE h, struct wim_inode *inode,
			struct winnt_scan_ctx *ctx)
{
	struct reparse_buffer_disk rpbuf;
	NTSTATUS status;
	u32 len;
	u16 rpbuflen;
	int ret;

	if (inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED) {
		/* See comment above assign_stream_types_encrypted()  */
		WARNING("Ignoring reparse data of encrypted file \"%ls\"",
			printable_path(ctx));
		return 0;
	}

	status = winnt_fsctl(h, FSCTL_GET_REPARSE_POINT,
			     NULL, 0, &rpbuf, sizeof(rpbuf), &len);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"\"%ls\": Can't get reparse point",
			    printable_path(ctx));
		return WIMLIB_ERR_READLINK;
	}

	rpbuflen = len;

	if (unlikely(rpbuflen < REPARSE_DATA_OFFSET)) {
		ERROR("\"%ls\": reparse point buffer is too short",
		      printable_path(ctx));
		return WIMLIB_ERR_INVALID_REPARSE_DATA;
	}

	if (le32_to_cpu(rpbuf.rptag) == WIM_IO_REPARSE_TAG_DEDUP) {
		/*
		 * Windows treats Data Deduplication reparse points specially.
		 * Reads from the unnamed data stream actually return the
		 * redirected file contents, even with FILE_OPEN_REPARSE_POINT.
		 * Deduplicated files also cannot be properly restored without
		 * also restoring the "System Volume Information" directory,
		 * which wimlib excludes by default.  Therefore, the logical
		 * behavior for us seems to be to ignore the reparse point and
		 * treat the file as a normal file.
		 */
		inode->i_attributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
		return 0;
	}

	if (ctx->params->add_flags & WIMLIB_ADD_FLAG_RPFIX) {
		ret = winnt_try_rpfix(&rpbuf, &rpbuflen, ctx->params);
		if (ret == RP_FIXED)
			inode->i_rp_flags &= ~WIM_RP_FLAG_NOT_FIXED;
		else if (ret)
			return ret;
	}

	inode->i_reparse_tag = le32_to_cpu(rpbuf.rptag);
	inode->i_rp_reserved = le16_to_cpu(rpbuf.rpreserved);

	if (!inode_add_stream_with_data(inode,
					STREAM_TYPE_REPARSE_POINT,
					NO_STREAM_NAME,
					rpbuf.rpdata,
					rpbuflen - REPARSE_DATA_OFFSET,
					ctx->params->blob_table))
		return WIMLIB_ERR_NOMEM;

	return 0;
}

static DWORD WINAPI
win32_tally_encrypted_size_cb(unsigned char *_data, void *_size_ret,
			      unsigned long len)
{
	*(u64*)_size_ret += len;
	return ERROR_SUCCESS;
}

static int
win32_get_encrypted_file_size(const wchar_t *path, bool is_dir, u64 *size_ret)
{
	DWORD err;
	void *file_ctx;
	int ret;
	DWORD flags = 0;

	if (is_dir)
		flags |= CREATE_FOR_DIR;

	err = OpenEncryptedFileRaw(path, flags, &file_ctx);
	if (err != ERROR_SUCCESS) {
		win32_error(err,
			    L"Failed to open encrypted file \"%ls\" for raw read",
			    path);
		return WIMLIB_ERR_OPEN;
	}
	*size_ret = 0;
	err = ReadEncryptedFileRaw(win32_tally_encrypted_size_cb,
				   size_ret, file_ctx);
	if (err != ERROR_SUCCESS) {
		win32_error(err,
			    L"Failed to read raw encrypted data from \"%ls\"",
			    path);
		ret = WIMLIB_ERR_READ;
	} else {
		ret = 0;
	}
	CloseEncryptedFileRaw(file_ctx);
	return ret;
}

static int
winnt_scan_efsrpc_raw_data(struct wim_inode *inode,
			   struct winnt_scan_ctx *ctx)
{
	wchar_t *path = ctx->params->cur_path;
	size_t path_nchars = ctx->params->cur_path_nchars;
	const bool is_dir = (inode->i_attributes & FILE_ATTRIBUTE_DIRECTORY);
	struct windows_file *windows_file;
	u64 size;
	int ret;

	/* OpenEncryptedFileRaw() expects a Win32 name.  */
	wimlib_assert(!wmemcmp(path, L"\\??\\", 4));
	path[1] = L'\\';

	ret = win32_get_encrypted_file_size(path, is_dir, &size);
	if (ret)
		goto out;

	/* Empty EFSRPC data does not make sense  */
	wimlib_assert(size != 0);

	windows_file = alloc_windows_file(path, path_nchars, NULL, 0,
					  ctx->snapshot, true);
	ret = add_stream(inode, windows_file, size, STREAM_TYPE_EFSRPC_RAW_DATA,
			 NO_STREAM_NAME, ctx->params->unhashed_blobs);
out:
	path[1] = L'?';
	return ret;
}

static bool
get_data_stream_name(const wchar_t *raw_stream_name, size_t raw_stream_name_nchars,
		     const wchar_t **stream_name_ret, size_t *stream_name_nchars_ret)
{
	const wchar_t *sep, *type, *end;

	/* The stream name should be returned as :NAME:TYPE  */
	if (raw_stream_name_nchars < 1)
		return false;
	if (raw_stream_name[0] != L':')
		return false;

	raw_stream_name++;
	raw_stream_name_nchars--;

	end = raw_stream_name + raw_stream_name_nchars;

	sep = wmemchr(raw_stream_name, L':', raw_stream_name_nchars);
	if (!sep)
		return false;

	type = sep + 1;
	if (end - type != 5)
		return false;

	if (wmemcmp(type, L"$DATA", 5))
		return false;

	*stream_name_ret = raw_stream_name;
	*stream_name_nchars_ret = sep - raw_stream_name;
	return true;
}

static int
winnt_scan_data_stream(wchar_t *raw_stream_name, size_t raw_stream_name_nchars,
		       u64 stream_size, struct wim_inode *inode,
		       struct winnt_scan_ctx *ctx)
{
	wchar_t *stream_name;
	size_t stream_name_nchars;
	struct windows_file *windows_file;

	/* Given the raw stream name (which is something like
	 * :streamname:$DATA), extract just the stream name part (streamname).
	 * Ignore any non-$DATA streams.  */
	if (!get_data_stream_name(raw_stream_name, raw_stream_name_nchars,
				  (const wchar_t **)&stream_name,
				  &stream_name_nchars))
		return 0;

	stream_name[stream_name_nchars] = L'\0';

	windows_file = alloc_windows_file(ctx->params->cur_path,
					  ctx->params->cur_path_nchars,
					  stream_name, stream_name_nchars,
					  ctx->snapshot, false);
	return add_stream(inode, windows_file, stream_size, STREAM_TYPE_DATA,
			  stream_name, ctx->params->unhashed_blobs);
}

/*
 * Load information about the data streams of an open file into a WIM inode.
 *
 * We use the NtQueryInformationFile() system call instead of FindFirstStream()
 * and FindNextStream(), since FindFirstStream() opens its own handle to the
 * file or directory and apparently does so without specifying
 * FILE_FLAG_BACKUP_SEMANTICS.  This causing access denied errors on certain
 * files, even when running as the Administrator.
 */
static noinline_for_stack int
winnt_scan_data_streams(HANDLE h, struct wim_inode *inode, u64 file_size,
			struct winnt_scan_ctx *ctx)
{
	int ret;
	PRAGMA_ALIGN(u8 _buf[4096], 8);
	u8 *buf;
	size_t bufsize;
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	FILE_STREAM_INFORMATION *info;

	buf = _buf;
	bufsize = sizeof(_buf);

	if (!(ctx->vol_flags & FILE_NAMED_STREAMS))
		goto unnamed_only;

	/* Get a buffer containing the stream information.  */
	while (!NT_SUCCESS(status = NtQueryInformationFile(h,
							   &iosb,
							   buf,
							   bufsize,
							   FileStreamInformation)))
	{

		switch (status) {
		case STATUS_BUFFER_OVERFLOW:
			{
				u8 *newbuf;

				bufsize *= 2;
				if (buf == _buf)
					newbuf = MALLOC(bufsize);
				else
					newbuf = REALLOC(buf, bufsize);
				if (!newbuf) {
					ret = WIMLIB_ERR_NOMEM;
					goto out_free_buf;
				}
				buf = newbuf;
			}
			break;
		case STATUS_NOT_IMPLEMENTED:
		case STATUS_NOT_SUPPORTED:
		case STATUS_INVALID_INFO_CLASS:
			goto unnamed_only;
		default:
			winnt_error(status,
				    L"\"%ls\": Failed to query stream information",
				    printable_path(ctx));
			ret = WIMLIB_ERR_READ;
			goto out_free_buf;
		}
	}

	if (iosb.Information == 0) {
		/* No stream information.  */
		ret = 0;
		goto out_free_buf;
	}

	/* Parse one or more stream information structures.  */
	info = (FILE_STREAM_INFORMATION *)buf;
	for (;;) {
		/* Load the stream information.  */
		ret = winnt_scan_data_stream(info->StreamName,
					     info->StreamNameLength / 2,
					     info->StreamSize.QuadPart,
					     inode, ctx);
		if (ret)
			goto out_free_buf;

		if (info->NextEntryOffset == 0) {
			/* No more stream information.  */
			break;
		}
		/* Advance to next stream information.  */
		info = (FILE_STREAM_INFORMATION *)
				((u8 *)info + info->NextEntryOffset);
	}
	ret = 0;
	goto out_free_buf;

unnamed_only:
	/* The volume does not support named streams.  Only capture the unnamed
	 * data stream.  */
	if (inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
				   FILE_ATTRIBUTE_REPARSE_POINT))
	{
		ret = 0;
		goto out_free_buf;
	}

	{
		wchar_t stream_name[] = L"::$DATA";
		ret = winnt_scan_data_stream(stream_name, 7, file_size,
					     inode, ctx);
	}
out_free_buf:
	/* Free buffer if allocated on heap.  */
	if (unlikely(buf != _buf))
		FREE(buf);
	return ret;
}

static u64
extract_starting_lcn(const RETRIEVAL_POINTERS_BUFFER *extents)
{
	if (extents->ExtentCount < 1)
		return 0;

	return extents->Extents[0].Lcn.QuadPart;
}

static noinline_for_stack u64
get_sort_key(HANDLE h)
{
	STARTING_VCN_INPUT_BUFFER in = { .StartingVcn.QuadPart = 0 };
	RETRIEVAL_POINTERS_BUFFER out;

	if (!NT_SUCCESS(winnt_fsctl(h, FSCTL_GET_RETRIEVAL_POINTERS,
				    &in, sizeof(in), &out, sizeof(out), NULL)))
		return 0;

	return extract_starting_lcn(&out);
}

static void
set_sort_key(struct wim_inode *inode, u64 sort_key)
{
	for (unsigned i = 0; i < inode->i_num_streams; i++) {
		struct wim_inode_stream *strm = &inode->i_streams[i];
		struct blob_descriptor *blob = stream_blob_resolved(strm);
		if (blob && blob->blob_location == BLOB_IN_WINDOWS_FILE)
			blob->windows_file->sort_key = sort_key;
	}
}

static inline bool
should_try_to_use_wimboot_hash(const struct wim_inode *inode,
			       const struct winnt_scan_ctx *ctx)
{
	/* Directories and encrypted files aren't valid for external backing. */
	if (inode->i_attributes & (FILE_ATTRIBUTE_DIRECTORY |
				   FILE_ATTRIBUTE_ENCRYPTED))
		return false;

	/* If the file is a reparse point, then try the hash fixup if it's a WOF
	 * reparse point and we're in WIMBOOT mode.  Otherwise, try the hash
	 * fixup if WOF may be attached. */
	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
		return (inode->i_reparse_tag == WIM_IO_REPARSE_TAG_WOF) &&
			(ctx->params->add_flags & WIMLIB_ADD_FLAG_WIMBOOT);
	return !ctx->wof_not_attached;
}

/*
 * This function implements an optimization for capturing files from a
 * filesystem with a backing WIM(s).  If a file is WIM-backed, then we can
 * retrieve the SHA-1 message digest of its original contents from its reparse
 * point.  This may eliminate the need to read the file's data and/or allow the
 * file's data to be immediately deduplicated with existing data in the WIM.
 *
 * If WOF is attached, then this function is merely an optimization, but
 * potentially a very effective one.  If WOF is detached, then this function
 * really causes WIM-backed files to be, effectively, automatically
 * "dereferenced" when possible; the unnamed data stream is updated to reference
 * the original contents and the reparse point is removed.
 *
 * This function returns 0 if the fixup succeeded or was intentionally not
 * executed.  Otherwise it returns an error code.
 */
static noinline_for_stack int
try_to_use_wimboot_hash(HANDLE h, struct wim_inode *inode,
			struct winnt_scan_ctx *ctx)
{
	struct blob_table *blob_table = ctx->params->blob_table;
	struct wim_inode_stream *reparse_strm = NULL;
	struct wim_inode_stream *strm;
	struct blob_descriptor *blob;
	u8 hash[SHA1_HASH_SIZE];
	int ret;

	if (inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		struct reparse_buffer_disk rpbuf;
		struct {
			WOF_EXTERNAL_INFO wof_info;
			struct wim_provider_rpdata wim_info;
		} *rpdata = (void *)rpbuf.rpdata;
		struct blob_descriptor *reparse_blob;

		/* The file has a WOF reparse point, so WOF must be detached.
		 * We can read the reparse point directly.  */
		ctx->wof_not_attached = true;
		reparse_strm = inode_get_unnamed_stream(inode, STREAM_TYPE_REPARSE_POINT);
		reparse_blob = stream_blob_resolved(reparse_strm);

		if (!reparse_blob || reparse_blob->size < sizeof(*rpdata))
			return 0;  /* Not a WIM-backed file  */

		ret = read_blob_into_buf(reparse_blob, rpdata);
		if (ret)
			return ret;

		if (rpdata->wof_info.Version != WOF_CURRENT_VERSION ||
		    rpdata->wof_info.Provider != WOF_PROVIDER_WIM ||
		    rpdata->wim_info.version != 2)
			return 0;  /* Not a WIM-backed file  */

		/* Okay, this is a WIM backed file.  Get its SHA-1 hash.  */
		copy_hash(hash, rpdata->wim_info.unnamed_data_stream_hash);
	} else {
		struct {
			WOF_EXTERNAL_INFO wof_info;
			WIM_PROVIDER_EXTERNAL_INFO wim_info;
		} out;
		NTSTATUS status;

		/* WOF may be attached.  Try reading this file's external
		 * backing info.  */
		status = winnt_fsctl(h, FSCTL_GET_EXTERNAL_BACKING,
				     NULL, 0, &out, sizeof(out), NULL);

		/* Is WOF not attached?  */
		if (status == STATUS_INVALID_DEVICE_REQUEST ||
		    status == STATUS_NOT_SUPPORTED) {
			ctx->wof_not_attached = true;
			return 0;
		}

		/* Is this file not externally backed?  */
		if (status == STATUS_OBJECT_NOT_EXTERNALLY_BACKED)
			return 0;

		/* Does this file have an unknown type of external backing that
		 * needed a larger information buffer?  */
		if (status == STATUS_BUFFER_TOO_SMALL)
			return 0;

		/* Was there some other failure?  */
		if (status != STATUS_SUCCESS) {
			winnt_error(status,
				    L"\"%ls\": FSCTL_GET_EXTERNAL_BACKING failed",
				    printable_path(ctx));
			return WIMLIB_ERR_STAT;
		}

		/* Is this file backed by a WIM?  */
		if (out.wof_info.Version != WOF_CURRENT_VERSION ||
		    out.wof_info.Provider != WOF_PROVIDER_WIM ||
		    out.wim_info.Version != WIM_PROVIDER_CURRENT_VERSION)
			return 0;

		/* Okay, this is a WIM backed file.  Get its SHA-1 hash.  */
		copy_hash(hash, out.wim_info.ResourceHash);
	}

	/* If the file's unnamed data stream is nonempty, then fill in its hash
	 * and deduplicate it if possible.
	 *
	 * With WOF detached, we require that the blob *must* de-duplicable for
	 * any action can be taken, since without WOF we can't fall back to
	 * getting the "dereferenced" data by reading the stream (the real
	 * stream is sparse and contains all zeroes).  */
	strm = inode_get_unnamed_data_stream(inode);
	if (strm && (blob = stream_blob_resolved(strm))) {
		struct blob_descriptor **back_ptr;

		if (reparse_strm && !lookup_blob(blob_table, hash))
			return 0;
		back_ptr = retrieve_pointer_to_unhashed_blob(blob);
		copy_hash(blob->hash, hash);
		if (after_blob_hashed(blob, back_ptr, blob_table,
				      inode) != blob)
			free_blob_descriptor(blob);
	}

	/* Remove the reparse point, if present.  */
	if (reparse_strm) {
		inode_remove_stream(inode, reparse_strm, blob_table);
		inode->i_attributes &= ~(FILE_ATTRIBUTE_REPARSE_POINT |
					 FILE_ATTRIBUTE_SPARSE_FILE);
		if (inode->i_attributes == 0)
			inode->i_attributes = FILE_ATTRIBUTE_NORMAL;
	}

	return 0;
}

struct file_info {
	u32 attributes;
	u32 num_links;
	u64 creation_time;
	u64 last_write_time;
	u64 last_access_time;
	u64 ino;
	u64 end_of_file;
	u32 ea_size;
};

static noinline_for_stack NTSTATUS
get_file_info(HANDLE h, struct file_info *info)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS status;
	FILE_ALL_INFORMATION all_info;

	status = NtQueryInformationFile(h, &iosb, &all_info, sizeof(all_info),
					FileAllInformation);

	if (unlikely(!NT_SUCCESS(status) && status != STATUS_BUFFER_OVERFLOW))
		return status;

	info->attributes = all_info.BasicInformation.FileAttributes;
	info->num_links = all_info.StandardInformation.NumberOfLinks;
	info->creation_time = all_info.BasicInformation.CreationTime.QuadPart;
	info->last_write_time = all_info.BasicInformation.LastWriteTime.QuadPart;
	info->last_access_time = all_info.BasicInformation.LastAccessTime.QuadPart;
	info->ino = all_info.InternalInformation.IndexNumber.QuadPart;
	info->end_of_file = all_info.StandardInformation.EndOfFile.QuadPart;
	info->ea_size = all_info.EaInformation.EaSize;
	return STATUS_SUCCESS;
}

static void
get_volume_information(HANDLE h, struct winnt_scan_ctx *ctx)
{
	PRAGMA_ALIGN(u8 _attr_info[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 128], 8);
	FILE_FS_ATTRIBUTE_INFORMATION *attr_info = (void *)_attr_info;
	FILE_FS_VOLUME_INFORMATION vol_info;
	struct file_info file_info;
	IO_STATUS_BLOCK iosb = { 0 };
	NTSTATUS status;

	/* Get volume flags  */
	status = NtQueryVolumeInformationFile(h, &iosb, attr_info,
					      sizeof(_attr_info),
					      FileFsAttributeInformation);
	if (NT_SUCCESS(status)) {
		ctx->vol_flags = attr_info->FileSystemAttributes;
		ctx->is_ntfs = (attr_info->FileSystemNameLength == 4 * sizeof(wchar_t)) &&
				!wmemcmp(attr_info->FileSystemName, L"NTFS", 4);
	} else {
		winnt_warning(status, L"\"%ls\": Can't get volume attributes",
			      printable_path(ctx));
	}

	/* Get volume ID.  */
	status = NtQueryVolumeInformationFile(h, &iosb, &vol_info,
					      sizeof(vol_info),
					      FileFsVolumeInformation);
	if ((NT_SUCCESS(status) || status == STATUS_BUFFER_OVERFLOW) &&
	    (iosb.Information >= offsetof(FILE_FS_VOLUME_INFORMATION,
					  VolumeSerialNumber) +
	     sizeof(vol_info.VolumeSerialNumber)))
	{
		ctx->params->capture_root_dev = vol_info.VolumeSerialNumber;
	} else {
		winnt_warning(status, L"\"%ls\": Can't get volume ID",
			      printable_path(ctx));
	}

	/* Get inode number.  */
	status = get_file_info(h, &file_info);
	if (NT_SUCCESS(status)) {
		ctx->params->capture_root_ino = file_info.ino;
	} else {
		winnt_warning(status, L"\"%ls\": Can't get file information",
			      printable_path(ctx));
	}
}

static int
winnt_build_dentry_tree(struct wim_dentry **root_ret,
			HANDLE cur_dir,
			const wchar_t *relative_path,
			size_t relative_path_nchars,
			const wchar_t *filename,
			struct winnt_scan_ctx *ctx,
			bool recursive)
{
	struct wim_dentry *root = NULL;
	struct wim_inode *inode = NULL;
	HANDLE h = NULL;
	int ret;
	NTSTATUS status;
	struct file_info file_info;
	u64 sort_key;

	ret = try_exclude(ctx->params);
	if (unlikely(ret < 0)) /* Excluded? */
		goto out_progress;
	if (unlikely(ret > 0)) /* Error? */
		goto out;

	/* Open the file with permission to read metadata.  Although we will
	 * later need a handle with FILE_LIST_DIRECTORY permission (or,
	 * equivalently, FILE_READ_DATA; they're the same numeric value) if the
	 * file is a directory, it can significantly slow things down to request
	 * this permission on all nondirectories.  Perhaps it causes Windows to
	 * start prefetching the file contents...  */
	status = winnt_openat(cur_dir, relative_path, relative_path_nchars,
			      FILE_READ_ATTRIBUTES | FILE_READ_EA |
				READ_CONTROL | ACCESS_SYSTEM_SECURITY,
			      &h);
	if (unlikely(!NT_SUCCESS(status))) {
		if (status == STATUS_DELETE_PENDING) {
			WARNING("\"%ls\": Deletion pending; skipping file",
				printable_path(ctx));
			ret = 0;
			goto out;
		}
		if (status == STATUS_SHARING_VIOLATION) {
			ERROR("Can't open \"%ls\":\n"
			      "        File is in use by another process! "
			      "Consider using snapshot (VSS) mode.",
			      printable_path(ctx));
			ret = WIMLIB_ERR_OPEN;
			goto out;
		}
		winnt_error(status, L"\"%ls\": Can't open file",
			    printable_path(ctx));
		if (status == STATUS_FVE_LOCKED_VOLUME)
			ret = WIMLIB_ERR_FVE_LOCKED_VOLUME;
		else
			ret = WIMLIB_ERR_OPEN;
		goto out;
	}

	/* Get information about the file.  */
	status = get_file_info(h, &file_info);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"\"%ls\": Can't get file information",
			    printable_path(ctx));
		ret = WIMLIB_ERR_STAT;
		goto out;
	}

	/* Create a WIM dentry with an associated inode, which may be shared.
	 *
	 * However, we need to explicitly check for directories and files with
	 * only 1 link and refuse to hard link them.  This is because Windows
	 * has a bug where it can return duplicate File IDs for files and
	 * directories on the FAT filesystem.
	 *
	 * Since we don't follow mount points on Windows, we don't need to query
	 * the volume ID per-file.  Just once, for the root, is enough.  But we
	 * can't simply pass 0, because then there could be inode collisions
	 * among multiple calls to win32_build_dentry_tree() that are scanning
	 * files on different volumes.  */
	ret = inode_table_new_dentry(ctx->params->inode_table,
				     filename,
				     file_info.ino,
				     ctx->params->capture_root_dev,
				     (file_info.num_links <= 1),
				     &root);
	if (ret)
		goto out;

	/* Get the short (DOS) name of the file.  */
	status = winnt_get_short_name(h, root);

	/* If we can't read the short filename for any reason other than
	 * out-of-memory, just ignore the error and assume the file has no short
	 * name.  This shouldn't be an issue, since the short names are
	 * essentially obsolete anyway.  */
	if (unlikely(status == STATUS_NO_MEMORY)) {
		ret = WIMLIB_ERR_NOMEM;
		goto out;
	}

	inode = root->d_inode;

	if (inode->i_nlink > 1) {
		/* Shared inode (hard link); skip reading per-inode information.
		 */
		goto out_progress;
	}

	inode->i_attributes = file_info.attributes;
	inode->i_creation_time = file_info.creation_time;
	inode->i_last_write_time = file_info.last_write_time;
	inode->i_last_access_time = file_info.last_access_time;

	/* Get the file's security descriptor, unless we are capturing in
	 * NO_ACLS mode or the volume does not support security descriptors.  */
	if (!(ctx->params->add_flags & WIMLIB_ADD_FLAG_NO_ACLS)
	    && (ctx->vol_flags & FILE_PERSISTENT_ACLS))
	{
		ret = winnt_load_security_descriptor(h, inode, ctx);
		if (ret)
			goto out;
	}

	/* Get the file's object ID.  */
	ret = winnt_load_object_id(h, inode, ctx);
	if (ret)
		goto out;

	/* Get the file's extended attributes.  */
	if (unlikely(file_info.ea_size != 0)) {
		ret = winnt_load_xattrs(h, inode, ctx, file_info.ea_size);
		if (ret)
			goto out;
	}

	/* If this is a reparse point, load the reparse data.  */
	if (unlikely(inode->i_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
		ret = winnt_load_reparse_data(h, inode, ctx);
		if (ret)
			goto out;
	}

	sort_key = get_sort_key(h);

	if (unlikely(inode->i_attributes & FILE_ATTRIBUTE_ENCRYPTED)) {
		/* Load information about the raw encrypted data.  This is
		 * needed for any directory or non-directory that has
		 * FILE_ATTRIBUTE_ENCRYPTED set.
		 *
		 * Note: since OpenEncryptedFileRaw() fails with
		 * ERROR_SHARING_VIOLATION if there are any open handles to the
		 * file, we have to close the file and re-open it later if
		 * needed.  */
		NtClose(h);
		h = NULL;
		ret = winnt_scan_efsrpc_raw_data(inode, ctx);
		if (ret)
			goto out;
	} else {
		/*
		 * Load information about data streams (unnamed and named).
		 *
		 * Skip this step for encrypted files, since the data from
		 * ReadEncryptedFileRaw() already contains all data streams (and
		 * they do in fact all get restored by WriteEncryptedFileRaw().)
		 *
		 * Note: WIMGAPI (as of Windows 8.1) gets wrong and stores both
		 * the EFSRPC data and the named data stream(s)...!
		 */
		ret = winnt_scan_data_streams(h,
					      inode,
					      file_info.end_of_file,
					      ctx);
		if (ret)
			goto out;
	}

	if (unlikely(should_try_to_use_wimboot_hash(inode, ctx))) {
		ret = try_to_use_wimboot_hash(h, inode, ctx);
		if (ret)
			goto out;
	}

	set_sort_key(inode, sort_key);

	if (inode_is_directory(inode) && recursive) {

		/* Directory: recurse to children.  */

		/* Re-open the directory with FILE_LIST_DIRECTORY access.  */
		if (h) {
			NtClose(h);
			h = NULL;
		}
		status = winnt_openat(cur_dir, relative_path,
				      relative_path_nchars, FILE_LIST_DIRECTORY,
				      &h);
		if (!NT_SUCCESS(status)) {
			winnt_error(status, L"\"%ls\": Can't open directory",
				    printable_path(ctx));
			ret = WIMLIB_ERR_OPEN;
			goto out;
		}
		ret = winnt_recurse_directory(h, root, ctx);
		if (ret)
			goto out;
	}

out_progress:
	ret = 0;
	if (recursive) { /* if !recursive, caller handles progress */
		if (likely(root))
			ret = do_scan_progress(ctx->params,
					       WIMLIB_SCAN_DENTRY_OK, inode);
		else
			ret = do_scan_progress(ctx->params,
					       WIMLIB_SCAN_DENTRY_EXCLUDED,
					       NULL);
	}
out:
	if (likely(h))
		NtClose(h);
	if (unlikely(ret)) {
		free_dentry_tree(root, ctx->params->blob_table);
		root = NULL;
		ret = report_scan_error(ctx->params, ret);
	}
	*root_ret = root;
	return ret;
}

static void
winnt_do_scan_warnings(const wchar_t *path, const struct winnt_scan_ctx *ctx)
{
	if (likely(ctx->num_get_sacl_priv_notheld == 0 &&
		   ctx->num_get_sd_access_denied == 0))
		return;

	WARNING("Scan of \"%ls\" complete, but with one or more warnings:", path);
	if (ctx->num_get_sacl_priv_notheld != 0) {
		WARNING("- Could not capture SACL (System Access Control List)\n"
			"            on %lu files or directories.",
			ctx->num_get_sacl_priv_notheld);
	}
	if (ctx->num_get_sd_access_denied != 0) {
		WARNING("- Could not capture security descriptor at all\n"
			"            on %lu files or directories.",
			ctx->num_get_sd_access_denied);
	}
	WARNING("To fully capture all security descriptors, run the program\n"
		"          with Administrator rights.");
}

/*----------------------------------------------------------------------------*
 *                         Fast MFT scan implementation                       *
 *----------------------------------------------------------------------------*/

#define ENABLE_FAST_MFT_SCAN	1

#ifdef ENABLE_FAST_MFT_SCAN

#ifndef _MSC_VER
typedef struct {
	u64 StartingCluster;
	u64 ClusterCount;
} CLUSTER_RANGE;

typedef struct {
	u64 StartingFileReferenceNumber;
	u64 EndingFileReferenceNumber;
} FILE_REFERENCE_RANGE;

/* The FSCTL_QUERY_FILE_LAYOUT ioctl.  This ioctl can be used on Windows 8 and
 * later to scan the MFT of an NTFS volume.  */
#define FSCTL_QUERY_FILE_LAYOUT		CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 157, METHOD_NEITHER, FILE_ANY_ACCESS)

/* The input to FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	u32 NumberOfPairs;
#define QUERY_FILE_LAYOUT_RESTART					0x00000001
#define QUERY_FILE_LAYOUT_INCLUDE_NAMES					0x00000002
#define QUERY_FILE_LAYOUT_INCLUDE_STREAMS				0x00000004
#define QUERY_FILE_LAYOUT_INCLUDE_EXTENTS				0x00000008
#define QUERY_FILE_LAYOUT_INCLUDE_EXTRA_INFO				0x00000010
#define QUERY_FILE_LAYOUT_INCLUDE_STREAMS_WITH_NO_CLUSTERS_ALLOCATED	0x00000020
	u32 Flags;
#define QUERY_FILE_LAYOUT_FILTER_TYPE_NONE		0
#define QUERY_FILE_LAYOUT_FILTER_TYPE_CLUSTERS		1
#define QUERY_FILE_LAYOUT_FILTER_TYPE_FILEID		2
#define QUERY_FILE_LAYOUT_NUM_FILTER_TYPES		3
	u32 FilterType;
	u32 Reserved;
	union {
		CLUSTER_RANGE ClusterRanges[1];
		FILE_REFERENCE_RANGE FileReferenceRanges[1];
	} Filter;
} QUERY_FILE_LAYOUT_INPUT;

/* The header of the buffer returned by FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	u32 FileEntryCount;
	u32 FirstFileOffset;
#define QUERY_FILE_LAYOUT_SINGLE_INSTANCED				0x00000001
	u32 Flags;
	u32 Reserved;
} QUERY_FILE_LAYOUT_OUTPUT;

/* Inode information returned by FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	u32 Version;
	u32 NextFileOffset;
	u32 Flags;
	u32 FileAttributes;
	u64 FileReferenceNumber;
	u32 FirstNameOffset;
	u32 FirstStreamOffset;
	u32 ExtraInfoOffset;
	u32 Reserved;
} FILE_LAYOUT_ENTRY;

/* Extra inode information returned by FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	struct {
		u64 CreationTime;
		u64 LastAccessTime;
		u64 LastWriteTime;
		u64 ChangeTime;
		u32 FileAttributes;
	} BasicInformation;
	u32 OwnerId;
	u32 SecurityId;
	s64 Usn;
} FILE_LAYOUT_INFO_ENTRY;

/* Filename (or dentry) information returned by FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	u32 NextNameOffset;
#define FILE_LAYOUT_NAME_ENTRY_PRIMARY	0x00000001
#define FILE_LAYOUT_NAME_ENTRY_DOS	0x00000002
	u32 Flags;
	u64 ParentFileReferenceNumber;
	u32 FileNameLength;
	u32 Reserved;
	wchar_t FileName[1];
} FILE_LAYOUT_NAME_ENTRY;

/* Stream information returned by FSCTL_QUERY_FILE_LAYOUT  */
typedef struct {
	u32 Version;
	u32 NextStreamOffset;
#define STREAM_LAYOUT_ENTRY_IMMOVABLE			0x00000001
#define STREAM_LAYOUT_ENTRY_PINNED			0x00000002
#define STREAM_LAYOUT_ENTRY_RESIDENT			0x00000004
#define STREAM_LAYOUT_ENTRY_NO_CLUSTERS_ALLOCATED	0x00000008
	u32 Flags;
	u32 ExtentInformationOffset;
	u64 AllocationSize;
	u64 EndOfFile;
	u64 Reserved;
	u32 AttributeFlags;
	u32 StreamIdentifierLength;
	wchar_t StreamIdentifier[1];
} STREAM_LAYOUT_ENTRY;


typedef struct {
#define STREAM_EXTENT_ENTRY_AS_RETRIEVAL_POINTERS	0x00000001
#define STREAM_EXTENT_ENTRY_ALL_EXTENTS			0x00000002
	u32 Flags;
	union {
		RETRIEVAL_POINTERS_BUFFER RetrievalPointers;
	} ExtentInformation;
} STREAM_EXTENT_ENTRY;
#endif

/* Extract the MFT number part of the full inode number  */
#define NTFS_MFT_NO(ref)	((ref) & (((u64)1 << 48) - 1))

/* Is the file the root directory of the NTFS volume?  The root directory always
 * occupies MFT record 5.  */
#define NTFS_IS_ROOT_FILE(ino)	(NTFS_MFT_NO(ino) == 5)

/* Is the file a special NTFS file, other than the root directory?  The special
 * files are the first 16 records in the MFT.  */
#define NTFS_IS_SPECIAL_FILE(ino)			\
	(NTFS_MFT_NO(ino) <= 15 && !NTFS_IS_ROOT_FILE(ino))

#define NTFS_SPECIAL_STREAM_OBJECT_ID		0x00000001
#define NTFS_SPECIAL_STREAM_EA			0x00000002
#define NTFS_SPECIAL_STREAM_EA_INFORMATION	0x00000004

/* Intermediate inode structure.  This is used to temporarily save information
 * from FSCTL_QUERY_FILE_LAYOUT before creating the full 'struct wim_inode'.  */
struct ntfs_inode {
	struct avl_tree_node index_node;
	u64 ino;
	u64 creation_time;
	u64 last_access_time;
	u64 last_write_time;
	u64 starting_lcn;
	u32 attributes;
	u32 security_id;
	u32 num_aliases;
	u32 num_streams;
	u32 special_streams;
	u32 first_stream_offset;
	struct ntfs_dentry *first_child;
	wchar_t short_name[13];
};

/* Intermediate dentry structure.  This is used to temporarily save information
 * from FSCTL_QUERY_FILE_LAYOUT before creating the full 'struct wim_dentry'. */
struct ntfs_dentry {
	u32 offset_from_inode : 31;
	u32 is_primary : 1;
	union {
		/* Note: build_children_lists() replaces 'parent_ino' with
		 * 'next_child'.  */
		u64 parent_ino;
		struct ntfs_dentry *next_child;
	};
	wchar_t name[0];
};

/* Intermediate stream structure.  This is used to temporarily save information
 * from FSCTL_QUERY_FILE_LAYOUT before creating the full 'struct
 * wim_inode_stream'.  */
struct ntfs_stream {
	u64 size;
	wchar_t name[0];
};

/* Map of all known NTFS inodes, keyed by inode number  */
struct ntfs_inode_map {
	struct avl_tree_node *root;
};

#define NTFS_INODE(node)				\
	avl_tree_entry((node), struct ntfs_inode, index_node)

#define SKIP_ALIGNED(p, size)	(_PTR((p) + ALIGN((size), 8)))

/* Get a pointer to the first dentry of the inode.  */
#define FIRST_DENTRY(ni) SKIP_ALIGNED((ni), sizeof(struct ntfs_inode))

/* Get a pointer to the first stream of the inode.  */
#define FIRST_STREAM(ni) (_PTR(ni + ni->first_stream_offset))

/* Advance to the next dentry of the inode.  */
#define NEXT_DENTRY(nd)	 SKIP_ALIGNED((nd), sizeof(struct ntfs_dentry) +   \
				(wcslen((nd)->name) + 1) * sizeof(wchar_t))

/* Advance to the next stream of the inode.  */
#define NEXT_STREAM(ns)	 SKIP_ALIGNED((ns), sizeof(struct ntfs_stream) +   \
				(wcslen((ns)->name) + 1) * sizeof(wchar_t))

static int
_avl_cmp_ntfs_inodes(const struct avl_tree_node *node1,
		     const struct avl_tree_node *node2)
{
	return cmp_u64(NTFS_INODE(node1)->ino, NTFS_INODE(node2)->ino);
}

/* Adds an NTFS inode to the map.  */
static void
ntfs_inode_map_add_inode(struct ntfs_inode_map *map, struct ntfs_inode *ni)
{
	if (avl_tree_insert(&map->root, &ni->index_node, _avl_cmp_ntfs_inodes)) {
		WARNING("Inode 0x%016"PRIx64" is a duplicate!", ni->ino);
		FREE(ni);
	}
}

/* Find an ntfs_inode in the map by inode number.  Returns NULL if not found. */
static struct ntfs_inode *
ntfs_inode_map_lookup(struct ntfs_inode_map *map, u64 ino)
{
	struct ntfs_inode tmp;
	struct avl_tree_node *res;

	tmp.ino = ino;
	res = avl_tree_lookup_node(map->root, &tmp.index_node, _avl_cmp_ntfs_inodes);
	if (!res)
		return NULL;
	return NTFS_INODE(res);
}

/* Remove an ntfs_inode from the map and free it.  */
static void
ntfs_inode_map_remove(struct ntfs_inode_map *map, struct ntfs_inode *ni)
{
	avl_tree_remove(&map->root, &ni->index_node);
	FREE(ni);
}

/* Free all ntfs_inodes in the map.  */
static void
ntfs_inode_map_destroy(struct ntfs_inode_map *map)
{
	struct ntfs_inode *ni;

	avl_tree_for_each_in_postorder(ni, map->root, struct ntfs_inode, index_node)
		FREE(ni);
}

static bool
file_has_streams(const FILE_LAYOUT_ENTRY *file)
{
	return (file->FirstStreamOffset != 0) &&
		!(file->FileAttributes & FILE_ATTRIBUTE_ENCRYPTED);
}

static bool
is_valid_name_entry(const FILE_LAYOUT_NAME_ENTRY *name)
{
	return name->FileNameLength > 0 &&
		name->FileNameLength % 2 == 0 &&
		!wmemchr(name->FileName, L'\0', name->FileNameLength / 2) &&
		(!(name->Flags & FILE_LAYOUT_NAME_ENTRY_DOS) ||
		 name->FileNameLength <= 24);
}

/* Validate the FILE_LAYOUT_NAME_ENTRYs of the specified file and compute the
 * total length in bytes of the ntfs_dentry structures needed to hold the name
 * information.  */
static int
validate_names_and_compute_total_length(const FILE_LAYOUT_ENTRY *file,
					size_t *total_length_ret)
{
	const FILE_LAYOUT_NAME_ENTRY *name =
		_PTR(file + file->FirstNameOffset);
	size_t total = 0;
	size_t num_long_names = 0;

	for (;;) {
		if (unlikely(!is_valid_name_entry(name))) {
			ERROR("Invalid FILE_LAYOUT_NAME_ENTRY! "
			      "FileReferenceNumber=0x%016"PRIx64", "
			      "FileNameLength=%"PRIu32", "
			      "FileName=%.*ls, Flags=0x%08"PRIx32,
			      file->FileReferenceNumber,
			      name->FileNameLength,
			      (int)(name->FileNameLength / 2),
			      name->FileName, name->Flags);
			return WIMLIB_ERR_UNSUPPORTED;
		}
		if (name->Flags != FILE_LAYOUT_NAME_ENTRY_DOS) {
			num_long_names++;
			total += ALIGN(sizeof(struct ntfs_dentry) +
				       name->FileNameLength + sizeof(wchar_t),
				       8);
		}
		if (name->NextNameOffset == 0)
			break;
		name = _PTR(name + name->NextNameOffset);
	}

	if (unlikely(num_long_names == 0)) {
		ERROR("Inode 0x%016"PRIx64" has no long names!",
		      file->FileReferenceNumber);
		return WIMLIB_ERR_UNSUPPORTED;
	}

	*total_length_ret = total;
	return 0;
}

static bool
is_valid_stream_entry(const STREAM_LAYOUT_ENTRY *stream)
{
	return stream->StreamIdentifierLength % 2 == 0 &&
		!wmemchr(stream->StreamIdentifier , L'\0',
			 stream->StreamIdentifierLength / 2);
}

/* assumes that 'id' is a wide string literal */
#define stream_has_identifier(stream, id)				\
	((stream)->StreamIdentifierLength == sizeof(id) - 2 &&		\
	 !memcmp((stream)->StreamIdentifier, id, sizeof(id) - 2))
/*
 * If the specified STREAM_LAYOUT_ENTRY represents a DATA stream as opposed to
 * some other type of NTFS stream such as a STANDARD_INFORMATION stream, return
 * true and set *stream_name_ret and *stream_name_nchars_ret to specify just the
 * stream name.  For example, ":foo:$DATA" would become "foo" with length 3
 * characters.  Otherwise return false.
 */
static bool
use_stream(const FILE_LAYOUT_ENTRY *file, const STREAM_LAYOUT_ENTRY *stream,
	   const wchar_t **stream_name_ret, size_t *stream_name_nchars_ret)
{
	const wchar_t *stream_name;
	size_t stream_name_nchars;

	if (stream->StreamIdentifierLength == 0) {
		/* The unnamed data stream may be given as an empty string
		 * rather than as "::$DATA".  Handle it both ways.  */
		stream_name = L"";
		stream_name_nchars = 0;
	} else if (!get_data_stream_name(stream->StreamIdentifier,
					 stream->StreamIdentifierLength / 2,
					 &stream_name, &stream_name_nchars))
		return false;

	/* Skip the unnamed data stream for directories.  */
	if (stream_name_nchars == 0 &&
	    (file->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	*stream_name_ret = stream_name;
	*stream_name_nchars_ret = stream_name_nchars;
	return true;
}

/* Validate the STREAM_LAYOUT_ENTRYs of the specified file and compute the total
 * length in bytes of the ntfs_stream structures needed to hold the stream
 * information.  In addition, set *special_streams_ret to a bitmask of special
 * stream types that were found.  */
static int
validate_streams_and_compute_total_length(const FILE_LAYOUT_ENTRY *file,
					  size_t *total_length_ret,
					  u32 *special_streams_ret)
{
	const STREAM_LAYOUT_ENTRY *stream =
		_PTR(file + file->FirstStreamOffset);
	size_t total = 0;
	u32 special_streams = 0;

	for (;;) {
		const wchar_t *name;
		size_t name_nchars;

		if (unlikely(!is_valid_stream_entry(stream))) {
			WARNING("Invalid STREAM_LAYOUT_ENTRY! "
				"FileReferenceNumber=0x%016"PRIx64", "
				"StreamIdentifierLength=%"PRIu32", "
				"StreamIdentifier=%.*ls",
				file->FileReferenceNumber,
				stream->StreamIdentifierLength,
				(int)(stream->StreamIdentifierLength / 2),
				stream->StreamIdentifier);
			return WIMLIB_ERR_UNSUPPORTED;
		}

		if (use_stream(file, stream, &name, &name_nchars)) {
			total += ALIGN(sizeof(struct ntfs_stream) +
				       (name_nchars + 1) * sizeof(wchar_t), 8);
		} else if (stream_has_identifier(stream, L"::$OBJECT_ID")) {
			special_streams |= NTFS_SPECIAL_STREAM_OBJECT_ID;
		} else if (stream_has_identifier(stream, L"::$EA")) {
			special_streams |= NTFS_SPECIAL_STREAM_EA;
		} else if (stream_has_identifier(stream, L"::$EA_INFORMATION")) {
			special_streams |= NTFS_SPECIAL_STREAM_EA_INFORMATION;
		}
		if (stream->NextStreamOffset == 0)
			break;
		stream = _PTR(stream + stream->NextStreamOffset);
	}

	*total_length_ret = total;
	*special_streams_ret = special_streams;
	return 0;
}

static void *
load_name_information(const FILE_LAYOUT_ENTRY *file, struct ntfs_inode *ni,
		      void *p)
{
	const FILE_LAYOUT_NAME_ENTRY *name =
		_PTR(file + file->FirstNameOffset);
	for (;;) {
		struct ntfs_dentry *nd = p;
		/* Note that a name may be just a short (DOS) name, just a long
		 * name, or both a short name and a long name.  If there is a
		 * short name, one name should also be marked as "primary" to
		 * indicate which long name the short name is associated with.
		 * Also, there should be at most one short name per inode.  */
		if (name->Flags & FILE_LAYOUT_NAME_ENTRY_DOS) {
			memcpy(ni->short_name,
			       name->FileName, name->FileNameLength);
			ni->short_name[name->FileNameLength / 2] = L'\0';
		}
		if (name->Flags != FILE_LAYOUT_NAME_ENTRY_DOS) {
			ni->num_aliases++;
			nd->offset_from_inode = (u8 *)nd - (u8 *)ni;
			nd->is_primary = ((name->Flags &
					   FILE_LAYOUT_NAME_ENTRY_PRIMARY) != 0);
			nd->parent_ino = name->ParentFileReferenceNumber;
			memcpy(nd->name, name->FileName, name->FileNameLength);
			nd->name[name->FileNameLength / 2] = L'\0';
			p = _PTR(p + ALIGN(sizeof(struct ntfs_dentry) +
				   name->FileNameLength + sizeof(wchar_t), 8));
		}
		if (name->NextNameOffset == 0)
			break;
		name = _PTR(name + name->NextNameOffset);
	}
	return p;
}

static u64
load_starting_lcn(const STREAM_LAYOUT_ENTRY *stream)
{
	const STREAM_EXTENT_ENTRY *entry;

	if (stream->ExtentInformationOffset == 0)
		return 0;

	entry = _PTR(stream + stream->ExtentInformationOffset);

	if (!(entry->Flags & STREAM_EXTENT_ENTRY_AS_RETRIEVAL_POINTERS))
		return 0;

	return extract_starting_lcn(&entry->ExtentInformation.RetrievalPointers);
}

static void *
load_stream_information(const FILE_LAYOUT_ENTRY *file, struct ntfs_inode *ni,
			void *p)
{
	const STREAM_LAYOUT_ENTRY *stream =
		_PTR(file + file->FirstStreamOffset);
	const u32 first_stream_offset = (const u8 *)p - (const u8 *)ni;
	for (;;) {
		struct ntfs_stream *ns = p;
		const wchar_t *name;
		size_t name_nchars;

		if (use_stream(file, stream, &name, &name_nchars)) {
			ni->first_stream_offset = first_stream_offset;
			ni->num_streams++;
			if (name_nchars == 0)
				ni->starting_lcn = load_starting_lcn(stream);
#ifdef _MSC_VER
			ns->size = stream->EndOfFile.QuadPart;
#else
			ns->size = stream->EndOfFile;
#endif
			wmemcpy(ns->name, name, name_nchars);
			ns->name[name_nchars] = L'\0';
			p = _PTR(p + ALIGN(sizeof(struct ntfs_stream) +
				   (name_nchars + 1) * sizeof(wchar_t), 8));
		}
		if (stream->NextStreamOffset == 0)
			break;
		stream = _PTR(stream + stream->NextStreamOffset);
	}
	return p;
}

/* Process the information for a file given by FSCTL_QUERY_FILE_LAYOUT.  */
static int
load_one_file(const FILE_LAYOUT_ENTRY *file, struct ntfs_inode_map *inode_map)
{
	const FILE_LAYOUT_INFO_ENTRY *info =
		_PTR(file + file->ExtraInfoOffset);
	size_t inode_size;
	struct ntfs_inode *ni;
	size_t n;
	int ret;
	void *p;
	u32 special_streams = 0;

	inode_size = ALIGN(sizeof(struct ntfs_inode), 8);

	/* The root file should have no names, and all other files should have
	 * at least one name.  But just in case, we ignore the names of the root
	 * file, and we ignore any non-root file with no names.  */
	if (!NTFS_IS_ROOT_FILE(file->FileReferenceNumber)) {
		if (file->FirstNameOffset == 0)
			return 0;
		ret = validate_names_and_compute_total_length(file, &n);
		if (ret)
			return ret;
		inode_size += n;
	}

	if (file_has_streams(file)) {
		ret = validate_streams_and_compute_total_length(file, &n,
								&special_streams);
		if (ret)
			return ret;
		inode_size += n;
	}

	/* To save memory, we allocate the ntfs_dentry's and ntfs_stream's in
	 * the same memory block as their ntfs_inode.  */
	ni = CALLOC(1, inode_size);
	if (!ni)
		return WIMLIB_ERR_NOMEM;

	ni->ino = file->FileReferenceNumber;
	ni->attributes = info->BasicInformation.FileAttributes;
#ifdef _MSC_VER
	ni->creation_time = info->BasicInformation.CreationTime.QuadPart;
	ni->last_write_time = info->BasicInformation.LastWriteTime.QuadPart;
	ni->last_access_time = info->BasicInformation.LastAccessTime.QuadPart;
#else
	ni->creation_time = info->BasicInformation.CreationTime;
	ni->last_write_time = info->BasicInformation.LastWriteTime;
	ni->last_access_time = info->BasicInformation.LastAccessTime;
#endif
	ni->security_id = info->SecurityId;
	ni->special_streams = special_streams;

	p = FIRST_DENTRY(ni);

	if (!NTFS_IS_ROOT_FILE(file->FileReferenceNumber))
		p = load_name_information(file, ni, p);

	if (file_has_streams(file))
		p = load_stream_information(file, ni, p);

	wimlib_assert((u8 *)p - (u8 *)ni == inode_size);

	ntfs_inode_map_add_inode(inode_map, ni);
	return 0;
}

/*
 * Quickly find all files on an NTFS volume by using FSCTL_QUERY_FILE_LAYOUT to
 * scan the MFT.  The NTFS volume is specified by the NT namespace path @path.
 * For each file, allocate an 'ntfs_inode' structure for each file and add it to
 * 'inode_map' keyed by inode number.  Include NTFS special files such as
 * $Bitmap (they will be removed later).
 */
static int
load_files_from_mft(const wchar_t *path, struct ntfs_inode_map *inode_map)
{
	HANDLE h = NULL;
	QUERY_FILE_LAYOUT_INPUT in = (QUERY_FILE_LAYOUT_INPUT) {
		.NumberOfPairs = 0,
		.Flags = QUERY_FILE_LAYOUT_RESTART |
			 QUERY_FILE_LAYOUT_INCLUDE_NAMES |
			 QUERY_FILE_LAYOUT_INCLUDE_STREAMS |
			 QUERY_FILE_LAYOUT_INCLUDE_EXTENTS |
			 QUERY_FILE_LAYOUT_INCLUDE_EXTRA_INFO |
			 QUERY_FILE_LAYOUT_INCLUDE_STREAMS_WITH_NO_CLUSTERS_ALLOCATED,
		.FilterType = QUERY_FILE_LAYOUT_FILTER_TYPE_NONE,
	};
	size_t outsize = 32768;
	QUERY_FILE_LAYOUT_OUTPUT *out = NULL;
	int ret;
	NTSTATUS status;

	status = winnt_open(path, wcslen(path),
			    FILE_READ_DATA | FILE_READ_ATTRIBUTES, &h);
	if (!NT_SUCCESS(status)) {
		ret = -1; /* Silently try standard recursive scan instead  */
		goto out;
	}

	for (;;) {
		/* Allocate a buffer for the output of the ioctl.  */
		out = MALLOC(outsize);
		if (!out) {
			ret = WIMLIB_ERR_NOMEM;
			goto out;
		}

		/* Execute FSCTL_QUERY_FILE_LAYOUT until it fails.  */
		while (NT_SUCCESS(status = winnt_fsctl(h,
						       FSCTL_QUERY_FILE_LAYOUT,
						       &in, sizeof(in),
						       out, outsize, NULL)))
		{
			const FILE_LAYOUT_ENTRY *file =
				_PTR(out + out->FirstFileOffset);
			for (;;) {
				ret = load_one_file(file, inode_map);
				if (ret)
					goto out;
				if (file->NextFileOffset == 0)
					break;
				file = _PTR(file + file->NextFileOffset);
			}
			in.Flags &= ~QUERY_FILE_LAYOUT_RESTART;
		}

		/* Enlarge the buffer if needed.  */
		if (status != STATUS_BUFFER_TOO_SMALL)
			break;
		FREE(out);
		outsize *= 2;
	}

	/* Normally, FSCTL_QUERY_FILE_LAYOUT fails with STATUS_END_OF_FILE after
	 * all files have been enumerated.  */
	if (status != STATUS_END_OF_FILE) {
		if (status == STATUS_INVALID_DEVICE_REQUEST /* old OS */ ||
		    status == STATUS_NOT_SUPPORTED /* Samba volume, WinXP */ ||
		    status == STATUS_INVALID_PARAMETER /* not root directory */ )
		{
			/* Silently try standard recursive scan instead  */
			ret = -1;
		} else {
			winnt_error(status,
				    L"Error enumerating files on volume \"%ls\"",
				    path);
			/* Try standard recursive scan instead  */
			ret = WIMLIB_ERR_UNSUPPORTED;
		}
		goto out;
	}
	ret = 0;
out:
	FREE(out);
	NtClose(h);
	return ret;
}

/* Build the list of child dentries for each inode in @map.  This is done by
 * iterating through each name of each inode and adding it to its parent's
 * children list.  Note that every name should have a parent, i.e. should belong
 * to some directory.  The root directory does not have any names.  */
static int
build_children_lists(struct ntfs_inode_map *map, struct ntfs_inode **root_ret)
{
	struct ntfs_inode *ni;

	avl_tree_for_each_in_order(ni, map->root, struct ntfs_inode, index_node)
	{
		struct ntfs_dentry *nd;
		u32 n;

		if (NTFS_IS_ROOT_FILE(ni->ino)) {
			*root_ret = ni;
			continue;
		}

		n = ni->num_aliases;
		nd = FIRST_DENTRY(ni);
		for (;;) {
			struct ntfs_inode *parent;

			parent = ntfs_inode_map_lookup(map, nd->parent_ino);
			if (unlikely(!parent)) {
				ERROR("Parent inode 0x%016"PRIx64" of"
				      "directory entry \"%ls\" (inode "
				      "0x%016"PRIx64") was missing from the "
				      "MFT listing!",
				      nd->parent_ino, nd->name, ni->ino);
				return WIMLIB_ERR_UNSUPPORTED;
			}
			nd->next_child = parent->first_child;
			parent->first_child = nd;
			if (!--n)
				break;
			nd = NEXT_DENTRY(nd);
		}
	}
	return 0;
}

struct security_map_node {
	struct avl_tree_node index_node;
	u32 disk_security_id;
	u32 wim_security_id;
};

/* Map from disk security IDs to WIM security IDs  */
struct security_map {
	struct avl_tree_node *root;
};

#define SECURITY_MAP_NODE(node)				\
	avl_tree_entry((node), struct security_map_node, index_node)

static int
_avl_cmp_security_map_nodes(const struct avl_tree_node *node1,
			    const struct avl_tree_node *node2)
{
	return cmp_u32(SECURITY_MAP_NODE(node1)->disk_security_id,
		       SECURITY_MAP_NODE(node2)->disk_security_id);
}

static s32
security_map_lookup(struct security_map *map, u32 disk_security_id)
{
	struct security_map_node tmp;
	const struct avl_tree_node *res;

	if (disk_security_id == 0)  /* No on-disk security ID; uncacheable  */
		return -1;

	tmp.disk_security_id = disk_security_id;
	res = avl_tree_lookup_node(map->root, &tmp.index_node,
				   _avl_cmp_security_map_nodes);
	if (!res)
		return -1;
	return SECURITY_MAP_NODE(res)->wim_security_id;
}

static int
security_map_insert(struct security_map *map, u32 disk_security_id,
		    u32 wim_security_id)
{
	struct security_map_node *node;

	if (disk_security_id == 0)  /* No on-disk security ID; uncacheable  */
		return 0;

	node = MALLOC(sizeof(*node));
	if (!node)
		return WIMLIB_ERR_NOMEM;

	node->disk_security_id = disk_security_id;
	node->wim_security_id = wim_security_id;
	avl_tree_insert(&map->root, &node->index_node,
			_avl_cmp_security_map_nodes);
	return 0;
}

static void
security_map_destroy(struct security_map *map)
{
	struct security_map_node *node;

	avl_tree_for_each_in_postorder(node, map->root,
				       struct security_map_node, index_node)
		FREE(node);
}

/*
 * Turn our temporary NTFS structures into the final WIM structures:
 *
 *	ntfs_inode	=> wim_inode
 *	ntfs_dentry	=> wim_dentry
 *	ntfs_stream	=> wim_inode_stream
 *
 * This also handles things such as exclusions and issuing progress messages.
 * It's similar to winnt_build_dentry_tree(), but this is much faster because
 * almost all information we need is already loaded in memory in the ntfs_*
 * structures.  However, in some cases we still fall back to
 * winnt_build_dentry_tree() and/or opening the file.
 */
static int
generate_wim_structures_recursive(struct wim_dentry **root_ret,
				  const wchar_t *filename, bool is_primary_name,
				  struct ntfs_inode *ni,
				  struct winnt_scan_ctx *ctx,
				  struct ntfs_inode_map *inode_map,
				  struct security_map *security_map)
{
	int ret = 0;
	struct wim_dentry *root = NULL;
	struct wim_inode *inode = NULL;
	const struct ntfs_stream *ns;

	/* Completely ignore NTFS special files.  */
	if (NTFS_IS_SPECIAL_FILE(ni->ino))
		goto out;

	/* Fall back to the standard scan for unhandled cases.  Reparse points,
	 * in particular, can't be properly handled here because a commonly used
	 * filter driver (WOF) hides reparse points from regular filesystem APIs
	 * but not from FSCTL_QUERY_FILE_LAYOUT.  */
	if (ni->attributes & (FILE_ATTRIBUTE_REPARSE_POINT |
			      FILE_ATTRIBUTE_ENCRYPTED) ||
	    ni->special_streams != 0)
	{
		ret = winnt_build_dentry_tree(&root, NULL,
					      ctx->params->cur_path,
					      ctx->params->cur_path_nchars,
					      filename, ctx, false);
		if (ret) /* Error? */
			goto out;
		if (!root) /* Excluded? */
			goto out_progress;
		inode = root->d_inode;
		goto process_children;
	}

	/* Test for exclusion based on path.  */
	ret = try_exclude(ctx->params);
	if (unlikely(ret < 0)) /* Excluded? */
		goto out_progress;
	if (unlikely(ret > 0)) /* Error? */
		goto out;

	/* Create the WIM dentry and possibly a new WIM inode  */
	ret = inode_table_new_dentry(ctx->params->inode_table, filename,
				     ni->ino, ctx->params->capture_root_dev,
				     false, &root);
	if (ret)
		goto out;

	inode = root->d_inode;

	/* Set the short name if needed.  */
	if (is_primary_name && *ni->short_name) {
		size_t nbytes = wcslen(ni->short_name) * sizeof(wchar_t);
		root->d_short_name = memdup(ni->short_name,
					    nbytes + sizeof(wchar_t));
		if (!root->d_short_name) {
			ret = WIMLIB_ERR_NOMEM;
			goto out;
		}
		root->d_short_name_nbytes = nbytes;
	}

	if (inode->i_nlink > 1) /* Already seen this inode?  */
		goto out_progress;

	/* The file attributes and timestamps were cached from the MFT.  */
	inode->i_attributes = ni->attributes;
	inode->i_creation_time = ni->creation_time;
	inode->i_last_write_time = ni->last_write_time;
	inode->i_last_access_time = ni->last_access_time;

	/* Set the security descriptor if needed.  */
	if (!(ctx->params->add_flags & WIMLIB_ADD_FLAG_NO_ACLS)) {
		/* Look up the WIM security ID that corresponds to the on-disk
		 * security ID.  */
		s32 wim_security_id =
			security_map_lookup(security_map, ni->security_id);
		if (likely(wim_security_id >= 0)) {
			/* The mapping for this security ID is already cached.*/
			inode->i_security_id = wim_security_id;
		} else {
			HANDLE h;
			NTSTATUS status;

			/* Create a mapping for this security ID and insert it
			 * into the security map.  */

			status = winnt_open(ctx->params->cur_path,
					    ctx->params->cur_path_nchars,
					    READ_CONTROL |
						ACCESS_SYSTEM_SECURITY, &h);
			if (!NT_SUCCESS(status)) {
				winnt_error(status, L"Can't open \"%ls\" to "
					    "read security descriptor",
					    printable_path(ctx));
				ret = WIMLIB_ERR_OPEN;
				goto out;
			}
			ret = winnt_load_security_descriptor(h, inode, ctx);
			NtClose(h);
			if (ret)
				goto out;

			ret = security_map_insert(security_map, ni->security_id,
						  inode->i_security_id);
			if (ret)
				goto out;
		}
	}

	/* Add data streams based on the cached information from the MFT.  */
	ns = FIRST_STREAM(ni);
	for (u32 i = 0; i < ni->num_streams; i++) {
		struct windows_file *windows_file;

		/* Reference the stream by path if it's a named data stream, or
		 * if the volume doesn't support "open by file ID", or if the
		 * application hasn't explicitly opted in to "open by file ID".
		 * Otherwise, only save the inode number (file ID).  */
		if (*ns->name ||
		    !(ctx->vol_flags & FILE_SUPPORTS_OPEN_BY_FILE_ID) ||
		    !(ctx->params->add_flags & WIMLIB_ADD_FLAG_FILE_PATHS_UNNEEDED))
		{
			windows_file = alloc_windows_file(ctx->params->cur_path,
							  ctx->params->cur_path_nchars,
							  ns->name,
							  wcslen(ns->name),
							  ctx->snapshot,
							  false);
		} else {
			windows_file = alloc_windows_file_for_file_id(ni->ino,
								      ctx->params->cur_path,
								      ctx->params->root_path_nchars,
								      ctx->snapshot);
		}

		ret = add_stream(inode, windows_file, ns->size,
				 STREAM_TYPE_DATA, ns->name,
				 ctx->params->unhashed_blobs);
		if (ret)
			goto out;
		ns = NEXT_STREAM(ns);
	}

	set_sort_key(inode, ni->starting_lcn);

	/* If processing a directory, then recurse to its children.  In this
	 * version there is no need to go to disk, as we already have the list
	 * of children cached from the MFT.  */
process_children:
	if (inode_is_directory(inode)) {
		const struct ntfs_dentry *nd = ni->first_child;

		while (nd != NULL) {
			size_t orig_path_nchars;
			struct wim_dentry *child;
			const struct ntfs_dentry *next = nd->next_child;

			ret = WIMLIB_ERR_NOMEM;
			if (!pathbuf_append_name(ctx->params, nd->name,
						 wcslen(nd->name),
						 &orig_path_nchars))
				goto out;

			ret = generate_wim_structures_recursive(
					&child,
					nd->name,
					nd->is_primary,
					_PTR(nd - nd->offset_from_inode),
					ctx,
					inode_map,
					security_map);

			pathbuf_truncate(ctx->params, orig_path_nchars);

			if (ret)
				goto out;

			attach_scanned_tree(root, child, ctx->params->blob_table);
			nd = next;
		}
	}

out_progress:
	if (likely(root))
		ret = do_scan_progress(ctx->params, WIMLIB_SCAN_DENTRY_OK, inode);
	else
		ret = do_scan_progress(ctx->params, WIMLIB_SCAN_DENTRY_EXCLUDED, NULL);
out:
	if (--ni->num_aliases == 0) {
		/* Memory usage optimization: when we don't need the ntfs_inode
		 * (and its names and streams) anymore, free it.  */
		ntfs_inode_map_remove(inode_map, ni);
	}
	if (unlikely(ret)) {
		free_dentry_tree(root, ctx->params->blob_table);
		root = NULL;
	}
	*root_ret = root;
	return ret;
}

static int
winnt_build_dentry_tree_fast(struct wim_dentry **root_ret,
			     struct winnt_scan_ctx *ctx)
{
	struct ntfs_inode_map inode_map = { .root = NULL };
	struct security_map security_map = { .root = NULL };
	struct ntfs_inode *root = NULL;
	wchar_t *path = ctx->params->cur_path;
	size_t path_nchars = ctx->params->cur_path_nchars;
	bool adjust_path;
	int ret;

	adjust_path = (path[path_nchars - 1] == L'\\');
	if (adjust_path)
		path[path_nchars - 1] = L'\0';

	ret = load_files_from_mft(path, &inode_map);

	if (adjust_path)
		path[path_nchars - 1] = L'\\';

	if (ret)
		goto out;

	ret = build_children_lists(&inode_map, &root);
	if (ret)
		goto out;

	if (!root) {
		ERROR("The MFT listing for volume \"%ls\" did not include a "
		      "root directory!", path);
		ret = WIMLIB_ERR_UNSUPPORTED;
		goto out;
	}

	root->num_aliases = 1;

	ret = generate_wim_structures_recursive(root_ret, L"", false, root, ctx,
						&inode_map, &security_map);
out:
	ntfs_inode_map_destroy(&inode_map);
	security_map_destroy(&security_map);
	return ret;
}

#endif /* ENABLE_FAST_MFT_SCAN */

/*----------------------------------------------------------------------------*
 *                 Entry point for directory tree scans on Windows            *
 *----------------------------------------------------------------------------*/

int
win32_build_dentry_tree(struct wim_dentry **root_ret,
			const wchar_t *root_disk_path,
			struct scan_params *params)
{
	struct winnt_scan_ctx ctx = { .params = params };
	UNICODE_STRING ntpath;
	HANDLE h = NULL;
	NTSTATUS status;
	int ret;

	if (params->add_flags & WIMLIB_ADD_FLAG_SNAPSHOT)
		ret = vss_create_snapshot(root_disk_path, &ntpath, &ctx.snapshot);
	else
		ret = win32_path_to_nt_path(root_disk_path, &ntpath);

	if (ret)
		goto out;

	if (ntpath.Length < 4 * sizeof(wchar_t) ||
	    wmemcmp(ntpath.Buffer, L"\\??\\", 4))
	{
		ERROR("\"%ls\": unrecognized path format", root_disk_path);
		ret = WIMLIB_ERR_INVALID_PARAM;
	} else {
		ret = pathbuf_init(params, ntpath.Buffer);
	}
	HeapFree(GetProcessHeap(), 0, ntpath.Buffer);
	if (ret)
		goto out;

	status = winnt_open(params->cur_path, params->cur_path_nchars,
			    FILE_READ_ATTRIBUTES, &h);
	if (!NT_SUCCESS(status)) {
		winnt_error(status, L"Can't open \"%ls\"", root_disk_path);
		if (status == STATUS_FVE_LOCKED_VOLUME)
			ret = WIMLIB_ERR_FVE_LOCKED_VOLUME;
		else
			ret = WIMLIB_ERR_OPEN;
		goto out;
	}

	get_volume_information(h, &ctx);

	NtClose(h);

#ifdef ENABLE_FAST_MFT_SCAN
	if (ctx.is_ntfs && !_wgetenv(L"WIMLIB_DISABLE_QUERY_FILE_LAYOUT")) {
		ret = winnt_build_dentry_tree_fast(root_ret, &ctx);
		if (ret >= 0 && ret != WIMLIB_ERR_UNSUPPORTED)
			goto out;
		if (ret >= 0) {
			WARNING("A problem occurred during the fast MFT scan.\n"
				"          Falling back to the standard "
				"recursive directory tree scan.");
		}
	}
#endif
	ret = winnt_build_dentry_tree(root_ret, NULL, params->cur_path,
				      params->cur_path_nchars, L"", &ctx, true);
out:
	vss_put_snapshot(ctx.snapshot);
	if (ret == 0)
		winnt_do_scan_warnings(root_disk_path, &ctx);
	return ret;
}

#endif /* _WIN32 */
