#!/usr/bin/env bash
# Rebuild only the DEV9 adapter library in the net GS build tree (after a native/pcsx2_dev9 change).
source "$(dirname "$0")/net-env.sh"
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
vc cmake --build "$OB_PCSX2_GS_BUILD" --target ob_pcsx2_dev9 > "$OB_LOGS/dev9-lib.log" 2>&1
rc=$?
echo "dev9 lib build exit $rc"
[ $rc = 0 ] || grep -a -E "error" "$OB_LOGS/dev9-lib.log" | head -10
exit $rc
