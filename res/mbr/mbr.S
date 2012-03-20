/********************************************************************************/
/* Rufus - The Reliable USB Formatting Utility, bootable USB MBR                */
/*                                                                              */
/* Copyright (c) 2011 Pete Batard <pete@akeo.ie>                                */
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
.intel_syntax noprefix	# Use Intel assembler syntax (same as IDA Pro)
.code16			# MBR code is executed in x86 CPU real/16 bit mode
/********************************************************************************/

/********************************************************************************/
/* Constants:                                                                   */
/********************************************************************************/
DOT_TIMEOUT   = 0x12	# Number of RTC interrupts for ~ 1s
DOT_NUMBER    = 0x04	# Number of dots to be printed before timeout
MBR_ADDR      = 0x7c00	# Same address for both original MBR and copy
MBR_SIZE      = 0x200
MBR_RESERVED  = 0x1b8	# Start of the reserved section (partition table, etc.)
BUF_SEGMENT   = 0x3000  # Segment for the buffer
PT_MAX        = 0x04	# Number of partition entries in the partition table
PT_ENTRY_SIZE = 0x10	# Size of a partition entry in the partition table
INT_RTC       = 0x08
INT_DSK       = 0x13

/********************************************************************************/
/* mbr: this section must reside at 0x00007c00, and be exactly 512 bytes        */
/********************************************************************************/
.section main, "ax"
.globl mbr			# label must be declared global for the linker
mbr:
# We must be able to reuse the original memory location that the BIOS copied
# the MBR to (0x00007c000), so our first task is to copy our MBR data to another
# location, and run our code from there
	inc  cx			# Register fixup
	dec  bx
	inc  bp
	dec  di
	cld
	xor  ax, ax
	cli			# Disable interrupts when fiddling with the stack
	mov  ss, ax		# First order of the day it to set up the stack
	mov  sp, MBR_ADDR	# This places the stack right before the original MBR
	sti
	mov  si, sp		# While we have that value handy
	mov  di, sp
	push ds			# Now that we have a stack...
	push es
	mov  ds, ax		# Original MBR is in segment 0
	mov  bx, 0x0413		# McAfee thinks we are a virus if we use 413 directly...
	mov  ax,[bx+0]
	dec  ax			# Remove 1KB from the RAM for our copy
	mov  [bx+0],ax
	shl  ax, 6		# Convert to segment address
	sub  ax, MBR_ADDR>>4	# For convenience of disassembly, so that irrespective
	mov  es, ax		# of the segment, our base address remains MBR_ADDR
	mov  cx, MBR_SIZE	# Move our code out of the way
	rep  movsb
	push ax			# Push the CS:IP for our retf
	push offset 0f
	retf

# ---------------------------------------------------------------------------

# From this point forward, we are running the copy at a different segment from seg 0
0:	mov  ds, ax		# AX = ES = CS, only DS points back to old seg => fix this
	push es
	xor  ax, ax
	mov  es, ax
	mov  bx, MBR_ADDR	# TODO optimize
	mov  ax, 0x0201
	mov  cx, 0x0001
	mov  dx, 0x0081		# Check next bootable disk
	int  0x13		# DISK - READ SECTORS INTO MEMORY
				# AL = number of sectors to read, CH = track, CL = sector
				# DH = head, DL = drive, ES:BX -> buffer to fill
				# Return: CF set on error, AH = status, AL = number of sectors read
	jnb  read_success

read_failure:			# If we couldn't get data for second bootable, just boot USB
	pop  es
	jmp  boot_usb_no_rtc

read_success:
	mov  cx, PT_MAX
	mov  bp, offset partition_table

check_table:			# check the partition table for an active (bootable) partition
	cmpb es:[bp+0], 0x00
	jl   active_partition	# 0x80 or greater means the partition is active
	jnz  invalid_table	# anything between 0x01 and 0x7f is invalid
	add  bp, PT_ENTRY_SIZE	# next partition
	loop check_table

no_active:
	pop  es
	jmp  exit

active_partition:
	push bp

check_one_active:		# check that all subsequent partitions are zero (only one active)
	add  bp, PT_ENTRY_SIZE
	dec  cx
	jz   valid_active
	cmpb es:[bp+0], 0x00
	jz   check_one_active
	pop  bp

invalid_table:
	pop  es
	jmp  exit

valid_active:
	pop  bp
	pop  es
	mov  si, offset prompt_string
	call print_string
	call flush_keyboard

set_rtc_int_vect:		# Set the interrupt vector for CMOS real-time clock
	mov  dx, offset rtc_interrupt
	mov  si, offset rtc_interrupt_org
	call set_int_vect

wait_for_keyboard:
	mov  ah, 0x01
	int  0x16		# KEYBOARD - CHECK BUFFER, DO NOT CLEAR
	jnz  boot_usb		# Z is clear when characters are present in the buffer
	mov  ah, 0x02
	int  0x16		# KEYBOARD - GET SHIFT STATUS
	and  al, 0x04		# AL = shift status bits
	jnz  boot_usb
	cmpw ds:counter_dot, 0x0000
	jg   short check_timeout

