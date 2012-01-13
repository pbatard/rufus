/*
  Copyright (C) 2003, 2004, 2005, 2008, 2009, 2011
  Rocky Bernstein <rocky@gnu.org>

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

/* Internal routines for CD I/O drivers. */


#ifndef __CDIO_PRIVATE_H__
#define __CDIO_PRIVATE_H__

#if defined(HAVE_CONFIG_H) && !defined(LIBCDIO_CONFIG_H)
# include "config.h"
#endif

#include <cdio/cdio.h>
#include <cdio/disc.h>
#include <cdio/iso9660.h>

#define CdioDataSource_t int

  /*! Implementation of CdIo type */
  struct _CdIo {
//    driver_id_t driver_id; /**< Particular driver opened. */
//    cdio_funcs_t op;       /**< driver-specific routines handling
//			        implementation*/
    void *env;             /**< environment. Passed to routine above. */
  };

  typedef struct {
    char *source_name;      /**< Name used in open. */
    bool  init;             /**< True if structure has been initialized */
    bool  toc_init;         /**< True if TOC read in */
    bool  b_cdtext_error;   /**< True if trouble reading CD-Text */
    
    int   ioctls_debugged;  /**< for debugging */

    /* Only one of data_source or fd is used; fd  is for CD-ROM
       devices and the data_source for stream reading (bincue, nrg, toc,
       network).
     */
    CdioDataSource_t *data_source;
    int     fd;             /**< File descriptor of device */
    track_t i_first_track;  /**< The starting track number. */
    track_t i_tracks;       /**< The number of tracks. */

    uint8_t i_joliet_level; /**< 0 = no Joliet extensions.
			       1-3: Joliet level. */
    iso9660_pvd_t pvd;      
    iso9660_svd_t svd;      
    CdIo_t   *cdio;         /**< a way to call general cdio routines. */
//    cdtext_t *cdtext;       /**< CD-Text for disc. */
//    track_flags_t track_flags[CDIO_CD_MAX_TRACKS+1];

    /* Memorized sense reply of the most recent SCSI command.
       Recorded by driver implementations of cdio_funcs_t.run_mmc_cmd(). 
       Read by API function mmc_get_cmd_scsi_sense().
    */
//    unsigned char  scsi_mmc_sense[263];   /* See SPC-3 4.5.3 : 252 bytes legal
//                                             but 263 bytes possible */
//    int            scsi_mmc_sense_valid;  /* Number of valid sense bytes */

    /* Memorized eventual system specific SCSI address tuple text.
       Empty text means that there is no such text defined for the drive.
       NULL means that the driver does not support "scsi-tuple".
       To be read by cdio_get_arg("scsi-tuple").
       System specific suffixes to the key may demand and eventually
       guarantee a further specified format.
       E.g. "scsi-tuple-linux" guarantees either "Bus,Host,Channel,Target,Lun",
                               or empty text, or NULL. No other forms.
    */
    char *scsi_tuple;
  } generic_img_private_t;
  
#endif
