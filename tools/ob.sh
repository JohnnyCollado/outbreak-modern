#!/bin/bash
# Windows (Git Bash) build/test/recompile/boot driver for PS2Recomp + Outbreak; Windows form of
# tools/recomp_cycle.sh. Reproduces the C:\ob scripts (build-tools.bat, tests.bat, ioptests.bat,
# recomp-cycle.sh, ovl-cycle.sh, build-runner.bat, boot-frames-26.ps1).
#
#   tools/ob.sh tools                        configure + build the tools tree ($OB_T)
#   tools/ob.sh test                         ps2x_tests, IOP tests, gs_cache tests: "<suite>: <pass>/<total>"
#   tools/ob.sh recompile                    main ELF + overlays demo submain netdnas, then the runner
#   tools/ob.sh boot [--secs N] [--tag T]    boot the runner (opens a window), kill after N s (200)
#   tools/ob.sh boot --script F [--tag T]    hidden scripted boot (PS2X_INPUT_SCRIPT, patch 0020); ends by
#                                            itself, returns the runner's exit code; dumps in $OB_ROOT/frames-<tag>
#   tools/ob.sh gate [--selftest] [--d N --p F]  recompile if stamp changed, scripted boot, gate.py selftest + compare
#   tools/ob.sh pcsx2-gs                     PCSX2 GS static lib (native/pcsx2_gs): clone/deps if missing, build, smoke
#
# Env (defaults): OB_ROOT=C:/ob, OB_SRC, OB_T, OB_R, OB_WORK, OB_OVL, OB_LOGS, OB_SPLAT under it;
# OB_RUN=<repo>/game_files (read only); OB_VCVARS=VS2022 Community vcvars64.bat;
# OB_PCSX2=C:/ob/pcsx2, OB_PCSX2_DEPS=C:/ob/pcsx2-deps, OB_PCSX2_GS_BUILD=C:/ob/pcsx2-gs-build.
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
    # Generated files reach the runner tree after the overlays, content-compared (tools/ob/sync_changed.py):
    # an identical file keeps its mtime, so ninja rebuilds only what changed.
    local w; w=$(cygpath -w "$OB_WORK/output")
    local srcs=("$w\\*.cpp") hdrs=("$w\\ps2_recompiled_functions.h" "$w\\ps2_recompiled_stubs.h") out
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
        w=$(cygpath -w "$OB_OVL/output-$o")
        srcs+=("$w\\*.cpp")
        hdrs+=("$w\\${o}_recompiled_functions.h" "$w\\${o}_recompiled_stubs.h")
    done
    out=$("$PY" -I "$REPO/tools/ob/sync_changed.py" "$(cygpath -w "$OB_SRC/ps2xRuntime/src/runner")" --prune "${srcs[@]}") || { echo "sources: sync failed: $out"; return 1; }
    echo "sources: $out"
    out=$("$PY" -I "$REPO/tools/ob/sync_changed.py" "$(cygpath -w "$OB_SRC/ps2xRuntime/include")" "${hdrs[@]}") || { echo "headers: sync failed: $out"; return 1; }
    echo "headers: $out"
    # Runner (build-runner.bat), with the PCSX2 external GS (native/pcsx2_gs; deps from ob.sh pcsx2-gs).
    local log="$OB_LOGS/ob-runner-build.log" start rc
    local px="${OB_PCSX2:-$OB_ROOT/pcsx2}" pxd="${OB_PCSX2_DEPS:-$OB_ROOT/pcsx2-deps}" egs="$REPO/native/pcsx2_gs"
    [ -f "$pxd/bin/shaderc_shared.dll" ] || { echo "runner: $pxd/bin/shaderc_shared.dll missing; run ob.sh pcsx2-gs first"; return 1; }
    start=$(date +%s)
    {
        { { [ -f "$OB_R/build.ninja" ] && grep -qxF "PS2X_EXTERNAL_GS_DIR:PATH=$egs" "$OB_R/CMakeCache.txt"; } ||
            vc cmake -S "$OB_SRC" -B "$OB_R" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=/O2 /Zi /DNDEBUG" -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_RECOMP=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF "-DFETCHCONTENT_BASE_DIR=$OB_T/_deps" "-DPS2X_EXTERNAL_GS_DIR=$egs" "-DOB_PCSX2=$px" "-DOB_PCSX2_DEPS=$pxd"; } &&
        vc cmake --build "$OB_R" -j $JOBS --target ps2EntryRunner
    } > "$log" 2>&1
    rc=$?
    echo "runner build exit $rc in $(( $(date +%s) - start )) s ($log)"
    [ $rc = 0 ] || { tail -5 "$log"; return 1; }
    local rd="$OB_R/ps2xRuntime"
    rm -rf "$rd/resources/shaders" && mkdir -p "$rd/resources" &&
        cp -r "$px/bin/resources/shaders" "$rd/resources/" &&
        cp "$pxd/bin/shaderc_shared.dll" "$pxd/bin/zstd.dll" "$rd/" || { echo "runner: pcsx2 staging failed"; return 1; }
    [ $bad = 0 ] || { echo "recompile errors > 0"; return 1; }
}

