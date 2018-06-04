/*
    Copyright (C) 2004, 2005, 2008, 2012 Rocky Bernstein <rocky@gnu.org>
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


#ifndef CDIO_CDTEXT_H_
#define CDIO_CDTEXT_H_

#include <cdio/types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MIN_CDTEXT_FIELD          0
#define MAX_CDTEXT_FIELDS         10

/*! Enumeration of CD-TEXT text fields. */
typedef enum {
  CDTEXT_FIELD_TITLE          =  0,   /**< title of album name or track titles */
  CDTEXT_FIELD_PERFORMER      =  1,   /**< name(s) of the performer(s) */
  CDTEXT_FIELD_SONGWRITER     =  2,   /**< name(s) of the songwriter(s) */
  CDTEXT_FIELD_COMPOSER       =  3,   /**< name(s) of the composer(s) */
  CDTEXT_FIELD_MESSAGE        =  4,   /**< message(s) from content provider or artist, ISO-8859-1 encoded*/
  CDTEXT_FIELD_ARRANGER       =  5,   /**< name(s) of the arranger(s) */
  CDTEXT_FIELD_ISRC           =  6,   /**< ISRC code of each track */
  CDTEXT_FIELD_UPC_EAN        =  7,   /**< upc/european article number of disc, ISO-8859-1 encoded */
  CDTEXT_FIELD_GENRE          =  8,   /**< genre identification and genre information, ASCII encoded */
  CDTEXT_FIELD_DISCID         =  9,   /**< disc identification, ASCII encoded (may be non-printable) */
  CDTEXT_FIELD_INVALID        =  MAX_CDTEXT_FIELDS /**< INVALID FIELD*/
} cdtext_field_t;

/*! Enumeration of possible genre codes. */
typedef enum {
  CDTEXT_GENRE_UNUSED         =  0,   /**< field is not used. default */
  CDTEXT_GENRE_UNDEFINED      =  1,   /**< not defined */
  CDTEXT_GENRE_ADULT_CONTEMP  =  2,   /**< Adult Contemporary */
  CDTEXT_GENRE_ALT_ROCK       =  3,   /**< Alternative Rock */
  CDTEXT_GENRE_CHILDRENS      =  4,   /**< Childrens Music */
  CDTEXT_GENRE_CLASSIC        =  5,   /**< Classical */
  CDTEXT_GENRE_CHRIST_CONTEMP =  6,   /**< Contemporary Christian */
  CDTEXT_GENRE_COUNTRY        =  7,   /**< Country */
  CDTEXT_GENRE_DANCE          =  8,   /**< Dance */
  CDTEXT_GENRE_EASY_LISTENING =  9,   /**< Easy Listening */
  CDTEXT_GENRE_EROTIC         = 10,   /**< Erotic */
  CDTEXT_GENRE_FOLK           = 11,   /**< Folk */
  CDTEXT_GENRE_GOSPEL         = 12,   /**< Gospel */
  CDTEXT_GENRE_HIPHOP         = 13,   /**< Hip Hop */
  CDTEXT_GENRE_JAZZ           = 14,   /**< Jazz */
  CDTEXT_GENRE_LATIN          = 15,   /**< Latin */
  CDTEXT_GENRE_MUSICAL        = 16,   /**< Musical */
  CDTEXT_GENRE_NEWAGE         = 17,   /**< New Age */
  CDTEXT_GENRE_OPERA          = 18,   /**< Opera */
  CDTEXT_GENRE_OPERETTA       = 19,   /**< Operetta */
  CDTEXT_GENRE_POP            = 20,   /**< Pop Music */
  CDTEXT_GENRE_RAP            = 21,   /**< RAP */
  CDTEXT_GENRE_REGGAE         = 22,   /**< Reggae */
  CDTEXT_GENRE_ROCK           = 23,   /**< Rock Music */
  CDTEXT_GENRE_RYTHMANDBLUES  = 24,   /**< Rhythm & Blues */
  CDTEXT_GENRE_SOUNDEFFECTS   = 25,   /**< Sound Effects */
  CDTEXT_GENRE_SOUNDTRACK     = 26,   /**< Soundtrack */
  CDTEXT_GENRE_SPOKEN_WORD    = 27,   /**< Spoken Word */
  CDTEXT_GENRE_WORLD_MUSIC    = 28    /**< World Music */
} cdtext_genre_t;

