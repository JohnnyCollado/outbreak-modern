#!/usr/bin/env bash
# S1: configure (once) native/pcsx2_gs into the net GS build tree, build and run ob_pcsx2_dev9_tests.
source "$(dirname "$0")/net-env.sh"
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
B=$OB_PCSX2_GS_BUILD
mkdir -p "$B" "$OB_LOGS"
start=$(date +%s)
if [ ! -f "$B/build.ninja" ]; then
    vc cmake -S "$REPO/native/pcsx2_gs" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOB_PCSX2=C:/ob/pcsx2 \
        -DOB_PCSX2_DEPS=C:/ob/pcsx2-deps > "$OB_LOGS/dev9-configure.log" 2>&1 || { echo "configure failed"; tail -20 "$OB_LOGS/dev9-configure.log"; exit 2; }
fi
vc cmake --build "$B" -j 14 --target ob_pcsx2_dev9_tests > "$OB_LOGS/dev9-build.log" 2>&1
rc=$?
echo "build rc=$rc in $(( $(date +%s) - start )) s"
if [ $rc != 0 ]; then grep -a -E "error|unresolved" "$OB_LOGS/dev9-build.log" | head -40; exit 3; fi
(cd "$B" && ./pcsx2_dev9/ob_pcsx2_dev9_tests.exe)
