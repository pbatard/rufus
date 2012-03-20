#
# This file is part of the Rufus project.
#
# Copyright (c) 2012 Pete Batard <pete@akeo.ie>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation, either version 3 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#

OBJECTS   = mbr.o
TARGET    = mbr

ASM       = gcc
CC        = gcc
LD        = ld
OBJDUMP   = objdump
OBJCOPY   = objcopy
CFLAGS    = -m32
LDFLAGS   = -nostartfile

.PHONY: all clean

all: $(TARGET).bin

clean:
	@-rm -f -v *.o *.out $(TARGET).map

%.o: %.c Makefile
	@echo "[CC]  $@"
	@$(CC) -c -o $*.o $(CFLAGS) $<

%.o: %.S Makefile
	@echo "[AS]  $<"
	@$(ASM) -c -o $*.o $(CFLAGS) $<

# Produce a disassembly dump, for verification purposes
dis: $(TARGET).out
	@echo "[DIS] $<"
	@$(OBJCOPY) -O binary -j .main --set-section-flags .main=alloc,load,readonly,code $< main.bin
	@$(OBJDUMP) -D -bbinary -mi8086 -Mintel main.bin | less
	@-rm -f main.bin

$(TARGET).out: $(OBJECTS) $(TARGET).ld
	@echo "[LD]  $@"
	@$(LD) $(LDFLAGS) -T$(TARGET).ld -o $@ $(OBJECTS) -Map $(TARGET).map

$(TARGET).bin: $(TARGET).out
	@echo "[MBR] $@"
	@# Note: -j only works for sections that have the 'ALLOC' flag set
	@$(OBJCOPY) -O binary -j .main --gap-fill=0x00 $< $@
