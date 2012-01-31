/*
  Copyright (C) 2003, 2004, 2005, 2008, 2011
  Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

  See also iso9660.h by Eric Youngdale (1993).

  Copyright 1993 Yggdrasil Computing, Incorporated
  Copyright (c) 1999,2000 J. Schilling

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

#ifndef __CDIO_ISO9660_PRIVATE_H__
#define __CDIO_ISO9660_PRIVATE_H__

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/types.h>

#define ISO_VERSION             1

PRAGMA_BEGIN_PACKED

typedef struct iso_volume_descriptor_s {
  uint8_t  type;      /**< 7.1.1 */
  char     id[5];     /**< "CD001" (ISO_STANDARD_ID) */
  uint8_t  version;   /**< 7.1.1 */
  char     data[2041];
}  GNUC_PACKED iso_volume_descriptor_t;

#define iso_volume_descriptor_t_SIZEOF ISO_BLOCKSIZE

#define iso9660_pvd_t_SIZEOF ISO_BLOCKSIZE

/*
 * XXX JS: The next structure has an odd length!
 * Some compilers (e.g. on Sun3/mc68020) padd the structures to even length.
 * For this reason, we cannot use sizeof (struct iso_path_table) or
 * sizeof (struct iso_directory_record) to compute on disk sizes.
 * Instead, we use offsetof(..., name) and add the name size.
 * See mkisofs.h
 */

/** We use this to help us look up the parent inode numbers. */
typedef struct iso_path_table_s {
  uint8_t  name_len; /**< 7.1.1 */
  uint8_t  xa_len;   /**< 7.1.1 */
  uint32_t extent;   /**< 7.3.1/7.3.2 */
  uint16_t parent;   /**< 7.2.1/7.2.2 */
  char     name[EMPTY_ARRAY_SIZE];
} GNUC_PACKED iso_path_table_t;

#define iso_path_table_t_SIZEOF 8

#define iso9660_dir_t_SIZEOF 33

PRAGMA_END_PACKED

#endif /* __CDIO_ISO9660_PRIVATE_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
