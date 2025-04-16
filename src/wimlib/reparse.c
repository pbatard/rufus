/*
 * reparse.c - Reparse point handling
 */

/*
 * Copyright (C) 2012, 2013, 2015 Eric Biggers
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

#include "wimlib/alloca.h"
#include "wimlib/blob_table.h"
#include "wimlib/endianness.h"
#include "wimlib/encoding.h"
#include "wimlib/error.h"
#include "wimlib/guid.h"
#include "wimlib/inode.h"
#include "wimlib/reparse.h"
#include "wimlib/resource.h"

/*
 * Reconstruct the header of a reparse point buffer.  This is necessary because
 * only reparse data is stored in WIM files.  The reparse tag is instead stored
 * in the on-disk WIM dentry, and the reparse data length is equal to the size
 * of the blob in which the reparse data was stored, minus the size of a GUID
 * (16 bytes) if the reparse tag does not have the "Microsoft" bit set.
 */
void
complete_reparse_point(struct reparse_buffer_disk *rpbuf,
		       const struct wim_inode *inode, u16 blob_size)
{
	rpbuf->rptag = cpu_to_le32(inode->i_reparse_tag);
	if (blob_size >= GUID_SIZE && !(inode->i_reparse_tag & 0x80000000))
		blob_size -= GUID_SIZE;
	rpbuf->rpdatalen = cpu_to_le16(blob_size);
	rpbuf->rpreserved = cpu_to_le16(inode->i_rp_reserved);
}

/* Parse the buffer for a symbolic link or junction reparse point and fill in a
 * 'struct link_reparse_point'.  */
int
parse_link_reparse_point(const struct reparse_buffer_disk *rpbuf, u16 rpbuflen,
			 struct link_reparse_point *link)
{
	u16 substitute_name_offset;
	u16 print_name_offset;
	const u8 *data;

	link->rptag = le32_to_cpu(rpbuf->rptag);

	/* Not a symbolic link or junction?  */
	if (link->rptag != WIM_IO_REPARSE_TAG_SYMLINK &&
	    link->rptag != WIM_IO_REPARSE_TAG_MOUNT_POINT)
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	/* Is the buffer too small to be a symlink or a junction?  */
	if (rpbuflen < offsetof(struct reparse_buffer_disk, link.junction.data))
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	link->rpreserved = le16_to_cpu(rpbuf->rpreserved);
	link->substitute_name_nbytes = le16_to_cpu(rpbuf->link.substitute_name_nbytes);
	substitute_name_offset = le16_to_cpu(rpbuf->link.substitute_name_offset);
	link->print_name_nbytes = le16_to_cpu(rpbuf->link.print_name_nbytes);
	print_name_offset = le16_to_cpu(rpbuf->link.print_name_offset);

	/* The names must be properly sized and aligned.  */
	if ((substitute_name_offset | print_name_offset |
	     link->substitute_name_nbytes | link->print_name_nbytes) & 1)
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	if (link->rptag == WIM_IO_REPARSE_TAG_SYMLINK) {
		if (rpbuflen < offsetof(struct reparse_buffer_disk, link.symlink.data))
			return WIMLIB_ERR_INVALID_REPARSE_DATA;
		link->symlink_flags = le32_to_cpu(rpbuf->link.symlink.flags);
		data = rpbuf->link.symlink.data;
	} else {
		data = rpbuf->link.junction.data;
	}

	/* Verify that the names don't overflow the buffer.  */
	if ((data - (const u8 *)rpbuf) + substitute_name_offset +
	    link->substitute_name_nbytes > rpbuflen)
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	if ((data - (const u8 *)rpbuf) + print_name_offset +
	    link->print_name_nbytes > rpbuflen)
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	/* Save the name pointers.  */
	link->substitute_name = (utf16lechar *)&data[substitute_name_offset];
	link->print_name = (utf16lechar *)&data[print_name_offset];
	return 0;
}

/* Translate a 'struct link_reparse_point' into a reparse point buffer.  */
int
make_link_reparse_point(const struct link_reparse_point *link,
			struct reparse_buffer_disk *rpbuf, u16 *rpbuflen_ret)
{
	u8 *data;

