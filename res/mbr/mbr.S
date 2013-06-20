/********************************************************************************/
/* Rufus - The Reliable USB Formatting Utility, bootable MBR with user prompt   */
/*                                                                              */
/* Copyright (c) 2012 Pete Batard <pete@akeo.ie>                                */
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
PT_MAX        = 0x04	# Number of partition entries in the partition table
PT_ENTRY_SIZE = 0x10	# Size of a partition entry in the partition table
INT_RTC       = 0x08
INT_DSK       = 0x13
DEBUG         = 0	# Set to 1 to debug INT13h (shows AH and DL values)


/********************************************************************************/
/* MBR: this section must reside at 0x00007c00, and be exactly 512 bytes        */
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
	mov  ax,[bx]
	dec  ax			# Remove 1KB from the RAM for our copy
	mov  [bx],ax
	shl  ax, 6		# Convert to segment address
	sub  ax, MBR_ADDR>>4	# For convenience of disassembly, so that irrespective
	mov  es, ax		# of the segment, our base address remains MBR_ADDR
	mov  cx, MBR_SIZE	# Move our code out of the way
	rep  movsb
	push ax			# Push the CS:IP for our retf
	push offset 0f
	retf

# ---------------------------------------------------------------------------

# From this point forward, we are running the copy at the same base but different segment
0:	mov  ds, ax		# AX = ES = CS, only DS points back to old seg => fix this
	xor  ebx, ebx		# Sector #1 in 64 bit address mode (#0)
	mov  es, bx		# ES remains set to segment 0 from here on
	inc  cx			# Sector #1 in CHS address mode (#1) (and CX = 0 from rep movsb)
	mov  dx, 0x0081		# drive number (DL), track 0 (DH)
	call read_sector
	jb   boot_usb		# If we couldn't get data => just boot USB

read_success:
	mov  bx, offset partition_table
	mov  cx, PT_MAX

check_table:			# check the partition table for an active (bootable) partition
	cmpb es:[bx], 0x00
	jl   found_active	# 0x80 or greater means the partition is active
	jnz  invalid_table	# anything between 0x01 and 0x7f is invalid
	add  bx, PT_ENTRY_SIZE	# next partition
	loop check_table

invalid_table:
	jmp  boot_usb

found_active:
	mov  si, offset prompt_string
	call print_string	# Prompt the user
	call flush_keyboard

set_rtc_int_vect:		# Set the interrupt vector for CMOS real-time clock
	mov  dx, offset rtc_interrupt
	mov  si, offset rtc_interrupt_org
	call set_int_vect

wait_for_keyboard:
	mov  ah, 0x01
	int  0x16		# KEYBOARD - CHECK BUFFER, DO NOT CLEAR
	jnz  boot_usb_rem_rtc	# Z is clear when characters are present in the buffer
	mov  ah, 0x02
	int  0x16		# KEYBOARD - GET SHIFT STATUS
	and  al, 0x04		# AL = shift status bits
	jnz  boot_usb
	cmpb ds:counter_dot, 0x00
	jg   check_timeout

print_dot:			# Every so often, we print a dot
	mov  si, offset dot_string
	call print_string
	movb ds:counter_dot, DOT_TIMEOUT

check_timeout:
	cmpb ds:counter_timeout, 0x00
	jnz  wait_for_keyboard

boot_fixed_disk:		# Timeout occurred => boot second bootable disk (non USB)
	call restore_rtc_vect	# Remove our RTC override
	movb ds:partition_table, 0x81	# target we want to swap with 0x80
	push 0x0080

boot_drive:
	mov  dx, offset dsk_interrupt		# Set interrupt override for int13h
	mov  si, offset dsk_interrupt_org
	call set_int_vect

	pop  dx			# retrieve disk index to feed BR
	pop  es
	pop  ds
	jmp  0:MBR_ADDR

# ---------------------------------------------------------------------------

boot_usb_rem_rtc:		# Boot USB drive (0x80)
	call restore_rtc_vect	# Remove our RTC override
boot_usb:
	call flush_keyboard	# Make sure the keyboard buffer is clear
	mov  bx, offset partition_table
	mov  dx, ds:[bx]
	push dx
	mov  dl, 0x80		# Override disk number, as we're not using our int yet
	mov  cx, ds:[bx+2]
	mov  ebx, ds:[bx+8]	# Must come last since it modifies BX
	call read_sector
	jnb  boot_drive
	pop  es			# failed to read PBR from USB - exit back to BIOS
	pop  ds
	retf


/********************************************************************************/
/* Subroutines                                                                  */
/********************************************************************************/

read_sector:			# Read a single sector in either CHS or LBA mode
				# EBX = LBA sector address (32 bit), CH = track
				# CL = sector,  DL = drive number, DH = head
	pusha			# save all registers
	mov  ah, 0x41
	mov  bx, 0x55aa
	int  0x13
	jb   no_ext		# failure to get ext
	cmp  bx, 0xaa55
	jnz  no_ext
	test cx, 1		# is packet access supported?
	jz   no_ext

