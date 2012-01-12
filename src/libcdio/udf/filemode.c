/*
  filemode.c -- make a string describing file modes

  Copyright (C) 2005, 2008, 2011 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 1985, 1990, 1993, 1998-2000 Free Software Foundation, Inc.

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

#if HAVE_CONFIG_H
# include <config.h>
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif 

#include <cdio/udf.h>

#if !S_IRUSR
# if S_IREAD
#  define S_IRUSR S_IREAD
# else
#  define S_IRUSR 00400
# endif
#endif

#if !S_IWUSR
# if S_IWRITE
#  define S_IWUSR S_IWRITE
# else
#  define S_IWUSR 00200
# endif
#endif

#if !S_IXUSR
# if S_IEXEC
#  define S_IXUSR S_IEXEC
# else
#  define S_IXUSR 00100
# endif
#endif

#if !S_IRGRP
# define S_IRGRP (S_IRUSR >> 3)
#endif
#if !S_IWGRP
# define S_IWGRP (S_IWUSR >> 3)
#endif
#if !S_IXGRP
# define S_IXGRP (S_IXUSR >> 3)
#endif
#if !S_IROTH
# define S_IROTH (S_IRUSR >> 6)
#endif
#if !S_IWOTH
# define S_IWOTH (S_IWUSR >> 6)
#endif
#if !S_IXOTH
# define S_IXOTH (S_IXUSR >> 6)
#endif

#ifdef STAT_MACROS_BROKEN
# undef S_ISBLK
# undef S_ISCHR
# undef S_ISDIR
# undef S_ISFIFO
# undef S_ISLNK
# undef S_ISMPB
# undef S_ISMPC
# undef S_ISNWK
# undef S_ISREG
# undef S_ISSOCK
#endif /* STAT_MACROS_BROKEN.  */

#if !defined S_ISBLK && defined S_IFBLK
# define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif
#if !defined S_ISCHR && defined S_IFCHR
# define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif
#if !defined S_ISDIR && defined S_IFDIR
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#if !defined S_ISREG && defined S_IFREG
# define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined S_ISFIFO && defined S_IFIFO
# define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#endif
#if !defined S_ISLNK && defined S_IFLNK
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#if !defined S_ISSOCK && defined S_IFSOCK
# define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif
#if !defined S_ISMPB && defined S_IFMPB /* V7 */
# define S_ISMPB(m) (((m) & S_IFMT) == S_IFMPB)
# define S_ISMPC(m) (((m) & S_IFMT) == S_IFMPC)
#endif
#if !defined S_ISNWK && defined S_IFNWK /* HP/UX */
# define S_ISNWK(m) (((m) & S_IFMT) == S_IFNWK)
#endif
#if !defined S_ISDOOR && defined S_IFDOOR /* Solaris 2.5 and up */
# define S_ISDOOR(m) (((m) & S_IFMT) == S_IFDOOR)
#endif
#if !defined S_ISCTG && defined S_IFCTG /* MassComp */
# define S_ISCTG(m) (((m) & S_IFMT) == S_IFCTG)
#endif



/* Set the 's' and 't' flags in file attributes string CHARS,
   according to the file mode BITS.  */

static void
setst (mode_t bits, char *chars)
{
#ifdef S_ISUID
  if (bits & S_ISUID)
    {
      if (chars[3] != 'x')
	/* Set-uid, but not executable by owner.  */
	chars[3] = 'S';
      else
	chars[3] = 's';
    }
#endif
#ifdef S_ISGID
  if (bits & S_ISGID)
    {
      if (chars[6] != 'x')
	/* Set-gid, but not executable by group.  */
	chars[6] = 'S';
      else
	chars[6] = 's';
    }
#endif
#ifdef S_ISVTX
  if (bits & S_ISVTX)
    {
      if (chars[9] != 'x')
	/* Sticky, but not executable by others.  */
	chars[9] = 'T';
      else
	chars[9] = 't';
    }
#endif
}

