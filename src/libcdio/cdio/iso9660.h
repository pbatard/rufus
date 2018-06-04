/*
    Copyright (C) 2003-2008, 2012-2013, 2017
                  Rocky Bernstein <rocky@gnu.org>
    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    See also iso9660.h by Eric Youngdale (1993).

    Copyright 1993 Yggdrasil Computing, Incorporated

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
/*!
 * \file iso9660.h
 *
 * \brief The top-level interface header for libiso9660: the ISO-9660
 * filesystem library; applications include this.
 *
 * See also the ISO-9660 specification. The freely available European
 * equivalant standard is called ECMA-119.
*/


#ifndef CDIO_ISO9660_H_
#define CDIO_ISO9660_H_

#include <time.h>

#include <cdio/cdio.h>
#include <cdio/ds.h>
#include <cdio/posix.h>

/** \brief ISO 9660 Integer and Character types

These are described in the section 7 of the ISO 9660 (or ECMA 119)
specification.
*/

typedef uint8_t  iso711_t; /*! See section 7.1.1 */
typedef int8_t   iso712_t; /*! See section 7.1.2 */
typedef uint16_t iso721_t; /*! See section 7.2.1 */
typedef uint16_t iso722_t; /*! See section 7.2.2 */
typedef uint32_t iso723_t; /*! See section 7.2.3 */
typedef uint32_t iso731_t; /*! See section 7.3.1 */
typedef uint32_t iso732_t; /*! See section 7.3.2 */
typedef uint64_t iso733_t; /*! See section 7.3.3 */

typedef char     achar_t;  /*! See section 7.4.1 */
typedef char     dchar_t;  /*! See section 7.4.1 */

#ifndef EMPTY_ARRAY_SIZE
#define EMPTY_ARRAY_SIZE 0
#endif

#include <cdio/types.h>
#include <cdio/xa.h>

#ifdef ISODCL
#undef ISODCL
#endif
/* This part borrowed from the bsd386 isofs */
#define ISODCL(from, to)        ((to) - (from) + 1)

#define MIN_TRACK_SIZE 4*75
#define MIN_ISO_SIZE MIN_TRACK_SIZE

/*! The below isn't really an enumeration one would really use in a
    program; things are done this way so that in a debugger one can to
    refer to the enumeration value names such as in a debugger
    expression and get something. With the more common a \#define
    mechanism, the name/value assocation is lost at run time.
  */
extern enum iso_enum1_s {
  ISO_PVD_SECTOR      =   16, /**< Sector of Primary Volume Descriptor. */
  ISO_EVD_SECTOR      =   17, /**< Sector of End Volume Descriptor. */
  LEN_ISONAME         =   31, /**< Size in bytes of the filename
                                 portion + null byte. */
  ISO_MAX_SYSTEM_ID   =   32, /**< Maximum number of characters in a system
                                 id. */
  MAX_ISONAME         =   37, /**< Size in bytes of the filename
                                 portion + null byte. */
  ISO_MAX_PREPARER_ID =  128, /**< Maximum number of characters in a
                                 preparer id. */
  MAX_ISOPATHNAME     =  255, /**< Maximum number of characters in the
                                 entire ISO 9660 filename. */
  ISO_BLOCKSIZE       = 2048  /**< Number of bytes in an ISO 9660 block. */

} iso_enums1;

/*! An enumeration for some of the ISO_* \#defines below. This isn't
    really an enumeration one would really use in a program it is here
    to be helpful in debuggers where wants just to refer to the
    ISO_*_ names and get something.
  */

/*! ISO 9660 directory flags. */
extern enum iso_flag_enum_s {
  ISO_FILE            =   0,   /**<  Not really a flag...                */
  ISO_EXISTENCE       =   1,   /**< Do not make existence known (hidden) */
  ISO_DIRECTORY       =   2,   /**< This file is a directory             */
  ISO_ASSOCIATED      =   4,   /**< This file is an associated file      */
  ISO_RECORD          =   8,   /**< Record format in extended attr. != 0 */
  ISO_PROTECTION      =  16,   /**< No read/execute perm. in ext. attr.  */
  ISO_DRESERVED1      =  32,   /**<, Reserved bit 5                      */
  ISO_DRESERVED2      =  64,   /**<, Reserved bit 6                      */
  ISO_MULTIEXTENT     = 128,   /**< Not final entry of a mult. ext. file */
} iso_flag_enums;

/*! Volume descriptor types */
extern enum iso_vd_enum_s {
  ISO_VD_BOOT_RECORD   =  0,  /**< CD is bootable */
  ISO_VD_PRIMARY       =  1,  /**< Is in any ISO-9660 */
  ISO_VD_SUPPLEMENTARY =  2,  /**< Used by Joliet, for example */
  ISO_VD_PARITION      =  3,  /**< Indicates a partition of a CD */
  ISO_VD_END           = 255
} iso_vd_enums;


/*!
   An ISO filename is:
   <em>abcd</em>.<em>eee</em> ->
   <em>filename</em>.<em>ext</em>;<em>version#</em>

    For ISO-9660 Level 1, the maximum needed string length is:

@code
         30 chars (filename + ext)
    +     2 chars ('.' + ';')
    +     5 chars (strlen("32767"))
    +     1 null byte
   ================================
    =    38 chars
@endcode

*/

/*! \brief Maximum number of characters in a publisher id. */
#define ISO_MAX_PUBLISHER_ID 128

/*! \brief Maximum number of characters in an application id. */
#define ISO_MAX_APPLICATION_ID 128

/*! \brief Maximum number of characters in a volume id. */
#define ISO_MAX_VOLUME_ID 32

/*! \brief Maximum number of characters in a volume-set id. */
#define ISO_MAX_VOLUMESET_ID 128

/*! \brief Maximum number of multi file extent licdio supports. */
#define ISO_MAX_MULTIEXTENT 8

