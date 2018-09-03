/*
  Copyright (C) 2003-2009, 2013-2014, 2016-2017 Rocky Bernstein
  <rocky@gnu.org>
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

/*! String inside frame which identifies an ISO 9660 filesystem. This
    string is the "id" field of an iso9660_pvd_t or an iso9660_svd_t.
    Note should come *before* #include <cdio/iso9660.h> which does
    a #define of this name.
*/
const char ISO_STANDARD_ID[] = {'C', 'D', '0', '0', '1'};

/* Private headers */
#include "iso9660_private.h"
#include "cdio_assert.h"

/* Public headers */
#include <cdio/bytesex.h>
#include <cdio/iso9660.h>
#include <cdio/util.h>

#include <time.h>
#include <ctype.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STDIO_H
# include <stdio.h>
#endif
#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifndef HAVE_SETENV
static int
setenv(const char *envname, const char *envval, int overwrite)
{
  return -1;
}
#endif

#ifndef HAVE_UNSETENV
static int
unsetenv(const char *envname)
{
  return -1;
}
#endif

#ifndef HAVE_TIMEGM
static time_t
timegm(struct tm *tm)
{
  time_t ret;
  char *tz;

  tz = getenv("TZ");
  setenv("TZ", "UTC", 1);
  tzset();
  ret = mktime(tm);
  if (tz)
    setenv("TZ", tz, 1);
  else
    unsetenv("TZ");
  tzset();
  return ret;
}
#endif

#ifndef HAVE_GMTIME_R
static struct tm *
gmtime_r(const time_t *timer, struct tm *result)
{
    struct tm *tmp = gmtime(timer);

    if (tmp) {
        *result = *tmp;
        return result;
    }
    return tmp;
}
#endif

#ifndef HAVE_LOCALTIME_R
static struct tm *
localtime_r(const time_t *timer, struct tm *result)
{
    struct tm *tmp = localtime(timer);

    if (tmp) {
        *result = *tmp;
        return result;
    }
    return tmp;
}
#endif

/* Variables to hold debugger-helping enumerations */
enum iso_enum1_s     iso_enums1;
enum iso_flag_enum_s iso_flag_enums;
enum iso_vd_enum_s   iso_vd_enums;
enum iso_extension_enum_s iso_extension_enums;

/* some parameters... */
#define SYSTEM_ID         "CD-RTOS CD-BRIDGE"
#define VOLUME_SET_ID     ""

/*!
   Change trailing blanks in str to nulls.  Str has a maximum size of
   n characters.
*/
static char *
strip_trail (const char str[], size_t n)
{
  static char buf[1025];
  int j;

  cdio_assert (n < 1024);

  strncpy (buf, str, n);
  buf[n] = '\0';

  for (j = strlen (buf) - 1; j >= 0; j--)
    {
      if (buf[j] != ' ')
        break;

      buf[j] = '\0';
    }

  return buf;
}

static void
pathtable_get_size_and_entries(const void *pt, unsigned int *size,
                               unsigned int *entries);

/*!
  Get time structure from structure in an ISO 9660 directory index
  record. Even though tm_wday and tm_yday fields are not explicitly in
  idr_date, the are calculated from the other fields.

  If tm is to reflect the localtime set b_localtime true, otherwise
  tm will reported in GMT.
*/
bool
iso9660_get_dtime (const iso9660_dtime_t *idr_date, bool b_localtime,
                   /*out*/ struct tm *p_tm)
{
  if (!idr_date) return false;

  /*
     Section 9.1.5 of ECMA 119 says:
     If all seven numbers are zero, it shall mean that the date and
     time are not specified.

     HACK: However we've seen it happen that everything except gmtoff
     is zero and the expected date is the beginning of the epoch. So
     we accept 6 numbers being zero. I'm also not sure if using the
     beginning of the Epoch is also the right thing to do either.
  */

  if ( 0 == idr_date->dt_year   && 0 == idr_date->dt_month &&
       0 == idr_date->dt_day    && 0 == idr_date->dt_hour  &&
       0 == idr_date->dt_minute && 0 == idr_date->dt_second ) {
    time_t t = 0;
    struct tm temp_tm;
    localtime_r(&t, &temp_tm);

    memcpy(p_tm, &temp_tm, sizeof(struct tm));
    return true;
  }

  memset(p_tm, 0, sizeof(struct tm));

  p_tm->tm_year   = idr_date->dt_year;
  p_tm->tm_mon    = idr_date->dt_month - 1;
  p_tm->tm_mday   = idr_date->dt_day;
  p_tm->tm_hour   = idr_date->dt_hour;
  p_tm->tm_min    = idr_date->dt_minute;
  p_tm->tm_sec    = idr_date->dt_second - idr_date->dt_gmtoff * (15 * 60);
  p_tm->tm_isdst  = -1; /* information not available */

#ifdef HAVE_STRUCT_TM_TM_ZONE
  /* Initialize everything */
  p_tm->tm_zone   = 0;
#endif

  /* Recompute tm_wday and tm_yday via mktime. mktime will also renormalize
     date values to account for the timezone offset. */
  {
    time_t t = 0;
    struct tm temp_tm;

    t = timegm(p_tm);

    if (b_localtime)
      localtime_r(&t, &temp_tm);
    else
      gmtime_r(&t, &temp_tm);

    memcpy(p_tm, &temp_tm, sizeof(struct tm));
  }


  return true;
}

