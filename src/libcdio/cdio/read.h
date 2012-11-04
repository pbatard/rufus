/*
    Copyright (C) 2005, 2006, 2007, 2008, 2012 Rocky Bernstein <rocky@gnu.org>

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

/** \file read.h 
 *
 *  \brief The top-level header for sector (block, frame)-related
 *  libcdio calls. 
 */

#ifndef CDIO_READ_H_
#define CDIO_READ_H_

#include <cdio/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /** All the different ways a block/sector can be read. */
  typedef enum {
    CDIO_READ_MODE_AUDIO,  /**< CD-DA, audio, Red Book */
    CDIO_READ_MODE_M1F1,   /**< Mode 1 Form 1 */
    CDIO_READ_MODE_M1F2,   /**< Mode 1 Form 2 */
    CDIO_READ_MODE_M2F1,   /**< Mode 2 Form 1 */
    CDIO_READ_MODE_M2F2    /**< Mode 2 Form 2 */
  } cdio_read_mode_t;
  
  /*!
    Reposition read offset
    Similar to (if not the same as) libc's fseek()

    @param p_cdio object which gets adjusted
    @param offset amount to seek
    @param whence  like corresponding parameter in libc's fseek, e.g. 
                   SEEK_SET or SEEK_END.
    @return (off_t) -1 on error. 
  */

  off_t cdio_lseek(const CdIo_t *p_cdio, off_t offset, int whence);
    
  /*!  Reads into buf the next size bytes.  Similar to (if not the
    same as) libc's read(). This is a "cooked" read, or one handled by
    the OS. It probably won't work on audio data. For that use
    cdio_read_audio_sector(s).

    @param p_cdio object to read from
    @param p_buf place to read data into. The caller should make sure
                 this location can store at least i_size bytes.
    @param i_size number of bytes to read

    @return (ssize_t) -1 on error. 
  */
  ssize_t cdio_read(const CdIo_t *p_cdio, void *p_buf, size_t i_size);
    
  /*!
    Read an audio sector

    @param p_cdio object to read from
    @param p_buf place to read data into. The caller should make sure
                 this location can store at least CDIO_FRAMESIZE_RAW
                 bytes.
    @param i_lsn sector to read
  */
  driver_return_code_t cdio_read_audio_sector (const CdIo_t *p_cdio, 
                                               void *p_buf, lsn_t i_lsn);

  /*!
    Reads audio sectors

    @param p_cdio object to read from
    @param p_buf place to read data into. The caller should make sure
                 this location can store at least CDIO_FRAMESIZE_RAW
                 * i_blocks bytes.
    @param i_lsn sector to read
    @param i_blocks number of sectors to read
  */
  driver_return_code_t cdio_read_audio_sectors (const CdIo_t *p_cdio, 
                                                void *p_buf, lsn_t i_lsn,
                                                uint32_t i_blocks);

  /*!
    Read data sectors

    @param p_cdio object to read from
    @param p_buf place to read data into.  The caller should make sure
                 this location can store at least ISO_BLOCKSIZE, 
                 M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE depending
                 on the kind of sector getting read. If you don't 
                 know whether you have a Mode 1/2, Form 1/ Form 2/Formless
                 sector best to reserve space for the maximum, 
                 M2RAW_SECTOR_SIZE.
    @param i_lsn sector to read
    @param i_blocksize size of block. Should be either CDIO_CD_FRAMESIZE, 
    M2RAW_SECTOR_SIZE, or M2F2_SECTOR_SIZE. See comment above under p_buf.

    @param i_blocks number of blocks to read
  */
  driver_return_code_t cdio_read_data_sectors ( const CdIo_t *p_cdio, 
                                                void *p_buf, lsn_t i_lsn,
                                                uint16_t i_blocksize,
                                                uint32_t i_blocks );
  /*!
    Reads a mode 1 sector

    @param p_cdio object to read from
    @param p_buf place to read data into.
    @param i_lsn sector to read
    @param b_form2 true for reading mode 1 form 2 sectors or false for 
    mode 1 form 1 sectors.
  */
  driver_return_code_t cdio_read_mode1_sector (const CdIo_t *p_cdio, 
                                               void *p_buf, lsn_t i_lsn, 
                                               bool b_form2);
  /*!
    Reads mode 1 sectors

    @param p_cdio object to read from
    @param p_buf place to read data into
    @param i_lsn sector to read
    @param b_form2 true for reading mode 1 form 2 sectors or false for 
    mode 1 form 1 sectors.
    @param i_blocks number of sectors to read
  */
  driver_return_code_t cdio_read_mode1_sectors (const CdIo_t *p_cdio, 
                                                void *p_buf, lsn_t i_lsn, 
                                                bool b_form2, 
                                                uint32_t i_blocks);
  /*!
    Reads a mode 2 sector

    @param p_cdio object to read from
    @param p_buf place to read data into. The caller should make sure
                 this location can store at least 
                 M2RAW_SECTOR_SIZE (for form 1) or CDIO_CD_FRAMESIZE (for 
                 form 2) bytes.
    @param i_lsn sector to read
    @param b_form2 true for reading mode 2 form 2 sectors or false for 
    mode 2 form 1 sectors.

    @return 0 if no error, nonzero otherwise.
  */
  driver_return_code_t cdio_read_mode2_sector (const CdIo_t *p_cdio, 
                                               void *p_buf, lsn_t i_lsn, 
                                               bool b_form2);
  
  /** The special case of reading a single block is a common one so we
      provide a routine for that as a convenience.
  */
  driver_return_code_t cdio_read_sector(const CdIo_t *p_cdio, void *p_buf, 
                                        lsn_t i_lsn, 
                                        cdio_read_mode_t read_mode);
  /*!
    Reads mode 2 sectors

    @param p_cdio object to read from
    @param p_buf place to read data into. The caller should make sure
                 this location can store at least 
                 M2RAW_SECTOR_SIZE (for form 1) or CDIO_CD_FRAMESIZE (for 
                 form 2) * i_blocks bytes.
    @param i_lsn sector to read
    @param b_form2 true for reading mode2 form 2 sectors or false for 
           mode 2  form 1 sectors.
    @param i_blocks number of sectors to read

    @return 0 if no error, nonzero otherwise.
  */
  driver_return_code_t cdio_read_mode2_sectors (const CdIo_t *p_cdio, 
                                                void *p_buf, lsn_t i_lsn, 
                                                bool b_form2, 
                                                uint32_t i_blocks);
  
  /*!
    Reads a number of sectors (AKA blocks).

    @param p_cdio cdio object
    @param p_buf place to read data into. The caller should make sure
    this location is large enough. See below for size information.
    @param read_mode the kind of "mode" to use in reading.
    @param i_lsn sector to read
    @param i_blocks number of sectors to read
    @return DRIVER_OP_SUCCESS (0) if no error, other (negative) enumerations
    are returned on error.

    If read_mode is CDIO_MODE_AUDIO,
      *p_buf should hold at least CDIO_FRAMESIZE_RAW * i_blocks bytes.

    If read_mode is CDIO_MODE_DATA,
      *p_buf should hold at least i_blocks times either ISO_BLOCKSIZE, 
      M1RAW_SECTOR_SIZE or M2F2_SECTOR_SIZE depending on the kind of 
      sector getting read. If you don't know whether you have a Mode 1/2, 
    Form 1/ Form 2/Formless sector best to reserve space for the maximum
    which is M2RAW_SECTOR_SIZE.

    If read_mode is CDIO_MODE_M2F1,
    *p_buf should hold at least M2RAW_SECTOR_SIZE * i_blocks bytes.
    
    If read_mode is CDIO_MODE_M2F2,
    *p_buf should hold at least CDIO_CD_FRAMESIZE * i_blocks bytes.
    
  */
  driver_return_code_t cdio_read_sectors(const CdIo_t *p_cdio, void *p_buf, 
                                         lsn_t i_lsn, 
                                         cdio_read_mode_t read_mode,
                                         uint32_t i_blocks);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_READ_H_ */
