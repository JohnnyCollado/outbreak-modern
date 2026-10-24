# Replacing the emulated IOP with native services (HLE): feasibility study (2026-10-24)

Read-only study. No code was changed in the project or in `C:\ob\up`. Sources: the repo docs and reports, the
logs in `C:\ob\logs`, PS2Recomp in `C:\ob\up` (our patched tree, including its git history), the splat output
`C:\ob\splat\SLPM_654.28`, the disc modules in `game_files\iop\modules` (read, strings and ELF offsets only), and
GitHub pages fetched on 2026-10-24. Other domains: not opened.

Terms used below: the **IOP** is the PS2's second CPU (a PlayStation 1 MIPS chip) that runs sound, disc, pad,
memory card, USB and network drivers. Its drivers are **IRX** modules. The EE (main CPU, our recompiled game)
talks to it over **SIF** (the EE-IOP link): **RPC** calls (call a function on the other side, with a send and a
receive buffer, identified by a server id **SID** and a function number **fno**) and SIF **DMA** (a block copy
from EE memory into IOP memory). **HLE** (high-level emulation) means implementing what a service does in native
code instead of running its original machine code on an emulated CPU. The **SPU2** is the PS2 sound chip.

## Verdict

**Partly yes, and much of it is already done; full replacement is not worth it now.**

1. The "easy" services (CDVD reads, pad, memory card, file I/O, USB keyboard, the IOP kernel itself) are
   **already HLE** in PS2Recomp: on the EE side as stubs in the recompile config, and inside the IOP as a virtual
   kernel. They do not cost IOP interpreter time when the game calls them.
2. What still runs on the interpreter is **the game's own sound stack** (Capcom's SoundDriver = TSNDDRV, Sony's
   `modhsyn`/`modmsin` synthesizer modules, Sony's `libsd`, CRI's ADX driver) plus a set of **driver modules
   that are loaded but probably serve nobody** (SIO2/pad/dbcman/mc2man from `DRIVE.DAT`, two USB stacks, dev9).
3. Replacing the sound stack at the RPC boundary is possible (PS2Recomp itself had silent HLE stand-ins for
   TSNDDRV and CRI DTX until 2026-09-19), but the sound driver's behaviour is undocumented, the EE depends on its
   timing (the 2026-10-21 bank wall), and without SPU2 output there is no way to check that a replacement sounds
   right. Doing it before sound exists would trade a known-correct path for an unverifiable one.
4. **Performance:** on Windows J's Bar already runs at the 60 vsync/s cap with the GS thread on, so any IOP
   win is CPU headroom, not frame rate. The whole IOP is ~24% of the game thread's samples (~0.14 of one core).
   About half of that is interpreter overhead that a **cheaper interpreter loop** can remove with no behaviour
   change; unloading idle modules may remove more. Native HLE of the sound stack would remove the rest but adds
   a native mixer's cost back.

Recommended path: measure per module (stage 0), make the interpreter's hot loop cheaper (stage 1), stop loading
modules nobody uses (stage 2, per module, with fallback), add SPU2 output with the real sound IRX still
interpreted (stage 3, M5), and only then consider native replacements for CRI ADX (stage 4) and TSNDDRV
(stage 5), each switchable back to the interpreter.

## 1. Inventory

### 1.1 Modules loaded at boot

`C:\ob\logs\boot-playiso.err:17-58` (current build, ISO boot) shows 14 IRX loads, by id and IOP address only.
Names come from matching the load order, the module start messages, and the RPC server addresses against
`game_files\iop\modules\drive.dat` (11 ELF modules at offsets 0x80 .. 0x4d5c0; names from each module's
`.iopmod` / `PsII...` strings, read with a small script, not committed). `sys.c` (`C:\ob\decomp-src\sys.c:20-93`)
reboots the IOP with `DNAS280.IMG`, copies `DRIVE.DAT` to the IOP heap and starts its modules with
`sceSifLoadStartModuleBuffer`, then loads `USBD.IRX` and `USBKB.IRX` (`docs/android-port-map.md:140-142`).

