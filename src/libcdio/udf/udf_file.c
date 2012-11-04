/*
  Copyright (C) 2005, 2006, 2008, 2010, 2012 Rocky Bernstein <rocky@gnu.org>

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
/* Access routines */

/* udf_private.h has to come first else _FILE_OFFSET_BITS are redefined in
   say opensolaris. */
#include "udf_private.h"
#include <cdio/bytesex.h>
#include "udf_fs.h"

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>  /* Remove when adding cdio/logging.h */
#endif

/* Useful defines */

#define MIN(a, b) (a<b) ? (a) : (b)
#define CEILING(x, y) ((x+(y-1))/y)

#define	GETICB(offset)	\
	&p_udf_fe->u.alloc_descs[offset]

const char *
udf_get_filename(const udf_dirent_t *p_udf_dirent)
{
  if (!p_udf_dirent) return NULL;
  if (!p_udf_dirent->psz_name) return "..";
  return p_udf_dirent->psz_name;
}

/* Copy an UDF File Entry into a Directory Entry structure. */
bool
udf_get_file_entry(const udf_dirent_t *p_udf_dirent, 
		   /*out*/ udf_file_entry_t *p_udf_fe)
{
  if (!p_udf_dirent) return false;
  memcpy(p_udf_fe, &p_udf_dirent->fe, sizeof(udf_file_entry_t));
  return true;
}

/*!
  Return the file id descriptor of the given file.
*/
bool udf_get_fileid_descriptor(const udf_dirent_t *p_udf_dirent, 
			       /*out*/ udf_fileid_desc_t *p_udf_fid)
{
  
  if (!p_udf_dirent) return false;
  if (!p_udf_dirent->fid) {
    /* FIXME do something about trying to get the descriptor. */
    return false;
  }
  memcpy(p_udf_fid, p_udf_dirent->fid, sizeof(udf_fileid_desc_t));
  return true;
}


/*!
  Return the number of hard links of the file. Return 0 if error.
*/
uint16_t udf_get_link_count(const udf_dirent_t *p_udf_dirent) 
{
  if (p_udf_dirent) {
    return uint16_from_le(p_udf_dirent->fe.link_count);
  }
  return 0; /* Error. Non-error case handled above. */
}

/*!
  Return the file length the file. Return 2147483647L if error.
*/
uint64_t udf_get_file_length(const udf_dirent_t *p_udf_dirent) 
{
  if (p_udf_dirent) {
    return uint64_from_le(p_udf_dirent->fe.info_len);
  }
  return 2147483647L; /* Error. Non-error case handled above. */
}

/*!
  Return true if the file is a directory.
*/
bool
udf_is_dir(const udf_dirent_t *p_udf_dirent)
{
  return p_udf_dirent->b_dir;
}

/*
 * Translate a file offset into a logical block and then into a physical
 * block.
 */
