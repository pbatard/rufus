/*
    Copyright (C) 2005, 2006, 2008, 2012 Rocky Bernstein <rocky@gnu.org>

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

/** \file track.h
 *  \brief  The top-level header for track-related libcdio calls.
 */
#ifndef CDIO_TRACK_H_
#define CDIO_TRACK_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*! Printable tags for track_format_t enumeration.  */
  extern const char *track_format2str[6];

  typedef enum  {
    TRACK_FORMAT_AUDIO,   /**< Audio track, e.g. CD-DA */
    TRACK_FORMAT_CDI,     /**< CD-i. How this is different from DATA below? */
    TRACK_FORMAT_XA,      /**< Mode2 of some sort */
    TRACK_FORMAT_DATA,    /**< Mode1 of some sort */
    TRACK_FORMAT_PSX,     /**< Playstation CD. Like audio but only 2336 bytes
                           *   of user data.
                           */
    TRACK_FORMAT_ERROR    /**< Dunno what is, or some other error. */
  } track_format_t;

  typedef enum {
    CDIO_TRACK_FLAG_FALSE,
    CDIO_TRACK_FLAG_TRUE,
    CDIO_TRACK_FLAG_ERROR,
    CDIO_TRACK_FLAG_UNKNOWN
  } track_flag_t;

  /*! \brief Structure containing attributes associated with a track */
  typedef struct {
    track_flag_t preemphasis; /**< Linear preemphasis on an audio track */
    track_flag_t copy_permit; /**< Whether copying is permitted */
    int channels;             /**< Number of audio channels, 2, 4. -2 if not
                                   implemented or -1 for error.
                              */
  } track_flags_t;

  /*! The leadout track is always 0xAA, regardless of # of tracks on
    disc, or what value may be used internally. For example although
    OS X uses a different value for the lead-out track internally than
    given below, programmers should use CDIO_CDROM_LEADOUT_TRACK and
    not worry about this.
  */

  /*! An enumeration for some of the CDIO_CDROM_* \#defines below. This
    isn't really an enumeration one would really use in a program; it
    is to be helpful in debuggers where wants just to refer to the
    CDIO_CDROM_* names and get something.
  */
  extern enum cdio_track_enums {
    CDIO_CDROM_LBA           = 0x01, /**< "logical block": first frame is #0 */
    CDIO_CDROM_MSF           = 0x02, /**< "minute-second-frame": binary, not
                                        BCD here! */
    CDIO_CDROM_DATA_TRACK    = 0x04,
    CDIO_CDROM_CDI_TRACK     = 0x10,
    CDIO_CDROM_XA_TRACK      = 0x20,
    CDIO_CD_MAX_TRACKS       =   99, /**< Largest CD track number */
    CDIO_CDROM_LEADOUT_TRACK = 0xAA, /**< Lead-out track number */
    CDIO_INVALID_TRACK       = 0xFF, /**<  Constant for invalid track number */

   } cdio_track_enums;

