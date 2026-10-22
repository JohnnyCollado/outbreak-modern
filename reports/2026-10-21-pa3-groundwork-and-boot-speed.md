# Project Architect setup, groundwork and the boot speed

- **Date:** 2026-10-21 (third session of the day, evening; Johnny's Windows PC)
- **Who:** Johnny Collado, with Claude (Claude Code desktop app, Opus 5.5) and its planner, expert and
  coder agents under Project Architect 3.0
- **Goal:** Start the first Project Architect plan (generation 1: correct 2D rendering, checked
  against PCSX2), and do its groundwork: the Windows build in the repo, a clean PS2Recomp working tree,
  a proper frame dump, scripted input.
- **Outcome:** Plan approved (commit 7f7d777). Tasks T1-T3 of phase 1.1 done: `PROJECT_CONTEXT.md`,
  `tools/ob.sh`, patch 0019 (frame dump). The boot got about **twice as fast** once PS2Recomp's
  default "aggressive logs" were switched off, and now reaches the Difficulty screen in ~2:11.
  T4 (scripted input, hidden window, patch 0020) in progress. Rendering is still wrong.
- **Versions:** PS2Recomp `eb8688b` + patches 0001-0019; Outbreak-decomp `eb6af38`; Windows 11,
  MSVC 19.44 (VS 2022), CMake 3.30, Ninja; Ryzen 7 5700G, 32 GB. Game: SLPM-65428 v1.04 + English
  patch v2.1.

## Summary

The Project Architect install had left the plan's inputs empty (no constitution, no roadmap, no
build/test commands on the card), so the first plan began by filling them. Moving the `C:\ob`
scripts into `tools/ob.sh` hit a compile error in a temporary profiler. The developer then found
the build "extremely slow", which raised whether PS2Recomp was the wrong base. Profiling (after
fixing a deadlock in the profiler itself) showed the game thread spent 64-97% of its time writing
log lines: PS2Recomp's `PS2X_ENABLE_AGRESSIVE_LOGS` option is **on by default**. Turning it off
roughly halved the time to every screen. PS2Recomp itself was not the problem.

## Struggles

### 1. Project Architect installed without its inputs

- **Seen:** `CLAUDE.md` points to `PROJECT_CONTEXT.md`, `GENERATION_PLAN.md` and
  `phase-ends/current/PHASE_PLAN.md`; none existed. The card (`HOW_WE_WORK.md`) had TODO placeholders
  for build, run, test and paths.
- **Found by:** listing the files before planning.
- **Cause:** the install interview found no source for them.
- **Fix:** the first phase (1.1) writes them: T1 the constitution (bbc169b, approved by the developer),
  T2/T5 the card.
- **Evidence:** commits 7f7d777, bbc169b, 83b3628.
- **Take-away:** check the plan's inputs exist before briefing a planner.

### 2. The runner did not build: `std::max` and `windows.h`

- **Seen:** `TempProfiler.cpp(143): error C2589: '(': illegal token on right side of '::'`.
- **Found by:** the first `ob.sh tools` run (`.run/1.1/logs/ob-tools.log`).
- **Cause:** a temporary profiler added to `C:\ob\src` at 17:38 includes `windows.h`, whose `max`
  macro breaks `std::max(...)`.
- **Fix:** `(std::max)(...)`. (The file was later removed from `C:\ob\src` by T3 and kept in
  `phase-ends/current/logs/T3.TempProfiler.cpp`.)
- **Evidence:** `.run/1.1/logs/ob-tools2.log` (exit 0 after the fix).
- **Take-away:** on Windows, write `(std::max)` or define `NOMINMAX` in any file that includes
  `windows.h`.

### 3. The runner can't open a hidden window

- **Seen:** every test boot opens a visible window, which the developer's rules put behind a
  "proceed" each time.
- **Found by:** reading `ps2_runtime.cpp:768` (only `FLAG_WINDOW_RESIZABLE` is set).
- **Cause:** upstream has no hidden mode.
- **Fix:** T4 / patch 0020: `PS2X_HIDDEN=1`, or any input script, sets raylib's hidden flag before
  the window opens and disables Esc; `bash tools/ob.sh boot --script gate/boot.script` uses it.