/*! String inside frame which identifies an ISO 9660 filesystem. This
    string is the "id" field of an iso9660_pvd_t or an iso9660_svd_t.
*/
extern const char ISO_STANDARD_ID[sizeof("CD001")-1];

#define ISO_STANDARD_ID      "CD001"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum strncpy_pad_check {
  ISO9660_NOCHECK = 0,
  ISO9660_7BIT,
  ISO9660_ACHARS,
  ISO9660_DCHARS
} strncpy_pad_check_t;

PRAGMA_BEGIN_PACKED

/*!
  \brief ISO-9660 shorter-format time structure. See ECMA 9.1.5.

  @see iso9660_dtime
 */
struct  iso9660_dtime_s {
  iso711_t      dt_year;   /**< Number of years since 1900 */
  iso711_t      dt_month;  /**< Has value in range 1..12. Note starts
                              at 1, not 0 like a tm struct. */
  iso711_t      dt_day;    /**< Day of the month from 1 to 31 */
  iso711_t      dt_hour;   /**< Hour of the day from 0 to 23 */
  iso711_t      dt_minute; /**< Minute of the hour from 0 to 59 */
  iso711_t      dt_second; /**< Second of the minute from 0 to 59 */
  iso712_t      dt_gmtoff; /**< GMT values -48 .. + 52 in 15 minute
                              intervals */
} GNUC_PACKED;

typedef struct iso9660_dtime_s  iso9660_dtime_t;

/*!
  \brief ISO-9660 longer-format time structure.

  Section 8.4.26.1 of ECMA 119. All values are encoded as character
  arrays, eg. '1', '9', '5', '5' for the year 1955 (no null terminated
  byte).

  @see iso9660_ltime
 */
struct  iso9660_ltime_s {
  char   lt_year        [ISODCL(   1,   4)];   /**< Add 1900 to value
                                                    for the Julian
                                                    year */
  char   lt_month       [ISODCL(   5,   6)];   /**< Has value in range
                                                  1..12. Note starts
                                                  at 1, not 0 like a
                                                  tm struct. */
  char   lt_day         [ISODCL(   7,   8)];   /**< Day of month: 1..31 */
  char   lt_hour        [ISODCL(   9,   10)];  /**< hour: 0..23 */
  char   lt_minute      [ISODCL(  11,   12)];  /**< minute: 0..59 */
  char   lt_second      [ISODCL(  13,   14)];  /**< second: 0..59 */
  char   lt_hsecond     [ISODCL(  15,   16)];  /**< The value is in
                                                  units of 1/100's of
                                                  a second */
  iso712_t lt_gmtoff;  /**< Offset from Greenwich Mean Time in number
                          of 15 min intervals from -48 (West) to +52
                          (East) recorded according to 7.1.2 numerical
                          value */
} GNUC_PACKED;

typedef struct iso9660_ltime_s  iso9660_ltime_t;
typedef struct iso9660_dir_s    iso9660_dir_t;
typedef struct iso9660_stat_s   iso9660_stat_t;

#include <cdio/rock.h>

/*! \brief Format of an ISO-9660 directory record

 Section 9.1 of ECMA 119.

 This structure may have an odd length depending on how many
 characters there are in the filename!  Some compilers (e.g. on
 Sun3/mc68020) pad the structures to an even length.  For this reason,
 we cannot use sizeof (struct iso_path_table) or sizeof (struct
 iso_directory_record) to compute on disk sizes.  Instead, we use
 offsetof(..., name) and add the name size.  See mkisofs.h of the
 cdrtools package.

  @see iso9660_stat
*/
struct iso9660_dir_s {
  iso711_t         length;            /*! Length of Directory record (9.1.1) */
  iso711_t         xa_length;         /*! XA length if XA is used. Otherwise
                                          zero. (9.1.2)  */
  iso733_t         extent;            /*! LBA of first local block allocated
                                          to the extent */
  iso733_t         size;              /*! data length of File Section. This
                                          does not include the length of
                                          any XA Records. (9.1.2) */
  iso9660_dtime_t  recording_time;    /*! Recording date and time (9.1.3) */
  uint8_t          file_flags;        /*! If no XA then zero. If a directory,
                                        then bits 2,3 and 7 are zero.
                                        (9.1.6) */
  iso711_t         file_unit_size;    /*! File Unit size for the File
                                        Section if the File Section
                                        is recorded in interleaved
                                        mode. Otherwise zero. (9.1.7) */
  iso711_t         interleave_gap;    /*! Interleave Gap size for the
                                        File Section if the File
                                        Section is interleaved. Otherwise
                                        zero. (9.1.8) */
  iso723_t volume_sequence_number;    /*! Ordinal number of the volume
                                          in the Volume Set on which
                                          the Extent described by this
                                          Directory Record is
                                          recorded. (9.1.9) */
/*! MSVC compilers cannot handle a zero sized array in the middle
    of a struct, and iso9660_dir_s is reused within iso9660_pvd_s.
    Therefore, instead of defining:
       iso711_t filename_len;
       char     filename[];
    we leverage the fact that iso711_t and char are the same size
    and use an union. The only gotcha is that the actual string
    payload of filename.str[] starts at 1, not 0. */
  union {
    iso711_t        len;
    char            str[1];
  } filename;
} GNUC_PACKED;

/*!
  \brief ISO-9660 Primary Volume Descriptor.
 */
