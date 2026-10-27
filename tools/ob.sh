#!/bin/bash
# Windows (Git Bash) build/test/recompile/boot driver for PS2Recomp + Outbreak; Windows form of
# tools/recomp_cycle.sh. Reproduces the C:\ob scripts (build-tools.bat, tests.bat, ioptests.bat,
# recomp-cycle.sh, ovl-cycle.sh, build-runner.bat, boot-frames-26.ps1).
#
#   tools/ob.sh tools                        configure + build the tools tree ($OB_T)
#   tools/ob.sh test                         ps2x_tests, IOP, gs_cache, ob_diag, pcsx2_vu/gs/spu2/ipu/sio2 tests: "<suite>: <pass>/<total>"
#   tools/ob.sh recompile                    main ELF + overlays ($OB_OVERLAYS: demo submain netdnas game single, plus
#                                            the online ones netwk netaq nethttp), then the runner
#                                            (OB_PROFILE=1: host libraries with debug info, copy in $OB_ROOT/r-profile)
#   tools/ob.sh boot [--secs N] [--tag T]    boot the runner (opens a window), kill after N s (200)
#                    [--no-diag] [--hidden]  --no-diag: without the ob_diag plugin (a play build; no script, dumps,
#                                            frame-time CSV); --hidden: PS2X_HIDDEN=1 without a script
#   tools/ob.sh boot --script F [--tag T]    hidden scripted boot (PS2X_INPUT_SCRIPT, ob_diag plugin); ends by
#                                            itself, returns the runner's exit code; dumps in $OB_ROOT/frames-<tag>
#   tools/ob.sh boot --pad NAME [--secs N] [--tag T]  hidden boot driven live over \\.\pipe\NAME (PS2X_PAD_PIPE,
#                                            ob_diag plugin; client tools/ob/pad.py); shots in $OB_ROOT/frames-<tag>
#   tools/ob.sh gate [--selftest] [--d N --p F]  recompile if stamp changed, scripted boot, gate.py selftest + compare;
#                                            then the J's Bar set (its own boot) once its references exist
#   tools/ob.sh check [--tag T] [--paced]    fast J's Bar check: gate/check.script unpaced (PS2X_PACING=0), ticks
#   tools/ob.sh pcsx2-gs                     PCSX2 GS static lib (native/pcsx2_gs): clone/deps if missing, build, smoke
#   tools/ob.sh vucompare [DIR|--synthetic] [--repeat N] [--verbose] [--no-flag-hack]
#                                            build native/pcsx2_vu + compare tool, diff interpreter vs microVU on
#                                            PS2X_VU1_CAPTURE files (default $OB_ROOT/vu1-captures)
#   tools/ob.sh vu1aot [DIR] [--mode mvu] [--repeat N]  VU1 ahead-of-time code: tools/vu1aot/emit.py -> $OB_ROOT/vu1aot/gen
#                                            (game-derived), compare tool with it in $OB_ROOT/vu1aot/build, compare --aot
#   tools/ob.sh depcheck                     dry run after touching gs_frontend.h: 0 generated game units must rebuild
#   tools/ob.sh hostprof-build               external sampling profiler tools/hostprof into $OB_HOSTPROF_BUILD
#                                            (C:/ob/hostprof-build/hostprof.exe); pair with OB_PROFILE=1 recompile
#   tools/ob.sh android [shaderc|configure|build|package|verify|all]  Android arm64 .so + APK (tools/ob/android.sh,
#                                            docs/ops/android.md); never touches a device
#
# Env (defaults): OB_ROOT=C:/ob, OB_SRC, OB_T, OB_R, OB_WORK, OB_OVL, OB_LOGS, OB_SPLAT under it; OB_OVERLAYS (above);
# OB_RUN=<repo>/game_files (read only; recompile input); OB_GAME=$OB_RUN/SLPM_654.28 (what boot/gate run: the
# ELF, an extracted folder or an ISO image); OB_VCVARS=VS2022 Community vcvars64.bat;
# OB_PCSX2=C:/ob/pcsx2, OB_PCSX2_DEPS=C:/ob/pcsx2-deps, OB_PCSX2_GS_BUILD=C:/ob/pcsx2-gs-build,
# OB_PCSX2_VU_BUILD=C:/ob/pcsx2-vu-build, OB_DIAG_BUILD=C:/ob/diag-build (ob_diag.dll, ob_diag_tests.exe).
# Writes nothing inside the repo; logs go to $OB_LOGS. cmake/ctest/MSVC steps run via tools/ob/vc.bat
# (its path must contain no spaces: cmd /c quoting).
set -u
REPO=$(cd "$(dirname "$0")/.." && pwd -W) || exit 1
: "${OB_ROOT:=C:/ob}"
: "${OB_SRC:=$OB_ROOT/src}" "${OB_T:=$OB_ROOT/t}" "${OB_R:=$OB_ROOT/r}" "${OB_WORK:=$OB_ROOT/work}"
: "${OB_OVL:=$OB_ROOT/ovl}" "${OB_LOGS:=$OB_ROOT/logs}" "${OB_SPLAT:=$OB_ROOT/splat/SLPM_654.28}"
: "${OB_RUN:=$REPO/game_files}"
: "${OB_OVERLAYS:=demo submain netdnas game single netwk netaq nethttp}"
# The game the runner boots (patch 0043): the boot ELF in the extracted folder (default, as before), the folder
# itself, or an ISO 9660 disc image, e.g. OB_GAME="$REPO/ps2_emu/roms/Biohazard - Outbreak (English v2.1).iso".
: "${OB_GAME:=$OB_RUN/SLPM_654.28}"
: "${OB_DIAG_BUILD:=$OB_ROOT/diag-build}" "${OB_PCSX2_VU_BUILD:=$OB_ROOT/pcsx2-vu-build}"
: "${OB_VCVARS:=C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat}"
export OB_VCVARS
PY="${OB_PY:-C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe}"
VC="$REPO/tools/ob/vc.bat"
JOBS=14
mkdir -p "$OB_LOGS"

