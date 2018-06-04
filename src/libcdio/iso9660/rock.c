/*
  Copyright (C) 2005, 2008, 2010-2011, 2014, 2017 Rocky Bernstein
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

#define CDIO_MKDEV(ma,mi)	((ma)<<16 | (mi))

enum iso_rock_enums iso_rock_enums;
iso_rock_nm_flag_t iso_rock_nm_flag;
iso_rock_sl_flag_t iso_rock_sl_flag;
iso_rock_tf_flag_t iso_rock_tf_flag;

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
#define CHECK_SP(FAIL)	       			\
      if(rr->u.SP.magic[0] != 0xbe) FAIL;	\
      if(rr->u.SP.magic[1] != 0xef) FAIL;       \
      p_stat->rr.s_rock_offset = rr->u.SP.skip;
/* We define a series of macros because each function must do exactly the
   same thing in certain places.  We use the macros to ensure that everything
   is done correctly */

#define CONTINUE_DECLS \
  int cont_extent = 0, cont_offset = 0, cont_size = 0;   \
  void *buffer = NULL

#define CHECK_CE				 \
  { cont_extent = from_733(*rr->u.CE.extent);	 \
    cont_offset = from_733(*rr->u.CE.offset);	 \
    cont_size = from_733(*rr->u.CE.size);	 \
    (void)cont_extent; (void)cont_offset, (void)cont_size; }

#define SETUP_ROCK_RIDGE(DE,CHR,LEN)	      		      	\
  {								\
    LEN= sizeof(iso9660_dir_t) + DE->filename.len;		\
    if(LEN & 1) LEN++;						\
    CHR = ((unsigned char *) DE) + LEN;				\
    LEN = *((unsigned char *) DE) - LEN;			\
    if (0xff != p_stat->rr.s_rock_offset)			\
      {								\
	LEN -= p_stat->rr.s_rock_offset;		       	\
	CHR += p_stat->rr.s_rock_offset;		       	\
	if (LEN<0) LEN=0;					\
      }								\
  }

/* Copy a long or short time from the iso_rock_tf_t into
   the specified field of a iso_rock_statbuf_t.
   non-paramater variables are p_stat, rr, and cnt.
*/
#define add_time(FLAG, TIME_FIELD)				  \
  if (rr->u.TF.flags & FLAG) {					  \
    p_stat->rr.TIME_FIELD.b_used = true;			  \
    p_stat->rr.TIME_FIELD.b_longdate =				  \
      (0 != (rr->u.TF.flags & ISO_ROCK_TF_LONG_FORM));		  \
    if (p_stat->rr.TIME_FIELD.b_longdate) {			  \
      memcpy(&(p_stat->rr.TIME_FIELD.t.ltime),			  \
	     &(rr->u.TF.time_bytes[cnt]),			  \
	     sizeof(iso9660_ltime_t)); 				  \
      cnt += sizeof(iso9660_ltime_t);				  \
    } else {							  \
      memcpy(&(p_stat->rr.TIME_FIELD.t.dtime),			  \
	     &(rr->u.TF.time_bytes[cnt]),			  \
	     sizeof(iso9660_dtime_t)); 				  \
      cnt += sizeof(iso9660_dtime_t);				  \
    }								  \
  }

/*!
  Get
  @return length of name field; 0: not found, -1: to be ignored
*/
int
get_rock_ridge_filename(iso9660_dir_t * p_iso9660_dir,
			/*out*/ char * psz_name,
			/*in/out*/ iso9660_stat_t *p_stat)
{
  int len;
  unsigned char *chr;
  int symlink_len = 0;
  CONTINUE_DECLS;
  int i_namelen = 0;
  int truncate=0;

  if (!p_stat || nope == p_stat->rr.b3_rock) return 0;
  *psz_name = 0;

  SETUP_ROCK_RIDGE(p_iso9660_dir, chr, len);
  /*repeat:*/
  {
    iso_extension_record_t * rr;
    int sig;
    int rootflag;

    while (len > 1){ /* There may be one byte for padding somewhere */
      rr = (iso_extension_record_t *) chr;
      sig = *chr+(*(chr+1) << 8);

      /* We used to check for some vaid values of SIG, specifically
	 SP, CE, ER, RR, PX, PN, SL, NM, CL, PL, TF, and ZF.
	 However there are various extensions to this set. So we
	 skip checking now.
      */

      if (rr->len == 0) goto out; /* Something got screwed up here */
      chr += rr->len;
      len -= rr->len;

      switch(sig){
      case SIG('S','P'):
	CHECK_SP(goto out);
	break;
      case SIG('C','E'):
	{
	  iso711_t i_fname = from_711(p_iso9660_dir->filename.len);
	  if ('\0' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	    break;
	  if ('\1' == p_iso9660_dir->filename.str[1] && 1 == i_fname)
	    break;
	}
	CHECK_CE;
	break;
      case SIG('E','R'):
	p_stat->rr.b3_rock = yep;
	cdio_debug("ISO 9660 Extensions: ");
	{
	  int p;
	  for(p=0;p<rr->u.ER.len_id;p++) cdio_debug("%c",rr->u.ER.data[p]);
	}
	break;
      case SIG('N','M'):
	/* Alternate name */
	p_stat->rr.b3_rock = yep;
	if (truncate) break;
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
	p_stat->rr.b3_rock    = yep;
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
      case SIG('R','E'):
	free(buffer);
	return -1;
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
	  p_stat->rr.b3_rock = yep;
	  break;
	}
      default:
	break;
      }
    }
  }
  free(buffer);
  return i_namelen; /* If 0, this file did not have a NM field */
 out:
  free(buffer);
  return 0;
}