struct iso9660_pvd_s {
  iso711_t         type;                         /**< ISO_VD_PRIMARY - 1 */
  char             id[5];                        /**< ISO_STANDARD_ID "CD001"
                                                  */
  iso711_t         version;                      /**< value 1 for ECMA 119 */
  char             unused1[1];                   /**< unused - value 0 */
  achar_t          system_id[ISO_MAX_SYSTEM_ID]; /**< each char is an achar */
  dchar_t          volume_id[ISO_MAX_VOLUME_ID]; /**< each char is a dchar */
  uint8_t          unused2[8];                   /**< unused - value 0 */
  iso733_t         volume_space_size;            /**< total number of
                                                    sectors */
  uint8_t          unused3[32];                  /**< unused - value 0 */
  iso723_t         volume_set_size;              /**< often 1 */
  iso723_t         volume_sequence_number;       /**< often 1 */
  iso723_t         logical_block_size;           /**< sector size, e.g. 2048 */
  iso733_t         path_table_size;              /**< bytes in path table */
  iso731_t         type_l_path_table;            /**< first sector of L Path
                                                      Table */
  iso731_t         opt_type_l_path_table;        /**< first sector of optional
                                                    L Path Table */
  iso732_t         type_m_path_table;            /**< first sector of M Path
                                                    table */
  iso732_t         opt_type_m_path_table;        /**< first sector of optional
                                                    M Path table */
  iso9660_dir_t    root_directory_record;        /**< See 8.4.18 and
                                                    section 9.1 of
                                                    ISO 9660 spec. */
  char             root_directory_filename;      /**< Is '\\0' or root
                                                  directory. Also pads previous
                                                  field to 34 bytes */
  dchar_t          volume_set_id[ISO_MAX_VOLUMESET_ID]; /**< Volume Set of
                                                           which the volume is
                                                           a member. See
                                                        section 8.4.19 */
  achar_t          publisher_id[ISO_MAX_PUBLISHER_ID];  /**< Publisher of
                                                         volume. If the first
                                                         character is '_' 0x5F,
                                                         the remaining bytes
                                                         specify a file
                                                         containing the user.
                                                         If all bytes are " "
                                                         (0x20) no publisher
                                                         is specified. See
                                                         section 8.4.20 of
                                                         ECMA 119 */
  achar_t          preparer_id[ISO_MAX_PREPARER_ID]; /**< preparer of
                                                         volume. If the first
                                                         character is '_' 0x5F,
                                                         the remaining bytes
                                                         specify a file
                                                         containing the user.
                                                         If all bytes are " "
                                                         (0x20) no preparer
                                                         is specified.
                                                         See section 8.4.21
                                                         of ECMA 119 */
  achar_t          application_id[ISO_MAX_APPLICATION_ID]; /**< application
                                                         use to create the
                                                         volume. If the first
                                                         character is '_' 0x5F,
                                                         the remaining bytes
                                                         specify a file
                                                         containing the user.
                                                         If all bytes are " "
                                                         (0x20) no application
                                                         is specified.
                                                         See section of 8.4.22
                                                         of ECMA 119 */
  dchar_t          copyright_file_id[37];     /**< Name of file for
                                                 copyright info. If
                                                 all bytes are " "
                                                 (0x20), then no file
                                                 is identified.  See
                                                 section 8.4.23 of ECMA 119
                                                 9660 spec. */
  dchar_t          abstract_file_id[37];      /**< See section 8.4.24 of
                                                 ECMA 119. */
  dchar_t          bibliographic_file_id[37]; /**< See section 7.5 of
                                                 ISO 9660 spec. */
  iso9660_ltime_t  creation_date;             /**< date and time of volume
                                                 creation. See section 8.4.26.1
                                                 of the ISO 9660 spec. */
  iso9660_ltime_t  modification_date;         /**< date and time of the most
                                                 recent modification.
                                                 See section 8.4.27 of the
                                                 ISO 9660 spec. */
  iso9660_ltime_t  expiration_date;           /**< date and time when volume
                                                 expires. See section 8.4.28
                                                 of the ISO 9660 spec. */
  iso9660_ltime_t  effective_date;            /**< date and time when volume
                                                 is effective. See section
                                                 8.4.29 of the ISO 9660
                                                 spec. */
  iso711_t         file_structure_version;    /**< value 1 for ECMA 119 */
  uint8_t           unused4[1];                /**< unused - value 0 */
  char             application_data[512];     /**< Application can put
                                                 whatever it wants here. */
  uint8_t          unused5[653];              /**< Unused - value 0 */
} GNUC_PACKED;

typedef struct iso9660_pvd_s  iso9660_pvd_t;

