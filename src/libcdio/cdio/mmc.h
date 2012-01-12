/*
    Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010
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

/**
   \file mmc.h 
   
   \brief Common definitions for MMC (Multimedia Commands). Applications
   include this for direct MMC access.

   The documents we make use of are described in several
   specifications made by the SCSI committee T10
   http://www.t10.org. In particular, SCSI Primary Commands (SPC),
   SCSI Block Commands (SBC), and Multi-Media Commands (MMC). These
   documents generally have a numeric level number appended. For
   example SPC-3 refers to ``SCSI Primary Commands - 3'.

   In year 2010 the current versions were SPC-3, SBC-2, MMC-5.

*/

#ifndef __CDIO_MMC_H__
#define __CDIO_MMC_H__

#include <cdio/cdio.h>
#include <cdio/types.h>
#include <cdio/dvd.h>
#include <cdio/audio.h>
#include <cdio/mmc_util.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* On GNU/Linux see <linux/byteorder/big_endian.h> and 
   <linux/byteorder/little_endian.h>
*/
#ifdef WORDS_BIGENDIAN
#  if !defined(__LITTLE_ENDIAN_BITFIELD) && !defined(__BIG_ENDIAN_BITFIELD)
#  define __MMC_BIG_ENDIAN_BITFIELD
#  endif
#else 
#  if !defined(__LITTLE_ENDIAN_BITFIELD) && !defined(__BIG_ENDIAN_BITFIELD)
#  define __MMC_LITTLE_ENDIAN_BITFIELD
#  endif
#endif

   /**
      Structure of a SCSI/MMC sense reply.
     
      This has been adapted from GNU/Linux request_sense of <linux/cdrom.h>
      include this for direct MMC access.
      See SCSI Primary Commands-2 (SPC-3) table 26 page 38.
    */
    typedef struct cdio_mmc_request_sense {
#if defined(__MMC_BIG_ENDIAN_BITFIELD)
	uint8_t valid		: 1;  /**< valid bit is 1 if info is valid */
	uint8_t error_code	: 7;
#else
	uint8_t error_code	: 7;
	uint8_t valid		: 1;  /**< valid bit is 1 if info is valid */
#endif
	uint8_t segment_number;
#if defined(__MMC_BIG_ENDIAN_BITFIELD)
	uint8_t filemark	: 1; /**< manditory in sequential
                                      * access devices */
	uint8_t eom	        : 1; /**< end of medium. manditory in
                                      * sequential access and
                                      * printer devices */
	uint8_t ili		: 1; /**< incorrect length indicator */
	uint8_t reserved1	: 1;
	uint8_t sense_key	: 4;
#else
	uint8_t sense_key	: 4;
	uint8_t reserved1	: 1;
	uint8_t ili		: 1; /**< incorrect length indicator */
	uint8_t eom	        : 1; /**< end of medium. manditory in
                                      * sequential access and
                                      * printer devices */
	uint8_t filemark	: 1; /**< manditory in sequential
                                      * access devices */
#endif
	uint8_t information[4];
	uint8_t additional_sense_len; /**< Additional sense length (n-7) */
	uint8_t command_info[4];      /**< Command-specific information */
	uint8_t asc;                  /**< Additional sense code */
	uint8_t ascq;                 /**< Additional sense code qualifier */
	uint8_t fruc;                 /**< Field replaceable unit code */
	uint8_t sks[3];               /**< Sense-key specific */
	uint8_t asb[46];              /**< Additional sense bytes */
    } cdio_mmc_request_sense_t;


    /**
       Meanings of the values of mmc_request_sense.sense_key
     */
    typedef enum {
        CDIO_MMC_SENSE_KEY_NO_SENSE        =  0,
        CDIO_MMC_SENSE_KEY_RECOVERED_ERROR =  1,
        CDIO_MMC_SENSE_KEY_NOT_READY       =  2,
        CDIO_MMC_SENSE_KEY_MEDIUM_ERROR    =  3,
        CDIO_MMC_SENSE_KEY_HARDWARE_ERROR  =  4,
        CDIO_MMC_SENSE_KEY_ILLEGAL_REQUEST =  5,
        CDIO_MMC_SENSE_KEY_UNIT_ATTENTION  =  6,
        CDIO_MMC_SENSE_KEY_DATA_PROTECT    =  7,
        CDIO_MMC_SENSE_KEY_BLANK_CHECK     =  8,
        CDIO_MMC_SENSE_KEY_VENDOR_SPECIFIC =  9,
        CDIO_MMC_SENSE_KEY_COPY_ABORTED    = 10,
        CDIO_MMC_SENSE_KEY_ABORTED_COMMAND = 11,
        CDIO_MMC_SENSE_KEY_OBSOLETE        = 12,
    } cdio_mmc_sense_key_t;

    /**
       \brief The opcode-portion (generic packet commands) of an MMC command.
       
       In general, those opcodes that end in 6 take a 6-byte command
       descriptor, those that end in 10 take a 10-byte
       descriptor and those that in in 12 take a 12-byte descriptor. 
       
       (Not that you need to know that, but it seems to be a
       big deal in the MMC specification.)
       
    */
    typedef enum {
  CDIO_MMC_GPCMD_TEST_UNIT_READY        = 0x00, /**< test if drive ready. */
  CDIO_MMC_GPCMD_INQUIRY                = 0x12, /**< Request drive 
                                                   information. */
  CDIO_MMC_GPCMD_MODE_SELECT_6          = 0x15, /**< Select medium 
                                                   (6 bytes). */
  CDIO_MMC_GPCMD_MODE_SENSE_6           = 0x1a, /**< Get medium or device
                                                 information. Should be issued
                                                 before MODE SELECT to get
                                                 mode support or save current
                                                 settings. (6 bytes). */
  CDIO_MMC_GPCMD_START_STOP_UNIT        = 0x1b, /**< Enable/disable Disc
                                                     operations. (6 bytes). */
  CDIO_MMC_GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL 
                                        = 0x1e, /**< Enable/disable Disc 
                                                   removal. (6 bytes). */

  /**
      Group 2 Commands (CDB's here are 10-bytes)   
  */
  CDIO_MMC_GPCMD_READ_10                = 0x28, /**< Read data from drive 
                                                   (10 bytes). */
  CDIO_MMC_GPCMD_READ_SUBCHANNEL        = 0x42, /**< Read Sub-Channel data.
                                                   (10 bytes). */
  CDIO_MMC_GPCMD_READ_TOC               = 0x43, /**< READ TOC/PMA/ATIP. 
                                                   (10 bytes). */
  CDIO_MMC_GPCMD_READ_HEADER            = 0x44,
  CDIO_MMC_GPCMD_PLAY_AUDIO_10          = 0x45, /**< Begin audio playing at
                                                   current position
                                                   (10 bytes). */
  CDIO_MMC_GPCMD_GET_CONFIGURATION      = 0x46, /**< Get drive Capabilities 
                                                   (10 bytes) */
  CDIO_MMC_GPCMD_PLAY_AUDIO_MSF         = 0x47, /**< Begin audio playing at
                                                   specified MSF (10
                                                   bytes). */
  CDIO_MMC_GPCMD_PLAY_AUDIO_TI          = 0x48,
  CDIO_MMC_GPCMD_PLAY_TRACK_REL_10      = 0x49, /**< Play audio at the track
                                                   relative LBA. (10 bytes).
                                                   Doesn't seem to be part
                                                   of MMC standards but is
                                                   handled by Plextor drives.
                                                */
                                                   
  CDIO_MMC_GPCMD_GET_EVENT_STATUS       = 0x4a, /**< Report events and 
                                                   Status. */
  CDIO_MMC_GPCMD_PAUSE_RESUME           = 0x4b, /**< Stop or restart audio 
                                                   playback. (10 bytes). 
                                                   Used with a PLAY command. */

  CDIO_MMC_GPCMD_READ_DISC_INFO         = 0x51, /**< Get CD information.
                                                   (10 bytes). */
  CDIO_MMC_GPCMD_READ_TRACK_INFORMATION = 0x52, /**< Information about a 
                                                   logical track. */
  CDIO_MMC_GPCMD_MODE_SELECT_10         = 0x55, /**< Select medium 
                                                   (10-bytes). */
  CDIO_MMC_GPCMD_MODE_SENSE_10          = 0x5a, /**< Get medium or device
                                                 information. Should be issued
                                                 before MODE SELECT to get
                                                 mode support or save current
                                                 settings. (6 bytes). */

  /**
      Group 5 Commands (CDB's here are 12-bytes) 
  */
  CDIO_MMC_GPCMD_PLAY_AUDIO_12          = 0xa5, /**< Begin audio playing at
                                                   current position
                                                 (12 bytes) */
  CDIO_MMC_GPCMD_LOAD_UNLOAD            = 0xa6, /**< Load/unload a Disc
                                                 (12 bytes) */
  CDIO_MMC_GPCMD_READ_12                = 0xa8, /**< Read data from drive 
                                                   (12 bytes). */
  CDIO_MMC_GPCMD_PLAY_TRACK_REL_12      = 0xa9, /**< Play audio at the track
                                                   relative LBA. (12 bytes).
                                                   Doesn't seem to be part
                                                   of MMC standards but is
                                                   handled by Plextor drives.
                                                */
  CDIO_MMC_GPCMD_READ_DVD_STRUCTURE     = 0xad, /**< Get DVD structure info
                                                   from media (12 bytes). */
  CDIO_MMC_GPCMD_READ_MSF               = 0xb9, /**< Read almost any field 
                                                   of a CD sector at specified
                                                   MSF. (12 bytes). */
  CDIO_MMC_GPCMD_SET_SPEED              = 0xbb, /**< Set drive speed 
                                                   (12 bytes). This is listed 
                                                   as optional in ATAPI 2.6, 
                                                   but is (curiously)
                                                   missing from Mt. Fuji, 
                                                   Table 57. It is mentioned
                                                   in Mt. Fuji Table 377 as an 
                                                   MMC command for SCSI
                                                   devices though...  Most
                                                   ATAPI drives support it. */
  CDIO_MMC_GPCMD_READ_CD                = 0xbe, /**< Read almost any field 
                                                   of a CD sector at current
                                                   location. (12 bytes). */
  /**
      Vendor-unique Commands  
  */
  CDIO_MMC_GPCMD_CD_PLAYBACK_STATUS  = 0xc4 /**< SONY unique  = command */,
  CDIO_MMC_GPCMD_PLAYBACK_CONTROL    = 0xc9 /**< SONY unique  = command */,
  CDIO_MMC_GPCMD_READ_CDDA           = 0xd8 /**< Vendor unique  = command */,
  CDIO_MMC_GPCMD_READ_CDXA           = 0xdb /**< Vendor unique  = command */,
  CDIO_MMC_GPCMD_READ_ALL_SUBCODES   = 0xdf /**< Vendor unique  = command */
  } cdio_mmc_gpcmd_t;


  /**
      Read Subchannel states 
  */
  typedef enum {
    CDIO_MMC_READ_SUB_ST_INVALID   = 0x00, /**< audio status not supported */
    CDIO_MMC_READ_SUB_ST_PLAY      = 0x11, /**< audio play operation in 
                                              progress */
    CDIO_MMC_READ_SUB_ST_PAUSED    = 0x12, /**< audio play operation paused */
    CDIO_MMC_READ_SUB_ST_COMPLETED = 0x13, /**< audio play successfully 
                                              completed */
    CDIO_MMC_READ_SUB_ST_ERROR     = 0x14, /**< audio play stopped due to 
                                              error */
    CDIO_MMC_READ_SUB_ST_NO_STATUS = 0x15, /**< no current audio status to
                                              return */
  } cdio_mmc_read_sub_state_t;

  /** Level values that can go into READ_CD */
  typedef enum {
    CDIO_MMC_READ_TYPE_ANY  =  0,  /**< All types */
    CDIO_MMC_READ_TYPE_CDDA =  1,  /**< Only CD-DA sectors */
    CDIO_MMC_READ_TYPE_MODE1 = 2,  /**< mode1 sectors (user data = 2048) */
    CDIO_MMC_READ_TYPE_MODE2 = 3,  /**< mode2 sectors form1 or form2 */
    CDIO_MMC_READ_TYPE_M2F1 =  4,  /**< mode2 sectors form1 */
    CDIO_MMC_READ_TYPE_M2F2 =  5   /**< mode2 sectors form2 */
  } cdio_mmc_read_cd_type_t;

  /**
      Format values for READ_TOC 
  */
  typedef enum {
    CDIO_MMC_READTOC_FMT_TOC     =  0,
    CDIO_MMC_READTOC_FMT_SESSION =  1,  
    CDIO_MMC_READTOC_FMT_FULTOC  =  2,
    CDIO_MMC_READTOC_FMT_PMA     =  3,  /**< Q subcode data */
    CDIO_MMC_READTOC_FMT_ATIP    =  4,  /**< includes media type */
    CDIO_MMC_READTOC_FMT_CDTEXT  =  5   /**< CD-TEXT info  */
  } cdio_mmc_readtoc_t;
    
  /**
     Page codes for MODE SENSE and MODE SET. 
  */
  typedef enum {
      CDIO_MMC_R_W_ERROR_PAGE     = 0x01,
      CDIO_MMC_WRITE_PARMS_PAGE   = 0x05,
      CDIO_MMC_CDR_PARMS_PAGE     = 0x0d,
      CDIO_MMC_AUDIO_CTL_PAGE     = 0x0e,
      CDIO_MMC_POWER_PAGE         = 0x1a,
      CDIO_MMC_FAULT_FAIL_PAGE    = 0x1c,
      CDIO_MMC_TO_PROTECT_PAGE    = 0x1d,
      CDIO_MMC_CAPABILITIES_PAGE  = 0x2a,
      CDIO_MMC_ALL_PAGES          = 0x3f,
  } cdio_mmc_mode_page_t;

 /**
    READ DISC INFORMATION Data Types
 */
  typedef enum {
      CDIO_MMC_READ_DISC_INFO_STANDARD   = 0x0,
      CDIO_MMC_READ_DISC_INFO_TRACK      = 0x1,
      CDIO_MMC_READ_DISC_INFO_POW        = 0x2,
  } cdio_mmc_read_disc_info_datatype_t;
        

