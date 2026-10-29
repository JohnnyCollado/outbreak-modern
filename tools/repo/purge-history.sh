#!/usr/bin/env bash
# Builds a cleaned copy of the repository's history (docs/plans/2026-10-29-security-queue.md, R1): a mirror clone at
# $2 without game_files/ and the third-party .exe, and with the personal address replaced by the noreply one. The
# working repository is never changed; publishing the copy (a force push) is a separate step for the developer.
# Usage: OLD_EMAIL=<personal address> purge-history.sh <repo> <mirror dir>; then purge-check.sh <mirror dir> <repo>.
# The address is passed in, never stored in the repo.
set -eu
: "${OLD_EMAIL:?set OLD_EMAIL to the address to replace}"
export OLD_EMAIL
SRC=$1
DST=$2
[ -e "$DST" ] && { echo "$DST exists: remove it first"; exit 2; }
git clone -q --mirror "$SRC" "$DST"
cd "$DST"
# filter-branch ships with git (git-filter-repo is not installed). Index filter: no checkout, fast.
FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f \
    --index-filter 'git rm -r -q --cached --ignore-unmatch -- game_files "Outbreak-ModernCam-v1.2.0 (1).exe"' \
    --env-filter '
        if [ "$GIT_AUTHOR_EMAIL" = "$OLD_EMAIL" ]; then
            GIT_AUTHOR_EMAIL="54557697+JohnnyC96@users.noreply.github.com"; export GIT_AUTHOR_EMAIL; fi
        if [ "$GIT_COMMITTER_EMAIL" = "$OLD_EMAIL" ]; then
            GIT_COMMITTER_EMAIL="54557697+JohnnyC96@users.noreply.github.com"; export GIT_COMMITTER_EMAIL; fi' \
    --tag-name-filter cat -- --all
# Drop the backup refs and the old objects so the copy holds only the clean history.
git for-each-ref --format='%(refname)' refs/original | xargs -r -n 1 git update-ref -d
git reflog expire --expire=now --all
git gc -q --prune=now --aggressive
git count-objects -vH | grep size-pack
