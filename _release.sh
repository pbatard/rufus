#!/bin/sh
rm -f rufus*.exe
./configure
make clean
make release
