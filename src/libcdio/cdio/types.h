/*
    Copyright (C) 2002, 2003, 2004, 2005, 2006, 2008
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

/** \file types.h 
 *  \brief  Common type definitions used pervasively in libcdio.
 */


#ifndef __CDIO_TYPES_H__
#define __CDIO_TYPES_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef EXTERNAL_LIBCDIO_CONFIG_H
#define EXTERNAL_LIBCDIO_CONFIG_H
#include <cdio/cdio_config.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

  /* provide some C99 definitions */

#if defined(HAVE_SYS_TYPES_H) 
#include <sys/types.h>
#endif 

#ifdef _MSC_VER
#include <windows.h>
typedef unsigned short mode_t;
#endif

#if defined(HAVE_STDINT_H)
# include <stdint.h>
#elif defined(HAVE_INTTYPES_H)
# include <inttypes.h>
#elif defined(AMIGA) || defined(__linux__)
  typedef u_int8_t uint8_t;
  typedef u_int16_t uint16_t;
  typedef u_int32_t uint32_t;
  typedef u_int64_t uint64_t;
#else
  /* warning ISO/IEC 9899:1999 <stdint.h> was missing and even <inttypes.h> */
  /* fixme */
#endif /* HAVE_STDINT_H */
  
typedef uint8_t ubyte;

  /* default HP/UX macros are broken */
#if defined(__hpux__)
# undef UINT16_C
# undef UINT32_C
# undef UINT64_C
# undef INT64_C
#endif

  /* if it's still not defined, take a good guess... should work for
     most 32bit and 64bit archs */
  
#ifndef UINT16_C
# define UINT16_C(c) c ## U
#endif
  
#ifndef UINT32_C
# if defined (SIZEOF_INT) && SIZEOF_INT == 4
#  define UINT32_C(c) c ## U
# elif defined (SIZEOF_LONG) && SIZEOF_LONG == 4
#  define UINT32_C(c) c ## UL
# else
#  define UINT32_C(c) c ## U
# endif
#endif
  
#ifndef UINT64_C
# if defined (SIZEOF_LONG) && SIZEOF_LONG == 8
#  define UINT64_C(c) c ## UL
# elif defined (SIZEOF_INT) && SIZEOF_INT == 8
#  define UINT64_C(c) c ## U
# else
#  define UINT64_C(c) c ## ULL
# endif
#endif
  
#ifndef INT64_C
# if defined (SIZEOF_LONG) && SIZEOF_LONG == 8
#  define INT64_C(c) c ## L
# elif defined (SIZEOF_INT) && SIZEOF_INT == 8
#  define INT64_C(c) c 
# else
#  define INT64_C(c) c ## LL
# endif
#endif
  
#ifndef __cplusplus
# if defined(HAVE_STDBOOL_H)
#  include <stdbool.h>
# else
   /* ISO/IEC 9899:1999 <stdbool.h> missing -- enabling workaround */
  
#   define false   0
#   define true    1
#   define bool uint8_t
# endif /*HAVE_STDBOOL_H*/
#endif /*C++*/
  
  /* some GCC optimizations -- gcc 2.5+ */
  
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
#define GNUC_PRINTF( format_idx, arg_idx )              \
  __attribute__((format (printf, format_idx, arg_idx)))
#define GNUC_SCANF( format_idx, arg_idx )               \
  __attribute__((format (scanf, format_idx, arg_idx)))
#define GNUC_FORMAT( arg_idx )                  \
  __attribute__((format_arg (arg_idx)))
#define GNUC_NORETURN                           \
  __attribute__((noreturn))
#define GNUC_CONST                              \
  __attribute__((const))
#define GNUC_UNUSED                             \
  __attribute__((unused))
#define GNUC_PACKED                             \
  __attribute__((packed))
#else   /* !__GNUC__ */
#define GNUC_PRINTF( format_idx, arg_idx )
#define GNUC_SCANF( format_idx, arg_idx )
#define GNUC_FORMAT( arg_idx )
#define GNUC_NORETURN
#define GNUC_CONST
#define GNUC_UNUSED
#define GNUC_PACKED
#endif  /* !__GNUC__ */
  
#if defined(__GNUC__)
  /* for GCC we try to use GNUC_PACKED */
# define PRAGMA_BEGIN_PACKED
# define PRAGMA_END_PACKED
#elif defined(HAVE_ISOC99_PRAGMA)
  /* should work with most EDG-frontend based compilers */
# define PRAGMA_BEGIN_PACKED _Pragma("pack(1)")
# define PRAGMA_END_PACKED   _Pragma("pack()")
#else /* neither gcc nor _Pragma() available... */
  /* ...so let's be naive and hope the regression testsuite is run... */
// TODO!
# define PRAGMA_BEGIN_PACKED
# define PRAGMA_END_PACKED
#endif
  
  /*
   * user directed static branch prediction gcc 2.96+
   */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 95)