/*
   A note regarding the strange strtol() testing below as pointed out SMS.
   From man strtol:

     If an underflow occurs, strtol() returns LONG_MIN. If an overflow
     occurs, strtol() returns LONG_MAX.  In both cases, errno is set to
     ERANGE.

   This implies that one should only look at errno if the value is
   LONG_MIN or LONG_MAX.
*/

#define set_ltime_field(TM_FIELD, LT_FIELD, ADD_CONSTANT)               \
  {                                                                     \
    char num[10]; long tmp;                                             \
    memcpy(num, p_ldate->LT_FIELD, sizeof(p_ldate->LT_FIELD));          \
    num[sizeof(p_ldate->LT_FIELD)] = '\0';                              \
    errno = 0;                                                          \
    tmp = strtol(num,                                                   \
                 (char **)NULL, 10);                                    \
    if ( tmp < INT_MIN || tmp > INT_MAX ||                              \
         ((unsigned long)tmp + ADD_CONSTANT) > INT_MAX ||               \
         (tmp + ADD_CONSTANT) < INT_MIN )                               \
      return false;                                                     \
    p_tm->TM_FIELD = tmp + ADD_CONSTANT;                                \
  }

/*!
  Get "long" time in format used in ISO 9660 primary volume descriptor
  from a Unix time structure.
*/
bool
iso9660_get_ltime (const iso9660_ltime_t *p_ldate,
                   /*out*/ struct tm *p_tm)
{
  if (!p_tm) return false;
  memset(p_tm, 0, sizeof(struct tm));
  set_ltime_field(tm_year, lt_year,  -1900);
  set_ltime_field(tm_mon,  lt_month, -1);
  set_ltime_field(tm_mday, lt_day,    0);
  set_ltime_field(tm_hour, lt_hour,   0);
  set_ltime_field(tm_min,  lt_minute, 0);
  set_ltime_field(tm_sec,  lt_second, 0);
  p_tm->tm_isdst= -1; /* information not available */
#ifndef HAVE_TM_GMTOFF
  p_tm->tm_sec += p_ldate->lt_gmtoff * (15 * 60);
#endif
#ifdef HAVE_STRUCT_TM_TM_ZONE
  /* Initialize everything */
  p_tm->tm_zone = 0;
#endif

  /* Recompute tm_wday and tm_yday via mktime. mktime will also renormalize
     date values to account for the timezone offset. */
  {
    time_t t;
    struct tm temp_tm;

    t = mktime(p_tm);

    localtime_r(&t, &temp_tm);

    memcpy(p_tm, &temp_tm, sizeof(struct tm));
  }
  p_tm->tm_isdst= -1; /* information not available */
#ifdef HAVE_TM_GMTOFF
  p_tm->tm_gmtoff = -p_ldate->lt_gmtoff * (15 * 60);
#endif
  return true;
}

/*!
  Set time in format used in ISO 9660 directory index record
  from a Unix time structure. timezone is given as an offset
  correction in minutes.
*/
void
iso9660_set_dtime_with_timezone (const struct tm *p_tm,
                                 int time_zone,
                                 /*out*/ iso9660_dtime_t *p_idr_date)
{
  memset (p_idr_date, 0, 7);

  if (!p_tm) return;

  p_idr_date->dt_year   = p_tm->tm_year;
  p_idr_date->dt_month  = p_tm->tm_mon + 1;
  p_idr_date->dt_day    = p_tm->tm_mday;
  p_idr_date->dt_hour   = p_tm->tm_hour;
  p_idr_date->dt_minute = p_tm->tm_min;
  p_idr_date->dt_second = p_tm->tm_sec;

  /* The ISO 9660 timezone is in the range -48..+52 and each unit
     represents a 15-minute interval. */
  p_idr_date->dt_gmtoff = time_zone / 15;

  if (p_idr_date->dt_gmtoff < -48 ) {

    cdio_warn ("Converted ISO 9660 timezone %d is less than -48. Adjusted",
               p_idr_date->dt_gmtoff);
    p_idr_date->dt_gmtoff = -48;
  } else if (p_idr_date->dt_gmtoff > 52) {
    cdio_warn ("Converted ISO 9660 timezone %d is over 52. Adjusted",
               p_idr_date->dt_gmtoff);
    p_idr_date->dt_gmtoff = 52;
  }
}

