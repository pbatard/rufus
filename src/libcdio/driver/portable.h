/*
  Copyright (C) 2006, 2008, 2011, 2012 Rocky Bernstein <rocky@gnu.org>

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

/* 
   This file contains definitions to fill in for differences or
   deficiencies to OS or compiler irregularities.  If this file is
   included other routines can be more portable.
*/

#ifndef CDIO_DRIVER_PORTABLE_H_
#define CDIO_DRIVER_PORTABLE_H_

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#if !defined(HAVE_FTRUNCATE)
# if defined (_WIN32)
#  define ftruncate chsize
# endif
#endif /*HAVE_FTRUNCATE*/

#if !defined(HAVE_SNPRINTF)
# if defined (_MSC_VER)
#  define snprintf _snprintf
# endif
#endif /*HAVE_SNPRINTF*/

#if !defined(HAVE_VSNPRINTF)
# if defined (_MSC_VER)
#  define vsnprintf _vsnprintf
# endif
#endif /*HAVE_SNPRINTF*/

#if !defined(HAVE_DRAND48) && defined(HAVE_RAND)
# define drand48()   (rand() / (double)RAND_MAX)
#endif

#endif /* CDIO_DRIVER_PORTABLE_H_ */
