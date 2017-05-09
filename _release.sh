#!/bin/sh
rm -f rufus*.exe
./configure --disable-debug "$@"
make -j12 clean
make -j12 release
