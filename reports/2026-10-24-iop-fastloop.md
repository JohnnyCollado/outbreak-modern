# IOP interpreter fast loop (patch 0049)

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session
- **Goal:** stage 1 of `research/2026-10-24-native-iop-hle.md`: cut the IOP interpreter's per-instruction
  bookkeeping (DMA start check, DMA due check, import-stub check) without changing what the IOP computes or when.
- **Outcome:** patch 0049 (`C:\ob\up` `ba5a2c5`): IOP 23.6% -> 19.8% of the J's Bar game thread (~3.9 -> ~3.0 CPU-s
  per 30 s), game thread 16.45 -> 15.17 CPU-s; same instructions and cycles in the tests, capture ticks within 2,
  gate 3/3. Smaller than the research's 11-14% estimate.
- **Versions:** PS2Recomp `C:\ob\up` `18a5602` (patch 0048) + 0049; Windows 11, MSVC 19.44; PCSX2 GS v2.9.134.

## Summary

Three checks ran after every IOP instruction: taking the memory's DMA start, walking the pending-DMA map, and
asking the import registry about the pc. Each now costs one inline compare, and the full work runs only when it
can do something (`PS2X_IOP_FASTLOOP=0` restores the old path). The first version saved less than expected
because two out-of-line calls per instruction were left (entry 4); after those, the IOP is 19.8% of the game
thread. What remains is real interpretation (`executeInstruction` 7.5%, `step` 4.8%, `read32` 2.5%). The
developer waived the fail-without-test rule for this speed-only patch; the behaviour tests compare both modes
instruction for instruction.

## Struggles

### 1. The "before" profile run crawled at 2-4 fps in J's Bar

- **Seen:** the baseline `gate/play.script` run (OB_PROFILE build, `PS2X_GS=pcsx2-vk`) reached the scenario load
  but ran at 2-4 vsync/s and never got to the `load90s` capture within 420 s.
- **Found by:** the `[fps]` lines, then `[vu1] interp from the default` at the top of `boot-iopl-before.log`.
- **Cause:** on Windows the VU1 backend still defaults to the interpreter (patch 0033; 0048 changed the default on
  Android only). The 2026-10-23 speed runs set `PS2X_VU1=mvu` (`reports/2026-10-23-gameplay.md` entry 27); the
  run script here did not.
- **Fix:** the run script sets `PS2X_VU1=mvu`; baseline re-run (60 vsync/s in the room).
- **Evidence:** `C:\ob\logs\boot-iopl-before.log` (first attempt overwritten by the re-run).
- **Take-away:** a speed run in J's Bar on Windows needs `PS2X_VU1=mvu`; check the `[vu1]` line first.

### 2. The DMA timing test saw both completions at once

- **Seen:** the new test's thread printed V (voice DMA done) and W (block DMA done) with `putchar`, but both marks
  appeared at the same checkpoint, so "voice before block" failed (in both modes alike).
- **Found by:** the test output (both at EE +3072).
- **Cause:** the IOP's stdio output is logged per line, not per character: the marks are no timing signal.
- **Fix:** the thread counts its MADR polls per channel and stores them in IOP RAM; the test reads them at the end
  (17 polls = ~64 cycles for the voice DMA, 17 + 752 = ~3072 cycles for the block DMA).
- **Evidence:** `ps2xIOP/tests/iop_batch_tests.cpp` `fastLoopKeepsDmaTiming`.
- **Take-away:** time guest events with guest-side counters, not log output.

### 3. The heredoc escaping trap again

- **Seen:** a probe line inserted with Python through a Bash heredoc compiled into a broken character literal (the
  build failed quietly, the old test exe ran).
- **Cause:** `\n` lost an escape level (the known trap in memory); the file is CRLF, so `\n` patterns also miss.
- **Fix:** edited with the Edit tool.
- **Take-away:** use the Edit tool for C++ edits.

### 4. The first fast loop saved less than estimated

- **Seen:** after the first version the IOP share fell only 23.6% -> 20.3% (`hostprof-iopl-before.txt` vs
  `hostprof-iopl-after.txt`), not to the 11-14% the research estimated.
- **Found by:** the per-function rows: `decode`, `servicePendingDmaInterrupts` and `takeDmaStart` were gone, but
  `step` grew 2.2 -> 5.0% and `runCycles` 1.0 -> 2.6% (the inlined checks now count there), and
  `schedulePendingDma` was still a call per instruction (1.6%) because its new guard sat inside it.
- **Cause:** two calls per instruction were left: `schedulePendingDma` and `IopMemory::physicalAddress`, a one-line
  mask defined in another .cpp, called twice by `step`.
- **Fix:** the DMA-flag test at the call site in `step`; `physicalAddress` inline in `iop_memory.h`. Result 19.8%
  (`hostprof-iopl-after2.txt`).
- **Take-away:** in this build a function in another .cpp is not inlined (no whole-program optimisation): count
  the per-instruction calls, not only the work inside them. Single 30 s profiles: about +-1 point is noise
  (unmeasured).

## Measurements (J's Bar, `gate/play.script`, pcsx2-vk, mvu, OB_PROFILE build, hostprof 30 s from `load90s`)

| run | game thread CPU-s | IOP rows | load60s / load90s / load240s tick |
|---|---|---|---|
| `iopl-before` (0048) | 16.45 | 23.6% | 6455 / 8261 / 17276 |
| `iopl-after` (first version) | 15.64 | 20.3% | 6451 / 8255 / 17270 |
| `iopl-after2` (0049) | 15.17 | 19.8% | 6454 / 8259 / 17274 |

IOP rows = the game thread's exclusive rows from `IopCpuCore`, `IopMemory`, `IopEmulator::Impl`,
`IopImportRegistry`, `IopKernel`, `advanceIopEeCycles` (top 30 list). Yesterday's `hostprof-gst-on.txt`: 24.0%.
Logs `C:\ob\logs\hostprof-iopl-*.txt`, `boot-iopl-*.log`.

Checks on the normal build: `ob.sh test` ps2x 523/523, iop 5/5, gs_cache 45/45, ob_diag 22/22, pcsx2_vu 3/3,
pcsx2_gs 68/68; depcheck ok (36 runtime steps, 0 game units); gate `PS2X_GS=pcsx2-sw` 3/3 gated green, 3 advisory,
wall 85.7 s. Rebuilds: 15 s (IOP-only change), 85 s on a profile/normal toggle, 0 game units each time.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Stage 1 takes the IOP to ~11-14% of the game thread (research estimate) | 19.8%: the removed rows partly moved into `step`/`runCycles`, interpretation itself is ~15% | hostprof before/after | this report, entry 4 |
| A play.script speed run needs only `PS2X_GS=pcsx2-vk` | Windows VU1 default is the interpreter; `PS2X_VU1=mvu` needed | 2-4 fps, `[vu1]` line | entry 1 |

## Still open

- The rest of the IOP cost is the interpreter itself. Next steps from the research: stage 0 (cost per module),
  stage 2 (skip modules nobody calls).
- Android: not built or measured with 0049 (shared code; needs the Odin and the developer's "proceed").
- An RPC trace diff (research section 4) was not built; equality rests on the tests, the gate and the capture ticks.

## Useful for other developers

- `C:\ob\src` (the build tree) is a plain copy of `C:\ob\up` (the patch repo): copy changed files across and `cmp`
  them before building (`reports/2026-10-24-android.md` entry 2).
