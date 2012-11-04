/*
    Copyright (C) 2005, 2006 2008, 2012 Rocky Bernstein <rocky@gnu.org>

    See also rock.c by Eric Youngdale (1993) from GNU/Linux 
    This is Copyright 1993 Yggdrasil Computing, Incorporated

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
   \file rock.h 
   \brief Things related to the Rock Ridge Interchange Protocol (RRIP)

   Applications will probably not include this directly but via 
   the iso9660.h header.
*/


#ifndef CDIO_ROCK_H_
#define CDIO_ROCK_H_

#include <cdio/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*! An enumeration for some of the ISO_ROCK_* \#defines below. This isn't
  really an enumeration one would really use in a program it is to
  be helpful in debuggers where wants just to refer to the ISO_ROCK_*
  names and get something.
*/
extern enum iso_rock_enums {
  ISO_ROCK_IRUSR  = 000400,   /**< read permission (owner) */
  ISO_ROCK_IWUSR  = 000200,   /**< write permission (owner) */
  ISO_ROCK_IXUSR  = 000100,   /**< execute permission (owner) */
  ISO_ROCK_IRGRP  = 000040,   /**< read permission (group) */
  ISO_ROCK_IWGRP  = 000020,   /**< write permission (group) */
  ISO_ROCK_IXGRP  = 000010,   /**< execute permission (group) */
  ISO_ROCK_IROTH  = 000004,   /**< read permission (other) */
  ISO_ROCK_IWOTH  = 000002,   /**< write permission (other) */
  ISO_ROCK_IXOTH  = 000001,   /**< execute permission (other) */

  ISO_ROCK_ISUID  = 004000,   /**< set user ID on execution */
  ISO_ROCK_ISGID  = 002000,   /**< set group ID on execution */
  ISO_ROCK_ISVTX  = 001000,   /**< save swapped text even after use */

  ISO_ROCK_ISSOCK = 0140000,  /**< socket */
  ISO_ROCK_ISLNK  = 0120000,  /**< symbolic link */
  ISO_ROCK_ISREG  = 0100000,  /**< regular */
  ISO_ROCK_ISBLK  = 060000,   /**< block special */
  ISO_ROCK_ISCHR  = 020000,   /**< character special */
  ISO_ROCK_ISDIR  = 040000,   /**< directory */
  ISO_ROCK_ISFIFO = 010000    /**< pipe or FIFO */
} iso_rock_enums;

#define ISO_ROCK_IRUSR    000400  /** read permission (owner) */
#define ISO_ROCK_IWUSR    000200  /** write permission (owner) */
#define ISO_ROCK_IXUSR    000100  /** execute permission (owner) */
#define ISO_ROCK_IRGRP    000040  /** read permission (group) */
#define ISO_ROCK_IWGRP    000020  /** write permission (group) */
#define ISO_ROCK_IXGRP    000010  /** execute permission (group) */
#define ISO_ROCK_IROTH    000004  /** read permission (other) */
#define ISO_ROCK_IWOTH    000002  /** write permission (other) */
#define ISO_ROCK_IXOTH    000001  /** execute permission (other) */

#define ISO_ROCK_ISUID    004000  /** set user ID on execution */
#define ISO_ROCK_ISGID    002000  /** set group ID on execution */
#define ISO_ROCK_ISVTX    001000  /** save swapped text even after use */

#define ISO_ROCK_ISSOCK  0140000  /** socket */
#define ISO_ROCK_ISLNK   0120000  /** symbolic link */
#define ISO_ROCK_ISREG   0100000  /** regular */
#define ISO_ROCK_ISBLK    060000  /** block special */
#define ISO_ROCK_ISCHR    020000  /** character special */
#define ISO_ROCK_ISDIR    040000  /** directory */
#define ISO_ROCK_ISFIFO   010000  /** pipe or FIFO */

