/*
 * scan.c - Helper routines for directory tree scans
 */

/*
 * Copyright (C) 2013-2017 Eric Biggers
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

#include <string.h>

#include "wimlib/blob_table.h"
#include "wimlib/dentry.h"
#include "wimlib/error.h"
#include "wimlib/paths.h"
#include "wimlib/pattern.h"
#include "wimlib/progress.h"
#include "wimlib/scan.h"
#include "wimlib/textfile.h"

/*
 * Tally a file (or directory) that has been scanned for a capture operation,
 * and possibly call the progress function provided by the library user.
 *
 * @params
 *	Current path, flags, optional progress function, and progress data for
 *	the scan operation.
 * @status
 *	Status of the scanned file.
 * @inode
 *	If @status is WIMLIB_SCAN_DENTRY_OK, this is a pointer to the WIM inode
 *	that has been created for the scanned file.  The first time the file is
 *	seen, inode->i_nlink will be 1.  On subsequent visits of the same inode
 *	via additional hard links, inode->i_nlink will be greater than 1.
 */
int
do_scan_progress(struct scan_params *params, int status,
		 const struct wim_inode *inode)
{
	int ret;
	tchar *cookie;

	switch (status) {
	case WIMLIB_SCAN_DENTRY_OK:
		if (!(params->add_flags & WIMLIB_ADD_FLAG_VERBOSE))
			return 0;
		break;
	case WIMLIB_SCAN_DENTRY_UNSUPPORTED:
	case WIMLIB_SCAN_DENTRY_EXCLUDED:
	case WIMLIB_SCAN_DENTRY_FIXED_SYMLINK:
	case WIMLIB_SCAN_DENTRY_NOT_FIXED_SYMLINK:
		if (!(params->add_flags & WIMLIB_ADD_FLAG_EXCLUDE_VERBOSE))
			return 0;
		break;
	}
	params->progress.scan.cur_path = params->cur_path;
	params->progress.scan.status = status;
	if (status == WIMLIB_SCAN_DENTRY_OK) {

		/* The first time the inode is seen, tally all its streams.  */
		if (inode->i_nlink == 1) {
			for (unsigned i = 0; i < inode->i_num_streams; i++) {
				const struct blob_descriptor *blob =
					stream_blob_resolved(&inode->i_streams[i]);
				if (blob)
					params->progress.scan.num_bytes_scanned += blob->size;
			}
		}

		/* Tally the file itself, counting every hard link.  It's
		 * debatable whether every link should be counted, but counting
		 * every link makes the statistics consistent with the ones
		 * placed in the FILECOUNT and DIRCOUNT elements of the WIM
		 * file's XML document.  It also avoids possible user confusion
		 * if the number of files reported were to be lower than that
		 * displayed by some other software such as file browsers.  */
		if (inode_is_directory(inode))
			params->progress.scan.num_dirs_scanned++;
		else
			params->progress.scan.num_nondirs_scanned++;
	}

	/* Call the user-provided progress function.  */

	cookie = progress_get_win32_path(params->progress.scan.cur_path);
	ret = call_progress(params->progfunc, WIMLIB_PROGRESS_MSG_SCAN_DENTRY,
			     &params->progress, params->progctx);
	progress_put_win32_path(cookie);
	return ret;
}

/*
 * Given a null-terminated pathname pattern @pat that has been read from line
 * @line_no of the file @path, validate and canonicalize the pattern.
 *
 * On success, returns 0.
 * On failure, returns WIMLIB_ERR_INVALID_CAPTURE_CONFIG.
 * In either case, @pat may have been modified in-place (and possibly
 * shortened).
 */
int
mangle_pat(tchar *pat, const tchar *path, unsigned long line_no)
{
	if (!is_any_path_separator(pat[0]) &&
	    pat[0] != T('\0') && pat[1] == T(':'))
	{
		/* Pattern begins with drive letter.  */

		if (!is_any_path_separator(pat[2])) {
			/* Something like c:file, which is actually a path
			 * relative to the current working directory on the c:
			 * drive.  We require paths with drive letters to be
			 * absolute.  */
			ERROR("%"TS":%lu: Invalid pattern \"%"TS"\":\n"
			      "        Patterns including drive letters must be absolute!\n"
			      "        Maybe try \"%"TC":%"TC"%"TS"\"?\n",
			      path, line_no, pat,
			      pat[0], OS_PREFERRED_PATH_SEPARATOR, &pat[2]);
			return WIMLIB_ERR_INVALID_CAPTURE_CONFIG;
		}

		WARNING("%"TS":%lu: Pattern \"%"TS"\" starts with a drive "
			"letter, which is being removed.",
			path, line_no, pat);

		/* Strip the drive letter.  */
		tmemmove(pat, pat + 2, tstrlen(pat + 2) + 1);
	}

	/* Collapse consecutive path separators, and translate both / and \ into
	 * / (UNIX) or \ (Windows).
	 *
	 * Note: we expect that this function produces patterns that can be used
	 * for both filesystem paths and WIM paths, so the desired path
	 * separators must be the same.  */
	STATIC_ASSERT(OS_PREFERRED_PATH_SEPARATOR == WIM_PATH_SEPARATOR);
	do_canonicalize_path(pat, pat);

	/* Relative patterns can only match file names, so they must be
	 * single-component only.  */
	if (pat[0] != OS_PREFERRED_PATH_SEPARATOR &&
	    tstrchr(pat, OS_PREFERRED_PATH_SEPARATOR))
	{
		ERROR("%"TS":%lu: Invalid pattern \"%"TS"\":\n"
		      "        Relative patterns can only include one path component!\n"
		      "        Maybe try \"%"TC"%"TS"\"?",
		      path, line_no, pat, OS_PREFERRED_PATH_SEPARATOR, pat);
		return WIMLIB_ERR_INVALID_CAPTURE_CONFIG;
	}

	return 0;
}