PRAGMA_BEGIN_PACKED
  struct mmc_audio_volume_entry_s 
  {
    uint8_t  selection; /* Only the lower 4 bits are used. */
    uint8_t  volume;
  } GNUC_PACKED;

  typedef struct mmc_audio_volume_entry_s mmc_audio_volume_entry_t;
  
  /**
      This struct is used by cdio_audio_get_volume and cdio_audio_set_volume 
  */
  struct mmc_audio_volume_s
  {
    mmc_audio_volume_entry_t port[4];
  } GNUC_PACKED;

  typedef struct mmc_audio_volume_s mmc_audio_volume_t;
  
PRAGMA_END_PACKED
  

/**
    Return type codes for GET_CONFIGURATION. 
*/
typedef enum {
  CDIO_MMC_GET_CONF_ALL_FEATURES     = 0,  /**< all features without regard
                                              to currency. */
  CDIO_MMC_GET_CONF_CURRENT_FEATURES = 1,  /**< features which are currently
                                              in effect (e.g. based on
                                              medium inserted). */
  CDIO_MMC_GET_CONF_NAMED_FEATURE    = 2   /**< just the feature named in
                                              the GET_CONFIGURATION cdb. */
} cdio_mmc_get_conf_t;
  

/**
    FEATURE codes used in GET CONFIGURATION. 
*/