/*!
  Set time in format used in ISO 9660 directory index record
  from a Unix time structure. */
void
iso9660_set_dtime(const struct tm *p_tm, /*out*/ iso9660_dtime_t *p_idr_date)
{
  int time_zone = 0;
  if (p_tm) {
#ifdef HAVE_TM_GMTOFF
    /* Convert seconds to minutes */
    time_zone = p_tm->tm_gmtoff / 60;
#else
    time_zone = (p_tm->tm_isdst > 0) ? -60 : 0;
#endif
  }
  iso9660_set_dtime_with_timezone (p_tm, time_zone, p_idr_date);
}

/*!
  Set "long" time in format used in ISO 9660 primary volume descriptor
  from a Unix time structure. timezone is given as an offset
  correction in minutes.
*/
void
iso9660_set_ltime_with_timezone(const struct tm *p_tm,
                                int time_zone,
                                /*out*/ iso9660_ltime_t *pvd_date)
{
  char *_pvd_date = (char *) pvd_date;

  memset (_pvd_date, (int) '0', 16);
  pvd_date->lt_gmtoff = (iso712_t) 0; /* Start out with time zone GMT. */

  if (!p_tm) return;

  snprintf(_pvd_date, 17,
           "%4.4d%2.2d%2.2d" "%2.2d%2.2d%2.2d" "%2.2d",
           p_tm->tm_year + 1900, p_tm->tm_mon + 1, p_tm->tm_mday,
           p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec,
           0 /* 1/100 secs */ );

  /* Set time zone in 15-minute interval encoding. */
  pvd_date->lt_gmtoff -= (time_zone / 15);
  if (pvd_date->lt_gmtoff < -48 ) {

    cdio_warn ("Converted ISO 9660 timezone %d is less than -48. Adjusted",
               (int) pvd_date->lt_gmtoff);
    pvd_date->lt_gmtoff = -48;
  } else if (pvd_date->lt_gmtoff > 52) {
    cdio_warn ("Converted ISO 9660 timezone %d is over 52. Adjusted",
               (int) pvd_date->lt_gmtoff);
    pvd_date->lt_gmtoff = 52;
  }
}

/*!
  Set "long" time in format used in ISO 9660 primary volume descriptor
  from a Unix time structure. */
void
iso9660_set_ltime(const struct tm *p_tm, /*out*/ iso9660_ltime_t *pvd_date)
{
  int time_zone = 0;
  if (p_tm) {
#ifdef HAVE_TM_GMTOFF
    /* Set time zone in 15-minute interval encoding. */
    time_zone = p_tm->tm_gmtoff / 60;
#else
    time_zone = (p_tm->tm_isdst > 0) ? -60 : 0;
#endif
  }
  iso9660_set_ltime_with_timezone (p_tm, time_zone, pvd_date);
}

/*!
   Convert an ISO-9660 file name which is in the format usually stored
   in a ISO 9660 directory entry into what's usually listed as the
   file name in a listing.  Lowercase name, and remove trailing ;1's
   or .;1's and turn the other ;'s into version numbers.

   @param psz_oldname the ISO-9660 filename to be translated.
   @param psz_newname returned string. The caller allocates this and
   it should be at least the size of psz_oldname.
   @return length of the translated string is returned. It will be no greater
   than the length of psz_oldname.
*/
int
iso9660_name_translate(const char *psz_oldname, char *psz_newname)
{
  return iso9660_name_translate_ext(psz_oldname, psz_newname, 0);
}

