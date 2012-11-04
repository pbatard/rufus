/*
    Copyright (C) 2008, 2012 Rocky Bernstein <rocky@gnu.org>
                  2001, 2004, 2005 Herbert Valerio Riedel <hvr@gnu.org>
                  2001 Sven Ottemann <ac-logic@freenet.de>

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

/** \file bytesex_asm.h 
 *  \brief  Assembly code to handle byte-swapping.

    Note: this header will is slated to get removed and libcdio will use 
    glib.h routines instead. 
*/

#ifndef CDIO_BYTESEX_ASM_H_
#define CDIO_BYTESEX_ASM_H_
#if !defined(DISABLE_ASM_OPTIMIZE)

#include <cdio/types.h>

#if !defined CDIO_INLINE
#if defined(__cplusplus) || defined(inline)
#define CDIO_INLINE inline
#elif defined(__GNUC__)
#define CDIO_INLINE __inline__
#elif defined(_MSC_VER)
#define CDIO_INLINE __inline
#else
#define CDIO_INLINE
#endif
#endif /* CDIO_INLINE */

#if defined(__powerpc__) && defined(__GNUC__)

static CDIO_INLINE
uint32_t uint32_swap_le_be_asm(const uint32_t a)
{
  uint32_t b;

  __asm__ ("lwbrx %0,0,%1"
           :"=r"(b)
           :"r"(&a), "m"(a));

  return b;
}

static CDIO_INLINE
uint16_t uint16_swap_le_be_asm(const uint16_t a)
{
  uint32_t b;

  __asm__ ("lhbrx %0,0,%1"
           :"=r"(b)
           :"r"(&a), "m"(a));

  return b;
}

#define UINT16_SWAP_LE_BE uint16_swap_le_be_asm
#define UINT32_SWAP_LE_BE uint32_swap_le_be_asm

#elif defined(__mc68000__) &&  defined(__STORMGCC__)

static CDIO_INLINE
uint32_t uint32_swap_le_be_asm(uint32_t a __asm__("d0"))
{
  /* __asm__("rolw #8,%0; swap %0; rolw #8,%0" : "=d" (val) : "0" (val)); */

  __asm__("move.l %1,d0;rol.w #8,d0;swap d0;rol.w #8,d0;move.l d0,%0"
          :"=r"(a)
          :"r"(a));

  return(a);
}

static CDIO_INLINE
uint16_t uint16_swap_le_be_asm(uint16_t a __asm__("d0"))
{
  __asm__("move.l %1,d0;rol.w #8,d0;move.l d0,%0"
          :"=r"(a)
          :"r"(a));
  
  return(a);
}

#define UINT16_SWAP_LE_BE uint16_swap_le_be_asm
#define UINT32_SWAP_LE_BE uint32_swap_le_be_asm

#elif 0 && defined(__i386__) && defined(__GNUC__)

static CDIO_INLINE
uint32_t uint32_swap_le_be_asm(uint32_t a)
{
  __asm__("xchgb %b0,%h0\n\t"     /* swap lower bytes     */
          "rorl $16,%0\n\t"       /* swap words           */
          "xchgb %b0,%h0"         /* swap higher bytes    */
          :"=q" (a)
          : "0" (a));

  return(a);
}

static CDIO_INLINE
uint16_t uint16_swap_le_be_asm(uint16_t a)
{
  __asm__("xchgb %b0,%h0"         /* swap bytes           */ 
          : "=q" (a) 
          :  "0" (a));
  
  return(a);
}

#define UINT16_SWAP_LE_BE uint16_swap_le_be_asm
#define UINT32_SWAP_LE_BE uint32_swap_le_be_asm

#endif

#endif /* !defined(DISABLE_ASM_OPTIMIZE) */
#endif /* CDIO_BYTESEX_ASM_H_ */


/* 
 * Local variables:
 *  c-file-style: "gnu"
 *  tab-width: 8
 *  indent-tabs-mode: nil
 * End:
 */
