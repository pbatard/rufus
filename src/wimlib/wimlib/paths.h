#ifndef _WIMLIB_PATHS_H
#define _WIMLIB_PATHS_H

#include "wimlib/compiler.h"
#include "wimlib/types.h"

const tchar *
path_basename(const tchar *path);

const tchar *
path_basename_with_len(const tchar *path, size_t len);

const tchar *
path_stream_name(const tchar *path);

void
do_canonicalize_path(const tchar *in, tchar *out);

tchar *
canonicalize_wim_path(const tchar *wim_path);

/* is_any_path_separator() - characters treated as path separators in WIM path
 * specifications and capture configuration files (the former will be translated
 * to WIM_PATH_SEPARATOR; the latter will be translated to
 * OS_PREFERRED_PATH_SEPARATOR)
 *
 * OS_PREFERRED_PATH_SEPARATOR - preferred (or only) path separator on the
 * operating system.  Used when constructing filesystem paths to extract or
 * archive.
 *
 * WIM_PATH_SEPARATOR - character treated as path separator for WIM paths.
 * Currently needs to be '/' on UNIX for the WIM mounting code to work properly.
 */

#ifdef _WIN32
#  define OS_PREFERRED_PATH_SEPARATOR L'\\'
#  define is_any_path_separator(c) ((c) == L'/' || (c) == L'\\')
#else
#  define OS_PREFERRED_PATH_SEPARATOR '/'
#  define is_any_path_separator(c) ((c) == '/' || (c) == '\\')
#endif

#define WIM_PATH_SEPARATOR WIMLIB_WIM_PATH_SEPARATOR

#endif /* _WIMLIB_PATHS_H */
