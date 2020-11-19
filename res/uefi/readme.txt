This directory contains a flat image of the FAT UEFI:NTFS partition added by
Rufus for NTFS and exFAT UEFI boot support.

See https://github.com/pbatard/uefi-ntfs for more details.

This image, which you can mount as FAT filesystem or open in 7-zip, contains
the following data:
o The NTFS and exFAT UEFI drivers from EfiFs (https://github.com/pbatard/efifs).
  These are the \EFI\Rufus\[exfat|ntfs]_[ia32|x64|arm|aa64].efi files, which are
  identical to the v1.7 EfiFs binaries published at https://efi.akeo.ie.
o The UEFI:NTFS binaries (https://github.com/pbatard/uefi-ntfs), which were
  compiled using Visual Studio 2019 Community Edition.
  These are the \EFI\Boot\boot[ia32|x64|arm|aa64].efi files.

The FAT partition was created on Debian GNU/Linux using the following commands
  dd if=/dev/zero of=uefi-ntfs.img bs=512 count=1024
  mkfs.vfat -n UEFI_NTFS uefi-ntfs.img
and then mounting the uefi-ntfs.img image and copying the relevant files.