	if (link->rptag == WIM_IO_REPARSE_TAG_SYMLINK)
		data = rpbuf->link.symlink.data;
	else if (link->rptag == WIM_IO_REPARSE_TAG_MOUNT_POINT)
		data = rpbuf->link.junction.data;
	else /* Callers should forbid this case, but check anyway.  */
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	/* Check if the names are too long to fit in a reparse point.  */
	if ((data - (u8 *)rpbuf) + link->substitute_name_nbytes +
	    link->print_name_nbytes +
	    2 * sizeof(utf16lechar) > REPARSE_POINT_MAX_SIZE)
		return WIMLIB_ERR_INVALID_REPARSE_DATA;

	rpbuf->rptag = cpu_to_le32(link->rptag);
	rpbuf->rpreserved = cpu_to_le16(link->rpreserved);
	rpbuf->link.substitute_name_offset = cpu_to_le16(0);
	rpbuf->link.substitute_name_nbytes = cpu_to_le16(link->substitute_name_nbytes);
	rpbuf->link.print_name_offset = cpu_to_le16(link->substitute_name_nbytes +
						    sizeof(utf16lechar));
	rpbuf->link.print_name_nbytes = cpu_to_le16(link->print_name_nbytes);

	if (link->rptag == WIM_IO_REPARSE_TAG_SYMLINK)
		rpbuf->link.symlink.flags = cpu_to_le32(link->symlink_flags);

	/* We null-terminate the substitute and print names, although this isn't
	 * strictly necessary.  Note that the nbytes fields do not include the
	 * null terminators.  */
	data = mempcpy(data, link->substitute_name, link->substitute_name_nbytes);
	*(utf16lechar *)data = cpu_to_le16(0);
	data += sizeof(utf16lechar);
	data = mempcpy(data, link->print_name, link->print_name_nbytes);
	*(utf16lechar *)data = cpu_to_le16(0);
	data += sizeof(utf16lechar);
	rpbuf->rpdatalen = cpu_to_le16(data - rpbuf->rpdata);

	*rpbuflen_ret = data - (u8 *)rpbuf;
	return 0;
}

/* UNIX symlink <=> Windows reparse point translation  */
#ifndef _WIN32

/* Retrieve the inode's reparse point buffer into @rpbuf and @rpbuflen_ret.
 * This gets the reparse data from @blob if specified, otherwise from the
 * inode's reparse point stream.  The inode's streams must be resolved.  */
static int
wim_inode_get_reparse_point(const struct wim_inode *inode,
			    struct reparse_buffer_disk *rpbuf,
			    u16 *rpbuflen_ret,
			    const struct blob_descriptor *blob)
{
	int ret;
	u16 blob_size = 0;

	if (!blob) {
		const struct wim_inode_stream *strm;

		strm = inode_get_unnamed_stream(inode, STREAM_TYPE_REPARSE_POINT);
		if (strm)
			blob = stream_blob_resolved(strm);
	}

	if (blob) {
		if (blob->size > REPARSE_DATA_MAX_SIZE)
			return WIMLIB_ERR_INVALID_REPARSE_DATA;
		blob_size = blob->size;
		ret = read_blob_into_buf(blob, rpbuf->rpdata);
		if (ret)
			return ret;
	}

	complete_reparse_point(rpbuf, inode, blob_size);

	*rpbuflen_ret = REPARSE_DATA_OFFSET + blob_size;
	return 0;
}

static void
copy(char **buf_p, size_t *bufsize_p, const char *src, size_t src_size)
{
	size_t n = min(*bufsize_p, src_size);
	memcpy(*buf_p, src, n);
	*buf_p += n;
	*bufsize_p -= n;
}

/*
 * Get a UNIX-style symlink target from the WIM inode for a reparse point.
 *
 * @inode
 *	The inode from which to read the symlink.  If not a symbolic link or
 *	junction reparse point, then -EINVAL will be returned.
 * @buf
 *	Buffer into which to place the link target.
 * @bufsize
 *	Available space in @buf, in bytes.
 * @blob
 *	If not NULL, the blob from which to read the reparse data.  Otherwise,
 *	the reparse data will be read from the reparse point stream of @inode.
 * @altroot
 *	If @altroot_len != 0 and the link is an absolute link that was stored as
 *	"fixed", then prepend this path to the link target.
 * @altroot_len
 *	Length of the @altroot string or 0.
 *
 * Similar to POSIX readlink(), this function writes as much of the symlink
 * target as possible (up to @bufsize bytes) to @buf with no null terminator and
 * returns the number of bytes written or a negative errno value on error.  Note
 * that the target is truncated and @bufsize is returned in the overflow case.
 */
