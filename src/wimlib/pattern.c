/*
 * pattern.c
 *
 * Wildcard pattern matching functions.
 */

/*
 * Copyright (C) 2013, 2015 Eric Biggers
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

#include <ctype.h>

#include "wimlib/dentry.h"
#include "wimlib/encoding.h"
#include "wimlib/paths.h"
#include "wimlib/pattern.h"

static bool
string_matches_pattern(const tchar *string, const tchar * const string_end,
		       const tchar *pattern, const tchar * const pattern_end)
{
	for (; string != string_end; string++, pattern++) {
		if (pattern == pattern_end)
			return false;
		if (*pattern == T('*')) {
			return string_matches_pattern(string, string_end,
						      pattern + 1, pattern_end) ||
			       string_matches_pattern(string + 1, string_end,
						      pattern, pattern_end);
		}
		if (*string != *pattern && *pattern != T('?') &&
		    !(default_ignore_case &&
		      totlower(*string) == totlower(*pattern)))
			return false;
	}

	while (pattern != pattern_end && *pattern == T('*'))
		pattern++;
	return pattern == pattern_end;
}

/* Advance past zero or more path separators.  */
static const tchar *
advance_to_next_component(const tchar *p)
{
	while (*p == WIM_PATH_SEPARATOR)
		p++;
	return p;
}

/* Advance past a nonempty path component.  */
static const tchar *
advance_through_component(const tchar *p)
{
	do {
		p++;
	} while (*p && *p != WIM_PATH_SEPARATOR);
	return p;
}

/*
 * Determine whether a path matches a wildcard pattern.
 *
 * @path
 *	The null-terminated path string to match.
 * @pattern
 *	The null-terminated wildcard pattern to match.  It can contain the
 *	wildcard characters '*' (which matches zero or more characters) and '?'
 *	(which matches any single character).  If there is no leading path
 *	separator, then the match is attempted with the filename component of
 *	@path only; otherwise, the match is attempted with the entire @path.
 * @match_flags
 *	MATCH_* flags, see the flag definitions.
 *
 * @path and @pattern can both contain path separators (character
 * WIM_PATH_SEPARATOR).  Leading and trailing path separators are not
 * significant, except when determining whether to match only the filename
 * component as noted above.  The lengths of interior path separator sequences
 * are not significant.  The '*' and '?' characters act within a single path
 * component only (they do not match path separators).
 *
 * Matching is done with the default case sensitivity behavior.
 *
 * Returns %true iff the path matched the pattern.
 */
bool
match_path(const tchar *path, const tchar *pattern, int match_flags)
{
	/* Filename only?  */
	if (*pattern != WIM_PATH_SEPARATOR)
		path = path_basename(path);

	for (;;) {
		const tchar *path_component_end;
		const tchar *pattern_component_end;

		path = advance_to_next_component(path);
		pattern = advance_to_next_component(pattern);

		/* Is the pattern exhausted?  */
		if (!*pattern)
			return !*path || (match_flags & MATCH_RECURSIVELY);

		/* Is the path exhausted (but not the pattern)?  */
		if (!*path)
			return (match_flags & MATCH_ANCESTORS);

		path_component_end = advance_through_component(path);
		pattern_component_end = advance_through_component(pattern);

		/* Do the components match?  */
		if (!string_matches_pattern(path, path_component_end,
					    pattern, pattern_component_end))
			return false;

		path = path_component_end;
		pattern = pattern_component_end;
	}
}

/*
 * Expand a path pattern in an in-memory tree of dentries.
 *
 * @root
 *	The root of the directory tree in which to expand the pattern.
 * @pattern
 *	The path pattern to expand, which may contain the '*' and '?' wildcard
 *	characters.  Path separators must be WIM_PATH_SEPARATOR.  Leading and
 *	trailing path separators are ignored.  The default case sensitivity
 *	behavior is used.
 * @consume_dentry
 *	A callback function which will receive each matched directory entry.
 * @ctx
 *	Opaque context argument for @consume_dentry.
 *
 * @return 0 on success; a positive error code on failure; or the first nonzero
 * value returned by @consume_dentry.
 */
int
expand_path_pattern(struct wim_dentry *root, const tchar *pattern,
		    int (*consume_dentry)(struct wim_dentry *, void *),
		    void *ctx)
{
	const tchar *pattern_component_end;
	struct wim_dentry *child;

	if (!root)
		return 0;

	pattern = advance_to_next_component(pattern);

	/* If there are no more components, then 'root' is matched.  */
	if (!*pattern)
		return (*consume_dentry)(root, ctx);

	pattern_component_end = advance_through_component(pattern);

	/* For each child dentry that matches the current pattern component,
	 * recurse with the remainder of the pattern.  */
	for_dentry_child(child, root) {
		const tchar *name;
		size_t name_nbytes;
		int ret;

		ret = utf16le_get_tstr(child->d_name, child->d_name_nbytes,
				       &name, &name_nbytes);
		if (ret)
			return ret;

		if (string_matches_pattern(name, &name[name_nbytes / sizeof(tchar)],
					   pattern, pattern_component_end))
			ret = expand_path_pattern(child, pattern_component_end,
						  consume_dentry, ctx);
		utf16le_put_tstr(name);
		if (ret)
			return ret;
	}
	return 0;
}
