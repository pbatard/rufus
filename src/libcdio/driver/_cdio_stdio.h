/*
  Copyright (C) 2003, 2008, 2012 Rocky Bernstein <rocky@gnu.org>
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


#ifndef CDIO_STDIO_H_
#define CDIO_STDIO_H_

#include "_cdio_stream.h"

/*!
  Initialize a new stdio stream reading from pathname.
  A pointer to the stream is returned or NULL if there was an error.

  cdio_stream_free should be called on the returned value when you
  don't need the stream any more. No other finalization is needed.
 */
CdioDataSource_t * cdio_stdio_new(const char psz_path[]);

/*!
  Deallocate resources assocaited with obj. After this obj is unusable.
*/
void cdio_stdio_destroy(CdioDataSource_t *p_obj);


#endif /* CDIO_STDIO_H_ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
