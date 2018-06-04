/*
  Copyright (C) 2005-2006, 2008, 2011, 2013-2014, 2017
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
/*
 * Portions copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifdef HAVE_CONFIG_H
# include "config.h"
# define __CDIO_CONFIG_H__ 1
#endif

#include <cdio/util.h>

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

/* These definitions are also to make debugging easy. Note that they
   have to come *before* #include <cdio/ecma_167.h> which sets
   #defines for these.
*/
const char VSD_STD_ID_BEA01[] = {'B', 'E', 'A', '0', '1'};
const char VSD_STD_ID_BOOT2[] = {'B', 'O', 'O', 'T', '2'};
const char VSD_STD_ID_CD001[] = {'C', 'D', '0', '0', '1'};
const char VSD_STD_ID_CDW01[] = {'C', 'D', 'W', '0', '2'};
const char VSD_STD_ID_NSR03[] = {'N', 'S', 'R', '0', '3'};
const char VSD_STD_ID_TEA01[] = {'T', 'E', 'A', '0', '1'};

#include <cdio/bytesex.h>
#include <cdio/utf8.h>
#include <cdio/util.h>

#include "udf_private.h"
#include "udf_fs.h"
#include "cdio_assert.h"

/*
 * The UDF specs are pretty clear on how each data structure is made
 * up, but not very clear on how they relate to each other.  Here is
 * the skinny... This demostrates a filesystem with one file in the
 * root directory.  Subdirectories are treated just as normal files,
 * but they have File Id Descriptors of their children as their file
 * data.  As for the Anchor Volume Descriptor Pointer, it can exist in
 * two of the following three places: sector 256, sector n (the max
 * sector of the disk), or sector n - 256.  It's a pretty good bet
 * that one will exist at sector 256 though.  One caveat is unclosed
 * CD media.  For that, sector 256 cannot be written, so the Anchor
 * Volume Descriptor Pointer can exist at sector 512 until the media
 * is closed.
 *
 *  Sector:
 *     256:
 *       n: Anchor Volume Descriptor Pointer
 * n - 256:	|
 *		|
 *		|-->Main Volume Descriptor Sequence
 *			|	|
 *			|	|
 *			|	|-->Logical Volume Descriptor
 *			|			  |
 *			|-->Partition Descriptor  |
 *				|		  |
 *				|		  |
 *				|-->Fileset Descriptor
 *					|
 *					|
 *					|-->Root Dir File Entry
 *						|
 *						|
 *						|-->File data:
 *						    File Id Descriptor
 *							|
 *							|
 *							|-->File Entry
 *								|
 *								|
 *								|-->File data
 */

static udf_dirent_t *
udf_new_dirent(udf_file_entry_t *p_udf_fe, udf_t *p_udf,
	       const char *psz_name, bool b_dir, bool b_parent);

/**
 * Check the descriptor tag for both the correct id and correct checksum.
 * Return zero if all is good, -1 if not.
 */
int
udf_checktag(const udf_tag_t *p_tag, udf_Uint16_t tag_id)
{
  uint8_t *itag;
  uint8_t i;
  uint8_t cksum = 0;

  itag = (uint8_t *)p_tag;

#ifdef WORDS_BIGENDIAN
  tag_id = UINT16_SWAP_LE_BE(tag_id);
#endif

  if (p_tag->id != tag_id)
    return -1;

  for (i = 0; i < 15; i++)
    cksum = cksum + itag[i];
  cksum = cksum - itag[4];

  if (cksum == p_tag->cksum)
    return 0;

  return -1;
}