typedef enum {
  CDIO_MMC_FEATURE_PROFILE_LIST     = 0x000, /**< Profile List Feature */
  CDIO_MMC_FEATURE_CORE             = 0x001, 
  CDIO_MMC_FEATURE_MORPHING         = 0x002, /**< Report/prevent operational
                                                  changes  */
  CDIO_MMC_FEATURE_REMOVABLE_MEDIUM = 0x003, /**< Removable Medium Feature */
  CDIO_MMC_FEATURE_WRITE_PROTECT    = 0x004, /**< Write Protect Feature */
  CDIO_MMC_FEATURE_RANDOM_READABLE  = 0x010, /**< Random Readable Feature */
  CDIO_MMC_FEATURE_MULTI_READ       = 0x01D, /**< Multi-Read Feature */
  CDIO_MMC_FEATURE_CD_READ          = 0x01E, /**< CD Read Feature */
  CDIO_MMC_FEATURE_DVD_READ         = 0x01F, /**< DVD Read Feature */
  CDIO_MMC_FEATURE_RANDOM_WRITABLE  = 0x020, /**< Random Writable Feature */
  CDIO_MMC_FEATURE_INCR_WRITE       = 0x021, /**< Incremental Streaming 
                                                Writable Feature */
  CDIO_MMC_FEATURE_SECTOR_ERASE     = 0x022, /**< Sector Erasable Feature */
  CDIO_MMC_FEATURE_FORMATABLE       = 0x023, /**< Formattable Feature */
  CDIO_MMC_FEATURE_DEFECT_MGMT      = 0x024, /**< Management Ability of the 
                                                Logical Unit/media system to
                                                provide an apparently 
                                                defect-free space.*/
  CDIO_MMC_FEATURE_WRITE_ONCE       = 0x025, /**< Write Once
                                                   Feature */
  CDIO_MMC_FEATURE_RESTRICT_OVERW   = 0x026, /**< Restricted Overwrite
                                                Feature */
  CDIO_MMC_FEATURE_CD_RW_CAV        = 0x027, /**< CD-RW CAV Write Feature */
  CDIO_MMC_FEATURE_MRW              = 0x028, /**< MRW Feature */
  CDIO_MMC_FEATURE_ENHANCED_DEFECT  = 0x029, /**< Enhanced Defect Reporting */
  CDIO_MMC_FEATURE_DVD_PRW          = 0x02A, /**< DVD+RW Feature */
  CDIO_MMC_FEATURE_DVD_PR           = 0x02B, /**< DVD+R Feature */
  CDIO_MMC_FEATURE_RIGID_RES_OVERW  = 0x02C, /**< Rigid Restricted Overwrite */
  CDIO_MMC_FEATURE_CD_TAO           = 0x02D, /**< CD Track at Once */ 
  CDIO_MMC_FEATURE_CD_SAO           = 0x02E, /**< CD Mastering (Session at 
                                                Once) */
  CDIO_MMC_FEATURE_DVD_R_RW_WRITE   = 0x02F, /**< DVD-R/RW Write */
  CDIO_MMC_FEATURE_CD_RW_MEDIA_WRITE= 0x037, /**< CD-RW Media Write Support */
  CDIO_MMC_FEATURE_DVD_PR_2_LAYER   = 0x03B, /**< DVD+R Double Layer */
  CDIO_MMC_FEATURE_POWER_MGMT       = 0x100, /**< Initiator and device directed
                                                power management */
  CDIO_MMC_FEATURE_CDDA_EXT_PLAY    = 0x103, /**< Ability to play audio CDs 
                                                via the Logical Unit's own
                                                analog output */
  CDIO_MMC_FEATURE_MCODE_UPGRADE    = 0x104, /* Ability for the device to 
                                                accept  new microcode via
                                                the interface */
  CDIO_MMC_FEATURE_TIME_OUT         = 0x105, /**< Ability to respond to all
                                                commands within a specific 
                                                time */
  CDIO_MMC_FEATURE_DVD_CSS          = 0x106, /**< Ability to perform DVD
                                                CSS/CPPM authentication and
                                                RPC */
  CDIO_MMC_FEATURE_RT_STREAMING     = 0x107, /**< Ability to read and write 
                                                using Initiator requested
                                                performance parameters   */
  CDIO_MMC_FEATURE_LU_SN            = 0x108, /**< The Logical Unit has a unique
                                                identifier. */
  CDIO_MMC_FEATURE_FIRMWARE_DATE    = 0x1FF, /**< Firmware creation date 
                                                report */
} cdio_mmc_feature_t;
                                
