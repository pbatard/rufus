/*
    Copyright (C) 2000, 2004 Herbert Valerio Riedel <hvr@gnu.org>
    Copyright (C) 2005, 2008, 2012, 2015 Rocky Bernstein <rocky@gnu.org>

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

/** \file bytesex.h
 *  \brief  Generic Byte-swapping routines.
 *
 *   Note: this header will is slated to get removed and libcdio will
 *   use glib.h routines instead.
*/

#ifndef CDIO_BYTESEX_H_
#define CDIO_BYTESEX_H_

#include <cdio/types.h>
#include <cdio/bytesex_asm.h> /* also defines CDIO_INLINE */
#include <cdio/logging.h>

/** 16-bit big-endian to little-endian */
#define UINT16_SWAP_LE_BE_C(val) ((uint16_t) ( \
    (((uint16_t) (val) & (uint16_t) 0x00ffU) << 8) | \
    (((uint16_t) (val) & (uint16_t) 0xff00U) >> 8)))

/** 32-bit big-endian to little-endian */
#define UINT32_SWAP_LE_BE_C(val) ((uint32_t) ( \
    (((uint32_t) (val) & (uint32_t) 0x000000ffU) << 24) | \
    (((uint32_t) (val) & (uint32_t) 0x0000ff00U) <<  8) | \
    (((uint32_t) (val) & (uint32_t) 0x00ff0000U) >>  8) | \
    (((uint32_t) (val) & (uint32_t) 0xff000000U) >> 24)))

/** 64-bit big-endian to little-endian */
#define UINT64_SWAP_LE_BE_C(val) ((uint64_t) ( \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x00000000000000ff)) << 56) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x000000000000ff00)) << 40) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x0000000000ff0000)) << 24) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x00000000ff000000)) <<  8) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x000000ff00000000)) >>  8) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0x00ff000000000000)) >> 40) | \
    (((uint64_t) (val) & (uint64_t) UINT64_C(0xff00000000000000)) >> 56)))

#ifndef UINT16_SWAP_LE_BE
# define UINT16_SWAP_LE_BE UINT16_SWAP_LE_BE_C
#endif

#ifndef UINT32_SWAP_LE_BE
# define UINT32_SWAP_LE_BE UINT32_SWAP_LE_BE_C
#endif

#ifndef UINT64_SWAP_LE_BE
# define UINT64_SWAP_LE_BE UINT64_SWAP_LE_BE_C
#endif

static CDIO_INLINE
uint16_t uint16_swap_le_be (const uint16_t val)
{
  return UINT16_SWAP_LE_BE (val);
}

static CDIO_INLINE
uint32_t uint32_swap_le_be (const uint32_t val)
{
  return UINT32_SWAP_LE_BE (val);
}

static CDIO_INLINE
uint64_t uint64_swap_le_be (const uint64_t val)
{
  return UINT64_SWAP_LE_BE (val);
}

# define UINT8_TO_BE(val)      ((uint8_t) (val))
# define UINT8_TO_LE(val)      ((uint8_t) (val))
#ifdef WORDS_BIGENDIAN
# define UINT16_TO_BE(val)     ((uint16_t) (val))
# define UINT16_TO_LE(val)     ((uint16_t) UINT16_SWAP_LE_BE(val))

# define UINT32_TO_BE(val)     ((uint32_t) (val))
# define UINT32_TO_LE(val)     ((uint32_t) UINT32_SWAP_LE_BE(val))

# define UINT64_TO_BE(val)     ((uint64_t) (val))
# define UINT64_TO_LE(val)     ((uint64_t) UINT64_SWAP_LE_BE(val))
#else
# define UINT16_TO_BE(val)     ((uint16_t) UINT16_SWAP_LE_BE(val))
# define UINT16_TO_LE(val)     ((uint16_t) (val))

# define UINT32_TO_BE(val)     ((uint32_t) UINT32_SWAP_LE_BE(val))
# define UINT32_TO_LE(val)     ((uint32_t) (val))

# define UINT64_TO_BE(val)     ((uint64_t) UINT64_SWAP_LE_BE(val))
# define UINT64_TO_LE(val)     ((uint64_t) (val))
#endif