/*! Enumeration of possible CD-TEXT languages.
 * 
 * The language code is encoded as specified in ANNEX 1 to part 5 of EBU
 * Tech 32 58 -E (1991).
 */
typedef enum {
  CDTEXT_LANGUAGE_UNKNOWN     = 0x00,
  CDTEXT_LANGUAGE_ALBANIAN    = 0x01,
  CDTEXT_LANGUAGE_BRETON      = 0x02,
  CDTEXT_LANGUAGE_CATALAN     = 0x03,
  CDTEXT_LANGUAGE_CROATIAN    = 0x04,
  CDTEXT_LANGUAGE_WELSH       = 0x05,
  CDTEXT_LANGUAGE_CZECH       = 0x06,
  CDTEXT_LANGUAGE_DANISH      = 0x07,
  CDTEXT_LANGUAGE_GERMAN      = 0x08,
  CDTEXT_LANGUAGE_ENGLISH     = 0x09,
  CDTEXT_LANGUAGE_SPANISH     = 0x0A,
  CDTEXT_LANGUAGE_ESPERANTO   = 0x0B,
  CDTEXT_LANGUAGE_ESTONIAN    = 0x0C,
  CDTEXT_LANGUAGE_BASQUE      = 0x0D,
  CDTEXT_LANGUAGE_FAROESE     = 0x0E,
  CDTEXT_LANGUAGE_FRENCH      = 0x0F,
  CDTEXT_LANGUAGE_FRISIAN     = 0x10,
  CDTEXT_LANGUAGE_IRISH       = 0x11,
  CDTEXT_LANGUAGE_GAELIC      = 0x12,
  CDTEXT_LANGUAGE_GALICIAN    = 0x13,
  CDTEXT_LANGUAGE_ICELANDIC   = 0x14,
  CDTEXT_LANGUAGE_ITALIAN     = 0x15,
  CDTEXT_LANGUAGE_LAPPISH     = 0x16,
  CDTEXT_LANGUAGE_LATIN       = 0x17,
  CDTEXT_LANGUAGE_LATVIAN     = 0x18,
  CDTEXT_LANGUAGE_LUXEMBOURGIAN = 0x19,
  CDTEXT_LANGUAGE_LITHUANIAN  = 0x1A,
  CDTEXT_LANGUAGE_HUNGARIAN   = 0x1B,
  CDTEXT_LANGUAGE_MALTESE     = 0x1C,
  CDTEXT_LANGUAGE_DUTCH       = 0x1D,
  CDTEXT_LANGUAGE_NORWEGIAN   = 0x1E,
  CDTEXT_LANGUAGE_OCCITAN     = 0x1F,
  CDTEXT_LANGUAGE_POLISH      = 0x20,
  CDTEXT_LANGUAGE_PORTUGUESE  = 0x21,
  CDTEXT_LANGUAGE_ROMANIAN    = 0x22,
  CDTEXT_LANGUAGE_ROMANSH     = 0x23,
  CDTEXT_LANGUAGE_SERBIAN     = 0x24,
  CDTEXT_LANGUAGE_SLOVAK      = 0x25,
  CDTEXT_LANGUAGE_SLOVENIAN   = 0x26,
  CDTEXT_LANGUAGE_FINNISH     = 0x27,
  CDTEXT_LANGUAGE_SWEDISH     = 0x28,
  CDTEXT_LANGUAGE_TURKISH     = 0x29,
  CDTEXT_LANGUAGE_FLEMISH     = 0x2A,
  CDTEXT_LANGUAGE_WALLON      = 0x2B,
  CDTEXT_LANGUAGE_ZULU        = 0x45,
  CDTEXT_LANGUAGE_VIETNAMESE  = 0x46,
  CDTEXT_LANGUAGE_UZBEK       = 0x47,
  CDTEXT_LANGUAGE_URDU        = 0x48,
  CDTEXT_LANGUAGE_UKRAINIAN   = 0x49,
  CDTEXT_LANGUAGE_THAI        = 0x4A,
  CDTEXT_LANGUAGE_TELUGU      = 0x4B,
  CDTEXT_LANGUAGE_TATAR       = 0x4C,
  CDTEXT_LANGUAGE_TAMIL       = 0x4D,
  CDTEXT_LANGUAGE_TADZHIK     = 0x4E,
  CDTEXT_LANGUAGE_SWAHILI     = 0x4F,
  CDTEXT_LANGUAGE_SRANANTONGO = 0x50,
  CDTEXT_LANGUAGE_SOMALI      = 0x51,
  CDTEXT_LANGUAGE_SINHALESE   = 0x52,
  CDTEXT_LANGUAGE_SHONA       = 0x53,
  CDTEXT_LANGUAGE_SERBO_CROAT = 0x54,
  CDTEXT_LANGUAGE_RUTHENIAN   = 0x55,
  CDTEXT_LANGUAGE_RUSSIAN     = 0x56,
  CDTEXT_LANGUAGE_QUECHUA     = 0x57,
  CDTEXT_LANGUAGE_PUSHTU      = 0x58,
  CDTEXT_LANGUAGE_PUNJABI     = 0x59,
  CDTEXT_LANGUAGE_PERSIAN     = 0x5A,
  CDTEXT_LANGUAGE_PAPAMIENTO  = 0x5B,
  CDTEXT_LANGUAGE_ORIYA       = 0x5C,
  CDTEXT_LANGUAGE_NEPALI      = 0x5D,
  CDTEXT_LANGUAGE_NDEBELE     = 0x5E,
  CDTEXT_LANGUAGE_MARATHI     = 0x5F,
  CDTEXT_LANGUAGE_MOLDAVIAN   = 0x60,
  CDTEXT_LANGUAGE_MALAYSIAN   = 0x61,
  CDTEXT_LANGUAGE_MALAGASAY   = 0x62,
  CDTEXT_LANGUAGE_MACEDONIAN  = 0x63,
  CDTEXT_LANGUAGE_LAOTIAN     = 0x64,
  CDTEXT_LANGUAGE_KOREAN      = 0x65,
  CDTEXT_LANGUAGE_KHMER       = 0x66,
  CDTEXT_LANGUAGE_KAZAKH      = 0x67,
  CDTEXT_LANGUAGE_KANNADA     = 0x68,
  CDTEXT_LANGUAGE_JAPANESE    = 0x69,
  CDTEXT_LANGUAGE_INDONESIAN  = 0x6A,
  CDTEXT_LANGUAGE_HINDI       = 0x6B,
  CDTEXT_LANGUAGE_HEBREW      = 0x6C,
  CDTEXT_LANGUAGE_HAUSA       = 0x6D,
  CDTEXT_LANGUAGE_GURANI      = 0x6E,
  CDTEXT_LANGUAGE_GUJURATI    = 0x6F,
  CDTEXT_LANGUAGE_GREEK       = 0x70,
  CDTEXT_LANGUAGE_GEORGIAN    = 0x71,
  CDTEXT_LANGUAGE_FULANI      = 0x72,
  CDTEXT_LANGUAGE_DARI        = 0x73,
  CDTEXT_LANGUAGE_CHURASH     = 0x74,
  CDTEXT_LANGUAGE_CHINESE     = 0x75,
  CDTEXT_LANGUAGE_BURMESE     = 0x76,
  CDTEXT_LANGUAGE_BULGARIAN   = 0x77,
  CDTEXT_LANGUAGE_BENGALI     = 0x78,
  CDTEXT_LANGUAGE_BIELORUSSIAN = 0x79,
  CDTEXT_LANGUAGE_BAMBORA     = 0x7A,
  CDTEXT_LANGUAGE_AZERBAIJANI = 0x7B,
  CDTEXT_LANGUAGE_ASSAMESE    = 0x7C,
  CDTEXT_LANGUAGE_ARMENIAN    = 0x7D,
  CDTEXT_LANGUAGE_ARABIC      = 0x7E,
  CDTEXT_LANGUAGE_AMHARIC     = 0x7F
} cdtext_lang_t;

