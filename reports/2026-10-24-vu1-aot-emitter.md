# VU1 ahead of time: the emitter

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session (started from the VU1 AOT front-end session)
- **Goal:** turn the 6 captured VU1 microprograms into C++, prove the result equals PS2Recomp's VU1 interpreter on the
  120 captures in `C:\ob\vu1-captures`, and time it against microVU (0.67-0.73 ns per VU1 cycle).
- **Outcome:** `tools/vu1aot/emit.py` generates the 6 programs (and all 27 without a refusal); on all 120 captures the
  generated code equals the interpreter (78 bit for bit, 42 differ only by STATUS/MAC left at their input value,
  microVU's pattern) and reproduces its cycle count 120/120. Speed: 2.0-2.2 ns per VU1 cycle weighted by in-game
  cycles, microVU 0.73-0.97 in the same runs: 2.3-2.5x slower. The cost is the interpreter-exact float semantics: with
  those helpers stubbed out the same code runs at 0.84 vs microVU 0.81.
  **Follow-up the same day, microVU-semantics mode (`emit.py --mode mvu`):** PCSX2's MXCSR (DAZ/FTZ, round toward
  zero) and microVU's own operand clamps and op variants; on all 120 captures equal to the x86 microVU golden files on
  every field but STATUS/MAC (112 bit for bit, 8 differ only in the final STATUS/MAC register); 0.62 ns per VU1 cycle
  weighted vs microVU 0.75 in the same run (0.83x, faster on all 5 measured programs).
- **Versions:** game SLPM-65428 v1.04 + English patch v2.1; PS2Recomp `C:\ob\up` (no change); PCSX2 v2.9.134 microVU
  (`native/pcsx2_vu`); MSVC 2022 14.44 (no clang-cl on this machine), generated files `/O2 /arch:AVX2`; captures
  `C:\ob\vu1-captures` (120 files, `play.script` to J's Bar).

## Summary

The interpreter's pipeline model (`ps2_vu1_core.cpp:625-1100`) runs once, at generation time. Every run starts with
the pipeline flushed (`ps2_vu1_core.cpp:1855-1858`), so the timing state at each pair (pending VF/VI/ACC writes,
Q/P/CLIP/MAC results in flight, EFU busy, VI branch backup) depends only on the path: the generator walks the
control-flow graph keyed by (pc, timing state) and emits one label per node. The states converge fast: 14-28 nodes for
the rigid and skinned programs (0.06-0.14 per pair), 224-270 for the clipper programs (0.5 per pair, because the
clipper's XGKICK waits split states, struggle 1). VF/VI/ACC values then follow program order (readers stall until the
write lands), so they are plain C++ locals; Q, P, CLIP and MAC readers see "the k-th newest result" with k a constant
per reader, so each is a short history of locals. Stall and flush cycles are constants per node, and the generated
code reproduces the interpreter's cycle count on all 120 captures, which checks the timing model independently of the
data.

The first build already matched the interpreter on all 120 captures (no correctness struggle at all). Speed took the
rest of the session: 2.60 ns/cycle at first, 2.04-2.23 after the optimisations below, against a structural floor of
0.84 (microVU 0.81). Three quarters of the operand normalisations and 275 of 279 MAC computations are removed by
generation-time analysis; what is left is the price of matching the interpreter's float rules exactly (a range check
on every multiply-add, a denormal flush on every add/sub/mul, normalisation of values loaded from memory).

## What was built

- `tools/vu1aot/emit.py` (generator, Python, no game data in it): a port of the interpreter's usage decoding
  (`decodeUpperUsage`/`decodeLowerUsage` :1100-1530) and stall rules (`calculatePairReadyCycle` :1017,
  `markPairWrites` :1062); three passes: discover nodes and edges; dataflow (normalisation state forward with AND at
  joins, MAC liveness backward); emit C++. Refuses (per program, with a reason) FCSET, status-flag readers,
  RINIT/RNEXT/RGET/RXOR, FMEQ/FMOR, branches or E bits in delay slots, an XGKICK in a delay slot that may have to
  wait, an upper write to vf0 read by the lower in the same pair. None of the 27 programs hits a refusal.
