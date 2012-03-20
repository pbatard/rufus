Rufus: The Reliable USB Formatting Utility - Custom MBR

# Description

This directory contains all the resources required to create an MBR that prompts
the user for boot selection, when a second bootable device (typically bootable
fixed HDD) is reported by the BIOS.

This aims at mimicking the Microsoft Windows optical installation media feature,
which may be necessary on for WinPE 2.x or earlier based installations.

# Compilation

Any gcc suite (except possibly the X-Code one on OS-X) should be able to compile
the MBR by invoking 'make'. A 'make dis', that produces a disassembly dump is
also provided for your convenience.

# Primer

The way this bootloader achieves the feature highlighted above is as follows:
1. An attempt to read the MBR of the second bootable drive (0x81) is made
   through INT_13h (in either CHS or LBA mode depending on the extensions 
   detected)
2. If that attempts succeeds, then the partition table from the newly read MBR
   is checked for an active/bootable entry.
3. If such a partition is found, a prompt is displayed to the user and an RTC 
   timer interrupt (INT_8h) override is added so that dots are displayed at
   regular interval. Then the keyboard is checked for entry.
4. If the user presses a key, the first partition boot record from the USB is
   read (according to the values found in the USB MBR partition table) and
   executed
5. If no key is pressed, then an INT_13h (disk access interrupt) override is
   added to masquerade the second bootable drive (0x81) as the first one (0x80)
   so that the Windows second stage installer, or any other program relying on
   BIOS disk access, behave as if there was no USB drive inserted.
6. In case there was a failure to read the second bootable drive's MBR, or no
   active partition was detected there, the USB is booted without prompts.

# Limitations

* If you are using software RAID or a non-conventional setup, the second
  bootable disk may not be accessible through the BIOS and therefore the USB
  will always be booted
* If the bootable HDD uses LILO, a "LILO - Keytable read/checksum error" will
  be displayed when trying to boot it.
* This MBR currently does not masquerade the bootable USB drive as secondary
  (0x81) therefore an installation program ran from USB to install an OS on
  an HDD may still configure that disk as the second drive, and prevent it to
  properly boot later on.
