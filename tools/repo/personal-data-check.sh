#!/usr/bin/env bash
# Tracked files hold no personal contact data (security queue H4): no Tailscale address (100.64.0.0/10, the
# test's deliberate 100.64.0.1 aside) and no personal mail address. Exit 0 when clean.
set -u
cd "$(dirname "$0")/../.." || exit 2
fail=0
tail=$(git grep -n -E '\b100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7])\.[0-9]+\.[0-9]+' -- . ':!tools/repo/personal-data-check.sh' | grep -v '100\.64\.0\.1"')
[ -z "$tail" ] && echo "ok: no Tailscale address" || { echo "FAIL: Tailscale address in:"; echo "$tail" | cut -c1-120; fail=1; }
mail=$(git grep -n -i -E '[a-z0-9._%+-]+@gmail\.com' -- . ':!tools/repo/personal-data-check.sh')
[ -z "$mail" ] && echo "ok: no personal mail address" || { echo "FAIL: personal mail address in $(echo "$mail" | cut -d: -f1 | sort -u | wc -l) file(s)"; fail=1; }
exit $fail
