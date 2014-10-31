/*
  Copyright (C) 2003, 2005, 2008, 2011 Rocky Bernstein <rocky@gnu.org>
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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

/*! String inside frame which identifies XA attributes.  Note should
    come *before* public headers which does a #define of
    this name.
*/
const char ISO_XA_MARKER_STRING[] = {'C', 'D', '-', 'X', 'A', '0', '0', '1'};

/* Public headers */
#include <cdio/iso9660.h>
#include <cdio/util.h>
#include <cdio/bytesex.h>

/* Private headers */
#include "cdio_assert.h"
#include "filemode.h"

/** The below variable is trickery to force enum symbol values to be
    recorded in debug symbol tables. It is used to allow one to refer
    to the enumeration value names in the typedefs above in a debugger
    and debugger expressions.
*/
xa_misc_enum_t debugger_xa_misc_enum;

#define BUF_COUNT 16
#define BUF_SIZE 80

/* Return a pointer to a internal free buffer */
static char *
_getbuf (void)
{
  static char _buf[BUF_COUNT][BUF_SIZE];
  static int _num = -1;
  
  _num++;
  _num %= BUF_COUNT;

  memset (_buf[_num], 0, BUF_SIZE);

  return _buf[_num];
}

/*!
  Returns a string which interpreting the extended attribute xa_attr. 
  For example:
  \verbatim
  d---1xrxrxr
  ---2--r-r-r
  -a--1xrxrxr
  \endverbatim

  A description of the characters in the string follows
  The 1st character is either "d" if the entry is a directory, or "-" if not.
  The 2nd character is either "a" if the entry is CDDA (audio), or "-" if not.
  The 3rd character is either "i" if the entry is interleaved, or "-" if not.
  The 4th character is either "2" if the entry is mode2 form2 or "-" if not.
  The 5th character is either "1" if the entry is mode2 form1 or "-" if not.
   Note that an entry will either be in mode2 form1 or mode form2. That
   is you will either see "2-" or "-1" in the 4th & 5th positions.

  The 6th and 7th characters refer to permissions for a user while the
  the 8th and 9th characters refer to permissions for a group while, and 
  the 10th and 11th characters refer to permissions for a others. 
 
  In each of these pairs the first character (6, 8, 10) is "x" if the 
  entry is executable. For a directory this means the directory is
  allowed to be listed or "searched".
  The second character of a pair (7, 9, 11) is "r" if the entry is allowed
  to be read. 
*/

const char *
iso9660_get_xa_attr_str (uint16_t xa_attr)
{
  char *result = _getbuf();

  xa_attr = uint16_from_be (xa_attr);

  result[ 0] = (xa_attr & XA_ATTR_DIRECTORY) ? 'd' : '-';
  result[ 1] = (xa_attr & XA_ATTR_CDDA) ? 'a' : '-';
  result[ 2] = (xa_attr & XA_ATTR_INTERLEAVED) ? 'i' : '-';
  result[ 3] = (xa_attr & XA_ATTR_MODE2FORM2) ? '2' : '-';
  result[ 4] = (xa_attr & XA_ATTR_MODE2FORM1) ? '1' : '-';

  result[ 5] = (xa_attr & XA_PERM_XUSR) ? 'x' : '-';
  result[ 6] = (xa_attr & XA_PERM_RUSR) ? 'r' : '-';

  result[ 7] = (xa_attr & XA_PERM_XGRP) ? 'x' : '-';
  result[ 8] = (xa_attr & XA_PERM_RGRP) ? 'r' : '-';

  /* Hack alert: wonder if this should be ROTH and XOTH? */
  result[ 9] = (xa_attr & XA_PERM_XSYS) ? 'x' : '-';
  result[10] = (xa_attr & XA_PERM_RSYS) ? 'r' : '-';

  result[11] = '\0';

  return result;
}

iso9660_xa_t *
iso9660_xa_init (iso9660_xa_t *_xa, uint16_t uid, uint16_t gid, uint16_t attr, 
	      uint8_t filenum)
{
  cdio_assert (_xa != NULL);
  
  _xa->user_id = uint16_to_be (uid);
  _xa->group_id = uint16_to_be (gid);
  _xa->attributes = uint16_to_be (attr);

  _xa->signature[0] = 'X';
  _xa->signature[1] = 'A';

  _xa->filenum = filenum;

  _xa->reserved[0] 
    = _xa->reserved[1] 
    = _xa->reserved[2] 
    = _xa->reserved[3] 
    = _xa->reserved[4] = 0x00;

  return _xa;
}

void
iso9660_xa_free (iso9660_xa_t *_xa)
{
  if (_xa != NULL)
    free(_xa);
}

/*!
  Returns POSIX mode bitstring for a given file.
*/
posix_mode_t 
iso9660_get_posix_filemode_from_xa(uint16_t i_perms) 
{
  posix_mode_t mode = 0;
  
  if (i_perms & XA_PERM_RUSR)  mode |= S_IRUSR;
  if (i_perms & XA_PERM_XUSR)  mode |= S_IXUSR;
  
#ifdef S_IRGRP
  if (i_perms & XA_PERM_RGRP)  mode |= S_IRGRP;
#endif
#ifdef S_IXGRP
  if (i_perms & XA_PERM_XGRP)  mode |= S_IXGRP;
#endif
  
#ifdef S_IROTH
  if (i_perms & XA_PERM_ROTH)  mode |= S_IROTH;
#endif
#ifdef S_IXOTH
  if (i_perms & XA_PERM_XOTH)  mode |= S_IXOTH;
#endif
  
  if (i_perms & XA_ATTR_DIRECTORY)  mode |= S_IFDIR;
  
  return mode;
}