bool
udf_get_lba(const udf_file_entry_t *p_udf_fe,
	    /*out*/ uint32_t *start, /*out*/ uint32_t *end)
{
  if (! p_udf_fe->i_alloc_descs)
    return false;

  switch (p_udf_fe->icb_tag.flags & ICBTAG_FLAG_AD_MASK) {
  case ICBTAG_FLAG_AD_SHORT:
    {
      /* The allocation descriptor field is filled with short_ad's. */
      udf_short_ad_t *p_ad = (udf_short_ad_t *)
	(p_udf_fe->u.ext_attr + uint32_from_le(p_udf_fe->i_extended_attr));

      *start = uint32_from_le(p_ad->pos);
      *end = *start +
	((uint32_from_le(p_ad->len) & UDF_LENGTH_MASK) - 1) / UDF_BLOCKSIZE;
      return true;
    }
    break;
  case ICBTAG_FLAG_AD_LONG:
    {
      /* The allocation descriptor field is filled with long_ad's */
      udf_long_ad_t *p_ad = (udf_long_ad_t *)
	(p_udf_fe->u.ext_attr + uint32_from_le(p_udf_fe->i_extended_attr));

      *start = uint32_from_le(p_ad->loc.lba); /* ignore partition number */
      *end = *start +
	((uint32_from_le(p_ad->len) & UDF_LENGTH_MASK) - 1) / UDF_BLOCKSIZE;
      return true;
    }
    break;
  case ICBTAG_FLAG_AD_EXTENDED:
    {
      udf_ext_ad_t *p_ad = (udf_ext_ad_t *)
	(p_udf_fe->u.ext_attr + uint32_from_le(p_udf_fe->i_extended_attr));

      *start = uint32_from_le(p_ad->ext_loc.lba); /* ignore partition number */
      *end = *start +
	((uint32_from_le(p_ad->len) & UDF_LENGTH_MASK) - 1) / UDF_BLOCKSIZE;
      return true;
    }
    break;
  default:
    return false;
  }
  return false;
}

#define udf_PATH_DELIMITERS "/\\"

/* Searches p_udf_dirent for a directory entry called psz_token.
   Note that p_udf_dirent may be replaced or freed during this call
   and only the returned udf_dirent_t must be used afterwards.
*/
static
udf_dirent_t *
udf_ff_traverse(udf_dirent_t *p_udf_dirent, char *psz_token)
{
  while ((p_udf_dirent = udf_readdir(p_udf_dirent))) {
    if (strcmp(psz_token, p_udf_dirent->psz_name) == 0) {
      char *next_tok = strtok(NULL, udf_PATH_DELIMITERS);

      if (!next_tok)
	return p_udf_dirent; /* found */
      else if (p_udf_dirent->b_dir) {
	udf_dirent_t * p_udf_dirent_next = udf_opendir(p_udf_dirent);

	if (p_udf_dirent_next) {
	  /* free p_udf_dirent to avoid leaking memory. */
	  udf_dirent_free(p_udf_dirent);

	  /* previous p_udf_dirent_next is freed by udf_ff_traverse. */
	  p_udf_dirent_next = udf_ff_traverse(p_udf_dirent_next, next_tok);

	  return p_udf_dirent_next;
	}
      }
    }
  }

  return NULL;
}

/* FIXME! */
#define udf_MAX_PATHLEN 2048

udf_dirent_t *
udf_fopen(udf_dirent_t *p_udf_root, const char *psz_name)
{
  udf_dirent_t *p_udf_file = NULL;

  if (p_udf_root) {
    char tokenline[udf_MAX_PATHLEN];
    char *psz_token;

    /* file position must be reset when accessing a new file */
    p_udf_root->p_udf->i_position = 0;

    strncpy(tokenline, psz_name, udf_MAX_PATHLEN-1);
    tokenline[udf_MAX_PATHLEN-1] = '\0';
    psz_token = strtok(tokenline, udf_PATH_DELIMITERS);
    if (psz_token) {
      udf_dirent_t *p_udf_dirent =
	udf_new_dirent(&p_udf_root->fe, p_udf_root->p_udf,
		       p_udf_root->psz_name, p_udf_root->b_dir,
		       p_udf_root->b_parent);
      p_udf_file = udf_ff_traverse(p_udf_dirent, psz_token);
    }
    else if ( 0 == strncmp("/", psz_name, sizeof("/")) ) {
      return udf_new_dirent(&p_udf_root->fe, p_udf_root->p_udf,
			    p_udf_root->psz_name, p_udf_root->b_dir,
			    p_udf_root->b_parent);
    }
  }
  return p_udf_file;
}

