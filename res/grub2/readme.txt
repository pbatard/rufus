This directory contains the Grub 2.0 boot records that are used by Rufus

* boot.img and core.img were compiled from
  http://ftp.gnu.org/gnu/grub/grub-2.02.tar.xz, on a Debian stretch x64 system.
  This was done following the guide from:
  http://pete.akeo.ie/2014/05/compiling-and-installing-grub2-for.html.
  --enable-boot-time was also added during ./configure for Manjaro Linux compatibility.

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