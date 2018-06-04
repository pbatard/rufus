/*
  Copyright (C) 2003-2005, 2008-2009, 2011-2012, 2016-2017
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


#ifndef CDIO_DRIVER_PRIVATE_H_
#define CDIO_DRIVER_PRIVATE_H_

#if defined(HAVE_CONFIG_H) && !defined(LIBCDIO_CONFIG_H)
# include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <cdio/cdio.h>
#include <cdio/audio.h>
#include <cdio/cdtext.h>
#include "mmc/mmc_private.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef HAVE_STRNDUP
#undef  strndup
#define strndup libcdio_strndup
static inline char *libcdio_strndup(const char *s, size_t n)
{
    char *result;
    size_t len = strlen (s);
    if (n < len)
        len = n;
    result = (char *) malloc (len + 1);
    if (!result)
        return 0;
    result[len] = '\0';
    return (char *) strncpy (result, s, len);
}
#endif /*HAVE_STRNDUP*/

  /*!
    Get directory name from file name.

    Callers must free return value after use.
   */
  extern char *cdio_dirname(const char *fname);

  /*!
    Construct an absolute file name from path and file name.

    Callers must free return value after use.
   */
  extern char *cdio_abspath(const char *cwd, const char *fname);

  /* Opaque type */
  typedef struct _CdioDataSource CdioDataSource_t;

#ifdef __cplusplus
}

#endif /* __cplusplus */

