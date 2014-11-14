Rufus: The Reliable USB Formatting Utility

Features:
- Formats USB flash drives as well as VHD images to FAT/FAT32/NTFS/UDF/exFAT/ReFS
- Creates DOS bootable USB drives, using FreeDOS or MS-DOS with no external files required
- Creates MBR or GPT/UEFI bootable drives
- Creates bootable drives from bootable ISOs (Windows, Linux, etc.)
- Twice as fast as Microsoft's USB/DVD tool or UNetbootin, on ISO->USB (1)
- Can perform bad blocks check, with fake drive detection
- Modern and familiar UI, with more than 30 languages natively supported (2)
- Small footprint, no installation required
- 100% Free Software (GPL v3)

Compilation:
  Use either Visual Studio 2013, WDK 7.1 (Windows Driver Kit) or MinGW and then
  invoke the .sln, wdk_build.cmd or configure/make respectively.

  Note that, since Rufus is a OSI compliant Open Source project, you are entitled to
  download and use the *freely available* Visual Studio 2013 Community Edition to
  build, run or develop for Rufus. As per the Visual Studio Community Edition license
  this applies regardless of whether you are an individual or a corporate user.
  For details, see http://www.visualstudio.com/products/visual-studio-community-vs
  or http://pete.akeo.ie/2014/11/visual-studio-2013-has-now-become.html

Additional information:
  Rufus provides extensive information about what it is doing, either through
  its easily accessible log, or through the Windows debug facility.

More info:
  http://rufus.akeo.ie
  http://rufus.akeo.ie/FAQ

Enhancements/Bugs
  https://github.com/pbatard/rufus/issues


(1) Tests carried out with a 16 GB USB 3.0 ADATA pen drive on a
    Core 2 duo/4 GB RAM platform running Windows 7 x64.
    ISO: en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso
    - Windows 7 USB/DVD Download Tool v1.0.30:  8 mins 10s
    - UNetbootin v1.1.1.1:                      6 mins 20s
    - Rufus v1.1.0:                             3 mins 25s

(2) http://rufus.akeo.ie/translations