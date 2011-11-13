#!/bin/sh
git branch | grep -q \*
if [ $? -eq 0 ]; then
  git symbolic-ref HEAD refs/heads/dummy
  echo "Switched to fake branch 'dummy'"
else
  git symbolic-ref HEAD refs/heads/master
  echo "Switched to branch 'master'"
fi
