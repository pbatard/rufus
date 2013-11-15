/*
 * Rufus: The Reliable USB Formatting Utility
 * SMART HDD vs Flash detection - isHDD() tables
 * Copyright © 2013 Pete Batard <pete@akeo.ie>
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

/*
 * (UNUSED) The list below contains the most common flash VIDs
 * according to (partial browsing of) http://flashboot.ru/iflash/saved/
 *
 * 0011 = Kingston
 * 03f0 = HP
 * 0420 = Chipsbank
 * 046d = Logitech
 * 048d = ITE
 * 04f3 = Elan
 * 058f = Alcor ALSO HDD
 * 05dc = Lexar
 * 05e3 = Genesys Logic
 * 0718 = Imation Corp.
 * 0781 = SanDisk
 * 090c = Silicon Motion
 * 0930 = Toshiba
 * 093a = Pixart
 * 0951 = Kingston
 * 09da = A4 Tech
 * 0bda = Realtek
 * 0b27 = Ritek
 * 0c76 = JMTek
 * 0cf2 = ENE
 * 1000 = Speed Tech
 * 1002 = Hisun
 * 1005 = Apacer Technology
 * 1043 = iCreate
 * 1221 = Kingston?
 * 12d1 = Huawei
 * 125f = Adata
 * 1307 = USBest
 * 13fe = Kingston
 * 14cd = Super Top
 * 1516 = CompUSA
 * 1687 = Kingmax
 * 18a5 = Verbatim ALSO HDD
 * 18ec = Arkmicro
 * 1908 = Ax216
 * 1b1c = Corsair
 * 1e3d = Chipsbank
 * 1f75 = Innostor ALSO HDD
 * 2001 = Micov
 * 201e = Evdo
 * 2188 = SMI
 * 3538 = PQI
 * 413c = Ameco
 * 5136 = Skymedi
 * 8564 = Transcend
 * 8644 = NandTec
 * eeee = ???
 */

/* String identifiers:
 * Some info comes from http://knowledge.seagate.com/articles/en_US/FAQ/204763en,
 * other http://svn.code.sf.net/p/smartmontools/code/trunk/smartmontools/drivedb.h
 * '#' means any number in [0-9]
 */
static str_score_t str_score[] = {
	{ "HP ", 10 },
	{ "IC#", 10 },
	{ "ST#", 10 },
	{ "MX#", 10 },
	{ "WDC", 10 },
	{ "IBM", 10 },
	{ "OCZ", 5 },
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
	{ "SAMSUNG", 10 },
	{ "FUJITSU", 10 },
	{ "TOSHIBA", 10 },
	{ "QUANTUM", 10 },
	{ "EXCELSTOR", 10 },
};


/* The lists belows set a score according to VID & VID:PID
 * These were constructed as follows:
 * 1. Pick all the VID:PIDs from http://svn.code.sf.net/p/smartmontools/code/trunk/smartmontools/drivedb.h
 * 2. Check that VID against http://flashboot.ru/iflash/saved/ as well as http://www.linux-usb.org/usb.ids
 * 3. If a lot of flash or card reader devices are returned, add the VID:PID, with a positive score,
 *    in the vidpid table (so that the default will be UFD, and HDD the exception)
 * 4. If only a few flash devices are returned, add the VID to our list with a positive score and
 *    add the flash entries in the VID:PID list with a negative score
 * These lists MUST be kept in increasing VID/VID:PID order
 */
static vid_score_t vid_score[] = {
	{ 0x0411, 5 },		// Buffalo
	{ 0x0480, 5 },		// Toshiba
	{ 0x04b4, 10 },		// Cypress
	{ 0x04e8, 5 },		// Samsung
	{ 0x04c5, 7 },		// Fujitsu
	{ 0x04fc, 5 },		// Sunplus
	{ 0x059b, 7 },		// Iomega
	{ 0x059f, 5 },		// LaCie
	{ 0x05ab, 10 },		// In-System Design
	{ 0x067b, 7 },		// Prolific
	{ 0x07ab, 8 },		// Freecom
	{ 0x0928, 10 },		// PLX Technology
	{ 0x0930, 5 },		// Toshiba
	{ 0x0bc2, 10 },		// Seagate
	{ 0x0d49, 10 },		// Maxtor
	{ 0x0dc4, 10 },		// Macpower Peripherals
	{ 0x1058, 10 },		// Western Digital
	{ 0x13fd, 10 },		// Initio
	{ 0x152d, 10 },		// JMicron
	{ 0x174c, 8 },		// ASMedia
	{ 0x1759, 8 },		// LucidPort
	{ 0x1a4a, 10 },		// Silicon Image
	{ 0x4971, 10 },		// Hitachi
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
	{ 0x04fc, 0x05d8, -20 },	// Verbatim flash drive
	{ 0x04fc, 0x5720, -20 },	// Card reader
	// LaCie exceptions
	{ 0x059f, 0x1027, -20 },	// 16 GB UFD
	{ 0x059f, 0x103B, -20 },	// 16 GB UFD
	{ 0x059f, 0x1064, -20 },	// 16 GB UFD
	// Prolific exceptions
	{ 0x067b, 0x2517, -20 },	// 1 GB UFD
	{ 0x067b, 0x2528, -20 },	// 8 GB UFD
	{ 0x067b, 0x3400, -10 },	// Hi-Speed Flash Disk with TruePrint AES3400
	{ 0x067b, 0x3500, -10 },	// Hi-Speed Flash Disk with TruePrint AES3500
	// Freecom exceptions
	{ 0x07ab, 0xfcab, -20 },	// 4 GB UFD
	// Toshiba exceptions
	{ 0x0930, 0x1400, -20 },
	{ 0x0930, 0x6533, -20 },
	{ 0x0930, 0x653e, -20 },
	{ 0x0930, 0x6544, -20 },
	{ 0x0930, 0x6545, -20 },
	// Verbatim exceptions
	{ 0x18a5, 0x0243, -20 },
	{ 0x18a5, 0x0245, -20 },
	{ 0x18a5, 0x0302, -20 },
	{ 0x18a5, 0x0304, -20 },
	{ 0x18a5, 0x3327, -20 },
};
