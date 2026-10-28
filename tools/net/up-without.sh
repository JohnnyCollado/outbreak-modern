#!/usr/bin/env bash
# Prove a test fails without a change: "save" copies the uncommitted source files of C:\ob\net-up (not tests) to
# C:\ob\net-logs\without and puts back the committed ones; "restore" copies them back.
set -e
cd /c/ob/net-up
S=/c/ob/net-logs/without
case "$1" in
save)
    rm -rf "$S"; mkdir -p "$S"
    git diff --name-only -- ps2xIOP/src ps2xRuntime ps2xRecomp > "$S/files.txt"
    while read -r f; do mkdir -p "$S/$(dirname "$f")"; cp "$f" "$S/$f"; done < "$S/files.txt"
    git checkout -- $(cat "$S/files.txt")
    echo "set aside:"; cat "$S/files.txt" ;;
restore)
    while read -r f; do cp "$S/$f" "$f"; done < "$S/files.txt"
    git status --short ;;
*) echo "usage: up-without.sh save|restore"; exit 2 ;;
esac
