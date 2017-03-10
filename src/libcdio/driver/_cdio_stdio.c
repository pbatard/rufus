/*
  Copyright (C) 2003, 2004, 2005, 2008, 2011 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <ctype.h>

#include <cdio/logging.h>
#include <cdio/util.h>
#include "_cdio_stream.h"
#include "_cdio_stdio.h"
#include "cdio_assert.h"

/* On 32 bit platforms, fseek can only access streams of 2 GB or less.
   Prefer fseeko/fseeko64, that take a 64 bit offset when LFS is enabled */
#if defined(HAVE_FSEEKO64) && defined(_FILE_OFFSET_BITS) && (_FILE_OFFSET_BITS == 64)
#define CDIO_FSEEK fseeko64
#elif defined(HAVE_FSEEKO)
#define CDIO_FSEEK fseeko
#else
#define CDIO_FSEEK fseek
#endif

/* Windows' fopen is not UTF-8 compliant, so we use our own */
#if defined(_WIN32)
#include <cdio/utf8.h>
#define CDIO_FOPEN fopen_utf8
#else
#define CDIO_FOPEN fopen
#endif

/* Use _stati64 if needed, on platforms that don't have transparent LFS support */
#if defined(HAVE__STATI64) && defined(_FILE_OFFSET_BITS) && (_FILE_OFFSET_BITS == 64)
#define CDIO_STAT_STRUCT _stati64
#if defined(_WIN32)
/* Once again, use our own UTF-8 compliant version */
static inline int _stati64_utf8(const char *path, struct _stati64 *buffer) {
  int ret;
  wchar_t* wpath = cdio_utf8_to_wchar(path);
  ret = _wstati64(wpath, buffer);
  cdio_free(wpath);
  return ret;
}
#define CDIO_STAT_CALL _stati64_utf8
#else
#define CDIO_STAT_CALL _stati64
#endif
#else
#define CDIO_STAT_STRUCT stat
#define CDIO_STAT_CALL stat
#endif

#define _STRINGIFY(a) #a
#define STRINGIFY(a) _STRINGIFY(a)

#define CDIO_STDIO_BUFSIZE (128*1024)

typedef struct {
  char *pathname;
  FILE *fd;
  char *fd_buf;
  off_t st_size; /* used only for source */
} _UserData;

static int
_stdio_open (void *user_data)
{
  _UserData *const ud = user_data;

  if ((ud->fd = CDIO_FOPEN (ud->pathname, "rb")))
    {
      ud->fd_buf = calloc (1, CDIO_STDIO_BUFSIZE);
      setvbuf (ud->fd, ud->fd_buf, _IOFBF, CDIO_STDIO_BUFSIZE);
    }

  return (ud->fd == NULL);
}

static int
_stdio_close(void *user_data)
{
  _UserData *const ud = user_data;

  if (fclose (ud->fd))
    cdio_error ("fclose (): %s", strerror (errno));

  ud->fd = NULL;

  free (ud->fd_buf);
  ud->fd_buf = NULL;

  return 0;
}

static void
_stdio_free(void *user_data)
{
  _UserData *const ud = user_data;

  if (ud->pathname)
    free(ud->pathname);

  if (ud->fd) /* should be NULL anyway... */
    _stdio_close(user_data);

  free(ud);
}

/*!
  Like fseek/fseeko(3) and in fact may be the same.

  This  function sets the file position indicator for the stream
  pointed to by stream.  The new position, measured in bytes, is obtained
  by  adding offset bytes to the position specified by whence.  If whence
  is set to SEEK_SET, SEEK_CUR, or SEEK_END, the offset  is  relative  to
  the  start of the file, the current position indicator, or end-of-file,
  respectively.  A successful call to the fseek function clears the end-
  of-file indicator for the stream and undoes any effects of the
  ungetc(3) function on the same stream.

  @return upon successful completion, DRIVER_OP_SUCCESS, else,
  DRIVER_OP_ERROR is returned and the global variable errno is set to
  indicate the error.
*/
static int
_stdio_seek(void *p_user_data, off_t i_offset, int whence)
{
  _UserData *const ud = p_user_data;
  int ret;
#if !defined(HAVE_FSEEKO) && !defined(HAVE_FSEEKO64)
  /* Detect if off_t is lossy-truncated to long to avoid data corruption */
  if ( (sizeof(off_t) > sizeof(long)) && (i_offset != (off_t)((long)i_offset)) ) {
    cdio_error ( STRINGIFY(CDIO_FSEEK) " (): lossy truncation detected!");
    errno = EFBIG;
    return DRIVER_OP_ERROR;
  }
#endif

  if ( (ret=CDIO_FSEEK (ud->fd, i_offset, whence)) ) {
    cdio_error ( STRINGIFY(CDIO_FSEEK) " (): %s", strerror (errno));
  }

  return ret;
}

static off_t
_stdio_stat(void *p_user_data)
{
  const _UserData *const ud = p_user_data;

  return ud->st_size;
}

/*!
  Like fread(3) and in fact is about the same.

  DESCRIPTION:
  The function fread reads nmemb elements of data, each size bytes long,
  from the stream pointed to by stream, storing them at the location
  given by ptr.

  RETURN VALUE:
  return the number of items successfully read or written (i.e.,
  not the number of characters).  If an error occurs, or the
  end-of-file is reached, the return value is a short item count
  (or zero).

  We do not distinguish between end-of-file and error, and callers
  must use feof(3) and ferror(3) to determine which occurred.
  */
static ssize_t
_stdio_read(void *user_data, void *buf, size_t count)
{
  _UserData *const ud = user_data;
  long read_count;

  read_count = fread(buf, 1, count, ud->fd);

  if (read_count != count)
    { /* fixme -- ferror/feof */
      if (feof (ud->fd))
        {
          cdio_debug ("fread (): EOF encountered");
          clearerr (ud->fd);
        }
      else if (ferror (ud->fd))
        {
          cdio_error ("fread (): %s", strerror (errno));
          clearerr (ud->fd);
        }
      else
        cdio_debug ("fread (): short read and no EOF?!?");
    }

  return read_count;
}

/*!
  Deallocate resources assocaited with obj. After this obj is unusable.
*/
void
cdio_stdio_destroy(CdioDataSource_t *p_obj)
{
  cdio_stream_destroy(p_obj);
}

CdioDataSource_t *
cdio_stdio_new(const char pathname[])
{
  CdioDataSource_t *new_obj = NULL;
  cdio_stream_io_functions funcs = { NULL, NULL, NULL, NULL, NULL, NULL };
  _UserData *ud = NULL;
  struct CDIO_STAT_STRUCT statbuf;
  char* pathdup;

  if (pathname == NULL)
    return NULL;

  /* MinGW may require a translated path */
  pathdup = _cdio_strdup_fixpath(pathname);
  if (pathdup == NULL)
    return NULL;

  if (CDIO_STAT_CALL (pathdup, &statbuf) == -1)
    {
      cdio_warn ("could not retrieve file info for `%s': %s",
                 pathdup, strerror (errno));
      cdio_free(pathdup);
      return NULL;
    }

  ud = calloc (1, sizeof (_UserData));
  cdio_assert (ud != NULL);

  ud->pathname = pathdup;
  ud->st_size  = statbuf.st_size; /* let's hope it doesn't change... */

  funcs.open   = _stdio_open;
  funcs.seek   = _stdio_seek;
  funcs.stat   = _stdio_stat;
  funcs.read   = _stdio_read;
  funcs.close  = _stdio_close;
  funcs.free   = _stdio_free;

  new_obj = cdio_stream_new(ud, &funcs);

  return new_obj;
}


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
