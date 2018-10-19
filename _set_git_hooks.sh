#!/bin/sh
# Sets the git hooks on a new git development system
if [ -e ".git/hooks/pre-commit" ]; then
    echo 'pre-commit git hook is already set, aborting.'
    exit
fi

echo 'Creating pre-commit git hook...'
echo '#!/bin/sh' > .git/hooks/pre-commit
echo 'if [ -x ./_pre-commit.sh ]; then' >> .git/hooks/pre-commit
echo '	. ./_pre-commit.sh' >> .git/hooks/pre-commit
echo 'fi' >> .git/hooks/pre-commit