# define GNUC_LIKELY(x)   __builtin_expect((x),true)
# define GNUC_UNLIKELY(x) __builtin_expect((x),false)
#else 
# define GNUC_LIKELY(x)   (x) 
# define GNUC_UNLIKELY(x) (x)
#endif
  
#ifndef NULL
# define NULL ((void*) 0)
#endif
  
  /* our own offsetof()-like macro */
#define __cd_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
  
  /*!
    \brief MSF (minute/second/frame) structure 

    One CD-ROMs addressing scheme especially used in audio formats
    (Red Book) is an address by minute, sector and frame which
    BCD-encoded in three bytes. An alternative format is an lba_t.

    Note: the fields in this structure are BCD encoded. Use
    cdio_to_bcd8() or cdio_from_bcd8() to convert an integer into or
    out of this format. The format specifier %x (not %d) can be used
    if you need to format or print values in this structure.
    
    @see lba_t
  */
  PRAGMA_BEGIN_PACKED
  struct msf_s {
    uint8_t m, s, f; /* BCD encoded! */
  } GNUC_PACKED;
  PRAGMA_END_PACKED
  
  typedef struct msf_s msf_t;

#define msf_t_SIZEOF 3

  /*!
    \brief UTF-8 char definition 

    Type to denote UTF-8 strings.
  */

  typedef char cdio_utf8_t;

  typedef enum  {
    nope  = 0,
    yep   = 1,
    dunno = 2
  } bool_3way_t;
  
  /* type used for bit-fields in structs (1 <= bits <= 8) */
#if defined(__GNUC__)
  /* this is strict ISO C99 which allows only 'unsigned int', 'signed
     int' and '_Bool' explicitly as bit-field type */
  typedef unsigned int bitfield_t;
#else
  /* other compilers might increase alignment requirements to match the
     'unsigned int' type -- fixme: find out how unalignment accesses can
     be pragma'ed on non-gcc compilers */
  typedef uint8_t bitfield_t;
#endif
  
  /*! The type of a Logical Block Address. We allow for an lba to be 
    negative to be consistent with an lba, although I'm not sure this
    this is possible.
      
   */
  typedef int32_t lba_t;
  
  /*! The type of a Logical Sector Number. Note that an lba can be negative
    and the MMC3 specs allow for a conversion of a negative lba.

    @see msf_t
  */
  typedef int32_t lsn_t;
  
  /* Address in either MSF or logical format */
  union cdio_cdrom_addr		
  {
    msf_t	msf;
    lba_t	lba;
  };

  /*! The type of a track number 0..99. */
  typedef uint8_t track_t;
  
  /*! The type of a session number 0..99. */
  typedef uint8_t session_t;
  
  /*! 
    Constant for invalid session number
  */
#define CDIO_INVALID_SESSION   0xFF
  
  /*! 
    Constant for invalid LBA. It is 151 less than the most negative
    LBA -45150. This provide slack for the 150-frame offset in
    LBA to LSN 150 conversions
  */
#define CDIO_INVALID_LBA    -45301
  
  /*! 
    Constant for invalid LSN
  */
#define CDIO_INVALID_LSN    CDIO_INVALID_LBA

  /*! 
    Number of ASCII bytes in a media catalog number (MCN).
    We include an extra 0 byte so these can be used as C strings.
  */
#define CDIO_MCN_SIZE       13

  /*! 
    Type to hold ASCII bytes in a media catalog number (MCN).
    We include an extra 0 byte so these can be used as C strings.
  */
  typedef char cdio_mcn_t[CDIO_MCN_SIZE+1];
  

  /*! 
    Number of ASCII bytes in International Standard Recording Codes (ISRC)
  */
#define CDIO_ISRC_SIZE       12

  /*! 
    Type to hold ASCII bytes in a ISRC.
    We include an extra 0 byte so these can be used as C strings.
  */
  typedef char cdio_isrc_t[CDIO_ISRC_SIZE+1];

  typedef int cdio_fs_anal_t;

  /*! 
    track flags
    Q Sub-channel Control Field (4.2.3.3)
  */
  typedef enum {
    CDIO_TRACK_FLAG_NONE = 		 0x00,	/**< no flags set */
    CDIO_TRACK_FLAG_PRE_EMPHASIS =	 0x01,	/**< audio track recorded with
                                                   pre-emphasis */
    CDIO_TRACK_FLAG_COPY_PERMITTED =	 0x02,	/**< digital copy permitted */
    CDIO_TRACK_FLAG_DATA =		 0x04,	/**< data track */
    CDIO_TRACK_FLAG_FOUR_CHANNEL_AUDIO = 0x08,  /**< 4 audio channels */
  CDIO_TRACK_FLAG_SCMS =		 0x10	/**< SCMS (5.29.2.7) */
} cdio_track_flag;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CDIO_TYPES_H__ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