#include "generic.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


  typedef struct {

    /*!
      Get volume of an audio CD.

      @param p_env the CD object to be acted upon.

    */
    driver_return_code_t (*audio_get_volume)
         (void *p_env,  /*out*/ cdio_audio_volume_t *p_volume);

    /*!
      Pause playing CD through analog output

      @param p_env the CD object to be acted upon.
    */
    driver_return_code_t (*audio_pause) (void *p_env);

    /*!
      Playing CD through analog output

      @param p_env the CD object to be acted upon.
    */
    driver_return_code_t (*audio_play_msf) ( void *p_env,
                                             msf_t *p_start_msf,
                                             msf_t *p_end_msf );

    /*!
      Playing CD through analog output

      @param p_env the CD object to be acted upon.
    */
    driver_return_code_t (*audio_play_track_index)
         ( void *p_env, cdio_track_index_t *p_track_index );

    /*!
      Get subchannel information.

      @param p_env the CD object to be acted upon.
    */
    driver_return_code_t (*audio_read_subchannel)
         ( void *p_env, cdio_subchannel_t *subchannel );

    /*!
      Resume playing an audio CD.

      @param p_env the CD object to be acted upon.

    */
    driver_return_code_t (*audio_resume) ( void *p_env );

    /*!
      Set volume of an audio CD.

      @param p_env the CD object to be acted upon.

    */
    driver_return_code_t (*audio_set_volume)
         ( void *p_env,  cdio_audio_volume_t *p_volume );

    /*!
      Stop playing an audio CD.

      @param p_env the CD object to be acted upon.

    */
    driver_return_code_t (*audio_stop) ( void *p_env );

    /*!
      Eject media in CD drive. If successful, as a side effect we
      also free p_env.

      @param p_env the CD object to be acted upon.
      If the CD is ejected *p_env is freed and p_env set to NULL.
    */
    driver_return_code_t (*eject_media) ( void *p_env );

    /*!
      Release and free resources associated with cd.
    */
    void (*free) (void *p_env);

    /*!
      Return the value associated with the key "arg".
    */
    const char * (*get_arg) (void *p_env, const char key[]);

    /*!
      Get the block size for subsequest read requests, via a SCSI MMC
      MODE_SENSE 6 command.
    */
    int (*get_blocksize) ( void *p_env );

    /*!
      Get cdtext information for a CdIo object.

      @param obj the CD object that may contain CD-TEXT information.
      @return the CD-TEXT object or NULL if obj is NULL
      or CD-TEXT information does not exist.
    */
    cdtext_t * (*get_cdtext) ( void *p_env );

    /*!
      Get raw cdtext information as on the disc for a CdIo object

      @param obj the CD object that may contain CD-TEXT information.
      @return pointer to the raw CD-TEXT data or NULL if obj is NULL
      or no CD-TEXT information present on the disc.

      free when done and not NULL.
    */
    uint8_t * (*get_cdtext_raw) ( void *p_env );

    /*!
      Return an array of device names. if CdIo is NULL (we haven't
      initialized a specific device driver), then find a suitable device
      driver.

      NULL is returned if we couldn't return a list of devices.
    */
    char ** (*get_devices) ( void );

    /*!
      Get the default CD device.

      @return a string containing the default CD device or NULL is
      if we couldn't get a default device.

      In some situations of drivers or OS's we can't find a CD device if
      there is no media in it and it is possible for this routine to return
      NULL even though there may be a hardware CD-ROM.
    */
    char * (*get_default_device) ( void );

    /*!
      Return the size of the CD in logical block address (LBA) units.
      @return the lsn. On error 0 or CDIO_INVALD_LSN.
    */
    lsn_t (*get_disc_last_lsn) ( void *p_env );

    /*!
      Get disc mode associated with cd_obj.
    */
    discmode_t (*get_discmode) ( void *p_env );

    /*!
      Return the what kind of device we've got.

      See cd_types.h for a list of bitmasks for the drive type;
    */
    void (*get_drive_cap) (const void *p_env,
                           cdio_drive_read_cap_t  *p_read_cap,
                           cdio_drive_write_cap_t *p_write_cap,
                           cdio_drive_misc_cap_t  *p_misc_cap);
    /*!
      Return the number of of the first track.
      CDIO_INVALID_TRACK is returned on error.
    */
    track_t (*get_first_track_num) ( void *p_env );

    /*!
      Get the CD-ROM hardware info via a SCSI MMC INQUIRY command.
      False is returned if we had an error getting the information.
    */
    bool (*get_hwinfo)
         ( const CdIo_t *p_cdio, /* out*/ cdio_hwinfo_t *p_hw_info );

    /*! Get the LSN of the first track of the last session of
      on the CD.

       @param p_cdio the CD object to be acted upon.
       @param i_last_session pointer to the session number to be returned.
    */
    driver_return_code_t (*get_last_session)
         ( void *p_env, /*out*/ lsn_t *i_last_session );

    /*!
      Find out if media has changed since the last call.
      @param p_env the CD object to be acted upon.
      @return 1 if media has changed since last call, 0 if not. Error
      return codes are the same as driver_return_code_t
    */
    int (*get_media_changed) ( const void *p_env );

    /*!
      Return the media catalog number MCN from the CD or NULL if
      there is none or we don't have the ability to get it.
    */
    char * (*get_mcn) ( const void *p_env );

    /*!
      Return the number of tracks in the current medium.
      CDIO_INVALID_TRACK is returned on error.
    */
    track_t (*get_num_tracks) ( void *p_env );

    /*! Return number of channels in track: 2 or 4; -2 if not
      implemented or -1 for error.
      Not meaningful if track is not an audio track.
    */
    int (*get_track_channels) ( const void *p_env, track_t i_track );

    /*! Return 0 if track is copy protected, 1 if not, or -1 for error
      or -2 if not implimented (yet). Is this meaningful if not an
      audio track?
    */
    track_flag_t (*get_track_copy_permit) ( void *p_env, track_t i_track );

    /*!
      Return the starting LBA for track number
      i_track in p_env.  Tracks numbers start at 1.
      The "leadout" track is specified either by
      using track_num LEADOUT_TRACK or the total tracks+1.
      CDIO_INVALID_LBA is returned on error.
    */
    lba_t (*get_track_lba) ( void *p_env, track_t i_track );

    /*!
      Return the starting LBA for the pregap for track number
      i_track in p_env.  Tracks numbers start at 1.
      CDIO_INVALID_LBA is returned on error.
    */
    lba_t (*get_track_pregap_lba) ( const void *p_env, track_t i_track );

    /*!
      Return the International Standard Recording Code (ISRC) for track number
      i_track in p_cdio.  Track numbers start at 1.

      Note: string is malloc'd so caller has to free() the returned
      string when done with it.
    */
    char * (*get_track_isrc) ( const void *p_env, track_t i_track );

    /*!
      Get format of track.
    */
    track_format_t (*get_track_format) ( void *p_env, track_t i_track );

    /*!
      Return true if we have XA data (green, mode2 form1) or
      XA data (green, mode2 form2). That is track begins:
      sync - header - subheader
      12     4      -  8

      FIXME: there's gotta be a better design for this and get_track_format?
    */
    bool (*get_track_green) ( void *p_env, track_t i_track );

    /*!
      Return the starting MSF (minutes/secs/frames) for track number
      i_track in p_env.  Tracks numbers start at 1.
      The "leadout" track is specified either by
      using i_track LEADOUT_TRACK or the total tracks+1.
      False is returned on error.
    */
    bool (*get_track_msf) ( void *p_env, track_t i_track, msf_t *p_msf );

    /*! Return 1 if track has pre-emphasis, 0 if not, or -1 for error
      or -2 if not implimented (yet). Is this meaningful if not an
      audio track?
    */
    track_flag_t (*get_track_preemphasis)
         ( const void  *p_env, track_t i_track );

    /*!
      lseek - reposition read/write file offset
      Returns (off_t) -1 on error.
      Similar to libc's lseek()
    */
    off_t (*lseek) ( void *p_env, off_t offset, int whence );

    /*!
      Reads into buf the next size bytes.
      Returns -1 on error.
      Similar to libc's read()
    */
    ssize_t (*read) ( void *p_env, void *p_buf, size_t i_size );

    /*!
      Reads a single mode2 sector from cd device into buf starting
      from lsn. Returns 0 if no error.
    */
    int (*read_audio_sectors) ( void *p_env, void *p_buf, lsn_t i_lsn,
                                unsigned int i_blocks );

    /*!
      Read a data sector

      @param p_env environment to read from

      @param p_buf place to read data into.  The caller should make sure
      this location can store at least CDIO_CD_FRAMESIZE,
      M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE depending
      on the kind of sector getting read. If you don't
      know whether you have a Mode 1/2, Form 1/ Form 2/Formless
      sector best to reserve space for the maximum,
      M2RAW_SECTOR_SIZE.

      @param i_lsn sector to read
      @param i_blocksize size of block. Should be either CDIO_CD_FRAMESIZE,
      M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE. See comment above under p_buf.
    */
    driver_return_code_t (*read_data_sectors)
         ( void *p_env, void *p_buf, lsn_t i_lsn, uint16_t i_blocksize,
           uint32_t i_blocks );

    /*!
      Reads a single mode2 sector from cd device into buf starting
      from lsn. Returns 0 if no error.
    */
    int (*read_mode2_sector)
         ( void *p_env, void *p_buf, lsn_t i_lsn, bool b_mode2_form2 );

    /*!
      Reads i_blocks of mode2 sectors from cd device into data starting
      from lsn.
      Returns 0 if no error.
    */
    int (*read_mode2_sectors)
         ( void *p_env, void *p_buf, lsn_t i_lsn, bool b_mode2_form2,
           unsigned int i_blocks );

    /*!
      Reads a single mode1 sector from cd device into buf starting
      from lsn. Returns 0 if no error.
    */
    int (*read_mode1_sector)
         ( void *p_env, void *p_buf, lsn_t i_lsn, bool mode1_form2 );

    /*!
      Reads i_blocks of mode1 sectors from cd device into data starting
      from lsn.
      Returns 0 if no error.
    */
    int (*read_mode1_sectors)
         ( void *p_env, void *p_buf, lsn_t i_lsn, bool mode1_form2,
           unsigned int i_blocks );

    bool (*read_toc) ( void *p_env ) ;

    /*!
      Run a SCSI MMC command.

      cdio              CD structure set by cdio_open().
      i_timeout_ms      time in milliseconds we will wait for the command
                        to complete.
      cdb_len           number of bytes in cdb (6, 10, or 12).
      cdb               CDB bytes. All values that are needed should be set on
                        input.
      b_return_data     TRUE if the command expects data to be returned in
                        the buffer
      len               Size of buffer
      buf               Buffer for data, both sending and receiving

      Returns 0 if command completed successfully.
    */
    mmc_run_cmd_fn_t run_mmc_cmd;

    /*!
      Set the arg "key" with "value" in the source device.
    */
    int (*set_arg) ( void *p_env, const char key[], const char value[] );

    /*!
      Set the blocksize for subsequent reads.
    */
    driver_return_code_t (*set_blocksize) ( void *p_env,
                                            uint16_t i_blocksize );

    /*!
      Set the drive speed.

      @return 0 if everything went okay, -1 if we had an error. is -2
      returned if this is not implemented for the current driver.
    */
    int (*set_speed) ( void *p_env, int i_speed );

  } cdio_funcs_t;


  /*! Implementation of CdIo type */
  struct _CdIo {
    driver_id_t driver_id; /**< Particular driver opened. */
    cdio_funcs_t op;       /**< driver-specific routines handling
                                implementation*/
    void *env;             /**< environment. Passed to routine above. */
  };

  /* This is used in drivers that must keep their own internal
     position pointer for doing seeks. Stream-based drivers (like bincue,
     nrg, toc, network) would use this.
   */
  typedef struct
  {
    off_t   buff_offset;      /* buffer offset in disk-image seeks. */
    track_t index;            /* Current track index in tocent. */
    lba_t   lba;              /* Current LBA */
  } internal_position_t;

  CdIo_t * cdio_new (generic_img_private_t *p_env, cdio_funcs_t *p_funcs);

  /* The below structure describes a specific CD Input driver  */
  typedef struct
  {
    driver_id_t  id;
    unsigned int flags;
    const char  *name;
    const char  *describe;
    bool (*have_driver) (void);
    CdIo_t *(*driver_open) (const char *psz_source_name);
    CdIo_t *(*driver_open_am) (const char *psz_source_name,
                             const char *psz_access_mode);
    char *(*get_default_device) (void);
    bool (*is_device) (const char *psz_source_name);
    char **(*get_devices) (void);
    driver_return_code_t (*close_tray) (const char *psz_device);
  } CdIo_driver_t;

  /* The below array gives of the drivers that are currently available for
     on a particular host. */
  extern CdIo_driver_t CdIo_driver[];

  /* The last valid entry of Cdio_driver. -1 means uninitialzed. -2
     means some sort of error.
   */
  extern int CdIo_last_driver;

  /* The below array gives all drivers that can possibly appear.
     on a particular host. */
  extern CdIo_driver_t CdIo_all_drivers[];

  /*!
    Add/allocate a drive to the end of drives.
    Use cdio_free_device_list() to free this device_list.
  */
  void cdio_add_device_list(char **device_list[], const char *psz_drive,
                            unsigned int *i_drives);

  driver_return_code_t close_tray_bsdi    (const char *psz_drive);
  driver_return_code_t close_tray_freebsd (const char *psz_drive);
  driver_return_code_t close_tray_linux   (const char *psz_drive);
  driver_return_code_t close_tray_netbsd  (const char *psz_drive);
  driver_return_code_t close_tray_osx     (const char *psz_drive);
  driver_return_code_t close_tray_solaris (const char *psz_drive);
  driver_return_code_t close_tray_win32   (const char *psz_drive);

  bool cdio_have_netbsd(void);
  CdIo_t * cdio_open_netbsd (const char *psz_source);
  char * cdio_get_default_device_netbsd(void);
  char **cdio_get_devices_netbsd(void);
  /*! Set up CD-ROM for reading using the NetBSD driver. The device_name is
      the some sort of device name.

     NULL is returned on error or there is no FreeBSD driver.

     @see cdio_open_cd, cdio_open
   */
  CdIo_t * cdio_open_am_netbsd (const char *psz_source,
                                const char *psz_access_mode);

  /*! DEPRICATED: use cdio_have_driver().
    True if AIX driver is available. */
  bool cdio_have_aix    (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if BSDI driver is available. */
  bool cdio_have_bsdi    (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if FreeBSD driver is available. */
  bool cdio_have_freebsd (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if GNU/Linux driver is available. */
  bool cdio_have_linux   (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if Sun Solaris driver is available. */
  bool cdio_have_solaris (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if IBM OS2 driver is available. */
  bool cdio_have_os2     (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if Apple OSX driver is available. */
  bool cdio_have_osx     (void);

  /*! DEPRICATED: use cdio_have_driver().
    True if Microsoft Windows driver is available. */
  bool cdio_have_win32   (void);

  /*! True if Nero driver is available. */
  bool cdio_have_nrg     (void);

  /*! True if BIN/CUE driver is available. */
  bool cdio_have_bincue  (void);

  /*! True if cdrdao CDRDAO driver is available. */
  bool cdio_have_cdrdao  (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_DRIVER_PRIVATE_H_ */
