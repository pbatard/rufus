/********************************************************************************/
/* MSG - A protective MBR that displays an ASCII message located in the         */
/*       subsequent sectors.                                                     */
/*                                                                              */
/* Copyright (c) 2019-2022 Pete Batard <pete@akeo.ie>                           */
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
MBR_RESERVED  = 0x1b8	# Start of the reserved section (partition table, etc.)
MSG_SECTOR    = 0x22	# First sector of the message (must be after the GPT)
NB_SECTORS    = 0x08	# Number of sectors to read
PT_MAX        = 0x04	# Number of partition entries in the partition table
PT_ENTRY_SIZE = 0x10	# Size of a partition entry in the partition table

/********************************************************************************/
/* Reference list of colours:                                                   */
/* You can escape these numeric values in your message text to set the colours  *
/* for the background and foreground. For instance "\17HIGHLIGHT" will display  */
/* light grey text (0x07) over a blue (0x01) background                         */
/********************************************************************************/
BLACK         = 0x00
BLUE          = 0x01
GREEN         = 0x02
CYAN          = 0x03
RED           = 0x04
MAGENTA       = 0x05
BROWN         = 0x06
LIGHT_GREY    = 0x07
DARK_GREY     = 0x08
LIGHT_BLUE    = 0x09
LIGHT_GREEN   = 0x0a
LIGHT_CYAN    = 0x0b
LIGHT_RED     = 0x0c
LIGHT_MAGENTA = 0x0d
LIGHT_YELLOW  = 0x0e
WHITE         = 0x0f

/********************************************************************************/
/* MBR: This section resides at 0x00007c00 and is exactly 512 bytes             */
/********************************************************************************/
.section main, "ax"
.globl mbr
mbr:
//	xchg bx, bx		# Uncomment to trigger Bochs magic breakpoint
	inc  cx
	dec  bx
	inc  bp
	dec  di
	cld
	xor  ax, ax
	cli			# Disable interrupts when fiddling with the stack
	mov  ss, ax		# First order of the day it to set up the stack
	mov  sp, MBR_ADDR	# This places the stack right before the MBR copy in RAM
	sti
	mov  ds, ax		# MBR and stack reside in segment 0
	mov  bx, 0x0413		# McAfee thinks we are a virus if we use 413 directly...
	mov  ax,[bx]
	sub  ax, NB_SECTORS / 2 # Amount of RAM we need to read the message sectors
	mov  [bx],ax
	shl  ax, 6		# Convert to segment address
	mov  es, ax		# Keep allocated RAM segment in ES

clear_display:
	mov  ax, 0x0003
	int  0x10		# Set Text Mode to 80x25, 16 color (setting mode also clears the screen)
	mov  ah, 0x02
	int  0x10		# Set cursor pos to top left

read_sectors:			# Copy the next sectors into RAM
	mov  ah, 0x41
	mov  bx, 0x55aa
	xor  cx, cx
	xor  dx, dx
	int  0x13
	jb   no_ext		# failure to get ext
	cmp  bx, 0xaa55
	jnz  no_ext
	test cx, 1		# is packet access supported?
	jz   no_ext

ext:	# http://en.wikipedia.org/wiki/INT_13H#INT_13h_AH.3D42h:_Extended_Read_Sectors_From_Drive
	xor  eax, eax
	push eax		# bits 32-63 of sector address
	push MSG_SECTOR # bits 0-31 of sector address
	push es			# destination segment
	push eax		# destination address (0)
	push NB_SECTORS		# number of sectors to be read
	push 0x0010		# size of DAP struct
	mov  si, sp		# DAP address (= stack)
	mov  ah, 0x42		# Extended Read Sectors From Drive
	int  0x13
	lahf
	add  sp,0x10
	sahf
	jmp check_error

no_ext:	# http://en.wikipedia.org/wiki/INT_13H#INT_13h_AH.3D02h:_Read_Sectors_From_Drive
	mov  ax, 0x0200 + NB_SECTORS
	mov  cx, (MSG_SECTOR + 1)	# Sector address (starts at 1)
	mov  dx, 0x0080		# Drive ID
	xor  bx, bx		# Destination address in ES
	int  0x13

check_error:
	mov  bx, 0x0007		# Default text to light grey on black
	jc   display_err

display_msg:			# Display the message
	xor  si, si
	mov  ax, es
	mov  ds, ax
	call print_msg
	jmp  prompt_user

display_err:			# Read error -> display shorter message from this MBR
	mov  si, offset err_msg
	call print_msg

prompt_user:
	xor  ax, ax
	mov  ds, ax
	mov  si, offset prompt_string
	call print_msg		# Prompt the user
	call flush_keyboard

wait_for_keyboard:
	mov  ah, 0x01
	int  0x16		# KEYBOARD - CHECK BUFFER, DO NOT CLEAR
	jnz  reboot		# Z is clear when characters are present in the buffer
	mov  ah, 0x02
	int  0x16		# KEYBOARD - GET SHIFT STATUS
	and  al, 0x04		# AL = shift status bits
	jz   wait_for_keyboard

reboot:				# Trigger a reboot
	xor  ax, ax
	mov  ds, ax
	mov  ax, 0x1234
	mov  [0x473], ax
	jmp  0xffff:0000

/********************************************************************************/
/* Subroutines                                                                  */
/********************************************************************************/

flush_keyboard:			# Flush the keyboard buffer
	mov  ah, 0x01
	int  0x16		# KEYBOARD - CHECK BUFFER, DO NOT CLEAR
	jz   0f			# Z is set if no character in buffer
	mov  ah, 0x00
	int  0x16		# KEYBOARD - READ CHAR FROM BUFFER
	loop flush_keyboard
0:	ret

# ---------------------------------------------------------------------------

print_msg:			# Print NUL terminated string in DS:SI to console
	lodsb
	cmp  al, 0x00		# NUL?
	jz   4f
	cmp  al, 0x0d		# Ignore CR
	jz   print_msg
	cmp  al, 0x0a		# Handle LF
	jnz  0f
	push bx
	mov  ah, 0x03
	int  0x10		# Get Cursor pos
	inc  dh			# Move to next line
	mov  dl, 0		# Set column to 0
	mov  ah, 0x02
	int  0x10		# Set Cursor pos
	pop  bx
	jmp  print_msg
0:	cmp  al, 0x5c		# '\' Escape sequence to set FG/BG color
	jnz  3f
	mov  cl, 2
1:	lodsb
	cmp  al, 0x46
	jg   print_msg		# Invalid escape sequence
	sub  al, 0x30		# '0'
	cmp  al, 0x09
	jle  2f
	sub  al, 0x07
2:	shl  bl, 4
	and  al, 0x0f
	or   bl, al
	dec  cl
	jnz  1b
	jmp  print_msg
3:	mov  cx, 0x0001
	mov  ah, 0x09
	int  0x10
	push bx
	xor  bx, bx
	mov  ah, 0x03
	int  0x10
	inc  dl
	mov  ah, 0x02
	int  0x10
	pop  bx
	jmp  print_msg
4:	ret

/********************************************************************************/
/* Data section                                                                 */
/********************************************************************************/
err_msg:
		.string "\r\n             \\04*** ERROR: THIS MEDIA CANNOT BOOT IN LEGACY MODE ***\\07\r\n"
prompt_string:
		.string "\r\n\r\n             \\70Please remove this media and press any key to reboot\\07"

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