static lba_t
offset_to_lba(const udf_dirent_t *p_udf_dirent, off_t i_offset, 
	      /*out*/ lba_t *pi_lba, /*out*/ uint32_t *pi_max_size)
{
  udf_t *p_udf = p_udf_dirent->p_udf;
  const udf_file_entry_t *p_udf_fe = (udf_file_entry_t *) 
    &p_udf_dirent->fe;
  const udf_icbtag_t *p_icb_tag = &p_udf_fe->icb_tag;
  const uint16_t strat_type= uint16_from_le(p_icb_tag->strat_type);

  if (i_offset < 0) {
    cdio_warn("Negative offset value");
    return CDIO_INVALID_LBA;
  }

  switch (strat_type) {
  case 4096:
    cdio_warn("Cannot deal with strategy4096 yet!");
    return CDIO_INVALID_LBA;
    break;
  case ICBTAG_STRATEGY_TYPE_4:
    {
      off_t icblen = 0;
      uint64_t lsector;
      int ad_offset, ad_num = 0;
      uint16_t addr_ilk = uint16_from_le(p_icb_tag->flags&ICBTAG_FLAG_AD_MASK);
      
      switch (addr_ilk) {
      case ICBTAG_FLAG_AD_SHORT: 
	{
	  udf_short_ad_t *p_icb;
	  /*
	   * The allocation descriptor field is filled with short_ad's.
	   * If the offset is beyond the current extent, look for the
	   * next extent.
	   */
	  do {
	    i_offset -= icblen;
	    ad_offset = sizeof(udf_short_ad_t) * ad_num;
	    if (ad_offset > uint32_from_le(p_udf_fe->i_alloc_descs)) {
	      cdio_warn("File offset out of bounds");
	      return CDIO_INVALID_LBA;
	    }
	    p_icb = (udf_short_ad_t *) 
	      GETICB( uint32_from_le(p_udf_fe->i_extended_attr) 
		      + ad_offset );
	    icblen = p_icb->len;
	    ad_num++;
	  } while(i_offset >= icblen);
	  
	  lsector = (i_offset / UDF_BLOCKSIZE) + p_icb->pos;
	  
	  *pi_max_size = p_icb->len;
	}
	break;
      case ICBTAG_FLAG_AD_LONG: 
	{
	  /*
	   * The allocation descriptor field is filled with long_ad's
	   * If the i_offset is beyond the current extent, look for the
	   * next extent.
	   */
	  udf_long_ad_t *p_icb;
	  do {
	    i_offset -= icblen;
	    ad_offset = sizeof(udf_long_ad_t) * ad_num;
	    if (ad_offset > uint32_from_le(p_udf_fe->i_alloc_descs)) {
	      cdio_warn("File offset out of bounds");
	      return CDIO_INVALID_LBA;
	    }
	    p_icb = (udf_long_ad_t *) 
	      GETICB( uint32_from_le(p_udf_fe->i_extended_attr)
		      + ad_offset );
	    icblen = p_icb->len;
	    ad_num++;
	  } while(i_offset >= icblen);
	
	  lsector = (i_offset / UDF_BLOCKSIZE) +
	    uint32_from_le(((udf_long_ad_t *)(p_icb))->loc.lba);
	  
	  *pi_max_size = p_icb->len;
	}
	break;
      case ICBTAG_FLAG_AD_IN_ICB:
	/*
	 * This type means that the file *data* is stored in the
	 * allocation descriptor field of the file entry.
	 */
	*pi_max_size = 0;
	cdio_warn("Don't know how to data in ICB handle yet");
	return CDIO_INVALID_LBA;
      case ICBTAG_FLAG_AD_EXTENDED:
	cdio_warn("Don't know how to handle extended addresses yet");
	return CDIO_INVALID_LBA;
      default:
	cdio_warn("Unsupported allocation descriptor %d", addr_ilk);
	return CDIO_INVALID_LBA;
      }

      *pi_lba = (lba_t)lsector + p_udf->i_part_start;
      if (*pi_lba < 0) {
	cdio_warn("Negative LBA value");
	return CDIO_INVALID_LBA;
      }
      return *pi_lba;
    }
  default:
    cdio_warn("Unknown strategy type %d", strat_type);
    return DRIVER_OP_ERROR;
  }
}

/**
  Attempts to read up to count bytes from UDF directory entry
  p_udf_dirent into the buffer starting at buf. buf should be a
  multiple of UDF_BLOCKSIZE bytes. Reading continues after the point
  at which we last read or from the beginning the first time.

  If count is zero, read() returns zero and has no other results. If
  count is greater than SSIZE_MAX, the result is unspecified.

  It is the caller's responsibility to ensure that count is less
  than the number of blocks recorded via p_udf_dirent.

  If there is an error, cast the result to driver_return_code_t for 
  the specific error code.
*/
ssize_t
udf_read_block(const udf_dirent_t *p_udf_dirent, void * buf, size_t count)
{
  if (count == 0) return 0;
  else {
    driver_return_code_t ret;
    uint32_t i_max_size=0;
    udf_t *p_udf = p_udf_dirent->p_udf;
    lba_t i_lba = offset_to_lba(p_udf_dirent, p_udf->i_position, &i_lba, 
				&i_max_size);
    if (i_lba != CDIO_INVALID_LBA) {
      uint32_t i_max_blocks = CEILING(i_max_size, UDF_BLOCKSIZE);
      if ( i_max_blocks < count ) {
	  cdio_warn("read count %u is larger than %u extent size.",
		  (unsigned int)count, i_max_blocks);
	  cdio_warn("read count truncated to %u", (unsigned int)count);
	  count = i_max_blocks;
      }
      ret = udf_read_sectors(p_udf, buf, i_lba, count);
      if (DRIVER_OP_SUCCESS == ret) {
	ssize_t i_read_len = MIN(i_max_size, count * UDF_BLOCKSIZE);
	p_udf->i_position += i_read_len;
	return i_read_len;
      }
      return ret;
    } else {
      return DRIVER_OP_ERROR;
    }
  }
}
