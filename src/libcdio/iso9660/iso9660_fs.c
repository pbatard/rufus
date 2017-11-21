/*
  Copyright (C) 2003-2008, 2011-2015 Rocky Bernstein <rocky@gnu.org>
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
/* iso9660 filesystem-based routines */

#if defined(HAVE_CONFIG_H) && !defined(__CDIO_CONFIG_H__)
#include "config.h"
#define __CDIO_CONFIG_H__ 1
#endif

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

#include <cdio/cdio.h>
#include <cdio/bytesex.h>
#include <cdio/iso9660.h>
#include <cdio/util.h>
#include <cdio/utf8.h>

/* Private headers */
#include "cdio_assert.h"
#include "_cdio_stdio.h"
#include "cdio_private.h"

/** Implementation of iso9660_t type */
struct _iso9660_s {
  CdioDataSource_t *stream; /**< Stream pointer */
  bool_3way_t b_xa;         /**< true if has XA attributes. */
  bool_3way_t b_mode2;      /**< true if has mode 2, false for mode 1. */
  uint8_t  u_joliet_level;  /**< 0 = no Joliet extensions.
			         1-3: Joliet level. */
  iso9660_pvd_t pvd;
  iso9660_svd_t svd;
  iso_extension_mask_t iso_extension_mask; /**< What extensions we
					        tolerate. */
  uint32_t i_datastart;     /**< Usually 0 when i_framesize is ISO_BLOCKSIZE.
			         This is the normal condition. But in a fuzzy
			         read we may be reading a CD-image
			         and not a true ISO 9660 image this might be
			         CDIO_CD_SYNC_SIZE
                            */
  uint32_t i_framesize;     /**< Usually ISO_BLOCKSIZE (2048), but in a
			        fuzzy read, we may be reading a CD-image
			        and not a true ISO 9660 image this might
			        be CDIO_CD_FRAMESIZE_RAW (2352) or
			        M2RAW_SECTOR_SIZE (2336).
                            */
  int i_fuzzy_offset;       /**< Adjustment in bytes to make ISO_STANDARD_ID
			         ("CD001") come out as ISO_PVD_SECTOR
			         (frame 16).  Normally this should be 0
			         for an ISO 9660 image, but if one is
			         say reading a BIN/CUE or cdrdao BIN/TOC
			         without having the CUE or TOC and
			         trying to extract an ISO-9660
			         filesystem inside that it may be
			         different.
			     */
    bool b_have_superblock; /**< Superblock has been read in? */

};

static long int iso9660_seek_read_framesize (const iso9660_t *p_iso,
					     void *ptr, lsn_t start,
					     long int size,
					     uint16_t i_framesize);

/* Adjust the p_iso's i_datastart, i_byte_offset and i_framesize
   based on whether we find a frame header or not.
*/
static void
adjust_fuzzy_pvd( iso9660_t *p_iso )
{
  long int i_byte_offset;

  if (!p_iso) return;

  i_byte_offset = (ISO_PVD_SECTOR * p_iso->i_framesize)
    + p_iso->i_fuzzy_offset + p_iso->i_datastart;

  /* If we have a raw 2352-byte frame then we should expect to see a sync
     frame and a header.
   */
  if (CDIO_CD_FRAMESIZE_RAW == p_iso->i_framesize) {
    char buf[CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE + CDIO_CD_SUBHEADER_SIZE];

    i_byte_offset -= CDIO_CD_SYNC_SIZE + CDIO_CD_HEADER_SIZE + CDIO_CD_SUBHEADER_SIZE;

    if ( DRIVER_OP_SUCCESS != cdio_stream_seek (p_iso->stream, i_byte_offset,
						SEEK_SET) )
      return;
    if (sizeof(buf) == cdio_stream_read (p_iso->stream, buf, sizeof(buf), 1)) {
      /* Does the sector frame header suggest Mode 1 format? */
      if (!memcmp(CDIO_SECTOR_SYNC_HEADER, buf+CDIO_CD_SUBHEADER_SIZE,
		  CDIO_CD_SYNC_SIZE)) {
	if (buf[14+CDIO_CD_SUBHEADER_SIZE] != 0x16) {
	  cdio_warn ("Expecting the PVD sector header MSF to be 0x16, is: %x",
		     buf[14]);
	}
	if (buf[15+CDIO_CD_SUBHEADER_SIZE] != 0x1) {
	  cdio_warn ("Expecting the PVD sector mode to be Mode 1 is: %x",
		     buf[15]);
	}
	p_iso->b_mode2 = nope;
	p_iso->b_xa = nope;
      } else if (!memcmp(CDIO_SECTOR_SYNC_HEADER, buf, CDIO_CD_SYNC_SIZE)) {
	/* Frame header indicates Mode 2 Form 1*/
	if (buf[14] != 0x16) {
	  cdio_warn ("Expecting the PVD sector header MSF to be 0x16, is: %x",
		     buf[14]);
	}
	if (buf[15] != 0x2) {
	  cdio_warn ("Expecting the PVD sector mode to be Mode 2 is: %x",
		     buf[15]);
	}
	p_iso->b_mode2 = yep;
	/* Do do: check Mode 2 Form 2? */
      } else {
	  /* Has no frame header */
	  p_iso->i_framesize = M2RAW_SECTOR_SIZE;
	  p_iso->i_fuzzy_offset = (CDIO_CD_FRAMESIZE_RAW - M2RAW_SECTOR_SIZE)
	    * ISO_PVD_SECTOR + p_iso->i_fuzzy_offset + p_iso->i_datastart;
	  p_iso->i_datastart = 0;
	}
    }
  }


}

/*!
  Open an ISO 9660 image for reading in either fuzzy mode or not.
*/
static iso9660_t *
iso9660_open_ext_private (const char *psz_path,
			  iso_extension_mask_t iso_extension_mask,
			  uint16_t i_fuzz, bool b_fuzzy)
{
  iso9660_t *p_iso = (iso9660_t *) calloc(1, sizeof(iso9660_t)) ;

  if (!p_iso) return NULL;

  p_iso->stream = cdio_stdio_new( psz_path );
  if (NULL == p_iso->stream)
    goto error;

  p_iso->i_framesize = ISO_BLOCKSIZE;

  p_iso->b_have_superblock = (b_fuzzy)
    ? iso9660_ifs_fuzzy_read_superblock(p_iso, iso_extension_mask, i_fuzz)
    : iso9660_ifs_read_superblock(p_iso, iso_extension_mask) ;

  if ( ! p_iso->b_have_superblock ) goto error;

  /* Determine if image has XA attributes. */

  p_iso->b_xa = strncmp ((char *) &(p_iso->pvd) + ISO_XA_MARKER_OFFSET,
			 ISO_XA_MARKER_STRING,
			 sizeof (ISO_XA_MARKER_STRING))
    ? nope : yep;

  p_iso->iso_extension_mask = iso_extension_mask;
  return p_iso;

 error:
  if (p_iso && p_iso->stream) cdio_stdio_destroy(p_iso->stream);
  free(p_iso);

  return NULL;
}

/*!
  Open an ISO 9660 image for reading. Maybe in the future we will have
  a mode. NULL is returned on error.
*/
iso9660_t *
iso9660_open (const char *psz_path /*, mode*/)
{
  return iso9660_open_ext(psz_path, ISO_EXTENSION_NONE);
}

