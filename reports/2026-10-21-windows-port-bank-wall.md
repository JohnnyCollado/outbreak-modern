# Windows bring-up: from the NETBIO wall to the main menu

- **Date:** 2026-10-21 (second session of the day; this one on Johnny's Windows PC)
- **Who:** Johnny Collado, with Claude (Claude Code desktop app; Opus 5.5, then Fable 5.1 mid-session)
- **Goal:** Rebuild the working state on Windows now that the game's data archives are local, then
  follow the boot wall by wall, towards the main menu.
- **Outcome:** **The main menu is reached.** Nine more PS2Recomp patches (0010-0018) and one new
  tool (`tools/mwo3_to_elf.py`). The game loads its archives, first sound bank and root files,
  shows its save-data screens, reads the controller (libpad2), answers the memory-card prompt, and
  runs its overlays recompiled as guest modules: the ACCESS and CAPCOM screens, the title
  ("PRESS START BUTTON"), the main menu (Single Play / Network Play / Collection / Character Look /
  Options), Scenario Select and character select, driven by a temporary auto-press. **Rendering
  is badly wrong** on every screen (missing title background, broken glyphs, corrupted
  character-select images); that is the next wall.
- **Versions:** PS2Recomp `eb8688b` + patches 0001-0018; Outbreak-decomp `eb6af38` (no new
  commits); PCSX2 2.9.134 (upstream pre-release) as reference; Windows 11, MSVC 19.44 (Visual
  Studio 2022), CMake 3.30, MSYS2 GCC 16.1 + gdb 17.2 for the debugger builds; Ryzen 7 5700G,
  32 GB. Game: SLPM-65428 v1.04 + English patch v2.1 (sha1-verified), now with every root file of
  the disc.

## Summary

The Linux recipe carried over to MSVC with one change (short build paths; the 260-character path
limit) and the first Windows run passed the old wall at once because the archives were there. Then
came a chain of walls, each found with the same method as before (loop pc → decomp function →
prove the cause, mostly with gdb on a GCC build or with temporary traces in the IOP) and each
fixed in PS2Recomp with a test that fails first: thread priority 0 refused; `ChangeThreadPriority`
not returning the old priority; the analyzer silently replacing 37 of the game's own stores with
NOPs; the game's heap capped 1 MB short so a 1.1 MB `malloc` failed; a missing `sceDmaRecvN`; and
on the IOP, a sound driver whose bank-upload thread was starved by the ADX streaming thread
because SPU2 streaming DMA completed 200× too fast, plus an `sceSifGetOtherData` that copied
nothing. Johnny's PCSX2 was the reference for the last one: its debugger showed the real kernel's
thread table (priority-0 threads exist; ADX's spinner is a scheduler lock) and that the bank send
takes 5 vsyncs there. Along the way the probes caught two of my own earlier claims being wrong.

## Struggles

### 1. MSVC: paths too long

- **Seen:** `fatal error C1083: Cannot open compiler generated file: '': Invalid argument` after
  `toml11` failed to clone (`unable to create file ... .content`).
- **Found by:** the paths: the session scratchpad is ~110 characters deep.
- **Cause:** Windows' 260-character path limit, hit by FetchContent's nested directories.
- **Fix:** build in `C:\ob` (Johnny's OK), `core.longpaths` for git.
- **Evidence:** `C:\ob\build-tools.bat`, `build-runner.bat`; 452/452 tests on MSVC.
- **Take-away:** on Windows, build in a short path from the start.

### 2. The spinner that wasn't the problem (ADX's scheduler lock)

- **Seen:** after the archives loaded, the boot sat in `func_001A8BD0` waiting for ADX partition
  10; later a thread at `0x12A908` ran forever.
- **Found by:** gdb Python probes on the GCC build counting kernel calls; disassembly of ADX's
  lock/unlock (`func_0012A6A8`/`func_0012A730`); the decomp's `fsinit.c`.
- **Cause:** three separate things: (a) PS2Recomp refused thread priority 0, so ADX's four server
  threads were never created (`KE_ILLEGAL_PRIORITY`); (b) `ChangeThreadPriority` returned `KE_OK`
  instead of the old priority, so ADX's unlock left the main thread at priority 0 forever; (c) see
  struggle 3. The spinner at `0x12A908` (ADX's priority-8 busy thread, resumed while the lock is
  held) is a crude scheduler lock and is *meant* to run whenever the holder sleeps.
- **Fix:** patches 0010 and 0011.
- **Evidence:** `upstream/ps2recomp/0010-*`, `0011-*`; tests fail on the old code (452/453, 453/454).
- **Take-away:** a "stuck thread" can be the designed behaviour; find who should wake the sleeper.

### 3. The analyzer's "self-modifying code" NOPs

- **Seen:** a CRI transfer object was created with a NULL buffer and the game walked into its own
  NULL trap (`func_00140C58`).
- **Found by:** gdb: object created with `buf=0`; the generated C++ for `func_00140FC0` had no store
  at `0x141034`; `recomp.log`: `Applied patch at 0x141034`.
- **Cause:** `ElfAnalyzer::tryPatchSelfModifyingStore` NOPs any `sw` into an executable section that
  is followed by `j`/`jal`. Outbreak's ELF has one executable section over code and data, so all 37
  hits were ordinary global writes (traced in `results/analyzer-nops-20261021.md`).
- **Fix:** patch 0012: report, never patch (matches the analyzer's own README).
- **Evidence:** 454/455 → 455/455; the results file.
- **Take-away:** a "warning" that silently edits the input is a bug class of its own.

### 4. The heap ended 1 MB early

- **Seen:** `ADXF_ReadNw` with `buf=0`: the font buffer, `malloc(0x11003F)`, returned NULL after the
  game's 22 MB `malloc`.
- **Found by:** gdb FinishBreakpoints logging every allocation and its result.
- **Cause:** PS2Recomp kept its own guest memory (RPC pools, callback stacks) at `0x1F00000+` and
  capped every heap there; on hardware `SetupHeap(-1)` ends at the stack (ps2tek).
- **Fix:** patch 0014: runtime memory moved into the kernel area `0x80000-0x100000`.
- **Evidence:** 456/458 → 458/458.

### 5. The sound bank that never arrived (IOP side)

- **Seen:** `func_001AB9E0` polled the sound driver every vsync, forever.
- **Found by:** PCSX2's debugger (the same code completes in 5 vsyncs; thread table); temporary
  traces in PS2Recomp's IOP: every import call of the driver, every RPC request, the IOP thread
  table at `SendMbx`; disassembly of the driver's RPC server and bank code from `DRIVE.DAT`;
  ps2sdk's libsd source for what `sceSdBlockTransStatus` reads.
- **Cause:** the driver's worker (priority 50) never ran because the ADX streaming thread
  (priority 39) never blocked: PS2Recomp completed each SPU2 streaming DMA in ~14 µs instead of
  ~5 ms and never advanced MADR. Independently, `sceSifGetOtherData` (how the driver reads EE
  memory) was a stub.
- **Fix:** patch 0015.
- **Evidence:** `results/sound-bank-wall-20261021.md`; IOP suite 3/4 → 4/4 with two new tests;
  the bank now loads in 6 vsyncs.
- **Take-away:** when an emulated device finishes work instantly, every thread that waits on it
  becomes a busy thread; strict-priority schedulers then starve everything below.

### 6. Missing disc files

- **Seen:** `sceCdSearchFile failed: \MCICON.BIN;1`, retried forever.
- **Cause:** only 7 of the disc's root entries were in `game_files`.
- **Fix:** extracted `CNICON.BIN`, `DUMMY.DAT`, `GCRES.BIN`, `HDDRC.BIN`, `MCICON.BIN` and the
  `NTGUI2` folder (40 files) from Johnny's ISO with pycdlib, into the git-ignored `game_files`.

### 7. A prompt that nobody could answer (libpad2)

- **Seen:** the memory-card prompt ("Start the game anyway? YES/NO") stayed up for 200 s with a
  temporary auto-press tapping Cross.
- **Found by:** frame dumps (Johnny declined screen access to the runner window, so the runner
  saved its own PNGs); the decomp's `padread.c`/`input.c`: the game uses **libpad2**
  (`scePad2CreateSocket`/`GetState`/`Read`) over libdbc, and PS2Recomp only served libpad, so
  the socket never connected.
- **Cause and fix:** patch 0016: the nine `scePad2*`/`sceDbc*` entry points as runtime stubs on
  top of libpad's ports. Then a second trace (the game's own "pressed" word, `D_003063E0`)
  showed Cross arriving as game bit `0x10` one frame later, and still nothing: this screen's
  yes/no state confirms on bit `0x20`, which `D_00223790` maps to **Circle** (Japanese layout),
  and its cursor starts on NO, so the answer is Left then Circle. With that the prompt is gone.
- **Evidence:** 459/459 tests; `[pad2:peek]` lines in `C:\ob\logs\boot-20.log`; boot 21 searched
  `\PROG\NETDNAS.BIN` and `\PROG\DEMO.BIN` right after the prompt.
- **Take-away:** when input "doesn't work", trace the value all the way to the consumer; here
  the stub was right and the button was wrong.

### 8. The overlay wall

- **Seen:** `[guest-branch:missing-target] kind=IndirectCall op=JALR source=0x1b652c
  target=0x540080` in `boot-21.err`; after it the main thread sleeps and only ADX's spinner
  runs; the screen is black.
- **Cause:** the game loaded `DEMO.BIN` (an `MWo3` overlay, 0x17BC0 bytes of code at 0x540000)
  with its own loader (`overlay.c` `func_001A8870`: memcpy to the load address, `MWo3_Init`)
  and jumped to its entry. PS2Recomp's function table covers the main ELF only
  (`0x100000-0x1D9ABC`); it has never seen the overlay's code.
- **Fix (design agreed with Johnny):** three pieces. `tools/mwo3_to_elf.py` (ours, with a test)
  wraps an MWo3 image as an ELF, header included as a `.mwo3` section. Patch 0018: a `module`
  config makes the recompiler prefix every name and register the overlay's own table with the
  runtime, fingerprinted by the image's first 64 bytes (the MWo3 header, which names the overlay).
  Patch 0017: `hasFunction`/`lookupFunction` fall back to the registered module whose fingerprint
  is in RAM, so DEMO/GAME/NETWK at the same address each get their own code. Nothing in the
  game's loader is hooked. Before trusting the header as a fingerprint I checked `MWo3_Init`
  (disassembled): it only clears bss and runs the static-init range, never writes the header.
- **Evidence:** both tests fail on the old code (runtime 459/460: "image A in RAM: its function
  exists"; recompiler 460/461: the `module` key was ignored and the global table written) and
  pass after (461/461). DEMO: 172 functions, 0 errors. Boot 22 ran DEMO and stopped at the next
  missing target, `0x34A7E0`, in SUBMAIN (loaded at boot, slot 0, `0x340000`); with SUBMAIN
  (190 functions) as a module too, boot 23 reached the title and boot 24 the main menu, with no
  missing function in 300 s.
- **Not covered:** NETDNAS (the DNAS network library, loaded at boot at `0x6C0000`) was never
  disassembled by the decomp: after `0x6C2A08` its splat output is one data block with 1,305
  start markers and no sizes, which `splat_functions.py` doesn't read. Only its first 30
  functions are in the module; offline play hasn't called the rest so far. GAME, NETWK, SINGLE,
  NETHTTP and NETAQ are not built yet (GAME is needed for gameplay).

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| "The game creates its ADX threads at priority 0" (justifying patch 0010) | The 0 came from an analyzer NOP that removed the store of the main thread's priority; the game copies the main thread's priority. Patch 0010 is still needed because PS2Recomp starts the main thread at 0 and refused 0 elsewhere; ps2sdk's own `InitThread` relies on priority 0 | the NOP trace | `results/analyzer-nops-20261021.md` |
| The memory allocator lock was affected by the NOPs | `__malloc_lock`/`unlock` are runtime stubs; the NOPs never ran | the same trace | same file |
| The sound driver was loaded at `0x3D000` (module id 9) | at `0x3D400` (id 10); its import stubs placed it | a stub address from the trace vs. the file | this report |
| Several traced boots "proved" things while their build had failed (stale exe) | `\n` inside C strings written through a Bash heredoc into Python arrives as a real newline; the build logs are UTF-16 and my `grep error` found nothing | reading the log through `iconv`, checking the exe for the trace string | memory note; the Edit tool for such lines |
| My `sceSifGetOtherData` test "failed first" for the right reason | it failed because its import table spelt the library "sifcdm" (byte order); the real failing-first run came after the fix of the name | a step trace of the test module | `ps2xIOP/tests/iop_emulator_tests.cpp` |
| PCSX2 memory breakpoints would count SPU2/SIF DMA kicks | they never fire on hardware registers | 0 hits during known traffic | `results/sound-bank-wall-20261021.md` |

## Still open

- **Where the boot is now:** main menu → Scenario Select → character select, driven blind by the
  temporary auto-press (Left, Circle, Start on a fixed cycle). Nobody has played it with a real
  controller yet.
- **Rendering is the next wall.** Johnny's verdict: "the graphics look like ass, but it looks
  like it is running." Seen in the dumps: the title's background art is missing, glyphs are
  misshapen on the save-data screens, the ACCESS screen has colour fringing, and the
  character-select portraits/panels are corrupted. Causes **unknown**; PCSX2 screenshots of the
  same screens are the reference to compare against. (The white bands in the dumps are the
  temporary dump saving the whole upload texture, not the game.)
- **PCSX2** is left paused in its debugger with the game at the bank-send breakpoints.
- The libdbc actuator functions (`func_00189308`/`func_00189388`, vibration) still run as game
  code against an RPC server that doesn't exist; they return and the game goes on. Unverified
  whether anything else in the game needs them.
- The main thread's initial priority on hardware; Sony's `sceGsSyncV` as a busy-wait vs. a sleep;
  the exact SPU2 streaming rate for ADX's mode (192 KB/s assumed).
- Patches 0010-0018 are tested on MSVC only (plus the IOP suite); Linux/Clang, the GS cache suite
  and Android not re-run. Nothing is pushed; the work is on `claude/windows-boot-adx`.
- Temporary traces still live in the `C:\ob\src` working tree (RPC/thread/import traces, pad2
  trace/peek, frame dump, auto-press); the committed vendored copy and patches are clean.

## Useful for other developers

- `results/analyzer-nops-20261021.md`: how to audit what a recompiler's analyzer silently changed.
- `results/sound-bank-wall-20261021.md`: using PCSX2's debugger as ground truth (EE Threads tab),
  and the IOP traces that show starvation.
- IOP modules in `DRIVE.DAT`: parsing import tables gives each module's needs; the libsd stub
  index pins a module's load address when the loader's log is ambiguous.
- Per-patch proof on Windows: `git am` on a clean upstream clone, build each commit alone,
  `ps2x_tests` + `ctest` for the IOP suite (`-DPS2X_IOP_BUILD_TESTS=ON`).
