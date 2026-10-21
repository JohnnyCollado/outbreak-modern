# The sound-bank wall: what PCSX2 does, what PS2Recomp did, and why

**Question (2026-10-21):** after patch 0014 the game loads its first sound bank
(`func_001AB9E0` in `sebank.c`: send the bank to the IOP's sound driver, then poll every vsync until
the driver says it has it) and never gets past the poll. What does the real thing do at that point?

**Answer:** on PCSX2 (real BIOS kernel, real IOP modules) the bank send completes in **1 + 4 vsyncs**.
In PS2Recomp the IOP sound driver never ran the transfer because its worker thread (priority 50)
was starved by the ADX streaming thread (priority 39), which never blocks because PS2Recomp finished
every SPU2 streaming DMA ~200× too fast; and even if it had run, the IOP's `sceSifGetOtherData`
(how the driver fetches each bank block from EE memory) was a stub that copied nothing.

## Input

Your files (never committed): SLPM-65428 v1.04 + English patch v2.1. PCSX2 **2.9.134** (upstream
pre-release) in `ps2_emu/` in portable mode (`portable.ini`), BIOS `ps2-0200j-20040614-100905`,
booting `roms/Biohazard - Outbreak (English v2.1).iso`; its debugger with EE execute breakpoints.
PS2Recomp `eb8688b` + patches 0001-0014, MSVC build on Windows, with temporary (uncommitted) traces
in the IOP: every import call of the sound driver, every RPC request the EE sent, the IOP thread
table at each `SendMbx` wake. Decomp `eb6af38` for the EE side; the IOP modules were read from
`DRIVE.DAT` (import tables, disassembly with rabbitizer).

## PCSX2 (Confirmed, from its debugger)

Breakpoints: `0x1AB9E0` (SendBank entry), `0x1ABA34` (first poll loop's `sceGsSyncV`), `0x1ABA3C`
(after it returns), `0x1ABA58` (second loop's `sceGsSyncV`), `0x1ABA78` (SendBank done), `0x12A908`
(the ADX spinner thread's loop body).

| What | Hits |
|---|---|
| First loop (`PollSema(4)` = "has the driver received the bank?") | 1 vsync |
| Second loop (`SdrGetStateSend` RPC = "driver state") | 4 vsyncs |
| ADX spinner iterations while SendBank ran | 108,640 (it runs whenever the main thread sleeps in an RPC) |
| Spinner iterations during the first `sceGsSyncV` wait | 0 |

The real kernel's thread table at the end (EE Threads tab), which answers several open questions:

| id | entry | prio | state | what |
|---|---|---|---|---|
| 0 | 0x81FC0 | 128 | RUN | the kernel's idle thread, in the kernel area |
| 1 | 0x100008 | 24 | SLEEP | main thread (`_start`); 24 is what ADX set, its initial value is still unknown |
| 2 | 0x115060 | **0** | WAIT sema 2 | a game thread at priority 0: **the EE kernel accepts 0** (patch 0010 is right) |
| 3 | 0x12A8E0 | 8 | SUSPEND | ADX's spinner, suspended = lock not held |
| 4, 5, 6 | 0x12A9D0 .. 0x12AB90 | 16, 18, 25 | SLEEP | ADX server threads (same priorities as in PS2Recomp) |
| 7 | 0x181768 | 1 | WAIT sema 16 | a game thread at priority 1 |

Also seen: PCSX2's IOP memory breakpoints on hardware registers (`0x1F8010C8` etc.) never fire, so
they can't count DMA kicks; its Modules/Stack tabs are empty for the IOP with this BIOS.

## PS2Recomp (Confirmed by traces)

The EE side was fine: the bank command reached the IOP (`[IOP:rpc] call sid=0x0 fno=0 send=0x293000/32`),
the driver's server function (`0x3FB28`) queued it with `SendMbx(1, …)` and returned, and the game
polled the driver state 136 times (`sid=0x1 fno=0x8000000C`) through the second loop, each time
getting "busy".

The IOP thread table at the `SendMbx` wake (state 1 = Ready):

| id | entry | prio | state |
|---|---|---|---|
| 17 | 0x3E8CC | **50** | Ready — the sound driver's worker, just woken |
| 19 | 0x53920 | **39** | Ready — the ADX driver's streaming thread |
| 10, 11, 18 | 0x3D448, 0x3D618, 0x3E47C | 47, 31, 32 | Sleep — the sound driver's other threads |
| 12, 16 | 0x4FA60, 0x500E4 | 39, 48 | Delay / Sleep — ADX |

IOP scheduling is strict priority (PS2Recomp and the hardware agree), so thread 17 never runs while
thread 19 is Ready. Thread 19 was Ready almost always: 52,280 `CpuSuspendIntr` calls, 4,345
`sceSdBlockTransStatus` polls and 369 `WaitSema`s in 120 s, all from the ADX streaming loop. It
polls the SPU2 DMA address (`sceSdBlockTransStatus` returns `SD_DMA_ADDR`) and waits for the
transfer-complete interrupt; PS2Recomp completed every SPU2 DMA in `max(words×2, 64)` IOP cycles
(≈14 µs for a 1 KB half-buffer) and never advanced the address, while the SPU2 plays a 1 KB
half-buffer in ~5.3 ms (48 kHz × 2 channels × 16 bits = 192 KB/s). On hardware the ADX thread
sleeps ~99% of the time.

What the driver would have done next (from its code, `0x3E778`): `sceSifGetOtherData` in 32 KB
chunks to pull each bank block from EE memory, then `sceSdVoiceTrans`. PS2Recomp's HLE answered
`sceSifGetOtherData` with 0 and no copy (sifcmd ordinal 23, `setV0(0)`).

The single `sceSdVoiceTrans` seen in the import trace (0x5080 bytes at init) **did** complete: the
DMA interrupt 0x24 was delivered, libsd's handler ran (`iSetEventFlag(7, 1)`), the driver's handler
signalled its semaphore. So SPU2 voice DMA itself works in the model.

## Fixes (patch 0015, see `upstream/ps2recomp/README.md`)

1. `sceSifGetOtherData` copies EE → IOP (synchronously; the SDK's mode is irrelevant here).
2. SPU2 **block** transfers (libsd writes `1 << core` to `0x1B0 + core × 1024`) complete after
   `bytes × 192` IOP cycles instead of `max(words × 2, 64)`, and the channel's MADR advances to the
   end of the transfer at completion. Voice transfers are unchanged.

Both have tests that fail on the old code (IOP suite: "did not copy the EE bytes into IOP memory";
"a block transfer must take the SPU2's playback time").

## Still unknown / unverified

- The main thread's **initial** priority on hardware (before ADX changes it to 24).
- Sony's `sceGsSyncV`: on PCSX2 the spinner had 0 iterations during the first vsync wait, which fits
  a busy-wait on the GS CSR register rather than a kernel sleep. PS2Recomp blocks the thread
  (`WaitVSyncTick`), which lets lower-priority threads run during it. Not shown to matter yet.
- The 192 KB/s figure assumes the streaming core consumes 16-bit stereo at 48 kHz; the real SPU2
  block-transfer rate for the ADX driver's mode hasn't been measured on hardware.
- PS2Recomp's IOP runs RPC server functions synchronously on a scratch CPU inside the EE's call
  (`callFunction`); a server that blocks (`WaitSema`) returns early with whatever is in `v0`. The
  sound driver's servers don't block, so this didn't bite here.
