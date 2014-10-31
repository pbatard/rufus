/*  
    Copyright (C) 2014 Robert Kausch <robert.kausch@freac.org>

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

/*!
 * \file memory.h 
 *
 * \brief memory management utility functions.
 *
*/

#ifndef CDIO_MEMORY_H_
#define CDIO_MEMORY_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*!
    Free the passed pointer.
  */
  void cdio_free(void *p_memory);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_MEMORY_H_ */