/*!
  Open an ISO 9660 image for reading. Maybe in the future we will have
  a mode. NULL is returned on error.
*/
iso9660_t *
iso9660_open_ext (const char *psz_path,
		  iso_extension_mask_t iso_extension_mask)
{
  return iso9660_open_ext_private(psz_path, iso_extension_mask, 0, false);
}


/*!
  Open an ISO 9660 image for reading. Maybe in the future we will have
  a mode. NULL is returned on error.
*/
iso9660_t *
iso9660_open_fuzzy (const char *psz_path, uint16_t i_fuzz /*, mode*/)
{
  return iso9660_open_fuzzy_ext(psz_path, ISO_EXTENSION_NONE, i_fuzz);
}

/*!
  Open an ISO 9660 image for reading. Maybe in the future we will have
  a mode. NULL is returned on error.
*/
iso9660_t *
iso9660_open_fuzzy_ext (const char *psz_path,
			iso_extension_mask_t iso_extension_mask,
			uint16_t i_fuzz)
{
  return iso9660_open_ext_private(psz_path, iso_extension_mask, i_fuzz,
				  true);
}

/*!
  Close previously opened ISO 9660 image.
  True is unconditionally returned. If there was an error false would
  be returned.
*/
bool
iso9660_close (iso9660_t *p_iso)
{
  if (NULL != p_iso) {
    cdio_stdio_destroy(p_iso->stream);
    free(p_iso);
  }
  return true;
}

static bool
check_pvd (const iso9660_pvd_t *p_pvd, cdio_log_level_t log_level)
{
  if ( ISO_VD_PRIMARY != from_711(p_pvd->type) ) {
// Commented out for Rufus usage
//    cdio_log (log_level, "unexpected PVD type %d", p_pvd->type);
    return false;
  }

  if (strncmp (p_pvd->id, ISO_STANDARD_ID, strlen (ISO_STANDARD_ID)))
    {
      cdio_log (log_level, "unexpected ID encountered (expected '"
		ISO_STANDARD_ID "', got '%.5s')", p_pvd->id);
      return false;
    }
  return true;
}


/*!
  Core procedure for the iso9660_ifs_get_###_id() calls.
  pvd_member/svd_member is a pointer to an achar_t or dchar_t
  ID string which we can superset as char.
  If the Joliet converted string is the same as the achar_t/dchar_t
  one, we fall back to using the latter, as it may be longer.
*/
static inline bool
get_member_id(iso9660_t *p_iso, cdio_utf8_t **p_psz_member_id,
              char* pvd_member, char* svd_member, size_t max_size)
{
  int j;
  bool strip;

  if (!p_iso) {
    *p_psz_member_id = NULL;
    return false;
  }
#ifdef HAVE_JOLIET
  if (p_iso->u_joliet_level) {
    /* Translate USC-2 string from Secondary Volume Descriptor */
    if (cdio_charset_to_utf8(svd_member, max_size,
                            p_psz_member_id, "UCS-2BE")) {
      /* NB: *p_psz_member_id is never NULL on success. */
      if (strncmp(*p_psz_member_id, pvd_member,
                  strlen(*p_psz_member_id)) != 0) {
        /* Strip trailing spaces */
        for (j = strlen(*p_psz_member_id)-1; j >= 0; j--) {
          if ((*p_psz_member_id)[j] != ' ')
            break;
          (*p_psz_member_id)[j] = '\0';
        }
        if ((*p_psz_member_id)[0] != 0) {
          /* Joliet string is not empty and differs from
             non Joliet one => use it */
          return true;
        }
      }
      /* Joliet string was either empty or same */
      free(*p_psz_member_id);
    }
  }
#endif /*HAVE_JOLIET*/
  *p_psz_member_id = calloc(max_size+1, sizeof(cdio_utf8_t));
  if (!*p_psz_member_id) {
    cdio_warn("Memory allocation error");
    return false;
  }
  /* Copy string while removing trailing spaces */
  (*p_psz_member_id)[max_size] = 0;
  for (strip=true, j=max_size-1; j>=0; j--) {
    if (strip && (pvd_member[j] == ' '))
      continue;
    strip = false;
    (*p_psz_member_id)[j] = pvd_member[j];
  }
  if (strlen(*p_psz_member_id) == 0) {
    free(*p_psz_member_id);
    *p_psz_member_id = NULL;
    return false;
  }
  return true;
}


/*!
  Return the application ID.  NULL is returned in psz_app_id if there
  is some problem in getting this.
*/
bool
iso9660_ifs_get_application_id(iso9660_t *p_iso,
			       /*out*/ cdio_utf8_t **p_psz_app_id)
{
  return get_member_id(p_iso, p_psz_app_id,
                       (char*)p_iso->pvd.application_id,
                       (char*)p_iso->svd.application_id,
                       ISO_MAX_APPLICATION_ID);
}

/*!
  Return the Joliet level recognized for p_iso.
*/
uint8_t iso9660_ifs_get_joliet_level(iso9660_t *p_iso)
{
  if (!p_iso) return 0;
  return p_iso->u_joliet_level;
}

/*!
   Return a string containing the preparer id with trailing
   blanks removed.
*/
bool
iso9660_ifs_get_preparer_id(iso9660_t *p_iso,
			/*out*/ cdio_utf8_t **p_psz_preparer_id)
{
  return get_member_id(p_iso, p_psz_preparer_id,
                       (char*)p_iso->pvd.preparer_id,
                       (char*)p_iso->svd.preparer_id,
                       ISO_MAX_PREPARER_ID);
}

/*!
   Return a string containing the PVD's publisher id with trailing
   blanks removed.
*/
bool iso9660_ifs_get_publisher_id(iso9660_t *p_iso,
                                  /*out*/ cdio_utf8_t **p_psz_publisher_id)
{
  return get_member_id(p_iso, p_psz_publisher_id,
                       (char*)p_iso->pvd.publisher_id,
                       (char*)p_iso->svd.publisher_id,
                       ISO_MAX_PUBLISHER_ID);
}

/*!
   Return a string containing the PVD's publisher id with trailing
   blanks removed.
*/
bool iso9660_ifs_get_system_id(iso9660_t *p_iso,
			       /*out*/ cdio_utf8_t **p_psz_system_id)
{
  return get_member_id(p_iso, p_psz_system_id,
                       (char*)p_iso->pvd.system_id,
                       (char*)p_iso->svd.system_id,
                       ISO_MAX_SYSTEM_ID);
}

/*!
   Return a string containing the PVD's publisher id with trailing
   blanks removed.
*/
bool iso9660_ifs_get_volume_id(iso9660_t *p_iso,
			       /*out*/ cdio_utf8_t **p_psz_volume_id)
{
  return get_member_id(p_iso, p_psz_volume_id,
                       (char*)p_iso->pvd.volume_id,
                       (char*)p_iso->svd.volume_id,
                       ISO_MAX_VOLUME_ID);
}

/*!
   Return a string containing the PVD's publisher id with trailing
   blanks removed.
*/
bool iso9660_ifs_get_volumeset_id(iso9660_t *p_iso,
				  /*out*/ cdio_utf8_t **p_psz_volumeset_id)
{
  return get_member_id(p_iso, p_psz_volumeset_id,
                       (char*)p_iso->pvd.volume_set_id,
                       (char*)p_iso->svd.volume_set_id,
                       ISO_MAX_VOLUMESET_ID);
}


