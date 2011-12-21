#INCLUDES = -I$(top_srcdir)

noinst_PROGRAMS = rufus rufus_fd

pkg_v_rc = $(pkg_v_rc_$(V))
pkg_v_rc_ = $(pkg_v_rc_$(AM_DEFAULT_VERBOSITY))
pkg_v_rc_0 = @echo "  RC     $@";

# NB: There's a good reason we don't do something more elegant than copy/paste for _fd
rufus_rc.o: rufus.rc
	$(pkg_v_rc)$(WINDRES) -i $< -o $@

rufus_fd_rc.o: rufus.rc
	$(pkg_v_rc)$(WINDRES) -DWITH_FREEDOS -i $< -o $@

rufus_SOURCES = fat12.c fat16.c fat32.c partition_info.c br.c file.c drive.c dos.c dos_locale.c badblocks.c format.c stdio.c stdlg.c rufus.c
rufus_CFLAGS = -I./inc $(AM_CFLAGS)
rufus_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_LDADD = rufus_rc.o -lsetupapi -lole32 -lgdi32

rufus_fd_SOURCES = fat12.c fat16.c fat32.c partition_info.c br.c file.c drive.c dos.c dos_locale.c badblocks.c format.c stdio.c stdlg.c rufus.c
rufus_fd_CFLAGS = -I./inc $(AM_CFLAGS)
rufus_fd_LDFLAGS = $(AM_LDFLAGS) -mwindows
rufus_fd_LDADD = rufus_fd_rc.o -lsetupapi -lole32 -lgdi32