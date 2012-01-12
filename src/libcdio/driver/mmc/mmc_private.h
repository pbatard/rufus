/* private MMC helper routines.
  Copyright (C) 2004, 2005, 2006, 2008 Rocky Bernstein <rocky@gnu.org>

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

#include <cdio/mmc.h>

/*! Convert milliseconds to seconds taking the ceiling value, i.e.
    1002 milliseconds gets rounded to 2 seconds.
*/
#define SECS2MSECS 1000
static inline unsigned int 
msecs2secs(unsigned int msecs) 
{
  return (msecs+(SECS2MSECS-1)) / SECS2MSECS;
}
#undef SECS2MSECS

/***********************************************************
  MMC CdIo Operations which a driver may use. 
  These are not directly user-accessible.
************************************************************/
/*!
  Read Audio Subchannel information
  
  @param p_user_data the CD object to be acted upon.
  
*/
driver_return_code_t
audio_read_subchannel_mmc ( void *p_user_data, 
			    cdio_subchannel_t *p_subchannel);

/*!
  Get the block size for subsequest read requests, via a SCSI MMC 
  MODE_SENSE 6 command.
*/
int get_blocksize_mmc (void *p_user_data);

/*!  
  Get the lsn of the end of the CD
  
  @return the lsn. On error return CDIO_INVALID_LSN.
*/
lsn_t get_disc_last_lsn_mmc( void *p_user_data );
  
void get_drive_cap_mmc (const void *p_user_data,
			/*out*/ cdio_drive_read_cap_t  *p_read_cap,
			/*out*/ cdio_drive_write_cap_t *p_write_cap,
			/*out*/ cdio_drive_misc_cap_t  *p_misc_cap);

int get_media_changed_mmc (const void *p_user_data);

char *get_mcn_mmc (const void *p_user_data);

driver_return_code_t get_tray_status (const void *p_user_data);

/*! Read just the user data part of some sort of data sector (via 
    mmc_read_cd). 

    @param p_user_data object to read from

    @param p_buf place to read data into.  The caller should make sure
                 this location can store at least CDIO_CD_FRAMESIZE,
                 M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE depending on
                 the kind of sector getting read. If you don't know
                 whether you have a Mode 1/2, Form 1/ Form 2/Formless
                 sector best to reserve space for the maximum,
                 M2RAW_SECTOR_SIZE.

    @param i_lsn sector to read
    @param i_blocksize size of block. Should be either CDIO_CD_FRAMESIZE, 
    M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE. See comment above under p_buf.

*/
driver_return_code_t read_data_sectors_mmc ( void *p_user_data, 
					     void *p_buf,  lsn_t i_lsn,
					     uint16_t i_blocksize,
					     uint32_t i_blocks );
char *get_mcn_mmc (const void *p_user_data);

/* Set read blocksize (via MMC) */
driver_return_code_t set_blocksize_mmc (void *p_user_data, 
					uint16_t i_blocksize);

/* Set the drive speed in CD-ROM speed units (via MMC). */
driver_return_code_t set_drive_speed_mmc (void *p_user_data, int i_speed);

/* Set CD-ROM drive speed  in K bytes per second. (via MMC) */
driver_return_code_t set_speed_mmc (void *p_user_data, int i_Kbs_speed);

/***********************************************************
  Miscellaenous other "private" routines. Probably need
  to better classify these.
************************************************************/

typedef driver_return_code_t (*mmc_run_cmd_fn_t) 
     ( void *p_user_data, 
       unsigned int i_timeout_ms,
       unsigned int i_cdb, 
       const mmc_cdb_t *p_cdb, 
       cdio_mmc_direction_t e_direction, 
       unsigned int i_buf, /*in/out*/ void *p_buf );
			     
int mmc_set_blocksize_mmc_private ( const void *p_env, const
				    mmc_run_cmd_fn_t run_mmc_cmd,
				    uint16_t i_blocksize );

/*! 
  Get the DVD type associated with cd object.
*/
discmode_t 
mmc_get_dvd_struct_physical_private ( void *p_env,
				      mmc_run_cmd_fn_t run_mmc_cmd, 
				      cdio_dvd_struct_t *s );


char *mmc_get_mcn_private ( void *p_env,
			    mmc_run_cmd_fn_t run_mmc_cmd
			    );

uint8_t * mmc_read_cdtext_private ( void *p_user_data, 
			       mmc_run_cmd_fn_t run_mmc_cmd
			       );

/*!
  On input a MODE_SENSE command was issued and we have the results
  in p. We interpret this and return a bit mask set according to the 
  capabilities.
 */
void mmc_get_drive_cap_buf(const uint8_t *p,
			   /*out*/ cdio_drive_read_cap_t  *p_read_cap,
			   /*out*/ cdio_drive_write_cap_t *p_write_cap,
			   /*out*/ cdio_drive_misc_cap_t  *p_misc_cap);

driver_return_code_t
mmc_set_blocksize_private ( void *p_env, 
			    const mmc_run_cmd_fn_t run_mmc_cmd, 
			    uint16_t i_blocksize);
