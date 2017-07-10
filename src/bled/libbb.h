/*
 * Library header for busybox/Bled
 *
 * Rewritten for Bled (Base Library for Easy Decompression)
 * Copyright © 2014-2015 Pete Batard <pete@akeo.ie>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* Memory leaks detection - define _CRTDBG_MAP_ALLOC as preprocessor macro */
#ifdef _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#ifndef LIBBB_H
#define LIBBB_H 1

#ifndef _WIN32
#error Only Windows platforms are supported
#endif

#include "platform.h"
#include "msapi_utf8.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <io.h>

#ifdef BUFSIZ
#undef BUFSIZ
#endif
#define BUFSIZ 65536

#define IF_DESKTOP(x) x
#define IF_NOT_DESKTOP(x)
#define IF_NOT_FEATURE_LZMA_FAST(x) x

#define uoff_t unsigned off_t
#define OFF_FMT "I64"

#ifndef _MODE_T_
#define _MODE_T_
typedef unsigned short mode_t;
#endif

#ifndef _PID_T_
#define _PID_T_
typedef int pid_t;
#endif

#ifndef _GID_T_
#define _GID_T_
typedef unsigned int gid_t;
#endif

#ifndef _UID_T_
#define _UID_T_
typedef unsigned int uid_t;
#endif

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#ifndef get_le64
#define get_le64(ptr) (*(const uint64_t *)(ptr))
#endif

#ifndef get_le32
#define get_le32(ptr) (*(const uint32_t *)(ptr))
#endif

#ifndef get_le16
#define get_le16(ptr) (*(const uint16_t *)(ptr))
#endif

extern smallint bb_got_signal;
extern uint32_t *global_crc32_table;
extern jmp_buf bb_error_jmp;

uint32_t* crc32_filltable(uint32_t *crc_table, int endian);
uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len, uint32_t *crc32table_le);
uint32_t crc32_be(uint32_t crc, unsigned char const *p, size_t len, uint32_t *crc32table_be);
#define crc32_block_endian0 crc32_le
#define crc32_block_endian1 crc32_be

#if defined(_MSC_VER)
#if _FILE_OFFSET_BITS == 64
#define stat _stat32i64
#define lseek _lseeki64
#else
#define stat _stat32
#define lseek _lseek
#endif
#endif

typedef struct _llist_t {
	struct _llist_t *link;
	char *data;
} llist_t;

extern void (*bled_printf) (const char* format, ...);
extern void (*bled_progress) (const uint64_t processed_bytes);
extern unsigned long* bled_cancel_request;

#define xfunc_die() longjmp(bb_error_jmp, 1)
#define bb_printf(...) do { if (bled_printf != NULL) bled_printf(__VA_ARGS__); \
	else { printf(__VA_ARGS__); putchar('\n'); } } while(0)
#define bb_error_msg(...) bb_printf("Error: " __VA_ARGS__)
#define bb_error_msg_and_die(...) do {bb_error_msg(__VA_ARGS__); xfunc_die();} while(0)
#define bb_error_msg_and_err(...) do {bb_error_msg(__VA_ARGS__); goto err;} while(0)
#define bb_perror_msg bb_error_msg
#define bb_perror_msg_and_die bb_error_msg_and_die
#define bb_putchar putchar

static inline void *xrealloc(void *ptr, size_t size) {
	void *ret = realloc(ptr, size);
	if (!ret)
		free(ptr);
	return ret;
}

#define bb_msg_read_error "read error"
#define bb_msg_write_error "write error"
#define bb_mode_string(mode) "[not implemented]"
#define bb_copyfd_exact_size(fd1, fd2, size) bb_error_msg("Not implemented")
#define bb_make_directory(path, mode, flags) SHCreateDirectoryExU(NULL, path, NULL)

static inline int link(const char *oldpath, const char *newpath) {errno = ENOSYS; return -1;}
static inline int symlink(const char *oldpath, const char *newpath) {errno = ENOSYS; return -1;}
static inline int chown(const char *path, uid_t owner, gid_t group) {errno = ENOSYS; return -1;}
static inline int mknod(const char *pathname, mode_t mode, dev_t dev) {errno = ENOSYS; return -1;}
static inline int utimes(const char *filename, const struct timeval times[2]) {errno = ENOSYS; return -1;}
static inline int fnmatch(const char *pattern, const char *string, int flags) {return PathMatchSpecA(string, pattern)?0:1;}
static inline pid_t wait(int* status) { *status = 4; return -1; }
#define wait_any_nohang wait

/* This override enables the display of a progress based on the number of bytes read */
extern uint64_t bb_total_rb;
static inline int full_read(int fd, void *buf, size_t count) {
	int rb;

	if (fd < 0) {
		errno = EBADF;
		return -1;
	}
	if (buf == NULL) {
		errno = EFAULT;
		return -1;
	}
	if ((bled_cancel_request != NULL) && (*bled_cancel_request != 0)) {
		errno = EINTR;
		return -1;
	}

	rb = _read(fd, buf, (int)count);
	if (rb > 0) {
		bb_total_rb += rb;
		if (bled_progress != NULL)
			bled_progress(bb_total_rb);
	}
	return rb;
}

static inline struct tm *localtime_r(const time_t *timep, struct tm *result) {
	if (localtime_s(result, timep) != 0)
		result = NULL;
	return result;
}

#define full_write _write
#define safe_read full_read
#define lstat stat
#define xmalloc malloc
#define xzalloc(x) calloc(x, 1)
#define malloc_or_warn malloc
#define mkdir(x, y) _mkdirU(x)

#if defined(_MSC_VER)
#define _S_IFBLK 0x3000

#define S_IFMT   _S_IFMT
#define S_IFDIR  _S_IFDIR
#define S_IFCHR  _S_IFCHR
#define S_IFIFO  _S_IFIFO
#define S_IFREG  _S_IFREG
#define S_IREAD  _S_IREAD
#define S_IWRITE _S_IWRITE
#define S_IEXEC  _S_IEXEC
#define S_IFBLK  _S_IFBLK

#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISFIFO(m) (((m) & _S_IFMT) == _S_IFIFO)
#define S_ISCHR(m)  (((m) & _S_IFMT) == _S_IFCHR)
#define S_ISBLK(m)  (((m) & _S_IFMT) == _S_IFBLK)
#define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)

#define O_RDONLY _O_RDONLY
#define O_WRONLY _O_WRONLY
#define O_RDWR   _O_RDWR
#define O_APPEND _O_APPEND

#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_EXCL  _O_EXCL
#endif

/* MinGW doesn't know these */
#define _S_IFLNK    0xA000
#define _S_IFSOCK   0xC000
#define S_IFLNK     _S_IFLNK
#define S_IFSOCK    _S_IFSOCK
#define S_ISLNK(m)  (((m) & _S_IFMT) == _S_IFLNK)
#define S_ISSOCK(m) (((m) & _S_IFMT) == _S_IFSOCK)

#endif
