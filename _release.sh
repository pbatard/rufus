#!/bin/sh
rm -f rufus*.exe
./configure --disable-debug "$@"
make -j12 clean
make -j12 release

# Update AppxManifest.xml with the build number
TAGVER=`git log --oneline | wc -l`
cat > cmd.sed <<\_EOF
s/^\([ \t]*\)Version="\([0-9]*\)\.\([0-9]*\)\.[0-9]*\.\([0-9]*\)"\(.*\)/\1Version="\2.\3.@@TAGVER@@.\4"\5/
_EOF
sed -i -e "s/@@TAGVER@@/$TAGVER/g" cmd.sed
sed -b -i -f cmd.sed res/appstore/AppxManifest.xml
rm cmd.sed
