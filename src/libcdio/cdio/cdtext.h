/*
    $Id: cdtext.h,v 1.14 2008/03/25 15:59:08 karl Exp $

    Copyright (C) 2004, 2005, 2008 Rocky Bernstein <rocky@gnu.org>
    adapted from cuetools
    Copyright (C) 2003 Svend Sanjay Sorensen <ssorensen@fastmail.fm>

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
 * \file cdtext.h 
 *
 * \brief The top-level header for CD-Text information. Applications
 *  include this for CD-Text access.
*/


#ifndef __CDIO_CDTEXT_H__
#define __CDIO_CDTEXT_H__

#include <cdio/cdio.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MAX_CDTEXT_FIELDS 13
#define MIN_CDTEXT_FIELD  0
#define MAX_CDTEXT_DATA_LENGTH 5000
#define MAX_CDTEXT_GENRE_CODE 28

  
  /*! \brief structure for holding CD-Text information

  @see cdtext_init, cdtext_destroy, cdtext_get, and cdtext_set.
  */
  struct cdtext {
    char *field[MAX_CDTEXT_FIELDS];
  };
  typedef struct cdtext cdtext_track_t;

  struct cdtext_s {
    cdtext_track_t track[100];  /* cdtext for track 1..99. 0 represents cd-text of disc */
    uint16_t genre_code;        /* genre code */
    uint8_t  block;
    char encoding[16];          /* encoding of character strings */
    char language[3];           /* ISO 639-1 (2 letter) language code */
  };
  
  /*! \brief A list of all of the CD-Text fields. Because
    the interval has no gaps, we can use ++ to iterate over fields.
   */
  typedef enum {
    CDTEXT_ARRANGER   =  0,   /**< name(s) of the arranger(s) */
    CDTEXT_COMPOSER   =  1,   /**< name(s) of the composer(s) */
    CDTEXT_DISCID     =  2,   /**< disc identification information */
    CDTEXT_GENRE      =  3,   /**< genre identification and genre information */
    CDTEXT_MESSAGE    =  4,   /**< ISRC code of each track */
    CDTEXT_ISRC       =  5,   /**< message(s) from the content provider or artist */
    CDTEXT_PERFORMER  =  6,   /**< name(s) of the performer(s) */
    CDTEXT_SIZE_INFO  =  7,   /**< size information of the block */
    CDTEXT_SONGWRITER =  8,   /**< name(s) of the songwriter(s) */
    CDTEXT_TITLE      =  9,   /**< title of album name or track titles */
    CDTEXT_TOC_INFO   = 10,   /**< table of contents information */
    CDTEXT_TOC_INFO2  = 11,   /**< second table of contents information */
    CDTEXT_UPC_EAN    = 12,
    CDTEXT_INVALID    = MAX_CDTEXT_FIELDS
  } cdtext_field_t;

  /*! Return string representation of the enum values above */
  const char *cdtext_field2str (cdtext_field_t i);

  /*! CD-Text genre codes */
  typedef enum {
    CDIO_CDTEXT_GENRE_UNUSED         =  0,        /**< not used  */
    CDIO_CDTEXT_GENRE_UNDEFINED      =  1,        /**< not defined */
    CDIO_CDTEXT_GENRE_ADULT_CONTEMP  =  2,        /**< Adult Contemporary       */
    CDIO_CDTEXT_GENRE_ALT_ROCK       =  3,        /**< Alternative Rock         */
    CDIO_CDTEXT_GENRE_CHILDRENS      =  4,        /**< Childrens Music          */
    CDIO_CDTEXT_GENRE_CLASSIC        =  5,        /**< Classical                */
    CDIO_CDTEXT_GENRE_CHRIST_CONTEMP =  6,        /**< Contemporary Christian   */
    CDIO_CDTEXT_GENRE_COUNTRY        =  7,        /**< Country                  */
    CDIO_CDTEXT_GENRE_DANCE          =  8,        /**< Dance                    */
    CDIO_CDTEXT_GENRE_EASY_LISTENING =  9,        /**< Easy Listening           */
    CDIO_CDTEXT_GENRE_EROTIC         = 10,        /**< Erotic                   */
    CDIO_CDTEXT_GENRE_FOLK           = 11,        /**< Folk                     */
    CDIO_CDTEXT_GENRE_GOSPEL         = 12,        /**< Gospel                   */
    CDIO_CDTEXT_GENRE_HIPHOP         = 13,        /**< Hip Hop                  */
    CDIO_CDTEXT_GENRE_JAZZ           = 14,        /**< Jazz                     */
    CDIO_CDTEXT_GENRE_LATIN          = 15,        /**< Latin                    */
    CDIO_CDTEXT_GENRE_MUSICAL        = 16,        /**< Musical                  */
    CDIO_CDTEXT_GENRE_NEWAGE         = 17,        /**< New Age                  */
    CDIO_CDTEXT_GENRE_OPERA          = 18,        /**< Opera                    */
    CDIO_CDTEXT_GENRE_OPERETTA       = 19,        /**< Operetta                 */
    CDIO_CDTEXT_GENRE_POP            = 20,        /**< Pop Music                */
    CDIO_CDTEXT_GENRE_RAP            = 21,        /**< RAP                      */
    CDIO_CDTEXT_GENRE_REGGAE         = 22,        /**< Reggae                   */
    CDIO_CDTEXT_GENRE_ROCK           = 23,        /**< Rock Music               */
    CDIO_CDTEXT_GENRE_RYTHMANDBLUES  = 24,        /**< Rhythm & Blues           */
    CDIO_CDTEXT_GENRE_SOUNDEFFECTS   = 25,        /**< Sound Effects            */
    CDIO_CDTEXT_GENRE_SOUNDTRACK     = 26,        /**< Soundtrack               */
    CDIO_CDTEXT_GENRE_SPOKEN_WORD    = 27,        /**< Spoken Word              */
    CDIO_CDTEXT_GENRE_WORLD_MUSIC    = 28         /**< World Music              */
  } cdtext_genre_t;
  
  /*! Return string representation of the given genre code */
  const char *cdtext_genre2str (cdtext_genre_t i);
  
  /*! Initialize a new cdtext structure.
    When the structure is no longer needed, release the 
    resources using cdtext_delete.
  */
  void cdtext_init (cdtext_t *cdtext);

  /*! Parse raw CD-Text data into cdtext structure */ 
  bool cdtext_data_init(cdtext_t *cdtext, uint8_t *wdata, size_t length);
  
  /*! Free memory assocated with cdtext*/
  void cdtext_destroy (cdtext_t *cdtext);
  
  /*! returns an allocated string associated with the given field.  NULL is
    returned if key is CDTEXT_INVALID or the field is not set.

    The user needs to free the string when done with it.

    @see cdio_get_const to retrieve a constant string that doesn't
    have to be freed.
  */
  char *cdtext_get (cdtext_field_t key, track_t track, const cdtext_t *cdtext);


  /*! returns a const string associated with the given field.  NULL is
    returned if key is CDTEXT_INVALID or the field is not set.

    Don't use the string when the cdtext object (i.e. the CdIo_t object
    you got it from) is no longer valid.

    @see cdio_get to retrieve an allocated string that persists past
    the cdtext object.
  */
  const char *cdtext_get_const (cdtext_field_t key, track_t track, const cdtext_t *cdtext);
  
  /*!
    returns enum of keyword if key is a CD-Text keyword, 
    returns MAX_CDTEXT_FIELDS non-zero otherwise.
  */
  cdtext_field_t cdtext_is_keyword (const char *key);
  
  /*! 
    sets cdtext's keyword entry to field 
  */
  void cdtext_set (cdtext_field_t key, track_t track, const char *value, cdtext_t *cdtext);
  
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __CDIO_CDTEXT_H__ */

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
