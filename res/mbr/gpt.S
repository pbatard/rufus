/********************************************************************************/
/* GPT - A protective MBR that displays a notice if a user attempts to boot a   */
/*       GPT drive in BIOS/Legacy mode.                                         */
/*                                                                              */
/* Copyright (c) 2019 Pete Batard <pete@akeo.ie>                                */
/*                                                                              */
/* This program is free software; you can redistribute it and/or modify it      */
/* under the terms of the GNU General Public License as published by the Free   */
/* Software Foundation, either version 3 of the License, or (at your option)    */
/* any later version.                                                           */
/*                                                                              */
/* This program is distributed in the hope that it will be useful, but WITHOUT  */
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or        */
/* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for    */
/* more details.                                                                */
/*                                                                              */
/* You should have received a copy of the GNU General Public License along with */
/* this program; if not, see <http://www.gnu.org/licenses/>.                    */
/*                                                                              */
/********************************************************************************/

/********************************************************************************/
/* GNU Assembler Settings:                                                      */
/********************************************************************************/
.intel_syntax noprefix
.code16
/********************************************************************************/

/********************************************************************************/
/* Constants:                                                                   */
/********************************************************************************/
MBR_ADDR      = 0x7c00
MBR_SIZE      = 0x200
MBR_RESERVED  = 0x1b8	# Start of the reserved section (partition table, etc.)
PT_MAX        = 0x04	# Number of partition entries in the partition table
PT_ENTRY_SIZE = 0x10	# Size of a partition entry in the partition table

/********************************************************************************/
/* MBR: This section resides at 0x00007c00 and is exactly 512 bytes             */
/********************************************************************************/
.section main, "ax"
.globl mbr
mbr:
	inc  cx
	dec  bx
	inc  bp
	dec  di
	cld
	xor  ax, ax
	mov  ds, ax
	mov  si, offset sep
	call print_string
	mov  si, offset hdr
	call print_string
	mov  si, offset sep
	call print_string
	mov  si, offset txt
	call print_string
	hlt

print_string:
	lodsb
	cmp  al, 0x00
	jz   0f
	mov  ah, 0x0e
	mov  bx, 0x0007
	int  0x10
	jmp  print_string
0:	ret

/********************************************************************************/
/* Data section                                                                 */
/********************************************************************************/
sep:
		.string "****************************************\r\n"
hdr:
		.string "*** ERROR: LEGACY BOOT OF UEFI MEDIA ***\r\n"
txt:
		.ascii "\r\n" \
		"This drive can only boot in UEFI mode.\r\n" \
		"It can not boot in BIOS/Legacy mode.\r\n" \
		"\r\n" \
		"If you want to boot this drive in BIOS/Legacy mode, you\r\n" \
		"should recreate it in Rufus using the following settings:\r\n" \
		"* Partition scheme -> MBR\r\n" \
		"* Target system -> BIOS...\r\n" \
		"\0"

/********************************************************************************/
/* From offset 0x1b8, the MBR contains the partition table and signature data   */
/********************************************************************************/
		.org MBR_RESERVED
disk_signature:
		.space 0x04
filler:
		.space 0x02
partition_table:
		.space PT_ENTRY_SIZE * PT_MAX
mbr_signature:
		.word 0xAA55