#define CDIO_CD_MIN_TRACK_NO  1 /**< Smallest CD track number */

  /*! track modes (Table 350)
    reference: MMC-3 draft revsion - 10g
  */
  typedef enum {
    AUDIO,                      /**< 2352 byte block length */
    MODE1,                      /**< 2048 byte block length */
    MODE1_RAW,                  /**< 2352 byte block length */
    MODE2,                      /**< 2336 byte block length */
    MODE2_FORM1,                /**< 2048 byte block length */
    MODE2_FORM2,                /**< 2324 byte block length */
    MODE2_FORM_MIX,             /**< 2336 byte block length */
    MODE2_RAW                   /**< 2352 byte block length */
  } trackmode_t;

  /*!
    Get the number of the first track.

    @return the track number or CDIO_INVALID_TRACK
    on error.
  */
  track_t cdio_get_first_track_num(const CdIo_t *p_cdio);

  /*!
    Return the last track number.
    CDIO_INVALID_TRACK is returned on error.
  */
  track_t cdio_get_last_track_num (const CdIo_t *p_cdio);


  /*! Find the track which contains lsn.
    CDIO_INVALID_TRACK is returned if the lsn outside of the CD or
    if there was some error.

    If the lsn is before the pregap of the first track 0 is returned.
    Otherwise we return the track that spans the lsn.
  */
  track_t cdio_get_track(const CdIo_t *p_cdio, lsn_t lsn);

  /*! Return number of channels in track: 2 or 4; -2 if not
      implemented or -1 for error.
      Not meaningful if track is not an audio track.
  */
  int cdio_get_track_channels(const CdIo_t *p_cdio, track_t i_track);

  /*! Return copy protection status on a track. Is this meaningful
      if not an audio track?
   */
  track_flag_t cdio_get_track_copy_permit(const CdIo_t *p_cdio,
                                          track_t i_track);

  /*!
    Get the format (audio, mode2, mode1) of track.
  */
  track_format_t cdio_get_track_format(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return true if we have XA data (green, mode2 form1) or
    XA data (green, mode2 form2). That is track begins:
    sync - header - subheader
    12     4      -  8

    FIXME: there's gotta be a better design for this and get_track_format?
  */
  bool cdio_get_track_green(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return the ending LSN for track number
    i_track in cdio.  CDIO_INVALID_LSN is returned on error.
  */
  lsn_t cdio_get_track_last_lsn(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Get the starting LBA for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    The "leadout" track is specified either by
    using i_track CDIO_CDROM_LEADOUT_TRACK or the total tracks+1.

    @param p_cdio object to get information from
    @param i_track  the track number we want the LSN for
    @return the starting LBA or CDIO_INVALID_LBA on error.
  */
  lba_t cdio_get_track_lba(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return the starting LSN for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    The "leadout" track is specified either by
    using i_track CDIO_CDROM_LEADOUT_TRACK or the total tracks+1.

    @param p_cdio object to get information from
    @param i_track  the track number we want the LSN for
    @return the starting LSN or CDIO_INVALID_LSN on error.
  */
  lsn_t cdio_get_track_lsn(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return the starting LBA for the pregap for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    @param p_cdio object to get information from
    @param i_track  the track number we want the LBA for
    @return the starting LBA or CDIO_INVALID_LBA on error.
  */
  lba_t cdio_get_track_pregap_lba(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return the starting LSN for the pregap for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    @param p_cdio object to get information from
    @param i_track  the track number we want the LSN for
    @return the starting LSN or CDIO_INVALID_LSN on error.
  */
  lsn_t cdio_get_track_pregap_lsn(const CdIo_t *p_cdio, track_t i_track);

  /*!
    Get the International Standard Recording Code (ISRC) for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    @return the International Standard Recording Code (ISRC) or NULL
    if there is none or we don't have the ability to get it.

    Note: The caller must free the returned string with cdio_free()
    when done with it.

  */
  char * cdio_get_track_isrc (const CdIo_t *p_cdio, track_t i_track);

  /*!
    Return the starting MSF (minutes/secs/frames) for track number
    i_track in p_cdio.  Track numbers usually start at something
    greater than 0, usually 1.

    The "leadout" track is specified either by
    using i_track CDIO_CDROM_LEADOUT_TRACK or the total tracks+1.

    @return true if things worked or false if there is no track entry.
  */
  bool cdio_get_track_msf(const CdIo_t *p_cdio, track_t i_track,
                          /*out*/ msf_t *msf);

  /*! Get linear preemphasis status on an audio track
      This is not meaningful if not an audio track?
   */
  track_flag_t cdio_get_track_preemphasis(const CdIo_t *p_cdio,
                                          track_t i_track);

  /*!
    Get the number of sectors between this track an the next.  This
    includes any pregap sectors before the start of the next track.
    Track numbers usually start at something
    greater than 0, usually 1.

    @return the number of sectors or 0 if there is an error.
  */
  unsigned int cdio_get_track_sec_count(const CdIo_t *p_cdio, track_t i_track);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_TRACK_H_ */
