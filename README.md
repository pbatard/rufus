Rufus: The Reliable USB Formatting Utility
==========================================

![Rufus logo](https://raw.githubusercontent.com/pbatard/rufus/master/res/icon-set/rufus-256.png)

Features
--------

* Formats USB and Virtual HD drives to FAT/FAT32/NTFS/UDF/exFAT/ReFS
* Creates DOS bootable USB drives, using [FreeDOS](http://www.freedos.org/) or MS-DOS
* Creates BIOS or UEFI bootable drives, including UEFI bootable NTFS
* Creates bootable drives from bootable ISOs (Windows, Linux, etc.)
* Creates bootbale drives from bootable disk images, including compressed ones
* Creates [Windows To Go](https://en.wikipedia.org/wiki/Windows_To_Go) drives
* Twice as fast as Microsoft's USB/DVD tool or UNetbootin, on ISO -> USB creation (1)
* Performs bad blocks checks, including detection of "fake" flash drives
* Modern and familiar UI, with more than [30 languages natively supported](http://rufus.akeo.ie/translations)
* Small footprint. No installation required.
* Portable
* 100% [Free Software](http://www.gnu.org/philosophy/free-sw.en.html) (GPL v3)

Compilation
-----------

Use either Visual Studio 2013, WDK 7.1 (Windows Driver Kit) or MinGW and then
invoke the `.sln`, `wdk_build.cmd` or `configure`/`make` respectively.

#### Visual Studio
Note that, since Rufus is an OSI compliant Open Source project, you are entitled to
download and use the *freely available* [Visual Studio 2013 Community Edition]
(http://www.visualstudio.com/products/visual-studio-community-vs) to
build, run or develop for Rufus. As per the Visual Studio Community Edition license
this applies regardless of whether you are an individual or a corporate user.
For details, see [this](http://pete.akeo.ie/2014/11/visual-studio-2013-has-now-become.html).

Additional information
----------------------

Rufus provides extensive information about what it is doing, either through
its easily accessible log, or through the Windows debug facility.

* Website: http://rufus.akeo.ie
* FAQ: http://rufus.akeo.ie/FAQ

Enhancements/Bugs
-----------------

Please use the [GitHub issue tracker](https://github.com/pbatard/rufus/issues)
for reporting problems or suggesting new features.


(1) Tests carried out with a 16 GB USB 3.0 ADATA pen drive on a Core 2 duo/4 GB RAM platform running Windows 7 x64.
ISO: `en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso`

| Name of tool | Version | Time |
| ------------ | ------- | ---- |
| [Windows USB/DVD Download Tool](http://www.microsoft.com/en-us/download/windows-usb-dvd-download-tool) | v1.0.30 | 8 mins 10s |
| [UNetbootin](http://unetbootin.sourceforge.net) | v1.1.1.1 | 6 mins 20s |
| **Rufus** | v1.1.0 | **3 mins 25s** |
