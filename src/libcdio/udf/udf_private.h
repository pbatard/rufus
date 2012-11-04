/*
  Copyright (C) 2005, 2006, 2008, 2011, 2012 Rocky Bernstein <rocky@gnu.org>

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

#ifndef CDIO_UDF_UDF_PRIVATE_H_
#define CDIO_UDF_UDF_PRIVATE_H_

#if defined(HAVE_CONFIG_H) && !defined(LIBCDIO_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif 

#include <cdio/types.h>
#include <cdio/ecma_167.h>
#include <cdio/udf.h>
#include "_cdio_stdio.h"

/* Implementation of opaque types */

struct udf_s {
  bool                  b_stream;     /* Use stream pointer, else use p_cdio */
  off_t                 i_position;   /* Position in file if positive */
  CdioDataSource_t      *stream;      /* Stream pointer if stream */
  CdIo_t                *cdio;        /* Cdio pointer if read device */
  anchor_vol_desc_ptr_t anchor_vol_desc_ptr;
  uint32_t              pvd_lba;      /* sector of Primary Volume Descriptor */
  partition_num_t       i_partition;  /* partition number */
  uint32_t              i_part_start; /* start of Partition Descriptor */
  uint32_t              lvd_lba;      /* sector of Logical Volume Descriptor */
  uint32_t              fsd_offset;   /* lba of fileset descriptor */
};

#endif /* CDIO_UDF_UDF_PRIVATE_H_ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
