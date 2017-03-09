/*
  Copyright (C) 2004, 2005, 2011, 2012, 2014, 2016
  Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/sector.h>
#include <cdio/util.h>
#include <cdio/logging.h>
#include "cdio_assert.h"
#include "portable.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <ctype.h>

/*! String of bytes used to identify the beginning of a Mode 1 or
  Mode 2 sector. */
const uint8_t CDIO_SECTOR_SYNC_HEADER[CDIO_CD_SYNC_SIZE] =
  {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

/* Variables to hold debugger-helping enumerations */
enum cdio_cd_enums;
enum m2_sector_enums;

lba_t
cdio_lba_to_lsn (lba_t lba)
{
  if (CDIO_INVALID_LBA     == lba) return CDIO_INVALID_LSN;
  return lba - CDIO_PREGAP_SECTORS;
}

/*
   The below is adapted from cdparanoia code which claims it is
   straight from the MMC3 spec.
*/

void
cdio_lsn_to_msf (lsn_t lsn, msf_t *msf)
{
  int m, s, f;

  cdio_assert (msf != 0);

  if ( lsn >= -CDIO_PREGAP_SECTORS ){
    m    = (lsn + CDIO_PREGAP_SECTORS) / CDIO_CD_FRAMES_PER_MIN;
    lsn -= m * CDIO_CD_FRAMES_PER_MIN;
    s    = (lsn + CDIO_PREGAP_SECTORS) / CDIO_CD_FRAMES_PER_SEC;
    lsn -= s * CDIO_CD_FRAMES_PER_SEC;
    f    = lsn + CDIO_PREGAP_SECTORS;
  } else {
    m    = (lsn + CDIO_CD_MAX_LSN)     / CDIO_CD_FRAMES_PER_MIN;
    lsn -= m * (CDIO_CD_FRAMES_PER_MIN);
    s    = (lsn+CDIO_CD_MAX_LSN)       / CDIO_CD_FRAMES_PER_SEC;
    lsn -= s * CDIO_CD_FRAMES_PER_SEC;
    f    = lsn + CDIO_CD_MAX_LSN;
  }

  if (m > 99) {
    cdio_warn ("number of minutes (%d) truncated to 99.", m);
    m = 99;
  }

  msf->m = cdio_to_bcd8 (m);
  msf->s = cdio_to_bcd8 (s);
  msf->f = cdio_to_bcd8 (f);
}

/*!
  Convert an LBA into a string representation of the MSF.
  \warning cdio_lba_to_msf_str returns new allocated string */
char *
cdio_lba_to_msf_str (lba_t lba)
{

  if (CDIO_INVALID_LBA == lba) {
    return strdup("*INVALID");
  } else {
    msf_t msf;
    msf.m = msf.s = msf.f = 0;
    cdio_lba_to_msf (lba, &msf);
    return cdio_msf_to_str(&msf);
  }
}

/*!
  Convert an LSN into the corresponding LBA.
  CDIO_INVALID_LBA is returned if there is an error.
*/
lba_t
cdio_lsn_to_lba (lsn_t lsn)
{
  if (CDIO_INVALID_LSN  == lsn) return CDIO_INVALID_LBA;
  return lsn + CDIO_PREGAP_SECTORS;
}

/*!
  Convert an LBA into the corresponding MSF.
*/
void
cdio_lba_to_msf (lba_t lba, msf_t *msf)
{
  cdio_assert (msf != 0);
  cdio_lsn_to_msf(cdio_lba_to_lsn(lba), msf);
}

/*!
  Convert a MSF into the corresponding LBA.
  CDIO_INVALID_LBA is returned if there is an error.
*/
lba_t
cdio_msf_to_lba (const msf_t *msf)
{
  uint32_t lba = 0;

  cdio_assert (msf != 0);

  lba = cdio_from_bcd8 (msf->m);
  lba *= CDIO_CD_SECS_PER_MIN;

  lba += cdio_from_bcd8 (msf->s);
  lba *= CDIO_CD_FRAMES_PER_SEC;

  lba += cdio_from_bcd8 (msf->f);

  return lba;
}

/*!
  Convert a MSF into the corresponding LSN.
  CDIO_INVALID_LSN is returned if there is an error.
*/
lba_t
cdio_msf_to_lsn (const msf_t *msf)
{
  return cdio_lba_to_lsn(cdio_msf_to_lba (msf));
}

/*!
  Convert an LBA into a string representation of the MSF.
  \warning cdio_lba_to_msf_str returns new allocated string */
char *
cdio_msf_to_str (const msf_t *msf)
{
  char buf[16];

  snprintf (buf, sizeof (buf), "%2.2x:%2.2x:%2.2x", msf->m, msf->s, msf->f);
  return strdup (buf);
}

/*!
  Convert a MSF - broken out as 3 integer components into the
  corresponding LBA.
  CDIO_INVALID_LBA is returned if there is an error.
*/
lba_t
cdio_msf3_to_lba (unsigned int minutes, unsigned int seconds,
                  unsigned int frames)
{
  return ((minutes * CDIO_CD_SECS_PER_MIN + seconds) * CDIO_CD_FRAMES_PER_SEC
	  + frames);
}

/*!
  Convert a string of the form MM:SS:FF into the corresponding LBA.
  CDIO_INVALID_LBA is returned if there is an error.
*/
lba_t
cdio_mmssff_to_lba (const char *psz_mmssff)
{
  int psz_field;
  lba_t ret;
  unsigned char c;

  if (0 == strcmp (psz_mmssff, "0"))
    return 0;

  c = *psz_mmssff++;
  if(c >= '0' && c <= '9')
    psz_field = (c - '0');
  else
    return CDIO_INVALID_LBA;
  while(':' != (c = *psz_mmssff++)) {
    if(c >= '0' && c <= '9')
      psz_field = psz_field * 10 + (c - '0');
    else
      return CDIO_INVALID_LBA;
  }

  ret = cdio_msf3_to_lba (psz_field, 0, 0);

  c = *psz_mmssff++;
  if(c >= '0' && c <= '9')
    psz_field = (c - '0');
  else
    return CDIO_INVALID_LBA;
  if(':' != (c = *psz_mmssff++)) {
    if(c >= '0' && c <= '9') {
      psz_field = psz_field * 10 + (c - '0');
      c = *psz_mmssff++;
      if(c != ':')
	return CDIO_INVALID_LBA;
    }
    else
      return CDIO_INVALID_LBA;
  }

  if(psz_field >= CDIO_CD_SECS_PER_MIN)
    return CDIO_INVALID_LBA;

  ret += cdio_msf3_to_lba (0, psz_field, 0);

  c = *psz_mmssff++;
  if (isdigit(c))
    psz_field = (c - '0');
  else
    return -1;
  if('\0' != (c = *psz_mmssff++)) {
    if (isdigit(c)) {
      psz_field = psz_field * 10 + (c - '0');
      c = *psz_mmssff++;
    }
    else
      return CDIO_INVALID_LBA;
  }

  if('\0' != c)
    return CDIO_INVALID_LBA;

  if(psz_field >= CDIO_CD_FRAMES_PER_SEC)
    return CDIO_INVALID_LBA;

  ret += psz_field;

  return ret;
}


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