static int
parse_rock_ridge_stat_internal(iso9660_dir_t *p_iso9660_dir,
			       iso9660_stat_t *p_stat, int regard_xa)
{
  int len;
  unsigned char * chr;
  int symlink_len = 0;
  CONTINUE_DECLS;

  if (nope == p_stat->rr.b3_rock) return 0;

  SETUP_ROCK_RIDGE(p_iso9660_dir, chr, len);
  if (regard_xa)
    {
      chr+=14;
      len-=14;
      if (len<0) len=0;
    }

  /* repeat:*/
  {
    int sig;
    iso_extension_record_t * rr;
    int rootflag;

    while (len > 1){ /* There may be one byte for padding somewhere */
      rr = (iso_extension_record_t *) chr;
      if (rr->len == 0) goto out; /* Something got screwed up here */
      sig = from_721(*chr);
      chr += rr->len;
      len -= rr->len;

      switch(sig){
      case SIG('S','P'):
	CHECK_SP(goto out);
	break;
      case SIG('C','E'):
	CHECK_CE;
	break;
      case SIG('E','R'):
	p_stat->rr.b3_rock = yep;
	cdio_debug("ISO 9660 Extensions: ");
	{ int p;
	  for(p=0;p<rr->u.ER.len_id;p++) cdio_debug("%c",rr->u.ER.data[p]);
	}
	break;
      case SIG('P','X'):
	p_stat->rr.st_mode   = from_733(rr->u.PX.st_mode);
	p_stat->rr.st_nlinks = from_733(rr->u.PX.st_nlinks);
	p_stat->rr.st_uid    = from_733(rr->u.PX.st_uid);
	p_stat->rr.st_gid    = from_733(rr->u.PX.st_gid);
	break;
      case SIG('P','N'):
	/* Device major,minor number */
	{ int32_t high, low;
	  high = from_733(rr->u.PN.dev_high);
	  low = from_733(rr->u.PN.dev_low);
	  /*
	   * The Rock Ridge standard specifies that if sizeof(dev_t) <= 4,
	   * then the high field is unused, and the device number is completely
	   * stored in the low field.  Some writers may ignore this subtlety,
	   * and as a result we test to see if the entire device number is
	   * stored in the low field, and use that.
	   */
	  if((low & ~0xff) && high == 0) {
	    p_stat->rr.i_rdev = CDIO_MKDEV(low >> 8, low & 0xff);
	  } else {
	    p_stat->rr.i_rdev = CDIO_MKDEV(high, low);
	  }
	}
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
	  p_stat->rr.b3_rock = yep;
	  break;
	}
      case SIG('S','L'):
	{
	  /* Symbolic link */
	  uint8_t slen;
	  iso_rock_sl_part_t * p_sl;
	  iso_rock_sl_part_t * p_oldsl;
	  slen = rr->len - 5;
	  p_sl = &rr->u.SL.link;
	  p_stat->rr.i_symlink = symlink_len;
	  while (slen > 1){
	    rootflag = 0;
	    switch(p_sl->flags &~1){
	    case 0:
	      realloc_symlink(p_stat, p_sl->len);
	      if (p_sl->text && p_sl->len)
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
	      p_stat->rr.i_symlink++;
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
      case SIG('R','E'):
	cdio_warn("Attempt to read p_stat for relocated directory");
	goto out;
#ifdef FINISHED
      case SIG('C','L'):
	{
	  iso9660_stat_t * reloc;
	  ISOFS_I(p_stat)->i_first_extent = from_733(rr->u.CL.location);
	  reloc = isofs_iget(p_stat->rr.i_sb, p_stat->rr.i_first_extent, 0);
	  if (!reloc)
	    goto out;
	  p_stat->rr.st_mode   = reloc->st_mode;
	  p_stat->rr.st_nlinks = reloc->st_nlinks;
	  p_stat->rr.st_uid    = reloc->st_uid;
	  p_stat->rr.st_gid    = reloc->st_gid;
	  p_stat->rr.i_rdev    = reloc->i_rdev;
	  p_stat->rr.i_symlink = reloc->i_symlink;
	  p_stat->rr.i_blocks  = reloc->i_blocks;
	  p_stat->rr.i_atime   = reloc->i_atime;
	  p_stat->rr.i_ctime   = reloc->i_ctime;
	  p_stat->rr.i_mtime   = reloc->i_mtime;
	  iput(reloc);
	}
	break;
#endif
      default:
	break;
      }
    }
  }
 out:
  free(buffer);
  return 0;
}

int
parse_rock_ridge_stat(iso9660_dir_t *p_iso9660_dir,
		      /*out*/ iso9660_stat_t *p_stat)
{
  int result;

  if (!p_stat) return 0;

  result = parse_rock_ridge_stat_internal(p_iso9660_dir, p_stat, 0);
  /* if Rock-Ridge flag was reset and we didn't look for attributes
   * behind eventual XA attributes, have a look there */
  if (0xFF == p_stat->rr.s_rock_offset && nope != p_stat->rr.b3_rock) {
    result = parse_rock_ridge_stat_internal(p_iso9660_dir, p_stat, 14);
  }
  return result;
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

  result[11] = '\0';

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