/*!
  Opaque type for CD-Text.
*/
typedef struct cdtext_s cdtext_t;

/*!
  Return string representation of the given genre code.
*/
const char *cdtext_genre2str (cdtext_genre_t i);

/*!
  Return string representation of the given language code.
*/
const char *cdtext_lang2str (cdtext_lang_t i);

/*!
  Return string representation of given field type.
*/
const char *cdtext_field2str (cdtext_field_t i);

/*! 
  Initialize a new cdtext structure.

  When the structure is no longer needed, release the 
  resources using cdtext_delete.
*/
cdtext_t *cdtext_init (void);

/*!
  Read a binary CD-TEXT and fill a cdtext struct.

  @param p_cdtext the CD-TEXT object
  @param wdata the data
  @param i_data size of wdata

  @returns 0 on success, non-zero on failure
*/       
int cdtext_data_init(cdtext_t *p_cdtext, uint8_t *wdata, size_t i_data);

/*!
  Free memory associated with the given cdtext_t object.

  @param p_cdtext the CD-TEXT object 
*/
void cdtext_destroy (cdtext_t *p_cdtext);

/*!
  Returns a copy of the return value of cdtext_get_const or NULL.

  Must be freed using cdio_free() when done.
  @see cdtext_get_const
*/
char *cdtext_get (const cdtext_t *p_cdtext, cdtext_field_t key, track_t track);

