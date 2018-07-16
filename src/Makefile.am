SUBDIRS = bled ms-sys syslinux/libfat syslinux/libinstaller syslinux/win libcdio/iso9660 libcdio/udf libcdio/driver ../res/loc

noinst_PROGRAMS = rufus

AM_V_WINDRES_0 = @echo "  RC     $@";$(WINDRES)
AM_V_WINDRES_1 = $(WINDRES)
AM_V_WINDRES_  = $(AM_V_WINDRES_$(AM_DEFAULT_VERBOSITY))
AM_V_WINDRES   = $(AM_V_WINDRES_$(V))

%_rc.o: %.rc ../res/loc/embedded.loc
	$(AM_V_WINDRES) $(AM_RCFLAGS) -i $< -o $@

rufus_SOURCES = badblocks.c checksum.c dev.c dos.c dos_locale.c drive.c format.c icon.c iso.c localization.c \
	net.c parser.c pki.c process.c rufus.c smart.c stdfn.c stdio.c stdlg.c syslinux.c ui.c vhd.c
rufus_CFLAGS = -I./ms-sys/inc -I./syslinux/libfat -I./syslinux/libinstaller -I./syslinux/win -I./libcdio $(AM_CFLAGS)
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_LDADD = rufus_rc.o bled/libbled.a ms-sys/libmssys.a syslinux/libfat/libfat.a syslinux/libinstaller/libinstaller.a syslinux/win/libwin.a \
	libcdio/iso9660/libiso9660.a libcdio/udf/libudf.a libcdio/driver/libdriver.a -lsetupapi -lole32 -lgdi32 -lshlwapi -lcrypt32 -lwintrust -lcomdlg32 -lcomctl32 -luuid -lpsapi
