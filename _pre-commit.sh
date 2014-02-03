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
# adjust so that we match the github commit count
TAGVER=`expr $VER + 1`
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
s/^\(.*\)"Rufus \(.*\)\.\(.*\)"\(.*\)/\1"Rufus \2.@@TAGVER@@"\4/
_EOF

# First run sed to substitute our variable in the sed command file
sed -i -e "s/@@TAGVER@@/$TAGVER/g" cmd.sed

# Run sed to update the nano version
sed -i -f cmd.sed src/rufus.rc
# MinGW's sed has the bad habit of eating CRLFs - make sure we keep 'em
sed -i 's/$/\r/' src/rufus.rc
# NB: we need to run git add else the modified files may be ignored
git add src/rufus.rc

rm cmd.sed