/*!
   Convert an ISO-9660 file name which is in the format usually stored
   in a ISO 9660 directory entry into what's usually listed as the
   file name in a listing.  Lowercase name if no Joliet Extension
   interpretation. Remove trailing ;1's or .;1's and turn the other
   ;'s into version numbers.

   @param psz_oldname the ISO-9660 filename to be translated.
   @param psz_newname returned string. The caller allocates this and
   it should be at least the size of psz_oldname.
   @param u_joliet_level 0 if not using Joliet Extension. Otherwise the
   Joliet level.
   @return length of the translated string is returned. It will be no greater
   than the length of psz_oldname.
*/
int
iso9660_name_translate_ext(const char *psz_oldname, char *psz_newname,
                           uint8_t u_joliet_level)
{
  int len = strlen(psz_oldname);
  int i;

  if (0 == len) return 0;
  for (i = 0; i < len; i++) {
    unsigned char c = psz_oldname[i];
    if (!c)
      break;

    /* Lower case, unless we have Joliet extensions.  */
    if (!u_joliet_level && isupper(c)) c = tolower(c);

    /* Drop trailing '.;1' (ISO 9660:1988 7.5.1 requires period) */
    if (c == '.' && i == len - 3
        && psz_oldname[i + 1] == ';' && psz_oldname[i + 2] == '1')
      break;

    /* Drop trailing ';1' */
    if (c == ';' && i == len - 2 && psz_oldname[i + 1] == '1')
      break;

    /* Convert remaining ';' to '.' */
    if (c == ';')
      c = '.';

    psz_newname[i] = c;
  }
  psz_newname[i] = '\0';
  return i;
}

/*!
  Pad string src with spaces to size len and copy this to dst. If
  len is less than the length of src, dst will be truncated to the
  first len characters of src.

  src can also be scanned to see if it contains only ACHARs, DCHARs,
  7-bit ASCII chars depending on the enumeration _check.

  In addition to getting changed, dst is the return value.
  Note: this string might not be NULL terminated.
 */
char *
iso9660_strncpy_pad(char dst[], const char src[], size_t len,
                    enum strncpy_pad_check _check)
{
  size_t rlen;

  cdio_assert (dst != NULL);
  cdio_assert (src != NULL);
  cdio_assert (len > 0);

  switch (_check)
    {
      int idx;
    case ISO9660_NOCHECK:
      break;

    case ISO9660_7BIT:
      for (idx = 0; src[idx]; idx++)
        if ((int8_t) src[idx] < 0)
          {
            cdio_warn ("string '%s' fails 7bit constraint (pos = %d)",
                      src, idx);
            break;
          }
      break;

    case ISO9660_ACHARS:
      for (idx = 0; src[idx]; idx++)
        if (!iso9660_is_achar (src[idx]))
          {
            cdio_warn ("string '%s' fails a-character constraint (pos = %d)",
                      src, idx);
            break;
          }
      break;

    case ISO9660_DCHARS:
      for (idx = 0; src[idx]; idx++)
        if (!iso9660_is_dchar (src[idx]))
          {
            cdio_warn ("string '%s' fails d-character constraint (pos = %d)",
                      src, idx);
            break;
          }
      break;

    default:
      cdio_assert_not_reached ();
      break;
    }

  rlen = strlen (src);

  if (rlen > len)
    cdio_warn ("string '%s' is getting truncated to %d characters",
              src, (unsigned int) len);

  strncpy (dst, src, len);
  if (rlen < len)
    memset(dst+rlen, ' ', len-rlen);
  return dst;
}

/*!
   Return true if c is a DCHAR - a valid ISO-9660 level 1 character.
   These are the ASCSII capital letters A-Z, the digits 0-9 and an
   underscore.
*/
bool
iso9660_is_dchar (int c)
{
  if (!IN (c, 0x30, 0x5f)
      || IN (c, 0x3a, 0x40)
      || IN (c, 0x5b, 0x5e))
    return false;

  return true;
}


/*!
   Return true if c is an ACHAR -
   These are the DCHAR's plus some ASCII symbols including the space
   symbol.
*/
bool
iso9660_is_achar (int c)
{
  if (!IN (c, 0x20, 0x5f)
      || IN (c, 0x23, 0x24)
      || c == 0x40
      || IN (c, 0x5b, 0x5e))
    return false;

  return true;
}

void
iso9660_set_evd(void *pd)
{
  iso_volume_descriptor_t ied;

  cdio_assert (sizeof(iso_volume_descriptor_t) == ISO_BLOCKSIZE);

  cdio_assert (pd != NULL);

  memset(&ied, 0, sizeof(ied));

  ied.type = to_711(ISO_VD_END);
  iso9660_strncpy_pad (ied.id, ISO_STANDARD_ID, sizeof(ied.id),
                       ISO9660_DCHARS);
  ied.version = to_711(ISO_VERSION);

  memcpy(pd, &ied, sizeof(ied));
}

