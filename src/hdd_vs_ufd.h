/*
 * Rufus: The Reliable USB Formatting Utility
 * SMART HDD vs Flash detection - isHDD() tables
 * Copyright © 2013-2014 Pete Batard <pete@akeo.ie>
 *
 * Based in part on drivedb.h from Smartmontools:
 * http://svn.code.sf.net/p/smartmontools/code/trunk/smartmontools/drivedb.h
 * Copyright © 2003-11 Philip Williams, Bruce Allen
 * Copyright © 2008-13 Christian Franke <smartmontools-support@lists.sourceforge.net>
 *
 * Also based on entries listed in the identification flash database
 * (http://flashboot.ru/iflash/saved/) as well as the Linux USB IDs
 * (http://www.linux-usb.org/usb.ids)
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
#pragma once

#include <stdint.h>

/*
 * A positive score means HDD, a negative one an UFD
 * The higher the absolute value, the greater the probability
 */
typedef struct {
	const char* name;
	const int score;
} str_score_t;

typedef struct {
	const uint16_t vid;
	const int score;
} vid_score_t;

typedef struct {
	const uint16_t vid;
	const uint16_t pid;
	const int score;
} vidpid_score_t;

/* String identifiers:
 * Some info comes from http://knowledge.seagate.com/articles/en_US/FAQ/204763en,
 * other http://svn.code.sf.net/p/smartmontools/code/trunk/smartmontools/drivedb.h
 * '#' means any number in [0-9]
 */
static str_score_t str_score[] = {
	{ "IC#", 10 },
	{ "ST#", 10 },
	{ "MX#", 10 },
	{ "WDC", 10 },
	{ "IBM", 10 },
	{ "STM#", 10 },
	{ "HDS#", 10 },		// These Hitachi drives are a PITA
	{ "HDP#", 10 },
	{ "HDT#", 10 },
	{ "HTE#", 10 },
	{ "HTS#", 10 },
	{ "HUA#", 10 },
	{ "APPLE", 10 },
	{ "INTEL", 10 },
	{ "MAXTOR", 10 },
	{ "HITACHI", 10 },
	{ "SEAGATE", 10 },
	{ "SAMSUNG", 5 },
	{ "FUJITSU", 10 },
	{ "TOSHIBA", 5 },
	{ "QUANTUM", 10 },
	{ "EXCELSTOR", 10 },
	{ "CORSAIR", -15 },
	{ "KINGMAX", -15 },
	{ "KINGSTON", -15 },
	{ "LEXAR", -15 },
	{ "MUSHKIN", -15 },
	{ "PNY", -15 },
	{ "SANDISK", -15 },
	{ "TRANSCEND", -15 },
};

static str_score_t str_adjust[] = {
	{ "Gadget", -10 },
	{ "Flash", -10 }
};

/* The lists belows set a score according to VID & VID:PID
 * These were constructed as follows:
 * 1. Pick all the VID:PIDs from http://svn.code.sf.net/p/smartmontools/code/trunk/smartmontools/drivedb.h
 * 2. Check that VID against http://flashboot.ru/iflash/saved/ as well as http://www.linux-usb.org/usb.ids
 * 3. If a lot of flash or card reader devices are returned, add the VID:PID, with a positive score,
 *    in the vidpid table (so that the default will be UFD, and HDD the exception)
 * 4. If only a few flash devices are returned, add the VID to our list with a positive score and
 *    add the flash entries in the VID:PID list with a negative score
 * 5. Add common UFD providers from http://flashboot.ru/iflash/saved/ with a negative score
 * These lists MUST be kept in increasing VID/VID:PID order
 */