/*!
  \brief ISO-9660 Supplementary Volume Descriptor.

  This is used for Joliet Extentions and is almost the same as the
  the primary descriptor but two unused fields, "unused1" and "unused3
  become "flags and "escape_sequences" respectively.
*/
struct iso9660_svd_s {
  iso711_t         type;                         /**< ISO_VD_SUPPLEMENTARY - 2
                                                  */
  char             id[5];                        /**< ISO_STANDARD_ID "CD001"
                                                  */
  iso711_t         version;                      /**< value 1 */
  char             flags;                        /**< Section 8.5.3 */
  achar_t          system_id[ISO_MAX_SYSTEM_ID]; /**< Section 8.5.4; each char
                                                    is an achar */
  dchar_t          volume_id[ISO_MAX_VOLUME_ID]; /**< Section 8.5.5; each char
                                                    is a dchar */
  char             unused2[8];
  iso733_t         volume_space_size;            /**< total number of
                                                    sectors */
  char             escape_sequences[32];         /**< Section 8.5.6 */
  iso723_t         volume_set_size;              /**< often 1 */
  iso723_t         volume_sequence_number;       /**< often 1 */
  iso723_t         logical_block_size;           /**< sector size, e.g. 2048 */
  iso733_t         path_table_size;              /**< 8.5.7; bytes in path
                                                    table */
  iso731_t         type_l_path_table;            /**< 8.5.8; first sector of
                                                    little-endian path table */
  iso731_t         opt_type_l_path_table;        /**< 8.5.9; first sector of
                                                    optional little-endian
                                                    path table */
  iso732_t         type_m_path_table;            /**< 8.5.10; first sector of
                                                    big-endian path table */
  iso732_t         opt_type_m_path_table;        /**< 8.5.11; first sector of
                                                    optional big-endian path
                                                    table */
  iso9660_dir_t    root_directory_record;        /**< See section 8.5.12 and
                                                    9.1 of ISO 9660 spec. */
  char             root_directory_filename;      /**< Is '\\0' or root
                                                  directory. Also pads previous
                                                  field to 34 bytes */
  dchar_t          volume_set_id[ISO_MAX_VOLUMESET_ID];    /**< 8.5.13;
                                                              dchars */
  achar_t          publisher_id[ISO_MAX_PUBLISHER_ID]; /**<
                                                          Publisher of volume.
                                                          If the first char-
                                                          aracter is '_' 0x5F,
                                                          the remaining bytes
                                                          specify a file
                                                          containing the user.
                                                          If all bytes are " "
                                                          (0x20) no publisher
                                                          is specified. See
                                                          section 8.5.14 of
                                                          ECMA 119 */
  achar_t          preparer_id[ISO_MAX_PREPARER_ID]; /**<
                                                        Data preparer of
                                                        volume. If the first
                                                        character is '_' 0x5F,
                                                        the remaining bytes
                                                        specify a file
                                                        containing the user.
                                                        If all bytes are " "
                                                        (0x20) no preparer
                                                        is specified.
                                                        See section 8.5.15
                                                        of ECMA 119 */
  achar_t          application_id[ISO_MAX_APPLICATION_ID]; /**< application
                                                         use to create the
                                                         volume. If the first
                                                         character is '_' 0x5F,
                                                         the remaining bytes
                                                         specify a file
                                                         containing the user.
                                                         If all bytes are " "
                                                         (0x20) no application
                                                         is specified.
                                                         See section of 8.5.16
                                                         of ECMA 119 */
  dchar_t          copyright_file_id[37];     /**< Name of file for
                                                 copyright info. If
                                                 all bytes are " "
                                                 (0x20), then no file
                                                 is identified.  See
                                                 section 8.5.17 of ECMA 119
                                                 9660 spec. */
  dchar_t          abstract_file_id[37];      /**< See section 8.5.18 of
                                                 ECMA 119. */
  dchar_t          bibliographic_file_id[37]; /**< See section 8.5.19 of
                                                 ECMA 119. */
  iso9660_ltime_t  creation_date;             /**< date and time of volume
                                                 creation. See section 8.4.26.1
                                                 of the ECMA 119 spec. */
  iso9660_ltime_t  modification_date;         /**< date and time of the most
                                                 recent modification.
                                                 See section 8.4.27 of the
                                                 ECMA 119 spec. */
  iso9660_ltime_t  expiration_date;           /**< date and time when volume
                                                 expires. See section 8.4.28
                                                 of the ECMA 119 spec. */
  iso9660_ltime_t  effective_date;            /**< date and time when volume
                                                 is effective. See section
                                                 8.4.29 of the ECMA 119
                                                 spec. */
  iso711_t         file_structure_version;    /**< value 1 for ECMA 119 */
  uint8_t           unused4[1];                /**< unused - value 0 */
  char             application_data[512];     /**< 8.5.20 Application can put
                                                 whatever it wants here. */
  uint8_t          unused5[653];              /**< Unused - value 0 */
} GNUC_PACKED;

typedef struct iso9660_svd_s  iso9660_svd_t;

PRAGMA_END_PACKED

/*! \brief A data type for a list of ISO9660
  statbuf file pointers returned from the various
  Cdio iso9660 readdir routines.
 */
typedef CdioList_t CdioISO9660FileList_t;

/*! \brief A data type for a list of ISO9660
  statbuf drectory pointer returned from the variious
  Cdio iso9660 readdir routines.
 */
typedef CdioList_t CdioISO9660DirList_t;

/*! \brief Unix stat-like version of iso9660_dir

   The iso9660_stat structure is not part of the ISO-9660
   specification. We use it for our to communicate information
   in a C-library friendly way, e.g struct tm time structures and
   a C-style filename string.

   @see iso9660_dir
*/
struct iso9660_stat_s { /* big endian!! */

  iso_rock_statbuf_t rr;              /**< Rock Ridge-specific fields  */

  struct tm          tm;              /**< time on entry - FIXME merge with
                                         one of entries above, like ctime? */
  uint64_t           size;            /**< total size in bytes */
  uint8_t            extents;         /**< number of multiextents */
                     /**⌵ start logical sector number for each extent */
  lsn_t              lsn[ISO_MAX_MULTIEXTENT];
                     /**⌵ size of each extent */
  uint32_t           extsize[ISO_MAX_MULTIEXTENT];
                     /**⌵ number of sectors allocated for each extent */
  uint32_t           secsize[ISO_MAX_MULTIEXTENT];
  iso9660_xa_t       xa;              /**< XA attributes */
  enum { _STAT_FILE = 1, _STAT_DIR = 2 } type;
  bool               b_xa;
  char               filename[EMPTY_ARRAY_SIZE];    /**< filename */
};

/** A mask used in iso9660_ifs_read_vd which allows what kinds
    of extensions we allow, eg. Joliet, Rock Ridge, etc. */
typedef uint8_t iso_extension_mask_t;

/*! An enumeration for some of the ISO_EXTENSION_* \#defines below. This isn't
    really an enumeration one would really use in a program it is here
    to be helpful in debuggers where wants just to refer to the
    ISO_EXTENSION_*_ names and get something.
  */
extern enum iso_extension_enum_s {
  ISO_EXTENSION_JOLIET_LEVEL1 = 0x01,
  ISO_EXTENSION_JOLIET_LEVEL2 = 0x02,
  ISO_EXTENSION_JOLIET_LEVEL3 = 0x04,
  ISO_EXTENSION_ROCK_RIDGE    = 0x08,
  ISO_EXTENSION_HIGH_SIERRA   = 0x10
} iso_extension_enums;


