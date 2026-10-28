#!/usr/bin/env bash
# Export the last $1 commits of C:\ob\net-up as numbered patches starting at $2 into upstream/ps2recomp.
set -e
OUT=$(cd "$(dirname "$0")/../.." && pwd -W)/upstream/ps2recomp
cd /c/ob/net-up
git format-patch -q -"$1" --start-number "$2" -o "$OUT"
ls "$OUT" | tail -"$1"
