/*
  Copyright (C) 2005, 2008, 2010, 2012 Rocky Bernstein <rocky@gnu.org>

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
/* Access routines */

/* udf_private.h has to come first else _FILE_OFFSET_BITS are redefined in
   say opensolaris. */
#include "udf_private.h"
#include <cdio/bytesex.h>
#include "filemode.h"

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

/** The below variables are trickery to force enum symbol values to be
    recorded in debug symbol tables. They are used to allow one to refer
    to the enumeration value names in the typedefs above in a debugger
    and debugger expressions
*/
tag_id_t                 debug_tagid;
file_characteristics_t   debug_file_characteristics;
icbtag_file_type_enum_t  debug_icbtag_file_type_enum;
icbtag_flag_enum_t       debug_flag_enum;
ecma_167_enum1_t         debug_ecma_167_enum1;
ecma_167_timezone_enum_t debug_ecma_167_timezone_enum;
udf_enum1_t              debug_udf_enum1;
  

/*!
  Returns POSIX mode bitstring for a given file.
*/
mode_t 
udf_get_posix_filemode(const udf_dirent_t *p_udf_dirent) 
{
  udf_file_entry_t udf_fe;
  mode_t mode = 0;

  if (udf_get_file_entry(p_udf_dirent, &udf_fe)) {
    uint32_t i_perms;
#ifdef S_ISUID
    uint16_t i_flags;

    i_flags = uint16_from_le(udf_fe.icb_tag.flags);
#endif
    i_perms = uint32_from_le(udf_fe.permissions);

    if (i_perms & FE_PERM_U_READ)  mode |= S_IRUSR;
    if (i_perms & FE_PERM_U_WRITE) mode |= S_IWUSR;
    if (i_perms & FE_PERM_U_EXEC)  mode |= S_IXUSR;
    
#ifdef S_IRGRP
    if (i_perms & FE_PERM_G_READ)  mode |= S_IRGRP;
    if (i_perms & FE_PERM_G_WRITE) mode |= S_IWGRP;
    if (i_perms & FE_PERM_G_EXEC)  mode |= S_IXGRP;
#endif
    
#ifdef S_IROTH
    if (i_perms & FE_PERM_O_READ)  mode |= S_IROTH;
    if (i_perms & FE_PERM_O_WRITE) mode |= S_IWOTH;
    if (i_perms & FE_PERM_O_EXEC)  mode |= S_IXOTH;
#endif

    switch (udf_fe.icb_tag.file_type) {
    case ICBTAG_FILE_TYPE_DIRECTORY: 
      mode |= S_IFDIR;
      break;
    case ICBTAG_FILE_TYPE_REGULAR:
      mode |= S_IFREG;
      break;
#ifdef S_IFLNK
    case ICBTAG_FILE_TYPE_SYMLINK:
      mode |= S_IFLNK;
      break;
#endif
    case ICBTAG_FILE_TYPE_CHAR:
      mode |= S_IFCHR;
      break;
#ifdef S_IFSOCK
    case ICBTAG_FILE_TYPE_SOCKET:
      mode |= S_IFSOCK;
      break;
#endif
    case ICBTAG_FILE_TYPE_BLOCK:
      mode |= S_IFBLK;
      break;
    default: ;
    };
  
#ifdef S_ISUID
    if (i_flags & ICBTAG_FLAG_SETUID) mode |= S_ISUID;
    if (i_flags & ICBTAG_FLAG_SETGID) mode |= S_ISGID;
    if (i_flags & ICBTAG_FLAG_STICKY) mode |= S_ISVTX;
#endif
  }
  
  return mode;
  
}

/*!
  Return the partition number of the the opened udf handle. -1 
  Is returned if we have an error.
*/
int16_t udf_get_part_number(const udf_t *p_udf)
{
  if (!p_udf) return -1;
  return p_udf->i_partition;
}