# vc <cmd> [args...]: run inside vcvars; no MSYS path conversion (args are Windows-form paths).
vc() { MSYS_NO_PATHCONV=1 cmd /c "${VC//\//\\}" "$@"; }

cmd_tools() {
    local log="$OB_LOGS/ob-tools.log"
    # Only the analyzer and the recompiler: building every target also built an unused second runner here (345 steps,
    # ~18 min after a recompiler header change; reports/2026-10-26-memory-cards.md). ob.sh test builds its own targets.
    { vc cmake -S "$OB_SRC" -B "$OB_T" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_FFMPEG=OFF &&
      vc cmake --build "$OB_T" -j $JOBS --target ps2_analyzer ps2_recomp; } > "$log" 2>&1
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

# diag_build: configure (once) + build native/ob_diag (the diagnostics plugin ob_diag.dll and ob_diag_tests.exe)
# into $OB_DIAG_BUILD. Headers from $OB_SRC (runtime/ps2_diag_hooks.h, ps2_vu1.h, MiniTest.h), stb from $OB_T.
diag_build() {
    local log="$OB_LOGS/ob-diag-build.log" rc start
    start=$(date +%s)
    mkdir -p "$OB_DIAG_BUILD"
    { { [ -f "$OB_DIAG_BUILD/build.ninja" ] ||
          vc cmake -S "$REPO/native/ob_diag" -B "$OB_DIAG_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOB_SRC="$OB_SRC" -DOB_T="$OB_T"; } &&
      vc cmake --build "$OB_DIAG_BUILD" -j $JOBS; } > "$log" 2>&1
    rc=$?
    echo "ob_diag: build exit $rc in $(( $(date +%s) - start )) s ($log)"
    [ $rc = 0 ] || tail -5 "$log"
    return $rc
}

cmd_test() {
    local red=0 log exe p t
    # ps2x_tests (tests.bat): build t with -k 0, then run from ps2xTest's source dir.
    # Only the test programs: building every target here recompiled a second, unused copy of the runner.
    vc cmake --build "$OB_T" -j $JOBS --target ps2x_tests ps2_gs_clut_cache_tests ps2_gs_memory_cache_tests ps2_gs_texture_cache_tests -- -k 0 > "$OB_LOGS/ob-test-build.log" 2>&1 || { red=1; echo "build t: errors (ob-test-build.log): the counts below are from a stale binary"; }
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
    # ob_diag plugin tests (native/ob_diag): the diagnostics that moved out of the engine.
    log="$OB_LOGS/ob-test-ob_diag.log"
    if diag_build > /dev/null && [ -f "$OB_DIAG_BUILD/ob_diag_tests.exe" ]; then
        (cd "$OB_DIAG_BUILD" && ./ob_diag_tests.exe) > "$log" 2>&1
        p=$(sed -n 's/^Passed: \([0-9]*\).*/\1/p' "$log" | tail -1); t=$(sed -n 's/^Total Tests: \([0-9]*\).*/\1/p' "$log" | tail -1)
        echo "ob_diag: ${p:-0}/${t:-0}"
        [ -n "$t" ] && [ "$p" = "$t" ] && [ "$t" -gt 0 ] || { red=1; grep -a 'Failed\]' "$log" | head -20; }
    else
        echo "ob_diag: 0/0 (build failed: $OB_LOGS/ob-diag-build.log)"; red=1
    fi
    # microVU placement/start-up tests (native/pcsx2_vu/tests): link the runner's ps2_runtime.lib, so they test
    # the runner as last built (ob.sh recompile, or a runner-only build after a runtime change).
    log="$OB_LOGS/ob-test-pcsx2_vu.log"
    if pcsx2_vu_build > /dev/null && [ -f "$OB_PCSX2_VU_BUILD/ob_pcsx2_vu_tests.exe" ]; then
        (cd "$OB_PCSX2_VU_BUILD" && ./ob_pcsx2_vu_tests.exe) > "$log" 2>&1
        p=$(sed -n 's/^Passed: \([0-9]*\).*/\1/p' "$log" | tail -1); t=$(sed -n 's/^Total Tests: \([0-9]*\).*/\1/p' "$log" | tail -1)
        echo "pcsx2_vu: ${p:-0}/${t:-0}"
        [ -n "$t" ] && [ "$p" = "$t" ] && [ "$t" -gt 0 ] || { red=1; grep -a 'Failed\]' -A2 "$log" | head -20; }
    else
        echo "pcsx2_vu: 0/0 (build failed: $OB_LOGS/ob-pcsx2-vu.log)"; red=1
    fi
    # GS thread tests (native/pcsx2_gs/tests): the command ring alone, and the adapter end to end on PCSX2's SW
    # renderer, built in the pcsx2-gs build dir (ob.sh pcsx2-gs staged resources/ and the DLLs there), Vulkan SDK off
    # PATH as for the smoke; and the Android GPU driver choice's pure part (ob_gpu_driver_tests).
    log="$OB_LOGS/ob-test-pcsx2_gs.log"
    local gsb="${OB_PCSX2_GS_BUILD:-$OB_ROOT/pcsx2-gs-build}" np
    np=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v -i vulkansdk | paste -sd:)
    if vc cmake --build "$gsb" --target ob_gs_ring_tests ob_pcsx2_gs_adapter_tests ob_gpu_driver_tests > "$OB_LOGS/ob-pcsx2-gs-tests-build.log" 2>&1; then
        (cd "$gsb" && ./ob_gs_ring_tests.exe; PATH="$np" ./ob_pcsx2_gs_adapter_tests.exe; ./ob_gpu_driver_tests.exe) > "$log" 2>&1
        p=$(awk '/^Passed: /{s+=$2} END{print s+0}' "$log"); t=$(awk '/^Total Tests: /{s+=$3; n++} END{print (n==3 ? s : 0)}' "$log")
        echo "pcsx2_gs: $p/$t"
        [ "$t" -gt 0 ] && [ "$p" = "$t" ] || { red=1; grep -a 'Failed\]' "$log" | head -20; }
    else
        echo "pcsx2_gs: 0/0 (build failed: $OB_LOGS/ob-pcsx2-gs-tests-build.log)"; red=1
    fi
    # PCSX2's SPU2 behind ob_spu2_bridge.h (native/pcsx2_spu2/tests): mixing rate, key-on output, DMA event, a restarted
    # IOP cycle count. Same build dir (z.dll and friends staged there).
    log="$OB_LOGS/ob-test-pcsx2_spu2.log"
    if vc cmake --build "$gsb" --target ob_pcsx2_spu2_tests > "$OB_LOGS/ob-pcsx2-spu2-tests-build.log" 2>&1; then
        (cd "$gsb" && ./pcsx2_spu2/ob_pcsx2_spu2_tests.exe) > "$log" 2>&1
        r=$(grep -o 'pcsx2_spu2: [0-9]*/[0-9]*' "$log" | tail -1)
        echo "${r:-pcsx2_spu2: 0/0 (no result: $log)}"
        if [ -z "$r" ] || grep -q '^FAIL' "$log"; then red=1; grep -a '^FAIL' "$log" | head -10; fi
    else
        echo "pcsx2_spu2: 0/0 (build failed: $OB_LOGS/ob-pcsx2-spu2-tests-build.log)"; red=1
    fi
    # PCSX2's IPU behind ob_ipu_bridge.h (native/pcsx2_ipu/tests): registers, DMA channel 4 into the FIFO, a DMAC
    # suspend, and a hand-built intra macroblock decoded by BDEC out through channel 3. Same build dir.
    log="$OB_LOGS/ob-test-pcsx2_ipu.log"
    if vc cmake --build "$gsb" --target ob_pcsx2_ipu_tests > "$OB_LOGS/ob-pcsx2-ipu-tests-build.log" 2>&1; then
        (cd "$gsb" && ./pcsx2_ipu/ob_pcsx2_ipu_tests.exe) > "$log" 2>&1
        r=$(grep -o 'pcsx2_ipu: [0-9]*/[0-9]*' "$log" | tail -1)
        echo "${r:-pcsx2_ipu: 0/0 (no result: $log)}"
        if [ -z "$r" ] || grep -q '^FAIL' "$log"; then red=1; grep -a '^FAIL' "$log" | head -10; fi
    else
        echo "pcsx2_ipu: 0/0 (build failed: $OB_LOGS/ob-pcsx2-ipu-tests-build.log)"; red=1
    fi
    # PCSX2's SIO2 + memory cards behind ob_sio2_bridge.h (native/pcsx2_sio2/tests): a new PCSX2-size card, probe,
    # sector write (in the file at once) and read-back over DATAIN/FIFO and DMA 11/12, the SIO2 interrupt. Same dir.
    log="$OB_LOGS/ob-test-pcsx2_sio2.log"
    if vc cmake --build "$gsb" --target ob_pcsx2_sio2_tests > "$OB_LOGS/ob-pcsx2-sio2-tests-build.log" 2>&1; then
        (cd "$gsb" && ./pcsx2_sio2/ob_pcsx2_sio2_tests.exe) > "$log" 2>&1
        r=$(grep -o 'pcsx2_sio2: [0-9]*/[0-9]*' "$log" | tail -1)
        echo "${r:-pcsx2_sio2: 0/0 (no result: $log)}"
        if [ -z "$r" ] || grep -q '^FAIL' "$log"; then red=1; grep -a '^FAIL' "$log" | head -10; fi
    else
        echo "pcsx2_sio2: 0/0 (build failed: $OB_LOGS/ob-pcsx2-sio2-tests-build.log)"; red=1
    fi
    # PCSX2's DEV9 + SMAP + Sockets adapter (native/pcsx2_dev9/tests): DHCP and DNS through the SMAP registers.
    log="$OB_LOGS/ob-test-pcsx2_dev9.log"
    if vc cmake --build "$gsb" --target ob_pcsx2_dev9_tests > "$OB_LOGS/ob-pcsx2-dev9-tests-build.log" 2>&1; then
        (cd "$gsb" && ./pcsx2_dev9/ob_pcsx2_dev9_tests.exe) > "$log" 2>&1
        r=$(grep -o 'pcsx2_dev9: [0-9]*/[0-9]*' "$log" | tail -1)
        echo "${r:-pcsx2_dev9: 0/0 (no result: $log)}"
        grep -a '^SKIP' "$log" | head -3
        if [ -z "$r" ] || grep -q '^FAIL' "$log"; then red=1; grep -a '^FAIL' "$log" | head -10; fi
    else
        echo "pcsx2_dev9: 0/0 (build failed: $OB_LOGS/ob-pcsx2-dev9-tests-build.log)"; red=1
    fi
    # The settings menu's core (native/ob_ui/tests): ini editing, the disc check, the menu trigger, memory cards.
    log="$OB_LOGS/ob-test-ob_ui.log"
    if vc cmake --build "$gsb" --target ob_ui_tests > "$OB_LOGS/ob-ui-tests-build.log" 2>&1; then
        (cd "$gsb" && ./ob_ui/ob_ui_tests.exe) > "$log" 2>&1
        r=$(grep -o 'ob_ui: [0-9]*/[0-9]*' "$log" | tail -1)
        echo "${r:-ob_ui: 0/0 (no result: $log)}"
        if [ -z "$r" ] || grep -q '^FAIL' "$log"; then red=1; grep -a '^FAIL' "$log" | head -10; fi
    else
        echo "ob_ui: 0/0 (build failed: $OB_LOGS/ob-ui-tests-build.log)"; red=1
    fi
    [ $red = 0 ] || echo "red suite(s): see $OB_LOGS/ob-test-*.log"
    return $red
}

cmd_recompile() {
    local bad=0 o up asm e
    # Main ELF (recomp-cycle.sh).
    cd "$OB_WORK" || return 1
    "$OB_T/ps2xAnalyzer/ps2_analyzer.exe" SLPM_654.28 analyzed.toml > analyzer.log 2>&1 || { echo "analyzer failed"; return 1; }
    # Sony's libmpeg/libipu run as recompiled game code on the external IPU (movies plan, step 1), not as PS2Recomp's
    # MPEG/IPU HLE stubs: Sofdec feeds libmpeg through its no-data callback, which the HLE never calls, and the HLE's
    # sceIpuInit uses another game's addresses (reports/2026-10-24-m5-sound.md entries 8-10).
    # Likewise libdbc's sceDbcInit/sceDbcEnd (memory cards, reports/2026-10-26-memory-cards.md): libmc2 talks to the
    # disc's mc2_s1 through recompiled libdbc sockets over the RPC client sceDbcInit binds to dbcman; PS2Recomp's
    # libpad2 HLE no-ops them (patch 0016), which leaves that client unbound. main-functions.csv names them, so
    # dropping them from the stubs list is not enough (the runtime stub's name matches): [general] recompile keeps
    # the game's code (patch 0073). The pads stay on the scePad2 HLE.
    # And libkernl's _sceFsSemInit (online play, reports/2026-10-26-online-play.md): with a network adapter the boot's
    # HDD check reaches the EE file I/O's semaphore setup (CreateSema once), a PS2Recomp stub left as a TODO.
    sed -e 's|^ghidra_output = ""|ghidra_output = "main-functions.csv"|' \
        -e '/^ *"_\{0,1\}sce\(Mpeg\|Ipu\)[A-Za-z0-9_]*@0x[0-9A-Fa-f]*",\{0,1\}$/d' \
        -e '/^ *"sceDbc\(Init\|End\)@0x[0-9A-Fa-f]*",\{0,1\}$/d' \
        -e '/^ *"_sceFsSemInit@0x[0-9A-Fa-f]*",\{0,1\}$/d' \
        -e '/^\[general\]$/a recompile = ["sceDbcInit", "sceDbcEnd", "_sceFsSemInit"]' analyzed.toml > decomp.toml
    grep -c "instructions = \[" decomp.toml | sed 's/^/instruction patch blocks: /'
    grep -c '"_\{0,1\}sce\(Mpeg\|Ipu\)' decomp.toml | sed 's/^/libmpeg\/libipu stubs left in config: /'
    grep -c '"sceDbc\(Init\|End\)@' decomp.toml | sed 's/^/sceDbcInit\/End stubs left in config: /'
    rm -rf output
    "$OB_T/ps2xRecomp/ps2_recomp.exe" decomp.toml > recomp.log 2>&1 || { echo "recomp failed"; return 1; }
    e=$(grep -c '\[error\]' recomp.log)
    echo "main: errors=$e warnings=$(grep -c '\[warn' recomp.log) files=$(ls output | wc -l)"
    [ "$e" = 0 ] || bad=1
    grep -c "scePad2\|sceDbc" decomp.toml | sed 's/^/pad2 stubs in config: /'
    cat output/sceDbc*_0x*.cpp 2>/dev/null | grep -c 'ps2_stubs::sceDbc\(Init\|End\)(' | sed 's/^/sceDbcInit\/End calls into runtime stubs: /'
    # Generated files reach the runner tree after the overlays, content-compared (tools/ob/sync_changed.py):
    # an identical file keeps its mtime, so ninja rebuilds only what changed.
    local w; w=$(cygpath -w "$OB_WORK/output")
    local srcs=("$w\\*.cpp") hdrs=("$w\\ps2_recompiled_functions.h" "$w\\ps2_recompiled_stubs.h") out
    # Overlays (ovl-cycle.sh demo submain netdnas). NETWK (lobby), NETAQ (in-game messages) and NETHTTP (the game's
    # HTTPS client) are online play (docs/plans/2026-10-26-online-play.md S0.1). NETDNAS keeps splat's 30 functions:
    # the rest has code scrambled at rest (reports/2026-10-26-online-play.md entry 3); our client skips DNAS.
    cd "$OB_OVL" || return 1
    for o in $OB_OVERLAYS; do
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
    # Runner (build-runner.bat), with the PCSX2 external GS (native/pcsx2_gs; deps from ob.sh pcsx2-gs) and the
    # external VU1 (native/pcsx2_vu, PCSX2 microVU; selected at run time by PS2X_VU1=mvu).
    local log="$OB_LOGS/ob-runner-build.log" start rc
    local px="${OB_PCSX2:-$OB_ROOT/pcsx2}" pxd="${OB_PCSX2_DEPS:-$OB_ROOT/pcsx2-deps}" egs="$REPO/native/pcsx2_gs" evu="$REPO/native/pcsx2_vu"
    [ -f "$pxd/bin/shaderc_shared.dll" ] || { echo "runner: $pxd/bin/shaderc_shared.dll missing; run ob.sh pcsx2-gs first"; return 1; }
    pcsx2_png_deps "$pxd" || return 1
    # Debug info: none by default (linking ~5,800 objects with /Zi took 5+ min); OB_DEBUG=1 for full PDBs.
    local cxxf="/O2 /DNDEBUG" ldf="/INCREMENTAL:NO"
    [ "${OB_DEBUG:-0}" = 1 ] && cxxf="/O2 /Zi /DNDEBUG" ldf="/DEBUG:FULL /INCREMENTAL:NO"
    # OB_PROFILE=1: debug info for the host libraries only (tools/ob/profile.cmake), for tools/hostprof; the game
    # units keep their flags, so toggling it rebuilds no game unit. Empty = normal build.
    local prof=
    [ "${OB_PROFILE:-0}" = 1 ] && prof="$REPO/tools/ob/profile.cmake"
    start=$(date +%s)
    {
        { { [ -f "$OB_R/build.ninja" ] && grep -qxF "PS2X_EXTERNAL_GS_DIR:PATH=$egs" "$OB_R/CMakeCache.txt" &&
              grep -qxF "PS2X_EXTERNAL_VU1_DIR:PATH=$evu" "$OB_R/CMakeCache.txt" &&
              grep -qxF "CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=$cxxf" "$OB_R/CMakeCache.txt" &&
              grep -qxF "CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO:STRING=$ldf" "$OB_R/CMakeCache.txt" &&
              grep -qxF "CMAKE_PROJECT_INCLUDE:FILEPATH=$prof" "$OB_R/CMakeCache.txt"; } ||
            vc cmake -S "$OB_SRC" -B "$OB_R" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=$cxxf" "-DCMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO=$ldf" -DPS2X_BUILD_STUDIO=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_RECOMP=OFF -DPS2X_ENABLE_FFMPEG=OFF -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF "-DFETCHCONTENT_BASE_DIR=$OB_T/_deps" "-DPS2X_EXTERNAL_GS_DIR=$egs" "-DPS2X_EXTERNAL_VU1_DIR=$evu" "-DOB_PCSX2=$px" "-DOB_PCSX2_DEPS=$pxd" "-DCMAKE_PROJECT_INCLUDE:FILEPATH=$prof"; } &&
        vc cmake --build "$OB_R" -j $JOBS --target ps2EntryRunner
    } > "$log" 2>&1
    rc=$?
    echo "runner build exit $rc in $(( $(date +%s) - start )) s ($log)$([ -n "$prof" ] && echo ', OB_PROFILE=1')"
    [ $rc = 0 ] || { tail -5 "$log"; return 1; }
    local rd="$OB_R/ps2xRuntime"
    rm -rf "$rd/resources/shaders" && mkdir -p "$rd/resources" &&
        cp -r "$px/bin/resources/shaders" "$rd/resources/" &&
        cp "$pxd/bin/shaderc_shared.dll" "$pxd/bin/zstd.dll" "$pxd/bin/z.dll" "$pxd/bin/libpng16.dll" "$rd/" &&
        cp "$REPO/native/windows/outbreak-icon.png" "$rd/" || # the window icon (ui_hook.cpp; tools/ob/make_icons.py)
        { echo "runner: pcsx2 staging failed"; return 1; }
    if [ -n "$prof" ]; then
        # A copy that survives the next normal build: play-profile.bat runs it; hostprof finds the PDB beside it.
        local pr="$OB_ROOT/r-profile"
        rm -rf "$pr" && mkdir -p "$pr" &&
            cp -r "$rd/ps2EntryRunner.exe" "$rd/ps2EntryRunner.pdb" "$rd/shaderc_shared.dll" "$rd/zstd.dll" "$rd/z.dll" \
                "$rd/libpng16.dll" "$rd/resources" "$pr/" ||
            { echo "runner: profile copy to $pr failed"; return 1; }
        echo "runner: OB_PROFILE build copied to $pr (exe + pdb)"
    fi
    diag_build || return 1
    [ $bad = 0 ] || { echo "recompile errors > 0"; return 1; }
}

# fresh_saves <tag>: scripted and live-pad boots get their own empty memory card folder, $OB_ROOT/saves-run/<tag>
# (patch 0072 PS2X_SAVES; the runner makes a blank Mcd001.ps2 there), so they repeat and never touch the developer's
# cards. PS2X_SAVES set by the caller wins (e.g. a run that loads a prepared card).
fresh_saves() {
    [ -n "${PS2X_SAVES:-}" ] && { echo "boot: memory cards in PS2X_SAVES=$PS2X_SAVES"; return 0; }
    local d="$OB_ROOT/saves-run/$1"
    rm -rf "$d"; mkdir -p "$d"
    export PS2X_SAVES; PS2X_SAVES=$(cygpath -w "$d")
}

cmd_boot() {
    local secs=200 tag=latest script= diag=1 pad=
    # Boots go straight to the game: the settings menu's launcher (PS2Recomp 0078) stays off unless set.
    export PS2X_LAUNCHER="${PS2X_LAUNCHER:-0}"
    while [ $# -gt 0 ]; do
        case $1 in
            --secs) secs=$2; shift 2 ;;
            --tag) tag=$2; shift 2 ;;
            --script) script=$2; shift 2 ;;
            --pad) pad=$2; shift 2 ;;
            --no-diag) diag=0; shift ;;
            --hidden) export PS2X_HIDDEN=1; shift ;;
            *) echo "boot: unknown arg $1"; return 2 ;;
        esac
    done
    local logs; logs=$(cygpath -w "$OB_LOGS")
    local frames="$OB_ROOT/frames"
    # Diagnostics plugin (native/ob_diag): scripts, frame dumps, PS2X_FRAME_TIME/PC_SAMPLE/VU1_CAPTURE need it.
    # --no-diag runs the engine alone, as a play build (the plugin is never staged beside the exe).
    if [ $diag = 1 ]; then
        [ -f "$OB_DIAG_BUILD/ob_diag.dll" ] || { echo "boot: $OB_DIAG_BUILD/ob_diag.dll missing (ob.sh recompile)"; return 2; }
        export PS2X_DIAG; PS2X_DIAG=$(cygpath -w "$OB_DIAG_BUILD/ob_diag.dll")
    else
        [ -z "$script" ] && [ -z "$pad" ] || { echo "boot: --script/--pad need the diagnostics plugin (drop --no-diag)"; return 2; }
        unset PS2X_DIAG
    fi
    if [ -n "$script" ]; then
        # Scripted boot: hidden window, script owns the pad, dumps only in capture steps to frames-<tag>.
        # The script's own timeout ends the run (exit 3); --secs (default 900 here) is only a safety kill.
        [ -f "$script" ] || { echo "boot: no script $script"; return 2; }
        [ "$secs" = 200 ] && secs=900
        frames="$OB_ROOT/frames-$tag"; rm -rf "$frames"; mkdir -p "$frames"
        export PS2X_INPUT_SCRIPT; PS2X_INPUT_SCRIPT=$(cygpath -w "$(realpath "$script")")
        export PS2X_HIDDEN=1
        # Default settings (gate/defaults.ini, no keys) unless PS2X_SETTINGS names a file: an outbreak.ini beside
        # the exe (the developer's play settings: upscale, textures, mods) must not change scripted runs.
        if [ -z "${PS2X_SETTINGS:-}" ]; then
            export PS2X_SETTINGS; PS2X_SETTINGS=$(cygpath -w "$REPO/gate/defaults.ini")
        fi
        fresh_saves "$tag"
    fi
    if [ -n "$pad" ]; then
        # Live pad: hidden window, the plugin owns pad port 0 and serves \\.\pipe\<name>; shots (one frame each)
        # go to frames-<tag>. Ends on the client's quit (exit 0) or the --secs kill.
        [ -z "$script" ] || { echo "boot: --pad and --script both drive the pad: pick one"; return 2; }
        frames="$OB_ROOT/frames-$tag"; rm -rf "$frames"; mkdir -p "$frames"
        export PS2X_PAD_PIPE="$pad" PS2X_PAD_SHOTS; PS2X_PAD_SHOTS=$(cygpath -w "$frames")
        export PS2X_HIDDEN=1
        fresh_saves "$tag"
    fi
    # Input bindings (patch 0038): nothing is staged; the runner reads input.ini only if one sits beside the exe
    # (example: native/input.ini.example) or PS2X_INPUT_INI names one.
    [ -f "$OB_R/ps2xRuntime/input.ini" ] && echo "boot: using $OB_R/ps2xRuntime/input.ini"
    # Vulkan SDK off PATH so the staged shaderc_shared.dll beside the runner is the one loaded (as cmd_pcsx2_gs).
    local np; np=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v -i vulkansdk | paste -sd:)
    PATH="$np" PS2X_FRAME_DUMP_DIR="$(cygpath -w "$frames")" powershell -NoProfile -Command "
        \$elf = '$(cygpath -w "$OB_GAME")'
        \$p = Start-Process -FilePath '$(cygpath -w "$OB_R/ps2xRuntime/ps2EntryRunner.exe")' -ArgumentList ('\"' + \$elf + '\"') -WorkingDirectory '$logs' -RedirectStandardOutput '$logs\\boot-$tag.log' -RedirectStandardError '$logs\\boot-$tag.err' -PassThru
        \$null = \$p.Handle
        if (-not \$p.WaitForExit(${secs}000)) { Stop-Process -Id \$p.Id -Force; 'stopped after $secs s'; exit 124 }
        'exited by itself, code ' + \$p.ExitCode; exit \$p.ExitCode"
    local rc=$?
    echo "boot $tag: exit $rc; game $OB_GAME; log $OB_LOGS/boot-$tag.log; frames $frames"
    return $rc
}

