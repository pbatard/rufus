/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2007-2008 H. Peter Anvin - All Rights Reserved
 *   Copyright 2009-2011 Intel Corporation; author: H. Peter Anvin
 *   Copyright 2011 Paulo Alcantara <pcacjr@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 53 Temple Place Ste 330,
 *   Boston MA 02111-1307, USA; either version 2 of the License, or
 *   (at your option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

#ifndef SYSLXINT_H
#define SYSLXINT_H

#include "syslinux.h"

#if defined(__386__) || defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
# define X86_MEM 1		/* Littleendian and unaligned safe */
#else
# define X86_MEM 0
#endif

#ifdef __GNUC__
# ifdef __MINGW32__
   /* gcc 4.7 miscompiles packed structures in MS-bitfield mode */
#  define GNUC_PACKED __attribute__((packed,gcc_struct))
# else
#  define GNUC_PACKED __attribute__((packed))
# endif
# define PRAGMA_BEGIN_PACKED
# define PRAGMA_END_PACKED
#elif defined(_MSC_VER)
# define GNUC_PACKED
# define PRAGMA_BEGIN_PACKED __pragma(pack(push, 1))
# define PRAGMA_END_PACKED   __pragma(pack(pop))
#else
# error "Need to define PACKED for this compiler"
#endif

/*
 * Access functions for littleendian numbers, possibly misaligned.
 */
static inline uint8_t get_8(const uint8_t * p)
{
    return *p;
}

static inline uint16_t get_16(const uint16_t * p)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    return *p;
#else
    const uint8_t *pp = (const uint8_t *)p;
    return pp[0] + ((uint16_t)pp[1] << 8);
#endif
}

static inline uint32_t get_32(const uint32_t * p)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    return *p;
#else
    const uint16_t *pp = (const uint16_t *)p;
    return get_16(pp[0]) + (uint32_t)get_16(pp[1]);
#endif
}

static inline uint64_t get_64(const uint64_t * p)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    return *p;
#else
    const uint32_t *pp = (const uint32_t *)p;
    return get_32(pp[0]) + (uint64_t)get_32(pp[1]);
#endif
}

static inline void set_8(uint8_t *p, uint8_t v)
{
    *p = v;
}

static inline void set_16(uint16_t *p, uint16_t v)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    *p = v;
#else
    uint8_t *pp = (uint8_t *) p;
    pp[0] = (v & 0xff);
    pp[1] = ((v >> 8) & 0xff);
#endif
}

static inline void set_32(uint32_t *p, uint32_t v)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    *p = v;
#else
    uint8_t *pp = (uint8_t *) p;
    pp[0] = (v & 0xff);
    pp[1] = ((v >> 8) & 0xff);
    pp[2] = ((v >> 16) & 0xff);
    pp[3] = ((v >> 24) & 0xff);
#endif
}

static inline void set_64(uint64_t *p, uint64_t v)
{
#if X86_MEM
    /* Littleendian and unaligned-capable */
    *p = v;
#else
    uint32_t *pp = (uint32_t *) p;
    set_32(pp[0], v);
    set_32(pp[1], v >> 32);
#endif
}

/*
 * Special handling for the MS-DOS derivative: syslinux_ldlinux
 * is a "far" object...
 */
#ifdef __MSDOS__

static inline __attribute__ ((const))
uint16_t ds(void)
{
    uint16_t v;
    asm("movw %%ds,%0":"=rm"(v));
    return v;
}

static inline void *set_fs(const void *p)
{
    uint16_t seg;

    seg = ds() + ((size_t) p >> 4);
    asm volatile ("movw %0,%%fs"::"rm" (seg));
    return (void *)((size_t) p & 0xf);
}

uint8_t get_8_sl(const uint8_t * p);
uint16_t get_16_sl(const uint16_t * p);
uint32_t get_32_sl(const uint32_t * p);
uint64_t get_64_sl(const uint64_t * p);
void set_8_sl(uint8_t * p, uint8_t v);
void set_16_sl(uint16_t * p, uint16_t v);
void set_32_sl(uint32_t * p, uint32_t v);
void set_64_sl(uint64_t * p, uint64_t v);
void memcpy_to_sl(void *dst, const void *src, size_t len);
void memcpy_from_sl(void *dst, const void *src, size_t len);

#else

/* Sane system ... */
#define get_8_sl(x)    		get_8(x)
#define get_16_sl(x)   		get_16(x)
#define get_32_sl(x)   		get_32(x)
#define get_64_sl(x)   		get_64(x)
#define set_8_sl(x,y)  		set_8(x,y)
#define set_16_sl(x,y) 		set_16(x,y)
#define set_32_sl(x,y) 		set_32(x,y)
#define set_64_sl(x,y) 		set_64(x,y)
#define memcpy_to_sl(d,s,l)	memcpy(d,s,l)
#define memcpy_from_sl(d,s,l)	memcpy(d,s,l)

#endif

#define LDLINUX_MAGIC	0x3eb202fe
#define BS_MAGIC_VER	(0x1b << 9)

/* Patch area for disk-based installers */
struct patch_area {
    uint32_t magic;		/* LDLINUX_MAGIC */
    uint32_t instance;		/* Per-version value */
    uint16_t data_sectors;
    uint16_t adv_sectors;
    uint32_t dwords;
    uint32_t checksum;
    uint16_t maxtransfer;
    uint16_t epaoffset;		/* Pointer to the extended patch area */
};

