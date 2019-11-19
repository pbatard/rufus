#!/bin/sh
#
# This script detects whether git commit is being executed in amend or regular mode
# Needed to determine whether the build number should be incremented or not.
#

# Need to figure out if we are running on Windows or *NIX
if [ "$(uname -o)" = "Msys" ]; then
  type -P PowerShell &>/dev/null || { echo "PowerShell command not found. Aborting." >&2; exit 1; }
  type -P grep &>/dev/null || { echo "grep command not found. Aborting." >&2; exit 1; }
  GITCMD=`PowerShell -command "Get-WmiObject win32_process -Filter \"name like '%git.exe'\" | select CommandLine"`
  if $(echo $GITCMD | grep -q -- --amend); then
    echo AMEND detected
    touch ./.amend
  fi
else
  if $(ps -fp $PPID | grep -q -- --amend); then
    echo AMEND detected
    touch ./.amend
  fi
fi
