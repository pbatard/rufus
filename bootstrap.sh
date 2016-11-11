#!/bin/sh
mkdir -p m4
aclocal || exit 1
automake -a -c || exit 1
autoconf || exit 1
rm -rf autom4te.cache
rm -d m4
