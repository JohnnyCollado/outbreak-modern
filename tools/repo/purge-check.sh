#!/usr/bin/env bash
# Checks a repository's whole history (docs/plans/2026-10-30-security-queue-2.md, P1-P2).
# Usage: OLD_EMAIL=<address> [OLD_ADDRESS=<ip>] purge-check.sh <repo> [<original repo>] (values are never stored here)
#   1. no path under game_files/, .claude-state/, tools/__pycache__/ and no .exe anywhere in history;
#   2. no commit authored or committed with OLD_EMAIL;
#   3. neither value in any version of any file or in any commit message;
#   with <original>: 4. every branch's tip has the original tip's files, ignoring the removed paths and the replaced
#   values' lines, and 5. the same number of commits.
# Prints one line per check ("ok" / "FAIL ..."); exit 0 only when all pass. A failing git is a failure.
set -u
REPO=$1
ORIG=${2:-}
: "${OLD_EMAIL:?set OLD_EMAIL}"
fail=0
say() { echo "$1"; case $1 in FAIL*) fail=1 ;; esac; }

git -C "$REPO" rev-parse --git-dir > /dev/null 2>&1 || { echo "FAIL: $REPO is not a git repository"; exit 2; }
objects=$(git -C "$REPO" rev-list --objects --all) || { echo "FAIL: cannot list history"; exit 2; }
[ -n "$objects" ] || { echo "FAIL: $REPO has no history"; exit 2; }
bad=$(echo "$objects" | grep -a -E ' (game_files|\.claude-state|tools/__pycache__)(/|$)|\.exe$' | head -5)
[ -z "$bad" ] && say "ok: no removed paths or .exe in history" || say "FAIL: still in history: $(echo "$bad" | tr '\n' ' ')"

idents=$(git -C "$REPO" log --all --format='%ae%n%ce') || { echo "FAIL: cannot read commits"; exit 2; }
n=$(echo "$idents" | grep -c -F "$OLD_EMAIL")
[ "$n" = 0 ] && say "ok: no personal address in commit identities" || say "FAIL: $n commit identities with the personal address"

for value in "$OLD_EMAIL" ${OLD_ADDRESS:-}; do
    hits=$(git -C "$REPO" log --all --format=%h -S"$value") || { echo "FAIL: cannot search contents"; exit 2; }
    msgs=$(git -C "$REPO" log --all --format=%h -F --grep="$value") || { echo "FAIL: cannot search messages"; exit 2; }
    if [ -z "$hits$msgs" ]; then say "ok: a searched value is in no file version or message"
    else say "FAIL: a searched value is in $(echo "$hits" | grep -c .) commits' files, $(echo "$msgs" | grep -c .) messages"; fi
done

if [ -n "$ORIG" ]; then
    for ref in $(git -C "$ORIG" for-each-ref --format='%(refname)' refs/heads refs/remotes | grep -v '/HEAD$'); do
        if ! git -C "$REPO" rev-parse -q --verify "$ref" > /dev/null; then
            say "FAIL: $ref missing"
            continue
        fi
        a=$(git -C "$ORIG" ls-tree -r --name-only "$ref" | grep -v -E '^(game_files|\.claude-state|tools/__pycache__)/|\.exe$' | sort | md5sum)
        b=$(git -C "$REPO" ls-tree -r --name-only "$ref" | sort | md5sum)
        [ "$a" = "$b" ] && say "ok: $ref same file list" || say "FAIL: $ref file list differs"
        na=$(git -C "$ORIG" rev-list --count "$ref")
        nb=$(git -C "$REPO" rev-list --count "$ref")
        [ "$na" = "$nb" ] && say "ok: $ref $na commits" || say "FAIL: $ref $nb commits, was $na"
    done
fi
exit $fail
