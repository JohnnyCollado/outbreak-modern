# VU1 ahead of time: capture launcher and generator front end

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session
- **Goal:** start VU1 ahead-of-time compilation (research/2026-10-24-vu1-aot-and-shaders.md): more captures, and the
  generator for the 6 captured programs.
- **Outcome:** `play-capture.bat` (git-ignored) for the developer's capture play session; `tools/vu1aot/vu1aot.py`
  extracts all 27 programs from the ELF and decodes them; the 6 captured programs match their capture images byte for
  byte. The facts (below) make a direct pair-by-pair C++ emitter feasible. The emitter itself is the next step.
- **Versions:** game SLPM-65428 v1.04 + English patch v2.1; captures `C:\ob\vu1-captures` (120 files, 8 code images).

## Summary

The interpreter cannot be the base of a fast generator: its 110 ns per VU1 cycle is the cycle-by-cycle pipeline model
(queued VF/VI/ACC writes, per-pair stall calculation, flag queues, stepped XGKICK; `ps2_vu1_core.cpp:625-1100`), and
decoding is already cached (`ps2_vu1_core.cpp:1556-1600`). A generator must resolve timing at compile time, as microVU
does. The front end's facts over all 27 programs show how little timing the results depend on.

## Facts (`python -I tools/vu1aot/vu1aot.py facts`, report in `C:\ob\logs\vu1aot-facts.txt`, not committed: derived
from game code)

- 27 programs, 7,439 pairs; no unknown opcodes; no M/D/T bits; 1 of 27 has a branch target in a delay slot.
- Flag readers: clip flags FCOR 48, FCAND 34, FCGET 7; MAC flags FMAND 3; status flags none.
- Q: 57 of 74 in-block readers sit at a distance of at least the latency (31 at exactly 7 pairs after DIV, 26 at
  exactly 13 after RSQRT; the rest 8-17), so they always see the new Q (stalls only add cycles); 17 producers reach a
  branch or stop before any reader (needs a runtime check); 14 readers are WAITQ.
- P: MFP 18 pairs after ERSQRT (its latency) where read in-block.
- JR/JALR 35 (the clipper's per-plane routines), BAL 148.

## Struggles

### 1. The descriptor records are not back to back

- **Seen:** reading 27 records of 0x30 bytes from `D_00212760` hit a record whose "chain" was 0x14.
- **Cause:** 7 small side records sit between the program records (`research/...` section 1.1).
- **Fix:** match records by shape (chain pointer in word 0, size in word 3, packet builder 0x0019xxxx in word 4).

### 2. Extracted programs were 3 pairs short

- **Seen:** no captured image matched; `vu1-000032` matched `D_00212BB0` up to byte 2936 of 2960.
- **Cause:** the record's size word is smaller than the DMA chain; the chain's length is the DMA tag's QWC.
- **Fix:** read 16 + QWC x 16 bytes; 6/6 captured programs then match byte for byte.

## Next (the emitter)

- One C++ function per program, a `switch` on the entry pc (0 and each E-bit stop + 2), a label per pair, `goto` for
  branches after their delay slot; pair semantics (upper and lower read the old registers); the interpreter's float
  helpers for results (same rounding/clamping); Q/P resolved statically where the distance is at least the latency,
  a cycle-stamped fallback otherwise; clip-flag ring and the 3 FMAND MAC flags computed, status flags not; VI branch
  backup; XGKICK through `submitGifPacket(Path1)`.
- Verify with vucompare against the interpreter (data, VF, VI, ACC, Q, P, CLIP, PATH1; STATUS/MAC as microVU's known
  patterns) on the 120 captures, then time it against microVU (0.67-0.73 ns per cycle).
