#!/bin/sh

# use glibtoolize if it is available
(glibtoolize --version) < /dev/null > /dev/null 2>&1 && LIBTOOLIZE=glibtoolize || LIBTOOLIZE=libtoolize

$LIBTOOLIZE --copy --force || exit 1
aclocal || exit 1
autoheader || exit 1
autoconf || exit 1
automake -a -c || exit 1
./configure --disable-debug $*
