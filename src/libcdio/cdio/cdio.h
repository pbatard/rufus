/* -*- c -*-
    Copyright (C) 2003, 2004, 2005, 2008, 2009, 2014
    Rocky Bernstein <rocky@gnu.org>
    Copyright (C) 2001 Herbert Valerio Riedel <hvr@gnu.org>

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

/** \file cdio.h 
 *
 *  \brief The top-level header for libcdio: the CD Input and Control
 *  library. Applications include this for anything regarding libcdio.
 */


#ifndef __CDIO_H__
#define __CDIO_H__

/** Application Interface or Protocol version number. If the public
 *  interface changes, we increase this number.
 */
#define CDIO_API_VERSION 6

#include <cdio/version.h>
#include <cdio/types.h>
#include <cdio/sector.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* For compatibility. */
#define CdIo CdIo_t
    
  /** This is an opaque structure for the CD object. */
  typedef struct _CdIo CdIo_t; 

#ifdef __cplusplus
}
#endif /* __cplusplus */

/* Drive(r)/Device-related functions. Perhaps we should break out 
   Driver from device?
*/
#include <cdio/device.h>

/* CD-Text-related functions. */
#include <cdio/cdtext.h>

/* Disc-related functions. */
#include <cdio/disc.h>

/* Sector (frame, or block)-related functions. Uses driver_return_code_t
   from <cdio/device.h> so it should come after that. 
*/
#include <cdio/read.h>

/* Track-related functions. */
#include <cdio/track.h>

/* Memory management functions. */
#include <cdio/memory.h>

#endif /* __CDIO_H__ */
