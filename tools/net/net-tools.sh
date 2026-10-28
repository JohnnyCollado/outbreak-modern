#!/usr/bin/env bash
# Tools tree for the net work: analyzer, recompiler and ps2x_tests from C:\ob\net-up into C:\ob\net-t, reusing
# C:\ob\t's FetchContent sources (no downloads). Then runs ps2x_tests from net-up's ps2xTest.
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
B=C:/ob/net-t
L=C:/ob/net-logs
start=$(date +%s)
if [ ! -f "$B/build.ninja" ]; then
    vc cmake -S C:/ob/net-up -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_FFMPEG=OFF \
        $( [ -d C:/ob/t/_deps ] && echo "-DFETCHCONTENT_BASE_DIR=C:/ob/t/_deps -DFETCHCONTENT_FULLY_DISCONNECTED=ON" ) > "$L/net-t-configure.log" 2>&1 ||
        { echo "configure failed"; tail -20 "$L/net-t-configure.log"; exit 2; }
fi
vc cmake --build "$B" -j 14 --target ps2_analyzer ps2_recomp ps2x_tests > "$L/net-t-build.log" 2>&1 ||
    { echo "build failed"; grep -a -E " error |error C|LNK" "$L/net-t-build.log" | head -20; exit 3; }
echo "tools + tests built in $(( $(date +%s) - start )) s"
exe=$(find "$B" -name ps2x_tests.exe | head -1)
(cd C:/ob/net-up/ps2xTest && vc "$(cygpath -w "$exe")") > "$L/net-ps2x.log" 2>&1
grep -a -E "^(Passed|Total Tests|Failed):" "$L/net-ps2x.log" | tail -3
grep -a "Failed\]" -A2 "$L/net-ps2x.log" | head -20