- `native/vu1aot/vu1aot_rt.h` (runtime, in git): the state record (`Vu1AotCtx`, plain types), the float helpers
  (operand normalisation `normalizeOperand`; FMAC result rules of `normalizeFmacExactResult` :397; FTOI/ITOF/CLIP,
  DIV/SQRT/RSQRT, the EFU ops copied from `ps2_vu1_lower.cpp:670-777`), MAC flags from the double value
  (`updateFmacFlags` :486), and the PATH1 transfer (one qword per 2 cycles from the kick, caught up before every store
  while active, `progressXgkick` :860). Round toward zero is set through MXCSR at entry, as the interpreter's
  `fesetround` (:1660); no FTZ/DAZ; no fused multiply-add.
- `native/pcsx2_vu/compare/main.cpp --aot` and `native/pcsx2_vu/CMakeLists.txt` `OB_VU1AOT_GEN`: the third backend in
  the compare tool, run on the same inputs, compared field by field with the interpreter (data memory, VF, VI, ACC,
  Q, P, I, R, STATUS, MAC, CLIP, PATH1 packets) and on the cycle count, timed as the other two (median of warm runs,
  only the call). Dispatch by the program's own bytes (`vu1aot_table.cpp`), not a 16 KB hash. The plain `vucompare`
  build is unchanged (no `--aot`).
- `tools/ob.sh vu1aot [DIR] [--repeat N] [--verbose]`: generate into `C:\ob\vu1aot\gen` (game-derived, never in git),
  build in `C:\ob\vu1aot\build`, run. A generator change rebuilds in 90-115 s (the 6 programs in parallel; the
  clipper programs are the long pole); an unchanged program is not rewritten and does not recompile.

## Results (`C:\ob\logs\vu1aot-final.log`, `--repeat 20`)

| Program | Share of in-game VU1 cycles | AOT ns/cycle (runs > 1000 cycles) | microVU same runs |
|---|---|---|---|
| `D_00212BB0` skinned | 57.7% | 2.56 | 0.72 |
| `D_00212C50` rigid + clipper | 37.6% | 1.79 | 0.82 |
| `D_00212B40` skinned, FMAND | 2.3% | 2.11 | 0.56 |
| `D_00212C80` rigid + clipper | 1.7% | 2.82 | 0.66 |
| `D_00212CB0` rigid + clipper | 0.6% | 1.36 | 0.76 |
| weighted by `summary.txt` | 100% | **2.23** | **0.97** |

(microVU's own figure moves between 0.73 and 0.97 weighted from run to run on this machine; the ratio stayed 2.3-2.5x
over five runs.) Correctness: `aot: ran 120 ... == interp bit for bit 78, equal except STATUS/MAC left at input 42,
anything else 0, errors 0; cycle count == interp 120/120; denormal multiply-add results kept 0`.

Speed steps (weighted ns/cycle, microVU in the same run):

| Build | AOT | microVU | Log |
|---|---|---|---|
| first (every read normalised, MAC for every FMAC in `B40`, SSE) | 2.60 | 1.00 | `C:\ob\logs\vu1aot-first.log` |
| floor: helpers stubbed (wrong in 8 captures), SSE | 1.00 | 0.97 | `C:\ob\logs\vu1aot-floor.log` |
| known-normal analysis + shadows, MAC liveness | 2.34 | 0.79 | `C:\ob\logs\vu1aot-known-normal.log` |
| + cheaper range check, `/arch:AVX2`, data pointer local | 2.10 | 0.84 | `C:\ob\logs\vu1aot-avx.log` |
| (slow path inlined: 2.04, +20 s compile; reverted) | 2.04 | 0.86 | `C:\ob\logs\vu1aot-inline-slow.log` |
| floor on the final flags | 0.84 | 0.81 | `C:\ob\logs\vu1aot-floor-avx.log` |
| final | 2.23 | 0.97 | `C:\ob\logs\vu1aot-final.log` |

