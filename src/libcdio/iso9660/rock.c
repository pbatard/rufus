/*
  Copyright (C) 2020, 2023 Pete Batard <pete@akeo.ie>
  Copyright (C) 2005, 2008, 2010-2011, 2014, 2017, 2022 Rocky Bernstein
  <rocky@gnu.org>

  Adapted from GNU/Linux fs/isofs/rock.c (C) 1992, 1993 Eric Youngdale

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
/* Rock Ridge Extensions to iso9660 */



#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#include <cdio/iso9660.h>
#include <cdio/logging.h>
#include <cdio/bytesex.h>
#include "filemode.h"
#include "cdio_private.h"

#define CDIO_MKDEV(ma,mi)	((ma)<<16 | (mi))

enum iso_rock_enums iso_rock_enums;
iso_rock_nm_flag_t iso_rock_nm_flag;
iso_rock_sl_flag_t iso_rock_sl_flag;
iso_rock_tf_flag_t iso_rock_tf_flag;

/* Used by get_rock_ridge_filename() */
extern iso9660_stat_t*
_iso9660_dd_find_lsn(void* p_image, lsn_t i_lsn);

/* Our own realloc routine tailored for the iso9660_stat_t symlink
   field.  I can't figure out how to make realloc() work without
   valgrind complaint.
*/
static bool
realloc_symlink(/*in/out*/ iso9660_stat_t *p_stat, uint8_t i_grow)
{
  if (!p_stat->rr.i_symlink) {
    const uint16_t i_max = 2*i_grow+1;
    p_stat->rr.psz_symlink = (char *) calloc(1, i_max);
    p_stat->rr.i_symlink_max = i_max;
    return (NULL != p_stat->rr.psz_symlink);
  } else {
    unsigned int i_needed = p_stat->rr.i_symlink + i_grow ;
    if ( i_needed <= p_stat->rr.i_symlink_max)
      return true;
    else {
      char * psz_newsymlink = (char *) calloc(1, 2*i_needed);
      if (!psz_newsymlink) return false;
      p_stat->rr.i_symlink_max = 2*i_needed;
      memcpy(psz_newsymlink, p_stat->rr.psz_symlink, p_stat->rr.i_symlink);
      free(p_stat->rr.psz_symlink);
      p_stat->rr.psz_symlink = psz_newsymlink;
      return true;
    }
  }
}

/* These functions are designed to read the system areas of a directory record
 * and extract relevant information.  There are different functions provided
 * depending upon what information we need at the time.  One function fills
 * out an inode structure, a second one extracts a filename, a third one
 * returns a symbolic link name, and a fourth one returns the extent number
 * for the file. */

#define SIG(A,B) ((A) | ((B) << 8)) /* isonum_721() */


/* This is a way of ensuring that we have something in the system
   use fields that is compatible with Rock Ridge */
#define CHECK_SP(FAIL)				\
      if (rr->u.SP.magic[0] != 0xbe) FAIL;	\
      if (rr->u.SP.magic[1] != 0xef) FAIL;	\
      p_stat->rr.s_rock_offset = rr->u.SP.skip;
/* We define a series of macros because each function must do exactly the
   same thing in certain places.  We use the macros to ensure that everything
   is done correctly */

#define CONTINUE_DECLS \
  uint32_t cont_extent = 0, cont_offset = 0, cont_size = 0;	\
  uint8_t *buffer = NULL, ce_count = 0

#define CHECK_CE(FAIL)				\
  { cont_extent = from_733(rr->u.CE.extent);	\
    cont_offset = from_733(rr->u.CE.offset);	\
    if (cont_offset >= ISO_BLOCKSIZE) FAIL;	\
    cont_size = from_733(rr->u.CE.size);	\
    if (cont_size >= ISO_BLOCKSIZE) FAIL;	\
  }

#define SETUP_ROCK_RIDGE(DE, CHR, LEN)				\
  {								\
    LEN= sizeof(iso9660_dir_t) + DE->filename.len;		\
    if (LEN & 1) LEN++;						\
    CHR = ((unsigned char *) DE) + LEN;				\
    LEN = *((unsigned char *) DE) - LEN;			\
    if (0xff != p_stat->rr.s_rock_offset)			\
      {								\
	LEN -= p_stat->rr.s_rock_offset;			\
	CHR += p_stat->rr.s_rock_offset;			\
	if (LEN<0) LEN=0;					\
      }								\
  }

