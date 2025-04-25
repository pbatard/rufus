#ifndef _WIMLIB_FILE_IO_H
#define _WIMLIB_FILE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef WITH_LIBCDIO
#  define DO_NOT_WANT_COMPATIBILITY
#  undef PRAGMA_BEGIN_PACKED
#  undef PRAGMA_END_PACKED
#  include <cdio/udf.h>
#  include <cdio/iso9660.h>
#endif

/* Wrapper around a file descriptor that keeps track of offset (including in
 * pipes, which don't support lseek()), allows the handling of files that are
 * contained within ISO images, and a cached flag that tells whether the file
 * descriptor is a pipe or not.  */
struct filedes {
	union {
		int fd;
#ifdef WITH_LIBCDIO
		iso9660_t* p_iso;
		udf_t* p_udf;
#endif
	};
	unsigned int is_pipe : 1;
#ifdef WITH_LIBCDIO
	unsigned int is_iso : 1;
	unsigned int is_udf : 1;
	union {
		udf_dirent_t* p_udf_file;
		iso9660_stat_t* p_iso_file;
	};
#endif
	off_t offset;
};

int
full_read(struct filedes *fd, void *buf, size_t n);

int
full_pread(struct filedes *fd, void *buf, size_t nbyte, off_t offset);

int
full_write(struct filedes *fd, const void *buf, size_t n);

int
full_pwrite(struct filedes *fd, const void *buf, size_t count, off_t offset);

#ifndef _WIN32
#  define O_BINARY 0
#endif

off_t
filedes_seek(struct filedes *fd, off_t offset);

bool
filedes_is_seekable(struct filedes *fd);

static inline void filedes_init(struct filedes *fd, int raw_fd)
{
	memset(fd, 0, sizeof(*fd));
	fd->fd = raw_fd;
}

static inline void filedes_invalidate(struct filedes *fd)
{
	fd->fd = -1;
}

#ifdef _MSC_VER
#include <io.h>
#define filedes_close(f) _close((f)->fd)
#else
#define filedes_close(f) close((f)->fd)
#endif

static inline bool
filedes_valid(const struct filedes *fd)
{
	return fd->fd != -1;
}

#endif /* _WIMLIB_FILE_IO_H */
