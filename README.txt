Rufus: The Reliable USB Formatting Utility

Features:
- Formats USB flash drives to FAT/FAT32/NTFS/exFAT
- Creates DOS bootable USB drives, with no external files required
- Creates bootable USB drives from bootable ISOs (Windows, Linux, etc.)
- Twice as fast as Microsoft's USB/DVD tool or UNetbootin, on ISO->USB (1)
- Bad blocks check
- Modern UI, with UAC elevation for Windows Vista and later
- Very small footprint, no installation required
- Fully Open Source (GPL v3)

Compilation:
  Use either Visual Studio 2010, WDK (Windows Driver Kit) or MinGW and then
  invoke the .sln, wdk_build.cmd or configure/make respectively.
  You can change the project options (FreeDOS support, etc) by editing the top-level
  ms-config.h (Visual Studio, WDK) or running "./configure --help" (MinGW).
  
Additional information:
  Rufus provides extensive information abour what it is doing through the Windows
  debug facility. This info can be accessed with an application such as DebugView.

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
