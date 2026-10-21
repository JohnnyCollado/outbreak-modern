# The analyzer's "self-modifying code" nops: what each one removed

**Answer: all 37 are ordinary data writes, none is self-modifying code.** PS2Recomp's analyzer
flagged them and wrote a nop (`0x0`) for each into the recompiler config. 34 took effect; the
other 3 are in functions PS2Recomp replaces with its own C++, so they never ran. Of the 34,
at least 3 broke Outbreak's boot (**Confirmed**), about 20 more sit in the CRI ADX sound/file
library and the Sofdec movie player and would have broken later features (**Unverified**), and
the rest are shut-down paths or have no known reader.

Found 2026-10-21 while tracing why the boot hung at `func_00140C58` (a deliberate trap on a NULL
buffer). Recompiler log: `Applied patch at 0x141034`.

## Why the analyzer flagged them

`ElfAnalyzer::tryPatchSelfModifyingStore` (PS2Recomp `ps2xAnalyzer/src/elf_analyzer.cpp`) flags
a `sw` whose target is a "code address" when the next instruction is `j`/`jal`. A code address is
anything inside a section marked executable. Outbreak's ELF has **one** `WAX` section covering
0x100000-0x23BA00, code and data together; the decomp's last function ends at 0x1D9ADC. So every
global variable counts as code, and "save a value, then call a function" (very common) is flagged.
The analyzer's own README says self-modifying code "is flagged but requires manual review"; the code
writes the patch instead.

## Input

Your files, never committed: SLPM-65428 v1.04 + English patch v2.1 (sha1-checked against the
decomp). Decomp `eb6af38` (splat output and `symbol_addrs.txt` for names). PS2Recomp `eb8688b` +
patches 0001-0009, analyzer run on Windows (MSVC). Each store was traced with a script over the
decomp's `text.s`: containing function, variable written, next call, every other function using
the variable, and the callers of the containing function.

## Results

### Broke the boot (Confirmed)

| Store | Function | Variable | What it holds | Effect of the nop |
|---|---|---|---|---|
| 0x141034 | `func_00140FC0` (EE↔IOP transfer setup, CRI) | `D_0020C000` | EE address of transfer buffer 0 | Buffer created as NULL; its callback `func_00140C58` hits its NULL trap and spins forever. **The hang we were chasing.** gdb: object created with `buf=0x0`. |
| 0x12b484 | `ADXPS2_SetupThrd` | `D_001FA2F8` | Main thread's current priority (from `ReferThreadStatus`) | Read as `$a3` = `initial_priority` by all six ADX thread-creation functions (`func_0012AEF8`-`func_0012B1D0`). With the nop, ADX's 4 threads were created at priority 0. gdb: `CreateThread ... prio=0` ×4. |
| 0x12b46c | `ADXPS2_SetupThrd` | `D_001F2A68` | Main thread id (from `GetThreadId`) | `ReferThreadStatus` in the next instruction reads this id back, so it queried thread 0 (= "self" on the EE) instead; also read by 4 shutdown/reset functions. Effect on the boot: masked by the line above. |

### Would break later features (Unverified: nothing has run that far yet)

