#!/usr/bin/env bash
# Fold the remaining changes of C:\ob\net-up's hand-written trees into its last commit (message kept).
set -e
cd /c/ob/net-up
git add -A ps2xIOP ps2xRuntime ps2xRecomp ps2xTest ps2xAnalyzer
git commit -q --amend --no-edit
git log --oneline -1
git status --short
