#!/bin/sh
rm -f rufus*.exe
./configure --disable-debug
make clean
make release