void
iso9660_set_pvd(void *pd,
                const char volume_id[],
                const char publisher_id[],
                const char preparer_id[],
                const char application_id[],
                uint32_t iso_size,
                const void *root_dir,
                uint32_t path_table_l_extent,
                uint32_t path_table_m_extent,
                uint32_t path_table_size,
                const time_t *pvd_time
                )
{
  iso9660_pvd_t ipd;
  struct tm temp_tm;

  cdio_assert (sizeof(iso9660_pvd_t) == ISO_BLOCKSIZE);

  cdio_assert (pd != NULL);
  cdio_assert (volume_id != NULL);
  cdio_assert (application_id != NULL);

  memset(&ipd,0,sizeof(ipd)); /* paranoia? */

  /* magic stuff ... thatis CD XA marker... */
  strncpy(((char*)&ipd)+ISO_XA_MARKER_OFFSET, ISO_XA_MARKER_STRING,8);

  ipd.type = to_711(ISO_VD_PRIMARY);
  iso9660_strncpy_pad (ipd.id, ISO_STANDARD_ID, 5, ISO9660_DCHARS);
  ipd.version = to_711(ISO_VERSION);

  iso9660_strncpy_pad (ipd.system_id, SYSTEM_ID, 32, ISO9660_ACHARS);
  iso9660_strncpy_pad (ipd.volume_id, volume_id, 32, ISO9660_DCHARS);

  ipd.volume_space_size = to_733(iso_size);

  ipd.volume_set_size = to_723(1);
  ipd.volume_sequence_number = to_723(1);
  ipd.logical_block_size = to_723(ISO_BLOCKSIZE);

  ipd.path_table_size = to_733(path_table_size);
  ipd.type_l_path_table = to_731(path_table_l_extent);
  ipd.type_m_path_table = to_732(path_table_m_extent);

  /* root_directory_record doesn't contain the 1-byte filename,
     so we add one for that. */
  cdio_assert (sizeof(ipd.root_directory_record) == 33);
  memcpy(&(ipd.root_directory_record), root_dir,
         sizeof(ipd.root_directory_record));
  ipd.root_directory_filename='\0';
  ipd.root_directory_record.length = sizeof(ipd.root_directory_record)+1;
  iso9660_strncpy_pad (ipd.volume_set_id, VOLUME_SET_ID,
                       ISO_MAX_VOLUMESET_ID, ISO9660_DCHARS);

  iso9660_strncpy_pad (ipd.publisher_id, publisher_id, ISO_MAX_PUBLISHER_ID,
                       ISO9660_ACHARS);
  iso9660_strncpy_pad (ipd.preparer_id, preparer_id, ISO_MAX_PREPARER_ID,
                       ISO9660_ACHARS);
  iso9660_strncpy_pad (ipd.application_id, application_id,
                       ISO_MAX_APPLICATION_ID, ISO9660_ACHARS);

  iso9660_strncpy_pad (ipd.copyright_file_id    , "", 37, ISO9660_DCHARS);
  iso9660_strncpy_pad (ipd.abstract_file_id     , "", 37, ISO9660_DCHARS);
  iso9660_strncpy_pad (ipd.bibliographic_file_id, "", 37, ISO9660_DCHARS);

  gmtime_r(pvd_time, &temp_tm);
  iso9660_set_ltime (&temp_tm, &(ipd.creation_date));
  gmtime_r(pvd_time, &temp_tm);
  iso9660_set_ltime (&temp_tm, &(ipd.modification_date));
  iso9660_set_ltime (NULL,     &(ipd.expiration_date));
  iso9660_set_ltime (NULL,     &(ipd.effective_date));

  ipd.file_structure_version = to_711(1);

  /* we leave ipd.application_data = 0 */

  memcpy(pd, &ipd, sizeof(ipd)); /* copy stuff to arg ptr */
}

unsigned int
iso9660_dir_calc_record_size(unsigned int namelen, unsigned int su_len)
{
  unsigned int length;

  length = sizeof(iso9660_dir_t);
  length += namelen;
  if (length % 2) /* pad to word boundary */
    length++;
  length += su_len;
  if (length % 2) /* pad to word boundary again */
    length++;

  return length;
}

