This directory contains a flat image of the FAT UEFI:NTFS partition added by
Rufus for NTFS UEFI boot support. See https://github.com/pbatard/uefi-ntfs.

This image, which you can mount as FAT filesystem or open in 7-zip, contains
the following data:
o The NTFS UEFI drivers from EfiFs (https://github.com/pbatard/efifs).
  These are the \EFI\Rufus\ntfs_[ia32|x64|arm|aa64].efi files, which were
  compiled unmodified from the EfiFs source (@d19363a5), using Visual Studio
  2017 Community Edition (v15.8.7) using the gnu-efi submodule rather than EDK2.
o The UEFI:NTFS binaries (https://github.com/pbatard/uefi-ntfs), which were also
  compiled using Visual Studio 2017 Community Edition.
  These are the \EFI\Boot\boot[ia32|x64|arm|aa64].efi files.

The FAT partition was created on Debian GNU/Linux using the following commands
  dd if=/dev/zero of=uefi-ntfs.img bs=512 count=1024
  mkfs.vfat -n UEFI_NTFS uefi-ntfs.img
and then mounting the uefi-ntfs.img image and copying the relevant files.
