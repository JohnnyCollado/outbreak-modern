# Memory cards: PCSX2 .ps2 cards through an emulated SIO2

- **Date:** 2026-10-26
- **Who:** Johnny (developer), Claude Code (side session of the libvu0 audit session)
- **Goal:** The game saves to and loads from a PS2 memory card kept in a folder the user chooses (`outbreak.ini`
  `saves = <path>`), in PCSX2's `.ps2` format so cards move between PCSX2 and the runner.
- **Outcome:** the game formats, saves and loads through PCSX2 `.ps2` cards in the chosen folder (hidden runs,
  entry 8). Patches 0071-0076, `native/pcsx2_sio2`, `saves` / `PS2X_SAVES`, `tools/ob/mcd.py`. All suites green, gate
  3/3 pcsx2-sw (advisories unchanged). Developer's own visible run (play.bat, pcsx2-vk + mvu) 2026-10-26: the runner
  created a blank card in the default folder, the game formatted it and saved, and loaded it after a restart ("looks
  good"); the card holds BISLPM-65428NETBIO. Not yet: Android, a PCSX2 round trip.
- **Versions:** PS2Recomp `eb8688b` + patches 0001-0070 (`C:\ob\up`), PCSX2 v2.9.134 (`C:\ob\pcsx2`), MSVC 19.44,
  game SLPM-65428 v1.04 + English patch v2.1; Windows 11.

## Summary

The game never used PS2Recomp's memory-card code: it links libmc2 and loads its own IOP drivers (sio2man, sio2d,
dbcman, mc2_s1) from `drive.dat`, and the IOP had no SIO2. PCSX2's SIO2 + `.ps2` cards now sit behind the IOP's SIO2
registers and DMA 11/12 (0071), with dmacman slice DMA and SecrAuthCard routed to it; the card folder is `saves` /
`PS2X_SAVES` / `<exe>/memcards` (0072). Two more layers were broken underneath: libdbc's sceDbcInit was an HLE no-op
(0073 lets the game's own run) and dbcman's replies used an unhandled sifman call (0074). A 31-minute rebuild exposed
`ob.sh tools` building a second runner (fixed). Then the IOP itself: RPC handlers could not block (0075, entries 6-7)
and an `LWL`/`LWR` pair lost data (0076, entry 8). After that the game formats a blank card, saves its system data
(`BISLPM-65428NETBIO`) and loads it back after a reboot.

Verification (2026-10-26): ps2x 552/552, iop 5/5, gs_cache 45/45, ob_diag 23/23, pcsx2_vu 3/3, pcsx2_gs 84/84,
pcsx2_spu2 7/7, pcsx2_ipu 22/22, pcsx2_sio2 20/20 (`.run/logs/ob-test-sio2.log`); gate 3/3 gated green, the three
advisories at the same values as before (`.run/logs/ob-gate-sio2.log` vs `C:\ob\logs\ob-libvu0-gate.log`).

## Struggles

### 1. The game never reaches PS2Recomp's memory-card code

- **Seen:** the boot prompt "There is no data on MEMORY CARD slot 1" (`reports/2026-10-24-android.md` entry 16),
  even though PS2Recomp has a folder-backed libmc HLE (`ps2xRuntime/src/lib/Kernel/Stubs/MemoryCard.cpp`,
  `ps2xIOP/src/modules/mcserv.cpp`, root `<game folder>/mc0`).
- **Found by:** reading code and the disc. The ELF says `PsIIlibmc2 2811`: the game uses **libmc2**, not libmc.
  `decomp.toml` lists `sceMc2*`, `_sceMcFat*`, `_sceMcp*` under `stubs` (lines ~601-660), but no runtime handler
  exists, so they are recompiled game code (`C:\ob\work\output\sceMc2Init_0x1808c8.cpp`). The card filesystem runs on
  the EE. The IOP side is the disc's own `iop/modules/drive.dat`: sio2man, sio2d, Dbc_Manager, ds2u_s1, **mc2_s1**,
  usbd, libsd, modhsyn, modmsin, CRI ADX, loaded from buffers (`load-buffer-emulated`, `C:\ob\logs\boot-gate.log`), so
  real IOP code (the HLE module keys in `iop_module_manager.cpp:20-52` match by name and never apply to them).
- **Cause:** the IOP emulates no SIO2 registers (`ps2xIOP/src/emulator/core/iop_memory.cpp`: only INTC and the SPU2
  DMA channels); `dmacman`'s imports all return 0 (`iop_emulator.cpp:385`), so `sio2man`'s `sceSetSliceDMA` /
  `sceStartDMA` (ordinals 28 / 32, ps2sdk `iop/system/dmacman/src/exports.tab`) never program DMA 11/12; `secrman`
  answers only ordinals 4/5, so `sio2d`'s `SecrAuthCard` (ordinal 6) is an unhandled import returning 0 (card
  authentication fails). The real driver finds no card.
