noinst_PROGRAMS = rufus

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

%_rc.o: %.rc
	$(pkg_v_rc)$(WINDRES) $(AM_RCFLAGS) -i $< -o $@

rufus_SOURCES = fat12.c fat16.c fat32.c partition_info.c br.c file.c drive.c dos.c dos_locale.c badblocks.c format.c stdio.c stdlg.c rufus.c
rufus_CFLAGS = -I./inc $(AM_CFLAGS)
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_LDADD = rufus_rc.o -lsetupapi -lole32 -lgdi32