void
iso9660_dir_add_entry_su(void *dir,
                         const char filename[],
                         uint32_t extent,
                         uint32_t size,
                         uint8_t file_flags,
                         const void *su_data,
                         unsigned int su_size,
                         const time_t *entry_time)
{
  iso9660_dir_t *idr = dir;
  uint8_t *dir8 = dir;
  unsigned int offset = 0;
  uint32_t dsize = from_733(idr->size);
  int length, su_offset;
  struct tm temp_tm;
  cdio_assert (sizeof(iso9660_dir_t) == 33);

  if (!dsize && !idr->length)
    dsize = ISO_BLOCKSIZE; /* for when dir lacks '.' entry */

  cdio_assert (dsize > 0 && !(dsize % ISO_BLOCKSIZE));
  cdio_assert (dir != NULL);
  cdio_assert (extent > 17);
  cdio_assert (filename != NULL);
  cdio_assert (strlen(filename) <= MAX_ISOPATHNAME);

  length = sizeof(iso9660_dir_t);
  length += strlen(filename);
  length = _cdio_ceil2block (length, 2); /* pad to word boundary */
  su_offset = length;
  length += su_size;
  length = _cdio_ceil2block (length, 2); /* pad to word boundary again */

  /* find the last entry's end */
  {
    unsigned int ofs_last_rec = 0;

    offset = 0;
    while (offset < dsize)
      {
        if (!dir8[offset])
          {
            offset++;
            continue;
          }

        offset += dir8[offset];
        ofs_last_rec = offset;
      }

    cdio_assert (offset == dsize);

    offset = ofs_last_rec;
  }

  /* be sure we don't cross sectors boundaries */
  offset = _cdio_ofs_add (offset, length, ISO_BLOCKSIZE);
  offset -= length;

  cdio_assert (offset + length <= dsize);

  idr = (iso9660_dir_t *) &dir8[offset];

  cdio_assert (offset+length < dsize);

  memset(idr, 0, length);

  idr->length = to_711(length);
  idr->extent = to_733(extent);
  idr->size = to_733(size);

  gmtime_r(entry_time, &temp_tm);
  iso9660_set_dtime (&temp_tm, &(idr->recording_time));

  idr->file_flags = to_711(file_flags);

  idr->volume_sequence_number = to_723(1);

  idr->filename.len = to_711(strlen(filename)
                             ? strlen(filename) : 1); /* working hack! */

  memcpy(&idr->filename.str[1], filename, from_711(idr->filename.len));
  if (su_size > 0 && su_data)
    memcpy(&dir8[offset] + su_offset, su_data, su_size);
}

void
iso9660_dir_init_new (void *dir,
                      uint32_t self,
                      uint32_t ssize,
                      uint32_t parent,
                      uint32_t psize,
                      const time_t *dir_time)
{
  iso9660_dir_init_new_su (dir, self, ssize, NULL, 0, parent, psize, NULL,
                           0, dir_time);
}

void
iso9660_dir_init_new_su (void *dir,
                         uint32_t self,
                         uint32_t ssize,
                         const void *ssu_data,
                         unsigned int ssu_size,
                         uint32_t parent,
                         uint32_t psize,
                         const void *psu_data,
                         unsigned int psu_size,
                         const time_t *dir_time)
{
  cdio_assert (ssize > 0 && !(ssize % ISO_BLOCKSIZE));
  cdio_assert (psize > 0 && !(psize % ISO_BLOCKSIZE));
  cdio_assert (dir != NULL);

  memset (dir, 0, ssize);

  /* "\0" -- working hack due to padding  */
  iso9660_dir_add_entry_su (dir, "\0", self, ssize, ISO_DIRECTORY, ssu_data,
                            ssu_size, dir_time);

  iso9660_dir_add_entry_su (dir, "\1", parent, psize, ISO_DIRECTORY, psu_data,
                            psu_size, dir_time);
}

/* Zero's out pathable. Do this first.  */
void
iso9660_pathtable_init (void *pt)
{
  cdio_assert (sizeof (iso_path_table_t) == 8);

  cdio_assert (pt != NULL);

  memset (pt, 0, ISO_BLOCKSIZE); /* fixme */
}

/*!
  Returns POSIX mode bitstring for a given file.
*/
mode_t
iso9660_get_posix_filemode(const iso9660_stat_t *p_iso_dirent)
{
  mode_t mode = 0;

#ifdef HAVE_ROCK
  if (yep == p_iso_dirent->rr.b3_rock) {
      return iso9660_get_posix_filemode_from_rock(&p_iso_dirent->rr);
  } else
#endif
  if (p_iso_dirent->b_xa) {
    return iso9660_get_posix_filemode_from_xa(p_iso_dirent->xa.attributes);
  }
  return mode;
}

static const iso_path_table_t *
pathtable_get_entry (const void *pt, unsigned int entrynum)
{
  const uint8_t *tmp = pt;
  unsigned int offset = 0;
  unsigned int count = 0;

  cdio_assert (pt != NULL);

  while (from_711 (*tmp))
    {
      if (count == entrynum)
        break;

      cdio_assert (count < entrynum);

      offset += sizeof (iso_path_table_t);
      offset += from_711 (*tmp);
      if (offset % 2)
        offset++;
      tmp = (uint8_t *)pt + offset;
      count++;
    }

  if (!from_711 (*tmp))
    return NULL;

  return (const void *) tmp;
}

