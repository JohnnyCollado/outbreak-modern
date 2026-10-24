# Game-thread headroom (patch 0052, hostprof call stacks, IOP stage 0)

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session
- **Goal:** free game-thread time in J's Bar's busy stretch: the CPU GS work beside PCSX2's, the `memmove`/`memchr`
  callers, IOP stages 0 (cost per module) and 2 (skip idle modules).
- **Outcome:** PS2Recomp 0052 (`C:\ob\up` `bca62d2`): decode-only GS frontend, recycled DMA chain buffer. The unpaced
  J's Bar check went 63 s -> 53 s with the same capture ticks; gate 3/3. hostprof gained call stacks (`--stacks`).
  IOP stage 0: 100% of IOP instructions are the sound stack, so stage 2 would gain nothing and was not done.
- **Versions:** PS2Recomp `C:\ob\up` 0051 + 0052; Windows 11, MSVC 19.44.

## Summary

hostprof only had exclusive samples, so `memmove`/`memchr` (~10% of the game thread) had no owner. With a stack copy per
sample and `StackWalk64` afterwards, both traced to one line: `writeIORegister` building every DMA chain in a fresh,
growing, then freed vector. Recycling one buffer and making the GS frontend skip drawing-only data when PCSX2 draws cut
the game thread's work by ~16% (check wall time). The same profile showed the next targets: `processVIF1Data` (20%), the
microVU JIT code (14%) and the IOP interpreter (~19%), whose time is all sound modules.

## Struggles

### 1. `memchr` on the game thread made no sense

- **Seen:** `memchr` 4.8% of the game thread, next to `memmove` 5.1%.
- **Found by:** call stacks: 100% of `memchr` and 79% of `memmove` from `std::vector::_Insert_counted_range` in
  `PS2Memory::writeIORegister` <- `submitDmaSend` (`hostprof-head-before.txt`).
- **Cause:** VCRUNTIME140 has no debug info; hostprof names a sample after the nearest export, so a copy routine shows
  as `memchr`. Both were the chain buffer's copies.
- **Take-away:** in export-only DLLs, trust the callers, not the name (`docs/ops/build.md` "Host profiling").

### 2. IOP stage 2 measured at zero

- **Seen / found by:** a temporary counter (build tree only, removed after; never in a patch): exclusive IOP
  instructions per module of the slice's start pc, unpaced play.script to load240s, 1.96 G instructions
  (`C:\ob\logs\boot-ioptmp.err`): modhsyn 39.7%, CRI ADX 38.6%, `buffer@0x16d5c0` 18.7% (no `.iopmod` name; by
  elimination TSNDDRV, **unverified**), libsd 3.0%, MIDI input 0.03%, sio2man / sio2d / dbcman / USB keyboard 0.00%.
- **Decision:** stage 2 (skip pad/USB/memory-card modules) not done: zero interpreter time to gain, and the game binds
  the USB keyboard's RPC server itself (`func_001D8230`), so skipping it risks the boot.
- **Take-away:** the research's stage 2 gain ("unknown, between 0 and most") is 0 here; the IOP cost is the sound
  stack, which only native sound drivers (research stages 4-5) remove.

## Measurements (hostprof 45 s, OB_PROFILE build, play.script from load120s + 30 s, default settings, paced)

| row (game thread, % of samples) | before (`head-before`) | after (`head-after`) |
|---|---|---|
| memmove + memchr + heap alloc/free | 13.1% | ~5.3% |
| GS writeRegisterPacked + vertexKick + buildDrawBatch | 3.2% | < 0.7% each (out of top list) |
| processVIF1Data | 18.8% | 20.0% (larger share of less work) |
| IOP (executeInstruction, step, runCycles, read32) | 17.8% | 19.1% |

The game thread is saturated in this stretch both times (0.95 / 0.92 cores), so shares move, not totals. Direct speed
measure, unpaced (`ob.sh check`): 63 s before (`fast5`, `fast6`), **53 s** after (`head1`), capture ticks 6449 / 8253
all three. Gate `pcsx2-sw` 3/3; ps2x 531/531, iop 5/5, gs_cache 45/45, ob_diag 23/23, pcsx2_vu 3/3, pcsx2_gs 68/68;
depcheck 0 game units.

## Still open (next game-thread targets, from `hostprof-head-after.txt`)

- `processVIF1Data` 20%: the VIF1 unpacker feeding VU1 and the GS.
- The remaining chain copy (~3%): parsing chains in place instead of assembling them.
- IOP ~19%: all sound (modhsyn, CRI ADX, TSNDDRV); native sound drivers after M5.
- `hasFunction`/`lookupFunction` walking guest modules (`residentModuleFunction`, memcmp of fingerprints) per
  dispatched branch: ~1%.
