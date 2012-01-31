/*
    $Id: sector.h,v 1.38 2008/03/25 15:59:09 karl Exp $

    Copyright (C) 2003, 2004, 2005, 2006, 2008 Rocky Bernstein <rocky@gnu.org>
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
/*!
   \file sector.h 
   \brief Things related to CD-ROM layout: tracks, sector sizes, MSFs, LBAs.

  A CD-ROM physical sector size is 2048, 2052, 2056, 2324, 2332, 2336,
  2340, or 2352 bytes long.

  Sector types of the standard CD-ROM data formats:

\verbatim 
  format   sector type               user data size (bytes)
  -----------------------------------------------------------------------------
    1     (Red Book)    CD-DA          2352    (CDIO_CD_FRAMESIZE_RAW)
    2     (Yellow Book) Mode1 Form1    2048    (CDIO_CD_FRAMESIZE)
    3     (Yellow Book) Mode1 Form2    2336    (M2RAW_SECTOR_SIZE)
    4     (Green Book)  Mode2 Form1    2048    (CDIO_CD_FRAMESIZE)
    5     (Green Book)  Mode2 Form2    2328    (2324+4 spare bytes)
 
 
        The layout of the standard CD-ROM data formats:
  -----------------------------------------------------------------------------
  - audio (red):                  | audio_sample_bytes |
                                  |        2352        |
 
  - data (yellow, mode1):         | sync - head - data - EDC - zero - ECC |
                                  |  12  -   4  - 2048 -  4  -   8  - 276 |
 
  - data (yellow, mode2):         | sync - head - data |
                                 |  12  -   4  - 2336 |
 
  - XA data (green, mode2 form1): | sync - head - sub - data - EDC - ECC |
                                  |  12  -   4  -  8  - 2048 -  4  - 276 |
 
  - XA data (green, mode2 form2): | sync - head - sub - data - Spare |
                                  |  12  -   4  -  8  - 2324 -  4    |
\endverbatim
 

*/

#ifndef _CDIO_SECTOR_H_
#define _CDIO_SECTOR_H_

