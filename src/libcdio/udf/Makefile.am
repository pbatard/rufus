#   Copyright (C) 2003, 2004, 2006, 2008, 2011
#    Rocky Bernstein <rocky@gnu.org>
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
# Things to make the libudf library
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

libudf_la_CURRENT = 1
libudf_la_REVISION = 0
libudf_la_AGE = 0

EXTRA_DIST = libudf.sym

noinst_HEADERS = udf_fs.h udf_private.h

lib_LTLIBRARIES = libudf.la

libudf_la_SOURCES = udf.c udf_file.c udf_fs.c udf_time.c filemode.c

libudf_la_LIBADD = @LIBCDIO_LIBS@ @LT_NO_UNDEFINED@

INCLUDES = $(LIBCDIO_CFLAGS)