/*!
  Read the Primary Volume Descriptor for an ISO 9660 image.
  True is returned if read, and false if there was an error.
*/
static bool
iso9660_ifs_read_pvd_loglevel (const iso9660_t *p_iso,
			       /*out*/ iso9660_pvd_t *p_pvd,
			       cdio_log_level_t log_level)
{
  if (0 == iso9660_iso_seek_read (p_iso, p_pvd, ISO_PVD_SECTOR, 1)) {
// Commented out for Rufus usage
//    cdio_log ( log_level, "error reading PVD sector (%d)", ISO_PVD_SECTOR );
    return false;
  }
  return check_pvd(p_pvd, log_level);
}

/*!
  Read the Primary Volume Descriptor for an ISO 9660 image.
  True is returned if read, and false if there was an error.
*/
bool
iso9660_ifs_read_pvd (const iso9660_t *p_iso, /*out*/ iso9660_pvd_t *p_pvd)
{
  return iso9660_ifs_read_pvd_loglevel(p_iso, p_pvd, CDIO_LOG_WARN);
}


/*!
  Read the Super block of an ISO 9660 image. This is the
  Primary Volume Descriptor (PVD) and perhaps a Supplemental Volume
  Descriptor if (Joliet) extensions are acceptable.
*/
bool
iso9660_ifs_read_superblock (iso9660_t *p_iso,
			     iso_extension_mask_t iso_extension_mask)
{
  iso9660_svd_t p_svd;  /* Secondary volume descriptor. */
  int i;

  if (!p_iso || !iso9660_ifs_read_pvd(p_iso, &(p_iso->pvd)))
    return false;

  p_iso->u_joliet_level = 0;

  /* There may be multiple Secondary Volume Descriptors (eg. El Torito + Joliet) */
  for (i=1; (0 != iso9660_iso_seek_read (p_iso, &p_svd, ISO_PVD_SECTOR+i, 1)); i++) {
    if (ISO_VD_END == from_711(p_svd.type) ) /* Last SVD */
      break;
    if ( ISO_VD_SUPPLEMENTARY == from_711(p_svd.type) ) {
      /* We're only interested in Joliet => make sure the SVD isn't overwritten */
      if (p_iso->u_joliet_level == 0)
        memcpy(&(p_iso->svd), &p_svd, sizeof(iso9660_svd_t));
      if (p_svd.escape_sequences[0] == 0x25
	  && p_svd.escape_sequences[1] == 0x2f) {
	switch (p_svd.escape_sequences[2]) {
	case 0x40:
	  if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL1)
	    p_iso->u_joliet_level = 1;
	  break;
	case 0x43:
	  if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL2)
	    p_iso->u_joliet_level = 2;
	  break;
	case 0x45:
	  if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL3)
	    p_iso->u_joliet_level = 3;
	  break;
	default:
	  cdio_info("Supplementary Volume Descriptor found, but not Joliet");
	}
	if (p_iso->u_joliet_level > 0) {
	  cdio_info("Found Extension: Joliet Level %d", p_iso->u_joliet_level);
	}
      }
    }
  }

  return true;
}

/*!
  Read the Super block of an ISO 9660 image but determine framesize
  and datastart and a possible additional offset. Generally here we are
  not reading an ISO 9660 image but a CD-Image which contains an ISO 9660
  filesystem.
*/
bool
iso9660_ifs_fuzzy_read_superblock (iso9660_t *p_iso,
				   iso_extension_mask_t iso_extension_mask,
				   uint16_t i_fuzz)
{
  /* Got some work to do to find ISO_STANDARD_ID ("CD001") */
  unsigned int i;

  for (i=0; i<i_fuzz; i++) {
    unsigned int j;
    char *pvd = NULL;

    for (j = 0; j <= 1; j++ ) {
      lsn_t lsn;
      uint16_t k;
      const uint16_t framesizes[] = { ISO_BLOCKSIZE, CDIO_CD_FRAMESIZE_RAW,
				      M2RAW_SECTOR_SIZE } ;

      /* We don't need to loop over a zero offset twice*/
      if (0==i && j)
	continue;

      lsn = (j) ? ISO_PVD_SECTOR - i : ISO_PVD_SECTOR + i;

      for (k=0; k < 3; k++) {
	char *p, *q;
	char frame[CDIO_CD_FRAMESIZE_RAW] = {'\0',};
	p_iso->i_framesize = framesizes[k];
	p_iso->i_datastart = (ISO_BLOCKSIZE == framesizes[k]) ?
			      0 : CDIO_CD_SYNC_SIZE;
	p_iso->i_fuzzy_offset = 0;
	if (0 == iso9660_seek_read_framesize (p_iso, frame, lsn, 1,
					      p_iso->i_framesize)) {
	  return false;
	}

	q = memchr(frame, 'C', p_iso->i_framesize);
	for ( p=q; p && p < frame + p_iso->i_framesize ; p=q+1 ) {
	  q = memchr(p, 'C', p_iso->i_framesize - (p - frame));
	  if ( !q || (pvd = strstr(q, ISO_STANDARD_ID)) )
	    break;
	}

	if (pvd) {
	  /* Yay! Found something */
	  p_iso->i_fuzzy_offset = (pvd - frame - 1) -
	    ((ISO_PVD_SECTOR-lsn)*p_iso->i_framesize) ;
	  /* But is it *really* a PVD? */
	  if ( iso9660_ifs_read_pvd_loglevel(p_iso, &(p_iso->pvd),
					     CDIO_LOG_DEBUG) ) {
	    adjust_fuzzy_pvd(p_iso);
	    return true;
	  }

	}
      }
    }
  }
  return false;
}


/*!
  Read the Primary Volume Descriptor for of CD.
*/
bool
iso9660_fs_read_pvd(const CdIo_t *p_cdio, /*out*/ iso9660_pvd_t *p_pvd)
{
  /* A bit of a hack, we'll assume track 1 contains ISO_PVD_SECTOR.*/
  char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };
  driver_return_code_t driver_return =
    cdio_read_data_sectors (p_cdio, buf, ISO_PVD_SECTOR, ISO_BLOCKSIZE, 1);

  if (DRIVER_OP_SUCCESS != driver_return) {
    cdio_warn ("error reading PVD sector (%d) error %d", ISO_PVD_SECTOR,
	       driver_return);
    return false;
  }

  /* The size of a PVD or SVD is smaller than a sector. So we
     allocated a bigger block above (buf) and now we'll copy just
     the part we need to save.
   */
  cdio_assert (sizeof(buf) >= sizeof (iso9660_pvd_t));
  memcpy(p_pvd, buf, sizeof(iso9660_pvd_t));

  return check_pvd(p_pvd, CDIO_LOG_WARN);
}