typedef enum {
  CDIO_MMC_FEATURE_INTERFACE_UNSPECIFIED = 0,
  CDIO_MMC_FEATURE_INTERFACE_SCSI        = 1,
  CDIO_MMC_FEATURE_INTERFACE_ATAPI       = 2,
  CDIO_MMC_FEATURE_INTERFACE_IEEE_1394   = 3,
  CDIO_MMC_FEATURE_INTERFACE_IEEE_1394A  = 4,
  CDIO_MMC_FEATURE_INTERFACE_FIBRE_CH    = 5
} cdio_mmc_feature_interface_t;
  

/**
    The largest Command Descriptor Block (CDB) size.
    The possible sizes are 6, 10, and 12 bytes.
*/
#define MAX_CDB_LEN 12

/**
    \brief A Command Descriptor Block (CDB) used in sending MMC 
    commands.
 */
typedef struct mmc_cdb_s {
  uint8_t field[MAX_CDB_LEN];
} mmc_cdb_t;
  
  /**
      \brief Format of header block in data returned from an MMC
    GET_CONFIGURATION command.
  */
  typedef struct mmc_feature_list_header_s {
    unsigned char length_msb;
    unsigned char length_1sb;
    unsigned char length_2sb;
    unsigned char length_lsb;
    unsigned char reserved1;
    unsigned char reserved2;
    unsigned char profile_msb;
    unsigned char profile_lsb;
  } cdio_mmc_feature_list_header_t;

  /**
      An enumeration indicating whether an MMC command is sending
      data, or getting data, or does none of both.
  */
  typedef enum mmc_direction_s {
    SCSI_MMC_DATA_READ,
    SCSI_MMC_DATA_WRITE,
    SCSI_MMC_DATA_NONE
  } cdio_mmc_direction_t;
  /**
     Indicate to applications that SCSI_MMC_DATA_NONE is available.
     It has been added after version 0.82 and should be used with commands
     that neither read nor write payload bytes. (At least on Linux such
     commands did work with SCSI_MMC_DATA_READ or SCSI_MMC_DATA_WRITE, too.)
  */
