#!/bin/env python3

# PE Load Configuration section enabler for MinGW/gcc executables.
# The PE executable should have a IMAGE_LOAD_CONFIG_DIRECTORY## section
# in .rdata with a 16-byte IMAGE_DIRECTORY_ENTRY_MARKER marker.

import os
import sys
import pefile

IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG = 10
IMAGE_DIRECTORY_ENTRY_MARKER      = b"_RUFUS_LOAD_CFG"

if len(sys.argv) < 2:
    raise RuntimeError("No executable path supplied")

# Create a temp file as our source
pe_dst = sys.argv[1]
pe_src = sys.argv[1] + ".tmp"
os.replace(pe_dst, pe_src)

# Open and parse PE
pe = pefile.PE(pe_src)

# Find .rdata section
rdata_section = next(
    (s for s in pe.sections if s.Name.rstrip(b'\x00') == b'.rdata'),
    None
)
if not rdata_section:
    raise RuntimeError(".rdata section not found")

# Read the section's raw data to search for the target string
raw_data = rdata_section.get_data()

# Look for the target data in the raw section data
offset = raw_data.find(IMAGE_DIRECTORY_ENTRY_MARKER)
if offset == -1:
    raise RuntimeError("Load Config marker not found")

# Move past our 16 bytes marker
offset += 0x10
# Calculate the RVA and size of the Load Config section
load_config_rva = rdata_section.VirtualAddress + offset
print(f"RVA  of Load Config: 0x{load_config_rva:X}")
load_config_size = pe.get_dword_at_rva(load_config_rva)
if (load_config_size < 0x20):
    raise RuntimeError("Size of Load Config section is too small")
print(f"Size of Load Config: 0x{load_config_size:X}")

# Update Load Config directory entry
pe.OPTIONAL_HEADER.DATA_DIRECTORY[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress = load_config_rva
pe.OPTIONAL_HEADER.DATA_DIRECTORY[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size = load_config_size

# Update the checksum
pe.OPTIONAL_HEADER.CheckSum = pe.generate_checksum()

# Write the updated PE file and remove temp
pe.write(pe_dst)
os.remove(pe_src)

# Can be validated with `DUMPBIN /LOADCONFIG <.exe>` or `objdump -x <.exe> | grep "Load Configuration"`
print(f"Successfully enabled Load Config section in '{pe_dst}'")
