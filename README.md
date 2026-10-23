Rufus: The Reliable USB Formatting Utility
==========================================

[![VS2022 Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/vs2026.yml?branch=master&style=flat-square&label=VS2026%20Build)](https://github.com/pbatard/rufus/actions/workflows/vs2026.yml)
[![MinGW Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/mingw.yml?branch=master&style=flat-square&label=MinGW%20Build)](https://github.com/pbatard/rufus/actions/workflows/mingw.yml)
[![Coverity Scan Status](https://img.shields.io/coverity/scan/2172.svg?style=flat-square&label=Coverity%20Analysis)](https://scan.coverity.com/projects/pbatard-rufus)  
[![Latest Release](https://img.shields.io/github/release-pre/pbatard/rufus.svg?style=flat-square&label=Latest%20Release)](https://github.com/pbatard/rufus/releases)
[![Licence](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square&label=License)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Download Stats](https://img.shields.io/github/downloads/pbatard/rufus/total.svg?label=Downloads&style=flat-square)](https://github.com/pbatard/rufus/releases)
[![Contributors](https://img.shields.io/github/contributors/pbatard/rufus.svg?style=flat-square&label=Contributors)](https://github.com/pbatard/rufus/graphs/contributors)

![Rufus logo](https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png)

Rufus is a utility that helps format and create bootable USB flash drives.

Features
--------

* Format USB, flash card and virtual drives to FAT/FAT32/NTFS/UDF/exFAT/ReFS/ext2/ext3
* Create DOS bootable USB drives using [FreeDOS](https://www.freedos.org) or MS-DOS
* Create BIOS or UEFI bootable drives, including [UEFI bootable NTFS](https://github.com/pbatard/uefi-ntfs)
* Create bootable drives from bootable ISOs (Windows, Linux, etc.)
* Create bootable drives from bootable disk images, including compressed ones
* Create Windows 11 installation drives for PCs that don't have TPM or Secure Boot
* Create [Windows To Go](https://en.wikipedia.org/wiki/Windows_To_Go) drives
* Create VHD/DD, VHDX and FFU images of an existing drive
* Create persistent Linux partitions
* Compute MD5, SHA-1, SHA-256 and SHA-512 checksums of the selected image
* Perform runtime validation of UEFI bootable media
* Improve Windows installation experience by automatically setting up OOBE parameters (local account, privacy options, etc.)
* Perform bad blocks checks, including detection of "fake" flash drives
* Download official Microsoft Windows 8, Windows 10 or Windows 11 retail ISOs
* Download [UEFI Shell](https://github.com/pbatard/UEFI-Shell) ISOs
* Modern and familiar UI, with [38 languages natively supported](https://github.com/pbatard/rufus/wiki/FAQ#What_languages_are_natively_supported_by_Rufus)
* Small footprint. No installation required.
* Portable. Secure Boot compatible.
* 100% [Free Software](https://www.gnu.org/philosophy/free-sw) ([GPL v3](https://www.gnu.org/licenses/gpl-3.0))

Compilation
-----------

Use either Visual Studio 2026 or MinGW and then invoke the `.sln` or `configure`/`make` respectively.

#### Visual Studio

Rufus is an OSI compliant Open Source project. You are entitled to
download and use the *freely available* [Visual Studio Community Edition](https://www.visualstudio.com/vs/community/)
to build, run or develop for Rufus. As per the Visual Studio Community Edition license,
this applies regardless of whether you are an individual or a corporate user.

Additional information
----------------------

Rufus provides extensive information about what it is doing, either through its
easily accessible log, or through the [Windows debug facility](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).

* [__Official Website__](https://rufus.ie)
* [FAQ](https://github.com/pbatard/rufus/wiki/FAQ)
* [Security and safety measures](https://github.com/pbatard/rufus/wiki/Security)

Automation command line mode
----------------------------

This change adds an explicit, opt-in automation mode for unattended image
flashing. Normal GUI operation and the existing `-i/--iso` startup behavior are
unchanged. Automation requires an explicit target disk and `--yes`; use
`--dry-run` to validate the image and target without writing.

Example (run from an elevated command prompt):

```text
rufus.exe --auto --image C:\\images\\bootstrap.img.gz --disk 3 --yes --mode dd --label ALPINE-STD
```

The disk selector is a physical disk number, so `3` selects
`\\.\PhysicalDrive3`. Fixed USB disks are excluded by default; add `--force`
when the target is not presented as removable. `--force` also disables the
image-size guard and should only be used with a verified target selector.

Before selecting a target, `rufus.exe --list-disks` prints the disks currently
visible to Rufus as tab-separated records containing the physical-drive name,
label, model, and size in bytes. Use `--force --list-disks` when fixed disks
must also be included.

For repeatable jobs, use a UTF-8 `key=value` file:

```text
image=C:\\images\\bootstrap.img.gz
disk=3
auto=true
yes=true
mode=dd
filesystem=fat32
label=ALPINE-STD
quiet=true
```

Run it with `rufus.exe --config flash.conf`. Supported keys are `image`,
`disk`, `auto`, `yes`, `quiet`, `dry-run`, `force`, `list-disks`, `mode` (`iso`
or `dd`), `filesystem`, and `label`. Command-line options override values
loaded from the configuration file. Automation returns zero on successful
completion or dry-run validation, one on a flash/validation failure, and two
on invalid automation arguments. `--list-disks` exits after printing the
inventory and does not modify any disk.

The command-line and configuration interfaces are specified in
[`docs/automation-cli.md`](docs/automation-cli.md).

Enhancements/Bugs
-----------------

Please use the [GitHub issue tracker](https://github.com/pbatard/rufus/issues)
for reporting problems or suggesting new features.
