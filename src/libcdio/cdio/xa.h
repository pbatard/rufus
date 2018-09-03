/*
    Copyright (C) 2003, 2004, 2005, 2006, 2008, 2012
    Rocky Bernstein <rocky@gnu.org>
    Copyright (C) 2000 Herbert Valerio Riedel <hvr@gnu.org>

    See also iso9660.h by Eric Youngdale (1993) and in cdrtools. These are 

    Copyright 1993 Yggdrasil Computing, Incorporated
    Copyright (c) 1999,2000 J. Schilling

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
   \file xa.h 
   \brief Things related to the ISO-9660 XA (Extended Attributes) format

   Applications will probably not include this directly but via 
   the iso9660.h header.
*/


#ifndef CDIO_XA_H_
#define CDIO_XA_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /*! An enumeration for some of the XA_* \#defines below. This isn't
    really an enumeration one would really use in a program it is to
    be helpful in debuggers where wants just to refer to the XA_*
    names and get something.
  */
  typedef enum {
    ISO_XA_MARKER_OFFSET =   1024,
    XA_PERM_RSYS =         0x0001,  /**< System Group Read */
    XA_PERM_XSYS =         0x0004,  /**< System Group Execute */
    
    XA_PERM_RUSR =         0x0010,  /**< User (owner) Read */
    XA_PERM_XUSR =         0x0040,  /**< User (owner) Execute */
    
    XA_PERM_RGRP =         0x0100,  /**< Group Read */
    XA_PERM_XGRP =         0x0400,  /**< Group Execute */
    
    XA_PERM_ROTH =         0x1000,  /**< Other (world) Read */
    XA_PERM_XOTH =         0x4000,  /**< Other (world) Execute */
    
    XA_ATTR_MODE2FORM1  =   (1 << 11),
    XA_ATTR_MODE2FORM2  =   (1 << 12),
    XA_ATTR_INTERLEAVED =   (1 << 13),
    XA_ATTR_CDDA        =   (1 << 14),
    XA_ATTR_DIRECTORY   =   (1 << 15),
    
    XA_PERM_ALL_READ    =   (XA_PERM_RUSR | XA_PERM_RSYS | XA_PERM_RGRP),
    XA_PERM_ALL_EXEC    =   (XA_PERM_XUSR | XA_PERM_XSYS | XA_PERM_XGRP),
    XA_PERM_ALL_ALL     =   (XA_PERM_ALL_READ | XA_PERM_ALL_EXEC),
    
    XA_FORM1_DIR  = (XA_ATTR_DIRECTORY | XA_ATTR_MODE2FORM1 | XA_PERM_ALL_ALL),
    XA_FORM1_FILE =  (XA_ATTR_MODE2FORM1 | XA_PERM_ALL_ALL),
    XA_FORM2_FILE =  (XA_ATTR_MODE2FORM2 | XA_PERM_ALL_ALL)
  } xa_misc_enum_t;
  
extern const char ISO_XA_MARKER_STRING[8];

/*! \brief "Extended Architecture" according to the Philips Yellow Book.
 
CD-ROM EXtended Architecture is a modification to the CD-ROM
specification that defines two new types of sectors.  CD-ROM XA was
developed jointly by Sony, Philips, and Microsoft, and announced in
August 1988. Its specifications were published in an extension to the
Yellow Book.  CD-i, Photo CD, Video CD and CD-EXTRA have all
subsequently been based on CD-ROM XA.

CD-XA defines another way of formatting sectors on a CD-ROM, including
headers in the sectors that describe the type (audio, video, data) and
some additional info (markers, resolution in case of a video or audio
sector, file numbers, etc).

The data written on a CD-XA is consistent with and can be in ISO-9660
file system format and therefore be readable by ISO-9660 file system
translators. But also a CD-I player can also read CD-XA discs even if
its own `Green Book' file system only resembles ISO 9660 and isn't
fully compatible. 

 Note structure is big-endian.
*/
typedef struct iso9660_xa_s
{
  uint16_t group_id;      /**< 0 */
  uint16_t user_id;       /**< 0 */
  uint16_t attributes;    /**< XA_ATTR_ */ 
  char     signature[2];  /**< { 'X', 'A' } */
  uint8_t  filenum;       /**< file number, see also XA subheader */
  uint8_t  reserved[5];   /**< zero */
} GNUC_PACKED iso9660_xa_t;
  
  
  /*!
    Returns POSIX mode bitstring for a given file.
  */
  posix_mode_t iso9660_get_posix_filemode_from_xa(uint16_t i_perms);

/*!
  Returns a string interpreting the extended attribute xa_attr. 
  For example:
  \verbatim
  d---1xrxrxr
  ---2--r-r-r
  -a--1xrxrxr
  \endverbatim
  
  A description of the characters in the string follows.
  The 1st character is either "d" if the entry is a directory, or "-" if not
  The 2nd character is either "a" if the entry is CDDA (audio), or "-" if not
  The 3rd character is either "i" if the entry is interleaved, or "-" if not
  The 4th character is either "2" if the entry is mode2 form2 or "-" if not
  The 5th character is either "1" if the entry is mode2 form1 or "-" if not
  Note that an entry will either be in mode2 form1 or mode form2. That
  is you will either see "2-" or "-1" in the 4th & 5th positions.
  
  The 6th and 7th characters refer to permissions for a user while the
  the 8th and 9th characters refer to permissions for a group while, and 
  the 10th and 11th characters refer to permissions for everyone. 
  
  In each of these pairs the first character (6, 8, 10) is "x" if the 
  entry is executable. For a directory this means the directory is
  allowed to be listed or "searched".
  The second character of a pair (7, 9, 11) is "r" if the entry is allowed
  to be read. 
*/
const char *
iso9660_get_xa_attr_str (uint16_t xa_attr);
  
/*! 
  Allocates and initalizes a new iso9600_xa_t variable and returns
  it. The caller must free the returned result using iso9660_xa_free().

  @see iso9660_xa
*/
iso9660_xa_t *
iso9660_xa_init (iso9660_xa_t *_xa, uint16_t uid, uint16_t gid, uint16_t attr, 
                 uint8_t filenum);

/*! 
  Frees the passed iso9600_xa_t structure.

  @see iso9660_xa
*/
void
iso9660_xa_free (iso9660_xa_t *_xa);

#ifdef __cplusplus
}

/** The below variables are trickery to force the above enum symbol
    values to be recorded in debug symbol tables. They are used to
    allow one to refer to the enumeration value names in the typedefs
    above in a debugger and debugger expressions.
*/
extern xa_misc_enum_t debugger_xa_misc_enum;

  
#endif /* __cplusplus */

#endif /* CDIO_XA_H_ */

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
