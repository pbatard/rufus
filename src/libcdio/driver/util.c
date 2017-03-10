/*
  Copyright (C) 2003, 2004, 2005, 2008, 2009, 2010, 2011
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

#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <ctype.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_INTTYPES_H
#include "inttypes.h"
#endif

#include <ctype.h>

#include "cdio_assert.h"
#include <cdio/types.h>
#include <cdio/util.h>
#include <cdio/version.h>

size_t
_cdio_strlenv(char **str_array)
{
  size_t n = 0;

  cdio_assert (str_array != NULL);

  while(str_array[n])
    n++;

  return n;
}

void
_cdio_strfreev(char **strv)
{
  int n;

  cdio_assert (strv != NULL);

  for(n = 0; strv[n]; n++)
    free(strv[n]);

  free(strv);
}

char **
_cdio_strsplit(const char str[], char delim) /* fixme -- non-reentrant */
{
  int n;
  char **strv = NULL;
  char *_str, *p;
  char _delim[2] = { 0, 0 };

  cdio_assert (str != NULL);

  _str = strdup(str);
  _delim[0] = delim;

  cdio_assert (_str != NULL);

  n = 1;
  p = _str;
  while(*p)
    if (*(p++) == delim)
      n++;

  strv = calloc (n+1, sizeof (char *));
  cdio_assert (strv != NULL);

  n = 0;
  while((p = strtok(n ? NULL : _str, _delim)) != NULL)
    strv[n++] = strdup(p);

  free(_str);

  return strv;
}

void *
_cdio_memdup (const void *mem, size_t count)
{
  void *new_mem = NULL;

  if (mem)
    {
      new_mem = calloc (1, count);
      cdio_assert (new_mem != NULL);
      memcpy (new_mem, mem, count);
    }

  return new_mem;
}

char *
_cdio_strdup_upper (const char str[])
{
  char *new_str = NULL;

  if (str)
    {
      char *p;

      p = new_str = strdup (str);

      while (*p)
        {
          *p = toupper ((unsigned char) *p);
          p++;
        }
    }

  return new_str;
}

/* Convert MinGW/MSYS paths that start in "/c/..." to "c:/..."
   so that they can be used with fopen(), stat(), etc.
   Returned string must be freed by the caller using cdio_free().*/
char *
_cdio_strdup_fixpath (const char path[])
{
  char *new_path = NULL;

  if (path)
    {
       new_path = strdup (path);
#if defined(_WIN32)
       if (new_path && (strlen (new_path) >= 3) && (new_path[0] == '/') &&
          (new_path[2] == '/') && (isalpha (new_path[1])))
         {
           new_path[0] = new_path[1];
           new_path[1] = ':';
         }
#endif
    }

  return new_path;
}

uint8_t
cdio_to_bcd8 (uint8_t n)
{
  /*cdio_assert (n < 100);*/

  return ((n/10)<<4) | (n%10);
}

uint8_t
cdio_from_bcd8(uint8_t p)
{
  return (0xf & p)+(10*(p >> 4));
}

const char *cdio_version_string = CDIO_VERSION;
const unsigned int libcdio_version_num = LIBCDIO_VERSION_NUM;


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
