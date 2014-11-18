This directory contains the Grub 2.0 boot records that are used by Rufus

* boot.img was compiled from git://git.savannah.gnu.org/grub.git at commit:
  72ec399ad8d6348b6c74ea63d80c79784c8b84ae.

* core.img was compiled from grub 2.00-22 using the tarballs found at
  https://launchpad.net/ubuntu/+source/grub2/2.00-22.
  The use of the 2.00-22 source is done for compatibility reasons.

* This was done on a Debian 7.7.0 x64 system using gcc-multilib 4.7.2, following
  the guide from http://pete.akeo.ie/2014/05/compiling-and-installing-grub2-for.html
  Note that exFAT was not included in core.img in order to keep it under 31.5 KB.

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