/* Return a character indicating the type of file described by
   file mode BITS:
   'd' for directories
   'D' for doors
   'b' for block special files
   'c' for character special files
   'n' for network special files
   'm' for multiplexor files
   'M' for an off-line (regular) file
   'l' for symbolic links
   's' for sockets
   'p' for fifos
   'C' for contigous data files
   '-' for regular files
   '?' for any other file type.  */

static char
ftypelet (mode_t bits)
{
#ifdef S_ISBLK
  if (S_ISBLK (bits))
    return 'b';
#endif
  if (S_ISCHR (bits))
    return 'c';
  if (S_ISDIR (bits))
    return 'd';
  if (S_ISREG (bits))
    return '-';
#ifdef S_ISFIFO
  if (S_ISFIFO (bits))
    return 'p';
#endif
#ifdef S_ISLNK
  if (S_ISLNK (bits))
    return 'l';
#endif
#ifdef S_ISSOCK
  if (S_ISSOCK (bits))
    return 's';
#endif
#ifdef S_ISMPC
  if (S_ISMPC (bits))
    return 'm';
#endif
#ifdef S_ISNWK
  if (S_ISNWK (bits))
    return 'n';
#endif
#ifdef S_ISDOOR
  if (S_ISDOOR (bits))
    return 'D';
#endif
#ifdef S_ISCTG
  if (S_ISCTG (bits))
    return 'C';
#endif

  /* The following two tests are for Cray DMF (Data Migration
     Facility), which is a HSM file system.  A migrated file has a
     `st_dm_mode' that is different from the normal `st_mode', so any
     tests for migrated files should use the former.  */

#ifdef S_ISOFD
  if (S_ISOFD (bits))
    /* off line, with data  */
    return 'M';
#endif
#ifdef S_ISOFL
  /* off line, with no data  */
  if (S_ISOFL (bits))
    return 'M';
#endif
  return '?';
}

/*! udf_mode_string - fill in string STR with an ls-style ASCII
   representation of the st_mode field of file stats block STATP.
   10 characters are stored in STR; no terminating null is added.
   The characters stored in STR are:

   0	File type.  'd' for directory, 'c' for character
	special, 'b' for block special, 'm' for multiplex,
	'l' for symbolic link, 's' for socket, 'p' for fifo,
	'-' for regular, '?' for any other file type

   1	'r' if the owner may read, '-' otherwise.

   2	'w' if the owner may write, '-' otherwise.

   3	'x' if the owner may execute, 's' if the file is
	set-user-id, '-' otherwise.
	'S' if the file is set-user-id, but the execute
	bit isn't set.

   4	'r' if group members may read, '-' otherwise.

   5	'w' if group members may write, '-' otherwise.

   6	'x' if group members may execute, 's' if the file is
	set-group-id, '-' otherwise.
	'S' if it is set-group-id but not executable.

   7	'r' if any user may read, '-' otherwise.

   8	'w' if any user may write, '-' otherwise.

   9	'x' if any user may execute, 't' if the file is "sticky"
	(will be retained in swap space after execution), '-'
	otherwise.
	'T' if the file is sticky but not executable.  */

char *
udf_mode_string (mode_t i_mode, char *psz_str)
{
  psz_str[ 0] = ftypelet (i_mode);
  psz_str[ 1] = i_mode & S_IRUSR ? 'r' : '-';
  psz_str[ 2] = i_mode & S_IWUSR ? 'w' : '-';
  psz_str[ 3] = i_mode & S_IXUSR ? 'x' : '-';
  psz_str[ 4] = i_mode & S_IRGRP ? 'r' : '-';
  psz_str[ 5] = i_mode & S_IWGRP ? 'w' : '-';
  psz_str[ 6] = i_mode & S_IXGRP ? 'x' : '-';
  psz_str[ 7] = i_mode & S_IROTH ? 'r' : '-';
  psz_str[ 8] = i_mode & S_IWOTH ? 'w' : '-';
  psz_str[ 9] = i_mode & S_IXOTH ? 'x' : '-';
  psz_str[10] = '\0';
  setst (i_mode, psz_str);
  return psz_str;
}
