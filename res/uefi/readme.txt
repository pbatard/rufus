This directory contains a flat image of the FAT UEFI:NTFS partition added by
Rufus for NTFS and exFAT UEFI boot support.

See https://github.com/pbatard/uefi-ntfs for more details.

This image, which can be mounted as a FAT file system or opened in 7-zip,
contains the following data:

o Secure Boot signed NTFS UEFI drivers, derived from ntfs-3g [1].
  These drivers are the exact same as the read-only binaries from release 1.7,
  except for the addition of Microsoft's Secure Boot signature.
  Note that, per Microsoft's current Secure Boot signing policies, the 32-bit
  ARM driver (ntfs_arm.efi) is not Secure Boot signed.

o Non Secure Boot signed exFAT UEFI drivers from EfiFs [2].
  These drivers are the exact same as the binaries from EfiFs release 1.9 but,
  because they are licensed under GPLv3, cannot be Secure Boot signed.

o Secure Boot signed UEFI:NTFS bootloader binaries [3].
  These drivers are the exact same as the binaries from release 2.3, except for
  the addition of Microsoft's Secure Boot signature.
  Note that, per Microsoft's current Secure Boot signing policies, the 32-bit
  ARM bootloader (bootarm.efi) is not Secure Boot signed.

The above means that, if booting an NTFS partition on an x86_32, x86_64 or ARM64
system, Secure Boot does not need to be disabled.

The FAT partition was created on Debian GNU/Linux using the following commands
  dd if=/dev/zero of=uefi-ntfs.img bs=512 count=2048
  mkfs.vfat -n UEFI_NTFS uefi-ntfs.img
and then mounting the uefi-ntfs.img image and copying the relevant files.

[1] https://github.com/pbatard/ntfs-3g
[2] https://github.com/pbatard/efifs
[3] https://github.com/pbatard/uefi-ntfs