print_dot:
	mov  si, offset dot_string
	call print_string
	movw ds:counter_dot, DOT_TIMEOUT

check_timeout:
	cmpw ds:counter_timeout, 0x0000
	jnz  wait_for_keyboard

boot_fixed_disk:		# Timeout occured => boot second bootable disk (non USB)
	call restore_rtc_vect	# Remove our RTC override
	mov  dx, offset dsk_interrupt		# Set interrupt override to have
	mov  si, offset dsk_interrupt_org	# disk 0x81 is seen as 0x80
	call set_int_vect

boot_drive:
	pop  es
	pop  ds
	mov  dl, 0x0080		# In both case, we pretend the disk is the first bootable
	jmp  0:MBR_ADDR

# ---------------------------------------------------------------------------

boot_usb:			# Boot USB drive (0x80)
	call restore_rtc_vect	# Remove our RTC override
	call flush_keyboard	# Make sure the keyboard buffer is clear
boot_usb_no_rtc:
	mov  bx, offset partition_table
	mov  dx, es:[bx]
	mov  cx, es:[bx+2]
	xor  ax, ax
	mov  es, ax		# make sure ES is set to seg 0 for copy
	mov  ax, 0x0201
	mov  bx, MBR_ADDR
	int  0x13
	jnb  boot_drive
exit:
	pop  es
	pop  ds
	retf


/********************************************************************************/
/* Subroutines                                                                  */
/********************************************************************************/

set_int_vect:			# Set the interrupt vector
	cli			# SI = pointer to backup vector (must contain the interrupt #)
	push es			# DX = pointer to interrupt
	xor  ax, ax
	mov  es, ax
	mov  bx, ds:[si]
	mov  eax, es:[bx]	# Backup the original vector
	mov  ds:[si], eax
	mov  es:[bx], dx
	mov  es:[bx+2], cs
	pop  es
	sti
	ret

# ---------------------------------------------------------------------------

restore_rtc_vect:		# Restore the interrupt vector for RTC
	cli
	push es
	xor  ax, ax
	mov  es, ax
	mov  bx, INT_RTC*4
	mov  eax, ds:rtc_interrupt_org
	mov  es:[bx], eax
	pop  es
	sti
	ret

# ---------------------------------------------------------------------------

flush_keyboard:			# Flush the keyboard buffer
	mov  ah, 0x01
	int  0x16		# KEYBOARD - CHECK BUFFER, DO NOT CLEAR
	jz   0f			# Z is set if no character in buffer
	mov  ah, 0x00
	int  0x16		# KEYBOARD - READ CHAR FROM BUFFER
	loop flush_keyboard
0:	ret

# ---------------------------------------------------------------------------

print_string:			# Print a NUL terminated string to console
	push ax
	push bx

ps_putchar:
	lodsb
	cmp  al, 0x00
	jz   ps_exit
	mov  ah, 0x0e
	mov  bx, 0x0007		# BH = display page, BL = foreground color
	int  0x10		# VIDEO - WRITE CHARACTER AND ADVANCE CURSOR
	jmp  ps_putchar

ps_exit:
	pop  bx
	pop  ax
	ret

# ---------------------------------------------------------------------------

disk_swap:			# Swap disks 0x80 and 0x81
	push dx
	and  dl, 0xfe
	cmp  dl, 0x80
	pop  dx
	jne  0f
	xor  dl, 0x01
0:	ret


/********************************************************************************/
/* Interrupt overrides                                                          */
/********************************************************************************/

# RTC (INT 8) interrupt override
rtc_interrupt:
	pushf
	cli
	cmpw cs:counter_timeout, 0x0000
	jz   rtc_exec_org
	decw cs:counter_dot
	decw cs:counter_timeout

rtc_exec_org:
rtc_interrupt_org = .+1		# Same trick used by the LILO mapper
	call 0:INT_RTC*4	# These CS:IP values will be changed at runtime
	iret

# ---------------------------------------------------------------------------

# DISK (INT 13h) interrupt override
dsk_interrupt:
	pushf
	cli
	call disk_swap
	mov cs:[int13_cmd], ah	# Keep a copy of the command for later referal

dsk_interrupt_org = .+1
	call 0:INT_DSK*4	# These CS:IP values will be changed at runtime
	push ax
	mov  ah, cs:[int13_cmd]	# Check the original command for swap exceptions
	cmp  ah, 0x08 		# LILO's mapper has these 2 exceptions
	je   0f
	cmp  ah, 0x15
	je   0f
	call disk_swap
0:	pop  ax
	iret


/********************************************************************************/
/* Data section                                                                 */
/********************************************************************************/

int13_cmd:	.byte 0x00
prompt_string:	.string "\r\nPress any key to boot from USB."
dot_string = 	.-2		# Reuse the end of previous string
counter_timeout:.word DOT_NUMBER*DOT_TIMEOUT + 1
counter_dot:	.word DOT_TIMEOUT


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