/* Convert unicode16 to UTF-8.
   The returned string is allocated and must be freed by the caller
*/
static char*
unicode16_decode(const uint8_t *data, int i_len)
{
  int i;
  char* r = NULL;

  switch (data[0])
  {
  case 8:
    r = (char*)calloc(i_len, 1);
    if (r == NULL)
      return r;
    for (i=0; i<i_len-1; i++)
      r[i] = data[i+1];
    return r;
  case 16:
    cdio_charset_to_utf8((char*)&data[1], i_len-1, &r, "UCS-2BE");
    return r;
  default:
    /* Empty string, as some existing sections can't take a NULL pointer */
    r = (char*)calloc(1, 1);
    return r;
  }
}


static udf_dirent_t *
udf_new_dirent(udf_file_entry_t *p_udf_fe, udf_t *p_udf,
	       const char *psz_name, bool b_dir, bool b_parent)
{
  udf_dirent_t *p_udf_dirent = (udf_dirent_t *)
    calloc(1, sizeof(udf_dirent_t));
  if (!p_udf_dirent) return NULL;

  p_udf_dirent->psz_name     = strdup(psz_name);
  p_udf_dirent->b_dir        = b_dir;
  p_udf_dirent->b_parent     = b_parent;
  p_udf_dirent->p_udf        = p_udf;
  p_udf_dirent->i_part_start = p_udf->i_part_start;
  p_udf_dirent->dir_left     = uint64_from_le(p_udf_fe->info_len);

  memcpy(&(p_udf_dirent->fe), p_udf_fe,
	 sizeof(udf_file_entry_t));
  udf_get_lba( p_udf_fe, &(p_udf_dirent->i_loc),
	       &(p_udf_dirent->i_loc_end) );
  return p_udf_dirent;
}

/*!
  Seek to a position i_start and then read i_blocks. Number of blocks read is
  returned. One normally expects the return to be equal to i_blocks.
*/
driver_return_code_t
udf_read_sectors (const udf_t *p_udf, void *ptr, lsn_t i_start,
		 long i_blocks)
{
  driver_return_code_t ret;
  long i_read;
  off_t i_byte_offset;

  if (!p_udf) return 0;
  /* Without the cast, i_start * UDF_BLOCKSIZE may be evaluated as 32 bit */
  i_byte_offset = ((off_t)i_start) * UDF_BLOCKSIZE;
  /* Since we're using SEEK_SET, the value must be positive */
  if (i_byte_offset < 0) {
    if (sizeof(off_t) <= 4)	/* probably missing LFS */
      cdio_warn("Large File Support is required to access streams of 2 GB or more");
    return DRIVER_OP_BAD_PARAMETER;
  }

  if (p_udf->b_stream) {
    ret = cdio_stream_seek (p_udf->stream, i_byte_offset, SEEK_SET);
    if (DRIVER_OP_SUCCESS != ret) return ret;
    i_read = cdio_stream_read (p_udf->stream, ptr, UDF_BLOCKSIZE, i_blocks);
    if (i_read) return DRIVER_OP_SUCCESS;
    return DRIVER_OP_ERROR;
  } else {
    return cdio_read_data_sectors(p_udf->cdio, ptr, i_start, UDF_BLOCKSIZE,
				  i_blocks);
  }
}

