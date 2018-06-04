/*
    Copyright (C) 2004, 2005, 2006, 2008, 2010, 2012, 2014
    Rocky Bernstein <rocky@gnu.org>
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

#ifndef CDIO_UTIL_H_
#define CDIO_UTIL_H_

/*!
   \file util.h
   \brief Miscellaneous utility functions.

   Warning: this will probably get removed/replaced by using glib.h
*/
#include <stdlib.h>
#include <cdio/types.h>

#if !defined CDIO_INLINE
#if defined(__cplusplus) || defined(inline)
#define CDIO_INLINE inline
#elif defined(__GNUC__)
#define CDIO_INLINE __inline__
#elif defined(_MSC_VER)
#define CDIO_INLINE __inline
#else
#define CDIO_INLINE
#endif
#endif /* CDIO_INLINE */

#undef  MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef  MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#undef  IN
#define IN(x, low, high) ((x) >= (low) && (x) <= (high))

#undef  CLAMP
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

static CDIO_INLINE uint32_t
_cdio_len2blocks (uint32_t i_len, uint16_t i_blocksize)
{
  uint32_t i_blocks;

  i_blocks = i_len / (uint32_t) i_blocksize;
  if (i_len % i_blocksize)
    i_blocks++;

  return i_blocks;
}


/*! free() and NULL out p_obj it is not already null. */
#define CDIO_FREE_IF_NOT_NULL(p_obj) \
  if (NULL != p_obj) { free(p_obj); p_obj=NULL; };


/* round up to next block boundary */
static CDIO_INLINE unsigned
_cdio_ceil2block (unsigned offset, uint16_t i_blocksize)
{
  return _cdio_len2blocks (offset, i_blocksize) * i_blocksize;
}

static CDIO_INLINE unsigned int
_cdio_ofs_add (unsigned offset, unsigned length, uint16_t i_blocksize)
{
  if (i_blocksize - (offset % i_blocksize) < length)
    offset = _cdio_ceil2block (offset, i_blocksize);

  offset += length;

  return offset;
}

static CDIO_INLINE const char *
_cdio_bool_str (bool b)
{
  return b ? "yes" : "no";
}

#ifdef __cplusplus
extern "C" {
#endif

void *
_cdio_memdup (const void *mem, size_t count);

char *
_cdio_strdup_upper (const char str[]);

/*! Duplicate path and make it platform compliant. Typically needed for
    MinGW/MSYS where a "/c/..." path must be translated to "c:/..." for
    use with fopen(), etc. Returned string must be freed by the caller
    using cdio_free(). */
char *
_cdio_strdup_fixpath (const char path[]);

void
_cdio_strfreev(char **strv);

size_t
_cdio_strlenv(char **str_array);

char **
_cdio_strsplit(const char str[], char delim);

uint8_t cdio_to_bcd8(uint8_t n);
uint8_t cdio_from_bcd8(uint8_t p);

/*!  cdio_realpath() same as POSIX.1-2001 realpath if that's
around. If not we do poor-man's simulation of that behavior.  */
char *cdio_realpath (const char *psz_src, char *psz_dst);

#ifdef WANT_FOLLOW_SYMLINK_COMPATIBILITY
# define cdio_follow_symlink cdio_realpath
#endif

#ifdef __cplusplus
}
#endif

#endif /* CDIO_UTIL_H_ */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