# check [--tag T] [--paced]: the fast J's Bar check. gate/check.script (play.script up to the load90s capture)
# unpaced (PS2X_PACING=0: guest timing unchanged, no wall-clock waits), pcsx2-vk + mvu unless set. Prints the wall
# time and the load60s/load90s capture ticks (play.script: ~6454 / ~8261).
cmd_check() {
    local tag=check pacing=0 start rc
    while [ $# -gt 0 ]; do
        case $1 in
            --tag) tag=$2; shift 2 ;;
            --paced) pacing=1; shift ;;
            *) echo "check: unknown arg $1"; return 2 ;;
        esac
    done
    start=$(date +%s)
    PS2X_PACING=$pacing PS2X_GS="${PS2X_GS:-pcsx2-vk}" PS2X_VU1="${PS2X_VU1:-mvu}" \
        cmd_boot --script "$REPO/gate/check.script" --tag "$tag" > "$OB_LOGS/check-$tag.txt" 2>&1
    rc=$?
    local ticks; ticks=$(grep -oE 'capture load(60|90)s k=0 tick=[0-9]+' "$OB_LOGS/boot-$tag.log" | sed 's/ k=0//' | tr '\n' ' ')
    echo "check $tag: exit $rc, wall $(( $(date +%s) - start )) s, pacing=$pacing, ${ticks:-no captures}(frames $OB_ROOT/frames-$tag)"
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
    if [ $selftest = 0 ] && [ $rc = 0 ]; then
        "$PY" -I "$G" compare --frames "$OB_ROOT/frames-gate" --log "$OB_LOGS/boot-gate.log" --start "$start" "${tol[@]}"; rc=$?
    fi
    # The J's Bar set (M4, gate/sets.json "jsbar"): its own boot (gate/jsbar.script, PCSX2 software GS + microVU
    # unless PS2X_GS/PS2X_VU1 say otherwise), selftest and compare, once its references exist. A refusal or a red
    # set wins over a green one; with --selftest only its selftest runs.
    local jrc=0 jman="$REPO/gate/ref_manifest_jsbar.json"
    if [ ! -f "$jman" ]; then
        echo "gate: jsbar set skipped (no references: $jman)"
    else
        if [ $selftest = 0 ]; then
            PS2X_GS="${PS2X_GS:-pcsx2-sw}" PS2X_VU1="${PS2X_VU1:-mvu}" cmd_boot --script "$REPO/gate/jsbar.script" --tag gate-jsbar ||
                { echo "gate: jsbar boot failed, exit 3, wall $(( $(date +%s) - start )) s"; return 3; }
        fi
        "$PY" -I "$G" selftest --set jsbar --start "$start" "${tol[@]}" "${dp[@]}"; jrc=$?
        if [ $selftest = 0 ] && [ $jrc = 0 ]; then
            "$PY" -I "$G" compare --set jsbar --frames "$OB_ROOT/frames-gate-jsbar" --log "$OB_LOGS/boot-gate-jsbar.log" \
                --start "$start" "${tol[@]}"; jrc=$?
        fi
    fi
    # worst result: refusal 2 > red 1 (selftest 5, boot 3, recompile 4 returned above)
    if [ $rc = 2 ] || [ $jrc = 2 ]; then return 2; fi
    if [ $rc != 0 ]; then return $rc; fi
    return $jrc
}

