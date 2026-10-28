#!/usr/bin/env bash
# Runner-only rebuild in the net trees (after a runtime/IOP or native/ change; generated game code unchanged).
source "$(dirname "$0")/net-env.sh"
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
start=$(date +%s)
vc cmake --build "$OB_R" -j 14 --target ps2EntryRunner > "$OB_LOGS/net-runner.log" 2>&1
rc=$?
echo "runner build exit $rc in $(( $(date +%s) - start )) s; game units rebuilt: $(grep -cE 'Unity[/\\]' "$OB_LOGS/net-runner.log")"
[ $rc = 0 ] || grep -a -E "error|unresolved" "$OB_LOGS/net-runner.log" | head -30
exit $rc