#define ISO_EXTENSION_ALL           0xFF
#define ISO_EXTENSION_NONE          0x00
#define ISO_EXTENSION_JOLIET     \
  (ISO_EXTENSION_JOLIET_LEVEL1 | \
   ISO_EXTENSION_JOLIET_LEVEL2 | \
   ISO_EXTENSION_JOLIET_LEVEL3 )


/** This is an opaque structure. */
typedef struct _iso9660_s iso9660_t;

  /*! Close previously opened ISO 9660 image and free resources
    associated with the image. Call this when done using using an ISO
    9660 image.

    @param p_iso the ISO-9660 file image to get data from

    @return true is unconditionally returned. If there was an error
    false would be returned. Resources associated with p_iso are
    freed.
  */
  bool iso9660_close (iso9660_t * p_iso);


  /*!
    Open an ISO 9660 image for reading. Maybe in the future we will have
    a mode. NULL is returned on error.

    @param psz_path full path of ISO9660 file.


    @return a IS9660 structure  is unconditionally returned. The caller
    should call iso9660_close() when done.
  */
  iso9660_t *iso9660_open (const char *psz_path /*flags, mode */);

  /*!
    Open an ISO 9660 image for reading allowing various ISO 9660
    extensions.  Maybe in the future we will have a mode. NULL is
    returned on error.

    @see iso9660_open_fuzzy
  */
  iso9660_t *iso9660_open_ext (const char *psz_path,
                               iso_extension_mask_t iso_extension_mask);

  /*! Open an ISO 9660 image for "fuzzy" reading. This means that we
    will try to guess various internal offset based on internal
    checks. This may be useful when trying to read an ISO 9660 image
    contained in a file format that libiso9660 doesn't know natively
    (or knows imperfectly.)

    Some tolerence allowed for positioning the ISO 9660 image. We scan
    for STANDARD_ID and use that to set the eventual offset to adjust
    by (as long as that is <= i_fuzz).

    Maybe in the future we will have a mode. NULL is returned on error.

    @see iso9660_open, @see iso9660_fuzzy_ext
  */
  iso9660_t *iso9660_open_fuzzy (const char *psz_path /*flags, mode */,
                                 uint16_t i_fuzz);

  /*!
    Open an ISO 9660 image for reading with some tolerence for positioning
    of the ISO9660 image. We scan for ISO_STANDARD_ID and use that to set
    the eventual offset to adjust by (as long as that is <= i_fuzz).

    Maybe in the future we will have a mode. NULL is returned on error.

    @see iso9660_open_ext @see iso9660_open_fuzzy
  */
  iso9660_t *iso9660_open_fuzzy_ext (const char *psz_path,
                                     iso_extension_mask_t iso_extension_mask,
                                     uint16_t i_fuzz
                                     /*flags, mode */);

  /*!
    Read the Super block of an ISO 9660 image but determine framesize
    and datastart and a possible additional offset. Generally here we are
    not reading an ISO 9660 image but a CD-Image which contains an ISO 9660
    filesystem.
  */
  bool iso9660_ifs_fuzzy_read_superblock (iso9660_t *p_iso,
                                          iso_extension_mask_t iso_extension_mask,
                                          uint16_t i_fuzz);

  /*!
    Seek to a position and then read i_size blocks.

    @param p_iso the ISO-9660 file image to get data from

    @param ptr place to put returned data. It should be able to store
    a least i_size bytes

    @param start location to start reading from

    @param i_size number of blocks to read. Each block is ISO_BLOCKSIZE bytes
    long.

    @return number of bytes (not blocks) read

  */
  long int iso9660_iso_seek_read (const iso9660_t *p_iso, /*out*/ void *ptr,
                                  lsn_t start, long int i_size);

  /*!
    Read the Primary Volume Descriptor for a CD.
    True is returned if read, and false if there was an error.
  */
  bool iso9660_fs_read_pvd ( const CdIo_t *p_cdio,
                             /*out*/ iso9660_pvd_t *p_pvd );

  /*!
    Read the Primary Volume Descriptor for an ISO 9660 image.
    True is returned if read, and false if there was an error.
  */
  bool iso9660_ifs_read_pvd (const iso9660_t *p_iso,
                             /*out*/ iso9660_pvd_t *p_pvd);

  /*!
    Read the Super block of an ISO 9660 image. This is the
    Primary Volume Descriptor (PVD) and perhaps a Supplemental Volume
    Descriptor if (Joliet) extensions are acceptable.
  */
  bool iso9660_fs_read_superblock (CdIo_t *p_cdio,
                                   iso_extension_mask_t iso_extension_mask);

  /*!
    Read the Super block of an ISO 9660 image. This is the
    Primary Volume Descriptor (PVD) and perhaps a Supplemental Volume
    Descriptor if (Joliet) extensions are acceptable.
  */
  bool iso9660_ifs_read_superblock (iso9660_t *p_iso,
                                    iso_extension_mask_t iso_extension_mask);