#define SCSI_MMC_HAS_DIR_NONE 1
  
  typedef struct mmc_subchannel_s
  {
    uint8_t       reserved;
    uint8_t       audio_status;
    uint16_t      data_length; /**< Really ISO 9660 7.2.2 */
    uint8_t       format;
    uint8_t       address:      4;
    uint8_t       control:      4;
    uint8_t       track;
    uint8_t       index;
    uint8_t       abs_addr[4];
    uint8_t       rel_addr[4];
  } cdio_mmc_subchannel_t;
  
#define CDIO_MMC_SET_COMMAND(cdb, command)      \
  cdb[0] = command
  
#define CDIO_MMC_SET_READ_TYPE(cdb, sector_type) \
  cdb[1] = (sector_type << 2)
  
#define CDIO_MMC_GETPOS_LEN16(p, pos)           \
  (p[pos]<<8) + p[pos+1]
  
#define CDIO_MMC_GET_LEN16(p)                   \
  (p[0]<<8) + p[1]
  
#define CDIO_MMC_GET_LEN32(p) \
  (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
  
#define CDIO_MMC_SET_LEN16(cdb, pos, len)       \
  cdb[pos  ] = (len >>  8) & 0xff;              \
  cdb[pos+1] = (len      ) & 0xff

#define CDIO_MMC_SET_READ_LBA(cdb, lba)         \
  cdb[2] = (lba >> 24) & 0xff; \
  cdb[3] = (lba >> 16) & 0xff; \
  cdb[4] = (lba >>  8) & 0xff; \
  cdb[5] = (lba      ) & 0xff

#define CDIO_MMC_SET_START_TRACK(cdb, command) \
  cdb[6] = command

#define CDIO_MMC_SET_READ_LENGTH24(cdb, len) \
  cdb[6] = (len >> 16) & 0xff; \
  cdb[7] = (len >>  8) & 0xff; \
  cdb[8] = (len      ) & 0xff

#define CDIO_MMC_SET_READ_LENGTH16(cdb, len) \
  CDIO_MMC_SET_LEN16(cdb, 7, len)

#define CDIO_MMC_SET_READ_LENGTH8(cdb, len) \
  cdb[8] = (len      ) & 0xff

#define CDIO_MMC_MCSB_ALL_HEADERS 0xf

#define CDIO_MMC_SET_MAIN_CHANNEL_SELECTION_BITS(cdb, val) \
  cdb[9] = val << 3;

/**
   Get the output port volumes and port selections used on AUDIO PLAY
   commands via a MMC MODE SENSE command using the CD Audio Control
   Page.
   @param p_cdio the CD object to be acted upon.
   @param p_volume volume parameters retrieved
   @return DRIVER_OP_SUCCESS if we ran the command ok.
*/
driver_return_code_t mmc_audio_get_volume (CdIo_t *p_cdio,  /*out*/
                                           mmc_audio_volume_t *p_volume);
  
  /**
     Read Audio Subchannel information
     
     @param p_cdio the CD object to be acted upon.
     @param p_subchannel place for returned subchannel information
  */
    driver_return_code_t
    mmc_audio_read_subchannel (CdIo_t *p_cdio, 
                           /*out*/ cdio_subchannel_t *p_subchannel);
  
  /**
     Read ISRC Subchannel information. Contributed by
     Scot C. Bontrager (scot@indievisible.org) 
     May 15, 2011 -
     
     @param p_cdio the CD object to be acted upon.
     @param track the track you to get ISRC info
     @param p_isrc place to put ISRC info
  */
    driver_return_code_t
    mmc_isrc_track_read_subchannel (CdIo_t *p_cdio,  /*in*/ const track_t track,
                                    /*out*/ char *p_isrc);
  
  /**
    Return a string containing the name of the audio state as returned from
    the Q_SUBCHANNEL.
  */
  const char *mmc_audio_state2str( uint8_t i_audio_state );
  
  /**
     Get the block size used in read requests, via MMC (e.g. READ_10, 
     READ_MSF, ...)
     @param p_cdio the CD object to be acted upon.
     @return the blocksize if > 0; error if <= 0
  */
  int mmc_get_blocksize ( CdIo_t *p_cdio );
  
  /**
    Return the length in bytes of the Command Descriptor
    Buffer (CDB) for a given MMC command. The length will be 
    either 6, 10, or 12. 
  */
  uint8_t mmc_get_cmd_len(uint8_t mmc_cmd);
  
  /**
    Get the lsn of the end of the CD
    
    @param p_cdio the CD object to be acted upon.
    @return the lsn. On error return CDIO_INVALID_LSN.
  */
  lsn_t mmc_get_disc_last_lsn( const CdIo_t *p_cdio );
  
  /**
    Return the discmode as reported by the MMC Read (FULL) TOC
    command.
    
    Information was obtained from Section 5.1.13 (Read TOC/PMA/ATIP)
    pages 56-62 from the MMC draft specification, revision 10a
    at http://www.t10.org/ftp/t10/drafts/mmc/mmc-r10a.pdf See
    especially tables 72, 73 and 75.
  */
  discmode_t mmc_get_discmode( const CdIo_t *p_cdio );
  
  
  typedef enum {
    CDIO_MMC_LEVEL_WEIRD,
    CDIO_MMC_LEVEL_1,
    CDIO_MMC_LEVEL_2,
    CDIO_MMC_LEVEL_3,
    CDIO_MMC_LEVEL_NONE
  } cdio_mmc_level_t;
  
  /**
    Get the MMC level supported by the device.
    @param p_cdio the CD object to be acted upon.
    @return MMC level supported by the device.
  */
  cdio_mmc_level_t mmc_get_drive_mmc_cap(CdIo_t *p_cdio);
  

  /**
    Get the DVD type associated with cd object.
    
    @param p_cdio the CD object to be acted upon.
    @param s location to store DVD information.
    @return the DVD discmode.
  */
  discmode_t mmc_get_dvd_struct_physical ( const CdIo_t *p_cdio, 
                                           cdio_dvd_struct_t *s);
  
  /**
    Find out if media tray is open or closed.
    @param p_cdio the CD object to be acted upon.
    @return 1 if media is open, 0 if closed. Error
    return codes are the same as driver_return_code_t
  */
  int mmc_get_tray_status ( const CdIo_t *p_cdio );

  /**
    Get the CD-ROM hardware info via an MMC INQUIRY command.
    
    @param p_cdio the CD object to be acted upon.
    @param p_hw_info place to store hardware information retrieved
    @return true if we were able to get hardware info, false if we had
    an error.
  */
  bool mmc_get_hwinfo ( const CdIo_t *p_cdio, 
                        /* out*/ cdio_hwinfo_t *p_hw_info );
  
  
  /**
    Find out if media has changed since the last call.
    @param p_cdio the CD object to be acted upon.
    @return 1 if media has changed since last call, 0 if not. Error
    return codes are the same as driver_return_code_t
  */
  int mmc_get_media_changed(const CdIo_t *p_cdio);
  
  /**
    Get the media catalog number (MCN) from the CD via MMC.
    
    @param p_cdio the CD object to be acted upon.
    @return the media catalog number r NULL if there is none or we
    don't have the ability to get it.
    
    Note: string is malloc'd so caller has to free() the returned
    string when done with it.
    
  */
  char * mmc_get_mcn(const CdIo_t *p_cdio);
  
  /**
    Report if CD-ROM has a particular kind of interface (ATAPI, SCSCI, ...)
    Is it possible for an interface to have several? If not this 
    routine could probably return the single mmc_feature_interface_t.
    @param p_cdio the CD object to be acted upon.
    @param e_interface 
    @return true if we have the interface and false if not.
  */
  bool_3way_t mmc_have_interface(CdIo_t *p_cdio, 
                                 cdio_mmc_feature_interface_t e_interface );
  

  /**
      Read just the user data part of some sort of data sector (via 
      mmc_read_cd). 
    
      @param p_cdio object to read from

      @param p_buf place to read data into.  The caller should make
             sure this location can store at least CDIO_CD_FRAMESIZE,
             M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE depending on the
             kind of sector getting read. If you don't know whether
             you have a Mode 1/2, Form 1/ Form 2/Formless sector best
             to reserve space for the maximum, M2RAW_SECTOR_SIZE.

      @param i_lsn sector to read
      @param i_blocksize size of each block
      @param i_blocks number of blocks to read

  */
  driver_return_code_t mmc_read_data_sectors ( CdIo_t *p_cdio, void *p_buf, 
                                               lsn_t i_lsn, 
                                               uint16_t i_blocksize,
                                               uint32_t i_blocks );
  
  /**
      Read sectors using SCSI-MMC GPCMD_READ_CD. 
      Can read only up to 25 blocks.
  */
  driver_return_code_t mmc_read_sectors ( const CdIo_t *p_cdio, void *p_buf, 
                                          lsn_t i_lsn,  int read_sector_type, 
                                          uint32_t i_blocks);
  
  /**
    Run a Multimedia command (MMC). 
    
    @param p_cdio        CD structure set by cdio_open().
    @param i_timeout_ms  time in milliseconds we will wait for the command
                         to complete. 
    @param p_cdb         CDB bytes. All values that are needed should be set 
                         on input. We'll figure out what the right CDB length 
                         should be.
    @param e_direction   direction the transfer is to go.
    @param i_buf         Size of buffer
    @param p_buf         Buffer for data, both sending and receiving.

    @return 0 if command completed successfully.
  */
  driver_return_code_t
  mmc_run_cmd( const CdIo_t *p_cdio, unsigned int i_timeout_ms,
               const mmc_cdb_t *p_cdb,
               cdio_mmc_direction_t e_direction, unsigned int i_buf, 
               /*in/out*/ void *p_buf );

  /**
    Run a Multimedia command (MMC) specifying the CDB length.
    The motivation here is for example ot use in is an undocumented 
    debug command for LG drives (namely E7), whose length is being 
    miscalculated by mmc_get_cmd_len(); it doesn't follow the usual 
    code number to length conventions. Patch supplied by SukkoPera.

    @param p_cdio        CD structure set by cdio_open().
    @param i_timeout_ms  time in milliseconds we will wait for the command
                         to complete. 
    @param p_cdb         CDB bytes. All values that are needed should be set 
                         on input. 
    @param i_cdb         number of CDB bytes. 
    @param e_direction   direction the transfer is to go.
    @param i_buf         Size of buffer
    @param p_buf         Buffer for data, both sending and receiving.

    @return 0 if command completed successfully.
  */
  driver_return_code_t
  mmc_run_cmd_len( const CdIo_t *p_cdio, unsigned int i_timeout_ms,
                   const mmc_cdb_t *p_cdb, unsigned int i_cdb,
                   cdio_mmc_direction_t e_direction, unsigned int i_buf,
                   /*in/out*/ void *p_buf );

  /**
      Obtain the SCSI sense reply of the most-recently-performed MMC command.
      These bytes give an indication of possible problems which occured in
      the drive while the command was performed. With some commands they tell
      about the current state of the drive (e.g. 00h TEST UNIT READY).
      @param p_cdio CD structure set by cdio_open().

      @param pp_sense returns the sense bytes received from the drive.
      This is allocated memory or NULL if no sense bytes are
      available. Dispose non-NULL pointers by free() when no longer
      needed.  See SPC-3 4.5.3 Fixed format sense data.  SCSI error
      codes as of SPC-3 Annex D, MMC-5 Annex F: sense[2]&15 = Key ,
      sense[12] = ASC , sense[13] = ASCQ

      @return number of valid bytes in sense, 0 in case of no sense
              bytes available, <0 in case of internal error.
  */
  int mmc_last_cmd_sense ( const CdIo_t *p_cdio, 
                           cdio_mmc_request_sense_t **pp_sense);

  /**
    Set the block size for subsequest read requests, via MMC.
  */
  driver_return_code_t mmc_set_blocksize ( const CdIo_t *p_cdio, 
                                           uint16_t i_blocksize);
  
#ifdef __cplusplus
}
#endif /* __cplusplus */

