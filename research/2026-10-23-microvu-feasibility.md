# microVU (PCSX2's VU1 recompiler) in-process: feasibility study (2026-10-23)

Read-only study. No code was changed anywhere. One throw-away compile of `C:\ob\pcsx2\pcsx2\x86\microVU.cpp` was
done in the session scratchpad (not the repo, not `C:\ob`) to get the real list of unresolved symbols; its object
file and logs are in the session scratchpad only.

Problem being solved: `reports/2026-10-23-gameplay.md` entry 3. During the scenario load 93% of samples are inside
PS2Recomp's VU1 interpreter (the VU1 is the PS2's second vector unit, a small programmable SIMD processor that runs
"microprograms" to transform vertices and emit GS draw packets). Probe numbers, `C:\ob\logs\boot-vu1probe.err`
(last lines): per 2 s, ~3,200-3,300 MSCALs (VIF command that starts a VU1 microprogram), ~10.0M VU1 cycles,
~1.77-1.87 s inside `VU1Interpreter::run`, 0 budget hits. That is ~178 ns per VU1 cycle, ~5.6M cycles/s.
Start-pc histogram (same lines): 0x640 ~770-1000 calls, 0x528 ~670-920, 0xb80 ~450-660, 0x128 ~260-360,
0x0 ~240-300, 0x58 ~160-220.

## Verdict

**Feasible with caveats.** microVU compiles as one translation unit (`x86/microVU.h:319-332` includes all `.inl`
files), it compiled cleanly against our pinned tree with the exact `ob_pcsx2_gs` flags in ~7 s, and the x86
emitter it needs is already inside `ob_pcsx2_gs` (`C:\ob\pcsx2-gs-build\build.ninja:852` lists
`common\emitter\*.cpp.obj`). Of 265 unresolved symbols, 198 are x86 emitter / Perf / Log / FileSystem (already
built), and 67 are outside. Of those 67, ~10 need real code (VU state, GIF hand-off, VIF TOP/ITOP, config defaults,
code memory), the rest are link-only stubs for code VU1 never runs (EE recompiler and VU0 macro mode). The three
caveats that decide success:

1. **Address reach (silent-miscompile risk).** Generated code addresses VU data memory by absolute address for
   constant-address loads/stores (`microVU_Misc.inl:342-360`, `ptr[mVU.regs().Mem + ...]`), encoded rip-relative
   or relative to the `rbx` text pointer, else a disp32 that only works below 2 GB (`common/emitter/x86emitter.cpp:298-330`).
   The out-of-range case is only a `pxAssertMsg`, which our release build compiles out. PCSX2 avoids it by mapping
   data and code in one area (`pcsx2/Memory.cpp:98-117`). PS2Recomp's VU1 data is a heap `new[]`
   (`C:\ob\up\ps2xRuntime\src\lib\ps2_memory.cpp:291-299`, `include/runtime/ps2_memory.h:364-365`), so the mVU1 code
   cache must be allocated within +-2 GB of it (or VU1 data must move/copy into an exe-static buffer). Globals
   (`vuRegs`, `microVU1`, `eeHw`, `_cpuRegistersPack`) are fine when statically linked into the exe: they are reached
   through `rbx = &cpuRegs.GPR.r[9]` (`x86/iR5900.h:31`, loaded by `xScopedStackFrame`, `x86emitter.cpp:1392`).
2. **Float results will not be bit-identical to the interpreter in edge cases** (clamping, NaN sign, MADD rounding,
   I register normalisation; section 3). The interpreter is not a hardware reference either, so the gate, not
   bit-equality, decides.
3. **XGKICK hand-off has no seam** without either a PCSX2 source edit or a wrapper-TU macro trick (section 1.4).

## 1. PCSX2 side (`C:\ob\pcsx2`, v2.9.134, `a81f1aa`)

### 1.1 Files

`pcsx2/x86/` (line counts from `wc -l`): `microVU.cpp` 485, `microVU.h` 332, `microVU_Alloc.inl` 133,
`_Analyze.inl` 678, `_Branch.inl` 634, `_Clamp.inl` 78, `_Compile.inl` 1074, `_Execute.inl` 381, `_Flags.inl` 475,
`_IR.h` 1139 (register allocator), `_Log.inl` 144, `_Lower.inl` 2203, `_Macro.inl` 900 (VU0 COP2 macro mode for the EE
recompiler; not needed), `_Misc.h` 335, `_Misc.inl` 605, `_Profiler.h` 143, `_Tables.inl` 214, `_Upper.inl` 672.
About 10.6k lines, one TU. Listed in `pcsx2/CMakeLists.txt:1027-1044`.

