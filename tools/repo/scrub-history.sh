#!/usr/bin/env bash
# Builds a scrubbed mirror of the repository (docs/plans/2026-10-30-security-queue-2.md, P1). The working repository
# is never changed; publishing the copy is a separate force push by the developer.
# Usage: OLD_EMAIL=.. NEW_EMAIL=.. OLD_ADDRESS=.. scrub-history.sh <repo> <new mirror dir>; then purge-check.sh.
set -euo pipefail
: "${OLD_EMAIL:?}" "${NEW_EMAIL:?}"
export OLD_EMAIL NEW_EMAIL OLD_ADDRESS="${OLD_ADDRESS:-}" NEW_ADDRESS="${NEW_ADDRESS:-<odin-ip>}"
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$1
DST=$2
[ -e "$DST" ] && { echo "$DST exists: remove it first"; exit 2; }
git init -q --bare "$DST"
git -C "$SRC" fast-export --all --signed-tags=strip | python -I "$HERE/scrub-history.py" | git -C "$DST" fast-import --quiet --force
git -C "$DST" gc -q --prune=now --aggressive
git -C "$DST" count-objects -vH | grep size-pack
