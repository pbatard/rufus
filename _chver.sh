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
s/^[ \t]*FILEVERSION[ \t]*.*,.*,\(.*\),\(.*\)/ FILEVERSION @@MAJOR@@,@@MINOR@@,\1,\2/
s/^[ \t]*PRODUCTVERSION[ \t]*.*,.*,\(.*\),\(.*\)/ PRODUCTVERSION @@MAJOR@@,@@MINOR@@,\1,\2/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*".*\..*\.\(.*\)"/\1VALUE "FileVersion", "@@MAJOR@@.@@MINOR@@.\2"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*".*\..*\.\(.*\)"/\1VALUE "ProductVersion", "@@MAJOR@@.@@MINOR@@.\2"/
s/^\(.*\)"Rufus .*\..*\.\(.*\)"\(.*\)/\1"Rufus @@MAJOR@@.@@MINOR@@.\2"\3/
_EOF

# First run sed to substitute our variable in the sed command file
sed -i -e "s/@@MAJOR@@/$MAJOR/g" -e "s/@@MINOR@@/$MINOR/g" cmd.sed
sed -i -f cmd.sed src/rufus.rc
sed -i 's/$/\r/' src/rufus.rc
rm cmd.sed
source ./bootstrap.sh
