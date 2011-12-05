INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = rufus

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

rufus_rc.o: rufus.rc
	$(pkg_v_rc)$(WINDRES) -i $< -o $@

rufus_SOURCES = fat12.c fat16.c fat32.c br.c file.c drive.c msdos.c format.c stdio.c stdlg.c rufus.c
rufus_CFLAGS = -I./inc $(ARCH_CFLAGS) $(AM_CFLAGS)
# static ensures that the exe can be shared as a standalone, and still allow driver installation
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows -static
rufus_LDADD = rufus_rc.o -lsetupapi -lole32 -lgdi32

# This step produces the UPX compressed and signed release that is made available for public download
release: rufus.exe
	@$(STRIP) rufus.exe
	@upx rufus.exe
	@cmd.exe /k _sign.bat rufus.exe