/*!
  Open an UDF for reading. Maybe in the future we will have
  a mode. NULL is returned on error.

  Caller must free result - use udf_close for that.
*/
udf_t *
udf_open (const char *psz_path)
{
  udf_t *p_udf = (udf_t *) calloc(1, sizeof(udf_t)) ;
  uint8_t data[UDF_BLOCKSIZE];

  if (!p_udf) return NULL;

  /* Sanity check */
  cdio_assert(sizeof(udf_file_entry_t) == UDF_BLOCKSIZE);

  p_udf->cdio = cdio_open(psz_path, DRIVER_UNKNOWN);
  if (!p_udf->cdio) {
    /* Not a CD-ROM drive or CD Image. Maybe it's a UDF file not
       encapsulated as a CD-ROM Image (e.g. often .UDF or (sic) .ISO)
    */
    p_udf->stream = cdio_stdio_new( psz_path );
    if (!p_udf->stream)
      goto error;
    p_udf->b_stream = true;
  }

  /*
   * Look for an Anchor Volume Descriptor Pointer at sector 256.
   */
  if (DRIVER_OP_SUCCESS != udf_read_sectors (p_udf, &data, 256, 1) )
    goto error;

  memcpy(&(p_udf->anchor_vol_desc_ptr), &data, sizeof(anchor_vol_desc_ptr_t));

  if (udf_checktag((udf_tag_t *)&(p_udf->anchor_vol_desc_ptr), TAGID_ANCHOR))
    goto error;

  /*
   * Then try to find a reference to a Primary Volume Descriptor.
   */
  {
    anchor_vol_desc_ptr_t *p_avdp = &p_udf->anchor_vol_desc_ptr;

    const uint32_t mvds_start =
      uint32_from_le(p_avdp->main_vol_desc_seq_ext.loc);
    const uint32_t mvds_end   = mvds_start +
      (uint32_from_le(p_avdp->main_vol_desc_seq_ext.len) - 1) / UDF_BLOCKSIZE;

    uint32_t i_lba;

    for (i_lba = mvds_start; i_lba < mvds_end; i_lba++) {
      udf_pvd_t *p_pvd = (udf_pvd_t *) &data;

      if (DRIVER_OP_SUCCESS != udf_read_sectors (p_udf, p_pvd, i_lba, 1) )
	goto error;

      if (!udf_checktag(&p_pvd->tag, TAGID_PRI_VOL)) {
	p_udf->pvd_lba = i_lba;
	break;
      }

    }

    /*
     * If we couldn't find a reference, bail out.
     */
    if (i_lba == mvds_end)
      goto error;
  }

  return p_udf;

 error:
  cdio_stdio_destroy(p_udf->stream);
  free(p_udf);
  return NULL;
}

/**
 * Gets the Volume Identifier, as an UTF-8 string
 * psz_volid, place to put the string
 * i_volid, size of the buffer psz_volid points to
 * returns the size of buffer needed for all data
 * Note: this call accepts a NULL psz_volid, to retrieve the length required.
 */
int
udf_get_volume_id(udf_t *p_udf, /*out*/ char *psz_volid, unsigned int i_volid)
{
  uint8_t data[UDF_BLOCKSIZE];
  const udf_pvd_t *p_pvd = (udf_pvd_t *) &data;
  char* r;
  unsigned int volid_len;

  /* clear the output to empty string */
  if (psz_volid != NULL)
    psz_volid[0] = 0;

  /* get primary volume descriptor */
  if ( DRIVER_OP_SUCCESS != udf_read_sectors(p_udf, &data, p_udf->pvd_lba, 1) )
    return 0;

  volid_len = p_pvd->vol_ident[UDF_VOLID_SIZE-1];
  if(volid_len > UDF_VOLID_SIZE-1) {
    /* this field is only UDF_VOLID_SIZE bytes something is wrong */
    volid_len = UDF_VOLID_SIZE-1;
  }

  r = unicode16_decode((uint8_t *) p_pvd->vol_ident, volid_len);
  if (r == NULL)
    return 0;

  volid_len = strlen(r)+1;     /* +1 for NUL terminator */
  if (psz_volid != NULL) {
    strncpy(psz_volid, r, MIN(volid_len, i_volid));
    psz_volid[i_volid-1] = 0;  /* strncpy does not always terminate the dest */
  }
  free(r);

  return volid_len;
}

