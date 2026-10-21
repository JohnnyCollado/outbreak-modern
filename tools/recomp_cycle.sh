#!/bin/bash
# Re-analyze, recompile, rebuild PS2Recomp's runner and boot Outbreak under a virtual display.
#
#   tools/recomp_cycle.sh [seconds] [tag]        (default 150 s, tag "latest")
#
# Needs these environment variables (see CLAUDE.md, "Rebuilding the working state"):
#   PS2RECOMP   PS2Recomp source tree with patches applied (e.g. PS2Recomp-main/PS2Recomp-main)
#   TOOLS_BUILD build dir holding ps2_analyzer and ps2_recomp (e.g. $PS2RECOMP/out/linux-gcc)
#   RUNNER_BUILD build dir configured for ps2EntryRunner (e.g. $PS2RECOMP/out/runner)
#   WORK        dir with SLPM_654.28 and main-functions.csv (from tools/splat_functions.py)
#   RUN         game folder: SLPM_654.28, SYSTEM.CNF, PROG/*.BIN, IOP/MODULES/, NETBIO*.DAT
#   LOGS        where boot-<tag>.log and runner-build-<tag>.log go
# Everything it writes is game-derived: keep WORK, RUN and LOGS outside the repo.
set -u
secs=${1:-150}; tag=${2:-latest}
for v in PS2RECOMP TOOLS_BUILD RUNNER_BUILD WORK RUN LOGS; do
    [ -n "${!v:-}" ] || { echo "set $v (see the header of this script)"; exit 1; }
done
mkdir -p "$LOGS"

cd "$WORK" || exit 1
"$TOOLS_BUILD/ps2xAnalyzer/ps2_analyzer" SLPM_654.28 analyzed.toml > analyzer.log 2>&1 || { echo "analyzer failed (analyzer.log)"; exit 1; }
sed -e 's|^ghidra_output = ""|ghidra_output = "main-functions.csv"|' analyzed.toml > decomp.toml
rm -rf output
"$TOOLS_BUILD/ps2xRecomp/ps2_recomp" decomp.toml > recomp.log 2>&1 || { echo "recomp failed (recomp.log)"; exit 1; }
echo "recomp errors=$(grep -c '\[error\]' recomp.log) warnings=$(grep -c '\[warn' recomp.log)"

# PS2Recomp's runner target globs ps2xRuntime/src/runner/*.cpp (all git-ignored upstream).
cd "$PS2RECOMP" || exit 1
mkdir -p ps2xRuntime/src/runner
rm -f ps2xRuntime/src/runner/*.cpp
cp "$WORK"/output/*.cpp ps2xRuntime/src/runner/
cp "$WORK"/output/ps2_recompiled_functions.h "$WORK"/output/ps2_recompiled_stubs.h ps2xRuntime/include/
start=$(date +%s)
cmake --build "$RUNNER_BUILD" -j"$(nproc)" --target ps2EntryRunner > "$LOGS/runner-build-$tag.log" 2>&1 || {
    echo "runner build failed"; grep -m5 ' error' "$LOGS/runner-build-$tag.log"; exit 1; }
echo "runner build in $(( $(date +%s) - start )) s"

cd "$RUN" || exit 1
timeout "$secs" xvfb-run -a -s "-screen 0 1280x720x24" "$RUNNER_BUILD/ps2xRuntime/ps2EntryRunner" "$RUN/SLPM_654.28" > "$LOGS/boot-$tag.log" 2>&1
echo "boot log: $LOGS/boot-$tag.log ($(wc -l < "$LOGS/boot-$tag.log") lines); last guest pc:"
grep 'run:tick' "$LOGS/boot-$tag.log" | tail -1 | cut -c1-160
