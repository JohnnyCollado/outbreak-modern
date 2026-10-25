# Reports

Logs of the problems hit while working on this project, and how each was worked out. They're
for reviewing the project later and for sharing with other developers: someone picking the work
up should be able to see what went wrong, why, what was done about it, and what's still open,
without replaying the sessions.

`docs/` holds plans and designs (what we intend). `results/` holds measurements and test outputs
(what a tool printed). `reports/` holds the story in between: struggles, causes, fixes, and the
lessons learned.

## How to write one

- Copy [`TEMPLATE.md`](TEMPLATE.md) to `YYYY-MM-DD-<short-topic>.md`. One report per working
  session or per piece of work, whichever is smaller.
- One entry per struggle, in the order they happened. Each entry: what was seen, how it was
  found, the root cause, the fix, the evidence (commit, patch, test, log), and what to take
  from it.
- **Include mistakes.** Wrong claims, wrong estimates, wasted detours: say what was claimed,
  how it was found to be wrong, and where it was corrected. These are the most useful entries
  for a reviewer.
- Mark anything not verified as **Unverified** or **Unknown**, as in the rest of the docs.
- Reference evidence by commit hash, patch number or file path, not "see above".
- **No game data**: no code, disassembly listings, assets or file contents from the disc. Addresses,
  function names, file names, sizes and hashes are fine.

## Index

| Date | Report | Covers |
|---|---|---|
| 2026-10-21 | [Native port bring-up](2026-10-21-native-port-bring-up.md) | Milestone plans, choosing PS2Recomp, nine PS2Recomp fixes, first recompile and boot of SLPM-65428 up to the data archives |
| 2026-10-21 | [Windows bring-up: to the main menu](2026-10-21-windows-port-bank-wall.md) | Patches 0010-0018, the sound bank wall, overlays as guest modules; the boot reaches the main menu |
| 2026-10-21 | [Project Architect groundwork and boot speed](2026-10-21-pa3-groundwork-and-boot-speed.md) | First PA3 plan, `tools/ob.sh`, patch 0019, the profiler deadlock, aggressive logs halving the boot speed |
| 2026-10-22 | [The gate: reference captures and a pixel tolerance](2026-10-22-gate.md) | Phase 1.2: PCSX2 reference capture kit, display-register probe, tolerance and baseline |
| 2026-10-22 | [Phase 1.3: PCSX2 GS renderer](2026-10-22-pcsx2-gs.md) | PCSX2 2.9.134 GS in-process as a static library (SW, Vulkan), the PS2Recomp seam, the PS2X_GS selector |
| 2026-10-23 | [Gameplay bring-up: past character select](2026-10-23-gameplay.md) | Difficulty state fix; GAME/SINGLE overlays never recompiled; NOW LOADING is VU1-bound; VU1 capture (0031) and microVU offline (~200x, interpreter EFU opcode bug); EFU decode fix (0032); depcheck restored; microVU in the runner (0033): first room (J's Bar) in ~100 s, ~27 vsync/s, next limits GS vsync + IOP; IOP in batches (0042): J's Bar 27-28 -> 37 vsync/s, IOP share 37.5% -> 19.6% |
| 2026-10-24 | [Disc image and the Android A1 build](2026-10-24-android.md) | Patch 0043 (ISO 9660 image, SYSTEM.CNF boot, outbreak.ini), 0044 (Android defaults); gate 3/3 from the ISO; PCSX2 GS on arm64, shaderc arm64, sse2neon fetch, APK without Gradle; 0045 (VU1 without software quad / fused multiply-add on arm64, 182 -> 110 ns per VU1 cycle on Windows) |
| 2026-10-24 | [IOP interpreter fast loop](2026-10-24-iop-fastloop.md) | Patch 0049: one-compare DMA start/due and import-stub checks per IOP instruction, `PS2X_IOP_FASTLOOP=0` for A/B; IOP 23.6% -> 19.8% of the J's Bar game thread (estimate was 11-14%); speed runs need `PS2X_VU1=mvu` |
| 2026-10-24 | [High resolution, widescreen, 4K textures](2026-10-24-display-features.md) | Patch 0050 (upscale/window settings, `.pnach` memory patches with code writes dropped, game serial); PCSX2 texture replacement built (zlib/libpng/libzip); all three work in J's Bar hidden; pack costs frames (mean 57.6), visible check open |
| 2026-10-24 | [The fast J's Bar check](2026-10-24-fast-check.md) | Patch 0051 (`PS2X_PACING=0`), `ob.sh check` 63 s vs 140 s paced, same capture ticks within 1; ob_diag capture hold (frame N-1 at vsync N); scripted boots on `gate/defaults.ini` |
| 2026-10-24 | [Game-thread headroom](2026-10-24-game-thread-headroom.md) | Patch 0052 (decode-only GS frontend, recycled DMA chain buffer): check 63 -> 53 s; hostprof `--stacks`; IOP stage 0: all sound, stage 2 not worth doing |
| 2026-10-24 | [VIF1 UNPACK](2026-10-24-vif1-unpack.md) | Patch 0053: UNPACK loop with locals/counters/unmasked fast path, bit-identical (recorded-hash test); processVIF1Data 22.3% -> 6.8%, check 53 -> 47 s; hostprof `--lines`; chains in place not done (2.7%, risky) |
| 2026-10-24 | [VU1 AOT front end](2026-10-24-vu1-aot-frontend.md) | `play-capture.bat` for capture play; `tools/vu1aot/vu1aot.py` extracts/decodes the 27 programs (6 match captures byte for byte); facts: clip flags only + 3 FMAND, Q reads at >= latency, no M/D/T |
| 2026-10-24 | [VU1 AOT emitter](2026-10-24-vu1-aot-emitter.md) | `tools/vu1aot/emit.py` + `native/vu1aot/vu1aot_rt.h`, `ob.sh vu1aot`, `vucompare --aot`: 6 programs, 120/120 equal to the interpreter (78 bit for bit, 42 STATUS/MAC = input), cycle count 120/120; 2.0-2.2 ns/cycle vs microVU 0.7-1.0 (exact float rules cost it; floor 0.84 vs 0.81); `--mode mvu` (microVU semantics): 120/120 equal to the x86 microVU golden but final STATUS/MAC in 8, 0.62 vs 0.75 ns/cycle |
| 2026-10-24 | [J's Bar golden frames](2026-10-24-jsbar-gate.md) | Gate sets (`gate/sets.json` menus/jsbar), excluded window rectangles (approved), `gate.py/refs.py --set`, `ob.sh gate` runs J's Bar once referenced; one PCSX2 shot matches the room (12 px off); the runner's scene pacing varies run to run (open) |
| 2026-10-24 | [M5 sound: PCSX2's SPU2 in the runner](2026-10-24-m5-sound.md) | `native/pcsx2_spu2` (PCSX2 SPU2 + AudioStream pull backend), patch 0056 (external SPU2 device); 0057 (EE `SQRT.S` reads ft: ADX coefficients, saturated music); 0058 (IOP ready queue: right-channel crackle 45 -> 1.1 clicks/s); movies: 0059 (EE thread calls return the id: ADX idle thread woken), libmpeg/libipu un-stubbed, `native/pcsx2_ipu` + 0060 (external IPU), 0061 (SPR DMA chain mode); probes: page-protection EE write watch, ordered event logs, PCSX2 over PINE |
