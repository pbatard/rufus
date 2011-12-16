The COMMAND.COM file was extracted from the fd11tst3.img image available at:
http://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/distributions/1.1-test3/

Because of issue #19 (see https://github.com/pbatard/rufus/issues/19) KERNEL.SYS
is a recompiled version, from the 2011.12.16 svn source, with FORCELBA enabled
and with the following patch having been applied:

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
