This directory contains a flat image of the FAT UEFI:TOGO partition added by
Rufus for Windows To Go UEFI mode support as well as seamless installation of
Windows in UEFI, in the case where the original media contains a >4GB file.

This image, which you can mount as FAT filesystem or open in 7-zip, contains
the following data:
o The NTFS UEFI driver from efifs (https://github.com/pbatard/efifs) which was
  compiled, with compression disabled, using Visual Studio 2013 Community Edition.
  This is the \EFI\Rufus\ntfs_x64.efi file.
o The UEFI:TOGO binary (https://github.com/pbatard/uefi-togo), which was compiled
  using Visual Studio 2013 Community Edition.
  This is the \EFI\Boot\bootx64.efi file.