ext:	# http://en.wikipedia.org/wiki/INT_13H#INT_13h_AH.3D42h:_Extended_Read_Sectors_From_Drive
	popa
	push ds
	xor  eax, eax
	mov  ds, ax		# We'll use the stack for DAP, which is in seg 0
	push eax		# bits 32-63 of sector address (set to 0)
	push ebx		# bits 0-31 of sector address (EBX)
	push ax			# destination segment
	push MBR_ADDR		# destination address
	inc  ax
	push ax			# number of sectors to be read (1)
	push 0x0010		# size of DAP struct
	mov  si, sp		# DAP address (= stack)
	mov  ah, 0x42		# Extended Read Sectors From Drive
	int  0x13
	lahf
	add  sp,0x10
	sahf
	pop  ds
	ret

no_ext:	# http://en.wikipedia.org/wiki/INT_13H#INT_13h_AH.3D02h:_Read_Sectors_From_Drive
	popa
	mov  bx, MBR_ADDR	# CL, CH, DL, DH, ES are already set
	mov  ax, 0x0201
	int  0x13
	ret

# ---------------------------------------------------------------------------

set_int_vect:			# Set the interrupt vector
	cli			# SI = pointer to backup vector (must contain the interrupt #)
	mov  bx, ds:[si]
	mov  eax, es:[bx]	# Backup the original vector (ES = 0)
	mov  ds:[si], eax
	mov  es:[bx], dx
	mov  es:[bx+2], cs
	sti
	ret

# ---------------------------------------------------------------------------

restore_rtc_vect:		# Restore the interrupt vector for RTC
	cli
	mov  bx, INT_RTC*4
	mov  eax, ds:rtc_interrupt_org
	mov  es:[bx], eax
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

print_string:			# Print NUL terminated string in DS:SI to console
	lodsb
	cmp  al, 0x00
	jz   0f
	mov  ah, 0x0e
	mov  bx, 0x0007		# BH = display page, BL = foreground color
	int  0x10		# VIDEO - WRITE CHARACTER AND ADVANCE CURSOR
	jmp  print_string
0:	ret

# ---------------------------------------------------------------------------

disk_swap:			# Swap disk according to part table entry
	push ax
	mov  al, cs:partition_table
	cmp  dl, 0x80
	jne  0f
	mov  dl, al	# 0x80 -> cs:pt
	jmp  1f
0:	cmp  dl, al	# cs:pt -> 0x80
	jne  1f
	mov  dl, 0x80
1:	pop ax
	ret

# ---------------------------------------------------------------------------

.if DEBUG
print_hex:			# Hex dump of AH,DL
	pusha
	mov  cx, 0x04
	mov  dh, ah
0:	rol  dx, 0x04
	mov  ax, 0xe0f
	and  al, dl
	daa
	add  al, 0xF0
	adc  al, 0x40
	int  0x10
	loop 0b
	popa
	ret
.endif


/********************************************************************************/
/* Interrupt overrides                                                          */
/********************************************************************************/

# RTC (INT 8) interrupt override
rtc_interrupt:
	cli
	cmpb cs:counter_timeout, 0x00
	jz   0f			# Don't decrement counters if timeout expired
	decb cs:counter_dot
	decb cs:counter_timeout

rtc_interrupt_org = .+1		# Same trick used by the LILO mapper
0:	jmp 0:INT_RTC*4		# These CS:IP values will be changed at runtime

# ---------------------------------------------------------------------------

# DISK (INT 13h) interrupt override
dsk_interrupt:
	pushf
.if DEBUG	
	call print_hex
.endif
	# Some machines (eg. IBM T43p) have a BIOS that reenters INT 13h to issue
	# an SCSI passthrough (AH = 50h). Therefore swapping the drive on each call
	# would result in failure. To ensure that the disk is only swapped once
	# we keep a counter, and swap only if that counter is 0.
	# NB: If concurrent INT 13h calls are issued, this approach will break
	incb cs:already_mapped
	jnz  0f
	call disk_swap
dsk_interrupt_org = .+1
0:	call 0:INT_DSK*4	# These CS:IP values will be changed at runtime
	# NB: subcommands 0x08 and 0x15 (disk props) modify DL, but they only 
	# do so to return the number of drives => unless your computer has more
	# than 128 drives, disk_swap will not touch those values.
	pushf			# Don't modify the returned flags
	decb cs:already_mapped
	jns  0f
	call disk_swap
0:	popf
	retf 2


/********************************************************************************/
/* Data section                                                                 */
/********************************************************************************/

already_mapped: .byte 0xff
counter_timeout:.byte DOT_NUMBER*DOT_TIMEOUT + 1
counter_dot:	.byte DOT_TIMEOUT
.if !DEBUG
prompt_string:	.string "\r\nPress any key to boot from USB."
.else
prompt_string:  .string "USB."
.endif
dot_string = 	.-2		# Reuse the end of previous string


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