| Store(s) | Function | Variable | What it most likely is |
|---|---|---|---|
| 0x128744 | `func_001285A0` (called by the `ADXF_LoadPartition*` family) | `D_001EF7B8` | Destination buffer of a partition read, used 4× by `func_00128870` (the partition loader's next step) |
| 0x12a054 | `ADXT_Init` | `D_001F1940` | Handle returned when registering ADX's server callback; `ADXT_Finish` (`func_0012A0C8`) passes it back to unregister |
| 0x12a078, 0x129e8c | `ADXT_Init`, `func_00129E70` | `D_001F1944` | Counter reset at init and incremented by the ADX server; read by `ADXT_Pause` and 5 more |
| 0x127e00 | `func_00127DE8` | `D_001EED34` | Second half of a (callback, argument) pair; the first half is kept, so the callback gets argument 0 |
| 0x12a990 | `func_0012A938` (ADX thread body) | `D_001F2A0C` | "Server running" flag, checked by `func_0012BAF8` |
| 0x12bd70 | `func_0012BD58` | `D_001F2A1C` | Request flag set before waking an ADX thread |
| 0x12bddc | `func_0012BDB8` | `D_001F2AD8` | Re-entry guard around `func_00129D60` |
| 0x13e1c0 | `func_0013DFF8` (PS2RNA sound setup, CRI) | `D_002039D0` | Size of an IOP heap buffer; read twice afterwards in the same function |
| 0x14109c | `func_00140FC0` | `D_0020C008` | IOP address of transfer buffer 0 (kept only here; may be unused) |
| 0x15e4b4, 0x15e504 | `mwPlyInitSfdFx` (Sofdec movie player init) | `D_002114DC`, `D_002103D4` | Player state reset / "initialised" flag; read by `func_00163C20`, `func_00163D78`, `func_00163E18` |
| 0x15e72c | `func_0015E710` | `D_002103D0` | "Re-initialise Sofdec" request flag (cleared, then init) |
| 0x163f08 | `func_00163EB8` (Sofdec) | `D_002114E0` | Stored pointer; no other reader found |
| 0x1782a0 | `func_00178180` (game code) | `D_00211764` | A string pointer; written here, used by `func_001782E0` |
| 0x17cc08 | `func_0017CBE0` (game code) | `D_00211824` | Field +4 of a 0x4A8-byte structure cleared just before (set to 8) |

### Shut-down, re-initialisation and reference counters (Unverified)

CRI's `X_Init`/`X_Finish` functions keep a count; `Finish` decrements it and only cleans up at 0.
The decrement's store was nopped in four of them, so a second `Init` after a `Finish` would skip
its set-up:

| Store | Function | Counter |
|---|---|---|
| 0x12816c | `func_00128150` (ADXF finish; its init is `func_00128088`) | `D_001EEE3C` |
| 0x12b55c, 0x12c0e4 | `func_0012B540`, `func_0012C0C8` (ADXPS2 thread shut-down) | `D_001F29D4` |
| 0x13f5cc | `func_0013F598` (called from `ADXT_Finish` and the Sofdec finish) | `D_002071D8` |

Others in the same family: 0x12a0f8 (`ADXT_Finish` clears `D_001FE99C`, the "transfers active"
flag that `func_00138ED8` checks), 0x12c158 (`D_001F29D8` cleared at shut-down), 0x13e454 and
0x13e464 (`func_0013E400` zeroing pointers after `sceSifFreeIopHeap`, which guards against freeing
twice), 0x12c014 and 0x12c02c (a second copy of the `ADXPS2_SetupThrd` stores, in `func_0012BF10`).

### No known effect

| Store | Why |
|---|---|
| 0x1003ac, 0x100be0, 0x100ef8 | Inside libcdvd helpers only called from CD functions PS2Recomp replaces (`sceCdRead`, `sceCdSeek`, `sceCdMmode`, `sceCdReadClock`, …) or from a callback thread created by a replaced function. Most likely never run. |
| 0x1008a4, 0x107e0c, 0x107e7c | Not applied: their functions (`sceCdLayerSearchFile`, `__malloc_lock`, `__malloc_unlock`) are runtime stubs. |
| 0x134370 | `D_001FDD28` has no other reader. |
| 0x138b0c | `D_001FE998` ("initialised" flag) has no other reader found by symbol. |

## Measured effect on the boot

| Config | Result |
|---|---|
| Analyzer patches + fixes 1 and 2 | Past the partition loop, then stuck in `func_00140C58`'s NULL trap |
| **No** analyzer patches + fixes 1 and 2 | All three NETBIO partitions load; stops at the unimplemented `sceDmaRecvN` (pc 0x15f688) |
| No analyzer patches + fix 2 only | Stuck at the partition loop again (see below) |

## Correction to an earlier claim (same day)

I first said fix 1 (accept priority 0) was justified because "the game creates its threads at
priority 0". **That was wrong:** the 0 came from the nopped store at 0x12b484. Without the nops
the game copies the main thread's priority, and PS2Recomp starts the main thread at 0 (it rejected
0 for every other thread), so fix 1 is still needed. The evidence for it is now ps2sdk's own kernel
code, which creates `KernelTopThread` with `initial_priority = 0` and fails if `CreateThread`
returns an error (`ee/kernel/src/thread.c`, ps2sdk `3f0c5cb`), i.e. real hardware accepts 0. What
priority the BIOS gives the main thread is still **Unknown**.

I had also listed the memory allocator lock as affected; it wasn't (its function is a stub).