# pcsx2_png_deps <deps>: zlib + libpng for PCSX2's texture replacement (tools/ob/pcsx2-deps-png.bat) when
# libpng16.dll is missing; downloads two pinned archives once (~2 MB), ~1 min to build.
pcsx2_png_deps() {
    local deps="$1"
    [ -f "$deps/bin/libpng16.dll" ] && return 0
    mkdir -p "$deps" "$deps-build"
    MSYS_NO_PATHCONV=1 cmd /c "$(cygpath -w "$VC")" "$(cygpath -w "$REPO/tools/ob/pcsx2-deps-png.bat")" \
        "$(cygpath -w "$deps")" "$(cygpath -w "$deps-build")" > "$OB_LOGS/ob-pcsx2-deps-png.log" 2>&1 ||
        { tail -5 "$OB_LOGS/ob-pcsx2-deps-png.log"; echo "pcsx2 png deps failed ($OB_LOGS/ob-pcsx2-deps-png.log)"; return 1; }
    echo "pcsx2 png deps: built ($OB_LOGS/ob-pcsx2-deps-png.log)"
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
    pcsx2_png_deps "$deps" || return 1
    mkdir -p "$b"
    { vc cmake -S "$REPO/native/pcsx2_gs" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DOB_PCSX2="$src" -DOB_PCSX2_DEPS="$deps" &&
      vc cmake --build "$b" -j $JOBS; } > "$log" 2>&1
    rc=$?
    [ $rc = 0 ] || { tail -5 "$log"; echo "pcsx2-gs: build exit $rc ($log)"; return $rc; }
    rm -rf "$b/resources/shaders" && mkdir -p "$b/resources" &&
        cp -r "$src/bin/resources/shaders" "$b/resources/" &&
        cp "$deps/bin/shaderc_shared.dll" "$deps/bin/zstd.dll" "$deps/bin/z.dll" "$deps/bin/libpng16.dll" "$b/" ||
        { echo "pcsx2-gs: staging failed"; return 1; }
    local np; np=$(printf '%s' "$PATH" | tr ':' '\n' | grep -v -i vulkansdk | paste -sd:)
    ( cd "$b" && PATH="$np" ./ob_pcsx2_gs_smoke.exe ); rc=$?
    echo "pcsx2-gs: smoke exit $rc"; return $rc
}

