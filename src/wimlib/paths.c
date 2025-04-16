/*
 * paths.c - Path manipulation routines
 */

/*
 * Copyright (C) 2012, 2013 Eric Biggers
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

#include "wimlib.h"
#include "wimlib/paths.h"
#include "wimlib/util.h"

/* Like the basename() function, but does not modify @path; it just returns a
 * pointer to it.  This assumes the path separator is the
 * OS_PREFERRED_PATH_SEPARATOR.  */
const tchar *
path_basename(const tchar *path)
{
	return path_basename_with_len(path, tstrlen(path));
}

/* Like path_basename(), but take an explicit string length.  */
const tchar *
path_basename_with_len(const tchar *path, size_t len)
{
	const tchar *p = &path[len];

	do {
		if (p == path)
			return &path[len];
	} while (*--p == OS_PREFERRED_PATH_SEPARATOR);

	do {
		if (p == path)
			return &path[0];
	} while (*--p != OS_PREFERRED_PATH_SEPARATOR);

	return ++p;
}


/* Returns a pointer to the part of @path following the first colon in the last
 * path component, or NULL if the last path component does not contain a colon
 * or has no characters following the first colon.  */
const tchar *
path_stream_name(const tchar *path)
{
	const tchar *base = path_basename(path);
	const tchar *stream_name = tstrchr(base, T(':'));
	if (stream_name == NULL || *(stream_name + 1) == T('\0'))
		return NULL;
	else
		return stream_name + 1;
}

/* Collapse and translate path separators, and strip trailing slashes.  Doesn't
 * add or delete a leading slash.
 *
 * @in may alias @out.
 */
void
do_canonicalize_path(const tchar *in, tchar *out)
{
	tchar *orig_out = out;

	while (*in) {
		if (is_any_path_separator(*in)) {
			/* Collapse multiple path separators into one  */
			*out++ = WIM_PATH_SEPARATOR;
			do {
				in++;
			} while (is_any_path_separator(*in));
		} else {
			/* Copy non-path-separator character  */
			*out++ = *in++;
		}
	}

	/* Remove trailing slash if existent  */
	if (out - orig_out > 1 && *(out - 1) == WIM_PATH_SEPARATOR)
		--out;

	*out = T('\0');
}

/*
 * canonicalize_wim_path() - Given a user-provided path to a file within a WIM
 * image, translate it into a "canonical" path.
 *
 * - Translate both types of slash into a consistent type (WIM_PATH_SEPARATOR).
 * - Collapse path separators.
 * - Add leading slash if missing.
 * - Strip trailing slashes.
 *
 * Examples (with WIM_PATH_SEPARATOR == '/'):
 *
 *		=> /		[ either NULL or empty string ]
 * /		=> /
 * \		=> /
 * hello	=> /hello
 * \hello	=> /hello
 * \hello	=> /hello
 * /hello/	=> /hello
 * \hello/	=> /hello
 * /hello//1	=> /hello/1
 * \\hello\\1\\	=> /hello/1
 */
tchar *
canonicalize_wim_path(const tchar *wim_path)
{
	const tchar *in;
	tchar *out;
	tchar *result;

	in = wim_path;
	if (!in)
		in = T("");

	result = MALLOC((1 + tstrlen(in) + 1) * sizeof(result[0]));
	if (!result)
		return NULL;

	out = result;

	/* Add leading slash if missing  */
	if (!is_any_path_separator(*in))
		*out++ = WIM_PATH_SEPARATOR;

	do_canonicalize_path(in, out);

	return result;
}