## microVU-semantics mode (follow-up, `C:\ob\logs\vu1aot-mvu-final.log`, `--repeat 20`)

- **What:** `emit.py --mode mvu` (`ob.sh vu1aot --mode mvu`, output `C:\ob\vu1aot\gen-mvu`, build
  `C:\ob\vu1aot\build-mvu`) gives PCSX2 microVU's results instead of the interpreter's, taken from microVU's code
  in the PCSX2 v2.9.134 tree we build:
  - MXCSR as PCSX2's default VU1 control register: round toward zero, DAZ, FTZ (`Pcsx2Config.cpp:36-40`); overflow
    clamping on, extra/sign overflow off (`Pcsx2Config.cpp:459-466`), so results are never clamped and operands only
    where the opcode table asks (`microVU_Upper.inl:582-663`): SUB family with xyzw (Fs, Ft), MUL family (Fs; Ft
    with xyzw), MULAx/y/z (Fs), MULAw (Fs; Ft with xyzw), MADDx/y/z/w and MADDAx/y/z/w (Fs). The clamp is
    `mVUclamp1` (min FLT_MAX, max -FLT_MAX; NaN becomes +FLT_MAX, `microVU_Clamp.inl:25-41`) on a copy, never on the
    register. VF0 is not clamped (`checkVFClamp`).
  - Plain SSE add/sub/mul (no denormal flush, no range check: FTZ/DAZ do that); `x - x` is +0 (`doSafeSub`, :173);
    MAX/MINI by integer compare of ordered bits (`MIN_MAX_PS`, `microVU_Misc.inl:380-428`); FTOI's `> 0x4effffff`
    fix-up (:477-505); CLIP with zero-exponent lanes as +0 and the raw |w| limit (:533-576); ABS raw; OPMULA/OPMSUB
    with a real w product; DIV/SQRT/RSQRT/ERSQRT/ERLENG as `microVU_Lower.inl` (zero tests by float compare,
    `mVUclamp1` on DIV/RSQRT results, ERLENG through `dpps 0x71`). Only ERSQRT and ERLENG of the EFU ops occur in
    the 27 programs; the others are refused in this mode.
  - MAC for the FMAND readers as `mVUupdateFlags` computes it (Z and S per dest lane, no U/O: overflow hack off).
  - Timing (which Q/P/CLIP/MAC instance a reader sees, XGKICK, cycle count) stays the interpreter's model: on these
    captures microVU and the interpreter agree on every value that depends on it.
- **Verification:** `vucompare --aot --golden C:\ob\vu1-captures\golden` (new: the AOT outputs against the golden
  files, all fields): `aot vs x86 microVU golden: compared 120, bit for bit 112, differing only in STATUS/MAC 8,
  anything else 0`. The 8: the final MAC register (all 8; one also STATUS), which microVU fills from the last flag instance
  it happened to compute and the AOT code leaves at its input value; no instruction of the run reads it (same class
  as the microVU-vs-interpreter flag pattern). On these captures the mode also still equals the interpreter
  (`== interp bit for bit 78, equal except STATUS/MAC left at input 42`): the inputs never reach the edge cases where
  the two semantics part (denormals, Inf/NaN, cancellation below FLT_MIN). The x86 microVU itself reads its golden
  back 120/120 in the same run.
- **Speed** (weighted by in-game cycles; runs > 1000 cycles per program):

| Program | AOT mvu mode ns/cycle | microVU same runs |
|---|---|---|
| `D_00212BB0` skinned (57.7%) | 0.54 | 0.68 |
| `D_00212C50` rigid + clipper (37.6%) | 0.61 | 0.73 |
| `D_00212B40` skinned, FMAND (2.3%) | 0.49 | 0.53 |
| `D_00212C80` rigid + clipper (1.7%) | 0.56 | 0.65 |
| `D_00212CB0` rigid + clipper (0.6%) | 0.59 | 0.72 |
| weighted | **0.62** | **0.75** (0.83x) |

  First run: 0.62 vs 0.76 (`C:\ob\logs\vu1aot-mvu-first.log`). Build: 90 s for the 6 programs.