/** symmetric conversions */
#define UINT8_FROM_BE(val)     (UINT8_TO_BE (val))
#define UINT8_FROM_LE(val)     (UINT8_TO_LE (val))
#define UINT16_FROM_BE(val)    (UINT16_TO_BE (val))
#define UINT16_FROM_LE(val)    (UINT16_TO_LE (val))
#define UINT32_FROM_BE(val)    (UINT32_TO_BE (val))
#define UINT32_FROM_LE(val)    (UINT32_TO_LE (val))
#define UINT64_FROM_BE(val)    (UINT64_TO_BE (val))
#define UINT64_FROM_LE(val)    (UINT64_TO_LE (val))

/** converter function template */
#define CVT_TO_FUNC(bits) \
 static CDIO_INLINE uint ## bits ## _t \
 uint ## bits ## _to_be (uint ## bits ## _t val) \
 { return UINT ## bits ## _TO_BE (val); } \
 static CDIO_INLINE uint ## bits ## _t \
 uint ## bits ## _to_le (uint ## bits ## _t val) \
 { return UINT ## bits ## _TO_LE (val); } \

CVT_TO_FUNC(8)
CVT_TO_FUNC(16)
CVT_TO_FUNC(32)
CVT_TO_FUNC(64)

#undef CVT_TO_FUNC

#define uint8_from_be(val)     (uint8_to_be (val))
#define uint8_from_le(val)     (uint8_to_le (val))
#define uint16_from_be(val)    (uint16_to_be (val))
#define uint16_from_le(val)    (uint16_to_le (val))
#define uint32_from_be(val)    (uint32_to_be (val))
#define uint32_from_le(val)    (uint32_to_le (val))
#define uint64_from_be(val)    (uint64_to_be (val))
#define uint64_from_le(val)    (uint64_to_le (val))

/** ISO9660-related field conversion routines */

/** Convert from uint8_t to ISO 9660 7.1.1 format */
#define to_711(i)   uint8_to_le(i)

/** Convert from ISO 9660 7.1.1 format to uint8_t */
#define from_711(i) uint8_from_le(i)

/** Convert from uint16_t to ISO 9669 7.2.1 format */
#define to_721(i)   uint16_to_le(i)

/** Convert from ISO 9660 7.2.1 format to uint16_t */
#define from_721(i) uint16_from_le(i)

/** Convert from uint16_t to ISO 9669 7.2.2 format */
#define to_722(i)   uint16_to_be(i)

/** Convert from ISO 9660 7.2.2 format to uint16_t */
#define from_722(i) uint16_from_be(i)

/** Convert from uint16_t to ISO 9669 7.2.3 format */
static CDIO_INLINE uint32_t
to_723(uint16_t i)
{
  return uint32_swap_le_be(i) | i;
}

/** Convert from ISO 9660 7.2.3 format to uint16_t */
static CDIO_INLINE uint16_t
from_723 (uint32_t p)
{
  uint8_t *u = (uint8_t *) &p;
  /* Return the little-endian part always, to handle non-specs-compliant images */
  return (u[0] | (u[1] << 8));
}

static CDIO_INLINE uint16_t
from_723_with_err (uint32_t p, bool *err)
{
  if (uint32_swap_le_be (p) != p) {
    cdio_warn ("from_723: broken byte order");
    *err = true;
  } else {
    *err = false;
  }
  return (0xFFFF & p);
}

/** Convert from uint16_t to ISO 9669 7.3.1 format */
#define to_731(i)   uint32_to_le(i)

/** Convert from ISO 9660 7.3.1 format to uint32_t */
#define from_731(i) uint32_from_le(i)

/** Convert from uint32_t to ISO 9669 7.3.2 format */
#define to_732(i)   uint32_to_be(i)

/** Convert from ISO 9660 7.3.2 format to uint32_t */
#define from_732(i) uint32_from_be(i)

/** Convert from uint16_t to ISO 9669 7.3.3 format */
static CDIO_INLINE uint64_t
to_733(uint32_t i)
{
  return uint64_swap_le_be(i) | i;
}

/** Convert from ISO 9660 7.3.3 format to uint32_t */
static CDIO_INLINE uint32_t
from_733 (uint64_t p)
{
  uint8_t *u = (uint8_t *) &p;
  /* Return the little-endian part always, to handle non-specs-compliant images */
  return (u[0] | (u[1] << 8) | (u[2] << 16) | (u[3] << 24));
}

static CDIO_INLINE uint32_t
from_733_with_err (uint64_t p, bool *err)
{
  if (uint64_swap_le_be (p) != p) {
    cdio_warn ("from_733: broken byte order");
    *err = true;
  } else {
    *err = false;
  }
  return (UINT32_C(0xFFFFFFFF) & p);
}

#endif /* CDIO_BYTESEX_H_ */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
