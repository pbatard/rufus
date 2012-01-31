/*
  $Id: udf_fs.h,v 1.3 2008/04/18 16:02:10 karl Exp $

  Copyright (C) 2006, 2008 Rocky Bernstein <rockyb@gnu.org>

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

#ifndef __CDIO_UDF_FS_H__
#define __CDIO_UDF_FS_H__

#include <cdio/ecma_167.h>
/**
 * Check the descriptor tag for both the correct id and correct checksum.
 * Return zero if all is good, -1 if not.
 */
int udf_checktag(const udf_tag_t *p_tag, udf_Uint16_t tag_id);

#endif /* __CDIO_UDF_FS_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