- **Why it beats the floor (0.84):** the floor kept the interpreter's code shape with the helpers stubbed (operand
  copies, the branchy slow-path call per multiply-add); this mode has no slow path at all, so MSVC keeps more values
  in registers.

## Struggles

### 1. The clipper programs kick PATH1 more than once per run

- **Seen:** the first generator run refused `D_00212C50`, `D_00212C80`, `D_00212CB0`: "XGKICK at 0xae0 while an
  earlier XGKICK may still transfer".
- **Found by:** the generator's own check (an XGKICK reachable while an earlier one of the same run may be active),
  then the disassembly: the clipper sends one packet per clipped polygon (`0x900`, `0xae0` in `D_00212C50`) before the
  run's final kick at `0x508`.
- **Cause:** the interpreter holds an XGKICK until the previous transfer has copied its last qword
  (`calculatePairReadyCycle`, `ps2_vu1_core.cpp:1057`; one qword per 2 cycles, `progressXgkick` :860), so the wait
  depends on the packet's length, which only the data knows. Everything after that pair shifts by the wait.
- **Fix:** `kick_wait` in `emit.py`: at such a kick the code finishes the earlier transfer (no store can happen while
  the pair waits, so copying the rest now gives the same bytes), computes the wait d, and jumps to the node for the
  pipeline state decayed by d (d clamped where nothing is left pending: at most a handful of cases).
- **Evidence:** `Emitter.kick_wait`; node counts 224-270 for the clipper programs; cycle count equal on all 120.
- **Take-away:** a data-dependent stall need not break static timing: it is a branch on the stall length into
  pre-decayed states.

### 2. Exactness costs more than the whole structure

- **Seen:** first build 2.60 ns/cycle vs microVU 1.00, although the generated code looked lean.
- **Found by:** a throwaway build with `vu_norm`, `vu_flush` and the multiply-add check stubbed out: 1.00 vs 0.97 (and
  8 captures then differ from the interpreter, so the helpers are needed); MSVC listings (`/FA`) of `D_00212BB0`.
- **Cause:** the interpreter normalises every operand (`normalizeOperand`), and decides every FMAC result from a
  double-precision recomputation (`calculateFmacExactResult` :214, `normalizeFmacExactResult` :397). Per FMAC that is
  4-6 vector instructions of real work plus 6 per normalised operand, 4 per add/sub/mul result and ~18 per
  multiply-add check, and every multiply-add carries a call to the slow path, which keeps long-lived values out of
  the caller-saved registers.
- **Fix (partial):** (a) generation-time "known normal" lanes per register: FMAC, MAX/MINI, ABS and ITOF results are
  normalised, so reads of them skip `vu_norm`; values from memory, MOVE of those, MFIR, MFP and FTOI are not; a
  shadow copy `nX = vu_norm(vX)` serves repeated reads of a constant loaded once (75% of normalisations removed);
  (b) MAC only for the 4 FMACs whose flags an FMAND can read (backward liveness over the history depth); (c) the
  multiply-add check as float compares on magnitudes (~10 instructions); (d) `/arch:AVX2`; (e) the data pointer in a
  local. 2.60 -> 2.04-2.23.
- **Why the check is sound:** with Ef, Ep the exponents of the float result and the float product,
  2 <= Ef <= 0xFD, Ep <= 0xFD and Ep <= Ef + 21 keep the double value within ulp(p) + ulp(f) of f, inside
  [FLT_MIN, FLT_MAX], where the interpreter keeps the float result; any other lane takes the interpreter's scalar
  code. For a single add/sub/mul under round toward zero the rules reduce to "denormal -> signed zero". The one case
  the known-normal analysis cannot see (a multiply-add whose float sum cancels below FLT_MIN while the double value is
  normal: the interpreter keeps a denormal) is counted at run time (`denormal multiply-add results kept`, 0).
