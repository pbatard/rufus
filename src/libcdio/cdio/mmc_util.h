/*
    Copyright (C) 2010 Rocky Bernstein <rocky@gnu.org>

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
   \file mmc_util.h 
   
   \brief Multimedia Command (MMC) "helper" routines that don't depend
   on anything other than headers.
*/

#ifndef __CDIO_MMC_UTIL_H__
#define __CDIO_MMC_UTIL_H__

#include <cdio/device.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

    /**
       Profile profile codes used in GET_CONFIGURATION - PROFILE LIST. */
    typedef enum {
        CDIO_MMC_FEATURE_PROF_NON_REMOVABLE = 0x0001, /**< Re-writable disc, capable
                                                         of changing behavior */
        CDIO_MMC_FEATURE_PROF_REMOVABLE     = 0x0002, /**< disk Re-writable; with 
                                                         removable  media */
        CDIO_MMC_FEATURE_PROF_MO_ERASABLE   = 0x0003, /**< Erasable Magneto-Optical
                                                         disk with sector erase
                                                         capability */
        CDIO_MMC_FEATURE_PROF_MO_WRITE_ONCE = 0x0004, /**< Write Once Magneto-Optical
                                                         write once */
        CDIO_MMC_FEATURE_PROF_AS_MO         = 0x0005, /**< Advance Storage
                                                         Magneto-Optical */
        CDIO_MMC_FEATURE_PROF_CD_ROM        = 0x0008, /**< Read only Compact Disc
                                                         capable */
        CDIO_MMC_FEATURE_PROF_CD_R          = 0x0009, /**< Write once Compact Disc
                                                         capable */
        CDIO_MMC_FEATURE_PROF_CD_RW         = 0x000A, /**< CD-RW Re-writable
                                                         Compact Disc capable */
        
        CDIO_MMC_FEATURE_PROF_DVD_ROM       = 0x0010, /**< Read only DVD */
        CDIO_MMC_FEATURE_PROF_DVD_R_SEQ     = 0x0011, /**< Re-recordable DVD using
                                                         Sequential recording */
        CDIO_MMC_FEATURE_PROF_DVD_RAM       = 0x0012, /**< Re-writable DVD */
        CDIO_MMC_FEATURE_PROF_DVD_RW_RO     = 0x0013, /**< Re-recordable DVD using
                                                         Restricted Overwrite */
        CDIO_MMC_FEATURE_PROF_DVD_RW_SEQ    = 0x0014, /**< Re-recordable DVD using
                                                         Sequential recording */
        CDIO_MMC_FEATURE_PROF_DVD_R_DL_SEQ  = 0x0015, /**< DVD-R/DL sequential
                                                         recording */
        CDIO_MMC_FEATURE_PROF_DVD_R_DL_JR   = 0x0016, /**< DVD-R/DL layer jump 
                                                         recording */
        CDIO_MMC_FEATURE_PROF_DVD_PRW       = 0x001A, /**< DVD+RW - DVD ReWritable */
        CDIO_MMC_FEATURE_PROF_DVD_PR        = 0x001B, /**< DVD+R - DVD Recordable */
        CDIO_MMC_FEATURE_PROF_DDCD_ROM      = 0x0020, /**< Read only  DDCD */
        CDIO_MMC_FEATURE_PROF_DDCD_R        = 0x0021, /**< DDCD-R Write only DDCD */
        CDIO_MMC_FEATURE_PROF_DDCD_RW       = 0x0022, /**< Re-Write only DDCD */
        CDIO_MMC_FEATURE_PROF_DVD_PRW_DL    = 0x002A, /**< "DVD+RW/DL */
        CDIO_MMC_FEATURE_PROF_DVD_PR_DL     = 0x002B, /**< DVD+R - DVD Recordable 
                                                         double layer */
        
        CDIO_MMC_FEATURE_PROF_BD_ROM        = 0x0040, /**< BD-ROM */
        CDIO_MMC_FEATURE_PROF_BD_SEQ        = 0x0041, /**< BD-R sequential 
                                                         recording */
        CDIO_MMC_FEATURE_PROF_BD_R_RANDOM   = 0x0042, /**< BD-R random recording */
        CDIO_MMC_FEATURE_PROF_BD_RE         = 0x0043, /**< BD-RE */
        
        CDIO_MMC_FEATURE_PROF_HD_DVD_ROM    = 0x0050, /**< HD-DVD-ROM */
        CDIO_MMC_FEATURE_PROF_HD_DVD_R      = 0x0051, /**< HD-DVD-R */
        CDIO_MMC_FEATURE_PROF_HD_DVD_RAM    = 0x0052, /**<"HD-DVD-RAM */
        
        CDIO_MMC_FEATURE_PROF_NON_CONFORM   = 0xFFFF, /**< The Logical Unit does not
                                                         conform to any Profile. */
    } cdio_mmc_feature_profile_t;
  
    /**
       @param i_feature MMC feature number
       @return string containing the name of the given feature
    */
    const char *mmc_feature2str( int i_feature );
    
    /**
       Get drive capabilities for a device.
       @param p_cdio the CD object to be acted upon.
       @param p_read_cap  list of read capabilities that are set on return
       @param p_write_cap list of write capabilities that are set on return
       @param p_misc_cap  list of miscellaneous capabilities (that are neither
       read nor write related) that are set on return
    */
    void mmc_get_drive_cap ( CdIo_t *p_cdio,
                             /*out*/ cdio_drive_read_cap_t  *p_read_cap,
                             /*out*/ cdio_drive_write_cap_t *p_write_cap,
                             /*out*/ cdio_drive_misc_cap_t  *p_misc_cap);
    
    /**
       Return a string containing the name of the given feature
    */
    const char *mmc_feature_profile2str( int i_feature_profile );

    bool mmc_is_disctype_bd(cdio_mmc_feature_profile_t disctype);
    bool mmc_is_disctype_cdrom(cdio_mmc_feature_profile_t disctype);
    bool mmc_is_disctype_dvd(cdio_mmc_feature_profile_t disctype);
    bool mmc_is_disctype_hd_dvd (cdio_mmc_feature_profile_t disctype);
    bool mmc_is_disctype_overwritable (cdio_mmc_feature_profile_t disctype);
    bool mmc_is_disctype_rewritable(cdio_mmc_feature_profile_t disctype);
    
    /** The default read timeout is 3 minutes. */
#define MMC_READ_TIMEOUT_DEFAULT 3*60*1000
    
    /**
       Set this to the maximum value in milliseconds that we will
       wait on an MMC read command.  
    */
    extern uint32_t mmc_read_timeout_ms;
    
    /**
       Maps a mmc_sense_key_t into a string name.
    */
    extern const char mmc_sense_key2str[16][40];

    /**
       The default timeout (non-read) is 6 seconds. 
    */
#define MMC_TIMEOUT_DEFAULT 6000

    /**
       Set this to the maximum value in milliseconds that we will
       wait on an MMC command.  
    */
    extern uint32_t mmc_timeout_ms; 

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __MMC_UTIL_H__ */
/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
