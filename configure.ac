AC_INIT([rufus], [1.0.7], [https://github.com/pbatard/rufus/issues], [rufus], [http://rufus.akeo.ie])
AM_INIT_AUTOMAKE([-Wno-portability foreign no-dist no-dependencies])
AC_CONFIG_SRCDIR([src/rufus.c])
AC_CONFIG_MACRO_DIR([m4])
# Enable silent build rules by default (Automake v1.11 or later).
# Disable by either passing --disable-silent-rules to configure or passing V=1 to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
         [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_PREREQ([2.50])
AC_PROG_CC
AC_PROG_RANLIB
AC_PATH_PROG(RM, rm, rm)
AC_CHECK_TOOL(STRIP, strip, strip)
AC_CHECK_TOOL(WINDRES, windres, windres)
AC_C_INLINE
AC_DEFINE([_GNU_SOURCE], [], [Use GNU extensions])

## We don't actually use config.h for the time being
#AM_CONFIG_HEADER([config.h])

## The following creates config.guess and a bunch of polluting stuff
## => avoid it for now
#AC_CANONICAL_HOST
#AC_MSG_CHECKING([development environment])
#case $host in
#*-mingw*)
#	AC_MSG_RESULT([MinGW])
#	AM_CFLAGS="-Wshadow"
#	;;
#*)
#	AC_MSG_ERROR([unsupported development environment])
#esac

AC_MSG_RESULT([enabling support for large files (_FILE_OFFSET_BITS=64)])
AM_CFLAGS="-D_FILE_OFFSET_BITS=64 -D_OFF_T_DEFINED -D_OFF_T_ -D_off_t=off64_t -Doff_t=off64_t"

# Clang needs an explicit WIN32_WINNT defined else it produces warnings
# in msapi_utf8.h - including winver.h only doesn't work
AM_CFLAGS="${AM_CFLAGS} -DWINVER=0x501 -D_WIN32_IE=0x501 -D_WIN32_WINNT=0x501"
AM_LDFLAGS="${AM_LDFLAGS} -Wl,-no-undefined"

AC_ARG_WITH([freedos],
	[AS_HELP_STRING([--with-freedos],
		[embed FreeDOS support (default=yes)])],
	[],
	[with_freedos=yes])
if test "x$with_freedos" != "xno" ; then
	AM_RCFLAGS="${AM_RCFLAGS} -DWITH_FREEDOS"
	EXESUFFIX="f"
fi

AC_ARG_WITH([syslinux],
	[AS_HELP_STRING([--with-syslinux],
		[embed SysLinux support (default=yes)])],
	[],
	[with_freedos=yes])
if test "x$with_syslinux" != "xno" ; then
	AM_RCFLAGS="${AM_RCFLAGS} -DWITH_SYSLINUX"
	EXESUFFIX="f"
fi


# Debug symbols
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [keep debug symbols for gdb (default=yes)]),
	[debug_enabled=$enableval],
	[debug_enabled='yes'])
if test "x$debug_enabled" = "xyes" ; then
  CFLAGS="-g -O2"
else
  CFLAGS="-O2"
  LDFLAGS="-s"
fi

# check for -Wno-pointer-sign compiler support (GCC >= 4)
saved_CFLAGS="${CFLAGS}"
CFLAGS="$CFLAGS -Wno-pointer-sign"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
	[nopointersign_cflags="-Wno-pointer-sign"], [nopointersign_cflags=""])
CFLAGS="${saved_CFLAGS}"

AM_CFLAGS="$AM_CFLAGS -std=gnu99 -Wshadow -Wall -Wundef -Wunused -Wstrict-prototypes -Werror-implicit-function-declaration $nopointersign_cflags"

AC_SUBST([VISIBILITY_CFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])
AC_SUBST([AM_RCFLAGS])
AC_SUBST([EXESUFFIX])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([src/ms-sys/Makefile])
AC_CONFIG_FILES([src/syslinux/libfat/Makefile])
AC_CONFIG_FILES([src/syslinux/libinstaller/Makefile])
AC_CONFIG_FILES([src/libcdio/iso9660/Makefile])
AC_CONFIG_FILES([src/libcdio/udf/Makefile])
AC_CONFIG_FILES([src/libcdio/driver/Makefile])
AC_OUTPUT