- **Evidence:** every T4 boot log (t4disc, t4dbg, t4a, t4b) prints `[run] window hidden=1 flags=0x84`;
  C:\ob\up ab85c05, repo 39faeb7.

### 4. The profiler froze every boot it ran in

- **Seen:** two profiled boots (17:40 and 18:49) stopped about 3 s in, with no profiler output;
  the unprofiled boot kept going.
- **Found by:** comparing the three boots' logs, then reading the sampler loop.
- **Cause:** the sampler suspended a game thread, then stored the sample in a `std::map` (a heap
  allocation) **while the thread was still suspended**. If that thread held the heap lock, the
  sampler waited forever for a lock owned by the thread it had suspended.
- **Fix:** read the context, resume the thread, then store the sample.
- **Evidence:** boot `t2c` (18:57): 8 profiler windows printed; logs `C:\ob\logs\boot-t2c.*`.
- **Take-away:** never allocate, lock or print between `SuspendThread` and `ResumeThread`.

### 5. The boot was slow because of logging, not PS2Recomp

- **Seen:** about 2 minutes to reach the first logo (ACCESS); ~24 vsync ticks/s against 60.
  Raised the question "was PS2Recomp a mistake?".
- **Found by:** the profiler (struggle 4): the game thread was 64-97% in `NtWriteFile` across all 8
  windows. The log held ~24,000 lines of `[sceSifSetDma:CALL]` / `[sceSifSetDma:DESC]`, written
  unbuffered to `std::cerr` (`SIF.cpp:651,665`).
- **Cause:** those lines sit behind `PS2_IF_AGRESSIVE_LOGS`, and PS2Recomp's
  `PS2X_ENABLE_AGRESSIVE_LOGS` CMake option defaults to **ON** (`ps2xRuntime/CMakeLists.txt:14`).
- **Fix:** configure the runner with `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF` (now in `tools/ob.sh`).
  No code changed.
- **Evidence:** same build, one option changed:

  | | logs on (`t2c`) | logs off (`t2d`) |
  |---|---|---|
  | character select | ~4 min | 1:39 |
  | Difficulty screen | ~4:13 | 2:11 |
  | steady rate after warm-up | ~24 ticks/s | ~60 ticks/s |
  | game thread in file writes | 64-97% | ~0 |

  Commit 686d39a; logs `C:\ob\logs\boot-t2c.*`, `boot-t2d.*`.
- **Take-away:** before blaming the recompiler, profile, and check the build's logging options.
  The first ~1,200 vsyncs (boot logos) are still slower than real time; cause **Unknown**.

### 6. Temporary hacks lived only in an untracked working tree

- **Seen:** `C:\ob\src` differed from the patched PS2Recomp (`C:\ob\up`) by 18 hunks in 8 files:
  traces, a frame dump that saved every 120th frame with white bands, the auto-press, the profiler.
- **Fix:** T3 sorted them. Kept: the frame dump, rewritten as patch 0019 (native size, a test that
  fails without it). Left for T4: the auto-press. Dropped: the traces and the profiler (saved).
  `C:\ob\src` now equals `C:\ob\up` except the auto-press.
- **Evidence:** C:\ob\up 3ee659b, repo 7f58abb; suites 463/463, 4/4, 45/45; a 60 s boot wrote 1,993
  frames at 512x448 (the first two, before the mode is set, are 640x448).
- **Take-away:** a debugging hack that proves useful becomes a numbered patch with a test the same
  day, or it rots.

### 7. Finding each boot screen in the game's memory

- **Seen:** the scripted boot has to wait for each screen by reading guest memory, but the decomp
  gave only part of the map: the title state sits behind a pointer, the logo task has no fixed
  name, and the character-select "step 9" was not on the title structure.
- **Found by:** three code lookups (R1.1-006..008), the splat assembly (the logo task is task-pool
  slot 0), then a hidden "discovery" boot: the old blind press cycle plus `watch` steps that log
  each value change with its vsync tick, matched against frames captured along the way.
- **Cause:** the decomp is partial; structure fields are reached through heap pointers.
- **Fix:** gate/boot.script waits on six confirmed predicates (boot task step/sub, title state word,
  select state byte). The screen order differs from the plan: the save-data prompt comes first,
  before the content warning, ACCESS and CAPCOM. The first gate run timed out (exit 3, naming
  the character-select wait): Scenario Select (select state 8) needs a Circle the script lacked;
  a second watch boot (t4dbg) showed it, and the script gained that step.
