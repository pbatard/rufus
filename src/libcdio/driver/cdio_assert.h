/*
  Copyright (C) 2008, 2011, 2012 Rocky Bernstein <rocky@gnu.org>
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

#ifndef CDIO_ASSERT_H_
#define CDIO_ASSERT_H_

#if defined(__GNUC__) && !defined(__MINGW32__)

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/types.h>
#include <cdio/logging.h>

#define cdio_assert(expr) \
 { \
   if (GNUC_UNLIKELY (!(expr))) cdio_log (CDIO_LOG_ASSERT, \
     "file %s: line %d (%s): assertion failed: (%s)", \
     __FILE__, __LINE__, __PRETTY_FUNCTION__, #expr); \
 }

#define cdio_assert_not_reached() \
 { \
   cdio_log (CDIO_LOG_ASSERT, \
     "file %s: line %d (%s): should not be reached", \
     __FILE__, __LINE__, __PRETTY_FUNCTION__); \
 }

#else /* non GNU C */

#include <assert.h>

#define cdio_assert(expr) \
 assert(expr)

#define cdio_assert_not_reached() \
 assert(0)

#endif

#endif /* CDIO_ASSERT_H_ */
