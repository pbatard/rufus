/* -*- c -*-

    Copyright (C) 2004, 2005, 2006, 2008, 2010, 2012, 2014
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

/**
   \file disc.h 
   \brief  The top-level header for disc-related libcdio calls.
 */
#ifndef CDIO_DISC_H_
#define CDIO_DISC_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

    /**
       disc modes. The first combined from MMC-5 6.33.3.13 (Send
       CUESHEET), "DVD Book" from MMC-5 Table 400, page 419.  and
       GNU/Linux /usr/include/linux/cdrom.h and we've added DVD.
    */
  typedef enum {
    CDIO_DISC_MODE_CD_DA,           /**< CD-DA */
    CDIO_DISC_MODE_CD_DATA,         /**< CD-ROM form 1 */
    CDIO_DISC_MODE_CD_XA,           /**< CD-ROM XA form2 */
    CDIO_DISC_MODE_CD_MIXED,        /**< Some combo of above. */
    CDIO_DISC_MODE_DVD_ROM,         /**< DVD ROM (e.g. movies) */
    CDIO_DISC_MODE_DVD_RAM,         /**< DVD-RAM */
    CDIO_DISC_MODE_DVD_R,           /**< DVD-R */
    CDIO_DISC_MODE_DVD_RW,          /**< DVD-RW */
    CDIO_DISC_MODE_HD_DVD_ROM,      /**< HD DVD-ROM */
    CDIO_DISC_MODE_HD_DVD_RAM,      /**< HD DVD-RAM */
    CDIO_DISC_MODE_HD_DVD_R,        /**< HD DVD-R */
    CDIO_DISC_MODE_DVD_PR,          /**< DVD+R */
    CDIO_DISC_MODE_DVD_PRW,         /**< DVD+RW */
    CDIO_DISC_MODE_DVD_PRW_DL,      /**< DVD+RW DL */
    CDIO_DISC_MODE_DVD_PR_DL,       /**< DVD+R DL */
    CDIO_DISC_MODE_DVD_OTHER,       /**< Unknown/unclassified DVD type */
    CDIO_DISC_MODE_NO_INFO,
    CDIO_DISC_MODE_ERROR,
    CDIO_DISC_MODE_CD_I         /**< CD-i. */
  } discmode_t;

  extern const char *discmode2str[];

  /**
    Get binary CD-Text information for a CdIo_t object.

    @param p_cdio the CD object that may contain CD-Text information.
    @return malloc'd pointer to raw CD-Text data as stored on the disc or
    NULL if p_cdio is NULL or CD-Text information does not exist. Return
    value must be freed with cdio_free() when done with it and not NULL.
  */
  uint8_t * cdio_get_cdtext_raw (CdIo_t *p_cdio);

  /** 
    Get CD-Text information for a CdIo_t object.
 
    @param p_cdio the CD object that may contain CD-Text information.
    @return the CD-Text object or NULL if p_cdio is NULL
    or CD-Text information does not exist.
  */
  cdtext_t *cdio_get_cdtext (CdIo_t *p_cdio);

  /** 
    Get disc mode - the kind of CD (CD-DA, CD-ROM mode 1, CD-MIXED, etc.
    that we've got. The notion of "CD" is extended a little to include
    DVD's.
  */
  discmode_t cdio_get_discmode (CdIo_t *p_cdio);

  /**
    Get the lsn of the end of the CD

    @return the lsn. On error 0 or CDIO_INVALD_LSN.
  */
  lsn_t cdio_get_disc_last_lsn(const CdIo_t *p_cdio);
  
  /**
    Return the Joliet level recognized for p_cdio.
  */
  uint8_t cdio_get_joliet_level(const CdIo_t *p_cdio);

  /**
    Get the media catalog number (MCN) from the CD.

    @return the media catalog number or NULL if there is none or we
    don't have the ability to get it.

    Note: The caller must free the returned string with cdio_free()
    when done with it.

  */
  char * cdio_get_mcn (const CdIo_t *p_cdio);

  /**
    Get the number of tracks on the CD.

    @return the number of tracks, or CDIO_INVALID_TRACK if there is
    an error.
  */
  track_t cdio_get_num_tracks (const CdIo_t *p_cdio);
  
  /**
    Return true if discmode is some sort of CD.
  */
  bool cdio_is_discmode_cdrom (discmode_t discmode);
  
  /**
    Return true if discmode is some sort of DVD.
  */
  bool cdio_is_discmode_dvd (discmode_t discmode);
  
  /**
      cdio_stat_size is deprecated. @see cdio_get_disc_last_lsn  
  */
#define cdio_stat_size cdio_get_disc_last_lsn

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_DISC_H_ */