- **Evidence:** C:\ob\logs\boot-t4disc.log, boot-t4dbg.log, boot-t4a0.log (timeout), boot-t4a.log
  and boot-t4b.log (exit 0).
- **Take-away:** a `watch` step that logs value changes per vsync turns "which address means which
  screen" from guessing into one boot.

### 8. How repeatable is a scripted boot?

- **Seen:** the gate (phase 1.2) needs to know how much two runs of the same build differ.
- **Found by:** two runs, t4a and t4b, of gate/boot.script from the same rebuilt runner, then a byte
  comparison of every dump.
- **Result:** every capture started at the same vsync tick in both runs; 17 of 18 frames are
  byte-identical. The one that differs (title, third frame) was latched at tick 1578 in one run
  and 1579 in the other, because the host window loop skipped a different tick. The script steps
  on guest vsyncs, which is why the rest match.
- **Take-away:** compare frames by tick, not by capture index, or capture fewer frames per screen.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| The slow `sceSifSetDma` lines were a leftover temporary trace (Claude) | They are PS2Recomp's own "aggressive logs", on by default | reading the `PS2_IF_AGRESSIVE_LOGS` macro and `CMakeLists.txt:14` before editing | struggle 5; fixed with a build option, not a code edit |
| "The fix is applied" (Claude to the T2 agent) | the edit had failed (file not read first); applied seconds later | the tool's error | same minute |
| The profiled boot's window was "slow" | it was frozen by the profiler (struggle 4) | the log stopped at +3 s | struggle 4 |
| Research report R1.1-006: `C:\ob\decomp-src` is missing | it exists, as loose `.c` files without git | the T4 agent looked | T4 log |
| Profiler as future patch "0020" (T3 agent) | 0020 is reserved for T4's scripted input | Claude's check against the plan | T3 summary: number left open |
| A scripted run whose game ends before `quit` exits 0 (T4 coder's first version) | a gate would read a run that never reached its captures as green | Claude rejected that deviation | T4.c2: exit 4 with `[script] game ended before quit at step <i> ...`, plus a test |
| Patch 0020 re-exported by `git commit --amend` in C:\ob\up (T4 coder) | the house rules say never amend | Claude's review of the coder report | noted as a deviation; C:\ob\up is a local, never-pushed patch source, so no further action |
| Plan: the save-data screens come after the title | they come first, before the content warning and the logos | the T4 discovery boot | struggle 7; plan wording unchanged |
| R1.1-008: character select is "step 9" of the title structure | it is state 9 of a separate select structure (pointer at 0x003065FC) | the first T4 gate run did not fire; a watch boot found it | struggle 7 |

## Still open

- **Rendering is wrong on every screen.** That's the rest of generation 1: the gate (phase 1.2), then
  fixes screen by screen (1.3-1.5).
- **After the Difficulty screen** the picture goes black and the game thread waits ~92% of the
  time: the start of loading a scenario (milestone M4). Not investigated.
- **The first ~1,200 vsyncs** run slower than real time. Cause **Unknown**; the profiler can measure
  it again once it returns as an opt-in patch.
- **Game code in git history:** commit bb0c282 ("Added Game_Files to repo"), reachable from
  `origin/main`, holds the game's ELF and its overlays. Removing it needs a history rewrite and a
  force-push: the developer's decision. Whether the GitHub repository is public is **Unknown**.
- Patch 0019's author line (GitHub noreply address) differs from 0001-0018 (full name and Gmail).

## Useful for other developers

- **PS2Recomp on Windows:** configure with `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF`. It's ON by default,
  and its unbuffered `std::cerr` traces took most of the game thread's time here.
- **A sampling profiler without a debugger:** suspend, `GetThreadContext`, resume, *then* record;
  resolve symbols through the PDB with dbghelp `SymFromAddr`, outside the sampling loop.
  `phase-ends/current/logs/T3.TempProfiler.cpp` is a working version.
- `tools/ob.sh tools|test|recompile|boot`: the whole Windows build, test and boot loop in one script.
