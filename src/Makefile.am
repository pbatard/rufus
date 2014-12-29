SUBDIRS = bled ms-sys syslinux/libfat syslinux/libinstaller libcdio/iso9660 libcdio/udf libcdio/driver ../res/localization

noinst_PROGRAMS = rufus

AM_V_WINDRES_0 = @echo "  RC     $@";$(WINDRES)
AM_V_WINDRES_1 = $(WINDRES)
AM_V_WINDRES_  = $(AM_V_WINDRES_$(AM_DEFAULT_VERBOSITY))
AM_V_WINDRES   = $(AM_V_WINDRES_$(V))

%_rc.o: %.rc ../res/localization/embedded.loc
	$(AM_V_WINDRES) $(AM_RCFLAGS) -i $< -o $@

rufus_SOURCES = drive.c icon.c parser.c localization.c iso.c net.c dos.c dos_locale.c badblocks.c syslinux.c usb.c vhd.c format.c smart.c stdio.c stdfn.c stdlg.c rufus.c
rufus_CFLAGS = -I./ms-sys/inc -I./syslinux/libfat -I./syslinux/libinstaller -I./libcdio $(AM_CFLAGS)
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_LDADD = rufus_rc.o bled/libbled.a ms-sys/libmssys.a syslinux/libfat/libfat.a syslinux/libinstaller/libinstaller.a \
	libcdio/iso9660/libiso9660.a libcdio/udf/libudf.a libcdio/driver/libdriver.a -lsetupapi -lole32 -lgdi32 -lwininet -lshlwapi
