#ifndef _WIMLIB_PATTERN_H
#define _WIMLIB_PATTERN_H

#include "wimlib/types.h"

struct wim_dentry;

/* Flags for match_path() and match_pattern_list() */

/*
 * If set, subdirectories (and sub-files) are also matched.
 * For example, the pattern "/dir" would match the path "/dir/file".
 */
#define MATCH_RECURSIVELY	0x01

/*
 * If set, ancestor directories are also matched.
 * For example, the pattern "/dir/file" would match the path "/dir".
 */
#define MATCH_ANCESTORS		0x02

bool
match_path(const tchar *path, const tchar *pattern, int match_flags);

int
expand_path_pattern(struct wim_dentry *root, const tchar *pattern,
		    int (*consume_dentry)(struct wim_dentry *, void *),
		    void *ctx);

#endif /* _WIMLIB_PATTERN_H  */
