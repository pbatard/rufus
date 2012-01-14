#/bin/sh
gcc -g -Wall -DISO_TEST -Isrc/libcdio -oiso.exe src/iso.c src/libcdio/udf/libudf.a src/libcdio/iso9660/libiso9660.a src/libcdio/driver/libdriver.a