cmd_boot() {
    local secs=200 tag=latest script=
    while [ $# -gt 0 ]; do
        case $1 in
            --secs) secs=$2; shift 2 ;;
            --tag) tag=$2; shift 2 ;;
            --script) script=$2; shift 2 ;;
            *) echo "boot: unknown arg $1"; return 2 ;;
        esac
    done
    local logs; logs=$(cygpath -w "$OB_LOGS")
    local frames="$OB_ROOT/frames"
    if [ -n "$script" ]; then
        # Scripted boot: hidden window, script owns the pad, dumps only in capture steps to frames-<tag>.
        # The script's own timeout ends the run (exit 3); --secs (default 900 here) is only a safety kill.
        [ -f "$script" ] || { echo "boot: no script $script"; return 2; }
        [ "$secs" = 200 ] && secs=900
        frames="$OB_ROOT/frames-$tag"; rm -rf "$frames"; mkdir -p "$frames"
        export PS2X_INPUT_SCRIPT; PS2X_INPUT_SCRIPT=$(cygpath -w "$(realpath "$script")")
        export PS2X_HIDDEN=1
    fi
    # Vulkan SDK off PATH so the staged shaderc_shared.dll beside the runner is the one loaded (as cmd_pcsx2_gs).
    local np; np=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v -i vulkansdk | paste -sd:)
    PATH="$np" PS2X_FRAME_DUMP_DIR="$(cygpath -w "$frames")" powershell -NoProfile -Command "
        \$elf = '$(cygpath -w "$OB_RUN/SLPM_654.28")'
        \$p = Start-Process -FilePath '$(cygpath -w "$OB_R/ps2xRuntime/ps2EntryRunner.exe")' -ArgumentList ('\"' + \$elf + '\"') -WorkingDirectory '$logs' -RedirectStandardOutput '$logs\\boot-$tag.log' -RedirectStandardError '$logs\\boot-$tag.err' -PassThru
        \$null = \$p.Handle
        if (-not \$p.WaitForExit(${secs}000)) { Stop-Process -Id \$p.Id -Force; 'stopped after $secs s'; exit 124 }
        'exited by itself, code ' + \$p.ExitCode; exit \$p.ExitCode"
    local rc=$?
    echo "boot $tag: exit $rc; log $OB_LOGS/boot-$tag.log; frames $frames"
    return $rc
}