int
wim_inode_readlink(const struct wim_inode *inode, char *buf, size_t bufsize,
		   const struct blob_descriptor *blob,
		   const char *altroot, size_t altroot_len)
{
	struct reparse_buffer_disk rpbuf;
	u16 rpbuflen;
	struct link_reparse_point link;
	char *target_buffer;
	char *target;
	size_t target_len;
	char *buf_ptr;
	bool rpfix_ok = false;

	/* Not a symbolic link or junction?  */
	if (!inode_is_symlink(inode))
		return -EINVAL;

	/* Retrieve the native Windows "substitute name".  */

	if (wim_inode_get_reparse_point(inode, &rpbuf, &rpbuflen, blob))
		return -EIO;

	if (parse_link_reparse_point(&rpbuf, rpbuflen, &link))
		return -EINVAL;

	/* Translate the substitute name to a multibyte string.  */
	if (utf16le_to_tstr(link.substitute_name, link.substitute_name_nbytes,
			    &target_buffer, &target_len))
		return -errno;
	target = target_buffer;

	/*
	 * The substitute name is a native Windows NT path. There are two cases:
	 *
	 * 1. The reparse point is a symlink (rptag=WIM_IO_REPARSE_TAG_SYMLINK)
	 *    and SYMBOLIC_LINK_RELATIVE is set.  Windows resolves the path
	 *    relative to the directory containing the reparse point file.  In
	 *    this case, we just translate the path separators.
	 * 2. Otherwise, Windows resolves the path from the root of the Windows
	 *    NT kernel object namespace.  In this case, we attempt to strip the
	 *    device name, in addition to translating the path separators; e.g.
	 *    "\??\C:\Users\Public" is translated to "/Users/Public".
	 *
	 * Also in case (2) the link target may have been stored as "fixed",
	 * meaning that with the device portion stripped off it is effectively
	 * "relative to the root of the WIM image".  If this is the case, and if
	 * the caller provided an alternate root directory, then rewrite the
	 * link to be relative to that directory.
	 */
	if (!link_is_relative_symlink(&link)) {
		static const char *const nt_root_dirs[] = {
			"\\??\\", "\\DosDevices\\", "\\Device\\",
		};
		for (size_t i = 0; i < ARRAY_LEN(nt_root_dirs); i++) {
			size_t len = strlen(nt_root_dirs[i]);
			if (!strncmp(target, nt_root_dirs[i], len)) {
				char *p = target + len;
				while (*p == '\\')
					p++;
				while (*p && *p != '\\')
					p++;
				target_len -= (p - target);
				target = p;
				break;
			}
		}

		if (!(inode->i_rp_flags & WIM_RP_FLAG_NOT_FIXED))
			rpfix_ok = true;
	}

	/* Translate backslashes (Windows NT path separator) to forward slashes
	 * (UNIX path separator).  In addition, translate forwards slashes to
	 * backslashes; this enables lossless handling of UNIX symbolic link
	 * targets that contain the backslash character.  */
	for (char *p = target; *p; p++) {
		if (*p == '\\')
			*p = '/';
		else if (*p == '/')
			*p = '\\';
	}

	/* Copy as much of the link target as possible to the output buffer and
	 * return the number of bytes copied.  */
	buf_ptr = buf;
	if (rpfix_ok && altroot_len != 0) {
		copy(&buf_ptr, &bufsize, altroot, altroot_len);
	} else if (target_len == 0) {
		/* An absolute link target that was made relative to the same
		 * directory pointed to will end up empty if the original target
		 * did not have a trailing slash.  Here, we are reading this
		 * adjusted link target without prefixing it.  This usually
		 * doesn't happen, but if it does then we need to change it to
		 * "/" so that it is a valid target.  */
		target = "/";
		target_len = 1;
	}
	copy(&buf_ptr, &bufsize, target, target_len);
	FREE(target_buffer);
	return buf_ptr - buf;
}

/* Given a UNIX-style symbolic link target, create a Windows-style reparse point
 * buffer and assign it to the specified inode.  */
int
wim_inode_set_symlink(struct wim_inode *inode, const char *_target,
		      struct blob_table *blob_table)

{
	int ret;
	utf16lechar *target;
	size_t target_nbytes;
	struct link_reparse_point link;
	struct reparse_buffer_disk rpbuf;
	u16 rpbuflen;

