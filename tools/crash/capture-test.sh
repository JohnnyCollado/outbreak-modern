#!/usr/bin/env bash
# The release's crash capture, end to end, without the game: a small program ends each way a release can, and each must
# leave its printed lines, a [crash] line and a dump. PS2X files stay out of it: only this probe's own logs folder is used.
cd "$(dirname "$0")/../.." || exit 1
source tools/net/net-env.sh
VC="$(pwd -W)/tools/ob/vc.bat"
VCW="${VC//\//\\}"
vc() { MSYS_NO_PATHCONV=1 cmd /c "$VCW" "$@"; }
vc cmake --build "$OB_PCSX2_GS_BUILD" --target ob_crash_capture_probe > "$OB_LOGS/crash-probe-build.log" 2>&1 ||
    { echo "build failed"; tail -8 "$OB_LOGS/crash-probe-build.log" | cut -c1-170; exit 2; }
grep -a "warning" "$OB_LOGS/crash-probe-build.log" | head -5 | cut -c1-170
P="$OB_PCSX2_GS_BUILD/ob_ui"
exe="$P/ob_crash_capture_probe.exe"
red=0
check() { if "${@:2}"; then echo "ok: $1"; else echo "FAIL: $1"; red=1; fi; }
has() { grep -a -q "$1" "$2"; }
for how in exit abort throw fault; do
    rm -rf "$P/logs"
    "$exe" "$how" > /dev/null 2>&1
    log=$(ls "$P"/logs/outbreak-*.log 2>/dev/null | head -1)
    check "$how: a log" test -n "$log"
    [ -n "$log" ] || continue
    check "$how: stdout and stderr in it" has "printed on stdout" "$log"
    check "$how: cerr in it" has "printed on cerr" "$log"
    check "$how: the build id" has "^\[build\] " "$log"
    dmp=${log%.log}.dmp
    if [ "$how" = exit ]; then
        check "exit: no dump" test ! -e "$dmp"
    else
        check "$how: a [crash] line" has "^\[crash\]" "$log"
        check "$how: a dump" test -s "$dmp"
    fi
    [ "$how" != throw ] || check "throw: the terminate message" has "\[terminate\]" "$log"
done
rm -rf "$P/logs"
"$exe" abort > /dev/null 2>&1
sleep 1
"$exe" report > /dev/null 2>&1
zip=$(ls "$P"/logs/crash-report-*.zip 2>/dev/null | head -1)
check "report: a zip" test -s "$zip"
if [ -s "$zip" ]; then
    # Windows' own zip reader must accept it, with the earlier crash's log and dump inside.
    list=$(python -I -c "import sys,zipfile; z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None; print(' '.join(z.namelist()))" "$(cygpath -w "$zip")")
    echo "  $list"
    case "$list" in *.dmp*) echo "ok: report holds the dump" ;; *) echo "FAIL: report holds the dump"; red=1 ;; esac
    case "$list" in *.log*) echo "ok: report holds the log" ;; *) echo "FAIL: report holds the log"; red=1 ;; esac
fi
rm -rf "$P/logs"
exit $red
