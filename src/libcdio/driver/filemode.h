/*
  filemode.h -- file modes common definitions

  Copyright (C) 2005, 2008, 2011, 2012 Rocky Bernstein <rocky@gnu.org>
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

#ifndef CDIO_DRIVER_FILEMODE_H_
#define CDIO_DRIVER_FILEMODE_H_

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifndef S_IRUSR
# ifdef S_IREAD
#  define S_IRUSR S_IREAD
# else
#  define S_IRUSR 00400
# endif
#endif

#ifndef S_IWUSR
# ifdef S_IWRITE
#  define S_IWUSR S_IWRITE
# else
#  define S_IWUSR 00200
# endif
#endif

#ifndef S_IXUSR
# ifdef S_IEXEC
#  define S_IXUSR S_IEXEC
# else
#  define S_IXUSR 00100
# endif
#endif

#ifndef S_IRGRP
# define S_IRGRP (S_IRUSR >> 3)
#endif
#ifndef S_IWGRP
# define S_IWGRP (S_IWUSR >> 3)
#endif
#ifndef S_IXGRP
# define S_IXGRP (S_IXUSR >> 3)
#endif
#ifndef S_IROTH
# define S_IROTH (S_IRUSR >> 6)
#endif
#ifndef S_IWOTH
# define S_IWOTH (S_IWUSR >> 6)
#endif
#ifndef S_IXOTH
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

#if !defined S_IFBLK && defined _WIN32
# define S_IFBLK 0x3000
#endif
#if !defined S_IFIFO && defined _WIN32
# define S_IFIFO 0x1000
#endif

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
#if !defined HAVE_S_ISLNK
# if !defined S_ISLNK && defined S_IFLNK
#  define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
# else
#  define S_ISLNK(m) ((void)m, 0)
# endif
#endif
#if !defined HAVE_S_ISSOCK
# if !defined S_ISSOCK && defined S_IFSOCK
#  define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
# else
#  define S_ISSOCK(m) ((void)m, 0)
# endif
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

#endif /* CDIO_DRIVER_FILEMODE_H_ */
