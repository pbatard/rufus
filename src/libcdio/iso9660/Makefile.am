noinst_LIBRARIES = libiso9660.a
libiso9660_a_SOURCES = iso9660.c iso9660_fs.c rock.c xa.c
# Boy do you NOT want to have HAVE_CONFIG_H set before $(AM_CFLAGS) with Clang!
libiso9660_a_CFLAGS = $(AM_CFLAGS) -DHAVE_CONFIG_H -I$(srcdir) -I$(srcdir)/.. -I$(srcdir)/../driver