static vid_score_t vid_score[] = {
	{ 0x0011, -5 },		// Kingston
	{ 0x03f0, -5 },		// HP
	{ 0x0409, -10 },	// NEC/Toshiba
	{ 0x0411, 5 },		// Buffalo
	{ 0x0420, -5 },		// Chipsbank
	{ 0x046d, -5 },		// Logitech
	{ 0x0480, 5 },		// Toshiba
	{ 0x048d, -5 },		// ITE
	{ 0x04b4, 10 },		// Cypress
	{ 0x04c5, 7 },		// Fujitsu
	{ 0x04e8, 5 },		// Samsung
	{ 0x04f3, -5 },		// Elan
	{ 0x04fc, 5 },		// Sunplus
	{ 0x056e, -5 },		// Elecom
	{ 0x058f, -5 },		// Alcor
	{ 0x059b, 7 },		// Iomega
	{ 0x059f, 5 },		// LaCie
	{ 0x05ab, 10 },		// In-System Design
	{ 0x05dc, -5 },		// Lexar
	{ 0x05e3, -5 },		// Genesys Logic
	{ 0x067b, 7 },		// Prolific
	{ 0x0718, -2 },		// Imation
	{ 0x0781, -5 },		// SanDisk
	{ 0x07ab, 8 },		// Freecom
	{ 0x090c, -5 },		// Silicon Motion (also used by Samsung)
	{ 0x0928, 10 },		// PLX Technology
	{ 0x0930, -8 },		// Toshiba
	{ 0x093a, -5 },		// Pixart
	{ 0x0951, -5 },		// Kingston
	{ 0x09da, -5 },		// A4 Tech
	{ 0x0b27, -5 },		// Ritek
	{ 0x0bc2, 10 },		// Seagate
	{ 0x0c76, -5 },		// JMTek
	{ 0x0cf2, -5 },		// ENE
	{ 0x0d49, 10 },		// Maxtor
	{ 0x0dc4, 10 },		// Macpower Peripherals
	{ 0x1000, -5 },		// Speed Tech
	{ 0x1002, -5 },		// Hisun
	{ 0x1005, -5 },		// Apacer
	{ 0x1043, -5 },		// iCreate
	{ 0x1058, 10 },		// Western Digital
	{ 0x1221, -5 },		// Kingston (?)
	{ 0x12d1, -5 },		// Huawei
	{ 0x125f, -5 },		// Adata
	{ 0x1307, -5 },		// USBest
	{ 0x13fd, 10 },		// Initio
	{ 0x13fe, -5 },		// Kingston
	{ 0x14cd, -5 },		// Super Top
	{ 0x1516, -5 },		// CompUSA
	{ 0x152d, 10 },		// JMicron
	{ 0x1687, -5 },		// Kingmax
	{ 0x174c, 3 },		// ASMedia (also used by SanDisk)
	{ 0x1759, 8 },		// LucidPort
	{ 0x18a5, -2 },		// Verbatim
	{ 0x18ec, -5 },		// Arkmicro
	{ 0x1908, -5 },		// Ax216
	{ 0x1a4a, 10 },		// Silicon Image
	{ 0x1b1c, -5 },		// Corsair
	{ 0x1e3d, -5 },		// Chipsbank
	{ 0x1f75, -2 },		// Innostor
	{ 0x2001, -5 },		// Micov
	{ 0x201e, -5 },		// Evdo
	{ 0x2188, -5 },		// SMI
	{ 0x3538, -5 },		// PQI
	{ 0x413c, -5 },		// Ameco
	{ 0x4971, 10 },		// Hitachi
	{ 0x5136, -5 },		// Skymedi
	{ 0x8564, -5 },		// Transcend
	{ 0x8644, -5 },		// NandTec
	{ 0xeeee, -5 },		// ????
};