/**
 * Gets the Volume Set Identifier, as a 128-byte dstring (not decoded)
 * WARNING This is not a null terminated string
 * volsetid, place to put the data
 * i_volsetid, size of the buffer psz_volsetid points to
 * the buffer should be >=128 bytes to store the whole volumesetidentifier
 * returns the size of the available volsetid information (128)
 * or 0 on error
 */
int
udf_get_volumeset_id(udf_t *p_udf, /*out*/ uint8_t *volsetid,
		     unsigned int i_volsetid)
{
  uint8_t data[UDF_BLOCKSIZE];
  const udf_pvd_t *p_pvd = (udf_pvd_t *) &data;

  /* get primary volume descriptor */
  if ( DRIVER_OP_SUCCESS != udf_read_sectors(p_udf, &data, p_udf->pvd_lba, 1) )
    return 0;

  if (i_volsetid > UDF_VOLSET_ID_SIZE) {
    i_volsetid = UDF_VOLSET_ID_SIZE;
  }

  memcpy(volsetid, p_pvd->volset_id, i_volsetid);

  return UDF_VOLSET_ID_SIZE;
}

/**
 * Gets the Logical Volume Identifier string, as an UTF-8 string
 * psz_logvolid, place to put the string (should be at least 64 bytes)
 * i_logvolid, size of the buffer psz_logvolid points to
 * returns the size of buffer needed for all data, including NUL terminator
 * A call to udf_get_root() should have been issued before this call
 * Note: this call accepts a NULL psz_volid, to retrieve the length required.
 */
int
udf_get_logical_volume_id(udf_t *p_udf, /*out*/ char *psz_logvolid, unsigned int i_logvolid)
{
  uint8_t data[UDF_BLOCKSIZE];
  logical_vol_desc_t *p_logvol = (logical_vol_desc_t *) &data;
  char* r;
  int logvolid_len;

  /* clear the output to empty string */
  if (psz_logvolid != NULL)
    psz_logvolid[0] = 0;

  if (DRIVER_OP_SUCCESS != udf_read_sectors (p_udf, p_logvol, p_udf->lvd_lba, 1) )
    return 0;

  r = unicode16_decode((uint8_t *) p_logvol->logvol_id, p_logvol->logvol_id[127]);
  if (r == NULL)
    return 0;

  logvolid_len = strlen(r)+1;  /* +1 for NUL terminator */
  if (psz_logvolid != NULL) {
    strncpy(psz_logvolid, r, MIN(logvolid_len, i_logvolid));
    psz_logvolid[i_logvolid-1] = 0;    /* strncpy does not always terminate the dest */
  }
  free(r);

  return logvolid_len;
}