- **Take-away:** for a translation that must equal the interpreter, the float rules dominate the cost, not the
  control flow or the pipeline; matching microVU instead (DAZ/FTZ, its clamp points) is where the speed is.

### 3. Compile time grew with the shadow registers

- **Seen:** after adding 32 shadow locals the 6 programs took 160 s to build (the clipper `D_00212C50` alone 155 s);
  a standalone `cl /FAs` of `D_00212BB0` did not finish in 300 s.
- **Cause:** MSVC's optimiser on one goto-heavy function per program with ~64 live vector locals and a slow-path call
  site per multiply-add; `/FAs` (source interleaving) was the rest of the standalone case (`/FA`: 32 s).
- **Fix:** `/arch:AVX2` (fewer instructions to allocate) brought the build to 90-115 s; inlining the slow path was
  tried and reverted (+20 s for 3%).
- **Take-away:** still the long pole of an iteration; next step if it matters: one function per loop region
  (strongly connected component) with explicit loads/stores of the registers it uses.

### 4. Bash heredoc + Python patch script (twice)

- **Seen:** `unexpected EOF while looking for matching '` from a `python - <<'EOF'` patch of `compare/main.cpp`; later a
  macro edit whose line continuations were joined into one line, and an assertion that failed after half the edits
  were written.
- **Cause:** the known trap (memory "Heredoc escaping trap"): C strings with `\n`, quotes and trailing backslashes
  inside a heredoc.
- **Fix:** patch scripts written to the scratchpad with the Write tool, run with `python -I`; macro lines rebuilt
  with `chr(92)`; the rest with the Edit tool.
- **Take-away:** never heredoc a patch that contains C string literals or line continuations.

### 5. Token pasting with the parameter's own name

- **Seen:** `syntax error: 'constant'` at the prologue of every generated file.
- **Cause:** `#define VU1AOT_NV(n) __m128 n##n` pastes the argument twice (`n1` became `11`).
- **Fix:** `#define VU1AOT_NV(k) __m128 n##k`.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| (to myself) the generated store at 0x3b8 was missing | two `sed` ranges were printed back to back | re-reading the line numbers | not a code change |
| the shadow build's compile took > 5 min | that was `/FAs` source interleaving; the ninja build took 50-155 s per program | object timestamps | struggle 3 |

## Still open

- Speed: the exact mode stays 2.3-2.5x slower than microVU (it is the verification reference against the
  interpreter); the microVU mode is 0.83x microVU. Further gains: loop regions as functions with registers loaded once
  per region, clang-cl for the generated files (not installed; installing needs the developer's "proceed").
- microVU mode: the final STATUS/MAC registers differ from microVU in 8 captures (unread); if the EE or a later run
  ever reads them, the mode would need microVU's flag write-back. The 6 refused EFU ops need microVU versions before
  a program that uses them can run in this mode (none of the 27 does).
- The 21 programs without a capture generate but are unverified; more captures (other rooms) or synthetic runs
  against the interpreter are needed.
- Runtime backend `PS2X_VU1=aot` (PS2Recomp patch with a fail-without test, fallback to mvu/interp on an unknown
  program or `c.error`), and the cycle count to report to the EE: the AOT reproduces the interpreter's count; the
  shipping backend (microVU) reports ~8% fewer (`vucompare-0045d.log`).
- No 65,536-cycle budget: the AOT code runs to the E bit (no capture hit the budget).
- arm64: the helpers are SSE4.1/AVX intrinsics; NEON versions needed for Android.

## Useful for other developers

- `bash tools/run.sh vu1aot -- bash tools/ob.sh vu1aot [--mode mvu] [--repeat N] [--verbose]`: generate, build,
  compare (mvu mode: also against the golden files).
- `python -I tools/vu1aot/emit.py --programs all --out DIR`: per program node count, normalisations skipped, MAC
  FMACs kept; a refusal names the construct and pc.
- Measuring a "floor" by stubbing the semantic helpers in a throwaway build separates semantic cost from structural
  cost in one build cycle.