/*!
  Read the Super block of an ISO 9660 image. This is the
  Primary Volume Descriptor (PVD) and perhaps a Supplemental Volume
  Descriptor if (Joliet) extensions are acceptable.
*/
bool
iso9660_fs_read_superblock (CdIo_t *p_cdio,
			    iso_extension_mask_t iso_extension_mask)
{
  if (!p_cdio) return false;

  {
    generic_img_private_t *p_env = (generic_img_private_t *) p_cdio->env;
    iso9660_pvd_t         *p_pvd = &(p_env->pvd);
    iso9660_svd_t         *p_svd = &(p_env->svd);
    char buf[CDIO_CD_FRAMESIZE_RAW] = { 0, };
    driver_return_code_t driver_return;

    if ( !iso9660_fs_read_pvd(p_cdio, p_pvd) )
      return false;

    p_env->u_joliet_level = 0;

    driver_return =
      cdio_read_data_sectors ( p_cdio, buf, ISO_PVD_SECTOR+1, ISO_BLOCKSIZE,
			       1 );

    if (DRIVER_OP_SUCCESS == driver_return) {
      /* The size of a PVD or SVD is smaller than a sector. So we
	 allocated a bigger block above (buf) and now we'll copy just
	 the part we need to save.
      */
      cdio_assert (sizeof(buf) >= sizeof (iso9660_svd_t));
      memcpy(p_svd, buf, sizeof(iso9660_svd_t));

      if ( ISO_VD_SUPPLEMENTARY == from_711(p_svd->type) ) {
	if (p_svd->escape_sequences[0] == 0x25
	    && p_svd->escape_sequences[1] == 0x2f) {
	  switch (p_svd->escape_sequences[2]) {
	  case 0x40:
	    if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL1)
	      p_env->u_joliet_level = 1;
	    break;
	  case 0x43:
	    if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL2)
	      p_env->u_joliet_level = 2;
	    break;
	  case 0x45:
	    if (iso_extension_mask & ISO_EXTENSION_JOLIET_LEVEL3)
	      p_env->u_joliet_level = 3;
	    break;
	  default:
	    cdio_info("Supplementary Volume Descriptor found, but not Joliet");
	  }
	  if (p_env->u_joliet_level > 0) {
	    cdio_info("Found Extension: Joliet Level %d",
		      p_env->u_joliet_level);
	  }
	}
      }
    }
  }

  return true;
}

/*!
  Seek to a position and then read n blocks. Size read is returned.
*/
static long int
iso9660_seek_read_framesize (const iso9660_t *p_iso, void *ptr,
			     lsn_t start, long int size,
			     uint16_t i_framesize)
{
  long int ret;
  int64_t i_byte_offset;

  if (!p_iso) return 0;
  i_byte_offset = (start * (int64_t)(p_iso->i_framesize))
    + p_iso->i_fuzzy_offset + p_iso->i_datastart;

  ret = cdio_stream_seek (p_iso->stream, i_byte_offset, SEEK_SET);
  if (ret!=0) return 0;
  return cdio_stream_read (p_iso->stream, ptr, i_framesize, size);
}

/*!
  Seek to a position and then read n blocks. Size read is returned.
*/
long int
iso9660_iso_seek_read (const iso9660_t *p_iso, void *ptr, lsn_t start,
		       long int size)
{
  return iso9660_seek_read_framesize(p_iso, ptr, start, size, ISO_BLOCKSIZE);
}



