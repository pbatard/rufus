/*  
    Copyright (C) 2005, 2006, 2008 Rocky Bernstein <rocky@gnu.org>

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
 * \file udf_file.h 
 *
 * \brief Routines involving UDF file operations
 *
*/

#ifndef UDF_FILE_H
#define UDF_FILE_H 

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

  /**
    Return the file id descriptor of the given file.
  */
  bool udf_get_fileid_descriptor(const udf_dirent_t *p_udf_dirent, 
				 /*out*/ udf_fileid_desc_t *p_udf_fid);

  /**
    Return the name of the file
  */
  const char *udf_get_filename(const udf_dirent_t *p_udf_dirent);
  
  /**
    Return the name of the file
  */
  bool udf_get_file_entry(const udf_dirent_t *p_udf_dirent, 
			  /*out*/ udf_file_entry_t *p_udf_fe);

  /**
    Return the number of hard links of the file. Return 0 if error.
  */
  uint16_t udf_get_link_count(const udf_dirent_t *p_udf_dirent);

  /**
    Return the file length the file. Return 2147483647L if error.
  */
  uint64_t udf_get_file_length(const udf_dirent_t *p_udf_dirent);

  /**  
    Returns a POSIX mode for a given p_udf_dirent.
  */
  mode_t udf_get_posix_filemode(const udf_dirent_t *p_udf_dirent);

  /**
    Return the next subdirectory. 
  */
  udf_dirent_t *udf_opendir(const udf_dirent_t *p_udf_dirent);
  
  /**
     Attempts to read up to count bytes from UDF directory entry
     p_udf_dirent into the buffer starting at buf. buf should be a
     multiple of UDF_BLOCKSIZE bytes. Reading continues after the
     point at which we last read or from the beginning the first time.
     
     If count is zero, read() returns zero and has no other results. If
     count is greater than SSIZE_MAX, the result is unspecified.
     
     If there is an error, cast the result to driver_return_code_t for 
     the specific error code.
  */
  /**
    Attempts to read up to count bytes from file descriptor fd into
    the buffer starting at buf.
    
    If count is zero, read() returns zero and has no other results. If
    count is greater than SSIZE_MAX, the result is unspecified.
  */
  ssize_t udf_read_block(const udf_dirent_t *p_udf_dirent, 
			 void * buf, size_t count);

  /**
    Advances p_udf_direct to the the next directory entry in the
    pointed to by p_udf_dir. It also returns this as the value.  NULL
    is returned on reaching the end-of-file or if an error. Also
    p_udf_dirent is free'd. If the end of is not reached the caller
    must call udf_dirent_free() with p_udf_dirent when done with it to 
    release resources.
  */
  udf_dirent_t *udf_readdir(udf_dirent_t *p_udf_dirent);
  
  /**
    free free resources associated with p_udf_dirent.
  */
  bool udf_dirent_free(udf_dirent_t *p_udf_dirent);
  
  /**
    Return true if the file is a directory.
  */
  bool udf_is_dir(const udf_dirent_t *p_udf_dirent);
  
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*UDF_FILE_H*/
