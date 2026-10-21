# Native port bring-up: from milestone plans to the first boot

- **Date:** 2026-10-20 to 2026-10-21
- **Who:** Johnny Collado, with Claude (Claude Code, cloud session) doing the research, code and runs
- **Goal:** Plan the native Windows/Android (later Linux) port milestone by milestone, choose
  what to build on, and get the game's own code recompiled and booting.
- **Outcome:** Built on PS2Recomp, with nine fixes to it. SLPM-65428's main program
  recompiles with 0 errors, builds in about 4 minutes, and boots through its own setup: graphics,
  11 IOP modules, the sound driver, and the DVD file system. It stops where it needs the game's
  data archives (`NETBIO00-02.DAT`), which weren't provided yet.
- **Versions:** PS2Recomp `eb8688b` (+ patches 0001-0009 in `upstream/ps2recomp/`),
  Outbreak-decomp `eb6af38`, REO `de19db7` (read only), ps2sdk `329e2bb` (reference only),
  PCSX2 `master` 2026-10-20 (reference only). Ubuntu 24.04 container, 4 cores, 15 GB RAM;
  GCC 13.3, Clang 18.1, Android NDK r27c, rabbitizer 1.16.2, splat 0.50.0. Game:
  SLPM-65428 v1.04 + English patch v2.1, all 9 binaries sha1-verified against the decomp.

## Summary

The plan assumed we'd write our own recompiler; research found two existing projects (REO,
PS2Recomp) and we switched to building on PS2Recomp. Most of the work after that was finding
why the boot stopped, one wall at a time: a Linux build flag, a case-sensitive file lookup, IOP
modules read from the wrong memory, three missing LOADFILE calls, a missing IOP kernel service
(message boxes), and a missing CD call. What got things unstuck every time was the same: find the
exact loop the game is in (address → decomp function → decomp C), then prove the cause with the
debugger before changing code. The biggest single improvement came from the user's question
"why aren't we using the decomp?": its function list took recompiler errors from 8,261 to 0.
Several of my claims along the way were wrong; they are listed under "Mistakes and corrections".

## Struggles

### 1. The decomp's PC port repository couldn't be read

- **Seen:** `git clone` of GalaxyNostalgia/Outbreak-PC asked for credentials.
- **Found by:** trying to clone it for the milestone research.
- **Cause:** Unknown: private, renamed, or deleted.
- **Fix:** Worked from the decomp itself (which is public), REO and PS2Recomp instead. Recorded as
  an open question in `docs/milestones/README.md`.
- **Evidence:** `docs/milestones/README.md` (sources table).
- **Take-away:** Note unreachable sources explicitly instead of assuming what's in them.

### 2. The design never considered existing recompilers