static iso9660_stat_t *
_iso9660_dir_to_statbuf (iso9660_dir_t *p_iso9660_dir,
			 iso9660_stat_t *last_p_stat,
			 bool_3way_t b_xa, uint8_t u_joliet_level)
{
  uint8_t dir_len= iso9660_get_dir_len(p_iso9660_dir);
  iso711_t i_fname;
  unsigned int stat_len;
  iso9660_stat_t *p_stat = last_p_stat;
  bool err;
  char rr_fname[256] = "";
  int  i_rr_fname;

  if (!dir_len) return NULL;

  i_fname  = from_711(p_iso9660_dir->filename.len);

  /* .. string in statbuf is one longer than in p_iso9660_dir's listing '\1' */
  stat_len      = sizeof(iso9660_stat_t)+i_fname+2;

  /* Reuse multiextent p_stat if not NULL */
  if (!p_stat)
    p_stat      = calloc(1, stat_len);
  if (!p_stat)
    {
    cdio_warn("Couldn't calloc(1, %d)", stat_len);
    return NULL;
    }
  p_stat->type    = (p_iso9660_dir->file_flags & ISO_DIRECTORY)
    ? _STAT_DIR : _STAT_FILE;
  p_stat->lsn[p_stat->extents] = from_733_with_err (p_iso9660_dir->extent, &err);
  if (err) {
    free(p_stat);
    return NULL;
  }
  p_stat->size[p_stat->extents] = from_733_with_err (p_iso9660_dir->size, &err);
  p_stat->total_size += p_stat->size[p_stat->extents];
  if (err) {
    free(p_stat);
    return NULL;
  }
  p_stat->secsize[p_stat->extents] = _cdio_len2blocks (p_stat->size[p_stat->extents], ISO_BLOCKSIZE);
  p_stat->rr.b3_rock = dunno; /*FIXME should do based on mask */
  p_stat->b_xa    = false;

  /* Only resolve the full filename when we're not dealing with extent */
  if ((p_iso9660_dir->file_flags & ISO_MULTIEXTENT) == 0)
  {
    /* Check if this is the last part of a multiextent file */
    if (p_stat->extents != 0) {
      if (strcmp(p_stat->filename, &p_iso9660_dir->filename.str[1]) != 0) {
	cdio_warn("Warning: Non consecutive multiextent file parts for '%s'", p_stat->filename);
	free(p_stat);
	return NULL;
      }
    }
    i_rr_fname =
#ifdef HAVE_ROCK
      get_rock_ridge_filename(p_iso9660_dir, rr_fname, p_stat);
#else
      0;
#endif

    if (i_rr_fname > 0) {
      if (i_rr_fname > i_fname) {
	/* realloc gives valgrind errors */
	iso9660_stat_t *p_stat_new =
	  calloc(1, sizeof(iso9660_stat_t)+i_rr_fname+2);
        if (!p_stat_new)
          {
          cdio_warn("Couldn't calloc(1, %d)", (int)(sizeof(iso9660_stat_t)+i_rr_fname+2));
	  free(p_stat);
          return NULL;
          }
	memcpy(p_stat_new, p_stat, stat_len);
	free(p_stat);
	p_stat = p_stat_new;
      }
      strncpy(p_stat->filename, rr_fname, i_rr_fname+1);
    } else {
      if ('\0' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	strncpy (p_stat->filename, ".", sizeof("."));
      else if ('\1' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	strncpy (p_stat->filename, "..", sizeof(".."));
#ifdef HAVE_JOLIET
      else if (u_joliet_level) {
	int i_inlen = i_fname;
	cdio_utf8_t *p_psz_out = NULL;
	if (cdio_charset_to_utf8(&p_iso9660_dir->filename.str[1], i_inlen,
                             &p_psz_out, "UCS-2BE")) {
          strncpy(p_stat->filename, p_psz_out, i_fname);
          free(p_psz_out);
        }
        else {
          free(p_stat);
          return NULL;
        }
      }
#endif /*HAVE_JOLIET*/
      else {
	strncpy (p_stat->filename, &p_iso9660_dir->filename.str[1], i_fname);
      }
    }
  } else {
      /* Use the plain ISO-9660 name when dealing with a multiextent file part */
      strncpy(p_stat->filename, &p_iso9660_dir->filename.str[1], i_fname);
  }
  p_stat->extents++;
  if (p_stat->extents > ISO_MAX_MULTIEXTENT) {
      cdio_warn("Warning: Too many multiextent file parts for '%s'", p_stat->filename);
      free(p_stat->rr.psz_symlink);
      free(p_stat);
      return NULL;
  }

  iso9660_get_dtime(&(p_iso9660_dir->recording_time), true, &(p_stat->tm));

  if (dir_len < sizeof (iso9660_dir_t)) {
    free(p_stat->rr.psz_symlink);
    free(p_stat);
    return NULL;
  }


  {
    int su_length = iso9660_get_dir_len(p_iso9660_dir)
      - sizeof (iso9660_dir_t);
    su_length -= i_fname;

    if (su_length % 2)
      su_length--;

    if (su_length < 0 || su_length < sizeof (iso9660_xa_t))
      return p_stat;

    if (nope == b_xa) {
      return p_stat;
    } else {
      iso9660_xa_t *xa_data =
	(void *) (((char *) p_iso9660_dir)
		  + (iso9660_get_dir_len(p_iso9660_dir) - su_length));
      cdio_log_level_t loglevel = (yep == b_xa)
	? CDIO_LOG_WARN : CDIO_LOG_INFO;

      if (xa_data->signature[0] != 'X'
	  || xa_data->signature[1] != 'A')
	{
	  cdio_log (loglevel,
		    "XA signature not found in ISO9660's system use area;"
		     " ignoring XA attributes for this file entry.");
	  cdio_debug ("%d %d %d, '%c%c' (%d, %d)",
		      iso9660_get_dir_len(p_iso9660_dir),
		      i_fname,
		      su_length,
		      xa_data->signature[0], xa_data->signature[1],
		      xa_data->signature[0], xa_data->signature[1]);
	  return p_stat;
	}
      p_stat->b_xa = true;
      p_stat->xa   = *xa_data;
    }
  }
  return p_stat;

}

/*!
  Return the directory name stored in the iso9660_dir_t

  A string is allocated: the caller must deallocate. This routine
  can return NULL if memory allocation fails.
 */
char *
iso9660_dir_to_name (const iso9660_dir_t *iso9660_dir)
{
  uint8_t len=iso9660_get_dir_len(iso9660_dir);

  if (!len) return NULL;

  cdio_assert (len >= sizeof (iso9660_dir_t));

  /* (iso9660_dir->file_flags & ISO_DIRECTORY) */

  if (iso9660_dir->filename.str[1] == '\0')
    return strdup(".");
  else if (iso9660_dir->filename.str[1] == '\1')
    return strdup("..");
  else {
    return strdup(&iso9660_dir->filename.str[1]);
  }
}

/*
   Return a pointer to a ISO 9660 stat buffer or NULL if there's an error
*/
static iso9660_stat_t *
_fs_stat_root (CdIo_t *p_cdio)
{

  if (!p_cdio) return NULL;

  {
    iso_extension_mask_t iso_extension_mask = ISO_EXTENSION_ALL;
    generic_img_private_t *p_env = (generic_img_private_t *) p_cdio->env;
    iso9660_dir_t *p_iso9660_dir;
    iso9660_stat_t *p_stat;
    bool_3way_t b_xa;

    if (!p_env->u_joliet_level)
      iso_extension_mask &= ~ISO_EXTENSION_JOLIET;

    /* FIXME try also with Joliet.*/
    if ( !iso9660_fs_read_superblock (p_cdio, iso_extension_mask) ) {
      cdio_warn("Could not read ISO-9660 Superblock.");
      return NULL;
    }

    switch(cdio_get_discmode(p_cdio)) {
    case CDIO_DISC_MODE_CD_XA:
      b_xa = yep;
      break;
    case CDIO_DISC_MODE_CD_DATA:
      b_xa = nope;
      break;
    default:
      b_xa = dunno;
    }

#ifdef HAVE_JOLIET
    p_iso9660_dir = p_env->u_joliet_level
      ? &(p_env->svd.root_directory_record)
      : &(p_env->pvd.root_directory_record) ;
#else
    p_iso9660_dir = &(p_env->pvd.root_directory_record) ;
#endif

    p_stat = _iso9660_dir_to_statbuf (p_iso9660_dir, NULL,
				      b_xa, p_env->u_joliet_level);
    return p_stat;
  }

}

static iso9660_stat_t *
_ifs_stat_root (iso9660_t *p_iso)
{
  iso9660_stat_t *p_stat;
  iso9660_dir_t *p_iso9660_dir;

#ifdef HAVE_JOLIET
  p_iso9660_dir = p_iso->u_joliet_level
    ? &(p_iso->svd.root_directory_record)
    : &(p_iso->pvd.root_directory_record) ;
#else
  p_iso9660_dir = &(p_iso->pvd.root_directory_record) ;
#endif

  p_stat = _iso9660_dir_to_statbuf (p_iso9660_dir, NULL,
				    p_iso->b_xa,
				    p_iso->u_joliet_level);
  return p_stat;
}

static iso9660_stat_t *
_fs_stat_traverse (const CdIo_t *p_cdio, const iso9660_stat_t *_root,
		   char **splitpath)
{
  unsigned offset = 0;
  uint8_t *_dirbuf = NULL;
  iso9660_stat_t *p_stat;
  generic_img_private_t *p_env = (generic_img_private_t *) p_cdio->env;

  if (!splitpath[0])
    {
      unsigned int len=sizeof(iso9660_stat_t) + strlen(_root->filename)+1;
      p_stat = calloc(1, len);
      cdio_assert (p_stat != NULL);
      memcpy(p_stat, _root, len);
      p_stat->rr.psz_symlink = calloc(1, p_stat->rr.i_symlink_max);
      cdio_assert (p_stat->rr.psz_symlink != NULL);
      memcpy(p_stat->rr.psz_symlink, _root->rr.psz_symlink,
	     p_stat->rr.i_symlink_max);
      return p_stat;
    }

  if (_root->type == _STAT_FILE)
    return NULL;

  cdio_assert (_root->type == _STAT_DIR);

  _dirbuf = calloc(1, _root->secsize[0] * ISO_BLOCKSIZE);
  if (!_dirbuf)
    {
    cdio_warn("Couldn't calloc(1, %d)", _root->secsize[0] * ISO_BLOCKSIZE);
    return NULL;
    }

  if (cdio_read_data_sectors (p_cdio, _dirbuf, _root->lsn[0], ISO_BLOCKSIZE,
			      _root->secsize[0]))
      return NULL;

  while (offset < (_root->secsize[0] * ISO_BLOCKSIZE))
    {
      iso9660_dir_t *p_iso9660_dir = (void *) &_dirbuf[offset];
      iso9660_stat_t *p_iso9660_stat;
      int cmp;

      if (!iso9660_get_dir_len(p_iso9660_dir))
	{
	  offset++;
	  continue;
	}

      p_iso9660_stat = _iso9660_dir_to_statbuf (p_iso9660_dir, NULL,
					dunno, p_env->u_joliet_level);

      cmp = strcmp(splitpath[0], p_iso9660_stat->filename);

      if ( 0 != cmp && 0 == p_env->u_joliet_level
	   && yep != p_iso9660_stat->rr.b3_rock ) {
	char *trans_fname = NULL;
	unsigned int i_trans_fname=strlen(p_iso9660_stat->filename);

	if (i_trans_fname) {
	  trans_fname = calloc(1, i_trans_fname+1);
	  if (!trans_fname) {
	    cdio_warn("can't allocate %lu bytes",
		      (long unsigned int) strlen(p_iso9660_stat->filename));
	    free(p_iso9660_stat);
	    return NULL;
	  }
	  iso9660_name_translate_ext(p_iso9660_stat->filename, trans_fname,
				     p_env->u_joliet_level);
	  cmp = strcmp(splitpath[0], trans_fname);
	  free(trans_fname);
	}
      }

      if (!cmp) {
	iso9660_stat_t *ret_stat
	  = _fs_stat_traverse (p_cdio, p_iso9660_stat, &splitpath[1]);
	free(p_iso9660_stat->rr.psz_symlink);
	free(p_iso9660_stat);
	free (_dirbuf);
	return ret_stat;
      }

      free(p_iso9660_stat->rr.psz_symlink);
      free(p_iso9660_stat);

      offset += iso9660_get_dir_len(p_iso9660_dir);
    }

  cdio_assert (offset == (_root->secsize[0] * ISO_BLOCKSIZE));

  /* not found */
  free (_dirbuf);
  return NULL;
}

static iso9660_stat_t *
_fs_iso_stat_traverse (iso9660_t *p_iso, const iso9660_stat_t *_root,
		       char **splitpath)
{
  unsigned offset;
  uint8_t *_dirbuf = NULL;
  int ret;
  iso9660_stat_t *p_stat = NULL;
  iso9660_dir_t *p_iso9660_dir = NULL;

  if (!splitpath[0])
    {
      unsigned int len=sizeof(iso9660_stat_t) + strlen(_root->filename)+1;
      p_stat = calloc(1, len);
      cdio_assert (p_stat != NULL);
      memcpy(p_stat, _root, len);
      p_stat->rr.psz_symlink = calloc(1, p_stat->rr.i_symlink_max);
      cdio_assert (p_stat->rr.psz_symlink != NULL);
      memcpy(p_stat->rr.psz_symlink, _root->rr.psz_symlink,
	     p_stat->rr.i_symlink_max);
      return p_stat;
    }

  if (_root->type == _STAT_FILE)
    return NULL;

  cdio_assert (_root->type == _STAT_DIR);

  _dirbuf = calloc(1, _root->secsize[0] * ISO_BLOCKSIZE);
  if (!_dirbuf)
    {
    cdio_warn("Couldn't calloc(1, %d)", _root->secsize[0] * ISO_BLOCKSIZE);
    return NULL;
    }

  ret = iso9660_iso_seek_read (p_iso, _dirbuf, _root->lsn[0], _root->secsize[0]);
  if (ret!=ISO_BLOCKSIZE*_root->secsize[0]) return NULL;

  for (offset = 0; offset < (_root->secsize[0] * ISO_BLOCKSIZE);
       offset += iso9660_get_dir_len(p_iso9660_dir))
    {
      p_iso9660_dir = (void *) &_dirbuf[offset];
      int cmp;

      if (!iso9660_get_dir_len(p_iso9660_dir))
	{
	  offset++;
	  continue;
	}

      p_stat = _iso9660_dir_to_statbuf (p_iso9660_dir, p_stat,
					p_iso->b_xa, p_iso->u_joliet_level);

      if (!p_stat) {
	cdio_warn("Bad directory information for %s", splitpath[0]);
	free(_dirbuf);
	return NULL;
      }

      /* If we have multiextent file parts, loop until the last one */
      if (p_iso9660_dir->file_flags & ISO_MULTIEXTENT)
	continue;

      cmp = strcmp(splitpath[0], p_stat->filename);

      if ( 0 != cmp && 0 == p_iso->u_joliet_level
	   && yep != p_stat->rr.b3_rock ) {
	char *trans_fname = NULL;
	unsigned int i_trans_fname=strlen(p_stat->filename);

	if (i_trans_fname) {
	  trans_fname = calloc(1, i_trans_fname+1);
	  if (!trans_fname) {
	    cdio_warn("can't allocate %lu bytes",
		      (long unsigned int) strlen(p_stat->filename));
	    free(p_stat);
	    return NULL;
	  }
	  iso9660_name_translate_ext(p_stat->filename, trans_fname,
				     p_iso->u_joliet_level);
	  cmp = strcmp(splitpath[0], trans_fname);
	  free(trans_fname);
	}
      }

      if (!cmp) {
	iso9660_stat_t *ret_stat
	  = _fs_iso_stat_traverse (p_iso, p_stat, &splitpath[1]);
	free(p_stat->rr.psz_symlink);
	free(p_stat);
	free (_dirbuf);
	return ret_stat;
      }

      free(p_stat->rr.psz_symlink);
      free(p_stat);
      p_stat = NULL;
    }

  cdio_assert (offset == (_root->secsize[0] * ISO_BLOCKSIZE));

  /* not found */
  free (_dirbuf);
  return NULL;
}

/*!
  Get file status for psz_path into stat. NULL is returned on error.
 */
iso9660_stat_t *
iso9660_fs_stat (CdIo_t *p_cdio, const char psz_path[])
{
  iso9660_stat_t *p_root;
  char **p_psz_splitpath;
  iso9660_stat_t *p_stat;

  if (!p_cdio)   return NULL;
  if (!psz_path) return NULL;

  p_root = _fs_stat_root (p_cdio);

  if (!p_root)   return NULL;

  p_psz_splitpath = _cdio_strsplit (psz_path, '/');
  p_stat = _fs_stat_traverse (p_cdio, p_root, p_psz_splitpath);
  free(p_root);
  _cdio_strfreev (p_psz_splitpath);

  return p_stat;
}

typedef iso9660_stat_t * (stat_root_t) (void *p_image);
typedef iso9660_stat_t * (stat_traverse_t)
  (const void *p_image, const iso9660_stat_t *_root, char **splitpath);

/*!
  Get file status for psz_path into stat. NULL is returned on error.
  pathname version numbers in the ISO 9660
  name are dropped, i.e. ;1 is removed and if level 1 ISO-9660 names
  are lowercased.
 */
static iso9660_stat_t *
fs_stat_translate (void *p_image, stat_root_t stat_root,
		   stat_traverse_t stat_traverse,
		   const char psz_path[])
{
  iso9660_stat_t *p_root;
  char **p_psz_splitpath;
  iso9660_stat_t *p_stat;

  if (!p_image)  return NULL;
  if (!psz_path) return NULL;

  p_root = stat_root (p_image);
  if (!p_root) return NULL;

  p_psz_splitpath = _cdio_strsplit (psz_path, '/');
  p_stat = stat_traverse (p_image, p_root, p_psz_splitpath);
  free(p_root);
  _cdio_strfreev (p_psz_splitpath);

  return p_stat;
}

iso9660_stat_t *
iso9660_fs_stat_translate (CdIo_t *p_cdio, const char psz_path[],
			   bool b_mode2)
{
  return fs_stat_translate(p_cdio, (stat_root_t *) _fs_stat_root,
			   (stat_traverse_t *) _fs_stat_traverse,
			   psz_path);
}

/*!
  Get file status for psz_path into stat. NULL is returned on error.
  pathname version numbers in the ISO 9660
  name are dropped, i.e. ;1 is removed and if level 1 ISO-9660 names
  are lowercased.
 */
iso9660_stat_t *
iso9660_ifs_stat_translate (iso9660_t *p_iso, const char psz_path[])
{
  return fs_stat_translate(p_iso, (stat_root_t *) _ifs_stat_root,
			   (stat_traverse_t *) _fs_iso_stat_traverse,
			   psz_path);
}


/*!
  Get file status for psz_path into stat. NULL is returned on error.
 */
iso9660_stat_t *
iso9660_ifs_stat (iso9660_t *p_iso, const char psz_path[])
{
  iso9660_stat_t *p_root;
  char **splitpath;
  iso9660_stat_t *stat;

  if (!p_iso)    return NULL;
  if (!psz_path) return NULL;

  p_root = _ifs_stat_root (p_iso);
  if (!p_root) return NULL;

  splitpath = _cdio_strsplit (psz_path, '/');
  stat = _fs_iso_stat_traverse (p_iso, p_root, splitpath);
  free(p_root);
  _cdio_strfreev (splitpath);

  return stat;
}

/*!
  Read psz_path (a directory) and return a list of iso9660_stat_t
  of the files inside that. The caller must free the returned result.

  b_mode2 is historical. It is not used.
*/
CdioList_t *
iso9660_fs_readdir (CdIo_t *p_cdio, const char psz_path[], bool b_mode2)
{
  generic_img_private_t *p_env;
  iso9660_dir_t *p_iso9660_dir;
  iso9660_stat_t *p_iso9660_stat = NULL;
  iso9660_stat_t *p_stat;

  if (!p_cdio)   return NULL;
  if (!psz_path) return NULL;

  p_env = (generic_img_private_t *) p_cdio->env;

  p_stat = iso9660_fs_stat (p_cdio, psz_path);
  if (!p_stat) return NULL;

  if (p_stat->type != _STAT_DIR) {
    free(p_stat->rr.psz_symlink);
    free(p_stat);
    return NULL;
  }

  {
    unsigned offset = 0;
    uint8_t *_dirbuf = NULL;
    CdioList_t *retval = _cdio_list_new ();

    _dirbuf = calloc(1, p_stat->secsize[0] * ISO_BLOCKSIZE);
    if (!_dirbuf)
      {
      cdio_warn("Couldn't calloc(1, %d)", p_stat->secsize[0] * ISO_BLOCKSIZE);
      _cdio_list_free (retval, true);
      return NULL;
      }

    if (cdio_read_data_sectors (p_cdio, _dirbuf, p_stat->lsn[0],
				ISO_BLOCKSIZE, p_stat->secsize[0])) {
      _cdio_list_free (retval, true);
      return NULL;
    }

    while (offset < (p_stat->secsize[0] * ISO_BLOCKSIZE))
      {
	p_iso9660_dir = (void *) &_dirbuf[offset];

	if (!iso9660_get_dir_len(p_iso9660_dir))
	  {
	    offset++;
	    continue;
	  }

	p_iso9660_stat = _iso9660_dir_to_statbuf(p_iso9660_dir,
						 p_iso9660_stat, dunno,
						 p_env->u_joliet_level);
	if ((p_iso9660_stat) &&
	    ((p_iso9660_dir->file_flags & ISO_MULTIEXTENT) == 0))
	  {
	    _cdio_list_append (retval, p_iso9660_stat);
	    p_iso9660_stat = NULL;
	  }

	offset += iso9660_get_dir_len(p_iso9660_dir);
      }

    cdio_assert (offset == (p_stat->secsize[0] * ISO_BLOCKSIZE));

    free (_dirbuf);
    free (p_stat);
    return retval;
  }
}

/*!
  Read psz_path (a directory) and return a list of iso9660_stat_t
  of the files inside that. The caller must free the returned result.
*/
CdioList_t *
iso9660_ifs_readdir (iso9660_t *p_iso, const char psz_path[])
{
  iso9660_dir_t *p_iso9660_dir;
  iso9660_stat_t *p_iso9660_stat = NULL;
  iso9660_stat_t *p_stat;

  if (!p_iso)    return NULL;
  if (!psz_path) return NULL;

  p_stat = iso9660_ifs_stat (p_iso, psz_path);
  if (!p_stat)   return NULL;

  if (p_stat->type != _STAT_DIR) {
    free(p_stat->rr.psz_symlink);
    free(p_stat);
    return NULL;
  }

  {
    long int ret;
    unsigned offset = 0;
    uint8_t *_dirbuf = NULL;
    CdioList_t *retval = _cdio_list_new ();
    const size_t dirbuf_len = p_stat->secsize[0] * ISO_BLOCKSIZE;


    if (!dirbuf_len)
      {
        cdio_warn("Invalid directory buffer sector size %u", p_stat->secsize[0]);
	free(p_stat->rr.psz_symlink);
	free(p_stat);
	_cdio_list_free (retval, true);
        return NULL;
      }

    _dirbuf = calloc(1, dirbuf_len);
    if (!_dirbuf)
      {
        cdio_warn("Couldn't calloc(1, %lu)", (unsigned long)dirbuf_len);
	free(p_stat->rr.psz_symlink);
	free(p_stat);
	_cdio_list_free (retval, true);
        return NULL;
      }

    ret = iso9660_iso_seek_read (p_iso, _dirbuf, p_stat->lsn[0], p_stat->secsize[0]);
    if (ret != dirbuf_len) 	  {
      _cdio_list_free (retval, true);
      free(p_stat->rr.psz_symlink);
      free(p_stat);
      free (_dirbuf);
      return NULL;
    }

    while (offset < (dirbuf_len))
      {
	p_iso9660_dir = (void *) &_dirbuf[offset];

	if (!iso9660_get_dir_len(p_iso9660_dir))
	  {
	    offset++;
	    continue;
	  }

	p_iso9660_stat = _iso9660_dir_to_statbuf(p_iso9660_dir,
						 p_iso9660_stat,
						 p_iso->b_xa,
						 p_iso->u_joliet_level);
	if ((p_iso9660_stat) &&
	    ((p_iso9660_dir->file_flags & ISO_MULTIEXTENT) == 0))
	  {
	    _cdio_list_append(retval, p_iso9660_stat);
	    p_iso9660_stat = NULL;
	  }

	offset += iso9660_get_dir_len(p_iso9660_dir);
      }

    free (_dirbuf);
    free(p_stat->rr.psz_symlink);
    free (p_stat);

    if (offset != dirbuf_len) {
      _cdio_list_free (retval, true);
      return NULL;
    }

    return retval;
  }
}

typedef CdioList_t * (iso9660_readdir_t)
  (void *p_image,  const char * psz_path);

static iso9660_stat_t *
find_lsn_recurse (void *p_image, iso9660_readdir_t iso9660_readdir,
		  const char psz_path[], lsn_t lsn,
		  /*out*/ char **ppsz_full_filename)
{
  CdioList_t *entlist = iso9660_readdir (p_image, psz_path);
  CdioList_t *dirlist =  _cdio_list_new ();
  CdioListNode_t *entnode;

  cdio_assert (entlist != NULL);

  /* iterate over each entry in the directory */

  _CDIO_LIST_FOREACH (entnode, entlist)
    {
      iso9660_stat_t *statbuf = _cdio_list_node_data (entnode);
      const char *psz_filename  = (char *) statbuf->filename;
      unsigned int len = strlen(psz_path) + strlen(psz_filename)+2;
      size_t extent;

      if (*ppsz_full_filename != NULL) free(*ppsz_full_filename);
      *ppsz_full_filename = calloc(1, len);
      _snprintf (*ppsz_full_filename, len, "%s%s/", psz_path, psz_filename);

      if (statbuf->type == _STAT_DIR
          && strcmp ((char *) statbuf->filename, ".")
          && strcmp ((char *) statbuf->filename, "..")) {
        _cdio_list_append (dirlist, strdup(*ppsz_full_filename));
      }

      for (extent = 0; extent < statbuf->extents; extent++) {
	if (statbuf->lsn[extent] == lsn) {
	  const unsigned int len2 = sizeof(iso9660_stat_t)+strlen(statbuf->filename)+1;
	  iso9660_stat_t *ret_stat = calloc(1, len2);
	  if (!ret_stat) {
	    _cdio_list_free (dirlist, true);
	    cdio_warn("Couldn't calloc(1, %d)", len2);
	    return NULL;
	  }
	  memcpy(ret_stat, statbuf, len2);
          _cdio_list_free (entlist, true);
          _cdio_list_free (dirlist, true);
          return ret_stat;
	}
      }

    }

  _cdio_list_free (entlist, true);

  /* now recurse/descend over directories encountered */

  _CDIO_LIST_FOREACH (entnode, dirlist)
    {
      char *psz_path_prefix = _cdio_list_node_data (entnode);
      iso9660_stat_t *ret_stat;
      free(*ppsz_full_filename);
      *ppsz_full_filename = NULL;
      ret_stat = find_lsn_recurse (p_image, iso9660_readdir,
				   psz_path_prefix, lsn,
				   ppsz_full_filename);

      if (NULL != ret_stat) {
        _cdio_list_free (dirlist, true);
        return ret_stat;
      }
    }

  if (*ppsz_full_filename != NULL) {
    free(*ppsz_full_filename);
    *ppsz_full_filename = NULL;
  }
  _cdio_list_free (dirlist, true);
  return NULL;
}

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   Returns stat_t of entry if we found lsn, or NULL otherwise.
 */
iso9660_stat_t *
iso9660_fs_find_lsn(CdIo_t *p_cdio, lsn_t i_lsn)
{
  char *psz_full_filename = NULL;
  return find_lsn_recurse (p_cdio, (iso9660_readdir_t *) iso9660_fs_readdir,
			   "/", i_lsn, &psz_full_filename);
}

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   Returns stat_t of entry if we found lsn, or NULL otherwise.
 */
iso9660_stat_t *
iso9660_fs_find_lsn_with_path(CdIo_t *p_cdio, lsn_t i_lsn,
			      /*out*/ char **ppsz_full_filename)
{
  return find_lsn_recurse (p_cdio, (iso9660_readdir_t *) iso9660_fs_readdir,
			   "/", i_lsn, ppsz_full_filename);
}

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   Returns stat_t of entry if we found lsn, or NULL otherwise.
 */
iso9660_stat_t *
iso9660_ifs_find_lsn(iso9660_t *p_iso, lsn_t i_lsn)
{
  char *psz_full_filename = NULL;
  return find_lsn_recurse (p_iso, (iso9660_readdir_t *) iso9660_ifs_readdir,
			   "/", i_lsn, &psz_full_filename);
}

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   Returns stat_t of entry if we found lsn, or NULL otherwise.
 */
iso9660_stat_t *
iso9660_ifs_find_lsn_with_path(iso9660_t *p_iso, lsn_t i_lsn,
			       /*out*/ char **ppsz_full_filename)
{
  return find_lsn_recurse (p_iso, (iso9660_readdir_t *) iso9660_ifs_readdir,
			   "/", i_lsn, ppsz_full_filename);
}

/*!
  Free the passed iso9660_stat_t structure.
 */
void
iso9660_stat_free(iso9660_stat_t *p_stat)
{
  if (p_stat != NULL)
    free(p_stat);
}

/*!
  Return true if ISO 9660 image has extended attrributes (XA).
*/
bool
iso9660_ifs_is_xa (const iso9660_t * p_iso)
{
  if (!p_iso) return false;
  return yep == p_iso->b_xa;
}

static bool_3way_t
iso_have_rr_traverse (iso9660_t *p_iso, const iso9660_stat_t *_root,
		      char **splitpath, uint64_t *pu_file_limit)
{
  unsigned offset = 0;
  uint8_t *_dirbuf = NULL;
  int ret;
  bool_3way_t have_rr = nope;

  if (!splitpath[0]) return false;

  if (_root->type == _STAT_FILE) return nope;
  if (*pu_file_limit == 0) return dunno;

  cdio_assert (_root->type == _STAT_DIR);

  _dirbuf = calloc(1, _root->secsize[0] * ISO_BLOCKSIZE);
  if (!_dirbuf)
    {
    cdio_warn("Couldn't calloc(1, %d)", _root->secsize[0] * ISO_BLOCKSIZE);
    return dunno;
    }

  ret = iso9660_iso_seek_read (p_iso, _dirbuf, _root->lsn[0], _root->secsize[0]);
  if (ret!=ISO_BLOCKSIZE*_root->secsize[0]) return false;

  while (offset < (_root->secsize[0] * ISO_BLOCKSIZE))
    {
      iso9660_dir_t *p_iso9660_dir = (void *) &_dirbuf[offset];
      iso9660_stat_t *p_stat;

      if (!iso9660_get_dir_len(p_iso9660_dir))
	{
	  offset++;
	  continue;
	}

      p_stat = _iso9660_dir_to_statbuf (p_iso9660_dir, NULL, p_iso->b_xa,
					p_iso->u_joliet_level);
      have_rr = p_stat->rr.b3_rock;
      if ( have_rr != yep) {
	have_rr = iso_have_rr_traverse (p_iso, p_stat, &splitpath[1], pu_file_limit);
      }
      free(p_stat);
      if (have_rr != nope) {
	free (_dirbuf);
	return have_rr;
      }

      offset += iso9660_get_dir_len(p_iso9660_dir);
      *pu_file_limit = (*pu_file_limit)-1;
      if ((*pu_file_limit) == 0) {
	free (_dirbuf);
	return dunno;
      }
    }

  cdio_assert (offset == (_root->secsize[0] * ISO_BLOCKSIZE));

  /* not found */
  free (_dirbuf);
  return nope;
}

/*!
  Return "yup" if any file has Rock-Ridge extensions. Warning: this can
  be time consuming. On an ISO 9600 image with lots of files but no Rock-Ridge
  extensions, the entire directory structure will be scanned up to u_file_limit.

  @param p_iso the ISO-9660 file image to get data from

  @param u_file_limit the maximimum number of (non-rock-ridge) files
  to consider before giving up and returning "dunno".

  "dunno" can also be returned if there was some error encountered
  such as not being able to allocate memory in processing.

*/
extern bool_3way_t
iso9660_have_rr(iso9660_t *p_iso, uint64_t u_file_limit)
{
  iso9660_stat_t *p_root;
  char *p_psz_splitpath[2] = {strdup("/"), strdup("")};
  bool_3way_t is_rr = nope;

  if (!p_iso) return false;

  p_root = _ifs_stat_root (p_iso);
  if (!p_root) return dunno;

  if (u_file_limit == 0) u_file_limit = UINT64_MAX;

  is_rr = iso_have_rr_traverse (p_iso, p_root, p_psz_splitpath, &u_file_limit);
  free(p_root);
  // _cdio_strfreev (p_psz_splitpath);

  return is_rr;
}
