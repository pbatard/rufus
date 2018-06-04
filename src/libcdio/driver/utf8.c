/*
  Copyright (C) 2006, 2008 Burkhard Plaum <plaum@ipf.uni-stuttgart.de>
  Copyright (C) 2011, 2014 Rocky Bernstein <rocky@gnu.org>
  Copyright (C) 2012 Pete Batard <pete@akeo.ie>

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
/* UTF-8 support */

#ifdef HAVE_CONFIG_H
# define __CDIO_CONFIG_H__ 1
# include "config.h"
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <cdio/utf8.h>
#include <cdio/logging.h>
#include <cdio/memory.h>
#include "cdio_assert.h"

/* Windows requires some basic UTF-8 support outside of Joliet */
#if defined(_WIN32)
#include <windows.h>

#define wchar_to_utf8_no_alloc(wsrc, dest, dest_size) \
	WideCharToMultiByte(CP_UTF8, 0, wsrc, -1, dest, dest_size, NULL, NULL)
#define utf8_to_wchar_no_alloc(src, wdest, wdest_size) \
	MultiByteToWideChar(CP_UTF8, 0, src, -1, wdest, wdest_size)

/*
 * Converts an UTF-16 string to UTF8 (allocate returned string)
 * Returns NULL on error
 */
static inline char* cdio_wchar_to_utf8(const wchar_t* wstr)
  {
  int size = 0;
  char* str = NULL;

  /* Find out the size we need to allocate for our converted string */
  size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  if (size <= 1) /* An empty string would be size 1 */
    return NULL;

  if ((str = (char*)calloc(size, 1)) == NULL)
    return NULL;

  if (wchar_to_utf8_no_alloc(wstr, str, size) != size) {
    free(str);
    return NULL;
  }

  return str;
  }

/*
 * Converts an UTF8 string to UTF-16 (allocate returned string)
 * Returns NULL on error
 */
wchar_t* cdio_utf8_to_wchar(const char* str)
  {
  int size = 0;
  wchar_t* wstr = NULL;

  /* Find out the size we need to allocate for our converted string */
  size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  if (size <= 1) /* An empty string would be size 1 */
    return NULL;

  if ((wstr = (wchar_t*)calloc(size, sizeof(wchar_t))) == NULL)
    return NULL;

  if (utf8_to_wchar_no_alloc(str, wstr, size) != size) {
    free(wstr);
    return NULL;
  }
  return wstr;
  }

/* UTF-8 compliant version of fopen() */
FILE* fopen_utf8(const char* filename, const char* mode)
{
  FILE* ret = NULL;
  wchar_t* wfilename = cdio_utf8_to_wchar(filename);
  wchar_t* wmode =  cdio_utf8_to_wchar(mode);
  ret = _wfopen(wfilename, wmode);
  cdio_free(wfilename);
  cdio_free(wmode);
  return ret;
}
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
struct cdio_charset_coverter_s
  {
  iconv_t ic;
  };

cdio_charset_coverter_t *
cdio_charset_converter_create(const char * src_charset,
                              const char * dst_charset)
  {
  cdio_charset_coverter_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->ic = iconv_open(dst_charset, src_charset);
  return ret;
  }

#if 0
static void bgav_hexdump(uint8_t * data, int len, int linebreak)
  {
  int i;
  int bytes_written = 0;
  int imax;

  while(bytes_written < len)
    {
    imax = (bytes_written + linebreak > len) ? len - bytes_written : linebreak;
    for(i = 0; i < imax; i++)
      fprintf(stderr, "%02x ", data[bytes_written + i]);
    for(i = imax; i < linebreak; i++)
      fprintf(stderr, "   ");
    for(i = 0; i < imax; i++)
      {
      if(!(data[bytes_written + i] & 0x80) && (data[bytes_written + i] >= 32))
        fprintf(stderr, "%c", data[bytes_written + i]);
      else
        fprintf(stderr, ".");
      }
    bytes_written += imax;
    fprintf(stderr, "\n");
    }
  }
#endif

void cdio_charset_converter_destroy(cdio_charset_coverter_t*cnv)
  {
  iconv_close(cnv->ic);
  free(cnv);
  }

#define BYTES_INCREMENT 16

