Rufus: The Reliable USB Formatting Utility

Features:
- Formats USB memory sticks to FAT/FAT32/NTFS/exFAT
- Creates MS-DOS/FreeDOS bootable USB memory sticks, with no external files required
- Checks for bad blocks
- Modern UI, with UAC elevation for Windows Vista and later
- Fully Open Source (GPL v3)

Compilation:
  Use either Visual Studio 2010, WDK (Windows Driver Kit) or MinGW and then
  invoke the .sln, wdk_build.cmd or configure/make respectively.
  You can change the project options (FreeDOS support, etc) by editing the top-level
  ms-config.h (Visual Studio, WDK) or running "./configure --help" (MinGW).
  
Additional information:
  Rufus provides extensive information abour what it is doing through the Windows
  debug facility, which can be accessed with an application such as DebugView.

More info:
  http://rufus.akeo.ie

Enhancements/Bugs
  https://github.com/pbatard/rufus/issues
