/*
  Copyright (C) 2003, 2004, 2005, 2008, 2011, 2012, 2014
   Rocky Bernstein <rocky@gnu.org>
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


#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif 

#include <cdio/cdio.h>
#include "cdio_private.h"

/* Must match discmode enumeration */
const char *discmode2str[] = {
  "CD-DA", 
  "CD-DATA (Mode 1)", 
  "CD DATA (Mode 2)", 
  "CD-ROM Mixed",
  "DVD-ROM", 
  "DVD-RAM", 
  "DVD-R", 
  "DVD-RW", 
  "HD DVD ROM",
  "HD_DVD RAM", 
  "HD DVD-R", 
  "DVD+R",
  "DVD+RW", 
  "DVD+RW DL", 
  "DVD+R DL", 
  "Unknown/unclassified DVD", 
  "No information",
  "Error in getting information",
  "CD-i" 
};

/*! 
  Get cdtext information for a CdIo object .
  
  @param obj the CD object that may contain CD-TEXT information.
  @return the CD-TEXT object or NULL if obj is NULL
  or CD-TEXT information does not exist.
*/
cdtext_t *
cdio_get_cdtext (CdIo *obj)
{
  if (obj == NULL) return NULL;
  
  if (NULL != obj->op.get_cdtext) {
    return obj->op.get_cdtext (obj->env);
  } else {
    return NULL;
  }
}

/*! 
  Get binary cdtext information for a CdIo object .
  
  @param obj the CD object that may contain CD-TEXT information.
  @return pointer to allocated memory area holding the raw CD-TEXT
  or NULL if obj is NULL or CD-TEXT does not exist. Return value
  must be freed with cdio_free() when done with it and not NULL.
*/
uint8_t *
cdio_get_cdtext_raw (CdIo *obj)
{
  if (obj == NULL) return NULL;

  if (NULL != obj->op.get_cdtext_raw) {
    return obj->op.get_cdtext_raw (obj->env);
  } else {
    return NULL;
  }
}

/*!
  Get the size of the CD in logical block address (LBA) units.
  
  @param p_cdio the CD object queried
  @return the lsn. On error 0 or CDIO_INVALD_LSN.
*/
lsn_t 
cdio_get_disc_last_lsn(const CdIo_t *p_cdio)
{
  if (!p_cdio) return CDIO_INVALID_LSN;
  return p_cdio->op.get_disc_last_lsn (p_cdio->env);
}

/*! 
  Get medium associated with cd_obj.
*/
discmode_t
cdio_get_discmode (CdIo_t *cd_obj)
{
  if (!cd_obj) return CDIO_DISC_MODE_ERROR;
  
  if (cd_obj->op.get_discmode) {
    return cd_obj->op.get_discmode (cd_obj->env);
  } else {
    return CDIO_DISC_MODE_NO_INFO;
  }
}

/*!
  Return a string containing the name of the driver in use.
  if CdIo is NULL (we haven't initialized a specific device driver), 
  then return NULL.
*/
char *
cdio_get_mcn (const CdIo_t *p_cdio) 
{
  if (p_cdio && p_cdio->op.get_mcn) {
    return p_cdio->op.get_mcn (p_cdio->env);
  } else {
    return NULL;
  }
}

bool
cdio_is_discmode_cdrom(discmode_t discmode) 
{
  switch (discmode) {
  case CDIO_DISC_MODE_CD_DA:
  case CDIO_DISC_MODE_CD_DATA:
  case CDIO_DISC_MODE_CD_XA:
  case CDIO_DISC_MODE_CD_MIXED:
  case CDIO_DISC_MODE_NO_INFO:
    return true;
  default:
    return false;
  }
}

bool
cdio_is_discmode_dvd(discmode_t discmode) 
{
  switch (discmode) {
    case CDIO_DISC_MODE_DVD_ROM:
    case CDIO_DISC_MODE_DVD_RAM:
    case CDIO_DISC_MODE_DVD_R:
    case CDIO_DISC_MODE_DVD_RW:
    case CDIO_DISC_MODE_DVD_PR:
    case CDIO_DISC_MODE_DVD_PRW:
    case CDIO_DISC_MODE_DVD_OTHER:
      return true;
    default:
      return false;
  }
}