# pcsx2_vu_build: configure (once) + build native/pcsx2_vu standalone into $OB_PCSX2_VU_BUILD: the compare tool and
# ob_pcsx2_vu_tests. Both link the runner's own ps2_runtime.lib ($OB_R, so ob.sh recompile first).
pcsx2_vu_build() {
    local src="${OB_PCSX2:-$OB_ROOT/pcsx2}" deps="${OB_PCSX2_DEPS:-$OB_ROOT/pcsx2-deps}"
    local b="$OB_PCSX2_VU_BUILD" log="$OB_LOGS/ob-pcsx2-vu.log" rc start
    [ -f "$OB_R/ps2xRuntime/ps2_runtime.lib" ] || { echo "pcsx2_vu: $OB_R/ps2xRuntime/ps2_runtime.lib missing; run ob.sh recompile first"; return 1; }
    start=$(date +%s)
    mkdir -p "$b"
    { { [ -f "$b/build.ninja" ] ||
          vc cmake -S "$REPO/native/pcsx2_vu" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release -DOB_PCSX2="$src"               -DOB_PCSX2_DEPS="$deps" -DOB_SRC="$OB_SRC" -DOB_R="$OB_R" -DOB_T="$OB_T" ${OB_PCSX2_VU_CMAKE_EXTRA:-}; } &&
      vc cmake --build "$b" -j $JOBS; } > "$log" 2>&1
    rc=$?
    echo "pcsx2_vu: build exit $rc in $(( $(date +%s) - start )) s ($log)"
    [ $rc = 0 ] || tail -5 "$log"
    return $rc
}