| id | base | module (source) | what it does | IOP threads (entry, prio) | EE side today |
|---|---|---|---|---|---|
| 1 | 0x10000 | sio2man (DRIVE.DAT #1) | SIO2 bus manager (pad/memory card port) | 0x1039c p16 | n/a |
| 2 | 0x11000 | sio2d (#2) | SIO2 device driver | 0x11a7c p46 | n/a |
| 3 | 0x13f00 | dbcman "Dbc_Manager" (#3) | Sony's controller socket manager (libdbc) | 0x15c7c/0x15d14/0x15dac p20 | `sceDbc*` stubbed (EE HLE) |
| 4 | 0x1a200 | pdman (#4) | pad driver under dbcman | 0x1a360 p46 | `scePad2*` stubbed |
| 5 | 0x1c400 | mc2man (#5) | memory card manager (libmc2) | 0x1c808 p104 | `sceMc2*` stubbed |
| 6 | 0x21100 | usbd "USB_driver" 1.1.0 (#6) | USB host stack | 0x25ae4 p30, 0x25b34 p36 | n/a |
| 7 | 0x27800 | libsd "Sound_Device_Library" (#7) | Sony's SPU2 access library | none | n/a |
| 8 | 0x2d200 | modhsyn "SPU2_Synthesizer_Module" (#8) | Sony's sequenced-sound synthesizer | none | n/a |
| 9 | 0x3d000 | modmsin "Midi_Message_Input_Module" (#9) | MIDI message input for modhsyn | none | n/a |
| 10 | 0x3d400 | **TSNDDRV** "SoundDriver (C)CAPCOM" (#11) | Capcom/Tamsoft sound driver, RPC SIDs 0 and 1 | 0x3d448 p47, 0x3d618 p31, 0x3e8cc p50, 0x3e47c p32 | `sdr.c` RPC (not stubbed) |
| 11 | 0x4fa00 | **CRI ADX Driver 8.82** (#10) | PCM stream output (PSM), DTX transport, SID 0x90000200 | 0x4fa60 p39, 0x500e4 p48, 0x53920 p39 | CRI ADXT/ADXF on the EE (not stubbed) |
| 12 | 0x62100 | dev9 (`dev9.irx`) | expansion bay (network adapter / HDD) | none seen | prints "unknown dev9 hardware" |
| 13 | 0x64900 | usbd (`USBD.IRX`) | a second USB stack | 0x692e4 p30, 0x69334 p36 | n/a |
| 14 | 0x6b000 | usbkb 1.03 (`USBKB.IRX`) | USB keyboard, RPC SID 0x80000211 | 0x6b6f8 p50 | `sceUsbKb*` stubbed; `func_001D8230` binds 0x80000211 |

Thread list: `C:\ob\logs\boot-t2c.err` `[IOP:thread] create` lines (2026-10-21 trace build). Mapping a thread to
a module is by entry address inside the module's range: inferred, not traced. The printed message order shows
the start message is logged before its `loaded IRX` line (the TSNDDRV and CRI RPC server addresses 0x3fb28 and
0x50010 fall in ids 10 and 11), so ids 10/11 are TSNDDRV/CRI even though `DRIVE.DAT` stores them CRI first:
inferred from addresses, not from a name log. Unverified: why id 10's base follows id 9 directly although
TSNDDRV is #11 in the file (load order in `sys.c` vs file order not checked).

Also on disc, not loaded in single player: `atad`, `hdd`, `pfs`, `smap`, `netcnf`, `netcnfif`, the
`NETMOD\*.IRX` stack (AVETCP/AVEPPP, modem drivers), `SpuRegMon.irx` (`game_files\iop\modules`).

### 1.2 RPC servers the EE binds

From the splat assembly (`sceSifBindRpc` call sites and the SID loaded into `$a1`, script over
`C:\ob\splat\SLPM_654.28\**\text.s`):

| SID | binder | serves |
|---|---|---|
| 0x80000592/593/595/597/59A/59C | `sceCdInit` 0x1010A8 and neighbours 0x100838..0x101580 | Sony cdvdfsv (CD commands). **EE stubs** (`C:\ob\work\decomp.toml:23ff`: `sceCdInit@0x001010A8`, `sceCdRead`, ...) |
| 0x80000001/3/6 | 0x1184E0, `sceSifInitIopHeap` 0x11B5B4, 0x11BAB8 | Sony file I/O, IOP heap, module loader: runtime HLE |
| 0x80001300/131B/131C | `sceDbcInit` 0x1884BC.. | dbcman: `sceDbcInit` is an EE stub (returns 1, `ps2xRuntime/src/lib/Kernel/Stubs/Pad2.cpp:51-56`) |
| 0x90000200 (from `D_001FE994`, `en\main\data\vu_data.data.s:55608`) | `func_00139038` | CRI DTX on the ADX driver |
| 0 and 1 | `func_001A6630` = `SdrBind` (`docs/android-port-map.md:83`) | TSNDDRV command / state channels |
| 0x80000211 | `func_001D8230` | usbkb |
| 0x01270030 (computed) | `func_001C3BD0` | unverified (value built at run time) |
| 0x80001101 | NETWK `func_005CDF00` | network overlay (not reached in single player) |

### 1.3 RPC calls actually made (traced)

The only RPC traces on disk are from 2026-10-21 trace builds (boot to title; no J's Bar trace exists). Counts in
`C:\ob\logs\boot-t2c.err` (grep `[IOP:rpc] call`):

| SID | fno | count | send/recv bytes | server |
|---|---|---|---|---|
| 0 (TSNDDRV commands) | 0 | 13 | 16-32 / 0 | IRX fn 0x3fb28 |
| 0 | 0x8000131A | 1 | 640 / 640 | 0x3fb28 |
| 1 (TSNDDRV state) | 0x8000000C | 9 | 16 / 16 | IRX fn 0x3fb9c |
| 1 | 16 | 1 | 16 / 16 | 0x3fb9c |
| 0x90000200 (CRI DTX) | 1024, 1058 (5 each), 1032 (3), 2 (2), 1027, 1034, 1036 | 18 | 0-16 / 0-4 | IRX fn 0x50010 |
| 0x80000211 (usbkb) | 1 | 2 | | IRX fn 0x6b76c |

Older traces (`boot-10.err`..`boot-26.err`, same day, before `sceDbcInit` was stubbed) also show dbcman calls
(SID 0x80001300, 640-byte packets, IRX fn 0x15b70) and 363 TSNDDRV state polls per run.

SIF DMA traffic (same log, `[sceSifSetDma:DESC]`, 11,951 transfers): 5,392 pairs of 0x880-byte transfers from
EE pc 0x13901C alternating into IOP 0x1C8340 / 0x1DD9C0, and ~600 0x800/0x400-byte transfers from pc 0x13867C
into a ring at 0x1C9580..0x1D1280. Both pcs are inside the CRI library next to the DTX binder. Reading: the EE
decodes ADX itself and streams the result to the IOP driver in double buffers; the IOP driver then feeds the
SPU2 with block DMA. **Inferred** from the pattern and the driver's "PSM thread" / `psmout_*` / `psmmix_*`
symbol strings in `drive.dat`; not traced inside the driver.

### 1.4 HLE that PS2Recomp already has

- **IOP virtual kernel** (imports the IRX modules call, served natively): sysmem, loadcore, intrman,
  thbase/thsemap/thevent/thmsgbx, sifman, sifcmd, ioman, modload, vblank, timrman, dmacman, stdio, sysclib,
  heaplib, secrman, and **cdvdman** (`sceCdRead/Seek/Sync/SearchFile/Callback/...`,
  `ps2xIOP/src/emulator/imports/iop_cdvd.cpp:216-342`); dispatch in `ps2xIOP/src/emulator/iop_emulator.cpp`
  (library switch ~258-360).
- **ROM modules "loaded" without code** (`ps2xIOP/src/iop_module_manager.cpp:13-53`): sio2man, padman, mcman,
  mcserv, libsd, cdvdman, cdvdfsv, dev9, usbd, ... A load of `rom0:` names returns an HLE id.
- **RPC-level services** MCSERV (0x80000400/480), LIBSD (0x80000701), DBCMAN (0x80001300) (`ps2xIOP/README.md:13-27`,
  `src/modules/{mcserv,libsd,dbcman}.cpp`). They are fallbacks: "A physical IRX RPC server is authoritative for
  its SID" (`README.md:11`). In Outbreak the disc's own dbcman/mc2man are loaded, so these stay dormant.
- **EE-side stubs** replacing the Sony libraries at the call (`C:\ob\work\decomp.toml` `stubs = [` at line 23):
  `sceCd*`, `scePad2*`, `sceDbc*`, `sceMc2*`, `sceOpen/Read/Lseek/Close`, `sceUsbKb*`, all `sceSif*`
  (`ps2xRuntime/src/lib/Kernel/Stubs/{CD,Pad2,MemoryCard,FileIO}.cpp`). So the game's pad, memory card and disc
  reads never reach the IOP modules that are loaded for them.
- **SPU2:** registers only, no sound (`docs/ARCHITECTURE.md:34`). SPU2 DMA completion is timed: block
  transfers take playback time at 192 IOP cycles per byte (`ps2xIOP/src/emulator/core/iop_memory.cpp:13,251-290`,
  our patch 0015 from the bank wall).
- **Removed HLE** (prior art in our own base): upstream commit `39251a0` (Ran-j, 2026-09-19, "feat: remove LLE
  IOPs") deleted game profiles with native stand-ins `src/modules/tsnddrv.cpp` (591 lines: TSNDDRV command
  lengths, `SdrBgmReq/BgmStop/HDDataSet/SeAllStop`, status block 0x42 bytes, address table), `cri_dtx.cpp`
  (1306 lines: CRI DTX protocol `SJX_CREATE/DESTROY/RESET`, `PS2RNA_CREATE`, `SJRMT_*` ring-buffer chunk calls),
  `sdrdrv.cpp`, `clfile.cpp`, `sound_update_stub.cpp`, and a native plugin loader (8,220 lines removed; profile
  `recvx-us` = Code Veronica X US, which also uses TSNDDRV and CRI DTX). They only kept the protocol state
  consistent: no audio (no mixer or host-audio calls in either file; `git show 39251a0^:ps2xIOP/src/modules/...`).
  The current README states the policy: "Game-specific IOP code executes from IRX modules"
  (`ps2xIOP/README.md:7-25`). Why upstream switched: not stated in the commit; my reading is that running the
  real modules fixed games the stand-ins could not (unverified).

## 2. Prior art

- **Play!** (`github.com/jpd002/Play-`, `Source/iop/`, fetched 2026-10-24): HLE modules for Sony's ROM services
  (`Iop_Cdvdman`, `Iop_Cdvdfsv`, `Iop_PadMan`, `Iop_McServ`, `Iop_LibSd`, `Iop_SifMan`, `Iop_SifCmd`, `Iop_Modload`,
  `Iop_Ioman`, ...) plus real hardware models `Iop_Spu`, `Iop_Spu2`, `Iop_Spu2_Core`, `Iop_Dmac`, `Iop_Sio2`.
  `IopBios.cpp`: a loaded module is replaced only if its name is in `m_hleModules` (filled in `Reset`:
  `rom0:SIO2MAN`, `PADMAN`, `XSIO2MAN`, `XPADMAN`, `XMTAPMAN`, `MCMAN`, `MCSERV`, `XMCMAN`, `XMCSERV`, `CDVDMAN`,
  `CDVDFSV`); any other IRX is loaded with `LoadExecutable` and run on its IOP CPU (`Iop_SubSystem.cpp`:
  `CGenericMipsExecutor`). No game-specific sound driver HLE seen. So Play! is the same split PS2Recomp already
  has: HLE for Sony's ROM services, real execution for game drivers, and an emulated SPU2 to make them audible.
  Whether its IOP executor is a JIT: not verified here (the excerpt does not say). Play!'s known limitations
  list: not opened.
- **REO** (`github.com/sp00nznet/reo`, README and `docs/architecture.md`, fetched 2026-10-24): the game's own
  IOP modules "run on PS2Recomp's IOP interpreter" (CRI ADX/DTX, Capcom's sound driver, USB, pad and memory
  card drivers); SPU2 output "not implemented" (README status table, build of 2026-10-19). Its architecture page
  lists an "IOP HLE" row with `ps2xIOP/src/modules/cdvdfsv.cpp` for file lookups. Its reasons for choosing the
  interpreter: not written in either page.
- **PCSX2**: emulates the IOP fully (interpreter and an x86 recompiler) and the SPU2; HLE only for host file
  access. From general knowledge; pages not opened in this study.
- **The removed PS2Recomp profiles** (above) are the only prior art for HLE of *these* drivers (TSNDDRV, CRI DTX),
  and they were silent stand-ins, then dropped.

Conclusion: no PS2 emulator or recomp I know of replaces a game's own sound IRX with native code and keeps
the sound; the established pattern is HLE for Sony's ROM services, real execution for game drivers.

## 3. The hard part: game drivers that program the SPU2

The two drivers that matter are the ones not stubbed: TSNDDRV (with modhsyn/modmsin/libsd under it) and the
CRI ADX driver.

**Can they be replaced at the RPC boundary?** In principle yes: the EE only sees RPC replies, the bytes the IOP
writes back into EE memory, completion semaphores and callbacks. A native replacement has to produce the same
replies at the same (guest) times and actually make the sound. Per driver:

- **CRI ADX driver** (smaller job). The EE does the ADX decoding (inferred, 1.3); the driver receives PCM
  chunks and mixes them to the SPU2 (`psmout`, `psmmix` symbols). The protocol side is known from the removed
  `cri_dtx.cpp` (SJX/SJRMT ring buffers). A native version: take the PCM from the DTX ring, hand it to a host
  mixer, and report consumption **at the real playback rate**. Effort: medium (the DTX state machine is ~1,300
  lines of prior art to re-derive; the PCM format, channel layout and the 0x880-byte block meaning are
  unverified).
- **TSNDDRV** (large job). The EE sends packed command streams (`sdr.c`: `SdrSeReq`, `SdrSendReq` ...,
  `docs/android-port-map.md:77-93`). The driver plays sound-bank waves and sequences through Sony's modhsyn
  (an HD/BD/SQ synthesizer: envelopes, pitch, voice allocation over 48 SPU2 voices) and sets SPU2 reverb
  (`SdrSetRev`). TSNDDRV's IOP side is not decompiled (`docs/milestones/m5-sound-saves-movies.md:52-55`); its
  command semantics are known only from how the EE packs them. A native version means reimplementing TSNDDRV,
  modhsyn and the SPU2 voice/reverb model. Effort: large, with sound differences likely ("Some effects may sound
  different", m5 doc).

**Timing and behaviour the EE depends on** (what a replacement must reproduce):

- **Asynchronous completion with real duration.** The 2026-10-21 bank wall: the EE polled the driver every vsync
  forever (`func_001AB9E0`) because the ADX streaming thread never blocked when SPU2 DMA finished in ~14 us
  instead of ~5 ms, starving TSNDDRV's worker (`reports/2026-10-21-windows-port-bank-wall.md:86-99`,
  `results/sound-bank-wall-20261021.md`). A native replacement that finishes work instantly reproduces exactly
  this class of bug, on the EE side.
- **State polled per vsync**: TSNDGET state (SID 1, fno 0x8000000C, 16-byte reply; 363 calls per early run in
  `boot-10.err`); the 0x42-byte status block and address table in the removed `tsnddrv.cpp` show the EE reads
  driver-owned memory too (`kGetStatusAddressFunction` 0x12, `kGetAddressTableFunction` 0x13).
- **IOP memory addresses handed to the EE**: the EE DMAs into IOP buffers it gets from RPC replies (0x1C8340,
  0x1DD9C0). A native driver must allocate them in emulated IOP memory, keep the layout plausible, and accept SIF
  DMA into them.
- **The driver reads EE memory itself** with `sceSifGetOtherData` (bank upload; bank wall fix).
- **Stream pacing**: ADX playback position drives music/voice timing (and Sofdec movie audio); the EE's ADX
  server threads sleep on it (EE thread table in the bank-wall result, lines 38-45).
- **RPC completion today is synchronous**: `SifCallRpc` runs the IOP server and completes the client before
  returning (`ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp:371`, `handleRpc` at 575, callbacks after). The
  drivers' own worker threads (woken with `SendMbx`) do the slow work later in IOP time; a native replacement
  needs its own notion of "later" tied to the batched IOP clock (patch 0042, `reports/2026-10-23-gameplay.md`
  entry 26).

## 4. Performance

**Where IOP time goes now** (game thread, exclusive samples, 30 s in J's Bar):

| function | batched, GS inline (`hostprof-iopbprof-on.txt`) | batched, GS thread (`hostprof-gst-on.txt`) |
|---|---|---|
| `IopCpuCore::executeInstruction` | 5.7% | 6.6% |
| `servicePendingDmaInterrupts` | 2.6% | 3.2% |
| `schedulePendingDma` | 2.5% | 2.7% |
| `IopMemory::takeDmaStart` | 0.6% | 0.8% |
| `IopMemory::read32` | 2.5% | 3.2% |
| `IopEmulator::Impl::step` | 1.9% | 2.7% |
| `IopImportRegistry::decode` | 2.1% | 2.3% |
| `runCycles`, `advanceIopEeCycles`, `beginNextReady` | 2.6% | 2.5% |
| **IOP total** | **~20.5%** | **~24%** |

The GS-thread run's game thread used 17.05 CPU-s in 30 s, so the IOP is ~4.1 CPU-s per 30 s, **~0.14 of one
core** on the developer's PC. J's Bar runs at the 60 vsync/s cap there (`gst-on3/4` median 60,
`reports/2026-10-23-gameplay.md` entry 27). So on Windows no IOP change can raise the frame rate; it frees
headroom (useful for higher resolution, texture packs, slower PCs). On Android the IOP's share is not measured
(`reports/2026-10-24-android.md` has no profile with the IOP split): unknown, probably similar or larger with no
IOP JIT (guess).

**Where the overhead comes from** (code read, `ps2xIOP/src/emulator/iop_emulator.cpp`):

- `step` calls `schedulePendingDma()` after **every** instruction (line 410), and every IOP memory write does
  it again (`write8/16/32`, lines 169-185), although only a write to the SPU2 DMA control registers can start a
  DMA (`iop_memory.cpp:251`).
- `runCpu` walks the pending-DMA map after every instruction while any DMA is pending (lines 425-426); with
  SPU2 streams now taking playback time (patch 0015), a DMA is pending most of the time, so this runs almost
  per instruction.
- `step` asks the import registry whether the pc is an import stub on every instruction (line 397).

These three (~8-9% of the game thread together with `takeDmaStart`, plus part of `step`) are pure overhead and
can be cut to near zero without changing what the IOP computes: start-DMA check only from the register write
path, a "next DMA deadline" compare instead of a map walk, an import-stub address range or bitmap test. That is
roughly **half the IOP cost** (estimate from the table, not measured).

**Expected gains by option** (estimates; basis in brackets):

| option | IOP share of game thread after | basis |
|---|---|---|
| today | ~24% | measured (hostprof-gst-on) |
| stage 1: cheaper interpreter loop | ~11-14% | overhead rows above removed, `executeInstruction` + `read32` + scheduling left (estimate) |
| stage 2: idle modules not loaded | unknown, between 0 and most of the rest | needs the per-module split (stage 0); if the pad/USB threads mostly sleep, the gain is small |
| full native sound stack | ~0% IOP, plus a native mixer (~1-3% of a core, guess) | no IOP code left; mixer cost from general experience, not measured |

Compared with the full HLE, stage 1 gets about half the gain for a fraction of the effort and no correctness
risk. The IOP batching patch (0042) already took the IOP from 37.5% to 19.6% this way
(`reports/2026-10-23-gameplay.md` entry 26).

**How to keep identical behaviour at each step:**

- The gate (`docs/ops/gate.md`, tolerance frozen) on every change.
- `gate/play.script` to J's Bar: the capture ticks must stay within 3 ticks of today's (load60s at tick
  6453-6455, entry 26) and the game must finish the script.
- An **RPC trace diff**: the runtime already records every call as a `SifRpcDebugEvent` (sid, fno, mode,
  buffers, send/receive previews, flags; `RPC.cpp:299,367,505,676,761`, `pushSifRpcDebugEvent`). Dump the ring
  to a file with the guest tick, run play.script with the interpreter and with the change, and diff the
  sequence (sid, fno, send bytes hash, receive bytes hash, tick bucket). Plus the SIF DMA list
  (`[sceSifSetDma:DESC]`) for the CRI stream.
- For sound replacements later: PCM capture of the native output vs PCSX2's output of the same scene.

## 5. Staged plan

| stage | what | switch / fallback | verification | effort | gain |
|---|---|---|---|---|---|
| 0 | **Measure.** An IOP pc sampler bucketed by module (the emulator knows each module's range), and a full play.script RPC + SIF DMA trace dumped to a file (trace build) | diagnostics only | none needed; produces the J's Bar RPC list that is missing today | ~0.5-1 day | answers which modules cost time and what the EE really calls in-game |
| 1 | **Cheaper interpreter loop**: DMA start check on register writes only, DMA deadline instead of map walk, import-stub range test | one PS2Recomp patch with tests; old path kept behind a variable for A/B, like `PS2X_IOP_BATCH=0` | IOP suite + a test that a DMA started by a register write completes at the same cycle; gate 3/3; play.script ticks; RPC trace identical | ~1 day | ~half the IOP cost (estimate) |
| 2 | **Don't load modules nobody calls**: sio2man, sio2d, dbcman, pdman, mc2man, both usbd, usbkb, dev9 - only those stage 0 shows the EE never reaches (all of them have EE stubs today) | per-module list in `outbreak.ini` (`iop_skip = ...`), default = today; a skipped module loads as a PS2Recomp HLE id (module manager already does this for ROM names) | RPC trace identical apart from the skipped SIDs; gate; play.script; boot to the memory-card screen and save/load | ~1 day | depends on stage 0 |
| 3 | **Sound with the real drivers (M5)**: SPU2 output (PCSX2's SPU2 core in-process, as with the GS, `docs/ops/gs.md`) fed by the interpreted TSNDDRV/modhsyn/libsd/CRI driver | `PS2X_SPU2=off|pcsx2` | sound capture vs PCSX2 in J's Bar and the title; gate unchanged | large (GPL-3.0 code: open item 3 in PROJECT_CONTEXT) | correct sound, the reference for anything native |
| 4 | **Native CRI ADX output**: replace the CRI driver at SID 0x90000200 + its DMA rings; PCM to the host mixer, consumption at playback rate | per-module: `iop_native = cri` falls back to the IRX | RPC/DMA trace diff vs stage 3; PCM capture vs stage 3 (must be the same PCM); ADX streams pause/stop/loop | ~1-2 weeks (guess) | the CRI threads' share (stage 0 tells) |
| 5 | **Native TSNDDRV**: only after its IOP code is decompiled or traced well enough | `iop_native = tsnd` | as stage 4, plus listening tests per effect | large, open-ended | the rest of the IOP |
| later | **Network**: no dev9/SMAP hardware is emulated, so online play needs a native transport anyway; the natural cut is the socket RPC of the network modules (`docs/android-port-map.md:150-156`) | own milestone | lobby/relay traces against Obsrv | own study | n/a |

Android: every stage is shared code; the interpreter fixes help Android at least as much as Windows. A native
mixer would go to Oboe/AAudio there (`docs/android-port-map.md:94`). Stage 0 should also be run on the Odin
(simpleperf or the same pc sampler), which needs the developer's "proceed".

Decision D4 ("IOP never emulated", `PROJECT_CONTEXT.md:160`, open item 4 at line 457) does not match the base;
this study suggests restating it as: Sony's ROM services HLE (done), the game's own drivers interpreted until
each has a verified native replacement.

## 6. Top risks

1. **Instant completion** in a native driver repeats the bank wall (EE threads starved or spinning). Every
   native service needs guest-time durations, not host-time.
2. **No sound to compare against** until stage 3: a native TSNDDRV/CRI built before it cannot be checked by ear
   or by PCM, only by RPC traces.
3. **Skipping modules shifts the IOP heap layout** (sound modules load after the pad/USB modules). Fine if the
   EE only uses addresses from RPC replies (it appears to); a hard-coded IOP address in game code would break.
   The RPC/DMA trace diff catches this.
4. **Unknown in-game RPC set**: the only traces are boot-to-title from 2026-10-21. Scenario events, memory card
   saves and the network may call things not seen yet (stage 0).
5. **Batch latency**: with patch 0042 an IOP reply can be up to one batch (~10 us guest) late; a native service
   would be immediate. Small, but it changes timing relative to today's tested path.
6. **Licensing**: an SPU2 from PCSX2 brings GPL-3.0 obligations (open item 3).

## Unverified, in one place

- Module ids 10/11 = TSNDDRV/CRI (from server addresses, not a name log); the thread-to-module mapping.
- The EE decodes ADX and sends PCM (from the DMA pattern and symbol names).
- What `func_001C3BD0` binds (SID computed at run time).
- Why upstream dropped its TSNDDRV/CRI stand-ins.
- Whether Play!'s IOP executor is a JIT; Play!'s and PCSX2's documented limitations (not opened).
- All gain figures except "today" are estimates; the Android IOP share is not measured.