void
pathtable_get_size_and_entries (const void *pt,
                                unsigned int *size,
                                unsigned int *entries)
{
  const uint8_t *tmp = pt;
  unsigned int offset = 0;
  unsigned int count = 0;

  cdio_assert (pt != NULL);

  while (from_711 (*tmp))
    {
      offset += sizeof (iso_path_table_t);
      offset += from_711 (*tmp);
      if (offset % 2)
        offset++;
      tmp = (uint8_t *)pt + offset;
      count++;
    }

  if (size)
    *size = offset;

  if (entries)
    *entries = count;
}

unsigned int
iso9660_pathtable_get_size (const void *pt)
{
  unsigned int size = 0;
  pathtable_get_size_and_entries (pt, &size, NULL);
  return size;
}

uint16_t
iso9660_pathtable_l_add_entry (void *pt,
                               const char name[],
                               uint32_t extent,
                               uint16_t parent)
{
  iso_path_table_t *ipt =
    (iso_path_table_t *)((char *)pt + iso9660_pathtable_get_size (pt));
  size_t name_len = strlen (name) ? strlen (name) : 1;
  unsigned int entrynum = 0;

  cdio_assert (iso9660_pathtable_get_size (pt) < ISO_BLOCKSIZE); /*fixme */

  memset (ipt, 0, sizeof (iso_path_table_t) + name_len); /* paranoia */

  ipt->name_len = to_711 (name_len);
  ipt->extent = to_731 (extent);
  ipt->parent = to_721 (parent);
  memcpy (ipt->name, name, name_len);

  pathtable_get_size_and_entries (pt, NULL, &entrynum);

  if (entrynum > 1)
    {
      const iso_path_table_t *ipt2
        = pathtable_get_entry (pt, entrynum - 2);

      cdio_assert (ipt2 != NULL);

      cdio_assert (from_721 (ipt2->parent) <= parent);
    }

  return entrynum;
}

uint16_t
iso9660_pathtable_m_add_entry (void *pt,
                               const char name[],
                               uint32_t extent,
                               uint16_t parent)
{
  iso_path_table_t *ipt =
    (iso_path_table_t *)((char *)pt + iso9660_pathtable_get_size (pt));
  size_t name_len = strlen (name) ? strlen (name) : 1;
  unsigned int entrynum = 0;

  cdio_assert (iso9660_pathtable_get_size(pt) < ISO_BLOCKSIZE); /* fixme */

  memset(ipt, 0, sizeof (iso_path_table_t) + name_len); /* paranoia */

  ipt->name_len = to_711 (name_len);
  ipt->extent = to_732 (extent);
  ipt->parent = to_722 (parent);
  memcpy (ipt->name, name, name_len);

  pathtable_get_size_and_entries (pt, NULL, &entrynum);

  if (entrynum > 1)
    {
      const iso_path_table_t *ipt2
        = pathtable_get_entry (pt, entrynum - 2);

      cdio_assert (ipt2 != NULL);

      cdio_assert (from_722 (ipt2->parent) <= parent);
    }

  return entrynum;
}

/*!
  Check that pathname is a valid ISO-9660 directory name.

  A valid directory name should not start out with a slash (/),
  dot (.) or null byte, should be less than 37 characters long,
  have no more than 8 characters in a directory component
  which is separated by a /, and consist of only DCHARs.
 */
bool
iso9660_dirname_valid_p (const char pathname[])
{
  const char *p = pathname;
  int len;

  cdio_assert (pathname != NULL);

  if (*p == '/' || *p == '.' || *p == '\0')
    return false;

  if (strlen (pathname) > MAX_ISOPATHNAME)
    return false;

  len = 0;
  for (; *p; p++)
    if (iso9660_is_dchar (*p))
      {
        len++;
        if (len > 8)
          return false;
      }
    else if (*p == '/')
      {
        if (!len)
          return false;
        len = 0;
      }
    else
      return false; /* unexpected char */

  if (!len)
    return false; /* last char may not be '/' */

  return true;
}

/*!
  Check that pathname is a valid ISO-9660 pathname.

  A valid pathname contains a valid directory name, if one appears and
  the filename portion should be no more than 8 characters for the
  file prefix and 3 characters in the extension (or portion after a
  dot). There should be exactly one dot somewhere in the filename
  portion and the filename should be composed of only DCHARs.

  True is returned if pathname is valid.
 */
