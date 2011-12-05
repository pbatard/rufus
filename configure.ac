AC_INIT([rufus], [1.0.1], [https://github.com/pbatard/rufus/issues], [rufus], [https://github.com/pbatard/rufus])
AM_INIT_AUTOMAKE([-Wno-portability foreign no-dist no-dependencies])
AC_CONFIG_SRCDIR([src/rufus.c])
AC_CONFIG_MACRO_DIR([m4])
# Enable silent build rules by default (Automake v1.11 or later).
# Disable by either passing --disable-silent-rules to configure or passing V=1 to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
         [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

AC_PREREQ([2.50])
AC_PROG_CC
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

AM_CFLAGS="${AM_CFLAGS} -DWINVER=0x501 -D_WIN32_IE=0x501"
AM_LDFLAGS="${AM_LDFLAGS} -Wl,-no-undefined"

# Debug logging
#AC_ARG_ENABLE([log], [AS_HELP_STRING([--enable-log], [enable logging (default y)])],
#	[log_enabled=$enableval],
#	[log_enabled='yes'])
#if test "x$log_enabled" != "xno"; then
#	AC_DEFINE([RUFUS_DEBUG], [1], [Debug logging])
#fi

# Debug symbols
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [keep debug symbols for gdb (default y)]),
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

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_OUTPUT
