#!/bin/sh
#
# Bumps the nano version according to the number of commits on this branch
#
# To have git run this script on commit, create a "pre-commit" text file in
# .git/hooks/ with the following content:
# #!/bin/sh
# if [ -x ./_pre-commit.sh ]; then
# 	source ./_pre-commit.sh
# fi

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

VER=`git log --oneline | wc -l`
# trim spaces
TAGVER=`echo $VER`
# there may be a better way to prevent improper nano on amend. For now the detection
# of a .amend file in the current directory will do
if [ -f ./.amend ]; then
	TAGVER=`expr $TAGVER - 1`
	git tag -d "b$TAGVER"
	rm ./.amend;
fi
echo "setting nano to $TAGVER"
echo $TAGVER > .tag

cat > cmd.sed <<\_EOF
s/^[ \t]*FILEVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ FILEVERSION \1,\2,\3,@@TAGVER@@/
s/^[ \t]*PRODUCTVERSION[ \t]*\(.*\),\(.*\),\(.*\),.*/ PRODUCTVERSION \1,\2,\3,@@TAGVER@@/
s/^\([ \t]*\)VALUE[ \t]*"FileVersion",[ \t]*"\(.*\)\..*"/\1VALUE "FileVersion", "\2.@@TAGVER@@"/
s/^\([ \t]*\)VALUE[ \t]*"ProductVersion",[ \t]*"\(.*\)\..*"/\1VALUE "ProductVersion", "\2.@@TAGVER@@"/
s/^\(.*\)"Rufus v\(.*\)\.\(.*\)"\(.*\)/\1"Rufus v\2.@@TAGVER@@"\4/
_EOF

# First run sed to substitute our variable in the sed command file
sed -e "s/@@TAGVER@@/$TAGVER/g" cmd.sed > cmd.sed~
mv cmd.sed~ cmd.sed

# Run sed to update the nano version
# NB: we need to run git add else the modified files may be ignored
sed -f cmd.sed src/rufus.rc > src/rufus.rc~
# MinGW's sed has the bad habit of eating CRLFs - make sure we keep 'em
sed 's/$/\r/' src/rufus.rc~ > src/rufus.rc
rm src/rufus.rc~
git add src/rufus.rc

rm cmd.sed
