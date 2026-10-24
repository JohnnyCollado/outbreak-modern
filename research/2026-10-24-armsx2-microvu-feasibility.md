# ARMSX2's ARM64 microVU in `native/pcsx2_vu` for Android: feasibility study (2026-10-24)

Read-only study. No code was changed in the project, `C:\ob\up` or `C:\ob\pcsx2`. Two things were done outside
the repo, both allowed by the brief:

- A blob-less, sparse clone of `github.com/ARMSX2/ARMSX2` into `C:\ob\armsx2` (`git clone --filter=blob:none
  --no-checkout`, then sparse checkout of `pcsx2/arm64/**`, `pcsx2/x86/**`, the top-level `pcsx2/*.{h,cpp,inl,txt}`,
  a few header-only `pcsx2/*/` dirs, `common/**`, `3rdparty/include/**`, `tests/ctest/core/**`). HEAD
  `90126bdfea7a3ffb1fd288b2f7f0ae0b033af2d6`, 2026-10-22 18:33 -0700 ("Android: ARMSX2_BUILD_EERUNNER requires
  ENABLE_RECOMPILER_TEST_HOOKS"). GitHub source only, no archives or binaries. `gc.auto 0` set in that clone.
- A throw-away NDK compile of the ARMSX2 VU sources in the session scratchpad
  (`...\scratchpad\a64mvu\`, not the repo, not `C:\ob`), with the same NDK clang that builds `C:\ob\android-r`,
  to get the real list of unresolved symbols.

Problem being solved: on Windows, PCSX2's x86 microVU replaced PS2Recomp's VU1 interpreter (patch 0033/0041,
`docs/ops/build.md` "VU1 backend in the runner"), ~240x faster per VU1 cycle (`reports/2026-10-23-gameplay.md`
entry 6/10). On Android the runner still runs the interpreter (`docs/ops/android.md`, "microVU: not built"),
whose float path is `long double` = software IEEE quad on arm64 NDK clang (`docs/milestones/m0-decision.md:18,56`).
(The VU1 is the PS2's second vector unit; a "microprogram" is the small program it runs per batch of vertices;
a recompiler, or JIT, turns those programs into host machine code at run time.)

## Verdict

**Feasible, recommended, with one structural change from the x86 pattern: build it as its own shared library.**

- The ARM64 microVU is a real port of microVU's design (same entry points, same program cache, same
  pipeline/flag model, same compile-time switches), emitting through **vixl**, whose sources are the same as the
  vixl already in our Android `ob_pcsx2_gs` (tree differs only in `3rdparty/vixl/CMakeLists.txt`, section 1.3).
- It compiles with our NDK: `microVU-arm64.cpp` (one TU, all `.inl`s) in 21 s at `-O2`, `AsmHelpers.cpp` and the
  11 `vuaot/*.cpp` in 11 s in parallel; exit 0 each (measured, scratchpad). Unresolved after linking them
  together: 148 vixl symbols (vixl library), ~25 from PCSX2 `common`, ~10 that need real code from us, ~45
  link-only stubs (section 1.6). Same shape as the x86 lift (`research/2026-10-23-microvu-feasibility.md` 1.4).
- **It must be compiled against ARMSX2's own headers**, not our PCSX2 v2.9.134 pin: its `VU.h` moves VU1 data
  memory into a fixed-offset struct the generated code depends on, and `Config.h`, `Gif_Unit.h`, `MTVU.h` differ
  (section 1.3). Linking ARMSX2-header objects and v2.9.134-header objects (our GS) into one `.so` breaks C++'s
  one-definition rule for same-named types with different layouts (`Pcsx2Config`, `Gif_Unit`, ...): the linker
  silently keeps one copy of each inline function. So: `libob_a64vu.so`, hidden visibility, exports only our
  plain-types API (`include/ob_pcsx2_vu.h`, unchanged), its own copy of vixl and of the few `common` pieces.
- Address reach is **not** a correctness risk on arm64 (unlike x86 disp32): every out-of-range branch or address
  falls back to a longer form (section 1.4).
- Biggest risks: (1) we cannot run ARM64 code here, so verification needs the Odin (adb, developer's
  "proceed") or a QEMU install in WSL (also a "proceed"); (2) ARMSX2 is a fast-moving fork (65 commits on
  `microVU-arm64.cpp` alone since 2026-06-21); we pin a commit and do not track it; (3) float differences vs the
  interpreter, as on x86, plus ARMSX2-specific ones (NEON `fmaxnm/fminnm` NaN rules, its fixes #795/#835).

## 1. The ARM64 microVU (ARMSX2 `90126bd`, read 2026-10-24)

### 1.1 Files (`wc -l`, `C:\ob\armsx2\pcsx2\arm64\`)

Core microVU (one TU via includes): `microVU-arm64.cpp` 2,795, `microVU-arm64.h` 1,206, `_IR-arm64.h` 1,202
(register allocator), `_Lower-arm64.inl` 2,761, `_Upper-arm64.inl` 1,505, `_Compile-arm64.inl` 1,681,
`_Branch-arm64.inl` 1,232, `_Flags-arm64.inl` 541, `_Misc-arm64.h` 564, `_Misc-arm64.inl` 501, `_Clamp-arm64.inl`
235, `_Alloc-arm64.inl` 143; on-disk program cache `_ProgCache-arm64.{h,inl}` 248 + 1,383, `_Persist-arm64.{h,inl}`
195 + 1,982; `vuaot/microVU_VuAot-arm64.inl` 3,170. Separate TUs: `vuaot/*.cpp` (11 files, 13,380 lines; largest
`VuAotLower.cpp` 5,594), `AsmHelpers.cpp` 770 + `.h` 488 (vixl glue). Shared with x86, byte-identical to our pin:
`x86/microVU_Analyze.inl`, `x86/microVU_Tables.inl`, `x86/microVU_Profiler.h` (`diff` = 0 lines; included from
`microVU-arm64.h:614,1196,1204`). Extra headers: `VuFmacFlags-arm64.h` 363, `EeFpuModelCall-arm64.h` 100,
`pcsx2/VuEfuModel.h` 36, `VuMulBand.h` 36, `EeFpuModel.h` 104, `EECycleRate.h` 257.
Total ARM64 VU code ~36.7k lines (x86 microVU: ~10.6k). CMake list: `pcsx2/CMakeLists.txt:1450-1489`.

### 1.2 Entry points (same as x86)

- `recMicroVU1::Reserve/Reset/Shutdown/Execute/Clear` (`microVU-arm64.cpp:2127-2165, 2463-2517`);
  `mVUinit` (`:450-485`, code cache from `SysMemory::GetVU1Rec()`), `mVUreset` (`:1164-1240`, emits dispatchers
  AB/CD, wait-MTVU, copy-pipeline-state, end-program-flags helper, cycle break, FPU-model call stubs).
- `Execute(cycles)` (`:2463-2508`): returns unless `VU0.VI[REG_VPU_STAT] & 0x100`; `TPC <<= 3`; calls
  `startFunct(TPC, cycles)` (or a parked "resume" entry after a budget stop); `TPC >>= 3`; raises `hwIntcIrq(7)` on
  flags bit 2. Our x86 adapter's `Run()` (`native/pcsx2_vu/src/pcsx2_side.cpp` `Run`) maps over unchanged.
- Compile-time switches identical in meaning to x86: `doDBitHandling = false`, `noFlagOpts = false`,
  `doWholeProgCompare = false` (`microVU_Misc-arm64.h:547-561`); `vu1RunCycles` 3,000,000 (`VUmicro.h:20`). So the
  x86 differences we documented (D bit ignored, STATUS/MAC computed only where read) carry over.

### 1.3 VU state struct vs PCSX2 v2.9.134 `VURegs` (diff of `C:\ob\pcsx2\pcsx2\VU.h` vs `C:\ob\armsx2\pcsx2\VU.h`)

Field by field, `VURegs` is the same except one addition:

| Change | ARMSX2 `VU.h` | Effect for us |
|---|---|---|
| New `VIWriteRec VIWrites[3]` (u16 old, u8 reg, u8 cyc) in the 12 bytes of tail padding | `:118-126, 199-204`; `static_assert(sizeof(VURegs) == 1680)` `:221` | Size unchanged; set to zero on `SetState` (interpreter branch-hazard bookkeeping, not part of a save state) |
| `vuRegs[2]` is no longer a global array: `struct alignas(16) VuStateStore { VURegs regs[2]; u8 vu1Mem[0x4000]; }; extern VuStateStore vuState; static VURegs (&vuRegs)[2] = vuState.regs;` | `:259-267` | **Load-bearing.** Generated code reaches VU1 data as `x19 (= &vuRegs[1]) + kVU1MemFromState` (`microVU_Misc-arm64.h:339`, uses `_Misc-arm64.inl:440, 476-478`, `vuaot/VuAotLower.cpp:961, 5263`). VU1 data must be `vuState.vu1Mem` and `vuRegs[1].Mem` must point at it |
| VU0 window / vu-aot tripwire declarations | `:282-313` | Declarations only |

Our x86 runner path already has the shape this needs: since patch 0041 PS2Memory takes its VU1 data/code from the
backend (`PS2ExternalVu1::vu1Memory`, `docs/ops/build.md` "VU1 backend"); the arm64 backend returns
`vuState.vu1Mem` as `OwnedData()` instead of the x86 one-reservation buffer. Zero copy, as on x86.
`VIFregisters` access is the same (`mVU.getVifRegs()` = `vu1Thread.vifRegs` under MTVU else `regs().GetVifRegs()`,
`microVU-arm64.h:812-815`; XTOP `_Lower-arm64.inl:2187-2208`).

Other headers microVU includes, ARMSX2 vs pin (`diff | wc -l`): `Config.h` 535, `Gif_Unit.h` 61, `MTVU.h` 23,
`R5900OpcodeTables.h` 22, `Common.h` 12, `VUmicro.h`/`Vif.h`/`GS.h` 0. `R5900.h` now includes
`IopThread/IopThreadMode.h` (first compile error before I widened the sparse checkout). Hence the
separate-`.so` decision in the verdict.

### 1.4 Dependencies

- **Emitter: vixl** (`AsmHelpers.h:10-11` includes `vixl/aarch64/macro-assembler-aarch64.h`; `armAsm` is a
  `thread_local vixl::aarch64::MacroAssembler*`, `AsmHelpers.h:115-119`). ARMSX2 `3rdparty/vixl` tree
  `b9e9416`, PCSX2 pin `1ff701c`; vs ARMSX2's merge base `36907f4` (tree `1ff701c`, i.e. identical to our pin) the
  only changed file is `3rdparty/vixl/CMakeLists.txt` (two warning suppressions:
  `-Wno-deprecated-enum-enum-conversion`, clang-cl `-Wno-c++11-narrowing`) (`git diff --name-status 36907f4 HEAD
  -- 3rdparty/vixl`). Our Android GS already builds this vixl (`native/pcsx2_gs/CMakeLists.txt:116-118, 144`).
- **AsmHelpers** (ARMSX2 superset of PCSX2's `pcsx2/arm64/AsmHelpers.cpp`: adds `ArmAddressRecorder`,
  `armGetWritableCodePtr`, `armPatchCodeWord`, `armEmitMovRun`, the EE-FPR helpers). Our Android build compiles
  **no** `pcsx2/arm64/*` file (only `GS/Renderers/SW/*.arm64.cpp`, `C:\ob\android-r\compile_commands.json`), so
  there is no symbol clash; in a separate `.so` there could not be one anyway.
- **Not GSVector, not FastJmp**: the undefined-symbol list (1.6) has neither.
- **ARMSX2-only infrastructure** (purpose, and what we do with it):
  - `ProgCache` / `Persist`: on-disk cache of compiled VU programs with a relocation recorder so code compiled in
    one process runs in another (`AsmHelpers.h:69-112`). Off by default (`EnableVUProgramCache = false`,
    `Pcsx2Config.cpp:494`), but turned on by `EnableVUAot` (`UseVUProgramCache()`, Config.h). We set both off.
  - `vuaot/`: "the whole-program VU1 compiler: programs that run often enough are recompiled above microVU"
    (Config.h comment), a second tier with a background worker thread (`vuaot::bg::Worker`). **On by default** in
    ARMSX2 (`EnableVUAot = true`, `Pcsx2Config.cpp:495`). Its text is `#include`d into the microVU TU
    (`microVU-arm64.cpp:2781`), so it must be compiled (or ~30 symbols stubbed); we compile it verbatim and set
    it **off** in our copied defaults for step 1, then measure it as an option (section 3).
  - FPU "hardware model" (`EeFpuModel.h`, `VuEfuModel.h`, `VuMulBand.h`): bit-level models of the PS2 FMAC/EFU
    (truncating multiplier, divide-unit recurrence), used only at vuClampMode 4 (`CHECK_VU_EXACT`, Config.h;
    13 sites in `_Lower-arm64.inl`, several in `_Upper`). Default off (`//vu1ExactMode = false`,
    `Pcsx2Config.cpp:505`). The stubs that call it are always generated (`mVUGenerateModelStubs`,
    `microVU-arm64.cpp:1153-1161`), so the symbols must link; bodies live in ARMSX2's `VUops.cpp`/`FPU.cpp`
    (interpreter). We stub them (abort if called), as x86's macro-mode symbols.
  - `EECycleRate` (VU0-only cycle scaling, `_Compile-arm64.inl:507`), `vu_capture`/`VU1Trace` (test builds only,
    `#ifdef PCSX2_RECOMPILER_TESTS`, `microVU-arm64.cpp:2471-2500`): stub / not built.
- **Code cache and reach.** 64 MB (`Memory.h:78-79, 151-152`, same as x86), last 8 MB a safe zone
  (`mVUcacheSafeZone`, `microVU-arm64.h:609`; capacity reasoning `microVU-arm64.cpp:1203-1218`). Reach rules:
  `armEmitJmp/armEmitCall` use `B/BL` (±128 MB) when in range, else `MOV x16, #addr; BR/BLR x16`
  (`AsmHelpers.cpp:380-450`); `armMoveAddressToReg` uses `ADRP+ADD/ORR` (±4 GB) when in range, else a
  `MOVZ/MOVK` run (`:518-566`). ARMSX2's own comment says data globals sit ~64 TB from the cache on Android and
  "adrp+add never reaches" (`microVU_Misc-arm64.h:320-326`), and works anyway. Hot state is reached through pinned
  registers instead: x19 = `&vuRegs[idx]`, x24 = flags, x25 = `mVUglob`, x29 = VU data (`microVU-arm64.cpp:590-605`).
  Only hard limits: in-place block-link patches assert `imm26` range (`armEmitJmpPtr`, `AsmHelpers.cpp:412-425`,
  `pxAssertRel`), satisfied by a 64 MB cache. **No placement constraint for us** (x86 needed the one reservation).
- **W^X / cache flush on Android.** No write-protect toggling off Apple: `HostSys::BeginCodeWriteRange` and
  friends are empty inlines unless `__APPLE__ && ARCH_ARM64` (`common/HostSys.h:99-112`). PCSX2's JIT arena is
  mapped RWX on Linux/Android (`common/Linux/LnxHostSys.cpp:244-298`, `prot = PROT_READ|PROT_WRITE|PROT_EXEC`).
  Each block end calls `HostSys::FlushInstructionCache` (`AsmHelpers.cpp:187`) = `__builtin___clear_cache`
  (`LnxHostSys.cpp:352-359`); scattered patches batch their flush (`ArmCodePatchFlusher`, `AsmHelpers.h:157-185`).
  So we need one anonymous `mmap(PROT_READ|PROT_WRITE|PROT_EXEC)` of 64 MB. Android apps may map anonymous
  executable memory (ARMSX2 ships this on Android; our own GS SW JIT does the same on the Odin, unverified on the
  device so far: `reports/2026-10-24-android.md` "Still open"). Note: `HostSys.h` in our pin lacks
  `BeginCodeWriteRange` (`C:\ob\pcsx2\common\HostSys.h:100-110`): another reason to build against ARMSX2's `common`
  headers.
- **FPCR** (the ARM float control register: rounding mode, flush-to-zero). The dispatcher loads `VU1FPCR` only if
  it differs from `FPUFPCR` (`mvuNeedsFPCRUpdate`, `microVU-arm64.cpp:509-515`); defaults are equal (both chop +
  DaZ/FTZ, `Pcsx2Config.cpp:35-45`). Same trap as x86 MXCSR (study 1.7): our adapter sets FPCR before the call
  and restores it after (`fesetround`-style via `mrs/msr` or `FPControlRegister::SetCurrent`).
- **Threading.** `armAsm` and the emit buffer are `thread_local` (`AsmHelpers.cpp:110-116`); MTVU is
  `THREAD_VU1 = REC_VU1 && Speedhacks.vuThread` (`Config.h:1860-1861`), default off (`vuThread`,
  `Pcsx2Config.cpp:367, 409-435`). With vu-aot and the program cache off, no extra thread starts. Same
  single-caller rule as x86: one host thread runs VU1 at a time. **`REC_VU1` must be true**
  (`EmuConfig.Cpu.Recompiler.EnableVU1`): ARMSX2's comment at `Config.h:1856-1859` warns that with it false
  `GetGSPacketSize` takes its interpreter path and returns a size with bit 31 set, which `mVU_XGKICK_` reads as a
  multi-GB transfer. Our fake `GetGSPacketSize` copies that same condition (`native/pcsx2_vu/src/ob_mvu_fakes.h`
  `CHECK_XGKICKHACK || !REC_VU1`), so the copied defaults must set `EnableVU1 = true`.

### 1.5 XGKICK and GIF seam

Same calls as x86: `gifUnit.GetGSPacketSize(GIF_PATH_1, vuRegs[1].Mem, ...)`,
`gifUnit.gifPath[GIF_PATH_1].CopyGSPacketData`, `gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, ...)`
(`microVU_Branch-arm64.inl:1102-1156`). Our macro trick (`#define gifUnit ObMvu::g_gifUnit` after the headers,
`native/pcsx2_vu/src/ob_mvu_jit.cpp`) applies unchanged; `GetGSPacketSize` is re-copied from ARMSX2's
`Gif_Unit.h:623` (the file differs from our pin by 61 lines).

### 1.6 Unresolved symbols (measured)

`llvm-nm -u -C` over `mvu.o` + `AsmHelpers.o` + 11 `vuaot` objects, minus what they define among themselves,
minus libc/libc++ (scratchpad `ext.txt`):

| Group | Count | Plan |
|---|---|---|
| vixl (`vixl::aarch64::*`, `CodeBuffer`) | 148 | vixl library, our pin's sources (1.4) |
| `common`: `FileSystem::*` 9, `Path::*` 2, `Error` 2, `Perf::*` 5, `Log::Writev`, `HostSys::FlushInstructionCache/GetRuntimePageSize`, `Threading::*` 4, `_aligned_malloc/free`, `pxOnAssertFail`, `fmt::vformat`, `EmuFolders::Cache` | ~30 | Small stub file in the `.so` (cache/vu-aot off: FileSystem/Path return failure; Perf no-op; Log -> `__android_log_print`; flush -> `__builtin___clear_cache`; aligned -> `posix_memalign`), fmt from `3rdparty/fmt` (tree identical in both repos) |
| Real: `vuState`, `gifUnit`, `vu1Thread`, `EmuConfig`, `SysMemory::GetCodePtr/GetDataPtr`, `eeHw`, `_cpuRegistersPack` | ~9 | Ours, as on x86 (`pcsx2_side.cpp`, `ob_mvu_fakes.h`, `config_defaults.cpp`); `GetDataPtr` only used by Persist (off) |
| EE recompiler: `_freeArm64GPR(regs)`, `_freeNEONreg(s)`, `armEmitEEClobberedPin*ForCOP2`, `vtlb_private::vtlbdata` | 7 | link-only stubs (VU0 COP2 macro mode only, `microVU-arm64.cpp:2669-2758`) |
| VU/EE glue: `CpuVU0`, `CpuVU1`, `CPU_INT`, `hwIntcIrq`, `VU_Thread::Open/WaitVU/Apply_MTVUChanges` | 7 | stubs / fake object (macro, as x86) |
| GIF inlines: `Gif_AddBlankGSPacket`, `Gif_AddCompletedGSPacket`, `Gif_AddGSPacketMTVU`, `Gif_FinishIRQ`, `Gif_HandlerAD(_MTVU)`, `Gif_MTGS_Wait`, `gifCheckPathStatus` | 8 | link-only stubs, as x86 |
| FPU model: `EeFpuModel::*` 3, `VuEfuModel::*` 12, `vuMulShortTailBandVu0/1`, `g_vuMulBand` | 17 | stubs that abort (exact mode off), or copy `VUops.cpp:14-53, 1843-1945` later if mode 4 is ever wanted |
| `EECycleRate::GetEffective/NoteVuReset` | 2 | stub (0 / no-op; VU0 only) |
| `sched_setaffinity`, `wmemchr`, two libc++ `__sort` instantiations, `vtable for vixl::aarch64::PrintDisassembler` | 5 | NDK libc/libc++ and vixl (disasm is in vixl) |

So ~10 real, ~45 stubs, ~30 common shims: the same order as the x86 lift (10 real, 49 stubs,
`native/pcsx2_vu/src/link_stubs.cpp:1-10`).

## 2. Completeness and evidence

- Self-reported: README lists EE, VU (COP2, mVU, VU1) and IOP JITs as "fully complete" (read 2026-10-23,
  `github.com/ARMSX2/ARMSX2`; not re-read today). No benchmark there.
- **Tests (strong evidence, not run by us).** `tests/ctest/core/recompilers/` has 211 files, 117 VU-related:
  `mvu_*` (block list, cache exhaustion, clear, E-bit exit, I-bit hack, lpState, model stubs, options sentinel,
  persist relocation/roundtrip, resume dispatch, VI backup, ...), `vu1_*` (ALU upper/lower, EFU, XGKICK, XGKICK
  drain), `vu_*` (clamp, flags, FTOI saturation, MAC pack, MADD/MADDA, mem addressing, pipeline, P/Q boundary,
  sticky STATUS) and many `*_console_*` tests that check against **first-party console captures**, e.g.
  `vu_pipeline_console_conformance_tests.cpp:1-30` (divide unit DIV 7/SQRT 7/RSQRT 13, EFU latencies,
  FMAC latency 4 measured on a PS2). They need ARMSX2's gtest harness (`ENABLE_RECOMPILER_TEST_HOOKS`,
  `pcsx2/CMakeLists.txt:16-17`) on arm64; we cannot run them here.
- **Device replay corpus.** PR #835 (closed 2026-10-13, `api.github.com/repos/ARMSX2/ARMSX2/issues/835`,
  read 2026-10-24): rewrite rules measured on 23 captured VU programs on SD865 A77/A55 and MQ65 A73/A53
  (0.86-0.90x cycles), "final state of every replayed program is byte-identical to master"; corpus of 2,528 VU1
  programs from Katamari, Burnout 3, SotC, Ratchet UYA; 5,415 God of War II captures. Also fixed a stale final
  STATUS when the E bit sits on a conditional branch. They work the way we do (capture, replay, compare); no
  absolute JIT-vs-interpreter speed is given.
- **Flags and clamps.** Same flag-elimination design as x86 (`noFlagOpts = false`); defaults `vu1Overflow =
  true`, extra/sign/exact off (`Pcsx2Config.cpp:502-505`) = x86's "Normal". Fixes in this area: #795 (closed
  2026-10-04, "the full-width sign clamp reads the resident clamp bounds"), #645 (2026-09-02, divide unit ceiling
  gated on clamp mode), #631 (clamp picker reaches mode 4), #353 ("Normal VU1 clamping for stable game behavior to
  Resident Evil 4", 2026-07-17; RE4, not Outbreak). PCSX2's GameIndex has no VU entry for any Outbreak release
  (x86 study section 3).
- **Outbreak on ARMSX2** (GitHub issue search "outbreak", 17 hits, 2026-10-24): #410, #388 open (crashes, no
  details, old versions v2.6.4-2.6.5); #497 closed 2026-08-11 "Fixed in v2.6.6.5" (green text box, thick text: a
  graphics bug, cause not stated); #314/#318/#321/#341 closed. Nothing names VU1. Unverified whether any was VU.
- **Churn.** 65 commits touch `pcsx2/arm64/microVU-arm64.cpp` between 2026-06-21 and 2026-10-18
  (`api.github.com/repos/ARMSX2/ARMSX2/commits?path=...`); repo GPL-3.0, 212 open issues, pushed 2026-10-23
  (`api.github.com/repos/ARMSX2/ARMSX2`). We pin `90126bd`.
- Expected differences vs our interpreter on the 120 captures: at least those x86 microVU shows (STATUS 39 / MAC
  44 captures, flag elimination; `reports/2026-10-23-gameplay.md` entry 7), plus NaN/denormal behaviour of NEON
  (`fmaxnm/fminnm` in the symbol list; ARM's FZ flushes inputs and outputs). Data memory and PATH1 packets were
  bit-exact for x86 microVU on all 120; that is the bar.

## 3. Extraction plan (our pattern)

Pin: ARMSX2 `90126bd` in `C:\ob\armsx2` (blob-less sparse clone; add to `docs/ops/pins.md`). PCSX2 GS stays on
v2.9.134. No ARMSX2 source edit.

**Copy verbatim (compile from `C:\ob\armsx2`, nothing copied into git):** `pcsx2/arm64/microVU-arm64.cpp` (via our
wrapper TU), `AsmHelpers.cpp`, `vuaot/*.cpp`; vixl from `C:\ob\pcsx2\3rdparty\vixl` (same sources) with the two
warning flags; fmt `format.cc`. Copy into `src/` by hand, marked "verbatim from ARMSX2 90126bd, re-copy on a bump":
the `Pcsx2Config` constructor bodies for `SpeedhackOptions`, `RecompilerOptions`, `CpuOptions` and the FPCR defaults
(as `config_defaults.cpp` does for x86), then our overrides: `EnableVU1 = true`, `vuThread = false`,
`EnableVUAot = false`, `EnableVUProgramCache = false`, `vu1ExactMode = false`.

**Our code (new files under `native/pcsx2_vu/arm64/`, x86 files untouched):**

| Piece | What | Lines |
|---|---|---|
| `ob_a64mvu_jit.cpp` | include ARMSX2's headers first, `#define gifUnit/vu1Thread/SysMemory`, `#include "arm64/microVU-arm64.cpp"`, thin `ObMvuJit` exports (same names as x86) | ~60 |
| `ob_a64mvu_fakes.h` | x86 fakes, `GetGSPacketSize` re-copied from ARMSX2 `Gif_Unit.h:623` | ~110 |
| `a64_side.cpp` | the `ob_pcsx2_vu.h` API: `VuStateStore vuState` storage, `InitOwned` = mmap 64 MB RWX + `OwnedData() = vuState.vu1Mem`, FPCR set/restore around `Run`, `SetState/GetState` (x86 code, plus `VIWrites` cleared), cache clear on code generation | ~300 (about 2/3 from `pcsx2_side.cpp`) |
| `config_defaults_a64.cpp` | verbatim ctor bodies + overrides | ~150 |
| `link_stubs_a64.cpp`, `common_shims_a64.cpp` | ~45 aborting stubs + ~30 shims (1.6) | ~250 |
| `exports.map` + CMake Android branch | `libob_a64vu.so`: `-fvisibility=hidden`, `-Wl,--version-script` exporting only `ObPcsx2Vu::*`, `-Wl,--exclude-libs,ALL`, own vixl static; `ps2x_external_vu1` (`adapter/hook.cpp`, unchanged) links it | ~90 |
| `tools/ob/android.sh` | pass `-DPS2X_EXTERNAL_VU1_DIR`, package the new `.so`, `verify` covers it | ~20 |
| PS2Recomp (numbered patch + test) | Android default `PS2X_VU1=mvu` when the backend exists (no env on a device; `outbreak.ini` key `vu1=`), like patch 0044 does for the GS | ~60 |
| `vucompare` for arm64 | `compare/main.cpp` built with the NDK as a command-line executable, linking `C:\ob\android-r\ps2xRuntime\libps2_runtime.a` (exists) + `libob_a64vu.so`; Windows-only bits (`QueryPerformanceCounter`-free already, uses `<chrono>`) | ~80 |

**Total ~1,100 lines of ours**, plus ~38k lines compiled verbatim.

**Build impact (five-minute rule).** Measured compile: the microVU TU 21 s at `-O2` (scratchpad; `-O3` not
measured), the other 12 TUs 11 s in parallel, vixl ~as in the GS build. These change only on an ARMSX2 bump. A
routine adapter or PS2Recomp change rebuilds one small TU + relinks one `.so`. The new library is its own target,
so a GS stub edit does not rebuild it and vice versa. Packaging adds one `.so` (size unmeasured; guess 3-5 MB
stripped). First configure after adding the dir must not rebuild the 329 game units: check with
`ob.sh depcheck`-style dry run (entry 8 method) before and after.

**Steps, each with what it proves:**

1. Library + `vucompare-a64` build in `C:\ob\android-r` (no device). Proves: it links; `verify` passes.
2. Golden files on Windows: run `ob.sh vucompare` with an option (new, ~40 lines) that writes x86 microVU's output
   per capture (data, VF/VI/ACC/Q/P/I/R/flags, packets). Proves: a second reference that shares microVU's
   flag-elimination design, so arm64 vs x86 microVU should be bit-exact except ARMSX2's deliberate changes.
3. On the Odin (each command needs the developer's "proceed"): `adb push` the binary, `libob_a64vu.so`, the 120
   captures (6.2 MB, `C:\ob\vu1-captures\summary.txt`) and the golden files to `/data/local/tmp/obvu`, then
   `adb shell 'cd /data/local/tmp/obvu && LD_LIBRARY_PATH=. ./ob_vucompare_a64 caps --repeat 20'`. Reports
   arm64 microVU vs the arm64 interpreter (bit for bit, as on Windows), vs x86 golden, and ns per VU1 cycle for
   both on real hardware. Proves correctness on Outbreak's 30 program keys and the real speed-up. A plain
   executable in `/data/local/tmp` avoids the APK entirely.
4. Runner on the device with `PS2X_VU1=mvu` default: `play.script`-equivalent to J's Bar, `[fps]` logcat line,
   frames pulled and compared by eye with the Windows `jsbar` capture (the gate does not reach VU1, entry 9).
5. Optional: `EnableVUAot = true` (+ program cache in the app's files dir), re-measure with step 3.

## 4. Verifying without the device: what is realistic

| Route | Status | Verdict |
|---|---|---|
| vixl's AArch64 simulator on x86 (sources present: `3rdparty/vixl/src/aarch64/simulator-aarch64.cc`, 15,385 lines, behind `VIXL_INCLUDE_SIMULATOR_AARCH64`) | Asserts no flush-to-zero and ties-to-even rounding only (`include/vixl/aarch64/simulator-aarch64.h:5317-5324`), while VU code runs with chop + FZ; generated code `BL`s into host C helpers, which the simulator cannot call; microVU-arm64 includes `<arm_neon.h>` and would have to compile for an x86 host | **Not realistic** |
| QEMU user mode | Not installed on Windows (and qemu-user is Linux-only). WSL Ubuntu exists (`wsl -l`: Ubuntu, stopped). `apt install qemu-user-static` inside it, then run an NDK `-static` build of `ob_vucompare_a64`. QEMU's softfloat honours FPCR FZ/rounding (general knowledge, unverified here) | **Realistic for correctness** after a "proceed" for the install; timing meaningless |
| Android emulator | AVDs installed are x86_64 images (`Sdk/system-images/android-36/google_apis/x86_64`, `android-37.1/*_ps16k/x86_64`); arm64 runs only through binary translation, which is no reference for FPCR or JIT behaviour | Not useful |
| Odin via adb (step 3) | Needs the developer's "proceed" per command | **Realistic, and the only route that measures speed. Recommended** |

## 5. Licence and attribution

- ARMSX2: repo licence GPL-3.0 (`api.github.com/repos/ARMSX2/ARMSX2`, 2026-10-24). File headers in the code we
  build: 38x `GPL-3.0+`, 2x `GPL-3.0` (`AsmHelpers.{h,cpp}`: "2002-2026 PCSX2 Dev Team", "2026 isztld"); copyright
  lines "2026 ARMSX2 Dev Team" (24), "2026 yaps2 Dev Team" (16), "isztld" (1).
- Ours: PS2Recomp is GPL-3.0 (`C:\ob\up\LICENSE:4`), PCSX2 GPL-3.0+, so the runner is already a GPL-3.0 work;
  GPL-3.0-only files fit. The repo has no top-level LICENSE file (none in `ls`); not a blocker for a private build,
  but worth fixing before any binary is shared (unverified intention).
- `CREDITS.md`: add a line: "ARMSX2 developers (ARMSX2 Dev Team, yaps2 Dev Team, isztld) -- the ARM64 microVU VU1
  recompiler and its vixl helpers (`pcsx2/arm64/microVU-arm64*`, `AsmHelpers`, `vuaot/`), GPL-3.0(+), built from
  github.com/ARMSX2/ARMSX2 at 90126bd." The existing "ARMSX2 / PCSX2 developers" line is about the emulator the old
  mod ran on. vixl: BSD-3 (its `LICENCE` file), already shipped with the GS; credit it too if not already.

## 6. Fallbacks if it is too entangled

- **B (best fallback): ahead-of-time C++ for the captured programs.** Outbreak runs 8 code images (30 start-pc
  keys) in the captures (`C:\ob\vu1-captures\summary.txt`: 8 distinct `code_hash`, 0 budget hits). A generator
  (Windows tool) turns each image + start pc into a C++ function, keyed by code hash; the APK carries them; a
  hash miss falls back to the interpreter (counted in logcat). It fits the static-recompile model, needs no JIT,
  no RWX memory, and **verifies on Windows**: the same generated C++ compiles for x86 and runs in `vucompare`
  bit for bit, so only timing needs the device. Correctness work: the static pipeline/flag/Q-P/branch-delay
  analysis microVU does at compile time (the generator can follow `x86/microVU_Analyze.inl`). Effort: 3-5k lines,
  1-3 weeks (guess). Gain: guess 20-80x over the quad-precision interpreter; unmeasured.
- **A: own minimal VU1 JIT with vixl** for the 6 hot start points (~80% of calls,
  `research/2026-10-23-microvu-feasibility.md` intro): same analysis as B plus register allocation and code-cache
  management, and verification needs the device. Strictly worse than B and than ARMSX2's mature JIT.
- **C: interpreter fast path** (x86 study 6.A) plus removing `long double` on arm64 (`m0-decision.md:103`): useful
  regardless, 2-5x guess, does not reach JIT speeds.

## Risks (ranked)

1. **No local arm64 execution**: correctness is first seen on the device (or WSL QEMU after an install).
   Mitigation: x86 golden files make the device run a pure diff.
2. **ODR / header-set mixing**: mitigated by the separate `.so` with hidden symbols; a mistake here is silent.
   Check: `llvm-nm -D libob_a64vu.so` exports only `ObPcsx2Vu::*`.
3. **Fork churn**: pin; re-copy defaults and the GIF fake on every bump; a bump is a deliberate step.
4. **Float/flag differences** vs interpreter and vs x86 microVU (NEON NaN rules, ARMSX2 fixes): `vucompare-a64`
   quantifies; data memory and packets are the bar, J's Bar frames the check.
5. **`REC_VU1` must be true** in our defaults or XGKICK sizes are misread (1.4); a test asserts it.
6. RWX mapping on the Odin: unverified on our build (ARMSX2 ships it).
7. vu-aot tier off at first: possible speed left on the table; measured in step 5.

## Effort

~1,100 lines of our code (table in section 3), ~38k lines compiled verbatim, about the x86 lift's size
(~1,200 planned). The x86 lift took one phase including the capture tool, which is reused here.

## Sources

Local, opened 2026-10-24: `C:\ob\armsx2` at `90126bd` (files cited inline: `pcsx2/arm64/microVU-arm64.{cpp,h}`,
`microVU_*-arm64.{h,inl}`, `AsmHelpers.{h,cpp}`, `vuaot/*`, `pcsx2/VU.h`, `Config.h`, `Pcsx2Config.cpp`,
`Memory.h`, `Gif_Unit.h`, `EeFpuModel.h`, `VuEfuModel.h`, `VuMulBand.h`, `arm64/EeFpuModelCall-arm64.h`,
`VUops.cpp`, `common/HostSys.h`, `common/Linux/LnxHostSys.cpp`, `pcsx2/CMakeLists.txt`,
`tests/ctest/core/recompilers/*`); `C:\ob\pcsx2` at `a81f1aa`+1 (`pcsx2/VU.h`, `x86/microVU*`, `common/HostSys.h`,
`common/Linux/LnxHostSys.cpp`, `3rdparty/vixl/*`); `C:\ob\android-r\compile_commands.json`; `C:\ob\up\LICENSE`;
`C:\ob\vu1-captures\summary.txt`; repo: `research/2026-10-23-microvu-feasibility.md`, `native/pcsx2_vu/*`,
`docs/ops/android.md`, `docs/ops/build.md`, `docs/milestones/m0-decision.md`, `reports/2026-10-23-gameplay.md`,
`reports/2026-10-24-android.md`, `CREDITS.md`, `native/pcsx2_gs/CMakeLists.txt`, `tools/ob/android.sh`.

Measurements (scratchpad `a64mvu\`, 2026-10-24): NDK 27.0.12077973 clang++ `--target=aarch64-none-linux-android29
-O2 -std=gnu++20 -fno-exceptions`, ARMSX2 headers + our pin's `3rdparty`: `microVU-arm64.cpp` exit 0, 21 s, 1.4 MB
object; `AsmHelpers.cpp` + 11 `vuaot` TUs exit 0, 11 s parallel; `llvm-nm -u -C` lists as in 1.6.

Web (GitHub only, 2026-10-24): `github.com/ARMSX2/ARMSX2.git` (clone); `api.github.com/repos/ARMSX2/ARMSX2`;
`api.github.com/search/issues?q=repo:ARMSX2/ARMSX2+{VU1,microVU,vu1+flag,clamp,outbreak,biohazard,VU+bug+is:open}`;
`api.github.com/repos/ARMSX2/ARMSX2/issues/{410,388,497,835}` and `/issues/497/comments`;
`api.github.com/repos/ARMSX2/ARMSX2/commits?path=pcsx2/arm64/microVU-arm64.cpp`. README claims from the
2026-10-23 study (not re-read).

Not opened: any non-GitHub site; ARMSX2's README today; ARMSX2 `VUops.cpp`/`FPU.cpp` model bodies beyond the
grep; QEMU softfloat FPCR handling (stated from general knowledge); Android's SELinux execmem policy text.
Unverified: arm64 speed per VU1 cycle (no source gives it); `.so` size; `-O3` compile time; that FZ/NaN
differences leave Outbreak's output unchanged.
