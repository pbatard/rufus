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

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#else
#ifndef EXTERNAL_LIBCDIO_CONFIG_H
#define EXTERNAL_LIBCDIO_CONFIG_H
#include <cdio/cdio_config.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h> 
#endif /*HAVE_UNISTD_H*/

#include <sys/stat.h>
#include <errno.h>

#include <cdio/logging.h>
#include <cdio/util.h>
#include "_cdio_stream.h"
#include "_cdio_stdio.h"

static const char _rcsid[] = "$Id: _cdio_stdio.c,v 1.6 2008/04/22 15:29:11 karl Exp $";

#define CDIO_STDIO_BUFSIZE (128*1024)

typedef struct {
  char *pathname;
  FILE *fd;
  char *fd_buf;
  int64_t st_size; /* used only for source */
} _UserData;

static int
_stdio_open (void *user_data) 
{
  _UserData *const ud = user_data;
  
  if ((ud->fd = fopen (ud->pathname, "rb")))
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
  Like fseek(3) and in fact may be the same.
  
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
static driver_return_code_t 
_stdio_seek(void *p_user_data, long i_offset, int whence)
{
  _UserData *const ud = p_user_data;

  if ( (i_offset=fseek (ud->fd, i_offset, whence)) ) {
    cdio_error ("fseek (): %s", strerror (errno));
  }

  return i_offset;
}

static uint64_t
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
static long
_stdio_read(void *user_data, void *buf, long int count)
{
  _UserData *const ud = user_data;
  long read_size;

  read_size = fread(buf, 1, count, ud->fd);

  if (read_size != count)
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

  return read_size;
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
  struct _stat64 statbuf;
  
  if (_stat64 (pathname, &statbuf) == -1) 
    {
      cdio_warn ("could not retrieve file info for `%s': %s", 
                 pathname, strerror (errno));
      return NULL;
    }

  ud = calloc (1, sizeof (_UserData));

  ud->pathname = _strdup(pathname);
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