/*!
  Returns value of the given field.

  NULL is returned if key is CDTEXT_INVALID or the field is not set.
  Strings are encoded in UTF-8.

  @param p_cdtext the CD-TEXT object
  @param field type of the field to return
  @param track specifies the track, 0 stands for disc
*/
const char *cdtext_get_const (const cdtext_t *p_cdtext, cdtext_field_t field, 
                              track_t track);

/*!
  Returns the discs genre code.

  @param p_cdtext the CD-TEXT object
*/
cdtext_genre_t cdtext_get_genre (const cdtext_t *p_cdtext);

/*!
  Returns the currently active language.

  @param p_cdtext the CD-TEXT object
*/
cdtext_lang_t cdtext_get_language (const cdtext_t *p_cdtext);

/*!
  Returns the first track number.

  @param p_cdtext the CD-TEXT object
*/
track_t cdtext_get_first_track(const cdtext_t *p_cdtext);

/*!
  Returns the last track number.

  @param p_cdtext the CD-TEXT object
*/
track_t cdtext_get_last_track(const cdtext_t *p_cdtext);

/*!
  Try to select the given language.

  @param p_cdtext the CD-TEXT object
  @param language string representation of the language

  @return true on success, false if language is not available
*/
bool cdtext_select_language(cdtext_t *p_cdtext, cdtext_lang_t language);

/*
  Returns a list of available languages or NULL.

  Internally the list is stored in a static array.

  @param p_cdtext the CD-TEXT object
*/
cdtext_lang_t *cdtext_list_languages (const cdtext_t *p_cdtext);

/*! 
  Sets the given field at the given track to the given value.
  
  Recodes to UTF-8 if charset is not NULL.
  
  @param p_cdtext the CD-TEXT object
  @param key field to set
  @param value value to set
  @param track track to work on
  @param charset charset to convert from
 */
void cdtext_set (cdtext_t *p_cdtext, cdtext_field_t key, const uint8_t *value, track_t track, const char *charset);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CDIO_CDTEXT_H_ */

/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