- **Fix:** (planned, approved 2026-10-26) PCSX2's SIO2 + memory-card code as an external SIO2 device (patch 0071),
  `dmacman` slice DMA for channels 11/12 and `SecrAuthCard` through the device; `saves` key (0072).
- **Evidence:** this entry; plan in the session notes (`memory-cards-plan.md`).
- **Take-away:** for any subsystem, check which library version the game links (`PsIIlib*` strings) and whether its
  IOP side is a disc module (real code) before assuming an HLE covers it.

### 2. libmc2's libdbc client is never bound: sceDbcInit is an HLE no-op

- **Seen:** while wiring the SIO2: libmc2's own layer calls recompiled libdbc (`func_188738` sceDbcCreateSocket,
  `func_1889E0` sceDbcGetConnection, `func_189088` sceDbcSendData3, from `func_00187388/187588/187770` in
  `C:\ob\work\output`), but `sceDbcInit`/`sceDbcEnd` are runtime stubs that return 1 and do nothing
  (`ps2xRuntime/src/lib/Kernel/Stubs/Pad2.cpp:51-63`, patch 0016's libpad2 HLE; `decomp.toml` stubs list lines
  234-235).
- **Found by:** following the `jal` targets in the generated code against the analyzer's names in `decomp.toml`.
- **Cause:** the real sceDbcInit binds libdbc's SIF RPC client to dbcman (SID 0x80001300); the no-op leaves it
  unbound, so every libmc2 socket call would fail before reaching the IOP, SIO2 or not. Pads never noticed: libpad2
  is HLE'd whole.
- **Fix, first try (not enough):** `ob.sh recompile` dropped the two names from the stubs list, as it does for the
  libmpeg/libipu stubs. The recompile then changed no generated file ("sources: copied 0"): `sceDbcInit_0x188440.cpp`
  was still `ps2_stubs::sceDbcInit(...)`. The recompiler stubs any function whose *name* is a runtime stub
  (`ps2xRecomp/src/lib/ps2_recompiler.cpp:2236-2247`, `ps2_runtime_calls::isStubName`), and `main-functions.csv`
  names it `sceDbcInit`; the libmpeg functions are `func_...` there, which is why their removal had worked.
- **Fix:** patch 0073, `[general] recompile = [...]` (name or name@address): those functions are translated from the
  game even when the runtime has a stub of the name (an explicit `stubs` entry still wins). `ob.sh recompile` adds
  `recompile = ["sceDbcInit", "sceDbcEnd"]` and keeps dropping them from `stubs`. The emulated (disc) dbcman answers
  before PS2Recomp's dbcman HLE (`ps2xIOP/src/iop_subsystem.cpp:239-250`). Other libpad2 games keep the HLE.
- **Evidence:** ps2x "recompile config keeps a game function that has a runtime stub's name"; `ob.sh recompile`
  prints `sceDbcInit/End calls into runtime stubs: 0`.
- **Take-away:** an HLE that no-ops an init function also breaks every other library sharing that init; and check
  the generated file, not the config, to know whether a function is stubbed.

### 3. The IOP test's return value lost after a load (test bug)

- **Seen:** the 0071 module test returned 0x1 instead of 0x11, although the device saw the right DMA and auth.
- **Found by:** storing intermediate values to fixed IOP addresses from the test module.
- **Cause:** my hand-assembled test code read `t1` in the instruction right after `lw t1`: the R3000's load delay
  slot, which PS2Recomp's IOP interpreter models. Not an emulator bug.
- **Fix:** a `nop` after the load (`ps2xIOP/tests/iop_emulator_tests.cpp`, writeSio2DriverIrx).
- **Evidence:** IOP suite 5/5; with the old `iop_emulator.cpp` (no dmacman/secrman part) 4/5, "SecrAuthCard must
  ask the external SIO2 and sceSetSliceDMA must succeed on channel 11" (`C:\ob\logs\ob-sio2-ioptests-without.log`).
- **Take-away:** hand-written MIPS I code for the IOP needs load delay slots.