- **Seen:** While researching the milestones, a search turned up **REO** (an Outbreak recomp of
  the US version, Windows, ~35x slower than real time in 3D) and **PS2Recomp** (the general
  recompiler it's built on).
- **Found by:** web search for existing Outbreak recompilation projects during planning.
- **Cause:** The original design compared "wait for the decomp / interpreter / static recomp" but
  not "write our own vs reuse one".
- **Fix:** Added a milestone 0 to choose the base; the user chose PS2Recomp (option B).
- **Evidence:** `656c8e6`, `fea86ab`, `docs/milestones/m0-choose-the-base.md`, `m0-decision.md`.
- **Take-away:** Search for existing projects before designing a big component.

### 3. PS2Recomp didn't build on Linux

- **Seen:** GCC: `inlining failed in call to 'always_inline' '_mm_extract_epi64': target specific
  option mismatch`. Earlier, raylib's configure failed for want of X11 headers.
- **Found by:** building it in the container.
- **Cause:** The runtime uses SSE4.1 intrinsics; only MSVC got an arch flag (`/arch:AVX2`).
  GCC/Clang need `-msse4.1`. (X11: missing `libxrandr-dev` and friends in the container.)
- **Fix:** Patch 0001 adds `-msse4.1` for non-MSVC x86-64; X11 dev packages installed.
- **Evidence:** `upstream/ps2recomp/0001-*.patch`, `5a83c8f`; tests 445/445 on GCC and Clang.
- **Take-away:** "Tested mainly with MSVC" in a README means check every other compiler.

### 4. Accuracy bugs in PS2Recomp's translation

- **Seen:** A code review of the recompiler listed suspect translations. Spot-checked in the code
  before trusting the review.
- **Found by:** Two review agents read the recompiler and runtime; the main claims were checked
  by hand, and each fix was compared with PCSX2's interpreter.
- **Cause / fix:** 64-bit branch compares done in 32 bits (0002); `CVT.W.S` rounding instead of
  truncating and not saturating, which was undefined in C++ and **gave different results on x86
  and ARM** (0003); VU0 `VRSQRT` ignoring an operand and wrong divide-by-zero results (0004);
  VU0 load/store ops using the wrong memory, swapped operands, and writable VI0 (0005).
- **Evidence:** patches 0002-0005; 449/449 tests; **each new test fails on the old code** (run to
  prove it); each patch builds and passes on its own.
- **Take-away:** Check reviewer findings in the code; prove a test can fail before trusting it.

### 5. "Does the game read Q before the divide finishes?"

- **Seen:** The user raised VU divide-pipeline timing (Q register latency), which PS2Recomp doesn't
  model.
- **Found by:** Instead of arguing it, wrote a scanner (`tools/q_scan.py`, tested) and ran it on
  the game.
- **Cause / fix:** None needed. All 42 divide ops wait (`VWAITQ`) before any read; 45 of 46 Q
  reads follow a wait, and the remaining one is a register dump.
- **Evidence:** `459f708`, `results/q-scan-20261021.md`.
- **Take-away:** When a hardware detail might matter, measure whether the game depends on it.

### 6. The game files were English-patched

- **Seen:** 4 of the 9 binaries didn't match the decomp's `build.sha1`.
- **Found by:** sha1 check.
- **Cause:** They're the English fan patch v2.1, which matches `english.sha1` exactly.
- **Fix:** Used the decomp's English configs (`*.en.yaml`) for those four.
- **Evidence:** `results/q-scan-20261021.md` (Input).
- **Take-away:** Always fingerprint inputs; a mismatch can be a known variant, not corruption.

### 7. PS2Recomp's analyzer decoded data as code (8,261 errors)

- **Seen:** The first recompile "succeeded" with 8,261 errors and 37 fake functions (up to 7.4 MB
  each). It found 3,509 functions where the decomp lists 5,358.
- **Found by:** All errors were past 0x1d9b00, where the decomp's `LAYOUT.md` says the VU microcode
  and data start; raw words were ASCII text. **The user asked "why aren't we using the decomp?"**
- **Cause:** I used PS2Recomp's fallback analyzer (a JAL scanner) as a shortcut. It can't tell
  code from data and misses functions only reached through pointers.
- **Fix:** Ran the decomp's splat configs and converted its function list into PS2Recomp's "Ghidra
  map" (`tools/splat_functions.py`, tested). Errors 8,261 → 0, functions 3,509 → 5,358.
- **Evidence:** `003fa3f`, `results/recomp-main-20261021.md` §1.
- **Take-away:** Use the decomp's knowledge first; shortcuts cost more than they save.

### 8. The decomp's configure script wanted the original files

- **Seen:** `configure.py` checks the unpatched sha1s before adding the English targets.
- **Found by:** reading `configure.py`.
- **Cause:** Its English step builds on top of the original-disc step.
- **Fix:** Ran splat directly on the committed `*.en.yaml` (patched files) and normal yamls (the 5
  unpatched overlays). Splat still checks every file's sha1.
- **Evidence:** `results/recomp-main-20261021.md` §1.
- **Take-away:** Read the tool's script before asking for more inputs.

### 9. Tooling mishaps while measuring the build

- **Seen:** A timed compile finished in "0 s"; a later `pkill` killed my own shell; a run showed
  "4 failed" compiles.
- **Cause:** `/usr/bin/time` isn't installed; `pkill -f compile1.sh` matched its own command line;
  the "failures" were the 4 parallel jobs interrupted by the stop.
- **Fix:** Shell timing instead; narrower process matching; checked that no completed file had failed.
- **Take-away:** When a number looks impossible (0 s, sudden failures), check the measurement first.

### 10. The game couldn't open `DRIVE.DAT` although it was there

- **Seen:** The boot looped in `func_001AA830`; the loop was `sceOpen("cdrom0:\IOP\MODULES\DRIVE.DAT;1")`.
- **Found by:** loop address → decomp map → decomp C → disassembly of the loop; then reading
  PS2Recomp's `PS2Vfs::resolveHostPath`.
- **Cause:** The VFS joined paths with exact case; the files were `drive.dat`. Fine on Windows,
  broken on Linux **and Android**.
- **Fix:** Patch 0006: on an exact-case miss, match each path component ignoring case.
- **Evidence:** `5bbf6c6`; its test fails without the fix (449/450).
- **Take-away:** Case-sensitivity is a Linux/Android porting bug class; test with lower-case copies.

### 11. Proving the milestone 2 check without trace switches

- **Seen:** The M2 check is "reaches the first `sceGsSyncV`", but upstream PS2Recomp has no trace
  switches (REO's `PS2X_TRACE_*` are from its own fork).
- **Fix:** gdb breakpoint on PS2Recomp's `sceGsSyncV` handler, with a backtrace through the
  generated functions: `_start` → `main` → boot graphics → `sceGsSyncV`. **M2 passes.**
- **Evidence:** `results/recomp-main-20261021.md` §5.
- **Take-away:** gdb on the generated code works well. Guest registers are at `$rsi + reg*16`
  (the `R5900Context*` argument), and generated functions are named `func_<ADDR>_0x<addr>`.

### 12. IOP modules couldn't be loaded from memory

- **Seen:** The game retried `sceSifLoadStartModuleBuffer` forever. With a handler added, the load
  still failed.
- **Found by:** gdb showed the handler was reached with buffer 0x120080 (IOP heap + the first
  module's offset); reading PS2Recomp showed `readGuest` reads **EE** RAM.
- **Cause:** On a PS2 the buffer lives in **IOP** memory: the game DMAs `DRIVE.DAT` there first.
  PS2Recomp looked in EE memory. Its own tests passed only because their fake host has a single
  memory.
- **Fix:** Patch 0007: read IOP RAM first, then EE RAM (old callers keep working). Then measured:
  all 11 modules load and start as real code, so option C's HLE side isn't needed to boot.
- **Evidence:** `d97e262` (in the clone) / patch 0007; the IOP-RAM test fails without the fix.
- **Take-away:** When something "can't fail" in tests but fails in the game, check whether the
  tests model the hardware's separate memories.

### 13. More LOADFILE calls: module search and start results

- **Seen:** Then the boot waited in `_lf_bind` (LOADFILE bind) via `sceSifSearchModuleByName("dev9")`.
  The HDD check also relies on the module start result, which wasn't returned.
- **Found by:** gdb backtrace; the decomp's `iop.c` (HDD setup) explained the logic. **Disassembled
  the game's own wrappers** to get the argument order rather than assume it.
- **Cause:** No handlers for `sceSifSearchModuleByName` or the internal `_sceSifLoadModule`; the
  public handler dropped the `*result` argument.
- **Fix:** Patch 0007: `_sceSifLoadModule`/`_sceSifLoadModuleBuffer` (write the start result) and
  `sceSifSearchModuleByName`. The game now detects "no HDD" exactly as it intends.
- **Evidence:** patch 0007; `results/recomp-main-20261021.md` §6.

### 14. The sound driver never came up

- **Seen:** The EE looped in `SdrDelayThread` (sleep between `SdrBind` retries); an IOP warning said
  `unhandled import thmsgbx:4`.
- **Found by:** gdb counters: 18,320 alarms, 18,319 wake-ups and 18,323 RPC binds in 70 s, so
  the sleep/wake path worked and the RPC server simply never appeared. The binds were to SID 0.
  Then a static parse of every module's import tables in `DRIVE.DAT`.
- **Cause:** The sound driver imports `thmsgbx` (IOP message boxes); PS2Recomp's IOP kernel didn't
  have them, so the driver stopped before registering its RPC server.
- **Fix:** Patch 0008: message boxes in the IOP kernel (behaviour from ps2sdk's headers and code,
  reimplemented). Result: **"Connected to TamsoftSoundDriver: Version 2.0"**.
- **Evidence:** `0a2cc9e` / patch 0008; `results/recomp-main-20261021.md` §6.
- **Take-away:** Counting calls with gdb separates "never wakes" from "wakes and retries"
  quickly. A module's import tables tell you which IOP services it needs before it runs.

### 15. `sceCdLayerSearchFile` missing

- **Seen / cause:** CRI's DVD file system called the dual-layer file search, which had no handler.
- **Fix:** Patch 0009: layer 0 searches like `sceCdSearchFile`, layer 1 reports not found.
- **Evidence:** `bf0a614` / patch 0009.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| ~1,660 of ~14,500 functions decompiled (design doc) | The decomp README says 238 + 77; a regex count says ~1,664. **Unknown** which is right | reading the decomp's README | `docs/milestones/README.md` |
| The GS cache tests pass | They had only printed a usage message; real result (via CTest) 45/45 | a later run returned exit 1 with "Pass one test name" | `upstream/ps2recomp/README.md` |
| PS2Recomp matches file names regardless of case | True only for the `sceCd*` helpers, not `sceOpen` | the boot couldn't open `drive.dat` | `results/recomp-main-20261021.md` §5, patch 0006 |
| `ps2EntryRunner` failing to link is an upstream bug | It's an artifact of how PS2Recomp was copied into this repo (its `.gitignore` excluded a tracked file) | `git status` in the clone showed the file as tracked | `7a45695` |
| Full build ~36 min (from a 200-file sample) | 250 s with PS2Recomp's unity build | measured | `results/recomp-main-20261021.md` §2 |
| Patch 0006 tested on GCC and Clang | Only GCC at first | re-read before reporting | `1a38eef` (Clang then run: 450/450) |
| The `thmsgbx` warning came from the CRI ADX driver; the sound driver isn't Tamsoft's | It came from the sound driver (module load order ≠ DRIVE.DAT order); the driver is Tamsoft's, as the port map said | DRIVE.DAT's directory is (offset, size) pairs, which I had first parsed wrongly; module size vs load gap | `results/recomp-main-20261021.md` §6 |
| Recompile with the fallback analyzer was good enough to start | It produced 8,261 errors and missed ~1,850 functions | the user's question | struggle 7 |

## Still open

- **Data archives:** `NETBIO00.DAT` (rooms), `NETBIO01.DAT` (sound), `NETBIO02.DAT` (movies), and
  possibly `0FLIST.DIR`, are needed for the next boot step. They're likely large; how to transfer
  them isn't settled (GitHub rejects files over 100 MB, and game data must not go in git).
- **Game files on `main`:** `game_files/` (program code and IOP modules) is committed to `main`.
  The repo is private, but this breaks the no-game-data rule. Cleanup postponed by the user.
- **Overlays:** PS2Recomp has no overlay support; the decomp's splat output for all 8 overlays
  is ready for that work.
- **Upstreaming:** patches 0001-0009 haven't been offered to PS2Recomp; that waits for the user's OK.
- **Clang** hasn't been re-run for patches 0007-0009. **VU0 divide-by-zero value** (±0x7FFFFFFF
  vs PCSX2's ±0x7F7FFFFF) is unknown. The **interpreter's cost on the Odin** hasn't been measured.

## Useful for other developers

- **Decomp → PS2Recomp:** `tools/splat_functions.py` turns splat output into PS2Recomp's
  `ghidra_output` CSV. For a game with a splat-based decomp, this replaces Ghidra and fixes
  data-as-code.
- **Finding where a boot is stuck:** the runtime's `[run:tick]` lines give the guest pc; map it
  to a function with the function CSV; read the decomp's C for that function; confirm with gdb
  (`break <handler>` / `break func_<ADDR>_0x<addr>`, guest registers at `$rsi + n*16`).
- **What an IRX needs:** parse its import tables (magic `0x41E00000`, library name at +12, then
  `jr ra; addiu $0,$0,<ordinal>` stubs) to list the IOP services it calls before running it.
- **Outbreak specifics:** `DRIVE.DAT` = `MUU-` magic, count, then (offset, size) pairs; 11 IRX
  modules; the game loads them in its own order. The sound driver binds RPC SIDs 0 and 1.
- **Process that worked:** one wall at a time; prove the cause (gdb, disassembly, decomp C) before
  changing code; every fix gets a test that's shown to fail without it; split fixes into
  independent patches; record wrong claims as soon as they're found.
