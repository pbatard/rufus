/*
    GNU fdisk - a clone of Linux fdisk.

    Copyright © 2020 Pete Batard <pete@akeo.ie>
    Copyright © 2006 Free Software Foundation, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

#include <inttypes.h>

#pragma once

typedef struct {
	const uint8_t type;
	const char *name;
} mbr_type_t;

/*
 * File system types for MBR partition tables
 * See http://en.wikipedia.org/wiki/Partition_type
 * Also http://www.win.tue.nl/~aeb/partitions/partition_types-1.html
 * Note: If googling APTI (Alternative Partition Table Identification)
 * doesn't return squat, then IT ISN'T A REAL THING!!
 */
mbr_type_t mbr_type[] = {
	{ 0x00, "Empty" },
	{ 0x01, "FAT12" },
	{ 0x02, "XENIX root" },
	{ 0x03, "XENIX usr" },
	{ 0x04, "Small FAT16" },
	{ 0x05, "Extended" },
	{ 0x06, "FAT16" },
	{ 0x07, "NTFS/exFAT/UDF" },
	{ 0x08, "AIX" },
	{ 0x09, "AIX Bootable" },
	{ 0x0a, "OS/2 Boot Manager" },
	{ 0x0b, "FAT32" },
	{ 0x0c, "FAT32 LBA" },
	{ 0x0e, "FAT16 LBA" },
	{ 0x0f, "Extended LBA" },
	{ 0x10, "OPUS" },
	{ 0x11, "Hidden FAT12" },
	{ 0x12, "Compaq Diagnostics" },
	{ 0x14, "Hidden Small FAT16" },
	{ 0x16, "Hidden FAT16" },
	{ 0x17, "Hidden NTFS" },
	{ 0x18, "AST SmartSleep" },
	{ 0x1b, "Hidden FAT32" },
	{ 0x1c, "Hidden FAT32 LBA" },
	{ 0x1e, "Hidden FAT16 LBA" },
	{ 0x20, "Windows Mobile XIP" },
	{ 0x21, "SpeedStor" },
	{ 0x23, "Windows Mobile XIP" },
	{ 0x24, "NEC DOS" },
	{ 0x25, "Windows Mobile IMGFS" },
	{ 0x27, "Hidden NTFS WinRE" },
	{ 0x39, "Plan 9" },
	{ 0x3c, "PMagic Recovery" },
	{ 0x40, "Venix 80286" },
	{ 0x41, "PPC PReP Boot" },
	{ 0x42, "SFS" },
	{ 0x4d, "QNX4.x" },
	{ 0x4e, "QNX4.x" },
	{ 0x4f, "QNX4.x" },
	{ 0x50, "OnTrack DM" },
	{ 0x51, "OnTrack DM" },
	{ 0x52, "CP/M" },
	{ 0x53, "OnTrack DM" },
	{ 0x54, "OnTrack DM" },
	{ 0x55, "EZ Drive" },
	{ 0x56, "Golden Bow" },
	{ 0x5c, "Priam EDisk" },
	{ 0x61, "SpeedStor" },
	{ 0x63, "GNU HURD/SysV" },
	{ 0x64, "Netware" },
	{ 0x65, "Netware" },
	{ 0x66, "Netware" },
	{ 0x67, "Netware" },
	{ 0x68, "Netware" },
	{ 0x69, "Netware" },
	{ 0x70, "DiskSecure MultiBoot" },
	{ 0x75, "PC/IX" },
	{ 0x77, "Novell" },
	{ 0x78, "XOSL" },
	{ 0x7e, "F.I.X." },
	{ 0x7e, "AODPS" },
	{ 0x80, "Minix" },
	{ 0x81, "Minix" },
	{ 0x82, "GNU/Linux Swap" },
	{ 0x83, "GNU/Linux" },
	{ 0x84, "Windows Hibernation" },
	{ 0x85, "GNU/Linux Extended" },
	{ 0x86, "NTFS Volume Set" },
	{ 0x87, "NTFS Volume Set" },
	{ 0x88, "GNU/Linux Plaintext" },
	{ 0x8d, "FreeDOS Hidden FAT12" },
	{ 0x8e, "GNU/Linux LVM" },
	{ 0x90, "FreeDOS Hidden FAT16" },
	{ 0x91, "FreeDOS Hidden Extended" },
	{ 0x92, "FreeDOS Hidden FAT16" },
	{ 0x93, "GNU/Linux Hidden" },
	{ 0x96, "CHRP ISO-9660" },
	{ 0x97, "FreeDOS Hidden FAT32" },
	{ 0x98, "FreeDOS Hidden FAT32" },
	{ 0x9a, "FreeDOS Hidden FAT16" },
	{ 0x9b, "FreeDOS Hidden Extended" },
	{ 0x9f, "BSD/OS" },
	{ 0xa0, "Hibernation" },
	{ 0xa1, "Hibernation" },
	{ 0xa2, "SpeedStor" },
	{ 0xa3, "SpeedStor" },
	{ 0xa4, "SpeedStor" },
	{ 0xa5, "FreeBSD" },
	{ 0xa6, "OpenBSD" },
	{ 0xa7, "NeXTSTEP" },
	{ 0xa8, "Darwin UFS" },
	{ 0xa9, "NetBSD" },
	{ 0xab, "Darwin Boot" },
	{ 0xaf, "HFS/HFS+" },
	{ 0xb0, "BootStar Dummy" },
	{ 0xb1, "QNX" },
	{ 0xb2, "QNX" },
	{ 0xb3, "QNX" },
	{ 0xb4, "SpeedStor" },
	{ 0xb6, "SpeedStor" },
	{ 0xb7, "BSDI" },
	{ 0xb8, "BSDI Swap" },
	{ 0xbb, "BootWizard Hidden" },
	{ 0xbc, "Acronis SZ" },
	{ 0xbe, "Solaris Boot" },
	{ 0xbf, "Solaris" },
	{ 0xc0, "Secured FAT" },
	{ 0xc1, "DR DOS FAT12" },
	{ 0xc2, "GNU/Linux Hidden" },
	{ 0xc3, "GNU/Linux Hidden Swap" },
	{ 0xc4, "DR DOS FAT16" },
	{ 0xc4, "DR DOS Extended" },
	{ 0xc6, "DR DOS FAT16" },
	{ 0xc7, "Syrinx" },
	{ 0xcd, "ISOHybrid" },
	{ 0xda, "Non-FS Data" },
	{ 0xdb, "CP/M" },
	{ 0xde, "Dell Utility" },
	{ 0xdf, "BootIt" },
	{ 0xe0, "ST AVFS" },
	{ 0xe1, "SpeedStor" },
	{ 0xe3, "SpeedStor" },
	{ 0xe4, "SpeedStor" },
	{ 0xe6, "SpeedStor" },
	{ 0xe8, "LUKS" },
	{ 0xea, "Rufus Extra" },
	{ 0xeb, "BeOS/Haiku" },
	{ 0xec, "SkyFS" },
	{ 0xed, "GPT Hybrid MBR" },
	{ 0xee, "GPT Protective MBR" },
	{ 0xef, "EFI System Partition" },
	{ 0xf0, "PA-RISC Boot" },
	{ 0xf1, "SpeedStor" },
	{ 0xf2, "DOS secondary" },
	{ 0xf3, "SpeedStor" },
	{ 0xf4, "SpeedStor" },
	{ 0xf6, "SpeedStor" },
	{ 0xfa, "Bochs" },
	{ 0xfb, "VMware VMFS" },
	{ 0xfc, "VMware VMKCORE" },
	{ 0xfd, "GNU/Linux RAID Auto" },
	{ 0xfe, "LANstep" },
	{ 0xff, "XENIX BBT" },
	{ 0, NULL }
};
