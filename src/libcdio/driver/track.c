/*
  Copyright (C) 2003, 2004, 2005, 2008, 2011 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2001 Herbert Valerio Riedel <hvr@gnu.org>

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
/*! Track-related routines. */


#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/cdio.h>
#include "cdio_private.h"

const char *track_format2str[6] = 
  {
    "audio", "CD-i", "XA", "data", "PSX", "error"
  };

/* Variables to hold debugger-helping enumerations */
enum cdio_track_enums;

/*!
  Return the number of the first track. 
  CDIO_INVALID_TRACK is returned on error.
*/
track_t
cdio_get_first_track_num (const CdIo_t *p_cdio)
{
  if (NULL == p_cdio) return CDIO_INVALID_TRACK;

  if (p_cdio->op.get_first_track_num) {
    return p_cdio->op.get_first_track_num (p_cdio->env);
  } else {
    return CDIO_INVALID_TRACK;
  }
}

/*!
  Return the last track number.
  CDIO_INVALID_TRACK is returned on error.
*/
track_t
cdio_get_last_track_num (const CdIo_t *p_cdio)
{
  if (NULL == p_cdio) return CDIO_INVALID_TRACK;
  {
    const track_t i_first_track = cdio_get_first_track_num(p_cdio);
    if ( CDIO_INVALID_TRACK != i_first_track ) {
      const track_t i_tracks = cdio_get_num_tracks(p_cdio);
      if ( CDIO_INVALID_TRACK != i_tracks ) 
	return i_first_track + i_tracks - 1;
    }
    return CDIO_INVALID_TRACK;
  }
}

/*! Return number of channels in track: 2 or 4; -2 if not
  implemented or -1 for error.
  Not meaningful if track is not an audio track.
*/
int
cdio_get_track_channels(const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio->op.get_track_channels) {
    return p_cdio->op.get_track_channels (p_cdio->env, i_track);
  } else {
    return -2;
  }
}

/*! Return copy protection status on a track. Is this meaningful
  if not an audio track?
*/
track_flag_t
cdio_get_track_copy_permit(const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio->op.get_track_copy_permit) {
    return p_cdio->op.get_track_copy_permit (p_cdio->env, i_track);
  } else {
    return CDIO_TRACK_FLAG_UNKNOWN;
  }
}

/*!  
  Get format of track. 
*/
track_format_t
cdio_get_track_format(const CdIo_t *p_cdio, track_t i_track)
{
  if (!p_cdio) return TRACK_FORMAT_ERROR;

  if (p_cdio->op.get_track_format) {
    return p_cdio->op.get_track_format (p_cdio->env, i_track);
  } else {
    return TRACK_FORMAT_ERROR;
  }
}
/*!  
  Return the Joliet level recognized for p_cdio.
*/
uint8_t 
cdio_get_joliet_level(const CdIo_t *p_cdio)
{
  if (!p_cdio) return 0;
  {
    const generic_img_private_t *p_env 
      = (generic_img_private_t *) (p_cdio->env);
    return p_env->i_joliet_level;
  }
}

/*! 
  Return the number of tracks in the current medium.
  CDIO_INVALID_TRACK is returned on error.
*/
track_t
cdio_get_num_tracks (const CdIo_t *p_cdio)
{
  if (p_cdio == NULL) return CDIO_INVALID_TRACK;

  if (p_cdio->op.get_num_tracks) {
    return p_cdio->op.get_num_tracks (p_cdio->env);
  } else {
    return CDIO_INVALID_TRACK;
  }
}

/*! Find the track which contans lsn.
    CDIO_INVALID_TRACK is returned if the lsn outside of the CD or
    if there was some error. 
    
    If the lsn is before the pregap of the first track 0 is returned.
    Otherwise we return the track that spans the lsn.
*/
track_t
cdio_get_track(const CdIo_t *p_cdio, lsn_t lsn)
{
  if (!p_cdio) return CDIO_INVALID_TRACK;
  
  {
    track_t i_low_track   = cdio_get_first_track_num(p_cdio);
    track_t i_high_track  = cdio_get_last_track_num(p_cdio)+1; /* LEADOUT */

    if (CDIO_INVALID_TRACK == i_low_track 
	|| CDIO_INVALID_TRACK == i_high_track ) return CDIO_INVALID_TRACK;
    
    if (lsn < cdio_get_track_lsn(p_cdio, i_low_track))
      return 0; /* We're in the pre-gap of first track */

    if (lsn > cdio_get_track_lsn(p_cdio, i_high_track))
      return CDIO_INVALID_TRACK; /* We're beyond the end. */

    do {
      const track_t i_mid = (i_low_track + i_high_track) / 2;
      const lsn_t i_mid_lsn = cdio_get_track_lsn(p_cdio, i_mid);
      if (lsn <= i_mid_lsn) i_high_track = i_mid - 1;
      if (lsn >= i_mid_lsn) i_low_track  = i_mid + 1;
    } while ( i_low_track <= i_high_track );

    return (i_low_track > i_high_track + 1) 
      ? i_high_track + 1 : i_high_track;
  }
}

/*!
  Return true if we have XA data (green, mode2 form1) or
  XA data (green, mode2 form2). That is track begins:
  sync - header - subheader
  12     4      -  8
  
  FIXME: there's gotta be a better design for this and get_track_format?
*/
bool
cdio_get_track_green(const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio == NULL) {
    return false;
  }

  if (p_cdio->op.get_track_green) {
    return p_cdio->op.get_track_green (p_cdio->env, i_track);
  } else {
    return false;
  }
}

