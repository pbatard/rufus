/*
  Copyright (C) 2004-2006, 2008-2009, 2012-2013
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


#ifndef CDIO_DRIVER_GENERIC_H_
#define CDIO_DRIVER_GENERIC_H_

#if defined(HAVE_CONFIG_H) && !defined(LIBCDIO_CONFIG_H)
# include "config.h"
#endif

#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/iso9660.h>

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*!
    Things common to private device structures. Even though not all
    devices may have some of these fields, by listing common ones
    we facilitate writing generic routines and even cut-and-paste
    code.
   */
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

    uint8_t u_joliet_level; /**< 0 = no Joliet extensions.
                               1-3: Joliet level. */
    iso9660_pvd_t pvd;
    iso9660_svd_t svd;
    CdIo_t   *cdio;         /**< a way to call general cdio routines. */
    cdtext_t *cdtext;       /**< CD-Text for disc. */
    track_flags_t track_flags[CDIO_CD_MAX_TRACKS+1];

    /* Memorized sense reply of the most recent SCSI command.
       Recorded by driver implementations of cdio_funcs_t.run_mmc_cmd().
       Read by API function mmc_get_cmd_scsi_sense().
    */
    unsigned char  scsi_mmc_sense[263];   /* See SPC-3 4.5.3 : 252 bytes legal
                                             but 263 bytes possible */
    int            scsi_mmc_sense_valid;  /* Number of valid sense bytes */

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

  /*!
    Bogus eject media when there is no ejectable media, e.g. a disk image
    We always return 2. Should we also free resources?
  */
  driver_return_code_t cdio_generic_unimplemented_eject_media (void *p_env);

  /*!
    Set the blocksize for subsequent reads.

    @return -2 since it's not implemented.
  */
  driver_return_code_t
  cdio_generic_unimplemented_set_blocksize (void *p_user_data,
                                            uint16_t i_blocksize);

  /*!
    Set the drive speed.

    @return -2 since it's not implemented.
  */
  driver_return_code_t cdio_generic_unimplemented_set_speed (void *p_user_data,
                                                             int i_speed);

  /*!
    Release and free resources associated with cd.
  */
  void cdio_generic_free (void *p_env);

  /*!
    Initialize CD device.
  */
  bool cdio_generic_init (void *p_env, int open_mode);

  /*!
    Reads into buf the next size bytes.
    Returns -1 on error.
    Is in fact libc's read().
  */
  off_t cdio_generic_lseek (void *p_env, off_t offset, int whence);

  /*!
    Reads into buf the next size bytes.
    Returns -1 on error.
    Is in fact libc's read().
  */
  ssize_t cdio_generic_read (void *p_env, void *p_buf, size_t size);

  /*!
    Reads a single form1 sector from cd device into data starting
    from lsn. Returns 0 if no error.
  */
  int cdio_generic_read_form1_sector (void * user_data, void *data,
                                      lsn_t lsn);

  /*!
    Release and free resources associated with stream or disk image.
  */
  void cdio_generic_stdio_free (void *env);

  /*!
    Return true if source_name could be a device containing a CD-ROM on
    Win32
  */
  bool cdio_is_device_win32(const char *source_name);

  /*!
    Return true if source_name could be a device containing a CD-ROM on
    OS/2
  */
  bool cdio_is_device_os2(const char *source_name);


  /*!
    Return true if source_name could be a device containing a CD-ROM on
    most Unix servers with block and character devices.
  */
  bool cdio_is_device_generic(const char *source_name);


  /*!
    Like above, but don't give a warning device doesn't exist.
  */
  bool cdio_is_device_quiet_generic(const char *source_name);

  /*!
    Get cdtext information for a CdIo object .

    @param obj the CD object that may contain CD-TEXT information.
    @return the CD-TEXT object or NULL if obj is NULL
    or CD-TEXT information does not exist.
  */
  cdtext_t *get_cdtext_generic (void *p_user_data);

  /*!
    Return the number of of the first track.
    CDIO_INVALID_TRACK is returned on error.
  */
  track_t get_first_track_num_generic(void *p_user_data);

  /*!
    Return the number of tracks in the current medium.
  */
  track_t get_num_tracks_generic(void *p_user_data);

  /*!
    Get disc type associated with cd object.
  */
  discmode_t get_discmode_generic (void *p_user_data );

  /*!
    Same as above but only handles CD cases
  */
  discmode_t get_discmode_cd_generic (void *p_user_data );

  /*! Return number of channels in track: 2 or 4; -2 if not
    implemented or -1 for error.
    Not meaningful if track is not an audio track.
  */
  int  get_track_channels_generic(const void *p_user_data, track_t i_track);

  /*! Return 1 if copy is permitted on the track, 0 if not, or -1 for error.
    Is this meaningful if not an audio track?
  */
  track_flag_t get_track_copy_permit_generic(void *p_user_data,
                                             track_t i_track);

  /*! Return 1 if track has pre-emphasis, 0 if not, or -1 for error.
    Is this meaningful if not an audio track?

    pre-emphasis is a non linear frequency response.
  */
  track_flag_t get_track_preemphasis_generic(const void *p_user_data,
                                             track_t i_track);

  /*!
    Read cdtext information for a CdIo object .

    return true on success, false on error or CD-Text information does
    not exist.
  */
  uint8_t * read_cdtext_generic (void *p_env);

  void set_track_flags(track_flags_t *p_track_flag, uint8_t flag);

  /*! Read mode 1 or mode2 sectors (using cooked mode).  */
  driver_return_code_t read_data_sectors_generic (void *p_user_data,
                                                  void *p_buf, lsn_t i_lsn,
                                                  uint16_t i_blocksize,
                                                  uint32_t i_blocks);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_DRIVER_GENERIC_H_ */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