#ifdef __cplusplus
    extern "C" {
#endif

#include <cdio/types.h>

      /*! Information that can be obtained through a Read Subchannel
        command.
      */
#define CDIO_SUBCHANNEL_SUBQ_DATA		0
#define CDIO_SUBCHANNEL_CURRENT_POSITION	1
#define CDIO_SUBCHANNEL_MEDIA_CATALOG	        2
#define CDIO_SUBCHANNEL_TRACK_ISRC		3
      
      /*! track flags
       * Q Sub-channel Control Field (4.2.3.3)
       */
      typedef enum {
	NONE = 			0x00,	/* no flags set */
	PRE_EMPHASIS =		0x01,	/* audio track recorded with pre-emphasis */
	COPY_PERMITTED =	0x02,	/* digital copy permitted */
	DATA =			0x04,	/* data track */
	FOUR_CHANNEL_AUDIO =	0x08,	/* 4 audio channels */
	SCMS =			0x10	/* SCMS (5.29.2.7) */
      } flag_t;
      
#define CDIO_PREGAP_SECTORS  150
#define CDIO_POSTGAP_SECTORS 150
      
      /*! An enumeration for some of the CDIO_CD \#defines below. This isn't
        really an enumeration one would really use in a program it is to
        be helpful in debuggers where wants just to refer to the CDIO_CD_
        names and get something.
      */
      extern enum cdio_cd_enums {
        CDIO_CD_MINS =              74,   /**< max. minutes per CD, not really
                                             a limit */
        CDIO_CD_SECS_PER_MIN =      60,   /**< seconds per minute */
        CDIO_CD_FRAMES_PER_SEC =    75,   /**< frames per second */
        CDIO_CD_SYNC_SIZE =         12,   /**< 12 sync bytes per raw data 
                                             frame */
        CDIO_CD_CHUNK_SIZE =        24,   /**< lowest-level "data bytes 
                                             piece" */
        CDIO_CD_NUM_OF_CHUNKS =     98,   /**< chunks per frame */
        CDIO_CD_FRAMESIZE_SUB =     96,   /**< subchannel data "frame" size */
        CDIO_CD_HEADER_SIZE =        4,   /**< header (address) bytes per raw
                                             frame */
        CDIO_CD_SUBHEADER_SIZE =     8,   /**< subheader bytes per raw XA data 
                                             frame */
        CDIO_CD_ECC_SIZE =         276,   /**< bytes ECC per most raw data 
                                             frame types */
        CDIO_CD_FRAMESIZE =       2048,   /**< bytes per frame, "cooked" 
                                             mode */
        CDIO_CD_FRAMESIZE_RAW =   2352,   /**< bytes per frame, "raw" mode */
        CDIO_CD_FRAMESIZE_RAWER = 2646,   /**< The maximum possible 
                                             returned  */
        CDIO_CD_FRAMESIZE_RAW1  = 2340,
        CDIO_CD_FRAMESIZE_RAW0  = 2336,
        CDIO_CD_MAX_SESSIONS =      99, 
        CDIO_CD_MIN_SESSION_NO =     1,   /**<, Smallest CD session number */
        CDIO_CD_MAX_LSN =       450150,   /**< Largest LSN in a CD */
        CDIO_CD_MIN_LSN      = -450150,   /**< Smallest LSN in a CD */
      } cdio_cd_enums;
        
      /*!
        Some generally useful CD-ROM information -- mostly based on the above.
        This is from linux.h - not to slight other OS's. This was the first
        place I came across such useful stuff.
      */
#define CDIO_CD_MINS              74   /**< max. minutes per CD, not really
                                          a limit */
#define CDIO_CD_SECS_PER_MIN      60   /**< seconds per minute */
#define CDIO_CD_FRAMES_PER_SEC    75   /**< frames per second */
#define CDIO_CD_SYNC_SIZE         12   /**< 12 sync bytes per raw data frame */
#define CDIO_CD_CHUNK_SIZE        24   /**< lowest-level "data bytes piece" */
#define CDIO_CD_NUM_OF_CHUNKS     98   /**< chunks per frame */
#define CDIO_CD_FRAMESIZE_SUB     96   /**< subchannel data "frame" size */
#define CDIO_CD_HEADER_SIZE        4   /**< header (address) bytes per raw
                                          data frame */
#define CDIO_CD_SUBHEADER_SIZE     8   /**< subheader bytes per raw XA data 
                                            frame */
#define CDIO_CD_EDC_SIZE           4   /**< bytes EDC per most raw data
                                          frame types */
#define CDIO_CD_M1F1_ZERO_SIZE     8   /**< bytes zero per yellow book mode
                                          1 frame */
#define CDIO_CD_ECC_SIZE         276   /**< bytes ECC per most raw data frame 
                                          types */
#define CDIO_CD_FRAMESIZE       2048   /**< bytes per frame, "cooked" mode */
#define CDIO_CD_FRAMESIZE_RAW   2352   /**< bytes per frame, "raw" mode */
#define CDIO_CD_FRAMESIZE_RAWER 2646   /**< The maximum possible returned 
                                          bytes */ 
#define CDIO_CD_FRAMESIZE_RAW1 (CDIO_CD_CD_FRAMESIZE_RAW-CDIO_CD_SYNC_SIZE) /*2340*/
#define CDIO_CD_FRAMESIZE_RAW0 (CDIO_CD_FRAMESIZE_RAW-CDIO_CD_SYNC_SIZE-CDIO_CD_HEADER_SIZE) /*2336*/
      
      /*! "before data" part of raw XA (green, mode2) frame */
#define CDIO_CD_XA_HEADER (CDIO_CD_HEADER_SIZE+CDIO_CD_SUBHEADER_SIZE) 
      
      /*! "after data" part of raw XA (green, mode2 form1) frame */
#define CDIO_CD_XA_TAIL   (CDIO_CD_EDC_SIZE+CDIO_CD_ECC_SIZE) 
      
      /*! "before data" sync bytes + header of XA (green, mode2) frame */
#define CDIO_CD_XA_SYNC_HEADER   (CDIO_CD_SYNC_SIZE+CDIO_CD_XA_HEADER) 
      
      /*! String of bytes used to identify the beginning of a Mode 1 or
          Mode 2 sector. */
      extern const uint8_t CDIO_SECTOR_SYNC_HEADER[CDIO_CD_SYNC_SIZE];
      /**<  
        {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
      */
      
      /*! An enumeration for some of the M2*_SECTOR_SIZE \#defines
        below. This isn't really an enumeration one would really use in a
        program it is to be helpful in debuggers where wants just to refer
        to the M2*_SECTOR_SIZE names and get something.
      */
      extern enum m2_sector_enums {
        M2F2_SECTOR_SIZE  = 2324,
        M2SUB_SECTOR_SIZE = 2332,
        M2RAW_SECTOR_SIZE = 2336
      } m2_sector_enums;
      
#define M2F2_SECTOR_SIZE    2324
#define M2SUB_SECTOR_SIZE   2332
#define M2RAW_SECTOR_SIZE   2336
      
      /*! Largest CD session number */
#define CDIO_CD_MAX_SESSIONS    99 
      /*! Smallest CD session number */
#define CDIO_CD_MIN_SESSION_NO   1
      
      /*! Largest LSN in a CD */
#define CDIO_CD_MAX_LSN   450150
      /*! Smallest LSN in a CD */
#define CDIO_CD_MIN_LSN  -450150
      
      
#define CDIO_CD_FRAMES_PER_MIN                          \
      (CDIO_CD_FRAMES_PER_SEC*CDIO_CD_SECS_PER_MIN)
      
#define CDIO_CD_74MIN_SECTORS (UINT32_C(74)*CDIO_CD_FRAMES_PER_MIN)
#define CDIO_CD_80MIN_SECTORS (UINT32_C(80)*CDIO_CD_FRAMES_PER_MIN)
#define CDIO_CD_90MIN_SECTORS (UINT32_C(90)*CDIO_CD_FRAMES_PER_MIN)
      
#define CDIO_CD_MAX_SECTORS                                     \
      (UINT32_C(100)*CDIO_CD_FRAMES_PER_MIN-CDIO_PREGAP_SECTORS)
      
#define msf_t_SIZEOF 3
      
      /*! 
        Convert an LBA into a string representation of the MSF.
        \warning cdio_lba_to_msf_str returns new allocated string */
      char *cdio_lba_to_msf_str (lba_t i_lba);
      
      /*! 
        Convert an MSF into a string representation of the MSF.
        \warning cdio_msf_to_msf_str returns new allocated string */
      char *cdio_msf_to_str (const msf_t *p_msf);
      
      /*! 
        Convert an LBA into the corresponding LSN.
      */
      lba_t cdio_lba_to_lsn (lba_t i_lba);
      
      /*! 
        Convert an LBA into the corresponding MSF.
      */
      void  cdio_lba_to_msf(lba_t i_lba, msf_t *p_msf);
      
      /*! 
        Convert an LSN into the corresponding LBA.
        CDIO_INVALID_LBA is returned if there is an error.
      */
      lba_t cdio_lsn_to_lba (lsn_t i_lsn);
      
      /*! 
        Convert an LSN into the corresponding MSF.
      */
      void  cdio_lsn_to_msf (lsn_t i_lsn, msf_t *p_msf);
      
      /*! 
        Convert a MSF into the corresponding LBA.
        CDIO_INVALID_LBA is returned if there is an error.
      */
      lba_t cdio_msf_to_lba (const msf_t *p_msf);
      
      /*! 
        Convert a MSF into the corresponding LSN.
        CDIO_INVALID_LSN is returned if there is an error.
      */
      lsn_t cdio_msf_to_lsn (const msf_t *p_msf);
      
      /*!  
        Convert a MSF - broken out as 3 integer components into the
        corresponding LBA.  
        CDIO_INVALID_LBA is returned if there is an error.
      */
      lba_t cdio_msf3_to_lba (unsigned int minutes, unsigned int seconds, 
                              unsigned int frames);
      
      /*! 
        Convert a string of the form MM:SS:FF into the corresponding LBA.
        CDIO_INVALID_LBA is returned if there is an error.
      */
      lba_t cdio_mmssff_to_lba (const char *psz_mmssff);
      
#ifdef __cplusplus
    }
#endif

#ifndef DO_NOT_WANT_PARANOIA_COMPATIBILITY
/** For compatibility with good ol' paranoia */
#define CD_FRAMESIZE_RAW        CDIO_CD_FRAMESIZE_RAW
#endif /*DO_NOT_WANT_PARANOIA_COMPATIBILITY*/

#endif /* _CDIO_SECTOR_H_ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
