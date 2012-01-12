#!/bin/sh
rm -f rufus*.exe
./configure --without-freedos --without-syslinux
make clean
make release -j2
./configure --with-freedos
# The only difference between FreeDOS and non FreeDOS is with the RC
# => instead of invoking 'make clean, just remove the RC object
rm src/rufus_rc.o
make release -j2
