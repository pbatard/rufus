/*
    Copyright (C) 2004, 2010 Rocky Bernstein <rocky@gnu.org>
    Modeled after GNU/Linux definitions in linux/cdrom.h

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

/**
   \file dvd.h 
   \brief Definitions for DVD access.

   The documents we make use of are described Multi-Media Commands
   (MMC). This document generally has a numeric level number
   appended. For example MMC-5 refers to ``Multi-Media Commands - 5'
   which is the current version in 2010.

*/

#ifndef __CDIO_DVD_H__
#define __CDIO_DVD_H__

#include <cdio/types.h>

/**
   Values used in a READ DVD STRUCTURE
 */

#define CDIO_DVD_STRUCT_PHYSICAL	0x00
#define CDIO_DVD_STRUCT_COPYRIGHT	0x01
#define CDIO_DVD_STRUCT_DISCKEY	        0x02
#define CDIO_DVD_STRUCT_BCA		0x03
#define CDIO_DVD_STRUCT_MANUFACT	0x04

/**
    Media definitions for "DVD Book" from MMC-5 Table 400, page 419.
*/
#define CDIO_DVD_BOOK_DVD_ROM     0x0 /**< DVD-ROM */
#define CDIO_DVD_BOOK_DVD_RAM     0x1 /**< DVD-RAM */
#define CDIO_DVD_BOOK_DVD_R       0x2 /**< DVD-R */
#define CDIO_DVD_BOOK_DVD_RW      0x3 /**< DVD-RW */
#define CDIO_DVD_BOOK_HD_DVD_ROM  0x4 /**< HD DVD-ROM */
#define CDIO_DVD_BOOK_HD_DVD_RAM  0x5 /**< HD DVD-RAM */
#define CDIO_DVD_BOOK_HD_DVD_R    0x6 /**< HD DVD-R */
#define CDIO_DVD_BOOK_DVD_PRW     0x9 /**< DVD+RW */
#define CDIO_DVD_BOOK_DVD_PR      0xa /**< DVD+R  */
#define CDIO_DVD_BOOK_DVD_PRW_DL  0xd /**< DVD+RW DL  */
#define CDIO_DVD_BOOK_DVD_PR_DL   0xe /**< DVD+R DL  */

typedef struct cdio_dvd_layer {
  unsigned int book_version	: 4;
  unsigned int book_type	: 4;
  unsigned int min_rate	        : 4;
  unsigned int disc_size	: 4;
  unsigned int layer_type	: 4;
  unsigned int track_path	: 1;
  unsigned int nlayers	        : 2;
  unsigned int track_density	: 4;
  unsigned int linear_density   : 4;
  unsigned int bca		: 1;
  uint32_t start_sector;
  uint32_t end_sector;
  uint32_t end_sector_l0;
} cdio_dvd_layer_t;

/**
    Maximum number of layers in a DVD.  
 */
#define CDIO_DVD_MAX_LAYERS	4

typedef struct cdio_dvd_physical {
  uint8_t type;
  uint8_t layer_num;
  cdio_dvd_layer_t layer[CDIO_DVD_MAX_LAYERS];
} cdio_dvd_physical_t;

typedef struct cdio_dvd_copyright {
  uint8_t type;
  
  uint8_t layer_num;
  uint8_t cpst;
  uint8_t rmi;
} cdio_dvd_copyright_t;

typedef struct cdio_dvd_disckey {
  uint8_t type;
  
  unsigned agid	: 2;
  uint8_t value[2048];
} cdio_dvd_disckey_t;

typedef struct cdio_dvd_bca {
  uint8_t type;
  
  int len;
  uint8_t value[188];
} cdio_dvd_bca_t;

typedef struct cdio_dvd_manufact {
  uint8_t type;
  
  uint8_t layer_num;
  int len;
  uint8_t value[2048];
} cdio_dvd_manufact_t;

typedef union {
  uint8_t type;
  
  cdio_dvd_physical_t	physical;
  cdio_dvd_copyright_t	copyright;
  cdio_dvd_disckey_t	disckey;
  cdio_dvd_bca_t	bca;
  cdio_dvd_manufact_t	manufact;
} cdio_dvd_struct_t;

#endif /* __CDIO_DVD_H__ */