/** Enforced file locking (shared w/set group ID) */
#define ISO_ROCK_ENFMT ISO_ROCK_ISGID

PRAGMA_BEGIN_PACKED

/*! The next two structs are used by the system-use-sharing protocol
   (SUSP), in which the Rock Ridge extensions are embedded.  It is
   quite possible that other extensions are present on the disk, and
   this is fine as long as they all use SUSP. */

/*! system-use-sharing protocol */
typedef struct iso_su_sp_s{
  unsigned char magic[2];
  uint8_t       skip;
} GNUC_PACKED iso_su_sp_t;

/*! system-use extension record */
typedef struct iso_su_er_s {
  iso711_t      len_id;  /**< Identifier length. Value 10?. */
  unsigned char len_des;
  unsigned char len_src;
  iso711_t      ext_ver; /**< Extension version. Value 1? */
  char data[EMPTY_ARRAY_SIZE];
} GNUC_PACKED iso_su_er_t;

typedef struct iso_su_ce_s {
  char extent[8];
  char offset[8];
  char size[8];
} iso_su_ce_t;

/*! POSIX file attributes, PX. See Rock Ridge Section 4.1.2 */
typedef struct iso_rock_px_s {
  iso733_t st_mode;       /*! file mode permissions; same as st_mode 
                            of POSIX:5.6.1 */
  iso733_t st_nlinks;     /*! number of links to file; same as st_nlinks
                            of POSIX:5.6.1 */
  iso733_t st_uid;        /*! user id owner of file; same as st_uid
                            of POSIX:5.6.1 */
  iso733_t st_gid;        /*! group id of file; same as st_gid of 
                            of POSIX:5.6.1 */
} GNUC_PACKED iso_rock_px_t ;

/*! POSIX device number, PN. A PN is mandatory if the file type
  recorded in the "PX" File Mode field for a Directory Record
  indicates a character or block device (ISO_ROCK_ISCHR |
  ISO_ROCK_ISBLK).  This entry is ignored for other (non-Direcotry)
  file types. No more than one "PN" is recorded in the System Use Area
  of a Directory Record.

  See Rock Ridge Section 4.1.2 */
typedef struct iso_rock_pn_s {
  iso733_t dev_high;     /**< high-order 32 bits of the 64 bit device number.
                            7.2.3 encoded */
  iso733_t dev_low;      /**< low-order 32 bits of the 64 bit device number.
                            7.2.3 encoded */
} GNUC_PACKED iso_rock_pn_t ;

/*! These are the bits and their meanings for flags in the SL structure. */
typedef enum {
  ISO_ROCK_SL_CONTINUE = 1,
  ISO_ROCK_SL_CURRENT  = 2,
  ISO_ROCK_SL_PARENT   = 4,
  ISO_ROCK_SL_ROOT     = 8
} iso_rock_sl_flag_t;

#define ISO_ROCK_SL_CONTINUE 1
#define ISO_ROCK_SL_CURRENT  2
#define ISO_ROCK_SL_PARENT   4
#define ISO_ROCK_SL_ROOT     8

typedef struct iso_rock_sl_part_s {
  uint8_t flags;
  uint8_t len;
  char text[EMPTY_ARRAY_SIZE];
} GNUC_PACKED iso_rock_sl_part_t ;

/*! Symbolic link. See Rock Ridge Section 4.1.3 */
typedef struct iso_rock_sl_s {
  unsigned char flags;
  iso_rock_sl_part_t link;
} GNUC_PACKED iso_rock_sl_t ;

/*! Alternate name. See Rock Ridge Section 4.1.4 */

/*! These are the bits and their meanings for flags in the NM structure. */
typedef enum {
  ISO_ROCK_NM_CONTINUE = 1,
  ISO_ROCK_NM_CURRENT  = 2,
  ISO_ROCK_NM_PARENT   = 4,
} iso_rock_nm_flag_t;

