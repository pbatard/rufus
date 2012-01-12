/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2001-2008 H. Peter Anvin - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * ulint.h
 *
 * Basic operations on unaligned, littleendian integers
 */

#ifndef ULINT_H
#define ULINT_H

#include <inttypes.h>

/* These are unaligned, littleendian integer types */

typedef uint8_t le8_t;		/*  8-bit byte */
typedef uint8_t le16_t[2];	/* 16-bit word */
typedef uint8_t le32_t[4];	/* 32-bit dword */

/* Read/write these quantities */

static inline unsigned char read8(le8_t * _p)
{
    return *_p;
}

static inline void write8(le8_t * _p, uint8_t _v)
{
    *_p = _v;
}

#if defined(__i386__) || defined(__x86_64__)

/* Littleendian architectures which support unaligned memory accesses */

static inline unsigned short read16(le16_t * _p)
{
    return *((const uint16_t *)_p);
}

static inline void write16(le16_t * _p, unsigned short _v)
{
    *((uint16_t *) _p) = _v;
}

static inline unsigned int read32(le32_t * _p)
{
    return *((const uint32_t *)_p);
}

static inline void write32(le32_t * _p, uint32_t _v)
{
    *((uint32_t *) _p) = _v;
}

#else

/* Generic, mostly portable versions */

static inline unsigned short read16(le16_t * _pp)
{
    uint8_t *_p = *_pp;
    uint16_t _v;

    _v = _p[0];
    _v |= _p[1] << 8;
    return _v;
}

static inline void write16(le16_t * _pp, uint16_t _v)
{
    uint8_t *_p = *_pp;

    _p[0] = _v & 0xFF;
    _p[1] = (_v >> 8) & 0xFF;
}

static inline unsigned int read32(le32_t * _pp)
{
    uint8_t *_p = *_pp;
    uint32_t _v;

    _v = _p[0];
    _v |= _p[1] << 8;
    _v |= _p[2] << 16;
    _v |= _p[3] << 24;
    return _v;
}

static inline void write32(le32_t * _pp, uint32_t _v)
{
    uint8_t *_p = *_pp;

    _p[0] = _v & 0xFF;
    _p[1] = (_v >> 8) & 0xFF;
    _p[2] = (_v >> 16) & 0xFF;
    _p[3] = (_v >> 24) & 0xFF;
}

#endif

#endif /* ULINT_H */