static bool
do_convert(iconv_t cd, const char * src, int src_len,
           char ** dst, int *dst_len)
  {
  char * ret;

  char *inbuf;
  char *outbuf;
  int alloc_size;
  int output_pos;
  size_t inbytesleft;
  size_t outbytesleft;

  if(src_len < 0)
    src_len = strlen(src);
#if 0
  fprintf(stderr, "Converting:\n");
  bgav_hexdump(src, src_len, 16);
#endif
  alloc_size = src_len + BYTES_INCREMENT;

  inbytesleft  = src_len;

  /* We reserve space here to add a final '\0' */
  outbytesleft = alloc_size-1;

  ret    = malloc(alloc_size);

  inbuf  = (char *)src;
  outbuf = ret;

  while(1)
    {

    if(iconv(cd, (ICONV_CONST char **)&inbuf, &inbytesleft,
             &outbuf, &outbytesleft) == (size_t)-1)
      {
      switch(errno)
        {
        case E2BIG:
          output_pos = (int)(outbuf - ret);

          alloc_size   += BYTES_INCREMENT;
          outbytesleft += BYTES_INCREMENT;

          ret = realloc(ret, alloc_size);
          if (ret == NULL)
            {
            cdio_warn("Can't realloc(%d).", alloc_size);
            return false;
            }
          outbuf = ret + output_pos;
          break;
        default:
          cdio_warn("Iconv failed: %s", strerror(errno));
          if (ret != NULL)
            free(ret);
          return false;
          break;
        }
      }
    if(!inbytesleft)
      break;
    }
  /* Zero terminate */
  *outbuf = '\0';

  /* Set return values */
  *dst = ret;
  if(dst_len)
    *dst_len = (int)(outbuf - ret);
#if 0
  fprintf(stderr, "Conversion done, src:\n");
  bgav_hexdump(src, src_len, 16);
  fprintf(stderr, "dst:\n");
  bgav_hexdump((uint8_t*)(ret), (int)(outbuf - ret), 16);
#endif
  return true;
  }

bool cdio_charset_convert(cdio_charset_coverter_t*cnv,
                          char * src, int src_len,
                          char ** dst, int * dst_len)
  {
  return do_convert(cnv->ic, src, src_len, dst, dst_len);
  }



bool cdio_charset_from_utf8(cdio_utf8_t * src, char ** dst,
                            int * dst_len, const char * dst_charset)
  {
  iconv_t ic;
  bool result;
  ic = iconv_open(dst_charset, "UTF-8");
  result = do_convert(ic, src, -1, dst, dst_len);
  iconv_close(ic);
  return result;
  }




bool cdio_charset_to_utf8(const char *src, size_t src_len, cdio_utf8_t **dst,
                          const char * src_charset)
  {
  iconv_t ic;
  bool result;
  ic = iconv_open("UTF-8", src_charset);
  result = do_convert(ic, src, src_len, dst, NULL);
  iconv_close(ic);
  return result;
  }
#elif defined(_WIN32)

bool cdio_charset_from_utf8(cdio_utf8_t * src, char ** dst,
                            int * dst_len, const char * dst_charset)
  {
  wchar_t* le_dst;
  size_t i, len;

  if (src == NULL || dst == NULL || dst_len == NULL || dst_charset == NULL || strcmp(dst_charset, "UTF-8") != 0)
    return false;

  /* Eliminate empty strings */
  le_dst = cdio_utf8_to_wchar(src);
  if ((le_dst == NULL) || (le_dst[0] == 0)) {
    cdio_free(le_dst);
    return false;
  }

  /* Perform byte reversal */
  len = wcslen(le_dst);
  *dst = (char*)calloc(len+1, sizeof(wchar_t));
  cdio_assert(*dst != NULL);
  for (i=0; i<2*len; i++) {
    (*dst)[i] = ((char*)le_dst)[i+1];
    (*dst)[i+1] = ((char*)le_dst)[i];
  }
  free(le_dst);

  return true;
  }

bool cdio_charset_to_utf8(const char *src, size_t src_len, cdio_utf8_t **dst,
                          const char *src_charset)
  {
  wchar_t* le_src;
  int i;

  if (src == NULL || dst == NULL || src_charset == NULL || strcmp(src_charset, "UCS-2BE") != 0)
    return false;

  /* Compute UCS-2 src length */
  if (src_len == (size_t)-1) {
    for (src_len = 0; ((uint16_t*)src)[src_len] !=0; src_len++);
  } else {
    src_len >>=1;
  }

  /* Eliminate empty strings */
  if ((src_len < 1) || ((src[0] == 0) && (src[1] == 0))) {
    *dst = NULL;
    return false;
  }

  /* Perform byte reversal */
  le_src = (wchar_t*)malloc(2*src_len+2);
  cdio_assert(le_src != NULL);
  for (i=0; i<src_len; i++) {
    ((char*)le_src)[2*i] = src[2*i+1];
    ((char*)le_src)[2*i+1] = src[2*i];
  }
  le_src[src_len] = 0;
  *dst = cdio_wchar_to_utf8(le_src);
  free(le_src);

  return (*dst != NULL);
}
#else
# error "The iconv library is needed to build drivers, but it is not detected"
#endif /* HAVE_ICONV */