#define ISO_ROCK_NM_CONTINUE 1
#define ISO_ROCK_NM_CURRENT  2
#define ISO_ROCK_NM_PARENT   4


typedef struct iso_rock_nm_s {
  unsigned char flags;
  char name[EMPTY_ARRAY_SIZE];
} GNUC_PACKED iso_rock_nm_t ;

/*! Child link. See Section 4.1.5.1 */
typedef struct iso_rock_cl_s {
  char location[1];
} GNUC_PACKED iso_rock_cl_t ;

/*! Parent link. See Section 4.1.5.2 */
typedef struct iso_rock_pl_s {
  char location[1];
} GNUC_PACKED iso_rock_pl_t ;

/*! These are the bits and their meanings for flags in the TF structure. */
typedef enum {
  ISO_ROCK_TF_CREATE     =  1,
  ISO_ROCK_TF_MODIFY     =  2,
  ISO_ROCK_TF_ACCESS     =  4,
  ISO_ROCK_TF_ATTRIBUTES =  8,
  ISO_ROCK_TF_BACKUP     =  16,
  ISO_ROCK_TF_EXPIRATION =  32,
  ISO_ROCK_TF_EFFECTIVE  =  64,
  ISO_ROCK_TF_LONG_FORM  = 128
} iso_rock_tf_flag_t;

/* These are the bits and their meanings for flags in the TF structure. */
#define ISO_ROCK_TF_CREATE      1
#define ISO_ROCK_TF_MODIFY      2
#define ISO_ROCK_TF_ACCESS      4
#define ISO_ROCK_TF_ATTRIBUTES  8
#define ISO_ROCK_TF_BACKUP     16
#define ISO_ROCK_TF_EXPIRATION 32
#define ISO_ROCK_TF_EFFECTIVE  64
#define ISO_ROCK_TF_LONG_FORM 128

/*! Time stamp(s) for a file. See Rock Ridge Section 4.1.6 */
typedef struct iso_rock_tf_s {
  uint8_t flags; /**< See ISO_ROCK_TF_* bits above. */
  uint8_t time_bytes[EMPTY_ARRAY_SIZE]; /**< A homogenious array of
                                           iso9660_ltime_t or
                                           iso9660_dtime_t entries
                                           depending on flags &
                                           ISO_ROCK_TF_LONG_FORM. Lacking
                                           a better method, we store
                                           this as an array of bytes
                                           and a cast to the
                                           appropriate type will have
                                           to be made before
                                           extraction.  */
} GNUC_PACKED iso_rock_tf_t ;

/*! File data in sparse format. See Rock Ridge Section 4.1.7 */
typedef struct iso_rock_sf_s {
  iso733_t virtual_size_high; /**< high-order 32 bits of virtual size */
  iso733_t virtual_size_low;  /**< low-order 32 bits of virtual size */
  uint8_t   table_depth;
} GNUC_PACKED iso_rock_sf_t ;

typedef struct iso_extension_record_s {
  char signature[2];   /**< signature word; either 'SP', 'CE', 'ER', 'RR',
                          'PX', 'PN', 'SL', 'NM', 'CL', 'PL', 'TF', or 
                          'ZF' */
  iso711_t len;        /**< length of system-user area - 44 for PX
                          20 for PN, 5+strlen(text) for SL, 21 for 
                          SF, etc. */
  iso711_t version;    /**< version number - value 1 */
  union {
    iso_su_sp_t    SP;  /**< system-use-sharing protocol - not
                          strictly part of Rock Ridge */
    iso_su_er_t    ER;  /**< system-use extension packet - not
                           strictly part of Rock Ridge */
    iso_su_ce_t    CE;  /**< system-use -  strictly part of Rock Ridge */
    iso_rock_px_t  PX;  /**< Rock Ridge POSIX file attributes */
    iso_rock_pn_t  PN;  /**< Rock Ridge POSIX device number */
    iso_rock_sl_t  SL;  /**< Rock Ridge symbolic link */
    iso_rock_nm_t  NM;  /**< Rock Ridge alternate name */
    iso_rock_cl_t  CL;  /**< Rock Ridge child link */
    iso_rock_pl_t  PL;  /**< Rock Ridge parent link */
    iso_rock_tf_t  TF;  /**< Rock Ridge timestamp(s) for a file */
  } u;
} GNUC_PACKED iso_extension_record_t;