/*!
  Get the root in p_udf. If b_any_partition is false then
  the root must be in the given partition.
  NULL is returned if the partition is not found or a root is not found or
  there is on error.

  Caller must free result - use udf_dirent_free for that.
*/
udf_dirent_t *
udf_get_root (udf_t *p_udf, bool b_any_partition, partition_num_t i_partition)
{
  const anchor_vol_desc_ptr_t *p_avdp = &p_udf->anchor_vol_desc_ptr;
  const uint32_t mvds_start =
    uint32_from_le(p_avdp->main_vol_desc_seq_ext.loc);
  const uint32_t mvds_end   = mvds_start +
    (uint32_from_le(p_avdp->main_vol_desc_seq_ext.len) - 1) / UDF_BLOCKSIZE;
  uint32_t i_lba;
  uint8_t data[UDF_BLOCKSIZE];

  /*
     Now we have the joy of finding the Partition Descriptor and the
     Logical Volume Descriptor for the Main Volume Descriptor
     Sequence. Once we've got that, we use the Logical Volume
     Descriptor to get a Fileset Descriptor and that has the Root
     Directory File Entry.
  */
  for (i_lba = mvds_start; i_lba < mvds_end; i_lba++) {
    uint8_t data2[UDF_BLOCKSIZE];

    partition_desc_t *p_partition = (partition_desc_t *) &data2;

    if (DRIVER_OP_SUCCESS != udf_read_sectors (p_udf, p_partition, i_lba, 1) )
      return NULL;

    if (!udf_checktag(&p_partition->tag, TAGID_PARTITION)) {
      const partition_num_t i_partition_check
	= uint16_from_le(p_partition->number);
      if (b_any_partition || i_partition_check == i_partition) {
	/* Squirrel away some data regarding partition */
	p_udf->i_partition = uint16_from_le(p_partition->number);
	p_udf->i_part_start = uint32_from_le(p_partition->start_loc);
	if (p_udf->lvd_lba) break;
      }
    } else if (!udf_checktag(&p_partition->tag, TAGID_LOGVOL)) {
      /* Get fileset descriptor */
      logical_vol_desc_t *p_logvol = (logical_vol_desc_t *) &data2;
      bool b_valid =
	UDF_BLOCKSIZE == uint32_from_le(p_logvol->logical_blocksize);

      if (b_valid) {
	p_udf->lvd_lba = i_lba;
	p_udf->fsd_offset =
	  uint32_from_le(p_logvol->lvd_use.fsd_loc.loc.lba);
	if (p_udf->i_part_start) break;
      }
    }
  }
  if (p_udf->lvd_lba && p_udf->i_part_start) {
    udf_fsd_t *p_fsd = (udf_fsd_t *) &data;

    driver_return_code_t ret =
      udf_read_sectors(p_udf, p_fsd, p_udf->i_part_start + p_udf->fsd_offset,
		       1);

    if (DRIVER_OP_SUCCESS == ret && !udf_checktag(&p_fsd->tag, TAGID_FSD)) {
      udf_file_entry_t *p_udf_fe = (udf_file_entry_t *) &data;
      const uint32_t parent_icb = uint32_from_le(p_fsd->root_icb.loc.lba);

      /* Check partition numbers match of last-read block?  */

      ret = udf_read_sectors(p_udf, p_udf_fe,
			     p_udf->i_part_start + parent_icb, 1);
      if (ret == DRIVER_OP_SUCCESS &&
	  !udf_checktag(&p_udf_fe->tag, TAGID_FILE_ENTRY)) {

	/* Check partition numbers match of last-read block? */

	/* We win! - Save root directory information. */
	return udf_new_dirent(p_udf_fe, p_udf, "/", true, false );
      }
    }
  }

  return NULL;
}

#define free_and_null(x) \
  CDIO_FREE_IF_NOT_NULL(x); \
  x=NULL

/*!
  Close UDF and free resources associated with p_udf.
*/
bool
udf_close (udf_t *p_udf)
{
  if (!p_udf) return true;
  if (p_udf->b_stream) {
    cdio_stdio_destroy(p_udf->stream);
  } else {
    cdio_destroy(p_udf->cdio);
  }

  /* Get rid of root directory if allocated. */

  free_and_null(p_udf);
  return true;
}

udf_dirent_t *
udf_opendir(const udf_dirent_t *p_udf_dirent)
{
  if (p_udf_dirent->b_dir && !p_udf_dirent->b_parent && p_udf_dirent->fid) {
    udf_t *p_udf = p_udf_dirent->p_udf;
    udf_file_entry_t udf_fe;

    driver_return_code_t i_ret =
      udf_read_sectors(p_udf, &udf_fe, p_udf->i_part_start
		       + p_udf_dirent->fid->icb.loc.lba, 1);

    if (DRIVER_OP_SUCCESS == i_ret
	&& !udf_checktag(&udf_fe.tag, TAGID_FILE_ENTRY)) {

      if (ICBTAG_FILE_TYPE_DIRECTORY == udf_fe.icb_tag.file_type) {
	udf_dirent_t *p_udf_dirent_new =
	  udf_new_dirent(&udf_fe, p_udf, p_udf_dirent->psz_name, true, true);
	return p_udf_dirent_new;
      }
    }
  }
  return NULL;
}

