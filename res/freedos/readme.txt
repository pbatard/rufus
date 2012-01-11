o COMMAND.COM file was extracted from the fd11tst3.img image available at:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.1-test3/

o The EGA files were extracted from the cpidos30.zip available at:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/cpi/

o DISPLAY.EXE was extracted from /disp013x.zip available at:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/display

o KEYB.EXE was extracted from KEYB201.ZIP available at:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/keyb/2.01/
  
o The keyboard layouts (KEYB___.SYS) were extracted from kpdos30x.zip:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/keyb/kblayout/

o MODE.COM was extracted from mode-2005may12.zip available at:
  http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/dos/mode/2005/

o Because of issue #19 (see https://github.com/pbatard/rufus/issues/19)
  KERNEL.SYS is a recompiled version, from the 2011.12.16 svn source, with
  FORCELBA enabled and with the following patch having been applied:

Index: kernel/initdisk.c
===================================================================
--- kernel/initdisk.c	(revision 1696)
+++ kernel/initdisk.c	(working copy)
@@ -810,10 +810,13 @@
 void print_warning_suspect(char *partitionName, UBYTE fs, struct CHS *chs,
                            struct CHS *pEntry_chs)
 {
-  printf("WARNING: using suspect partition %s FS %02x:", partitionName, fs);
-  printCHS(" with calculated values ", chs);
-  printCHS(" instead of ", pEntry_chs);
-  printf("\n");
+  if (!InitKernelConfig.ForceLBA)
+  {
+    printf("WARNING: using suspect partition %s FS %02x:", partitionName, fs);
+    printCHS(" with calculated values ", chs);
+    printCHS(" instead of ", pEntry_chs);
+    printf("\n");
+  }
   memcpy(pEntry_chs, chs, sizeof(struct CHS));
 }

Recompilation was done using Open Watcom, as described at:
http://pete.akeo.ie/2011/12/compiling-freedos-on-windows-using.html
