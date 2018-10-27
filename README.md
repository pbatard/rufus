Rufus: The Reliable USB Formatting Utility
==========================================

[![Build status](https://img.shields.io/appveyor/ci/pbatard/rufus.svg?style=flat-square)](https://ci.appveyor.com/project/pbatard/rufus)
[![Coverity Scan Build Status](https://img.shields.io/coverity/scan/2172.svg?style=flat-square)](https://scan.coverity.com/projects/pbatard-rufus)
[![Build contributors](https://img.shields.io/github/contributors/pbatard/rufus.svg?style=flat-square)](https://github.com/pbatard/rufus/graphs/contributors)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.en.html)

![Rufus logo](https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png)

Features
--------

* Format USB, flash card and virtual drives to FAT/FAT32/NTFS/UDF/exFAT/ReFS
* Create DOS bootable USB drives, using [FreeDOS](http://www.freedos.org) or MS-DOS (Windows 8.1 or earlier)
* Create BIOS or UEFI bootable drives, including [UEFI bootable NTFS](https://github.com/pbatard/uefi-ntfs)
* Create bootable drives from bootable ISOs (Windows, Linux, etc.)
* Create bootable drives from bootable disk images, including compressed ones
* Create [Windows To Go](https://en.wikipedia.org/wiki/Windows_To_Go) drives
* Compute MD5, SHA-1 and SHA-256 checksums of the selected image
* Twice as fast as Microsoft's USB/DVD tool or UNetbootin, on ISO -> USB creation <sup>(1)</sup>
* Perform bad blocks checks, including detection of "fake" flash drives
* Modern and familiar UI, with [39 languages natively supported](https://github.com/pbatard/rufus/wiki/FAQ#What_languages_are_natively_supported_by_Rufus)
* Small footprint. No installation required.
* Portable
* 100% [Free Software](https://www.gnu.org/philosophy/free-sw) ([GPL v3](https://www.gnu.org/licenses/gpl-3.0))

Compilation
-----------

Use either Visual Studio 2017 (version 15.8 or later with SDK 10.0.17134 installed) or MinGW and
then invoke the `.sln` or `configure`/`make` respectively.

#### Visual Studio
Note that, since Rufus is an OSI compliant Open Source project, you are entitled to
download and use the *freely available* [Visual Studio Community Edition](https://www.visualstudio.com/vs/community/)
to build, run or develop for Rufus. As per the Visual Studio Community Edition license
this applies regardless of whether you are an individual or a corporate user.

Additional information
----------------------

Rufus provides extensive information about what it is doing, either through its
easily accessible log, or through the [Windows debug facility](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).

* [__Official Website__](https://rufus.ie)
* [FAQ](https://github.com/pbatard/rufus/wiki/FAQ)

Enhancements/Bugs
-----------------

Please use the [GitHub issue tracker](https://github.com/pbatard/rufus/issues)
for reporting problems or suggesting new features.


<sup>(1)</sup> Tests carried out with a 16 GB USB 3.0 ADATA pen drive on a Core 2 duo/4 GB RAM platform running Windows 7 x64.
ISO: `en_windows_7_ultimate_with_sp1_x64_dvd_618240.iso`

| Name of tool | Version | Time |
| ------------ | ------- | ---- |
| [Windows USB/DVD Download Tool](https://www.microsoft.com/en-us/download/windows-usb-dvd-download-tool) | v1.0.30 | 8 mins 10s |
| [UNetbootin](https://unetbootin.github.io) | v1.1.1.1 | 6 mins 20s |
| **Rufus** | v1.1.0 | **3 mins 25s** |
