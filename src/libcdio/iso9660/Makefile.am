#   $Id: Makefile.am,v 1.18 2008/10/20 01:25:15 rocky Exp $
#
#   Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008
#   Rocky Bernstein <rocky@gnu.org>
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.

########################################################
# Things to make the libiso9660 library
########################################################
#
# From libtool documentation amended with guidance from N. Boullis: 
#
#  1. Start with version information of `0:0:0' for each libtool library.
#
#  2. It is probably not a good idea to update the version information 
#     several times between public releases, but rather once per public
#     release. (This seems to be more an aesthetic consideration than
#     a hard technical one.)
#
#  3. If the library source code has changed at all since the last
#     update, then increment REVISION (`C:R:A' becomes `C:R+1:A').
#
#  4. If any interfaces have been added, removed, or changed since the
#     last update, increment CURRENT, and set REVISION to 0.
#
#  5. If any interfaces have been added since the last public release,
#     then increment AGE.
#
#  6. If any interfaces have been removed or changed since the last
#     public release, then set AGE to 0. A changed interface means an
#     incompatibility with previous versions.

libiso9660_la_CURRENT = 8
libiso9660_la_REVISION = 0
libiso9660_la_AGE = 0

EXTRA_DIST = libiso9660.sym

noinst_HEADERS = iso9660_private.h

lib_LTLIBRARIES = libiso9660.la

if ENABLE_ROCK
rock_src = rock.c
else
rock_src =
endif 

libiso9660_la_SOURCES = \
	iso9660.c \
	iso9660_private.h \
	iso9660_fs.c \
	$(rock_src) \
	xa.c

libiso9660_la_LIBADD = @LIBCDIO_LIBS@
libiso9660_la_ldflags = -version-info $(libiso9660_la_CURRENT):$(libiso9660_la_REVISION):$(libiso9660_la_AGE) @LT_NO_UNDEFINED@
libiso9660_la_dependencies = $(top_builddir)/lib/driver/libcdio.la

INCLUDES = $(LIBCDIO_CFLAGS)

########################################################
# Things to version the symbols in the libraries
########################################################

# An explanation of the versioning problem from Nicolas Boullis and
# the versioned symbol solution he uses below...
# 
# Currently, libvcdinfo uses the cdio_open function from libcdio.
# Let's imagine a program foobar that uses both the vcdinfo_open
# function from libvcdinfo and the cdio_open function from libcdio.

# Currently, libcdio has SONAME libcdio.so.0, libvcdinfo has SONAME
# libvcdinfo.so.0 and requires libcdio.so.0, and foobar requires both
# libvcdinfo.so.0 and libcdio.so.0. Everything looks fine.
#
# Now, for some reason, you decide to change the cdio_open function.
# That's your right, but you have to bump the CURRENT version and (if I
# understand it correctly, athough this is not that clear in libtool's
# documentation) set the AGE to 0. Anyway, this bumps the SONAME, which is
# sane since the interface changes incompatibly.

# Now, you have a new libcdio with SONAME libcdio.so.1. But libvcdinfo and
# foobar still require libcdio.so.0. Everything is still fine.

# Now, after some minor changes, the author of foobar recompiles foobar.
# Then, foobar now requires libvcdinfo.so.0 and libcdio.so.1. And foobar
# now segfaults...

# What is happening? When you run foobar, if brings both libvcdinfo.so.0
# and libcdio.so.1, but libvcdinfo.so.0 also brings libcdio.so.0. So you
# have both libcdio.so.0 and libcdio.so.1 that bring their symbols to the
# global namespace. Hence, you have to incompatible versions of the
# cdio_open function in the name space. When foobar calls cdio_open, it
# may choose the wrong function, and segfaults...

# With versioned symbols, the cdio_open function from libcdio.so.0 may be
# known as (something that looks like) cdio_open@@CDIO_0. An the cdio_open
# function from libcdio.so.1 as cdio_open@@CDIO_1. Both versions of
# libcdio would still be brought in by the most recent foobar, but foobar
# (and libvcdinfo) know which versioned function to use and then use the
# good one.


# This is some simple versioning where every symbol is versioned with
# something that looks like the SONAME of the library. More complex (and
# better) versioning is possible; it is for example what is used by glibc.
# But good complex versioning is something that requires much more
# work...


# The below is a impliments symbol versioning.  First of all, I
# compute MAJOR as CURENT - AGE; that is what is used within libtool
# (at least on GNU/Linux systems) for the number in the SONAME.  The
# nm command gives the list of symbols known in each of the object
# files that will be part of the shared library. And the sed command
# extracts from this list those symbols that will be shared. (This sed
# command comes from libtool.)

libiso9660_la_MAJOR = $(shell expr $(libiso9660_la_CURRENT) - $(libiso9660_la_AGE))
if BUILD_VERSIONED_LIBS
libiso9660_la_LDFLAGS = $(libiso9660_la_ldflags) -Wl,--version-script=libiso9660.la.ver
libiso9660_la_DEPENDENCIES = $(libcdio9660_la_dependencies) libiso9660.la.ver

libiso9660.la.ver: $(libiso9660_la_OBJECTS) $(srcdir)/libiso9660.sym
	echo 'ISO9660_$(libiso9660_la_MAJOR) {' > $@
	objs=`for obj in $(libiso9660_la_OBJECTS); do sed -ne "s/^pic_object='\(.*\)'$$/\1/p" $$obj; done`;
	if test -n "$$objs" ; then \
	  nm $${objs} | sed -n -e 's/^.*[ 	][ABCDGIRSTW][ABCDGIRSTW]*[ 	][ 	]*\([_A-Za-z][_A-Za-z0-9]*\)$$/\1/p' | sort -u | { first=true; while read symbol; do if grep -q "^$${symbol}\$$" $(srcdir)/libiso9660.sym; then if test $$first = true; then echo "  global:"; first=false; fi; echo "    $${symbol};"; fi; done; } >> $@; \
	  nm $${objs} | sed -n -e 's/^.*[ 	][ABCDGIRSTW][ABCDGIRSTW]*[ 	][ 	]*\([_A-Za-z][_A-Za-z0-9]*\)$$/\1/p' | sort -u | { first=true; while read symbol; do if grep -q "^$${symbol}\$$" $(srcdir)/libiso9660.sym; then :; else if test $$first = true; then echo "  local:"; first=false; fi; echo "    $${symbol};"; fi; done; } >> $@; \
	fi
	echo '};' >> $@

MOSTLYCLEANFILES = libiso9660.la.ver
else 
libiso9660_la_LDFLAGS = $(libiso9660_la_ldflags)
libiso9660_la_DEPENDENCIES = $(libcdio9660_la_dependencies)
endif