/*
 * Read, parse, and validate a capture configuration file from either an on-disk
 * file or an in-memory buffer.
 *
 * To read from a file, specify @config_file, and use NULL for @buf.
 * To read from a buffer, specify @buf and @bufsize.
 *
 * @config must be initialized to all 0's.
 *
 * On success, 0 will be returned, and the resulting capture configuration will
 * be stored in @config.
 *
 * On failure, a positive error code will be returned, and the contents of
 * @config will be invalidated.
 */
int
read_capture_config(const tchar *config_file, const void *buf,
		    size_t bufsize, struct capture_config *config)
{
	int ret;

	/* [PrepopulateList] is used for apply, not capture.  But since we do
	 * understand it, recognize it, thereby avoiding the unrecognized
	 * section warning, but discard the resulting strings.
	 *
	 * We currently ignore [CompressionExclusionList] and
	 * [CompressionFolderList].  This is a known issue that doesn't seem to
	 * have any real consequences, so don't issue warnings about not
	 * recognizing those sections.  */
	STRING_LIST(prepopulate_pats);
	STRING_LIST(compression_exclusion_pats);
	STRING_LIST(compression_folder_pats);

	struct text_file_section sections[] = {
		{T("ExclusionList"),
			&config->exclusion_pats},
		{T("ExclusionException"),
			&config->exclusion_exception_pats},
		{T("PrepopulateList"),
			&prepopulate_pats},
		{T("CompressionExclusionList"),
			&compression_exclusion_pats},
		{T("CompressionFolderList"),
			&compression_folder_pats},
	};
	void *mem;

	ret = load_text_file(config_file, buf, bufsize, &mem,
			     sections, ARRAY_LEN(sections),
			     LOAD_TEXT_FILE_REMOVE_QUOTES, mangle_pat);
	if (ret) {
		ERROR("Failed to load capture configuration file \"%"TS"\"",
		      config_file);
		switch (ret) {
		case WIMLIB_ERR_INVALID_UTF8_STRING:
		case WIMLIB_ERR_INVALID_UTF16_STRING:
			ERROR("Note: the capture configuration file must be "
			      "valid UTF-8 or UTF-16LE");
			ret = WIMLIB_ERR_INVALID_CAPTURE_CONFIG;
			break;
		case WIMLIB_ERR_OPEN:
		case WIMLIB_ERR_STAT:
		case WIMLIB_ERR_NOMEM:
		case WIMLIB_ERR_READ:
			ret = WIMLIB_ERR_UNABLE_TO_READ_CAPTURE_CONFIG;
			break;
		}
		return ret;
	}

	FREE(prepopulate_pats.strings);
	FREE(compression_exclusion_pats.strings);
	FREE(compression_folder_pats.strings);

	config->buf = mem;
	return 0;
}

void
destroy_capture_config(struct capture_config *config)
{
	FREE(config->exclusion_pats.strings);
	FREE(config->exclusion_exception_pats.strings);
	FREE(config->buf);
}

/*
 * Determine whether @path matches any of the patterns in @list.
 * Path separators in @path must be WIM_PATH_SEPARATOR.
 */
bool
match_pattern_list(const tchar *path, const struct string_list *list,
		   int match_flags)
{
	for (size_t i = 0; i < list->num_strings; i++)
		if (match_path(path, list->strings[i], match_flags))
			return true;
	return false;
}

/*
 * Determine if a file should be excluded from capture.
 *
 * This function tests exclusions from both possible sources of exclusions:
 *
 *	(1) The capture configuration file
 *	(2) The user-provided progress function
 *
 * params->root_path_nchars must have been set beforehand.  Example for UNIX: if
 * the capture root directory is "foobar/subdir", then all paths will be
 * provided starting with "foobar/subdir", so params->root_path_nchars must have
 * been set to strlen("foobar/subdir") so that the appropriate path suffix can
 * be matched against the patterns in the exclusion list.
 *
 * Returns:
 *	< 0 if excluded
 *	= 0 if not excluded and no error
 *	> 0 (wimlib error code) if error
 */