### 4. A 31-minute rebuild after a recompiler change

- **Seen:** `ob.sh tools` + `ob.sh recompile` after patch 0073 took 31 min 30 s (five-minute ceiling).
- **Found by:** step counts in the logs: `C:\ob\logs\ob-tools.log` 345 steps ending in
  `Linking CXX executable ps2xRuntime\ps2EntryRunner.exe`, ~18 min; `ob-runner-build.log` 368 steps, 329 game units,
  732 s.
- **Cause:** `cmd_tools` built every target of `C:\ob\t`, including a second runner nobody runs (`cmd_test` already
  avoided this for its own build). The runner's 329 game units rebuilt once because the generated function header
  changed (sceDbcInit now translated): the documented one-time full rebuild.
- **Fix:** `cmd_tools` builds `ps2_analyzer ps2_recomp` only; an unchanged run is now 33 s (CMake's configure check).
- **Evidence:** `tools/ob.sh` cmd_tools.
- **Take-away:** a tools tree that includes the runner rebuilds it on every header change; build named targets.

### 5. The disc's dbcman cannot reply: sceSifSetDmaIntr unhandled

- **Seen:** first hidden live-pad boot on a blank card (`PS2X_SAVES=C:\ob\saves-test`, tag `mcsave`): the same
  "There is no data on MEMORY CARD SLOT 1" screen as the PCSX2 reference with slot 1 off
  (`C:\ob\frames-mcsave\prompt1.png` vs `C:\ob\ref\savedata-a.png`), and `[IOP Kprintf] dbcman: sceSifSetDma faild`
  three times in `C:\ob\logs\boot-mcsave.err`. Never seen before: until sceDbcInit ran (entry 2) dbcman was idle.
- **Found by:** a temporary log on each failure path of PS2Recomp's `sceSifSetDma` (none fired), then dbcman's import
  list (`sifman` 5, 7, 8, 29, 32) against ps2sdk `iop/system/sifman/src/exports.tab`: 32 = `sceSifSetDmaIntr`.
- **Cause:** `IopRpcBridge::dispatchSifManImport` (`iop_rpc.cpp:28-131`) has no case 32; the default returns 0
  ("failed").
- **Fix:** patch 0074: ordinal 32 does ordinal 7's transfer, then queues `func(data)` as a guest callback.
- **Evidence:** IOP suite 5/5, 4/5 without; boot `tmpdbg2`: no dbcman error.
- **Take-away:** the import table of each disc module is the checklist of what the IOP must answer.

### 6. libmc2 sent its requests, but they never reached the card

- **Seen:** blank card, scripted boots `tmpdbg2`-`tmpdbg11`: still the "no data" screen; libmc2's GetInfo result at
  `0x308d9c` = `0x8101006F`; the card got only probes, `27`, `BF` and `28` polls, never `26` (specs) or reads.
- **Found by:** temporary prints at the entry of every libmc2/libdbc function in the generated code and on the EE's
  `sceSifCallRpc`: libmc2 runs its worker thread, `GetConnection` reads "connected" (status table `0x276580` from
  dbcman), and `SendData3` calls go out on a second client bound to SID `0x8000131C` (12 per boot) and are handled by
  the disc's dbcman (handler at module offset `0x1BB8`, disassembled with capstone from `drive.dat`). dbcman calls
  `mc2_s1`'s device entry 5 (`0x1EB0`) inside the handler; an IOP import trace inside the RPC showed `sio2man`'s
  transfer (import 51) set an event flag for `sio2man`'s worker thread, then `WaitEventFlag` returned -418 at once.
- **Cause:** PS2Recomp ran an emulated server's RPC handler as a nested call with no thread (`iop_rpc.cpp:322`), so a
  handler that blocks on another IOP thread cannot work.
- **Fix:** entry 7 (patch 0075).
- **Take-away:** an RPC handler is thread code; "it returned" is not "it worked".

### 7. RPC handlers as IOP threads (patch 0075), and an IOP heap that never reuses blocks

- **Seen:** first version: the boot stalled before the save-data screen (exit 3); the EE polled SID `0x90000200` and no
  handler ran after a few hundred calls.
- **Found by:** a print on each threaded handler run: `createInternalThread` started failing.
- **Cause:** a fresh 8 KB stack per call from `IopMemory::allocate`, a bump allocator (`iop_memory.cpp:374-395`): freed
  blocks behind the cursor are never reused, so the heap ran out.
- **Fix:** handler threads take stacks from a pool kept by the emulator (allocated once, reused, dropped on IOP reset).
  Each handler runs at the priority of the thread that registered its server, stepped 64 cycles at a time until it
  returns (budget one IOP second, logged). After it: real card I/O on the wire (`11` probe, `23` + `43` reads,
  `26` specs with the right answer `00 02 10 00 00 40 00 00 52`).
- **Evidence:** IOP suite 5/5, 4/5 without (`ob-0075-without.log`); 300 consecutive calls test; ps2x 552/552, all
  suites green, gate 3/3 with the same advisory values.
- **Take-away:** check what an allocator does on free before allocating per call.

### 8. The specs reply lost its first half: LWL/LWR back to back (patch 0076)

- **Seen:** with real I/O, libmc2 still `0x8101006F`, also on a formatted card made with `tools/ob/mcd.py format`.
  libmc2's `func_00182F20` took the error branch at `0x183008`: the card info's first word was 0. The reply blocks
  that dbcman DMAs to `0x26FE80`/`0x26FF00` (alternating, all transfers OK) carried page size 0 and `0xD00`.
- **Found by:** `mc2_s1`'s command 3 (`0x28C8`) copies 8 bytes from its parser `0x1398`, which checks the XOR and the
  terminator and then copies the specs bytes from the unaligned address `0x3D33` with `lwl a3,-5(s1)` /
  `lwr a3,-8(s1)` back to back.
- **Cause:** the interpreter models the R3000 load delay but `LWR` merged into the register's old value, not the
  value `LWL` had just loaded (still pending). psx-spx "Unaligned Load/Store": no delay is needed between an
  `lwl`/`lwr` pair on one register.
- **Fix:** patch 0076: `LWL`/`LWR` merge into the pending load when it targets the same register.
- **Evidence:** IOP suite 5/5, 4/5 without (`ob-0076-without.log`); all suites green, gate 3/3 (`ob-0076-verify.txt`).
  In the game (hidden live-pad runs, 2026-10-26): formatted card: OPTIONS > SAVE/LOAD > SAVE DATA "Create the
  system data?" YES -> "Data has been saved" (`C:\ob\frames-mcplay\s3.png`); the card then holds
  `BISLPM-65428NETBIO` (system file 261,320 bytes, two icons, `icon.sys`); after a reboot on that card LOAD DATA ->
  "The Data is loaded." (`C:\ob\frames-mcload\l2.png`). Blank card: SAVE DATA -> "not formatted. Will you format it?"
  YES -> "Data has been saved" (`C:\ob\frames-mcblank\k2.png`), same folder on the card.
- **Take-away:** the IOP interpreter's instruction semantics are worth a test per odd case (delay slots, pairs).

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Plan: a missing slot 1 card is created "blank, formatted" | PCSX2 creates new cards as 8 MB of 0xFF (`MemoryCardFile.cpp:1071-1084`, `FileMemoryCard::Create` :380-399): unformatted, since "formatted" = the "Sony PS2 Memory Card Format" superblock at offset 0 (:874-888). The game formats it, as a new card on a PS2 | Reading PCSX2's card creation while wiring the default | This report; `native/outbreak.ini.example` says "created blank; the game then offers to format it" |

## Still open

- A PCSX2 round trip (open the runner's card in PCSX2 and back) is unverified.
- Android: `native/pcsx2_sio2` not built for arm64 yet (`ob.sh android` not run); not on the Odin.
- PCSX2 folder cards (MemoryCardFolder.cpp) are stubbed out (needs rapidyaml); `.ps2` files only.
- Card files are not closed at exit (`_Exit`): PCSX2's close-time checksum at 0x210 is never written. PCSX2 reads such
  cards fine (the value only feeds its change detection); unverified with a real PCSX2 round trip.

## Useful for other developers

- `mcd_info.py` (session scratch): formatted or not, root directory of a `.ps2` card; `--blank` makes PCSX2's blank
  card (8,650,752 bytes of 0xFF). The developer's `ps2_emu/memcards/Mem1.ps2`/`mem2.ps2` are blank PCSX2 cards.
- `irx_imports.py` (session scratch, method below): list an IRX bundle's import tables by scanning for the
  `0x41e00000` magic, then `jr ra` / `addiu $0,$0,ordinal` pairs. That is how `sio2man`'s `dmacman` 28/32-35 and
  `sio2d`'s `secrman` 6 were found.
