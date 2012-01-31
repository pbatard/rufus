noinst_LIBRARIES = libdriver.a
libdriver_a_SOURCES = disc.c ds.c logging.c read.c sector.c track.c util.c _cdio_stdio.c _cdio_stream.c utf8.c
# Boy do you NOT want to have HAVE_CONFIG_H set before $(AM_CFLAGS) with Clang!
libdriver_a_CFLAGS = $(AM_CFLAGS) -DHAVE_CONFIG_H -I. -I..