typedef struct iso_rock_time_s {
  bool          b_used;     /**< If true, field has been set and  is valid. 
                               Otherwise remaning fields are meaningless. */
  bool          b_longdate;  /**< If true date format is a iso9660_ltime_t. 
                               Otherwise date is iso9660_dtime_t */
  union 
  {
    iso9660_ltime_t ltime;
    iso9660_dtime_t dtime;
  } t;
} GNUC_PACKED iso_rock_time_t;

typedef struct iso_rock_statbuf_s {
  bool_3way_t   b3_rock;              /**< has Rock Ridge extension. 
                                         If "yep", then the fields
                                         are used.
                                      */
  posix_mode_t  st_mode;              /**< protection */
  posix_nlink_t st_nlinks;            /**< number of hard links */
  posix_uid_t   st_uid;               /**< user ID of owner */
  posix_gid_t   st_gid;               /**< group ID of owner */
  uint8_t       s_rock_offset;
  int           i_symlink;            /**< size of psz_symlink */
  int           i_symlink_max;        /**< max allocated to psz_symlink */
  char         *psz_symlink;          /**< if symbolic link, name
                                         of pointed to file.  */
  iso_rock_time_t create;             /**< create time See ISO 9660:9.5.4. */
  iso_rock_time_t modify;             /**< time of last modification
                                         ISO 9660:9.5.5. st_mtime field of 
                                         POSIX:5.6.1. */
  iso_rock_time_t access;             /**< time of last file access st_atime 
                                         field of POSIX:5.6.1. */
  iso_rock_time_t attributes;         /**< time of last attribute change.
                                         st_ctime field of POSIX:5.6.1. */
  iso_rock_time_t backup;             /**< time of last backup. */
  iso_rock_time_t expiration;         /**< time of expiration; See ISO 
                                         9660:9.5.6. */
  iso_rock_time_t effective;          /**< Effective time; See ISO 9660:9.5.7.
                                       */
  uint32_t i_rdev;                    /**< the upper 16-bits is major device 
                                         number, the lower 16-bits is the
                                         minor device number */

} iso_rock_statbuf_t;
  
PRAGMA_END_PACKED

/*! return length of name field; 0: not found, -1: to be ignored */
int get_rock_ridge_filename(iso9660_dir_t * de, /*out*/ char * retname, 
                            /*out*/ iso9660_stat_t *p_stat);

  int parse_rock_ridge_stat(iso9660_dir_t *de, /*out*/ iso9660_stat_t *p_stat);

  /*!
    Returns POSIX mode bitstring for a given file.
  */
  mode_t 
  iso9660_get_posix_filemode_from_rock(const iso_rock_statbuf_t *rr);

/*!
  Returns a string which interpreting the POSIX mode st_mode. 
  For example:
  \verbatim
  drwxrws---
  -rw---Sr--
  lrwxrwxrwx
  \endverbatim
  
  A description of the characters in the string follows
  The 1st character is either "d" if the entry is a directory, "l" is
  a symbolic link or "-" if neither.
  
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
const char *iso9660_get_rock_attr_str(posix_mode_t st_mode);

/** These variables are not used, but are defined to facilatate debugging
    by letting us use enumerations values (which also correspond to 
    \#define's inside a debugged program.
 */
extern iso_rock_nm_flag_t iso_rock_nm_flag;
extern iso_rock_sl_flag_t iso_rock_sl_flag;
extern iso_rock_tf_flag_t iso_rock_tf_flag;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_ROCK_H_ */

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
