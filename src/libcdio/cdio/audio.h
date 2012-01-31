/* -*- c -*-
    Copyright (C) 2005, 2007, 2008 Rocky Bernstein <rocky@gnu.org>

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

/** \file audio.h 
 *
 *  \brief The top-level header for CD audio-related libcdio
 *         calls.  These control playing of the CD-ROM through its 
 *         line-out jack.
 */
#ifndef __CDIO_AUDIO_H__
#define __CDIO_AUDIO_H__

#include <cdio/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*! This struct is used by the cdio_audio_read_subchannel */
  typedef struct cdio_subchannel_s 
  {
    uint8_t format;
    uint8_t audio_status;
    uint8_t address:	4;
    uint8_t control:	4;
    uint8_t track;
    uint8_t index;
    msf_t   abs_addr;
    msf_t   rel_addr;
  } cdio_subchannel_t;
  
  /*! This struct is used by cdio_audio_get_volume and cdio_audio_set_volume */
  typedef struct cdio_audio_volume_s
  {
    uint8_t level[4];
  } cdio_audio_volume_t;
  

  /*! This struct is used by the CDROMPLAYTRKIND ioctl */
  typedef struct cdio_track_index_s
  {
    uint8_t	i_start_track;	/**< start track */
    uint8_t	i_start_index;	/**< start index */
    uint8_t	i_end_track;	/**< end track */
    uint8_t	i_end_index;	/**< end index */
  } cdio_track_index_t;

  /*!
    Get volume of an audio CD.
    
    @param p_cdio the CD object to be acted upon.
    @param p_volume place to put the list of volume outputs levels

    p_volume can be NULL in which case we return only whether the driver
    has the ability to get the volume or not.

  */
  driver_return_code_t cdio_audio_get_volume (CdIo_t *p_cdio,  /*out*/
					      cdio_audio_volume_t *p_volume);

  /*! 
    Return the number of seconds (discarding frame portion) of an MSF 
  */
  uint32_t cdio_audio_get_msf_seconds(msf_t *p_msf);

  /*!
    Pause playing CD through analog output

    @param p_cdio the CD object to be acted upon.
  */
  driver_return_code_t cdio_audio_pause (CdIo_t *p_cdio);

  /*!
    Playing CD through analog output at the given MSF.

    @param p_cdio the CD object to be acted upon.
    @param p_start_msf pointer to staring MSF
    @param p_end_msf pointer to ending MSF
  */
  driver_return_code_t cdio_audio_play_msf (CdIo_t *p_cdio, 
					    /*in*/msf_t *p_start_msf,
					    /*in*/ msf_t *p_end_msf);

  /*!
    Playing CD through analog output at the desired track and index

    @param p_cdio the CD object to be acted upon.
    @param p_track_index location to start/end.
  */
  driver_return_code_t cdio_audio_play_track_index 
  ( CdIo_t *p_cdio,  cdio_track_index_t *p_track_index);

  /*!
    Get subchannel information.

    @param p_cdio the CD object to be acted upon.
    @param p_subchannel place for returned subchannel information
  */
  driver_return_code_t cdio_audio_read_subchannel (CdIo_t *p_cdio, 
						   /*out*/ cdio_subchannel_t *p_subchannel);

  /*!
    Resume playing an audio CD.
    
    @param p_cdio the CD object to be acted upon.

  */
  driver_return_code_t cdio_audio_resume (CdIo_t *p_cdio);

  /*!
    Set volume of an audio CD.
    
    @param p_cdio the CD object to be acted upon.
    @param p_volume place for returned volume-level information

  */
  driver_return_code_t cdio_audio_set_volume (CdIo_t *p_cdio, /*out*/
					      cdio_audio_volume_t *p_volume);

  /*!
    Stop playing an audio CD.
    
    @param p_cdio the CD object to be acted upon.

  */
  driver_return_code_t cdio_audio_stop (CdIo_t *p_cdio);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CDIO_AUDIO_H__ */