# Gate (docs/ops/gate.md): exit 0 green, 1 red, 2 refusal, 3 boot failed, 4 recompile failed, 5 selftest red.
cmd_gate() {
    local start selftest=0 tol=() dp=() s t rc
    start=$(date +%s)
    while [ $# -gt 0 ]; do
        case $1 in
            --selftest) selftest=1; shift ;;
            --d|--p) dp+=("$1" "$2"); shift 2 ;;
            --tolerance) tol+=("$1" "$2"); shift 2 ;;
            *) echo "gate: unknown arg $1"; return 2 ;;
        esac
    done
    local G="$REPO/tools/gate/gate.py"
    # T6: d/p overrides are selftest-only; the full gate scores with the tolerance file (rule P9).
    if [ $selftest = 0 ] && [ ${#dp[@]} -gt 0 ]; then
        echo "gate: --d/--p need --selftest (the gate reads the tolerance file), exit 2, wall 0 s"; return 2
    fi
    if [ $selftest = 0 ]; then
        s=$("$PY" -I "$G" stamp) || { echo "gate: stamp failed, exit 2, wall $(( $(date +%s) - start )) s"; return 2; }
        if [ "$s" != "$(cat "$OB_R/gate.stamp" 2>/dev/null)" ] || [ ! -f "$OB_R/ps2xRuntime/ps2EntryRunner.exe" ]; then
            t=$(date +%s)
            ( cmd_recompile ) || { echo "gate: recompile failed, exit 4, wall $(( $(date +%s) - start )) s"; return 4; }
            echo "$s" > "$OB_R/gate.stamp"
            echo "gate: recompiled in $(( $(date +%s) - t )) s"
        else
            echo "gate: not recompiled (stamp unchanged)"
        fi
        cmd_boot --script "$REPO/gate/boot.script" --tag gate ||
            { echo "gate: boot failed, exit 3, wall $(( $(date +%s) - start )) s"; return 3; }
    fi
    "$PY" -I "$G" selftest --start "$start" "${tol[@]}" "${dp[@]}"; rc=$?
    [ $selftest = 0 ] && [ $rc = 0 ] || return $rc
    "$PY" -I "$G" compare --frames "$OB_ROOT/frames-gate" --log "$OB_LOGS/boot-gate.log" --start "$start" "${tol[@]}"
}

# pcsx2-gs: PCSX2 v2.9.134 tree (clone + upstream/pcsx2/*.patch if missing), zstd/shaderc deps
# (tools/ob/pcsx2-deps.bat if missing), configure + build native/pcsx2_gs, stage shaders + DLLs beside
# the smoke exe, run it with the Vulkan SDK off PATH so the staged shaderc_shared.dll is the one loaded.
cmd_pcsx2_gs() {
    local src="${OB_PCSX2:-$OB_ROOT/pcsx2}" deps="${OB_PCSX2_DEPS:-$OB_ROOT/pcsx2-deps}"
    local b="${OB_PCSX2_GS_BUILD:-$OB_ROOT/pcsx2-gs-build}" log="$OB_LOGS/ob-pcsx2-gs.log"
    local want=a81f1aa39fc56ef60dbc913637c74c49c1792033 rc
    if [ ! -d "$src/.git" ]; then
        git clone --depth 1 --branch v2.9.134 https://github.com/PCSX2/pcsx2.git "$src" || return 1
        [ "$(git -C "$src" rev-parse HEAD)" = $want ] || { echo "pcsx2-gs: clone HEAD is not $want"; return 1; }
        local p=("$REPO"/upstream/pcsx2/*.patch)
        if [ -e "${p[0]}" ]; then git -C "$src" am "${p[@]}" || return 1; fi
    fi
    git -C "$src" merge-base --is-ancestor $want HEAD || { echo "pcsx2-gs: $src is not based on $want"; return 1; }
    if [ ! -f "$deps/bin/shaderc_shared.dll" ]; then
        mkdir -p "$deps" "$deps-build"
        MSYS_NO_PATHCONV=1 cmd /c "$(cygpath -w "$VC")" "$(cygpath -w "$REPO/tools/ob/pcsx2-deps.bat")" \
            "$(cygpath -w "$src")" "$(cygpath -w "$deps")" "$(cygpath -w "$deps-build")" > "$OB_LOGS/ob-pcsx2-deps.log" 2>&1 ||
            { tail -5 "$OB_LOGS/ob-pcsx2-deps.log"; echo "pcsx2-gs: deps failed ($OB_LOGS/ob-pcsx2-deps.log)"; return 1; }
    fi
    mkdir -p "$b"
    { vc cmake -S "$REPO/native/pcsx2_gs" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DOB_PCSX2="$src" -DOB_PCSX2_DEPS="$deps" &&
      vc cmake --build "$b" -j $JOBS; } > "$log" 2>&1
    rc=$?
    [ $rc = 0 ] || { tail -5 "$log"; echo "pcsx2-gs: build exit $rc ($log)"; return $rc; }
    rm -rf "$b/resources/shaders" && mkdir -p "$b/resources" &&
        cp -r "$src/bin/resources/shaders" "$b/resources/" &&
        cp "$deps/bin/shaderc_shared.dll" "$deps/bin/zstd.dll" "$b/" || { echo "pcsx2-gs: staging failed"; return 1; }
    local np; np=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v -i vulkansdk | paste -sd:)
    ( cd "$b" && PATH="$np" ./ob_pcsx2_gs_smoke.exe ); rc=$?
    echo "pcsx2-gs: smoke exit $rc"; return $rc
}

case ${1:-} in
    tools) cmd_tools ;;
    test) cmd_test ;;
    recompile) cmd_recompile ;;
    boot) shift; cmd_boot "$@" ;;
    gate) shift; cmd_gate "$@" ;;
    pcsx2-gs) cmd_pcsx2_gs ;;
    *) sed -n '6,13p' "$0"; exit 2 ;;
esac