/* Copy a long or short time from the iso_rock_tf_t into
   the specified field of a iso_rock_statbuf_t.
   non-paramater variables are p_stat, rr, and cnt.
*/
#define add_time(FLAG, TIME_FIELD)				\
  if (rr->u.TF.flags & FLAG) {					\
    p_stat->rr.TIME_FIELD.b_used = true;			\
    p_stat->rr.TIME_FIELD.b_longdate =				\
      (0 != (rr->u.TF.flags & ISO_ROCK_TF_LONG_FORM));		\
    if (p_stat->rr.TIME_FIELD.b_longdate) {			\
      memcpy(&(p_stat->rr.TIME_FIELD.t.ltime),			\
	     &(rr->u.TF.time_bytes[cnt]),			\
	     sizeof(iso9660_ltime_t));				\
      cnt += sizeof(iso9660_ltime_t);				\
    } else {							\
      memcpy(&(p_stat->rr.TIME_FIELD.t.dtime),			\
	     &(rr->u.TF.time_bytes[cnt]),			\
	     sizeof(iso9660_dtime_t));				\
      cnt += sizeof(iso9660_dtime_t);				\
    }								\
  }

/* Indicates if we should process deep directory entries */
static inline bool
is_rr_dd_enabled(void * p_image) {
  cdio_header_t* p_header = (cdio_header_t*)p_image;
  if (!p_header)
    return false;
  return !(p_header->u_flags & CDIO_HEADER_FLAGS_DISABLE_RR_DD);
}

