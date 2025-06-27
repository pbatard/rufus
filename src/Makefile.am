SUBDIRS = ../.mingw bled ext2fs ms-sys syslinux/libfat syslinux/libinstaller syslinux/win libcdio/iso9660 libcdio/udf libcdio/driver wimlib ../res/loc
# As far as I can tell, the following libraries are *not* vulnerable to side-loading, so we link using their regular version:
NONVULNERABLE_LIBS = -lole32 -lgdi32 -lshlwapi -lcomctl32 -luuid -lntdll
# The following libraries are vulnerable (or have an unknown vulnerability status), so we link using our delay-loaded replacement:
# See https://github.com/pbatard/rufus/issues/2272
VULNERABLE_LIBS = -lcrypt32-delaylib -ldwmapi-delaylib -lsetupapi-delaylib -luxtheme-delaylib -lversion-delaylib -lvirtdisk-delaylib -lwininet-delaylib -lwintrust-delaylib

noinst_PROGRAMS = rufus

AM_V_WINDRES_0 = @echo "  RC     $@";$(WINDRES)
AM_V_WINDRES_1 = $(WINDRES)
AM_V_WINDRES_  = $(AM_V_WINDRES_$(AM_DEFAULT_VERBOSITY))
AM_V_WINDRES   = $(AM_V_WINDRES_$(V))

%_rc.o: %.rc ../res/loc/embedded.loc
	$(AM_V_WINDRES) $(AM_RCFLAGS) -i $< -o $@

rufus_SOURCES = badblocks.c darkmode.c dev.c dos.c dos_locale.c drive.c format.c format_ext.c format_fat32.c hash.c icon.c iso.c localization.c \
	 net.c parser.c pki.c process.c cregex_compile.c cregex_parse.c cregex_vm.c rufus.c smart.c stdfn.c stdio.c stdlg.c syslinux.c ui.c vhd.c wue.c xml.c
rufus_CFLAGS = -I$(srcdir)/ms-sys/inc -I$(srcdir)/syslinux/libfat -I$(srcdir)/syslinux/libinstaller -I$(srcdir)/syslinux/win -I$(srcdir)/libcdio -I$(srcdir)/wimlib -I$(srcdir)/../res $(AM_CFLAGS) \
	-DEXT2_FLAT_INCLUDES=0 -D_RUFUS -DSOLUTION=rufus
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows -L../.mingw
rufus_LDADD = rufus_rc.o bled/libbled.a ext2fs/libext2fs.a ms-sys/libmssys.a syslinux/libfat/libfat.a syslinux/libinstaller/libinstaller.a syslinux/win/libwin.a \
	libcdio/iso9660/libiso9660.a libcdio/udf/libudf.a libcdio/driver/libdriver.a wimlib/libwim.a $(NONVULNERABLE_LIBS) $(VULNERABLE_LIBS)
