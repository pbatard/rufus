SUBDIRS = ms-sys syslinux/libfat syslinux/libinstaller libcdio/iso9660 libcdio/udf libcdio/driver

noinst_PROGRAMS = rufus

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

%_rc.o: %.rc
	$(pkg_v_rc)$(WINDRES) $(AM_RCFLAGS) -i $< -o $@

rufus_SOURCES = drive.c iso.c dos.c dos_locale.c badblocks.c syslinux.c format.c stdio.c stdlg.c rufus.c
rufus_CFLAGS = -I./ms-sys/inc -I./syslinux/libfat -I./syslinux/libinstaller -I./libcdio $(AM_CFLAGS)
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_LDADD = rufus_rc.o ms-sys/libmssys.a syslinux/libfat/libfat.a syslinux/libinstaller/libinstaller.a \
	libcdio/iso9660/libiso9660.a libcdio/udf/libudf.a libcdio/driver/libdriver.a -lsetupapi -lole32 -lgdi32 
