AC_INIT([rufus], [3.5], [https://github.com/pbatard/rufus/issues], [rufus], [https://rufus.ie])
AM_INIT_AUTOMAKE([-Wno-portability foreign no-dist no-dependencies])
AC_CONFIG_SRCDIR([src/rufus.c])
AC_CONFIG_MACRO_DIR([m4])
AM_SILENT_RULES([yes])

AC_PREREQ([2.50])
AC_PROG_CC
# autoconf 2.61 doesn't have AC_PROG_AR, but 2.63 has it
AC_DEFUN([AC_PROG_AR], [AC_CHECK_TOOL(AR, ar, :)])
AC_PROG_AR
AC_PROG_RANLIB
AC_PROG_SED
AC_PATH_PROG(RM, rm, rm)
AC_CHECK_TOOL(STRIP, strip, strip)
AC_CHECK_TOOL(WINDRES, windres, windres)
AC_C_INLINE
AC_DEFINE([_GNU_SOURCE], [], [Use GNU extensions])

AM_CFLAGS="${AM_CFLAGS} -DWINVER=0x601 -D_WIN32_WINNT=0x601 -D_WIN32_IE=0x800"
# "-Wl,--nxcompat" to enable DEP (Data Execution Prevention)
AM_LDFLAGS="${AM_LDFLAGS} -Wl,-no-undefined -Wl,--nxcompat -Wl,--no-insert-timestamp"

# Debug symbols
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],
		[keep debug symbols for gdb (default=yes)])],
	[debug_enabled=$enableval],
	[debug_enabled='yes'])
if test "x$debug_enabled" != "xno" ; then
  CFLAGS="-g -O0"
else
  CFLAGS="-Os"
  LDFLAGS="-s"
fi

# Alpha/Beta/Test
AC_ARG_ENABLE([alpha],[AS_HELP_STRING([--enable-alpha], [build an ALPHA release (default=no)])], [alpha_enabled=$enableval], [alpha_enabled='no'])
if test "x$alpha_enabled" != "xno" ; then
  CFLAGS+=" -DALPHA"
  SUFFIX=_ALPHA
fi
AC_ARG_ENABLE([beta],[AS_HELP_STRING([--enable-beta], [build a BETA release (default=no)])], [beta_enabled=$enableval], [beta_enabled='no'])
if test "x$beta_enabled" != "xno" ; then
  CFLAGS+=" -DBETA"
  SUFFIX=_BETA
fi
AC_ARG_ENABLE([test],[AS_HELP_STRING([--enable-test=#], [build a TEST release (default=no)])], [test_enabled=$enableval], [test_enabled='no'])
if test "x$test_enabled" != "xno" ; then
  if test "x$test_enabled" == "xyes" ; then $enableval="" ; fi
  CFLAGS+=" -DTEST=$enableval"
  SUFFIX=_TEST$enableval
fi

AC_MSG_RESULT([enabling Large File Support (ISO support)])
AM_CFLAGS="$AM_CFLAGS -D_FILE_OFFSET_BITS=64 -D_OFF_T_ -D_off_t=off64_t -Doff_t=off64_t -Doff32_t=long"

# check for -Wno-pointer-sign compiler support (GCC >= 4)
saved_CFLAGS="${CFLAGS}"
CFLAGS="$CFLAGS -Wno-pointer-sign"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
	[nopointersign_cflags="-Wno-pointer-sign"], [nopointersign_cflags=""])
CFLAGS="${saved_CFLAGS}"

AM_CFLAGS="$AM_CFLAGS -DUNICODE -D_UNICODE -UNDEBUG -DCOBJMACROS -std=gnu99 -Wshadow -Wall -Wundef -Wunused -Wstrict-prototypes -Wno-restrict -Werror-implicit-function-declaration $nopointersign_cflags"

AC_SUBST([VISIBILITY_CFLAGS])
AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_LDFLAGS])
AC_SUBST([SUFFIX])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([src/bled/Makefile])
AC_CONFIG_FILES([src/libcdio/iso9660/Makefile])
AC_CONFIG_FILES([src/libcdio/udf/Makefile])
AC_CONFIG_FILES([src/libcdio/driver/Makefile])
AC_CONFIG_FILES([res/loc/Makefile])
AC_CONFIG_FILES([src/ms-sys/Makefile])
AC_CONFIG_FILES([src/syslinux/libfat/Makefile])
AC_CONFIG_FILES([src/syslinux/libinstaller/Makefile])
AC_CONFIG_FILES([src/syslinux/win/Makefile])
AC_OUTPUT
