#!/usr/bin/env bash
# Step A: configure (once), build and run native/ob_server's tests in C:\ob\net-ob-server.
REPO=$(cd "$(dirname "$0")/../.." && pwd -W)
VC="$REPO/tools/ob/vc.bat"
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }
B=C:/ob/net-ob-server
L=C:/ob/net-logs
{ [ -f "$B/build.ninja" ] || vc cmake -S "$REPO/native/ob_server" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release; } \
    > "$L/ob-server-configure.log" 2>&1 || { echo "configure failed"; tail -15 "$L/ob-server-configure.log"; exit 2; }
vc cmake --build "$B" --target ob_server_tests > "$L/ob-server-build.log" 2>&1 || { echo "build failed"; grep -a -E "error|warning C" "$L/ob-server-build.log" | head -20; exit 3; }
grep -a -c "warning C" "$L/ob-server-build.log" | sed 's/^/compiler warnings: /'
"$B/ob_server_tests.exe"