`microVU.h:12-23` includes `Common.h`, `VU.h`, `MTVU.h`, `GS.h`, `Gif_Unit.h`, `iR5900.h`, `R5900OpcodeTables.h`,
`common/emitter/x86emitter.h`, `common/Perf.h`. Declarations only matter at link time; see 1.5.

### 1.2 Entry points and how a run is bounded

- Lifetime: `recMicroVU1::Reserve()` -> `mVUinit` (`microVU.cpp:15-29, 315-319`; takes the code cache from
  `SysMemory::GetVU1Rec()`, i.e. `GetCodePtr(mVU1recOffset)`, 64 MB, `Memory.h:78-79, 139-140`; also calls
  `vu1Thread.Open()`). `Reset()` -> `mVUreset` (`microVU.cpp:32-83, 341-346`) emits the dispatchers
  (`mVUdispatcherAB/CD`, wait-MTVU, copy-pipeline-state, compare-state; `microVU_Execute.inl:23-310`). PCSX2 calls
  these from `VMManager.cpp:2706-2707`; also sets `x86Emitter::use_avx` (`VMManager.cpp:407`), which we must do.
- Start: PCSX2's `vu1ExecMicro(addr)` (`VU1micro.cpp:49-80`) sets `VPU_STAT |= 0x100` (VU1 busy), `VI[TPC] = addr`,
  `SetStartPC(TPC<<3)`, then `Execute(vu1RunCycles)` (3,000,000, `VUmicro.h:20`) when Instant VU1 is on.
- `recMicroVU1::Execute(cycles)` (`microVU.cpp:379-394`): returns at once unless `VU0.VI[REG_VPU_STAT] & 0x100`;
  calls the generated `startFunct(TPC<<3, cycles)`; raises `hwIntcIrq(7)` if flags bit 2 was set (T/D bit interrupt).
- Budget: `mVUexecute` stores `cycles` into `mVU.cycles` (`microVU_Execute.inl:318-334`); every block subtracts its
  cycle count and exits early when it runs out (`mVUtestCycles`, `microVU_Compile.inl:449-497`), saving the pipeline
  state into `prog.lpState` so a later Execute resumes at TPC (this is how MSCNT continues).
- End: E-bit ends via `mVUDTendProgram`/`mVUendProgram` (`microVU_Branch.inl:20-140`): flushes pending XGKICK, saves
  Q/P/flags to `VI[]`, writes `VI[REG_TPC]`, clears `VPU_STAT` bit 0x100 (`:126-131`), jumps to `exitFunct`.
- Cleanup: `mVUcleanUp` (`microVU_Execute.inl:340-372`) adds cycles to `VU1.cycle`; adds to `cpuRegs.cycle` only
  `min(cycles,3000) * EECycleSkip` (0 by default, `Pcsx2Config.cpp:390-394`), so it does not touch our EE timing.