/**
    The below variables are trickery to force the above enum symbol
    values to be recorded in debug symbol tables. They are used to
    allow one to refer to the enumeration value names in the typedefs
    above in a debugger and debugger expressions
*/
extern cdio_mmc_feature_t           debug_cdio_mmc_feature;
extern cdio_mmc_feature_interface_t debug_cdio_mmc_feature_interface;
extern cdio_mmc_feature_profile_t   debug_cdio_mmc_feature_profile;
extern cdio_mmc_get_conf_t          debug_cdio_mmc_get_conf;
extern cdio_mmc_gpcmd_t             debug_cdio_mmc_gpcmd;
extern cdio_mmc_read_sub_state_t    debug_cdio_mmc_read_sub_state;
extern cdio_mmc_read_cd_type_t      debug_cdio_mmc_read_cd_type;
extern cdio_mmc_readtoc_t           debug_cdio_mmc_readtoc;
extern cdio_mmc_mode_page_t         debug_cdio_mmc_mode_page;
  
#ifndef DO_NOT_WANT_OLD_MMC_COMPATIBILITY
#define CDIO_MMC_GPCMD_START_STOP CDIO_MMC_GPCMD_START_STOP_UNIT 
#define CDIO_MMC_GPCMD_ALLOW_MEDIUM_REMOVAL  \
    CDIO_MMC_GPCMD_PREVENT_ALLOW_MEDIUM_REMOVAL
#endif /*DO_NOT_WANT_PARANOIA_COMPATIBILITY*/

#endif /* __MMC_H__ */

/* 
 * Local variables:
 *  c-file-style: "ruby"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