/*!  
  Return the starting LBA for track number
  track_num in cdio.  Tracks numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.
  CDIO_INVALID_LBA is returned on error.
*/
lba_t
cdio_get_track_lba(const CdIo_t *p_cdio, track_t i_track)
{
  if (!p_cdio) return CDIO_INVALID_LBA;

  if (p_cdio->op.get_track_lba) {
    return p_cdio->op.get_track_lba (p_cdio->env, i_track);
  } else {
    msf_t msf;
    if (p_cdio->op.get_track_msf) 
      if (cdio_get_track_msf(p_cdio, i_track, &msf))
        return cdio_msf_to_lba(&msf);
    return CDIO_INVALID_LBA;
  }
}

/*!  
  Return the starting LSN for track number
  i_track in cdio.  Tracks numbers start at 1.
  The "leadout" track is specified either by
  using i_track LEADOUT_TRACK or the total tracks+1.
  CDIO_INVALID_LSN is returned on error.
*/
lsn_t
cdio_get_track_lsn(const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio == NULL) return CDIO_INVALID_LSN;

  if (p_cdio->op.get_track_lba) {
    return cdio_lba_to_lsn(p_cdio->op.get_track_lba (p_cdio->env, i_track));
  } else {
    msf_t msf;
    if (cdio_get_track_msf(p_cdio, i_track, &msf))
      return cdio_msf_to_lsn(&msf);
    return CDIO_INVALID_LSN;
  }
}

/*!
  Return the International Standard Recording Code (ISRC) for track number
  i_track in p_cdio.  Track numbers start at 1.

  Note: string is malloc'd so caller has to free() the returned
  string when done with it.
*/
char *
cdio_get_track_isrc (const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio == NULL) return NULL;

  if (p_cdio->op.get_track_isrc) {
    return p_cdio->op.get_track_isrc (p_cdio->env, i_track);
  } else {
    return NULL;
  }
}

/*!  
  Return the starting LBA for the pregap for track number
  i_track in cdio.  Track numbers start at 1.
  CDIO_INVALID_LBA is returned on error.
*/
lba_t
cdio_get_track_pregap_lba(const CdIo_t *p_cdio, track_t i_track)
{
  if (p_cdio == NULL) return CDIO_INVALID_LBA;

  if (p_cdio->op.get_track_pregap_lba) {
    return p_cdio->op.get_track_pregap_lba (p_cdio->env, i_track);
  } else {
    return CDIO_INVALID_LBA;
  }
}

/*!  
  Return the starting LSN for the pregap for track number
  i_track in cdio.  Track numbers start at 1.
  CDIO_INVALID_LSN is returned on error.
*/
lsn_t
cdio_get_track_pregap_lsn(const CdIo_t *p_cdio, track_t i_track)
{
  return cdio_lba_to_lsn(cdio_get_track_pregap_lba(p_cdio, i_track));
}

/*!  
  Return the ending LSN for track number
  i_track in cdio.  CDIO_INVALID_LSN is returned on error.
*/
lsn_t
cdio_get_track_last_lsn(const CdIo_t *p_cdio, track_t i_track)
{
  lsn_t lsn = cdio_get_track_lsn(p_cdio, i_track+1);

  if (CDIO_INVALID_LSN == lsn) return CDIO_INVALID_LSN;
  /* Safe, we've always the leadout. */
  return lsn - 1;
}

/*!  
  Return the starting MSF (minutes/secs/frames) for track number
  i_track in cdio.  Track numbers start at 1.
  The "leadout" track is specified either by
  using i_track LEADOUT_TRACK or the total tracks+1.
  False is returned if there is no track entry.
*/
bool
cdio_get_track_msf(const CdIo_t *p_cdio, track_t i_track, /*out*/ msf_t *msf)
{
  if (!p_cdio) return false;

  if (p_cdio->op.get_track_msf) {
    return p_cdio->op.get_track_msf (p_cdio->env, i_track, msf);
  } else if (p_cdio->op.get_track_lba) {
    lba_t lba = p_cdio->op.get_track_lba (p_cdio->env, i_track);
    if (lba  == CDIO_INVALID_LBA) return false;
    cdio_lba_to_msf(lba, msf);
    return true;
  } else {
    return false;
  }
}

/*! Return copy protection status on a track. Is this meaningful
  if not an audio track?
*/
track_flag_t
cdio_get_track_preemphasis(const CdIo *p_cdio, track_t i_track)
{
  if (p_cdio->op.get_track_preemphasis) {
    return p_cdio->op.get_track_preemphasis (p_cdio->env, i_track);
  } else {
    return CDIO_TRACK_FLAG_UNKNOWN;
  }
}

/*!  
  Return the number of sectors between this track an the next.  This
  includes any pregap sectors before the start of the next track.
  Tracks start at 1.
  0 is returned if there is an error.
*/
unsigned int
cdio_get_track_sec_count(const CdIo_t *p_cdio, track_t i_track)
{
  const track_t i_tracks = cdio_get_num_tracks(p_cdio);

  if (i_track >=1 && i_track <= i_tracks) 
    return ( cdio_get_track_lba(p_cdio, i_track+1) 
             - cdio_get_track_lba(p_cdio, i_track) );
  return 0;
}
