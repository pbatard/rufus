/*
 * Rufus: The Reliable USB Formatting Utility
 * MS-DOS boot file extraction, from the FAT12 floppy image in diskcopy.dll
 * Copyright (c) 2011-2012 Pete Batard <pete@akeo.ie>
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

/* http://www.c-jump.com/CIS24/Slides/FAT/lecture.html
   Ideally, we'd read the following from the FAT Boot Sector, but we have
   a pretty good idea of what they are for a 1.44 MB floppy image */
#define FAT12_ROOTDIR_OFFSET        0x2600
#define FAT12_CLUSTER_SIZE          0x200	// = sector size
#define FAT12_DATA_START            0x4200
#define FAT12_CLUSTER_OFFSET        ((FAT12_DATA_START/FAT12_CLUSTER_SIZE)-2)	// First cluster in data area is #2

/* 
 * Lifted from ReactOS:
 * http://reactos-mirror.googlecode.com/svn/trunk/reactos/drivers/filesystems/fastfat_new/fat.h
 */

#pragma pack(push, 1)	// You *DO* want packed structs here

//
//  Directory Structure:
//
typedef struct _FAT_TIME
{
    union {
        struct {
            USHORT DoubleSeconds : 5;
            USHORT Minute : 6;
            USHORT Hour : 5;
        };
        USHORT Value;
    };
} FAT_TIME, *PFAT_TIME;
//
//
//
typedef struct _FAT_DATE {
    union {
        struct {
            USHORT Day : 5;
            USHORT Month : 4;
            /* Relative to 1980 */
            USHORT Year : 7;
        };
        USHORT Value;
    };
} FAT_DATE, *PFAT_DATE;
//
//
//
typedef struct _FAT_DATETIME {
    union {
        struct {
            FAT_TIME    Time;
            FAT_DATE    Date;
        };
        ULONG Value;
    };
} FAT_DATETIME, *PFAT_DATETIME;
//
//
//
typedef struct _DIR_ENTRY
{
    UCHAR FileName[11];
    UCHAR Attributes;
    UCHAR Case;
    UCHAR CreationTimeTenMs;
    FAT_DATETIME CreationDateTime;
    FAT_DATE LastAccessDate;
    union {
        USHORT ExtendedAttributes;
        USHORT FirstClusterOfFileHi;
    };
    FAT_DATETIME LastWriteDateTime;
    USHORT FirstCluster;
    ULONG FileSize;
} DIR_ENTRY, *PDIR_ENTRY;
//  sizeof = 0x020

typedef struct _LONG_FILE_NAME_ENTRY {
    UCHAR SeqNum;
    UCHAR NameA[10];
    UCHAR Attributes;
    UCHAR Type;
    UCHAR Checksum;
    USHORT NameB[6];
    USHORT Reserved;
    USHORT NameC[2];
} LONG_FILE_NAME_ENTRY, *PLONG_FILE_NAME_ENTRY;
//  sizeof = 0x020

#pragma pack(pop)

#define FAT_LFN_NAME_LENGTH \
    (RTL_FIELD_SIZE(LONG_FILE_NAME_ENTRY, NameA) \
        + RTL_FIELD_SIZE(LONG_FILE_NAME_ENTRY, NameB) \
        + RTL_FIELD_SIZE(LONG_FILE_NAME_ENTRY, NameC))

#define FAT_FN_DIR_ENTRY_LAST       0x40
#define FAT_FN_MAX_DIR_ENTIES       0x14

#define FAT_BYTES_PER_DIRENT        0x20
#define FAT_BYTES_PER_DIRENT_LOG    0x05
#define FAT_DIRENT_NEVER_USED       0x00
#define FAT_DIRENT_REALLY_0E5       0x05
#define FAT_DIRENT_DIRECTORY_ALIAS  0x2e
#define FAT_DIRENT_DELETED          0xe5

#define FAT_CASE_LOWER_BASE	        0x08
#define FAT_CASE_LOWER_EXT 	        0x10

#define FAT_DIRENT_ATTR_READ_ONLY        0x01
#define FAT_DIRENT_ATTR_HIDDEN           0x02
#define FAT_DIRENT_ATTR_SYSTEM           0x04
#define FAT_DIRENT_ATTR_VOLUME_ID        0x08
#define FAT_DIRENT_ATTR_DIRECTORY        0x10
#define FAT_DIRENT_ATTR_ARCHIVE          0x20
#define FAT_DIRENT_ATTR_DEVICE           0x40
#define FAT_DIRENT_ATTR_LFN              (FAT_DIRENT_ATTR_READ_ONLY | \
                                          FAT_DIRENT_ATTR_HIDDEN |    \
                                          FAT_DIRENT_ATTR_SYSTEM |    \
                                          FAT_DIRENT_ATTR_VOLUME_ID)

extern BOOL SetDOSLocale(const char* path, BOOL bFreeDOS);