- T bit: `mVUDoTBit` tests `VU0.VI[REG_FBRST]` bit 0x800 and sets `VPU_STAT |= 0x400` (`microVU_Compile.inl:578-595`).
  D bit handling is compiled out (`doDBitHandling = false`, `microVU_Misc.h:313`; uses `microVU_Branch.inl:335,446,577`),
  while PS2Recomp honours `dBitEnabled` (difference; Outbreak's use of D bit unverified).
- XGKICK (VU1 instruction that sends a GS packet from VU data memory to the GIF on PATH1):
  `mVU_XGKICK` (`microVU_Lower.inl:1822-1870`) records the address; the transfer happens one instruction later via
  `mVU_XGKICK_DELAY` -> `mVU_XGKICK_(addr)` (`:1698-1714`), which asks `gifUnit.GetGSPacketSize(GIF_PATH_1, ...)` and
  sends the whole packet at once with `gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, ...)` (wrapping at 0x4000).
  With the `XGKickHack` gamefix it trickles instead (`_vuXGKICKTransfermVU`, `:1716-1786`); off by default and not set
  for Outbreak (section 3).

### 1.3 State it operates on (`pcsx2/VU.h:118-200`)

`VURegs`: `VF[32]` (128-bit), `VI[32]` (each padded to 128 bits; 0-15 integer regs, 16 STATUS, 17 MAC, 18 CLIP,
20 R, 21 I, 22 Q, 23 P, 26 TPC, 28 FBRST, 29 VPU_STAT, `VU.h:7-23`), `ACC`, `q`, `p`, `idx`, `cycle`, `flags`,
`start_pc`, `pending_q`, `pending_p`, `micro_macflags[4]`, `micro_clipflags[4]`, `micro_statusflags[4]` (the four
in-flight flag instances), `nextBlockCycles`, `Mem` (data, 16 KB), `Micro` (code, 16 KB), XGKICK fields, and the
interpreter's pipe arrays (unused by mVU). `vuRegs[2]` is `extern` (`VU.h:213`); VU1 FBRST/VPU_STAT live in
`VU0.VI[]`. The dispatcher loads Q/P/pending Q/P and the flag instances from `vuRegs[1]` (`microVU_Execute.inl:39-67`).
XTOP/XITOP read `vif1Regs.top/itop` (`microVU_Lower.inl:1649-1691`), which is `eeHw[0x3C00]` (`Vif.h:225`).
`mVU.regs().Micro` is read at compile time and in the program-compare memcmp; `Mem` is read by generated code.

### 1.4 Dependencies outside the VU code (measured)

Throw-away compile: `cl /c` of `microVU.cpp` with the `ob_pcsx2_gs` DEFINES/FLAGS/INCLUDES from
`C:\ob\pcsx2-gs-build\build.ninja` (GSState.cpp entry), ~7 s wall including vcvars, exit 0; `dumpbin /symbols`:
265 distinct external UNDEF symbols. Classification:

| Group | Symbols | Plan |
|---|---|---|
| x86 emitter (`x86Emitter::*`, `x86Ptr`, `JZ32/JNZ32`) | ~190 | already in `ob_pcsx2_gs` (`common/emitter/*.cpp`) |
| `Perf::any/vu0/vu1`, `Log::Writev`, `FileSystem::OpenCFile`, `g_cpu` | 6 | already in `ob_pcsx2_gs` (`common/Perf.cpp`, `Console.cpp`, `FileSystem.cpp`, `GS/MultiISA.cpp:98`) |
| `vuRegs` | 1 | **real**: our storage, `Mem`/`Micro` pointed at PS2Recomp's VU1 data/code (zero copy) |
| `gifUnit` (+ 8 `Gif_*` / `gifCheckPathStatus` pulled by `Gif_Unit.h` inlines) | 9 | **real seam**: see below |
| `vu1Thread` (`Open`, `WaitVU`, `Get_MTVUChanges`, `vuFBRST`, `mtvuInterrupts`, `vifRegs`) | 4 | stub object; MTVU off |
| `EmuConfig` | 1 | exists in `config_stubs.cpp:18`, but with **empty** option ctors (`:21-25`): must copy `SpeedhackOptions` (`Pcsx2Config.cpp:378-387`), `RecompilerOptions` (`:445-470`), `CpuOptions` (`:596-605`) and the FPCR defaults (`:30-41`) verbatim, else clamping and FPCR are all zero |
| `SysMemory::GetCodePtr` | 1 | exists (`native/pcsx2_gs/stubs/host_stubs.cpp:100-108`) but asserts SWrec only: extend with the 64 MB mVU1 range, allocated near VU1 data (caveat 1) |
| `eeHw`, `_cpuRegistersPack` | 2 | exist (`host_stubs.cpp:95-96`) |
| `hwIntcIrq`, `CPU_INT` | 2 | stub (PS2Recomp's callback does not raise an interrupt today, `ps2_runtime.cpp:717-753`) |
| EE recompiler / iCore (`_allocX86reg`, `_freeXMMreg`, `xmmregs`, `x86regs`, `g_pCurInstInfo`, `recCall`, `iFlushCall`, `vtlb_DynGen*`, `_eeMoveGPRtoM`, `recDoBranchImm`, `TrySwapDelaySlot`, `g_cpuConstRegs`, ...) | ~30 | link-only stubs (abort). Used by `microVU_Macro.inl` and by the reg allocator only in `cop2mode` (`microVU_IR.h:322, 373-409, 545, 870`), i.e. VU0 macro mode |
| VU0/VU glue (`CpuVU0/1`, `BaseVUmicroCPU::ExecuteBlockJIT`, `vu1ExecMicro`, `vu1Finish`, `vu0/vu1ResetRegs`, `VCALLMS(R)`, `_vu0WaitMicro`, `_vu0FinishMicro`, `scaleblockcycles_clear`, `s_nBlockInterlocked`, `pc`) | ~13 | link-only stubs |

So: ~10 real, ~55 stubs. Same shape as the GS stubs (`docs/ops/gs.md` "Stubs"), smaller.

**GIF seam without editing PCSX2.** `Gif_Unit.h` is also included by `GS.h` and `Hardware.h`, so a shim header
searched first (the D3D-shim trick, `docs/ops/gs.md` Stubs) would collide with the real one. Instead: a wrapper TU
`ob_mvu.cpp` includes every header `microVU.h` includes (they are `#pragma once`), then
`#define gifUnit ob_mvu_gif()` and `#define vu1Thread ob_mvu_vu1thread()` returning small fake objects with the
same member names (`GetGSPacketSize`, `gifPath[GIF_PATH_1].CopyGSPacketData`, `TransferGSPacketData`; `vuFBRST`,
`mtvuInterrupts`, `vifRegs`, `WaitVU`, `Open`, `IsOpen`, `Get_MTVUChanges`), then `#include "x86/microVU.cpp"`.
The macros only reach microVU's own text. The fake GIF unit parses GIFtags (same rule as PS2Recomp's
`progressXgkick`, `ps2_vu1_core.cpp:844-916`), copies the packet with wrap, and calls a bridge function that ends in
`PS2Memory::submitGifPacket(GifPathId::Path1, ...)` (as `ps2_vu1_core.cpp:918-928` does). Unverified until built:
that no other symbol in the headers collides with the macro names. Fallback: one numbered patch in
`upstream/pcsx2/` adding a hook pointer in `mVU_XGKICK_`.

### 1.5 Program cache and invalidation

- Programs are kept per start pc (`prog.prog[startPC/8]`, lists of `microProgram`), each with a copy of the code
  bytes it compiled and the ranges it covers (`microVU.h:39-77`). Lookup (`mVUsearchProg`, `microVU.cpp:244-299`):
  quick pointer if not cleared, else memcmp of the compiled ranges against `Micro` (`mVUcmpProg`, `:216-241`);
  no match -> new program. Whole-memory compare and hashing are off (`doWholeProgCompare = false`,
  `microVU_Misc.h:316-320`; `mVUrangesHash` only for stats, `microVU.cpp:169-191`). Blocks inside a program are
  keyed by pipeline state (`microBlockManager`, `microVU.h:140-274`).
- Invalidation: VIF MPG calls `CpuVU1->Clear(addr,size)` before the memcpy (`Vif_Codes.cpp:337-365`), which only sets
  `prog.cleared = 1` and drops quick pointers (`mVUclear`, `microVU.cpp:102-114`); the next run re-searches by
  memcmp. Our equivalent: before each run, if `PS2Memory::getVU1CodeGeneration()` (`ps2_memory.h:240`; bumped by VIF
  MPG `ps2_vif1_interpreter.cpp:504-523` and EE writes `ps2_memory.cpp:408,915,958,1023,1079,1137`) changed, call
  `CpuMicroVU1.Clear(0,0x4000)`. Cache full -> `mVUreset` (`microVU_Execute.inl:346-350`).

### 1.6 Stub vs real, estimate

Real: vuRegs storage + translation, GIF fake + bridge, vif1Regs TOP/ITOP write into `eeHw`, VPU_STAT/FBRST/TPC,
code-cache allocation near VU1 data, copied config ctors, MXCSR handling (1.7), lifecycle (use_avx, Reserve, Reset).
Stub: ~55 link-only symbols, vu1Thread fake, hwIntcIrq/CPU_INT.

### 1.7 MXCSR trap

The dispatcher loads `VU1FPCR` on entry and `FPUFPCR` on exit only if they differ at generation time
(`mvuNeedsFPCRUpdate`, `microVU_Execute.inl:12-20, 35-36, 75-76`). Defaults are equal (both round-to-zero + DAZ/FTZ,
`Pcsx2Config.cpp:30-41`), so no load is emitted and mVU runs with whatever MXCSR the host has (round-nearest, no
DAZ/FTZ). The adapter must set MXCSR to `VU1FPCR` before the call and restore after (PS2Recomp's interpreter does the
same with `fesetround(FE_TOWARDZERO)`, `ps2_vu1_core.cpp:1634-1635, 1838-1839`).

## 2. PS2Recomp side (`C:\ob\up`, `a6fadcf`, read only)

- State: `VU1State` (`ps2xRuntime/include/runtime/ps2_vu1.h:10-36`): `vf[32][4]` float, `vi[16]` int32 (stored
  sign-extended from 16 bits, `ps2_vu1_core.cpp:825-828`), `acc[4]`, `q`, `p`, `i`, `r`, `pc`, `mac`, `clip`,
  `status`, `cycles`, end/halt bits, `dBitEnabled`, `tBitEnabled`, `stoppedByD/T`, `top`, `itop`, branch state.
  Private pipeline model (`ps2_vu1.h:194-241`): flag, FDIV, EFU, store, VF/VI/ACC write queues, XGKICK buffer.
- Start: VIF1 MSCAL/MSCALF/MSCNT (`ps2_vif1_interpreter.cpp:440-476`) latch TOP/ITOP, toggle DBF and call the
  runtime callbacks; `ps2_runtime.cpp:717-753` copies FBRST D/T enables, calls
  `m_vu1.execute(code, 0x4000, data, 0x4000, gs, &memory, startPC, top, itop, 65536)` (MSCNT: `resume`), then sets
  `vu0_vpu_stat` bits 0x200/0x400 from `stoppedByD/T`. Only these two sites run VU1 (grep of `m_vu1.`).
- Run loop: `VU1Interpreter::run` (`ps2_vu1_core.cpp:1624-1840`): per instruction pair: decode from a cache rebuilt
  wholesale on any code-generation change (`:1549-1574`, `rebuildDecodedCodeCache`), stall calculation
  (`calculatePairReadyCycle`, `:990-1036`), memcpy save/restore of written VF/ACC to queue delayed writes, and per
  cycle `commitReadyPipelines` scanning ~50 queue entries (`:746-842`) plus `progressXgkick`. `execute` resets the
  scheduler every MSCAL (`:1588-1610`); `flushPipelines` at program end (`:984-988`).
- XGKICK: copied one qword per 2 cycles into a 64 KB buffer, submitted once at EOP via
  `finishXgkick` -> `m_activeMemory->submitGifPacket(GifPathId::Path1, ...)` (`:918-928`) into the GIF arbiter
  (`runtime/gs/ps2_gif_arbiter.h`), which tees to the external GS (`docs/ops/gs.md` Runner integration).
- EE coupling: fully synchronous. The EE's `sceDmaSend` drives the VIF1 interpreter, which calls VU1 inline and
  returns; no VU1 cycles are charged to the EE and VPU_STAT busy (0x100) is never left set. So VU1 timing differences
  between backends are invisible to the EE today (good for a swap; also means the 65536-cycle budget silently drops a
  program that has not finished, `:1636`).
- Floats: operands and results normalised (denormal -> signed 0, Inf/NaN -> signed max, `:110-153`); FMAC flags and
  rounding computed from a `long double` "exact" result (`:198-233, 403-431, 482`); on MSVC `long double` is the
  same 53-bit type as `double` (general MSVC fact, not opened as a source here).

### 2.1 Where `PS2X_VU1=interp|mvu` cuts in

At the two callbacks in `ps2_runtime.cpp:717-753`, read once at startup next to `PS2X_GS` (`ps2_runtime.cpp:752-775`
per `docs/ops/gs.md`; line numbers there predate patch 0030). Same pattern as the GS: a PS2Recomp patch adds an
interface (e.g. `PS2ExternalVu1 { mscal(startPC, top, itop, fbrst) -> {stoppedByD, stoppedByT}; mscnt(...);
reset(); }`), null = interpreter; CMake links `native/pcsx2_vu` (or extends `ob_pcsx2_gs`) only when the external
dir is set. Two-target split as for the GS (`hook.cpp` with PS2Recomp headers, `pcsx2_side.cpp` with PCSX2 headers,
plain-types bridge), since `VU.h` and `ps2_vu1.h` both define VU types.

### 2.2 State translation

In mvu mode `vuRegs[1]` is the only copy of VU1 state; translation is needed only (a) at the switch/reset and
(b) in the differential smoke. Per MSCAL the adapter writes: `VU0.VI[REG_FBRST]` = EE `vu0_fbrst`;
`VU0.VI[REG_VPU_STAT] |= 0x100`; `VU1.VI[REG_TPC] = startPC/8`, `VU1.start_pc = startPC`; `vif1Regs.top/itop`
(in `eeHw`); MXCSR; cache clear if code generation changed. After: `stoppedByT = VPU_STAT & 0x400` (D never, see
1.2), restore MXCSR, clear 0x100 if the budget ran out (matches today's "drop"). For (a)/(b), interpreter -> mVU:
`VF[]` raw bits; `VI[0..15] = vi & 0xFFFF`; `ACC`; `VI[REG_Q]=pending_q=q`, `VI[REG_P]=pending_p=p`, `VI[REG_I]=i`,
`VI[REG_R]=r`; `VI[STATUS/MAC/CLIP]` and `micro_*flags[0..3]` broadcast of the same value (what mVU itself does at
E-bit, `microVU_Branch.inl:101-113`); `prog.lpState` zero (mVU clears it at E-bit, `:56-59`). Back: the reverse,
VI sign-extended from 16 bits. Valid only between programs (the interpreter flushes its pipelines at program end,
`ps2_vu1_core.cpp:1826-1833`); a budget-truncated program cannot be translated (pending pipeline state), so the smoke
skips those.

## 3. Correctness risks

- Clamp modes: default "Normal" = `vu1Overflow` only (`Pcsx2Config.cpp:462-466`): results clamped with min/max
  against +-FLT_MAX, NaN becomes +max (sign lost, `microVU_Clamp.inl:19-41`); operand clamping and sign-preserving
  modes need Extra/Extra+Sign (`:48-78`, `Config.h:1530-1533`). PS2Recomp keeps the NaN sign and also flushes
  denormal operands; mVU relies on DAZ/FTZ in MXCSR (hence 1.7). Expected bit differences: NaN/Inf inputs, I register
  (PS2Recomp normalises it on LOI/I-bit, `ps2_vu1_core.cpp:1700`; mVU keeps raw bits unless overflow clamping,
  `microVU_Compile.inl:137-145`), MADD/MSUB (mVU: separate SSE mul then add, two roundings; PS2Recomp: computes the
  `long double` product+acc for flags, `:256-259`; whether its stored value also uses one rounding is unverified),
  MAC/STATUS O/U flags. Whether any of these change Outbreak's output: unverified (the smoke measures it).
- Speed hacks on by default: `vuFlagHack` (status flag only computed for blocks that read it, `microVU_Misc.h:327-330`)
  and `vu1Instant` (`Pcsx2Config.cpp:378-387`). Keep PCSX2 defaults; `mvuFlag: 0` exists per game for breakage.
- GameIndex (`ps2_emu/resources/GameIndex.yaml`): SLPM-65428 (`:43786-43796`) and SLPM-74201 (`:54042-54051`): name,
  compat 5, memcard filters only. SLUS-20765 (`:68310-68319`), SLES-51589 (`:18251-18258`), SLUS-20984
  (`:69554-69565`): one EE patch each "removes silly mask causing high barriers on draws" (a performance patch on EE
  code, not VU). SCES-50987 (`:5212`), SLES-53319 (`:23047`): no fixes. **No vuClampMode, mvuFlag, instantVU1, mtvu,
  XGKickHack or VUSync entry for any Outbreak release**, so PCSX2's defaults run this game at compat 5 (key parsing:
  `GameDatabase.cpp:172-175`). This is the strongest evidence that stock mVU settings are correct for Outbreak.
- MTVU (VU1 on its own thread): `THREAD_VU1 = REC_VU1 && Speedhacks.vuThread` (`Config.h:1502`), off by default;
  PCSX2 even notes it "doesn't work properly anyway" and fakes VU1 completion (`VU1micro.cpp:51-63`). We should not:
  PS2Recomp's VIF1/DMA is synchronous, and making it asynchronous is a much bigger change than the JIT. Revisit only if
  VU1 is still hot after mVU.
- Threads: the x86 emitter's state is `thread_local` (`x86emitter.cpp:51-53`) and reset per run
  (`microVU_Execute.inl:331-332`), so the calling thread may vary; two host threads must never run VU1 at once
  (unverified whether PS2Recomp's EE scheduler can; the interpreter has the same constraint).
- XGKICK timing: mVU sends the whole packet one instruction after XGKICK; the interpreter copies 1 qword / 2 cycles.
  A microprogram that overwrites the packet within that window would differ (unverified; the smoke compares packets).

## 4. ARMSX2 (github.com only, fetched 2026-10-23 via api.github.com and github.com)

- `api.github.com/repos/ARMSX2/ARMSX2`: fork of PCSX2/pcsx2, GPL-3.0, default branch master, pushed 2026-10-23.
- `api.github.com/repos/ARMSX2/ARMSX2/contents/pcsx2/arm64`: an ARM64 microVU exists: `microVU-arm64.cpp` (114 KB),
  `microVU-arm64.h` (49 KB), `_Alloc/_Branch/_Clamp/_Compile/_Flags/_Lower/_Misc/_Upper-arm64.inl`, `_IR-arm64.h`,
  plus new `microVU_Persist-arm64.*` and `microVU_ProgCache-arm64.*` and a `vuaot/` directory (ahead-of-time VU
  compilation); also ARM64 EE/IOP/VIF recompilers and `pcsx2/vu_capture.cpp`, `VU1Trace.cpp` at the top level.
- `github.com/ARMSX2/ARMSX2/blob/master/pcsx2/arm64/microVU-arm64.cpp`: header "SPDX-FileCopyrightText: 2026 yaps2 Dev
  Team", GPL-3.0+; emits through `armAsm->` with `a64::` types (vixl-style; the library name was not confirmed);
  uses `EmuConfig` and `THREAD_VU1` heavily; a comment says a content hash is "not yet wired into the search".
- README (github.com/ARMSX2/ARMSX2): lists EE, VU (COP2, mVU, VU1) and IOP JITs as "fully complete" and claims
  "hardware-accurate floating point emulation in jit". **Self-reported**; no benchmarks or test method given.
- Divergence: `api.github.com/repos/PCSX2/pcsx2/compare/v2.9.134...ARMSX2:ARMSX2:master`: "diverged", 4,499 commits
  ahead, 77 behind, merge base `36907f4` (2026-09-27). Heavily rewritten (new files, own copyright line), so the
  Android step will be a separate extraction with its own stub set, not a recompile of our x86 adapter. Not opened:
  the arm64 `.inl` bodies, `vuaot/`, `vu_capture.cpp` (worth reading later as a capture format).

## 5. Plan

Each step names what it proves. Steps 1-3 need no change to the runtime's behaviour.

1. **Capture patch (PS2Recomp, numbered, env-gated, test included).** `PS2X_VU1_CAPTURE=<dir>[,N]` in the MSCAL
   callback (`ps2_runtime.cpp:717`): before the run, dump VU1 code (16 KB), data (16 KB), `VU1State`, startPC, TOP,
   ITOP, FBRST, code generation; after, dump data, state, and every Path1 packet (hook `submitGifPacket` while
   capturing). Capture the first N calls per start pc for the six hot pcs during `play.script`'s load phase.
   Proves: we have real inputs and the interpreter's outputs as reference files; costs one boot to the load screen.
2. **`native/pcsx2_vu` static lib + offline smoke `ob_pcsx2_vu_smoke.exe`.** Wrapper TU (1.4), stubs, copied config
   ctors, code cache allocated near a test VU-data buffer. Smoke loads each capture, runs mVU, compares data memory,
   VF/VI/ACC/Q/P/flags and the Path1 packet bytes against the interpreter's dump bit for bit, prints per-program
   mismatches by field and lane, and times both (`QueryPerformanceCounter`, N repeats). Also run PS2Recomp's own
   VU1 unit tests (`ps2xTest/src/ps2_vu1_tests.cpp`, ~40 cases) through the mVU backend. Proves: it links, generated
   code is correct for Outbreak's six hot programs, and the real speed-up per VU cycle (replaces the estimate below).
3. **Triage mismatches** against PCSX2 itself if needed (reference PCSX2 in `ps2_emu/`; needs the developer's
   "proceed" to launch). A mismatch in the interpreter's favour is a mVU difference to accept or a clamp setting;
   one in mVU's favour is an interpreter bug (log in the struggle report either way).
4. **Seam patch (PS2Recomp)**: `PS2ExternalVu1` + `PS2X_VU1=interp|mvu` (unset = interp; unknown = error exit, like
   `PS2X_GS`), CMake link, gate stamp includes `native/pcsx2_vu/`. Test: interface null = unchanged behaviour; a
   fake backend receives MSCAL/MSCNT with the right pc/top/itop/fbrst.
5. **End checks**: `PS2X_GS=pcsx2-sw PS2X_VU1=mvu bash tools/ob.sh gate` (frozen tolerance, `docs/ops/gate.md`) and
   `play.script` to the GAME.BIN hand-off: vsync rate during NOW LOADING (today 823 vsyncs in 337 s,
   `C:\ob\logs\play5.csv`) and a new `PS2X_PC_SAMPLE` profile to confirm the vu1 share collapsed and to find the next
   bottleneck (expected: the IOP-bound phase, entry 3 "Secondary").

### Expected speed-up (estimate, unverified)

- Measured today: ~178 ns per VU1 cycle (`boot-vu1probe.err`). Basis for the estimate: a JIT runs straight-line host
  SSE code per instruction pair with flags/stalls resolved at compile time (`mVUtestCycles` is one subtract-and-branch
  per block, `microVU_Compile.inl:478-497`), versus the interpreter's per-pair decode copy, memcpys, stall scan and
  per-cycle ~50-entry queue scans. Guess: 1-10 ns per VU1 cycle, i.e. 20-100x on VU1. No PCSX2 or ARMSX2 source
  gives a number (ARMSX2's README has none); step 2 measures it.
- Whole-frame effect (Amdahl on entry 3's numbers): VU-bound frames are 600-675 ms, 93% VU1 (~600 ms) + ~45 ms
  other. At 25x: ~24 + 45 = ~70 ms/frame (~9x); at 100x: ~51 ms. Loading becomes bound by the remaining guest/IOP
  work, not VU1; reaching 60 Hz also needs the IOP batching fix.
- Recompile cost: few programs (six start pcs ~80% of calls), so compile time should amortise unless the game
  re-uploads MPG often (unverified; step 1 records the code generation per call).

### Effort (rough)

| Piece | New code |
|---|---|
| `native/pcsx2_vu` CMake (reuse `ob_extract` lists, one TU) | ~60 lines |
| Wrapper TU + GIF/vu1Thread fakes | ~150 |
| Link-only stubs (~55 symbols) + config ctors copied verbatim + code-cache allocator | ~200 |
| Bridge + adapter (lifecycle, state translation, MXCSR, cache clear) | ~250 |
| PS2Recomp seam patch + test | ~150 |
| Capture patch + test | ~120 |
| Smoke/diff tool | ~300 |
| **Total** | **~1,200 lines**, no PCSX2 source edit if the macro seam holds |

### Build time under the five-minute rule

`microVU.cpp` alone compiled in ~7 s (measured, scratchpad). It changes only on a PCSX2 bump; the adapter and stubs
are small TUs. A routine PS2Recomp or adapter change recompiles one or two small files plus a relink, not the 10.6k
line TU and not `ob_pcsx2_gs` (keep mVU in its own static library target so a GS stub edit does not rebuild it, and
vice versa; shared stubs such as `GetCodePtr`/`EmuConfig` move to a small common stub lib). Runner relink time with
one more static lib: not measured.

### Risks and unknowns (ranked)

1. Address reach of VU1 data vs code cache (caveat 1); silent in release. Mitigation: allocate the code cache near
   `m_vu1Data` and assert the distance at init in all builds.
2. Float/flag divergence from the interpreter (section 3); the smoke quantifies, the gate decides.
3. Macro seam collisions in the wrapper TU (unverified until built); fallback one `upstream/pcsx2` patch.
4. Budget/partial-run semantics: interpreter 65,536 cycles vs mVU block-granular stop; today 0 budget hits, so low.
5. D bit ignored by mVU (`doDBitHandling=false`); Outbreak's use unverified (capture records FBRST and stop bits).
6. Config defaults: our `EmuConfig` ctors are empty today; every PCSX2 bump must re-copy (same rule as GSOptions).
7. Android: x86-only; ARMSX2's ARM64 mVU is a heavily diverged, self-reported-complete fork; separate study.

## 6. Fallback if extraction is too entangled

- **A. Interpreter fast path (first choice if mVU stalls).** Profile `VU1Interpreter::run` on the captures (step 1
  makes this offline and repeatable), then: keep a bitmask / next-ready cycle per queue instead of scanning ~50
  entries every cycle (`ps2_vu1_core.cpp:746-842`); skip idle cycles in `advanceTo` (`:953-957`); invalidate the
  decoded cache by range instead of rebuilding all 2,048 pairs on each code generation (`:1535-1574`); return the
  decoded pair by reference; drop the VF/ACC save-restore memcpys for pairs without a pending-write conflict
  (`:1683-1790`). Expected gain: 2-5x (guess, unverified); does not close 26x alone. Each change is a PS2Recomp
  patch testable with the existing VU1 tests.
- **B. Own minimal VU1 recompiler for the six hot programs**, cached by code hash: either emit C++ ahead of time
  (fits the static-recompile model: dump the programs once, generate functions, fall back to the interpreter on hash
  miss) or a small JIT. Must reproduce the flag pipeline, Q/P latency, branch delay slots and XGKICK; that is the
  bulk of what microVU already does. Effort: several thousand lines and weeks (guess). Only if mVU proves unusable.
- Not recommended: PCSX2's own VU interpreter (`VUops.cpp`, 4,049 lines) as a replacement; no evidence it is faster.

## Sources

Local (opened this session): `C:\ob\pcsx2` files cited inline (`pcsx2/x86/microVU*.{cpp,h,inl}`, `VU.h`,
`VUmicro.h`, `VU1micro.cpp`, `Vif_Codes.cpp`, `Vif.h`, `MTVU.h`, `Gif_Unit.h`, `Config.h`, `Pcsx2Config.cpp`,
`Memory.h`, `Memory.cpp`, `VMManager.cpp`, `GameDatabase.cpp`, `common/emitter/x86emitter.cpp`, `jmp.cpp`,
`CMakeLists.txt`); `C:\ob\up\ps2xRuntime` (`include/runtime/ps2_vu1.h`, `src/lib/vu/ps2_vu1_core.cpp`,
`src/lib/ps2_vif1_interpreter.cpp`, `src/lib/ps2_runtime.cpp`, `include/runtime/ps2_memory.h`,
`src/lib/ps2_memory.cpp`), `C:\ob\up\ps2xTest\src\ps2_vu1_tests.cpp`; `C:\ob\pcsx2-gs-build\build.ninja`;
`native/pcsx2_gs/stubs/{host,config}_stubs.cpp`; `ps2_emu/resources/GameIndex.yaml`; `C:\ob\logs\boot-vu1probe.err`;
`docs/ARCHITECTURE.md`, `docs/ops/gs.md`, `reports/2026-10-23-gameplay.md`.
Measurement: scratchpad compile of `microVU.cpp` (exit 0, ~7 s) and `dumpbin /symbols` (265 external UNDEF).
Web (2026-10-23, GitHub only): `api.github.com/repos/ARMSX2/ARMSX2`, `.../contents/pcsx2`, `.../contents/pcsx2/arm64`,
`github.com/ARMSX2/ARMSX2` (README), `github.com/ARMSX2/ARMSX2/blob/master/pcsx2/arm64/microVU-arm64.cpp`,
`api.github.com/repos/PCSX2/pcsx2/compare/v2.9.134...ARMSX2:ARMSX2:master`.
Not opened: any PCSX2 wiki/forum performance figures; ARMSX2 arm64 `.inl` bodies, `vuaot/`, `vu_capture.cpp`;
MSVC documentation on `long double`.