/*====================================================
  Time conversion
 ====================================================*/

  /*!
    Set time in format used in ISO 9660 directory index record
    from a Unix time structure.
  */
  void iso9660_set_dtime (const struct tm *tm,
                          /*out*/ iso9660_dtime_t *idr_date);


  /*!
    Set time in format used in ISO 9660 directory index record
    from a Unix time structure. timezone is given as an offset
    correction in minutes.
  */
  void iso9660_set_dtime_with_timezone (const struct tm *p_tm,
                                        int timezone,
                                        /*out*/ iso9660_dtime_t *p_idr_date);

  /*!
    Set "long" time in format used in ISO 9660 primary volume descriptor
    from a Unix time structure. */
  void iso9660_set_ltime (const struct tm *_tm,
                          /*out*/ iso9660_ltime_t *p_pvd_date);

  /*!
    Set "long" time in format used in ISO 9660 primary volume descriptor
    from a Unix time structure. */
  void iso9660_set_ltime_with_timezone (const struct tm *_tm,
                                        int timezone,
                                        /*out*/ iso9660_ltime_t *p_pvd_date);

  /*!
    Get Unix time structure from format use in an ISO 9660 directory index
    record. Even though tm_wday and tm_yday fields are not explicitly in
    idr_date, they are calculated from the other fields.

    If tm is to reflect the localtime, set "b_localtime" true, otherwise
    tm will reported in GMT.
  */
  bool iso9660_get_dtime (const iso9660_dtime_t *idr_date, bool b_localtime,
                          /*out*/ struct tm *tm);


  /*!
    Get "long" time in format used in ISO 9660 primary volume descriptor
    from a Unix time structure.
  */
  bool iso9660_get_ltime (const iso9660_ltime_t *p_ldate,
                          /*out*/ struct tm *p_tm);

  /*====================================================
    Character Classification and String Manipulation
    ====================================================*/
  /*!
    Return true if c is a DCHAR - a character that can appear in an an
    ISO-9600 level 1 directory name. These are the ASCII capital
    letters A-Z, the digits 0-9 and an underscore.
  */
  bool iso9660_is_dchar (int c);

  /*!
    Return true if c is an ACHAR -
    These are the DCHAR's plus some ASCII symbols including the space
    symbol.
  */
  bool iso9660_is_achar (int c);

  /*!
    Convert an ISO-9660 file name which is in the format usually stored
    in a ISO 9660 directory entry into what's usually listed as the
    file name in a listing.  Lowercase name, and remove trailing ;1's
    or .;1's and turn the other ;'s into version numbers.

    @param psz_oldname the ISO-9660 filename to be translated.
    @param psz_newname returned string. The caller allocates this and
    it should be at least the size of psz_oldname.
    @return length of the translated string is returned.
  */
  int iso9660_name_translate(const char *psz_oldname,
                             /*out*/ char *psz_newname);

  /*!
    Convert an ISO-9660 file name which is in the format usually stored
    in a ISO 9660 directory entry into what's usually listed as the
    file name in a listing.  Lowercase name if no Joliet Extension
    interpretation. Remove trailing ;1's or .;1's and turn the other
    ;'s into version numbers.

    @param psz_oldname the ISO-9660 filename to be translated.
    @param psz_newname returned string. The caller allocates this and
    it should be at least the size of psz_oldname.
    @param i_joliet_level 0 if not using Joliet Extension. Otherwise the
    Joliet level.
    @return length of the translated string is returned. It will be no greater
    than the length of psz_oldname.
  */
  int iso9660_name_translate_ext(const char *psz_oldname, char *psz_newname,
                                 uint8_t i_joliet_level);

  /*!
    Pad string src with spaces to size len and copy this to dst. If
    len is less than the length of src, dst will be truncated to the
    first len characters of src.

    src can also be scanned to see if it contains only ACHARs, DCHARs,
    7-bit ASCII chars depending on the enumeration _check.

    In addition to getting changed, dst is the return value.
    Note: this string might not be NULL terminated.
  */
  char *iso9660_strncpy_pad(char dst[], const char src[], size_t len,
                            enum strncpy_pad_check _check);

  /*=====================================================================
    File and Directory Names
    ======================================================================*/

  /*!
    Check that psz_path is a valid ISO-9660 directory name.

    A valid directory name should not start out with a slash (/),
    dot (.) or null byte, should be less than 37 characters long,
    have no more than 8 characters in a directory component
    which is separated by a /, and consist of only DCHARs.

    True is returned if psz_path is valid.
  */
  bool iso9660_dirname_valid_p (const char psz_path[]);

  /*!
    Take psz_path and a version number and turn that into a ISO-9660
    pathname.  (That's just the pathname followd by ";" and the version
    number. For example, mydir/file.ext -> MYDIR/FILE.EXT;1 for version
    1. The resulting ISO-9660 pathname is returned.
  */
  char *iso9660_pathname_isofy (const char psz_path[], uint16_t i_version);

  /*!
    Check that psz_path is a valid ISO-9660 pathname.

    A valid pathname contains a valid directory name, if one appears and
    the filename portion should be no more than 8 characters for the
    file prefix and 3 characters in the extension (or portion after a
    dot). There should be exactly one dot somewhere in the filename
    portion and the filename should be composed of only DCHARs.

    True is returned if psz_path is valid.
  */
  bool iso9660_pathname_valid_p (const char psz_path[]);

/*=====================================================================
  directory tree
======================================================================*/

void
iso9660_dir_init_new (void *dir, uint32_t self, uint32_t ssize,
                      uint32_t parent, uint32_t psize,
                      const time_t *dir_time);

void
iso9660_dir_init_new_su (void *dir, uint32_t self, uint32_t ssize,
                         const void *ssu_data, unsigned int ssu_size,
                         uint32_t parent, uint32_t psize,
                         const void *psu_data, unsigned int psu_size,
                         const time_t *dir_time);

void
iso9660_dir_add_entry_su (void *dir, const char filename[], uint32_t extent,
                          uint32_t size, uint8_t file_flags,
                          const void *su_data,
                          unsigned int su_size, const time_t *entry_time);

unsigned int
iso9660_dir_calc_record_size (unsigned int namelen, unsigned int su_len);

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   @param p_cdio the CD object to read from

   @return stat_t of entry if we found lsn, or NULL otherwise.
   Caller must free return value using iso9660_stat_free().
 */
#define iso9660_fs_find_lsn  iso9660_find_fs_lsn
iso9660_stat_t *iso9660_fs_find_lsn(CdIo_t *p_cdio, lsn_t i_lsn);


