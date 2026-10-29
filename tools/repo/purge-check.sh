#!/usr/bin/env bash
# Checks a repository after the history clean-up (docs/plans/2026-10-29-security-queue.md, R1).
# Usage: OLD_EMAIL=<personal address> purge-check.sh <repo> [<original repo>] (the address is never stored in the repo)
#   1. no path under game_files/ and no .exe anywhere in history;
#   2. no commit authored or committed with OLD_EMAIL;
#   with <original>: 3. every branch's tip has the original tip's tree minus the removed paths, and
#   4. the same number of commits.
# Prints one line per check ("ok" / "FAIL ..."); exit 0 only when all pass.
set -u
REPO=$1
ORIG=${2:-}
fail=0
say() { echo "$1"; case $1 in FAIL*) fail=1 ;; esac; }

bad=$(git -C "$REPO" rev-list --objects --all | grep -E ' game_files/| [^ ]*\.exe$|\.exe$' | head -5)
[ -z "$bad" ] && say "ok: no game_files/ or .exe in history" || say "FAIL: still in history: $(echo "$bad" | tr '\n' ' ')"

mails=$(git -C "$REPO" log --all --format='%ae%n%ce' | grep -c -F "${OLD_EMAIL:?set OLD_EMAIL}")
[ "$mails" = 0 ] && say "ok: no personal address in commits" || say "FAIL: $mails commits with the personal address"

if [ -n "$ORIG" ]; then
    for ref in $(git -C "$ORIG" for-each-ref --format='%(refname)' refs/heads refs/remotes | grep -v '/HEAD$'); do
        if ! git -C "$REPO" rev-parse -q --verify "$ref" > /dev/null; then
            say "FAIL: $ref missing"
            continue
        fi
        a=$(git -C "$ORIG" ls-tree -r "$ref" | grep -v -E $'\tgame_files/|\.exe$' | sort | md5sum)
        b=$(git -C "$REPO" ls-tree -r "$ref" | sort | md5sum)
        [ "$a" = "$b" ] && say "ok: $ref tree matches" || say "FAIL: $ref tree differs"
        na=$(git -C "$ORIG" rev-list --count "$ref")
        nb=$(git -C "$REPO" rev-list --count "$ref")
        [ "$na" = "$nb" ] && say "ok: $ref $na commits" || say "FAIL: $ref $nb commits, was $na"
    done
fi
exit $fail