	/* Translate the link target to UTF-16LE.  */
	ret = tstr_to_utf16le(_target, strlen(_target), &target, &target_nbytes);
	if (ret)
		return ret;

	/* Translate forward slashes (UNIX path separator) to backslashes
	 * (Windows NT path separator).  In addition, translate backslashes to
	 * forward slashes; this enables lossless handling of UNIX symbolic link
	 * targets that contain the backslash character.  */
	for (utf16lechar *p = target; *p; p++) {
		if (*p == cpu_to_le16('/'))
			*p = cpu_to_le16('\\');
		else if (*p == cpu_to_le16('\\'))
			*p = cpu_to_le16('/');
	}

	link.rptag = WIM_IO_REPARSE_TAG_SYMLINK;
	link.rpreserved = 0;

	/* Note: an absolute link that was rewritten to be relative to another
	 * directory is assumed to either be empty or to have a leading slash.
	 * See unix_relativize_link_target().  */
	if (*target == cpu_to_le16('\\') || !*target) {
		/*
		 * UNIX link target was absolute.  In this case we represent the
		 * link as a symlink reparse point with SYMBOLIC_LINK_RELATIVE
		 * cleared.  For this to work we need to assign it a path that
		 * can be resolved from the root of the Windows NT kernel object
		 * namespace.  We do this by using "\??\C:" as a dummy prefix.
		 *
		 * Note that we could instead represent UNIX absolute links by
		 * setting SYMBOLIC_LINK_RELATIVE and then leaving the path
		 * backslash-prefixed like "\Users\Public".  On Windows this is
		 * valid and denotes a path relative to the root of the
		 * filesystem on which the reparse point resides.  The problem
		 * with this is that neither WIMGAPI nor wimlib (on Windows)
		 * will do "reparse point fixups" when extracting such links
		 * (modifying the link target to point into the actual
		 * extraction directory).  So for the greatest cross-platform
		 * consistency, we have to use the fake C: drive approach.
		 */
		static const utf16lechar prefix[6] = {
			cpu_to_le16('\\'),
			cpu_to_le16('?'),
			cpu_to_le16('?'),
			cpu_to_le16('\\'),
			cpu_to_le16('C'),
			cpu_to_le16(':'),
		};

		/* Do not show \??\ in print name  */
		const size_t num_unprintable_chars = 4;

		link.symlink_flags = 0;
		link.substitute_name_nbytes = sizeof(prefix) + target_nbytes;
		link.substitute_name = alloca(link.substitute_name_nbytes);
		memcpy(link.substitute_name, prefix, sizeof(prefix));
		memcpy(link.substitute_name + ARRAY_LEN(prefix), target, target_nbytes);
		link.print_name_nbytes = link.substitute_name_nbytes -
					 (num_unprintable_chars * sizeof(utf16lechar));
		link.print_name = link.substitute_name + num_unprintable_chars;
	} else {
		/* UNIX link target was relative.  In this case we represent the
		 * link as a symlink reparse point with SYMBOLIC_LINK_RELATIVE
		 * set.  This causes Windows to interpret the link relative to
		 * the directory containing the reparse point file.  */
		link.symlink_flags = SYMBOLIC_LINK_RELATIVE;
		link.substitute_name_nbytes = target_nbytes;
		link.substitute_name = target;
		link.print_name_nbytes = target_nbytes;
		link.print_name = target;
	}

	/* Generate the reparse buffer.  */
	ret = make_link_reparse_point(&link, &rpbuf, &rpbuflen);
	if (ret)
		goto out_free_target;

	/* Save the reparse data with the inode.  */
	ret = WIMLIB_ERR_NOMEM;
	if (!inode_add_stream_with_data(inode,
					STREAM_TYPE_REPARSE_POINT,
					NO_STREAM_NAME,
					rpbuf.rpdata,
					rpbuflen - REPARSE_DATA_OFFSET,
					blob_table))
		goto out_free_target;

	/* The inode is now a reparse point.  */
	inode->i_reparse_tag = link.rptag;
	inode->i_attributes &= ~FILE_ATTRIBUTE_NORMAL;
	inode->i_attributes |= FILE_ATTRIBUTE_REPARSE_POINT;

	ret = 0;
out_free_target:
	FREE(target);
	return ret;
}

#endif /* !_WIN32 */