/*!
   Given a directory pointer, find the filesystem entry that contains
   LSN and return information about it.

   @param p_cdio the ISO-9660 file image to get data from.

   @param i_lsn the LSN to find

   @param ppsz_full_filename the place to store the name of the path that has LSN.
   On entry this should point to NULL. If not, the value will be freed.
   On exit a value is malloc'd and the caller is responsible for
   freeing the result.

   @return stat_t of entry if we found lsn, or NULL otherwise.
   Caller must free return value using iso9660_stat_free().
 */
iso9660_stat_t *iso9660_fs_find_lsn_with_path(CdIo_t *p_cdio, lsn_t i_lsn,
                                              /*out*/ char **ppsz_full_filename);

/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   @param p_iso the ISO-9660 file image to get data from.

   @param i_lsn the LSN to find

   @return stat_t of entry if we found lsn, or NULL otherwise.
   Caller must free return value using iso9660_stat_free().
 */
iso9660_stat_t *iso9660_ifs_find_lsn(iso9660_t *p_iso, lsn_t i_lsn);


/*!
   Given a directory pointer, find the filesystem entry that contains
   lsn and return information about it.

   @param p_iso pointer to iso_t

   @param i_lsn LSN to find

   @param ppsz_path  full path of lsn filename. On entry *ppsz_path should be
   NULL. On return it will be allocated an point to the full path of the
   file at lsn or NULL if the lsn is not found. You should deallocate
   *ppsz_path when you are done using it.

   @return stat_t of entry if we found lsn, or NULL otherwise.
   Caller must free return value using iso9660_stat_free().
 */
iso9660_stat_t *iso9660_ifs_find_lsn_with_path(iso9660_t *p_iso,
                                               lsn_t i_lsn,
                                               /*out*/ char **ppsz_path);

/*!
  Free the passed iso9660_stat_t structure.

  @param p_stat iso9660 stat buffer to free.

 */
void iso9660_stat_free(iso9660_stat_t *p_stat);

/*!
  Return file status for psz_path. NULL is returned on error.

  @param p_cdio the CD object to read from

  @param psz_path filename path to look up and get information about

  @return ISO 9660 file information. The caller must free the returned
  result using iso9660_stat_free().


  Important note:

  You make get different results looking up "/" versus "/." and the
  latter may give more complete information. "/" will take information
  from the PVD only, whereas "/." will force a directory read of "/" and
  find "." and in that Rock-Ridge information might be found which fills
  in more stat information. Ideally iso9660_fs_stat should be fixed.
  Patches anyone?
 */
iso9660_stat_t *iso9660_fs_stat (CdIo_t *p_cdio, const char psz_path[]);


/*!
  Return file status for path name psz_path. NULL is returned on error.
  pathname version numbers in the ISO 9660 name are dropped, i.e. ;1
  is removed and if level 1 ISO-9660 names are lowercased.

  @param p_cdio the CD object to read from

  @param psz_path filename path to look up and get information about

  @return ISO 9660 file information.  The caller must free the
  returned result using iso9660_stat_free().

 */
iso9660_stat_t *iso9660_fs_stat_translate (CdIo_t *p_cdio,
                                           const char psz_path[]);
/*!

  @param p_iso the ISO-9660 file image to get data from

  @param psz_path path the look up

  @return file status for pathname. NULL is returned on error.
  The caller must free the returned result using iso9660_stat_free().
 */
iso9660_stat_t *iso9660_ifs_stat (iso9660_t *p_iso, const char psz_path[]);


/*!
  @param p_iso the ISO-9660 file image to get data from

  @param psz_path filename path translate

  @return file status for path name psz_path. NULL is returned on
  error.  pathname version numbers in the ISO 9660 name are dropped,
  i.e. ;1 is removed and if level 1 ISO-9660 names are lowercased.
  The caller must free the returned result using iso9660_stat_free().
 */
iso9660_stat_t *iso9660_ifs_stat_translate (iso9660_t *p_iso,
                                            const char psz_path[]);


/*!
  Create a new data structure to hold a list of
  ISO9660 statbuf-entry pointers for the files inside
  a directory.

  @return allocated list. Free with iso9660_filelist_free()
*/
CdioISO9660FileList_t * iso9660_filelist_new(void);


/*!
  Create a new data structure to hold a list of
  ISO9660 statbuf entries for directory
  pointers for the files inside a directory.

  @return allocated list. Free with iso9660_dirlist_free()
*/
CdioISO9660DirList_t * iso9660_dirlist_new(void);



/*!
  Free the passed CdioISOC9660FileList_t structure.
*/
void iso9660_filelist_free(CdioISO9660FileList_t *p_filelist);


/*!
  Free the passed CdioISOC9660Dirlist_t structure.
*/
void iso9660_dirlist_free(CdioISO9660DirList_t *p_filelist);


/*!
  Read psz_path (a directory) and return a list of iso9660_stat_t
  pointers for the files inside that directory.

  @param p_cdio the CD object to read from

  @param psz_path path the read the directory from.

  @return file status for psz_path. The caller must free the
  The caller must free the returned result using iso9660_stat_free().
*/
CdioList_t * iso9660_fs_readdir (CdIo_t *p_cdio, const char psz_path[]);

/*!
  Read psz_path (a directory) and return a list of iso9660_stat_t
  pointers for the files inside that directory.

  @param p_iso the ISO-9660 file image to get data from

  @param psz_path path the read the directory from.

  @return file status for psz_path. The caller must free the
  The caller must free the returned result using iso9660_stat_free().
*/
CdioList_t * iso9660_ifs_readdir (iso9660_t *p_iso, const char psz_path[]);

