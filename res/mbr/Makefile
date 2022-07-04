#
# This file is part of the Rufus project.
#
# Copyright (c) 2012-2022 Pete Batard <pete@akeo.ie>
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

TEST_TARGET = msg

ASM         = gcc
CC          = gcc
LD          = ld
OBJDUMP     = objdump
OBJCOPY     = objcopy
CFLAGS      = -m32 -nostartfiles -nodefaultlibs
BOCHS       = "C:/Program Files/Bochs/bochsdbg.exe"

.PHONY: all clean

all: mbr.bin msg.bin

clean:
	@-rm -f -v *.o *.out *.map *.bin *.img *.ini

%.o: %.c Makefile
	@echo "[CC]  $@"
	@$(CC) -c -o $*.o $(CFLAGS) $<

%.o: %.S Makefile
	@echo "[AS]  $<"
	@$(ASM) -c -o $*.o $(CFLAGS) $<

# Produce a disassembly dump, for verification purposes
dis: $(TEST_TARGET).out
	@echo "[DIS] $<"
	@$(OBJCOPY) -O binary -j .main --set-section-flags .main=alloc,load,readonly,code $< main.bin
	@$(OBJDUMP) -D -bbinary -mi8086 -Mintel main.bin | less
	@-rm -f main.bin

# Run the MBR in a Bochs environment (append msg.txt in subsequent blocks)
test: $(TEST_TARGET).bin
	@test -s $(BOCHS) || { echo "Error: $(BOCHS) was not found on this system"; exit 1; }
	@cp $(TEST_TARGET).bin disk.img
	@truncate -c -s 17K disk.img
	@cat msg.txt >> disk.img
	@truncate -c -s 10M disk.img
	@-$(BOCHS) -f bochsrc.bxrc -q

%.out: %.o mbr.ld
	@echo "[LD]  $@"
	@$(LD) $(LDFLAGS) -Tmbr.ld -o $@ $< -Map $*.map

%.bin: %.out
	@echo "[MBR] $@"
	@# Note: -j only works for sections that have the 'ALLOC' flag set
	@$(OBJCOPY) -O binary -j .main --gap-fill=0x00 $< $@
	@-rm -f $< $*.map