/*!
  Get
  @return length of name field; 0: not found, -1: to be ignored
*/
int
get_rock_ridge_filename(iso9660_dir_t * p_iso9660_dir,
			/*in*/ void * p_image,
			/*out*/ char * psz_name,
			/*in/out*/ iso9660_stat_t *p_stat)
{
  int len;
  unsigned char *chr;
  int symlink_len = 0;
  CONTINUE_DECLS;
  int i_namelen = 0;
  int truncate=0;

  if (!p_stat || nope == p_stat->rr.b3_rock)
    return 0;
  *psz_name = 0;

  SETUP_ROCK_RIDGE(p_iso9660_dir, chr, len);
repeat:
  {
    iso_extension_record_t * rr;
    int sig;
    int rootflag;

    while (len > 1){ /* There may be one byte for padding somewhere */
      rr = (iso_extension_record_t *) chr;
      sig = *chr+(*(chr+1) << 8);

      /* We used to check for some valid values of SIG, specifically
	 SP, CE, ER, RR, PX, PN, SL, NM, CL, PL, TF, and ZF.
	 However there are various extensions to this set. So we
	 skip checking now.
      */

      if (rr->len == 0)
	goto out; /* Something got screwed up here */
      chr += rr->len;
      len -= rr->len;

      switch(sig) {
      case SIG('S','P'):
	CHECK_SP({cdio_warn("Invalid Rock Ridge SP field"); goto out;});
	p_stat->rr.u_su_fields |= ISO_ROCK_SUF_SP;
	break;
      case SIG('C','E'):
	{
	  iso711_t i_fname = from_711(p_iso9660_dir->filename.len);
	  if ('\0' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	    break;
	  if ('\1' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	    break;
	}
	CHECK_CE({cdio_warn("Invalid Rock Ridge CE field"); goto out;});
	p_stat->rr.u_su_fields |= ISO_ROCK_SUF_CE;
	/* Though no mastering utility in its right mind would produce anything
	   like this, the specs make it theoretically possible to have more RR
	   extensions after a CE, so we delay the CE block processing for later.
	*/
	break;
      case SIG('E','R'):
	cdio_debug("ISO 9660 Extensions: ");
	{
	  int p;
	  for (p=0; p < rr->u.ER.len_id; p++)
	    cdio_debug("%c", rr->u.ER.data[p]);
	}
	break;
      case SIG('N','M'):
	/* Alternate name */
	p_stat->rr.u_su_fields |= ISO_ROCK_SUF_NM;
	if (truncate)
	  break;
	if (rr->u.NM.flags & ISO_ROCK_NM_PARENT) {
	  i_namelen = sizeof("..");
	  strncat(psz_name, "..", i_namelen);
	  break;
	} else if (rr->u.NM.flags & ISO_ROCK_NM_CURRENT) {
	  i_namelen = sizeof(".");
	  strncat(psz_name, ".", i_namelen);
	  break;
	}

	if (rr->u.NM.flags & ~1) {
	  cdio_info("Unsupported NM flag settings (%d)",rr->u.NM.flags);
	  break;
	}
	if((strlen(psz_name) + rr->len - 5) >= 254) {
	  truncate = 1;
	  break;
	}
	strncat(psz_name, rr->u.NM.name, rr->len - 5);
	i_namelen += rr->len - 5;
	break;
      case SIG('P','X'):
	/* POSIX file attributes */
	p_stat->rr.st_mode   = from_733(rr->u.PX.st_mode);
	p_stat->rr.st_nlinks = from_733(rr->u.PX.st_nlinks);
	p_stat->rr.st_uid    = from_733(rr->u.PX.st_uid);
	p_stat->rr.st_gid    = from_733(rr->u.PX.st_gid);
	p_stat->rr.u_su_fields |= ISO_ROCK_SUF_PX;
	break;
      case SIG('S','L'):
	{
	  /* Symbolic link */
	  uint8_t slen;
	  iso_rock_sl_part_t * p_sl;
	  iso_rock_sl_part_t * p_oldsl;
	  slen = rr->len - 5;
	  p_sl = &rr->u.SL.link;
	  p_stat->rr.i_symlink = symlink_len;
	  p_stat->rr.u_su_fields |= ISO_ROCK_SUF_SL;
	  while (slen > 1){
	    rootflag = 0;
	    switch(p_sl->flags &~1){
	    case 0:
	      realloc_symlink(p_stat, p_sl->len);
	      memcpy(&(p_stat->rr.psz_symlink[p_stat->rr.i_symlink]),
		     p_sl->text, p_sl->len);
	      p_stat->rr.i_symlink += p_sl->len;
	      break;
	    case 4:
	      realloc_symlink(p_stat, 1);
	      p_stat->rr.psz_symlink[p_stat->rr.i_symlink++] = '.';
	      /* continue into next case. */
	    case 2:
	      realloc_symlink(p_stat, 1);
	      p_stat->rr.psz_symlink[p_stat->rr.i_symlink++] = '.';
	      break;
	    case 8:
	      rootflag = 1;
	      realloc_symlink(p_stat, 1);
	      p_stat->rr.psz_symlink[p_stat->rr.i_symlink++] = '/';
	      break;
	    default:
	      cdio_warn("Symlink component flag not implemented");
	    }
	    slen -= p_sl->len + 2;
	    p_oldsl = p_sl;
	    p_sl = (iso_rock_sl_part_t *) (((char *) p_sl) + p_sl->len + 2);

	    if (slen < 2) {
	      if (((rr->u.SL.flags & 1) != 0) && ((p_oldsl->flags & 1) == 0))
		p_stat->rr.i_symlink += 1;
	      break;
	    }

	    /*
	     * If this component record isn't continued, then append a '/'.
	     */
	    if (!rootflag && (p_oldsl->flags & 1) == 0) {
	      realloc_symlink(p_stat, 1);
	      p_stat->rr.psz_symlink[p_stat->rr.i_symlink++] = '/';
	    }
	  }
	}
	symlink_len = p_stat->rr.i_symlink;
	realloc_symlink(p_stat, 1);
	p_stat->rr.psz_symlink[symlink_len]='\0';
	break;
      case SIG('T','F'):
	/* Time stamp(s) for a file */
	{
	  int cnt = 0;
	  add_time(ISO_ROCK_TF_CREATE,     create);
	  add_time(ISO_ROCK_TF_MODIFY,     modify);
	  add_time(ISO_ROCK_TF_ACCESS,     access);
	  add_time(ISO_ROCK_TF_ATTRIBUTES, attributes);
	  add_time(ISO_ROCK_TF_BACKUP,     backup);
	  add_time(ISO_ROCK_TF_EXPIRATION, expiration);
	  add_time(ISO_ROCK_TF_EFFECTIVE,  effective);
	  p_stat->rr.u_su_fields |= ISO_ROCK_SUF_TF;
	  break;
	}
      case SIG('C','L'):
	/* Child Link for a deep directory */
	if (!is_rr_dd_enabled(p_image))
	  break;
	{
	  iso9660_stat_t* target = NULL;
	  p_stat->rr.u_su_fields |= ISO_ROCK_SUF_CL;
	  target = _iso9660_dd_find_lsn(p_image, from_733(rr->u.PL.location));
	  if (!target) {
	    cdio_warn("Could not get Rock Ridge deep directory child");
	    break;
	  }
	  memcpy(p_stat, target, sizeof(iso9660_stat_t));
	  /* Prevent the symlink from being freed on the duplicated struct */
	  target->rr.psz_symlink = NULL;
	  iso9660_stat_free(target);
	}
	break;
      case SIG('P','L'):
	/* Parent link of a deep directory */
	if (is_rr_dd_enabled(p_image))
	  p_stat->rr.u_su_fields |= ISO_ROCK_SUF_PL;
	break;
      case SIG('R','E'):
	/* Relocated entry for a deep directory */
	if (is_rr_dd_enabled(p_image))
	  p_stat->rr.u_su_fields |= ISO_ROCK_SUF_RE;
	break;
      case SIG('S','F'):
	/* Sparse File */
	p_stat->rr.u_su_fields |= ISO_ROCK_SUF_SF;
	cdio_warn("Rock Ridge Sparse File detected");
	break;
      default:
	break;
      }
    }
  }
  /* Process delayed CE blocks */
  if (cont_size != 0) {
      free(buffer);
      buffer = calloc(1, ISO_BLOCKSIZE);
      if (!buffer)
	  goto out;
      if (iso9660_iso_seek_read(p_image, buffer, cont_extent, 1) != ISO_BLOCKSIZE)
	  goto out;
      chr = &buffer[cont_offset];
      len = cont_size;
      cont_size = 0;
      /* Someone abusing the specs may also be creating looping CEs */
      if (ce_count++ < 64)
	  goto repeat;
      else
	  cdio_warn("More than 64 consecutive Rock Ridge CEs detected");
  }
  if (p_stat->rr.u_su_fields & ISO_ROCK_SUF_FORMAL)
    p_stat->rr.b3_rock = yep;
  free(buffer);
  return i_namelen; /* If 0, this file did not have a NM field */
out:
  free(buffer);
  return 0;
}

#define BUF_COUNT 16
#define BUF_SIZE sizeof("drwxrwxrwx")

/* Return a pointer to a internal free buffer */
static char *
_getbuf (void)
{
  static char _buf[BUF_COUNT][BUF_SIZE];
  static int _i = -1;

  _i++;
  _i %= BUF_COUNT;

  memset (_buf[_i], 0, BUF_SIZE);

  return _buf[_i];
}

/*!
  Returns a string which interpreting the POSIX mode st_mode.
  For example:
  \verbatim
  drwxrws---
  -rw-rw-r--
  lrwxrwxrwx
  \endverbatim

  A description of the characters in the string follows
  The 1st character is either "b" for a block device,
  "c" for a character device, "d" if the entry is a directory, "l" for
  a symbolic link, "p" for a pipe or FIFO, "s" for a "socket",
  or "-" if none of the these.

  The 2nd to 4th characters refer to permissions for a user while the
  the 5th to 7th characters refer to permissions for a group while, and
  the 8th to 10h characters refer to permissions for everyone.

  In each of these triplets the first character (2, 5, 8) is "r" if
  the entry is allowed to be read.

  The second character of a triplet (3, 6, 9) is "w" if the entry is
  allowed to be written.

  The third character of a triplet (4, 7, 10) is "x" if the entry is
  executable but not user (for character 4) or group (for characters
  6) settable and "s" if the item has the corresponding user/group set.

  For a directory having an executable property on ("x" or "s") means
  the directory is allowed to be listed or "searched". If the execute
  property is not allowed for a group or user but the corresponding
  group/user is set "S" indicates this. If none of these properties
  holds the "-" indicates this.
*/
const char *
iso9660_get_rock_attr_str(posix_mode_t st_mode)
{
  char *result = _getbuf();

  if (S_ISBLK(st_mode))
    result[ 0] = 'b';
  else if (S_ISDIR(st_mode))
    result[ 0] = 'd';
  else if (S_ISCHR(st_mode))
    result[ 0] = 'c';
  else if (S_ISLNK(st_mode))
    result[ 0] = 'l';
  else if (S_ISFIFO(st_mode))
    result[ 0] = 'p';
  else if (S_ISSOCK(st_mode))
    result[ 0] = 's';
  /* May eventually fill in others.. */
  else
    result[ 0] = '-';

  result[ 1] = (st_mode & ISO_ROCK_IRUSR) ? 'r' : '-';
  result[ 2] = (st_mode & ISO_ROCK_IWUSR) ? 'w' : '-';

  if (st_mode & ISO_ROCK_ISUID)
    result[ 3] = (st_mode & ISO_ROCK_IXUSR) ? 's' : 'S';
  else
    result[ 3] = (st_mode & ISO_ROCK_IXUSR) ? 'x' : '-';

  result[ 4] = (st_mode & ISO_ROCK_IRGRP) ? 'r' : '-';
  result[ 5] = (st_mode & ISO_ROCK_IWGRP) ? 'w' : '-';

  if (st_mode & ISO_ROCK_ISGID)
    result[ 6] = (st_mode & ISO_ROCK_IXGRP) ? 's' : 'S';
  else
    result[ 6] = (st_mode & ISO_ROCK_IXGRP) ? 'x' : '-';

  result[ 7] = (st_mode & ISO_ROCK_IROTH) ? 'r' : '-';
  result[ 8] = (st_mode & ISO_ROCK_IWOTH) ? 'w' : '-';
  result[ 9] = (st_mode & ISO_ROCK_IXOTH) ? 'x' : '-';

  result[10] = '\0';

  return result;
}

/*!
  Returns POSIX mode bitstring for a given file.
*/
mode_t
iso9660_get_posix_filemode_from_rock(const iso_rock_statbuf_t *rr)
{
  return (mode_t) rr->st_mode;
}
