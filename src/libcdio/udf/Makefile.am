noinst_LIBRARIES = libudf.a
libudf_a_SOURCES = udf.c udf_file.c udf_fs.c udf_time.c filemode.c
# Boy do you NOT want to have HAVE_CONFIG_H set before $(AM_CFLAGS) with Clang!
libudf_a_CFLAGS = $(AM_CFLAGS) -DHAVE_CONFIG_H -I$(srcdir) -I$(srcdir)/.. -I$(srcdir)/../driver
