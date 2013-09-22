Rufus: The Reliable USB Formatting Utility

Features:
- Formats USB flash drives to FAT/FAT32/NTFS/UDF/exFAT
- Creates DOS bootable USB drives, with no external files required
- Creates MBR or GPT/UEFI bootable USB drives
- Creates bootable USB drives from bootable ISOs (Windows, Linux, etc.)
- Twice as fast as Microsoft's USB/DVD tool or UNetbootin, on ISO->USB (1)
- Can perform bad blocks check, with fake drive detection
- Modern UI
- Small footprint, no installation required
- 100% Free Source Software (GPL v3)

Compilation:
  Use either Visual Studio 2012, WDK 7.1 (Windows Driver Kit) or MinGW and then
  invoke the .sln, wdk_build.cmd or configure/make respectively.
  
Additional information:
  Rufus provides extensive information about what it is doing, either through
  its easily accessible log, or through the Windows debug facility.

More info:
  http://rufus.akeo.ie

Enhancements/Bugs
  https://github.com/pbatard/rufus/issues


(1) Tests carried out with a 16 GB USB 3.0 ADATA pen drive on a
    Core 2 duo/4 GB RAM platform running Windows 7 x64.
    ISO: en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso
    - Windows 7 USB/DVD Download Tool v1.0.30:  8 mins 10s
    - UNetbootin v1.1.1.1:                      6 mins 20s
    - Rufus v1.1.0:                             3 mins 25s
