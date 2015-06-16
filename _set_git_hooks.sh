#!/bin/sh
# Sets the git hooks on a new git development system
if [ -e ".git/hooks/pre-commit" ] || [ -e ".git/hooks/post-commit" ] ; then
    echo 'pre-commit or post-commit git hook is already set, aborting.'
    exit
fi

echo 'Creating pre-commit git hook...'
echo '#!/bin/sh' > .git/hooks/pre-commit
echo 'if [ -x ./_pre-commit.sh ]; then' >> .git/hooks/pre-commit
echo '	source ./_pre-commit.sh' >> .git/hooks/pre-commit
echo 'fi' >> .git/hooks/pre-commit

echo 'Creating post-commit git hook...'
echo '#!/bin/sh' > .git/hooks/post-commit
echo 'if [ -x ./_post-commit.sh ]; then' >> .git/hooks/post-commit
echo '	source ./_post-commit.sh' >> .git/hooks/post-commit
echo 'fi' >> .git/hooks/post-commit
