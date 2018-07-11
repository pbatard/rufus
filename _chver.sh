#!/bin/sh
# Changes the version number
# !!!THIS SCRIPT IS FOR INTERNAL DEVELOPER USE ONLY!!!

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }

if [ ! -n "$1" ]; then
  echo "you must provide a version number (eg. 2.1)"
  exit 1
else
  MAJOR=`echo $1 | sed "s/\(.*\)[.].*/\1/"`
  MINOR=`echo $1 | sed "s/.*[.]\(.*\)/\1/"`
fi
case $MAJOR in *[!0-9]*) 
  echo "$MAJOR is not a number"
  exit 1
esac
case $MINOR in *[!0-9]*) 
  echo "$MINOR is not a number"
  exit 1
esac
echo "changing version to $MAJOR.$MINOR"
sed -i -e "s/^AC_INIT(\[\([^ ]*\)\], \[[^ ]*\]\(.*\)/AC_INIT([\1], [$MAJOR.$MINOR]\2/" configure.ac
cat > cmd.sed <<\_EOF
s/^\([ \t]*\)\(FILE\|PRODUCT\)VERSION\([ \t]*\)[0-9]*,[0-9]*\(.*\)/\1\2VERSION\3@@MAJOR@@,@@MINOR@@\4/
s/^\([ \t]*\)VALUE\([ \t]*\)"\(File\|Product\)Version",\([ \t]*\)"[0-9]*\.[0-9]*\.\(.*\)/\1VALUE\2"\3Version",\4"@@MAJOR@@.@@MINOR@@.\5/
s/^\([ \t]*\)VALUE\([ \t]*\)"OriginalFilename",\([ \t]*\)"rufus-[0-9]*\.[0-9]*\.exe\(.*\)/\1VALUE\2"OriginalFilename",\3"rufus-@@MAJOR@@.@@MINOR@@.exe\4/
s/^\(.*\)"Rufus [0-9]*\.[0-9]*\.\(.*\)"\(.*\)/\1"Rufus @@MAJOR@@.@@MINOR@@.\2"\3/
s/^\([ \t]*\)Version="[0-9]*\.[0-9]*\.\(.*\)"\(.*\)/\1Version="@@MAJOR@@.@@MINOR@@.\2"\3/
s/^set VERSION=[0-9]*\.[0-9]*/set VERSION=@@MAJOR@@.@@MINOR@@/
_EOF

# First run sed to substitute our variable in the sed command file
sed -i -e "s/@@MAJOR@@/$MAJOR/g" -e "s/@@MINOR@@/$MINOR/g" cmd.sed
sed -b -i -f cmd.sed src/rufus.rc
sed -b -i -f cmd.sed res/appstore/AppxManifest.xml
sed -b -i -f cmd.sed res/appstore/packme.cmd
rm cmd.sed
source ./bootstrap.sh
