#!/usr/bin/env bash
# .gitignore keeps game data, keys and local logs out (security queue H5), and ignores nothing already tracked.
set -u
cd "$(dirname "$0")/../.." || exit 2
fail=0
for p in disc.iso some/dir/Game.ISO main.elf memcards/Mcd001.ps2 keys/debug.keystore keys/release.jks \
         native/android/net.env logs/run.log x/build/out.o; do
    if git check-ignore -q --no-index "$p"; then echo "ok: ignored $p"; else echo "FAIL: not ignored $p"; fail=1; fi
done
for p in gate/evidence/1.2/new.log native/ob_server/src/net.cpp docs/ops/build.md; do
    if git check-ignore -q --no-index "$p"; then echo "FAIL: ignored $p"; fail=1; else echo "ok: kept $p"; fi
done
tracked=$(git ls-files -ci --exclude-standard)
[ -z "$tracked" ] && echo "ok: no tracked file is ignored" || { echo "FAIL: tracked files now ignored:"; echo "$tracked" | head; fail=1; }
exit $fail
