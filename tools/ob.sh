#!/bin/bash
# Windows (Git Bash) build/test/recompile/boot driver for PS2Recomp + Outbreak; Windows form of
# tools/recomp_cycle.sh. Reproduces the C:\ob scripts (build-tools.bat, tests.bat, ioptests.bat,
# recomp-cycle.sh, ovl-cycle.sh, build-runner.bat, boot-frames-26.ps1).
#
#   tools/ob.sh tools                        configure + build the tools tree ($OB_T)
#   tools/ob.sh test                         ps2x_tests, IOP tests, gs_cache tests: "<suite>: <pass>/<total>"
#   tools/ob.sh recompile                    main ELF + overlays demo submain netdnas, then the runner
#   tools/ob.sh boot [--secs N] [--tag T]    boot the runner (opens a window), kill after N s (200)
#
# Env (defaults): OB_ROOT=C:/ob, OB_SRC, OB_T, OB_R, OB_WORK, OB_OVL, OB_LOGS, OB_SPLAT under it;
# OB_RUN=<repo>/game_files (read only); OB_VCVARS=VS2022 Community vcvars64.bat.
# Writes nothing inside the repo; logs go to $OB_LOGS. cmake/ctest/MSVC steps run via tools/ob/vc.bat
# (its path must contain no spaces: cmd /c quoting).
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd -W) || exit 1
: "${OB_ROOT:=C:/ob}"
: "${OB_SRC:=$OB_ROOT/src}" "${OB_T:=$OB_ROOT/t}" "${OB_R:=$OB_ROOT/r}" "${OB_WORK:=$OB_ROOT/work}"
: "${OB_OVL:=$OB_ROOT/ovl}" "${OB_LOGS:=$OB_ROOT/logs}" "${OB_SPLAT:=$OB_ROOT/splat/SLPM_654.28}"
: "${OB_RUN:=$REPO/game_files}"
: "${OB_VCVARS:=C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat}"
export OB_VCVARS
PY=$(sed -n 's/.*"python": *"\([^"]*\)".*/\1/p' "$REPO/.claude/pa.json")
VC="$REPO/tools/ob/vc.bat"
JOBS=14
mkdir -p "$OB_LOGS"

# vc <cmd> [args...]: run inside vcvars; no MSYS path conversion (args are Windows-form paths).
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }

cmd_tools() {
    local log="$OB_LOGS/ob-tools.log"
    { vc cmake -S "$OB_SRC" -B "$OB_T" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_FFMPEG=OFF &&
      vc cmake --build "$OB_T" -j $JOBS; } > "$log" 2>&1
    local rc=$?
    tail -5 "$log"; echo "tools build exit $rc ($log)"; return $rc
}

# ctest_count <log>: "<pass>/<total>" from ctest's "N% tests passed, F tests failed out of T".
ctest_count() {
    local line; line=$(grep -E 'tests failed out of [0-9]+' "$1" | tail -1)
    [ -n "$line" ] || { echo "0/0"; return; }
    local f t; f=$(echo "$line" | sed -E 's/.* ([0-9]+) tests failed out of ([0-9]+).*/\1/')
    t=$(echo "$line" | sed -E 's/.*out of ([0-9]+).*/\1/')
    echo "$((t - f))/$t"
}

cmd_test() {
    local red=0 log exe p t
    # ps2x_tests (tests.bat): build t with -k 0, then run from ps2xTest's source dir.
    vc cmake --build "$OB_T" -j $JOBS -- -k 0 > "$OB_LOGS/ob-test-build.log" 2>&1 || echo "build t: errors (ob-test-build.log)"
    log="$OB_LOGS/ob-test-ps2x.log"
    exe=$(find "$OB_T" -name ps2x_tests.exe | head -1)
    if [ -n "$exe" ]; then
        (cd "$OB_SRC/ps2xTest" && vc "$(cygpath -w "$exe")") > "$log" 2>&1
        p=$(sed -n 's/^Passed: \([0-9]*\).*/\1/p' "$log" | tail -1); t=$(sed -n 's/^Total Tests: \([0-9]*\).*/\1/p' "$log" | tail -1)
        echo "ps2x_tests: ${p:-0}/${t:-0}"
        [ -n "$t" ] && [ "$p" = "$t" ] && [ "$t" -gt 0 ] || { red=1; grep -a 'Failed\]' "$log" | head -20; }
    else
        echo "ps2x_tests: 0/0 (ps2x_tests.exe not built)"; red=1
    fi
    # IOP tests (ioptests.bat).
    log="$OB_LOGS/ob-test-iop.log"
    {
        { [ -f "$OB_ROOT/ioptests/build.ninja" ] ||
            vc cmake -S "$OB_SRC/ps2xIOP" -B "$OB_ROOT/ioptests" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_IOP_BUILD_TESTS=ON; } &&
        vc cmake --build "$OB_ROOT/ioptests" -j $JOBS && vc ctest --test-dir "$OB_ROOT/ioptests" --output-on-failure
    } > "$log" 2>&1 || red=1
    echo "iop: $(ctest_count "$log")"
    # gs_cache tests.
    log="$OB_LOGS/ob-test-gs_cache.log"
    vc ctest --test-dir "$OB_T/ps2xTest/gs_cache" --output-on-failure > "$log" 2>&1 || red=1
    echo "gs_cache: $(ctest_count "$log")"
    [ $red = 0 ] || echo "red suite(s): see $OB_LOGS/ob-test-*.log"
    return $red
}

