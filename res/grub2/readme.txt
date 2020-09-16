This directory contains the Grub 2.0 boot records that are used by Rufus

* boot.img and core.img were created from:
    https://ftp.gnu.org/gnu/grub/grub-2.04.tar.xz
  with the following two extra patches applied:
  - https://lists.gnu.org/archive/html/grub-devel/2020-07/msg00016.html
  - https://lists.gnu.org/archive/html/grub-devel/2020-07/msg00017.html
  on a Debian 10.x x64 system using the commands:
    ./autogen.sh
    ./configure --disable-nls --enable-boot-time
    make -j6
    cd grub-core
    ../grub-mkimage -v -O i386-pc -d. -p\(hd0,msdos1\)/boot/grub biosdisk part_msdos fat ntfs exfat -o core.img

* boot.img has been modified to nop the jump @ 0x66 as per grub2's setup.c comments:
  /* If DEST_DRIVE is a hard disk, enable the workaround, which is
     for buggy BIOSes which don't pass boot drive correctly. Instead,
     they pass 0x00 or 0x01 even when booted from 0x80.  */

* Note that, for convenience reasons, the content of boot.img is *not* the one that
  Rufus processes when writing the MBR.
  Instead, the byte array from src/ms-sys/inc/mbr_grub2.h (whose content is identical)
  is what Rufus uses. If you modify these files, be mindful that you may also need
  to update the array in mbr_grub2.h.

* For details, see src/format.c, src/msys/br.c and src/msys/inc/mbr_grub2.h.