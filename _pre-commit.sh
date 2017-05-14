#!/bin/sh
#
# Bumps the micro version according to the number of commits on this branch
#
# To have git run this script on commit, create a "pre-commit" text file in
# .git/hooks/ with the following content:
# #!/bin/sh
# if [ -x ./_pre-commit.sh ]; then
# 	source ./_pre-commit.sh
# fi

type -P sed &>/dev/null || { echo "sed command not found. Aborting." >&2; exit 1; }
type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

TAGVER=`git rev-list HEAD --count`
# adjust so that we match the github commit count
((TAGVER++))
# there may be a better way to prevent improper micro on amend. For now the detection
# of a .amend file in the current directory will do
if [ -f ./.amend ]; then
	((TAGVER--))
	git tag -d "b$TAGVER"
	rm ./.amend;
fi
echo "setting micro to $TAGVER"
echo $TAGVER > .tag

cat > cmd.sed <<\_EOF
s/^\([ \t]*\)*\(FILE\|PRODUCT\)VERSION\([ \t]*\)\([0-9]*\),\([0-9]*\),[0-9]*,\(.*\)/\1\2VERSION\3\4,\5,@@TAGVER@@,\6/
s/^\([ \t]*\)VALUE\([ \t]*\)"\(File\|Product\)Version",\([ \t]*\)"\(.*\)\..*"[ \t]*/\1VALUE\2"\3Version",\4"\5.@@TAGVER@@"/
s/^\(.*\)"Rufus \(.*\)\..*"\(.*\)/\1"Rufus \2.@@TAGVER@@"\3/
s/^\([ \t]*\)Version="\([0-9]*\)\.\([0-9]*\)\.[0-9]*\.\([0-9]*\)"\(.*\)/\1Version="\2.\3.@@TAGVER@@.\4"\5/
_EOF

# First run sed to substitute our variable in the sed command file
sed -i -e "s/@@TAGVER@@/$TAGVER/g" cmd.sed
# Run sed to update the nano version
sed -b -i -f cmd.sed src/rufus.rc
# NB: we need to run git add else the modified files may be ignored
git add src/rufus.rc

rm cmd.sed
