/*
 * win32.h - Windows-specific declarations needed by non-Windows-specific files.
 */

#ifndef _WIMLIB_WIN32_H
#define _WIMLIB_WIN32_H

#ifdef _WIN32

#include <unistd.h>
#include "wimlib/types.h"

struct blob_descriptor;
struct consume_chunk_callback;
struct windows_file;

struct windows_file *
clone_windows_file(const struct windows_file *file);

void
free_windows_file(struct windows_file *file);

int
cmp_windows_files(const struct windows_file *file1,
		  const struct windows_file *file2);

int
read_windows_file_prefix(const struct blob_descriptor *blob, u64 size,
			 const struct consume_chunk_callback *cb,
			 bool recover_data);

int
win32_global_init(int init_flags);

void
win32_global_cleanup(void);

int
fsync(int fd);

tchar *
realpath(const tchar *path, tchar *resolved_path);

int
win32_rename_replacement(const tchar *oldpath, const tchar *newpath);

int
win32_truncate_replacement(const tchar *path, off_t size);

int
win32_strerror_r_replacement(int errnum, tchar *buf, size_t buflen);

FILE *
win32_open_logfile(const wchar_t *path);

ssize_t
win32_read(int fd, void *buf, size_t count);

ssize_t
win32_write(int fd, const void *buf, size_t count);

ssize_t
win32_pread(int fd, void *buf, size_t count, off_t offset);

ssize_t
win32_pwrite(int fd, const void *buf, size_t count, off_t offset);

#endif /* _WIN32 */

#endif /* _WIMLIB_WIN32_H */
