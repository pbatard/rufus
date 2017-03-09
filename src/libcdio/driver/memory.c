/*
  Copyright (C) 2014-2015 Robert Kausch <robert.kausch@freac.org>

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

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <cdio/memory.h>
#include <cdio/types.h>

/*! 
  Free the passed pointer.
  
  @param p_memory a pointer to memory allocated by a libcdio funtion.
*/
void
cdio_free (void *p_memory)
{
  if (p_memory != NULL)
    free(p_memory);
}
