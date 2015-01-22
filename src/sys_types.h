/*
    GNU fdisk - a clone of Linux fdisk.

    Copyright (C) 2006
    Free Software Foundation, Inc.

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

#ifndef SYS_TYPES_H_INCLUDED
#define SYS_TYPES_H_INCLUDED

typedef struct {
	unsigned char type;
	const char *name;
} SysType;

#define N_(String) String

/*
 * File system types for MBR partition tables
 * See http://en.wikipedia.org/wiki/Partition_type
 * Also http://www.win.tue.nl/~aeb/partitions/partition_types-1.html
 * Note: If googling APTI (Alternative Partition Table Identification)
 * doesn't return squat, then IT ISN'T A REAL THING!!
 */
SysType msdos_systypes[] = {
	{ 0x00, N_("Empty") },
	{ 0x01, N_("FAT12") },
	{ 0x02, N_("XENIX root") },
	{ 0x03, N_("XENIX usr") },
	{ 0x04, N_("Small FAT16") },
	{ 0x05, N_("Extended") },
	{ 0x06, N_("FAT16") },
	{ 0x07, N_("NTFS/exFAT/UDF") },
	{ 0x08, N_("AIX") },
	{ 0x09, N_("AIX Bootable") },
	{ 0x0a, N_("OS/2 Boot Manager") },
	{ 0x0b, N_("FAT32") },
	{ 0x0c, N_("FAT32 LBA") },
	{ 0x0e, N_("FAT16 LBA") },
	{ 0x0f, N_("Extended LBA") },
	{ 0x10, N_("OPUS") },
	{ 0x11, N_("Hidden FAT12") },
	{ 0x12, N_("Compaq Diagnostics") },
	{ 0x14, N_("Hidden Small FAT16") },
	{ 0x16, N_("Hidden FAT16") },
	{ 0x17, N_("Hidden NTFS") },
	{ 0x18, N_("AST SmartSleep") },
	{ 0x1b, N_("Hidden FAT32") },
	{ 0x1c, N_("Hidden FAT32 LBA") },
	{ 0x1e, N_("Hidden FAT16 LBA") },
	{ 0x20, N_("Windows Mobile XIP") },
	{ 0x21, N_("SpeedStor") },
	{ 0x23, N_("Windows Mobile XIP") },
	{ 0x24, N_("NEC DOS") },
	{ 0x25, N_("Windows Mobile IMGFS") },
	{ 0x27, N_("Hidden NTFS WinRE") },
	{ 0x39, N_("Plan 9") },
	{ 0x3c, N_("PMagic Recovery") },
	{ 0x40, N_("Venix 80286") },
	{ 0x41, N_("PPC PReP Boot") },
	{ 0x42, N_("SFS") },
	{ 0x4d, N_("QNX4.x") },
	{ 0x4e, N_("QNX4.x") },
	{ 0x4f, N_("QNX4.x") },
	{ 0x50, N_("OnTrack DM") },
	{ 0x51, N_("OnTrack DM") },
	{ 0x52, N_("CP/M") },
	{ 0x53, N_("OnTrack DM") },
	{ 0x54, N_("OnTrack DM") },
	{ 0x55, N_("EZ Drive") },
	{ 0x56, N_("Golden Bow") },
	{ 0x5c, N_("Priam EDisk") },
	{ 0x61, N_("SpeedStor") },
	{ 0x63, N_("GNU HURD/SysV") },
	{ 0x64, N_("Netware") },
	{ 0x65, N_("Netware") },
	{ 0x66, N_("Netware") },
	{ 0x67, N_("Netware") },
	{ 0x68, N_("Netware") },
	{ 0x69, N_("Netware") },
	{ 0x70, N_("DiskSecure MultiBoot") },
	{ 0x75, N_("PC/IX") },
	{ 0x77, N_("Novell") },
	{ 0x78, N_("XOSL") },
	{ 0x7e, N_("F.I.X.") },
	{ 0x7e, N_("AODPS") },
	{ 0x80, N_("Minix") },
	{ 0x81, N_("Minix") },
	{ 0x82, N_("GNU/Linux Swap") },
	{ 0x83, N_("GNU/Linux") },
	{ 0x84, N_("Windows Hibernation") },
	{ 0x85, N_("GNU/Linux Extended") },
	{ 0x86, N_("NTFS Volume Set") },
	{ 0x87, N_("NTFS Volume Set") },
	{ 0x88, N_("GNU/Linux Plaintext") },
	{ 0x8d, N_("FreeDOS Hidden FAT12") },
	{ 0x8e, N_("GNU/Linux LVM") },
	{ 0x90, N_("FreeDOS Hidden FAT16") },
	{ 0x91, N_("FreeDOS Hidden Extended") },
	{ 0x92, N_("FreeDOS Hidden FAT16") },
	{ 0x93, N_("GNU/Linux Hidden") },
	{ 0x96, N_("CHRP ISO-9660") },
	{ 0x97, N_("FreeDOS Hidden FAT32") },
	{ 0x98, N_("FreeDOS Hidden FAT32") },
	{ 0x9a, N_("FreeDOS Hidden FAT16") },
	{ 0x9b, N_("FreeDOS Hidden Extended") },
	{ 0x9f, N_("BSD/OS") },
	{ 0xa0, N_("Hibernation") },
	{ 0xa1, N_("Hibernation") },
	{ 0xa2, N_("SpeedStor") },
	{ 0xa3, N_("SpeedStor") },
	{ 0xa4, N_("SpeedStor") },
	{ 0xa5, N_("FreeBSD") },
	{ 0xa6, N_("OpenBSD") },
	{ 0xa7, N_("NeXTSTEP") },
	{ 0xa8, N_("Darwin UFS") },
	{ 0xa9, N_("NetBSD") },
	{ 0xab, N_("Darwin Boot") },
	{ 0xaf, N_("HFS/HFS+") },
	{ 0xb0, N_("BootStar Dummy") },
	{ 0xb1, N_("QNX") },
	{ 0xb2, N_("QNX") },
	{ 0xb3, N_("QNX") },
	{ 0xb4, N_("SpeedStor") },
	{ 0xb6, N_("SpeedStor") },
	{ 0xb7, N_("BSDI") },
	{ 0xb8, N_("BSDI Swap") },
	{ 0xbb, N_("BootWizard Hidden") },
	{ 0xbc, N_("Acronis SZ") },
	{ 0xbe, N_("Solaris Boot") },
	{ 0xbf, N_("Solaris") },
	{ 0xc0, N_("Secured FAT") },
	{ 0xc1, N_("DR DOS FAT12") },
	{ 0xc2, N_("GNU/Linux Hidden") },
	{ 0xc3, N_("GNU/Linux Hidden Swap") },
	{ 0xc4, N_("DR DOS FAT16") },
	{ 0xc4, N_("DR DOS Extended") },
	{ 0xc6, N_("DR DOS FAT16") },
	{ 0xc7, N_("Syrinx") },
	{ 0xda, N_("Non-FS Data") },
	{ 0xdb, N_("CP/M") },
	{ 0xde, N_("Dell Utility") },
	{ 0xdf, N_("BootIt") },
	{ 0xe0, N_("ST AVFS") },
	{ 0xe1, N_("SpeedStor") },
	{ 0xe3, N_("SpeedStor") },
	{ 0xe4, N_("SpeedStor") },
	{ 0xe6, N_("SpeedStor") },
	{ 0xe8, N_("LUKS") },
	{ 0xea, N_("Rufus Extra") },
	{ 0xeb, N_("BeOS/Haiku") },
	{ 0xec, N_("SkyFS") },
	{ 0xed, N_("GPT Hybrid MBR") },
	{ 0xee, N_("GPT Protective MBR") },
	{ 0xef, N_("EFI FAT") },
	{ 0xf0, N_("PA-RISC Boot") },
	{ 0xf1, N_("SpeedStor") },
	{ 0xf2, N_("DOS secondary") },
	{ 0xf3, N_("SpeedStor") },
	{ 0xf4, N_("SpeedStor") },
	{ 0xf6, N_("SpeedStor") },
	{ 0xfa, N_("Bochs") },
	{ 0xfb, N_("VMware VMFS") },
	{ 0xfc, N_("VMware VMKCORE") },
	{ 0xfd, N_("GNU/Linux RAID Auto") },
	{ 0xfe, N_("LANstep") },
	{ 0xff, N_("XENIX BBT") },
	{ 0, NULL }
};

#endif