udf_dirent_t *
udf_readdir(udf_dirent_t *p_udf_dirent)
{
  udf_t *p_udf;
  uint8_t* p;

  if (p_udf_dirent->dir_left <= 0) {
    udf_dirent_free(p_udf_dirent);
    return NULL;
  }

  /* file position must be reset when accessing a new file */
  p_udf = p_udf_dirent->p_udf;
  p_udf->i_position = 0;

  if (p_udf_dirent->fid) {
    /* advance to next File Identifier Descriptor */
    /* FIXME: need to advance file entry (fe) as well.  */
    uint32_t ofs = 4 *
      ((sizeof(*(p_udf_dirent->fid)) + p_udf_dirent->fid->u.i_imp_use
	+ p_udf_dirent->fid->i_file_id + 3) / 4);

    p_udf_dirent->fid =
      (udf_fileid_desc_t *)((uint8_t *)p_udf_dirent->fid + ofs);
  }

  if (!p_udf_dirent->fid) {
    uint32_t i_sectors =
      (p_udf_dirent->i_loc_end - p_udf_dirent->i_loc + 1);
    uint32_t size = UDF_BLOCKSIZE * i_sectors;
    driver_return_code_t i_ret;

    if (!p_udf_dirent->sector)
      p_udf_dirent->sector = (uint8_t*) malloc(size);
    i_ret = udf_read_sectors(p_udf, p_udf_dirent->sector,
			     p_udf_dirent->i_part_start+p_udf_dirent->i_loc,
			     i_sectors);
    if (DRIVER_OP_SUCCESS == i_ret)
      p_udf_dirent->fid = (udf_fileid_desc_t *) p_udf_dirent->sector;
    else
      p_udf_dirent->fid = NULL;
  }

  if (p_udf_dirent->fid && !udf_checktag(&(p_udf_dirent->fid->tag), TAGID_FID))
    {
      uint32_t ofs =
	4 * ((sizeof(*p_udf_dirent->fid) + p_udf_dirent->fid->u.i_imp_use
	      + p_udf_dirent->fid->i_file_id + 3) / 4);

      p_udf_dirent->dir_left -= ofs;
      p_udf_dirent->b_dir =
	(p_udf_dirent->fid->file_characteristics & UDF_FILE_DIRECTORY) != 0;
      p_udf_dirent->b_parent =
	(p_udf_dirent->fid->file_characteristics & UDF_FILE_PARENT) != 0;

      {
	const unsigned int i_len = p_udf_dirent->fid->i_file_id;

	if (DRIVER_OP_SUCCESS != udf_read_sectors(p_udf, &p_udf_dirent->fe, p_udf->i_part_start
			 + uint32_from_le(p_udf_dirent->fid->icb.loc.lba), 1)) {
		udf_dirent_free(p_udf_dirent);
		return NULL;
	}

       free_and_null(p_udf_dirent->psz_name);
       p = (uint8_t*)p_udf_dirent->fid->u.imp_use.data + p_udf_dirent->fid->u.i_imp_use;
       p_udf_dirent->psz_name = unicode16_decode(p, i_len);
      }
      return p_udf_dirent;
    }
  udf_dirent_free(p_udf_dirent);
  return NULL;
}

/*!
  free free resources associated with p_udf_dirent.
*/
bool
udf_dirent_free(udf_dirent_t *p_udf_dirent)
{
  if (p_udf_dirent) {
    p_udf_dirent->fid = NULL;
    free_and_null(p_udf_dirent->psz_name);
    free_and_null(p_udf_dirent->sector);
    free_and_null(p_udf_dirent);
  }
  return true;
}
