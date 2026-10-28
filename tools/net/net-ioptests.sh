#!/usr/bin/env bash
# S2: the IOP tests of the PS2Recomp worktree C:\ob\net-up (the DEV9 device patch) in C:\ob\net-ioptests.
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
B=C:/ob/net-ioptests
mkdir -p C:/ob/net-logs
{ [ -f "$B/build.ninja" ] || vc cmake -S C:/ob/net-up/ps2xIOP -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_IOP_BUILD_TESTS=ON; } \
    > C:/ob/net-logs/ioptests-configure.log 2>&1 || { echo "configure failed"; tail -20 C:/ob/net-logs/ioptests-configure.log; exit 2; }
vc cmake --build "$B" -j 6 > C:/ob/net-logs/ioptests-build.log 2>&1 || { echo "build failed"; grep -a -E "error" C:/ob/net-logs/ioptests-build.log | head -30; exit 3; }
vc ctest --test-dir "$B" --output-on-failure > C:/ob/net-logs/ioptests.log 2>&1; rc=$?
grep -a -E "tests passed|FAIL|Failed|failed:|must " C:/ob/net-logs/ioptests.log | head -20
exit $rc