int
try_exclude(const struct scan_params *params)
{
	int ret;

	if (params->config) {
		const tchar *path = params->cur_path + params->root_path_nchars;
		if (match_pattern_list(path, &params->config->exclusion_pats,
				       MATCH_RECURSIVELY) &&
		    !match_pattern_list(path, &params->config->exclusion_exception_pats,
					MATCH_RECURSIVELY | MATCH_ANCESTORS))
			return -1;
	}

	if (unlikely(params->add_flags & WIMLIB_ADD_FLAG_TEST_FILE_EXCLUSION)) {

		union wimlib_progress_info info;
		tchar *cookie;

		info.test_file_exclusion.path = params->cur_path;
		info.test_file_exclusion.will_exclude = false;

		cookie = progress_get_win32_path(info.test_file_exclusion.path);

		ret = call_progress(params->progfunc, WIMLIB_PROGRESS_MSG_TEST_FILE_EXCLUSION,
				    &info, params->progctx);

		progress_put_win32_path(cookie);

		if (ret)
			return ret;
		if (info.test_file_exclusion.will_exclude)
			return -1;
	}

	return 0;
}

/*
 * Determine whether a directory entry of the specified name should be ignored.
 * This is a lower level function which runs prior to try_exclude().  It handles
 * the standard '.' and '..' entries, which show up in directory listings but
 * should not be archived.  It also checks for odd filenames that usually should
 * not exist but could cause problems if archiving them were to be attempted.
 */
bool
should_ignore_filename(const tchar *name, const int name_nchars)
{
	if (name_nchars <= 0) {
		WARNING("Ignoring empty filename");
		return true;
	}

	if (name[0] == T('.') &&
	    (name_nchars == 1 || (name_nchars == 2 && name[1] == T('.'))))
		return true;

	for (int i = 0; i < name_nchars; i++) {
		if (name[i] == T('\0')) {
			WARNING("Ignoring filename containing embedded null character");
			return true;
		}
		if (name[i] == OS_PREFERRED_PATH_SEPARATOR) {
			WARNING("Ignoring filename containing embedded path separator");
			return true;
		}
	}

	return false;
}

/* Attach a newly scanned directory tree to its parent directory, with duplicate
 * handling.  */
void
attach_scanned_tree(struct wim_dentry *parent, struct wim_dentry *child,
		    struct blob_table *blob_table)
{
	struct wim_dentry *duplicate;

	if (child && (duplicate = dentry_add_child(parent, child))) {
		WARNING("Duplicate file path: \"%"TS"\".  Only capturing "
			"the first version.", dentry_full_path(duplicate));
		free_dentry_tree(child, blob_table);
	}
}

/* Set the path at which the directory tree scan is beginning. */
int
pathbuf_init(struct scan_params *params, const tchar *root_path)
{
	size_t nchars = tstrlen(root_path);
	size_t alloc_nchars = nchars + 1 + 1024;

	params->cur_path = MALLOC(alloc_nchars * sizeof(tchar));
	if (!params->cur_path)
		return WIMLIB_ERR_NOMEM;
	tmemcpy(params->cur_path, root_path, nchars + 1);
	params->cur_path_nchars = nchars;
	params->cur_path_alloc_nchars = alloc_nchars;
	params->root_path_nchars = nchars;
	return 0;
}

/*
 * Append a filename to the current path.
 *
 * If successful, returns a pointer to the filename component and sets
 * *orig_path_nchars_ret to the old path length, which can be restored later
 * using pathbuf_truncate().  Otherwise returns NULL (out of memory).
 */
const tchar *
pathbuf_append_name(struct scan_params *params, const tchar *name,
		    size_t name_nchars, size_t *orig_path_nchars_ret)
{
	size_t path_nchars = params->cur_path_nchars;
	size_t required_nchars = path_nchars + 1 + name_nchars + 1;
	tchar *buf = params->cur_path;

	if (unlikely(required_nchars > params->cur_path_alloc_nchars)) {
		required_nchars += 1024;
		buf = REALLOC(buf, required_nchars * sizeof(tchar));
		if (!buf)
			return NULL;
		params->cur_path = buf;
		params->cur_path_alloc_nchars = required_nchars;
	}
	*orig_path_nchars_ret = path_nchars;

	/*
	 * Add the slash, but not if it will be a duplicate (which can happen if
	 * the path to the capture root directory ends in a slash), because
	 * on Windows duplicate slashes sometimes don't work as expected.
	 */
	if (path_nchars && buf[path_nchars - 1] != OS_PREFERRED_PATH_SEPARATOR)
		buf[path_nchars++] = OS_PREFERRED_PATH_SEPARATOR;

	tmemcpy(&buf[path_nchars], name, name_nchars);
	path_nchars += name_nchars;
	buf[path_nchars] = T('\0');
	params->cur_path_nchars = path_nchars;
	return &buf[path_nchars - name_nchars];
}

/* Truncate the current path to the specified number of characters. */
void
pathbuf_truncate(struct scan_params *params, size_t nchars)
{
	wimlib_assert(nchars <= params->cur_path_nchars);
	params->cur_path[nchars] = T('\0');
	params->cur_path_nchars = nchars;
}
