#!/usr/bin/env bash
# Fold the remaining changes of C:\ob\net-up's hand-written trees into its last commit with the message in file $1.
set -e
MSG=$(cd "$(dirname "$1")" && pwd -W)/$(basename "$1")
cd /c/ob/net-up
git add -A ps2xIOP ps2xRuntime ps2xRecomp ps2xTest ps2xAnalyzer
git commit -q --amend -F "$MSG"
git log --oneline -1
git status --short