cmd_recompile() {
    local bad=0 o up asm e
    # Main ELF (recomp-cycle.sh).
    cd "$OB_WORK" || return 1
    "$OB_T/ps2xAnalyzer/ps2_analyzer.exe" SLPM_654.28 analyzed.toml > analyzer.log 2>&1 || { echo "analyzer failed"; return 1; }
    sed -e 's|^ghidra_output = ""|ghidra_output = "main-functions.csv"|' analyzed.toml > decomp.toml
    grep -c "instructions = \[" decomp.toml | sed 's/^/instruction patch blocks: /'
    rm -rf output
    "$OB_T/ps2xRecomp/ps2_recomp.exe" decomp.toml > recomp.log 2>&1 || { echo "recomp failed"; return 1; }
    e=$(grep -c '\[error\]' recomp.log)
    echo "main: errors=$e warnings=$(grep -c '\[warn' recomp.log) files=$(ls output | wc -l)"
    [ "$e" = 0 ] || bad=1
    grep -c "scePad2\|sceDbc" decomp.toml | sed 's/^/pad2 stubs in config: /'
    cd "$OB_SRC" || return 1
    mkdir -p ps2xRuntime/src/runner
    rm -f ps2xRuntime/src/runner/*.cpp
    cp "$OB_WORK"/output/*.cpp ps2xRuntime/src/runner/
    cp "$OB_WORK"/output/ps2_recompiled_functions.h "$OB_WORK"/output/ps2_recompiled_stubs.h ps2xRuntime/include/
    echo "copied $(ls ps2xRuntime/src/runner/*.cpp | wc -l) sources"
    # Overlays (ovl-cycle.sh demo submain netdnas).
    cd "$OB_OVL" || return 1
    for o in demo submain netdnas; do
        up=$(echo "$o" | tr a-z A-Z)
        asm="$OB_SPLAT/en/$o"; [ -d "$asm" ] || asm="$OB_SPLAT/$o"
        "$PY" -I "$REPO/tools/mwo3_to_elf.py" "$OB_RUN/PROG/$up.BIN" "$o.elf" || return 1
        "$PY" -I "$REPO/tools/splat_functions.py" "$asm" "$o-functions.csv" || return 1
        sed -e "s/demo/$o/g" demo.toml > "$o.gen.toml"
        rm -rf "output-$o"
        "$OB_T/ps2xRecomp/ps2_recomp.exe" "$o.gen.toml" > "recomp-$o.log" 2>&1 || { echo "$o: recomp failed (recomp-$o.log)"; tail -5 "recomp-$o.log"; return 1; }
        e=$(grep -c '\[error\]' "recomp-$o.log")
        echo "$o: errors=$e warnings=$(grep -c '\[warn' "recomp-$o.log") files=$(ls "output-$o" | wc -l)"
        [ "$e" = 0 ] || bad=1
        cp "output-$o"/*.cpp "$OB_SRC/ps2xRuntime/src/runner/"
        cp "output-$o/${o}_recompiled_functions.h" "output-$o/${o}_recompiled_stubs.h" "$OB_SRC/ps2xRuntime/include/"
    done
    # Runner (build-runner.bat).
    local log="$OB_LOGS/ob-runner-build.log" start rc
    start=$(date +%s)
    {
        { [ -f "$OB_R/build.ninja" ] ||
            vc cmake -S "$OB_SRC" -B "$OB_R" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O2 /Zi /DNDEBUG" -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_RECOMP=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF "-DFETCHCONTENT_BASE_DIR=$OB_T/_deps"; } &&
        vc cmake --build "$OB_R" -j $JOBS --target ps2EntryRunner
    } > "$log" 2>&1
    rc=$?
    echo "runner build exit $rc in $(( $(date +%s) - start )) s ($log)"
    [ $rc = 0 ] || { tail -5 "$log"; return 1; }
    [ $bad = 0 ] || { echo "recompile errors > 0"; return 1; }
}

cmd_boot() {
    local secs=200 tag=latest
    while [ $# -gt 0 ]; do
        case $1 in
            --secs) secs=$2; shift 2 ;;
            --tag) tag=$2; shift 2 ;;
            *) echo "boot: unknown arg $1"; return 2 ;;
        esac
    done
    local logs; logs=$(cygpath -w "$OB_LOGS")
    PS2X_AUTO_PRESS=1 PS2X_FRAME_DUMP_DIR="$OB_ROOT/frames" powershell -NoProfile -Command "
        \$elf = '$(cygpath -w "$OB_RUN/SLPM_654.28")'
        \$p = Start-Process -FilePath '$(cygpath -w "$OB_R/ps2xRuntime/ps2EntryRunner.exe")' -ArgumentList ('\"' + \$elf + '\"') -WorkingDirectory '$logs' -RedirectStandardOutput '$logs\\boot-$tag.log' -RedirectStandardError '$logs\\boot-$tag.err' -PassThru
        if (-not \$p.WaitForExit(${secs}000)) { Stop-Process -Id \$p.Id -Force; 'stopped after $secs s' } else { 'exited by itself' }"
}

case ${1:-} in
    tools) cmd_tools ;;
    test) cmd_test ;;
    recompile) cmd_recompile ;;
    boot) shift; cmd_boot "$@" ;;
    *) sed -n '6,9p' "$0"; exit 2 ;;
esac