static vidpid_score_t vidpid_score[] = {
	{ 0x03f0, 0xbd07, 10 },		// HP Desktop HD BD07
	{ 0x0402, 0x5621, 10 },		// ALi M5621
	// NOT in VID list as 040d:6205 is a card reader
	{ 0x040d, 0x6204, 10 },		// Connectland BE-USB2-35BP-LCM
	// NOT in VID list as 043e:70e2 & 043e:70d3 are flash drives
	{ 0x043e, 0x70f1, 10 },		// LG Mini HXD5
	// NOT in VID list as 0471:0855 is a flash drive
	{ 0x0471, 0x2021, 10 },		// Philips
	// NOT in VID list as many UFDs and card readers exist
	{ 0x05e3, 0x0718, 10 },		// Genesys Logic IDE/SATA Adapter
	{ 0x05e3, 0x0719, 10 },		// Genesys Logic SATA adapter
	{ 0x05e3, 0x0731, 10 },		// Genesys Logic GL3310 SATA 3Gb/s Bridge Controller
	{ 0x05e3, 0x0731, 2 },		// Genesys Logic Mass Storage Device
	// Only one HDD device => keep in this list
	{ 0x0634, 0x0655, 5 },		// Micron USB SSD
	// NOT in VID list as plenty of UFDs
	{ 0x0718, 0x1000, 7 },		// Imation Odyssey external USB dock
	// Only one HDD device
	{ 0x0939, 0x0b16, 10 },		// Toshiba Stor.E
	// Plenty of card readers
	{ 0x0c0b, 0xb001, 10 },		// Dura Micro
	{ 0x0c0b, 0xb159, 10 },		// Dura Micro 509
	// Meh
	{ 0x0e21, 0x0510, 5 },		// Cowon iAudio X5
	{ 0x11b0, 0x6298, 10 },		// Enclosure from Kingston SSDNow notebook upgrade kit
	// NOT in VID list as plenty of UFDs
	{ 0x125f, 0xa93a, 10 },		// A-DATA SH93
	{ 0x125f, 0xa94a, 10 },		// A-DATA DashDrive
	// NOT in VID list as plenty of card readers
	{ 0x14cd, 0x6116, 10 },		// Super Top generic enclosure
	// Verbatim are way too widespread - good candidate for ATA passthrough
	{ 0x18a5, 0x0214, 10 },		// Verbatim Portable Hard Drive
	{ 0x18a5, 0x0215, 10 },		// Verbatim FW/USB160
	{ 0x18a5, 0x0216, 10 },		// Verbatim External Hard Drive 47519
	{ 0x18a5, 0x0227, 10 },		// Verbatim Pocket Hard Drive
	{ 0x18a5, 0x022a, 10 },		// Verbatim External Hard Drive
	{ 0x18a5, 0x022b, 10 },		// Verbatim Portable Hard Drive (Store'n'Go)
	{ 0x18a5, 0x0237, 10 },		// Verbatim Portable Hard Drive (500 GB)
	// SunPlus seem to have a bunch of UFDs
	{ 0x1bcf, 0x0c31, 10 },		// SunplusIT
	// Plenty of Innostor UFDs
	{ 0x1f75, 0x0888, 10 },		// Innostor IS888
	// NOT in VID list as plenty of UFDs
	{ 0x3538, 0x0902, 10 },		// PQI H560
	// Too many card readers to be in VID list
	{ 0x55aa, 0x0015, 10 },		// OnSpec Hard Drive
	{ 0x55aa, 0x0102, 8 },		// OnSpec SuperDisk
	{ 0x55aa, 0x0103, 10 },		// OnSpec IDE Hard Drive
	{ 0x55aa, 0x1234, 8 },		// OnSpec ATAPI Bridge
	{ 0x55aa, 0x2b00, 8 },		// OnSpec USB->PATA
	// Smartmontools are uncertain about that one, and so am I
	{ 0x6795, 0x2756, 2 },		// Sharkoon 2-Bay RAID Box

	// OCZ exceptions
	{ 0x0324, 0xbc06, -20 },	// OCZ ATV USB 2.0 Flash Drive
	{ 0x0324, 0xbc08, -20 },	// OCZ Rally2 / ATV USB 2.0 Flash Drive
	{ 0x0325, 0xac02, -20 },	// OCZ ATV Turbo / Rally2 Dual Channel USB 2.0 Flash Drive
	// Buffalo exceptions
	{ 0x0411, 0x01e8, -20 },	// Buffalo HD-PNTU2
	// Samsung exceptions
	{ 0x04e8, 0x0100, -20 },	// Kingston Flash Drive (128MB)
	{ 0x04e8, 0x0100, -20 },	// Connect3D Flash Drive
	{ 0x04e8, 0x0101, -20 },	// Connect3D Flash Drive
	{ 0x04e8, 0x1a23, -20 },	// 2 GB UFD
	{ 0x04e8, 0x5120, -20 },	// 4 GB UFD
	{ 0x04e8, 0x6818, -20 },	// 8 GB UFD
	{ 0x04e8, 0x6845, -20 },	// 16 GB UFD
	{ 0x04e8, 0x685E, -20 },	// 16 GB UFD
	// Sunplus exceptions
	{ 0x04fc, 0x05d8, -20 },	// Verbatim Flash Drive
	{ 0x04fc, 0x5720, -20 },	// Card Reader
	// LaCie exceptions
	{ 0x059f, 0x1027, -20 },	// 16 GB UFD
	{ 0x059f, 0x103B, -20 },	// 16 GB UFD
	{ 0x059f, 0x1064, -20 },	// 16 GB UFD
	// Prolific exceptions
	{ 0x067b, 0x2506, -20 },	// 8 GB Micro Hard Drive
	{ 0x067b, 0x2517, -20 },	// 1 GB UFD
	{ 0x067b, 0x2528, -20 },	// 8 GB UFD
	{ 0x067b, 0x2731, -20 },	// SD/TF Card Reader
	{ 0x067b, 0x3400, -10 },	// Hi-Speed Flash Disk with TruePrint AES3400
	{ 0x067b, 0x3500, -10 },	// Hi-Speed Flash Disk with TruePrint AES3500
	// Freecom exceptions
	{ 0x07ab, 0xfcab, -20 },	// 4 GB UFD
	// Samsung exceptions
	{ 0x090c, 0x1000, -20 },	// Samsung Flash Drive
	// Toshiba exceptions
	{ 0x0930, 0x1400, -20 },
	{ 0x0930, 0x6533, -20 },
	{ 0x0930, 0x653e, -20 },
	{ 0x0930, 0x6544, -20 },
	{ 0x0930, 0x6545, -20 },
	// Innostor exceptions
	{ 0x0BC2, 0x03312, -20 },
	// Verbatim exceptions
	{ 0x18a5, 0x0243, -20 },
	{ 0x18a5, 0x0245, -20 },
	{ 0x18a5, 0x0302, -20 },
	{ 0x18a5, 0x0304, -20 },
	{ 0x18a5, 0x3327, -20 },
	// More Innostor
	{ 0x1f75, 0x0917, -10 },	// Intenso Speed Line USB Device
};
