#!/bin/sh
rm -f rufus*.exe
./configure --disable-debug "$@"
make -j4 clean
make -j4 release