# vucompare: pcsx2_vu_build, then run the compare tool.
# Exit: the tool's (0 = every capture bit-exact, 1 = some differ, 2 = usage / no captures).
cmd_vucompare() {
    local dir="$OB_ROOT/vu1-captures" rc
    if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then dir=$1; shift; fi
    if [ $# -gt 0 ] && [ "$1" = --synthetic ]; then dir=--synthetic; shift; fi
    pcsx2_vu_build; rc=$?
    [ $rc = 0 ] || return $rc
    [ "$dir" = --synthetic ] || dir=$(cygpath -w "$dir")
    "$OB_PCSX2_VU_BUILD/ob_pcsx2_vu_compare.exe" "$dir" "$@"
}

# vu1aot: generate the VU1 ahead-of-time code for the captured programs (emit.py writes only changed files, so an
# unchanged program does not recompile), build the compare tool with it in its own directory (first build ~3-4 min:
# its own copy of ob_pcsx2_gs), run it with --aot. --mode mvu: microVU semantics (emit.py --mode mvu) in gen-mvu /
# build-mvu, compared also against the x86 microVU golden files (DIR/golden). Exit: emit.py's (1 = a program could not
# be generated) or the tool's.
cmd_vu1aot() {
    local dir="$OB_ROOT/vu1-captures" mode=exact sfx="" rc
    if [ $# -gt 0 ] && [ "${1#--}" = "$1" ]; then dir=$1; shift; fi
    if [ $# -gt 1 ] && [ "$1" = --mode ]; then mode=$2; shift 2; fi
    [ "$mode" = mvu ] && sfx=-mvu
    local gen="$OB_ROOT/vu1aot/gen$sfx" b="$OB_ROOT/vu1aot/build$sfx"
    "$PY" -I "$REPO/tools/vu1aot/emit.py" --elf "$(cygpath -w "$OB_RUN/SLPM_654.28")" --captures "$(cygpath -w "$dir")" \
        --out "$(cygpath -w "$gen")" --mode "$mode"; rc=$?
    [ $rc = 0 ] || { echo "vu1aot: emit exit $rc"; return $rc; }
    OB_PCSX2_VU_BUILD="$b" OB_PCSX2_VU_CMAKE_EXTRA="-DOB_VU1AOT_GEN=$(cygpath -m "$gen")" pcsx2_vu_build; rc=$?
    [ $rc = 0 ] || return $rc
    if [ "$mode" = mvu ]; then
        "$b/ob_pcsx2_vu_compare.exe" "$(cygpath -w "$dir")" --aot --golden "$(cygpath -w "$dir/golden")" "$@"
    else
        "$b/ob_pcsx2_vu_compare.exe" "$(cygpath -w "$dir")" --aot "$@"
    fi
}

# depcheck: proves a runtime-internal header change does not rebuild generated game code (patch 0028). Touches
# gs_frontend.h, asks ninja what the runner build would run (dry run), restores the header's mtime, and counts the
# steps: game units (`Unity/` or `Unity\` in the step name) must be 0, runtime steps (`ps2_runtime.dir`) > 0.
# `-f <absolute build.ninja>`: with CMake's CONFIGURE_DEPENDS globs the manifest's verify-globs edge is always
# dirty, and a dry run stops right after "Re-running CMake"; naming the manifest by another path skips that check.
# Exit 0 ok, 1 game units would rebuild (listed) or no runtime step, 2 no runner build.
cmd_depcheck() {
    local h="$OB_SRC/ps2xRuntime/include/runtime/gs/gs_frontend.h" log="$OB_LOGS/ob-depcheck.log" ref game rt rc
    [ -f "$OB_R/build.ninja" ] && [ -f "$h" ] || { echo "depcheck: no runner build in $OB_R or no $h (ob.sh recompile first)"; return 2; }
    ref=$(mktemp) && touch -r "$h" "$ref" && touch "$h"
    vc cmake --build "$OB_R" --target ps2EntryRunner -- -n -f "$OB_R/build.ninja" > "$log" 2>&1
    rc=$?
    touch -r "$ref" "$h"; rm -f "$ref"
    [ $rc = 0 ] || { tail -5 "$log"; echo "depcheck: dry run failed, exit $rc ($log)"; return 1; }
    game=$(grep -E '^\[[0-9]+/[0-9]+\]' "$log" | grep -cE 'Unity[/\\]')
    rt=$(grep -E '^\[[0-9]+/[0-9]+\]' "$log" | grep -c 'ps2_runtime\.dir')
    if [ "$game" = 0 ] && [ "$rt" -gt 0 ]; then
        echo "depcheck: ok: gs_frontend.h rebuilds $rt runtime step(s), 0 generated game units"
        return 0
    fi
    echo "depcheck: FAIL: gs_frontend.h rebuilds $rt runtime step(s), $game generated game unit(s) ($log)"
    grep -E '^\[[0-9]+/[0-9]+\]' "$log" | grep -E 'Unity[/\\]' | head -40
    return 1
}

# hostprof-build: configure (once) + build tools/hostprof (hostprof.exe, the external sampling profiler; no engine
# code) into $OB_HOSTPROF_BUILD. Usage: hostprof.exe (--pid N | --name ps2EntryRunner.exe) --secs 30 --out FILE.
cmd_hostprof_build() {
    local b="${OB_HOSTPROF_BUILD:-$OB_ROOT/hostprof-build}" log="$OB_LOGS/ob-hostprof-build.log" rc start
    start=$(date +%s)
    mkdir -p "$b"
    { { [ -f "$b/build.ninja" ] || vc cmake -S "$REPO/tools/hostprof" -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release; } &&
      vc cmake --build "$b"; } > "$log" 2>&1
    rc=$?
    echo "hostprof: build exit $rc in $(( $(date +%s) - start )) s: $b/hostprof.exe ($log)"
    [ $rc = 0 ] || tail -15 "$log"
    return $rc
}

case ${1:-} in
    tools) cmd_tools ;;
    test) cmd_test ;;
    recompile) cmd_recompile ;;
    boot) shift; cmd_boot "$@" ;;
    gate) shift; cmd_gate "$@" ;;
    check) shift; cmd_check "$@" ;;
    pcsx2-gs) cmd_pcsx2_gs ;;
    vucompare) shift; cmd_vucompare "$@" ;;
    vu1aot) shift; cmd_vu1aot "$@" ;;
    depcheck) cmd_depcheck ;;
    hostprof-build) cmd_hostprof_build ;;
    android) shift; bash "$REPO/tools/ob/android.sh" "$@" ;;
    *) sed -n '6,27p' "$0"; exit 2 ;;
esac