/*!
  Return the PVD's application ID.

  @param p_pvd the PVD to get data from

  @return  the application id.
  NULL is returned if there is some problem in getting this.
  The caller must free the resturned result using free() if
  not null.
*/
char * iso9660_get_application_id(iso9660_pvd_t *p_pvd);

/*!
  Return the PVD's application ID.

  @param p_iso the ISO-9660 file image to get data from

  @param p_psz_app_id the application id set on success.

  NULL is returned if there is some problem in getting this.
  The caller must free the resturned result using free() if
  not null.
*/
bool iso9660_ifs_get_application_id(iso9660_t *p_iso,
                                    /*out*/ cdio_utf8_t **p_psz_app_id);

/*!
  Return the Joliet level recognized for p_iso.
*/
uint8_t iso9660_ifs_get_joliet_level(iso9660_t *p_iso);

uint8_t iso9660_get_dir_len(const iso9660_dir_t *p_idr);

#ifdef FIXME
uint8_t iso9660_get_dir_size(const iso9660_dir_t *p_idr);

lsn_t iso9660_get_dir_extent(const iso9660_dir_t *p_idr);
#endif

  /*!
    Return the directory name stored in the iso9660_dir_t

    A string is allocated: the caller must deallocate. This routine
    can return NULL if memory allocation fails.
  */
  char * iso9660_dir_to_name (const iso9660_dir_t *p_iso9660_dir);

  /*!
    Returns a POSIX mode for a given p_iso_dirent.
  */
  mode_t iso9660_get_posix_filemode(const iso9660_stat_t *p_iso_dirent);

  /*!
    Return a string containing the preparer id with trailing
    blanks removed.
  */
  char *iso9660_get_preparer_id(const iso9660_pvd_t *p_pvd);

  /*!
    Get the preparer ID.  psz_preparer_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  bool iso9660_ifs_get_preparer_id(iso9660_t *p_iso,
                                   /*out*/ cdio_utf8_t **p_psz_preparer_id);

  /*!
    Return a string containing the PVD's publisher id with trailing
    blanks removed.
  */
  char *iso9660_get_publisher_id(const iso9660_pvd_t *p_pvd);

  /*!
    Get the publisher ID.  psz_publisher_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  bool iso9660_ifs_get_publisher_id(iso9660_t *p_iso,
                                    /*out*/ cdio_utf8_t **p_psz_publisher_id);

  uint8_t iso9660_get_pvd_type(const iso9660_pvd_t *p_pvd);

  const char * iso9660_get_pvd_id(const iso9660_pvd_t *p_pvd);

  int iso9660_get_pvd_space_size(const iso9660_pvd_t *p_pvd);

  int iso9660_get_pvd_block_size(const iso9660_pvd_t *p_pvd) ;

  /*! Return the primary volume id version number (of pvd).
    If there is an error 0 is returned.
  */
  int iso9660_get_pvd_version(const iso9660_pvd_t *pvd) ;

  /*!
    Return a string containing the PVD's system id with trailing
    blanks removed.
  */
  char *iso9660_get_system_id(const iso9660_pvd_t *p_pvd);

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
  bool_3way_t iso9660_have_rr(iso9660_t *p_iso, uint64_t u_file_limit);

  /*!
    Get the system ID.  psz_system_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  bool iso9660_ifs_get_system_id(iso9660_t *p_iso,
                                 /*out*/ cdio_utf8_t **p_psz_system_id);


  /*! Return the LSN of the root directory for pvd.
    If there is an error CDIO_INVALID_LSN is returned.
  */
  lsn_t iso9660_get_root_lsn(const iso9660_pvd_t *p_pvd);

  /*!
    Get the volume ID in the PVD.  psz_volume_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  char *iso9660_get_volume_id(const iso9660_pvd_t *p_pvd);

  /*!
    Get the volume ID in the PVD.  psz_volume_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  bool iso9660_ifs_get_volume_id(iso9660_t *p_iso,
                                 /*out*/ cdio_utf8_t **p_psz_volume_id);

  /*!
    Return the volumeset ID in the PVD.
    NULL is returned if there is some problem in getting this.
  */
  char *iso9660_get_volumeset_id(const iso9660_pvd_t *p_pvd);

  /*!
    Get the volumeset ID.  psz_systemset_id is set to NULL if there
    is some problem in getting this and false is returned.
  */
  bool iso9660_ifs_get_volumeset_id(iso9660_t *p_iso,
                                    /*out*/ cdio_utf8_t **p_psz_volumeset_id);

  /* pathtable */

  /*! Zero's out pathable. Do this first. */
  void iso9660_pathtable_init (void *pt);

  unsigned int iso9660_pathtable_get_size (const void *pt);

  uint16_t iso9660_pathtable_l_add_entry (void *pt, const char name[],
                                          uint32_t extent, uint16_t parent);

  uint16_t iso9660_pathtable_m_add_entry (void *pt, const char name[],
                                          uint32_t extent, uint16_t parent);

  /**=====================================================================
     Volume Descriptors
     ======================================================================*/

  void iso9660_set_pvd (void *pd, const char volume_id[],
                        const char application_id[],
                        const char publisher_id[], const char preparer_id[],
                        uint32_t iso_size, const void *root_dir,
                        uint32_t path_table_l_extent,
                        uint32_t path_table_m_extent,
                        uint32_t path_table_size, const time_t *pvd_time);

  void iso9660_set_evd (void *pd);

  /*!
    Return true if ISO 9660 image has extended attrributes (XA).
  */
  bool iso9660_ifs_is_xa (const iso9660_t * p_iso);


#ifndef DO_NOT_WANT_COMPATIBILITY
/** For compatibility with < 0.77 */
#define iso9660_isdchar       iso9660_is_dchar
#define iso9660_isachar       iso9660_is_achar
#endif /*DO_NOT_WANT_COMPATIBILITY*/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef ISODCL
#endif /* CDIO_ISO9660_H_ */

/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
