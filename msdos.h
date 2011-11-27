/*
 * Rufus: The Resourceful USB Formatting Utility
 * MS-DOS boot file extraction, from the FAT12 floppy image in diskcopy.dll
 * Copyright (c) 2011 Pete Batard <pete@akeo.ie>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* http://www.c-jump.com/CIS24/Slides/FAT/lecture.html */
#define FAT12_ROOTDIR_OFFSET        0x2600
#define FAT12_ROOTDIR_ENTRY_SIZE    0x20
#define FAT12_ROOTDIR_NB_ENTRIES    0xE0
#define FAT12_ROOTDIR_FIRSTCLUSTER  0x1A		// No need for high word on a 1.44 MB media
#define FAT12_ROOTDIR_FILESIZE      0x1C
#define FAT12_DELETED_ENTRY         0xE5

/* Ideally, we'd read those from the FAT Boot Sector, but we have
   a pretty good idea of what they are for a 1.44 MB floppy image */
#define FAT12_CLUSTER_SIZE          0x200	// = sector size
#define FAT12_DATA_START            0x4200
#define FAT12_CLUSTER_OFFSET        ((FAT12_DATA_START/FAT12_CLUSTER_SIZE)-2)	// First cluster in data area is #2

#ifndef GET_ULONG_LE
#define GET_ULONG_LE(n,b,i)                     \
{                                               \
    (n) = ( (ULONG) (b)[(i)    ]       )        \
        | ( (ULONG) (b)[(i) + 1] <<  8 )        \
        | ( (ULONG) (b)[(i) + 2] << 16 )        \
        | ( (ULONG) (b)[(i) + 3] << 24 );       \
}
#endif

#ifndef GET_USHORT_LE
#define GET_USHORT_LE(n,b,i)                    \
{                                               \
    (n) = ( (USHORT) (b)[(i)    ]       )       \
        | ( (USHORT) (b)[(i) + 1] <<  8 );      \
}
#endif
