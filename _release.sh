#!/bin/sh
./configure --disable-debug "$@"
make -j12 clean
make -j12 release