struct ext_patch_area {
    uint16_t advptroffset;	/* ADV pointers */
    uint16_t diroffset;		/* Current directory field */
    uint16_t dirlen;		/* Length of current directory field */
    uint16_t subvoloffset;	/* Subvolume field */
    uint16_t subvollen;		/* Length of subvolume field */
    uint16_t secptroffset;	/* Sector extent pointers */
    uint16_t secptrcnt;		/* Number of sector extent pointers */

    uint16_t sect1ptr0;		/* Boot sector offset of sector 1 ptr LSW */
    uint16_t sect1ptr1;		/* Boot sector offset of sector 1 ptr MSW */
    uint16_t raidpatch;		/* Boot sector RAID mode patch pointer */
};

/* Sector extent */
PRAGMA_BEGIN_PACKED
struct syslinux_extent {
    uint64_t lba;
    uint16_t len;
} GNUC_PACKED;

/* FAT bootsector format, also used by other disk-based derivatives */
struct fat_boot_sector {
    uint8_t bsJump[3];
    char bsOemName[8];
    uint16_t bsBytesPerSec;
    uint8_t bsSecPerClust;
    uint16_t bsResSectors;
    uint8_t bsFATs;
    uint16_t bsRootDirEnts;
    uint16_t bsSectors;
    uint8_t bsMedia;
    uint16_t bsFATsecs;
    uint16_t bsSecPerTrack;
    uint16_t bsHeads;
    uint32_t bsHiddenSecs;
    uint32_t bsHugeSectors;

    union {
	struct {
	    uint8_t DriveNumber;
	    uint8_t Reserved1;
	    uint8_t BootSignature;
	    uint32_t VolumeID;
	    char VolumeLabel[11];
	    char FileSysType[8];
	    uint8_t Code[442];
	} GNUC_PACKED bs16;
	struct {
	    uint32_t FATSz32;
	    uint16_t ExtFlags;
	    uint16_t FSVer;
	    uint32_t RootClus;
	    uint16_t FSInfo;
	    uint16_t BkBootSec;
	    uint8_t Reserved0[12];
	    uint8_t DriveNumber;
	    uint8_t Reserved1;
	    uint8_t BootSignature;
	    uint32_t VolumeID;
	    char VolumeLabel[11];
	    char FileSysType[8];
	    uint8_t Code[414];
	} GNUC_PACKED bs32;
    } GNUC_PACKED;

    uint32_t bsMagic;
    uint16_t bsForwardPtr;
    uint16_t bsSignature;
} GNUC_PACKED;

/* NTFS bootsector format */
struct ntfs_boot_sector {
    uint8_t bsJump[3];
    char bsOemName[8];
    uint16_t bsBytesPerSec;
    uint8_t bsSecPerClust;
    uint16_t bsResSectors;
    uint8_t bsZeroed_0[3];
    uint16_t bsZeroed_1;
    uint8_t bsMedia;
    uint16_t bsZeroed_2;
    uint16_t bsUnused_0;
    uint16_t bsUnused_1;
    uint32_t bsUnused_2;
    uint32_t bsZeroed_3;
    uint32_t bsUnused_3;
    uint64_t bsTotalSectors;
    uint64_t bsMFTLogicalClustNr;
    uint64_t bsMFTMirrLogicalClustNr;
    uint8_t bsClustPerMFTrecord;
    uint8_t bsUnused_4[3];
    uint8_t bsClustPerIdxBuf;
    uint8_t bsUnused_5[3];
    uint64_t bsVolSerialNr;
    uint32_t bsUnused_6;

    uint8_t Code[420];

    uint32_t bsMagic;
    uint16_t bsForwardPtr;
    uint16_t bsSignature;
} GNUC_PACKED;
PRAGMA_END_PACKED

#define FAT_bsHead      bsJump
#define FAT_bsHeadLen   offsetof(struct fat_boot_sector, bsBytesPerSec)
#define FAT_bsCode	    bs32.Code	/* The common safe choice */
#define FAT_bsCodeLen   (offsetof(struct fat_boot_sector, bsSignature) - \
		     offsetof(struct fat_boot_sector, FAT_bsCode))

#define NTFS_bsHead     bsJump
#define NTFS_bsHeadLen  offsetof(struct ntfs_boot_sector, bsOemName)
#define NTFS_bsCode     Code
#define NTFS_bsCodeLen  (offsetof(struct ntfs_boot_sector, bsSignature) - \
                            offsetof(struct ntfs_boot_sector, NTFS_bsCode))

/* Check if there are specific zero fields in an NTFS boot sector */
static inline int ntfs_check_zero_fields(const struct ntfs_boot_sector *sb)
{
    return !sb->bsResSectors && (!sb->bsZeroed_0[0] && !sb->bsZeroed_0[1] &&
            !sb->bsZeroed_0[2]) && !sb->bsZeroed_1 && !sb->bsZeroed_2 &&
            !sb->bsZeroed_3;
}

static inline int ntfs_check_sb_fields(const struct ntfs_boot_sector *sb)
{
    return ntfs_check_zero_fields(sb) &&
            (!memcmp(sb->bsOemName, "NTFS    ", 8) ||
             !memcmp(sb->bsOemName, "MSWIN4.0", 8) ||
             !memcmp(sb->bsOemName, "MSWIN4.1", 8));
}

static inline int fat_check_sb_fields(const struct fat_boot_sector *sb)
{
    return sb->bsResSectors && sb->bsFATs &&
            (!memcmp(sb->bs16.FileSysType, "FAT12   ", 8) ||
             !memcmp(sb->bs16.FileSysType, "FAT16   ", 8) ||
             !memcmp(sb->bs16.FileSysType, "FAT     ", 8) ||
             !memcmp(sb->bs32.FileSysType, "FAT32   ", 8));
}

#endif /* SYSLXINT_H */
