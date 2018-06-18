#!/bin/sh
#
# Creates a tag according to the number of commits on this branch
#
# To have git run this script on commit, create a "post-commit" text file in
# .git/hooks/ with the following content:
# #!/bin/sh
# if [ -x ./_post-commit.sh ]; then
# 	. ./_post-commit.sh
# fi

type -P git &>/dev/null || { echo "git command not found. Aborting." >&2; exit 1; }

TAGVER=`cat ./.tag`
# Only apply a tag if we're dealing with the master branch
if [ "`git rev-parse --abbrev-ref HEAD`" == "master" ]; then
  git tag "b$TAGVER"
fi
rm ./.tag