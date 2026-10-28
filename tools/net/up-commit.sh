#!/usr/bin/env bash
# Commit the PS2Recomp worktree C:\ob\net-up (branch net-online) with the message in file $1; prints the new commit.
set -e
MSG=$(cd "$(dirname "$1")" && pwd -W)/$(basename "$1")
cd /c/ob/net-up
git add -A ps2xIOP ps2xRuntime ps2xRecomp ps2xTest ps2xAnalyzer
git commit -q -F "$MSG"
git log --oneline -1
git status --short