bool
iso9660_pathname_valid_p (const char pathname[])
{
  const char *p = NULL;

  cdio_assert (pathname != NULL);

  if ((p = strrchr (pathname, '/')))
    {
      bool rc;
      char *_tmp = strdup (pathname);

      *strrchr (_tmp, '/') = '\0';

      rc = iso9660_dirname_valid_p (_tmp);

      free (_tmp);

      if (!rc)
        return false;

      p++;
    }
  else
    p = pathname;

  if (strlen (pathname) > (MAX_ISOPATHNAME - 6))
    return false;

  {
    int len = 0;
    int dots = 0;

    for (; *p; p++)
      if (iso9660_is_dchar (*p))
        {
          len++;
          if (dots == 0 ? len > 8 : len > 3)
            return false;
        }
      else if (*p == '.')
        {
          dots++;
          if (dots > 1)
            return false;
          if (!len)
            return false;
          len = 0;
        }
      else
        return false;

    if (dots != 1)
      return false;
  }

  return true;
}

/*!
  Take pathname and a version number and turn that into a ISO-9660
  pathname.  (That's just the pathname followd by ";" and the version
  number. For example, mydir/file.ext -> mydir/file.ext;1 for version
  1. The resulting ISO-9660 pathname is returned.
*/
char *
iso9660_pathname_isofy (const char pathname[], uint16_t version)
{
  char tmpbuf[1024] = { 0, };

  cdio_assert (strlen (pathname) < (sizeof (tmpbuf) - sizeof (";65535")));

  snprintf (tmpbuf, sizeof(tmpbuf), "%s;%d", pathname, version);

  return strdup (tmpbuf);
}

/*!
  Return the PVD's application ID.
  NULL is returned if there is some problem in getting this.
*/
char *
iso9660_get_application_id(iso9660_pvd_t *p_pvd)
{
  if (NULL==p_pvd) return NULL;
  return strdup(strip_trail(p_pvd->application_id, ISO_MAX_APPLICATION_ID));
}

#ifdef FIXME
lsn_t
iso9660_get_dir_extent(const iso9660_dir_t *idr)
{
  if (NULL == idr) return 0;
  return from_733(idr->extent);
}
#endif

uint8_t
iso9660_get_dir_len(const iso9660_dir_t *idr)
{
  if (NULL == idr) return 0;
  return idr->length;
}

#ifdef FIXME
uint8_t
iso9660_get_dir_size(const iso9660_dir_t *idr)
{
  if (NULL == idr) return 0;
  return from_733(idr->size);
}
#endif

uint8_t
iso9660_get_pvd_type(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return 255;
  return(pvd->type);
}

const char *
iso9660_get_pvd_id(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return "ERR";
  return(pvd->id);
}

int
iso9660_get_pvd_space_size(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return 0;
  return from_733(pvd->volume_space_size);
}

int
iso9660_get_pvd_block_size(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return 0;
  return from_723(pvd->logical_block_size);
}

/*! Return the primary volume id version number (of pvd).
    If there is an error 0 is returned.
 */
int
iso9660_get_pvd_version(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return 0;
  return pvd->version;
}

/*! Return the LSN of the root directory for pvd.
    If there is an error CDIO_INVALID_LSN is returned.
 */
lsn_t
iso9660_get_root_lsn(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd)
    return CDIO_INVALID_LSN;
  else {
    const iso9660_dir_t *idr = &(pvd->root_directory_record);
    if (NULL == idr) return CDIO_INVALID_LSN;
    return(from_733 (idr->extent));
  }
}

/*!
   Return a string containing the preparer id with trailing
   blanks removed.
*/
char *
iso9660_get_preparer_id(const iso9660_pvd_t *pvd)
{
  if (NULL==pvd) return NULL;
  return strdup(strip_trail(pvd->preparer_id, ISO_MAX_PREPARER_ID));
}

/*!
   Return a string containing the publisher id with trailing
   blanks removed.
*/
char *
iso9660_get_publisher_id(const iso9660_pvd_t *pvd)
{
  if (NULL==pvd) return NULL;
  return strdup(strip_trail(pvd->publisher_id, ISO_MAX_PUBLISHER_ID));
}

/*!
   Return a string containing the PVD's system id with trailing
   blanks removed.
*/
char *
iso9660_get_system_id(const iso9660_pvd_t *pvd)
{
  if (NULL==pvd) return NULL;
  return strdup(strip_trail(pvd->system_id, ISO_MAX_SYSTEM_ID));
}

/*!
  Return the PVD's volume ID.
*/
char *
iso9660_get_volume_id(const iso9660_pvd_t *pvd)
{
  if (NULL == pvd) return NULL;
  return strdup(strip_trail(pvd->volume_id, ISO_MAX_VOLUME_ID));
}

/*!
  Return the PVD's volumeset ID.
  NULL is returned if there is some problem in getting this.
*/
char *
iso9660_get_volumeset_id(const iso9660_pvd_t *pvd)
{
  if ( NULL == pvd ) return NULL;
  return strdup(strip_trail(pvd->volume_set_id, ISO_MAX_VOLUMESET_ID));
}


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
