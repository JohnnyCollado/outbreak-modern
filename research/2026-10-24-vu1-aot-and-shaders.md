# VU1 ahead-of-time C++ and GPU shaders: can they replace microVU?

- **Date:** 2026-10-24
- **Who:** Claude Code research agent (read-only: no code changed; throw-away analysis scripts in the session
  scratchpad, not kept)
- **Question:** can runtime JIT emulation of VU1 (microVU, x86 on Windows, ARMSX2's arm64 port on Android) be
  replaced by (A) ahead-of-time (AOT) translation of Outbreak's VU1 microprograms to C++ compiled into the build,
  and later (B) GPU shaders, without losing speed against microVU, and gaining on Android?

Terms used once: **VU1** = the PS2's second vector unit, a small programmable vertex processor; a **microprogram**
is its code, uploaded into its 16 KB **micro memory** by the **VIF** (the DMA front end) with the **MPG** command and
started with **MSCAL** (start at an address) or **MSCNT** (continue after the last stop). **XGKICK** sends a finished
GS packet from VU1 data memory to the **GS** (the PS2's rasteriser). **microVU** is PCSX2's VU JIT (just-in-time
compiler: translates VU code to host code while the game runs).

## Verdict

- **(A) AOT C++: feasible, and the program set is small and static.** All 27 programs are fixed data in the main
  ELF, uploaded whole at micro address 0 from fixed DMA chains; no overlay holds VU code (heuristic search); no
  program uses the D/T/M bits; the 8 captured micro-memory images are 6 of the 27 programs with different leftover
  bytes after the program. Two programs carry ~95% of captured VU1 cycles. Total code is 7,439 instruction pairs.
  Every program is a loop over a vertex count read from the packet, so it is data-dependent, which C++ handles
  without trouble. Speed: parity with microVU is a reasonable expectation and 1-2x better is plausible (a guess,
  unmeasured); the whole VU1 share of a J's Bar frame was ~12% with microVU, so the frame-time gain on Windows is a
  few percent at best. Android is the better case: one tested code base, no JIT memory with write and execute
  rights, and the fresh arm64 microVU port drops out of the hot path. Effort: 2-4 weeks (guess) incl. verification.
- **(B) Shaders: not as a VU1 replacement.** VU1's output is GS packets that PCSX2's GS consumes on the CPU side;
  running the programs on the GPU would need the packets read back every draw, which costs more than it saves.
  Shaders pay off only together with a native renderer that takes vertices directly instead of emulating the GS
  (the OpenGOAL route). Then they are hand ports per program family, not machine translation: 21 of the 27 programs
  have a per-vertex body that maps to a vertex shader (two-bone skinning, lighting, projection); 7 (6 rigid + 1
  skinned) contain a software polygon clipper with a variable vertex count that a native renderer replaces with GPU
  clipping instead. The ceiling is the ~12% VU1 share plus whatever the GS emulation costs, at the price of a new
  renderer.

## 1. The programs

### 1.1 Where they are (verified)

- Selector: `func_001983E0` (`C:\ob\splat\SLPM_654.28\en\main\text.s:185980-186477`, called from `text.s:168853`)
  compares a render-flag word (`0xC($a0)` low 24 bits) and a vertex-format word (`0x4($a0) | 0x8($a0)`) and stores a
  descriptor pointer at `0x4C($a0)` and an index at `0x48($a0)`.
- Descriptor table: 27 records of 0x30 bytes, `D_00212760` ... `D_00212CB0`
  (`en\main\data\vu_data.data.s:88831-89349`), plus 7 small side records (`D_002127C0`, `D_002129B0`, `D_00212A80`,
  `D_00212AC0`, `D_00212B30`, `D_00212BA0`, `D_00212C10`: words 0x14-0x18 / 0x20 / 3, read here as bone-palette
  parameters; their meaning is unverified). Each record holds: the microcode DMA chain (`D_001D9B00` ...), its byte
  size, an EE-side packet builder (`func_0019A090`, `func_0019B290`, ...), small parameters, a VU1 buffer offset
  (0xC80, 0xF80, 0xB40, 0x640, ...), and a float 128.0 or 255.0 (colour scale).
- Each chain is one DMA "ret" tag followed by VIF `MPG` commands at micro address 0 (256 pairs per MPG, up to 3), and
  in 7 programs one `UNPACK V4-32` of a constant to data qword 0x3A6. All MPG addresses are 0: **one program is
  resident at a time, uploaded whole.**
- Matching the 8 code images in `C:\ob\vu1-captures\*.cap` (16 KB micro memory at file offset 104,
  `native\ob_diag\include\ob_vu1_capture.h:58-78`) against the ELF chains: every written pair matches exactly:

| Capture code hash (16 KB) | Program | Flags / format (selector) | Share of captured VU1 cycles |
|---|---|---|---|
| e6fecedd4c51048f | `D_00212BB0` (370 pairs, `func_0019B290`) | 0x2C802, skinned 0x221135/0x321135 | 57.7% |
| 8b65d74c28e30741 | `D_00212C50` (486 pairs, `func_0019B550`) | 0x2C380, rigid 0x121035/0x21035 | 37.6% |
| 270f8e30f64db1be | `D_00212B40` (395 pairs, `func_0019B290`) | 0x2C822, skinned | 2.3% |
| 248ed9f990d4b572 | `D_00212C80` (452 pairs) | 0x24380, rigid | 1.7% |
| 30624c52c9a266a0 | `D_00212CB0` (420 pairs) | 0x24080, rigid | 0.6% |
| 08e8fbb1..., eed97955..., c92e016c... | `D_002128F0` (100 pairs, three different leftovers) | 0x4002, rigid | 0.08% |

  (Shares from `C:\ob\vu1-captures\summary.txt`, 1,539,623,745 cycles over 495,616 runs, `play.script` to J's Bar.)
- **Consequence for dispatch:** a hash of the whole 16 KB is the wrong key (one program shows up as three keys); the
  key must cover the program's own range, or come from the MPG upload itself.
- **Static?** Yes, for everything observed. The code generation counter (incremented per MPG write,
  `C:\ob\up\ps2xRuntime\include\runtime\ps2_memory.h:240,397`) reached at least 234 in the 420 s capture run, and the
  same images come back across generations (e.g. `D_00212CB0` at generations 5-73): the game re-uploads the same
  static images a few times per second at most. No `.word 0x4AxxNN00` MPG pattern in the GAME/DEMO/NETWK overlay
  data (grep heuristic, not a full DMA walk). Not checked: whether `patch.c` `func_001D9810` patches the microcode
  (`docs/milestones/m4-overlays-3d.md:28-29`); a range hash catches that case anyway.
- VU0 micro mode: one wrapper `func_00102058` (`text.s:2462`: `ctc2 $vi27; vcallmsr`) called once from
  `text.s:2369`; whether it runs in play is unverified. Not in scope here.

### 1.2 What they do (from a disassembly of all 27; my own decoder, opcode tables as in PCSX2 `microVU_Tables.inl`)

Every program has the same skeleton (E-bit stops match the capture start pcs exactly, e.g. `D_00212BB0` stops at
0x80/0x220/0x640/0xB80 and the captures run MSCNT at 0x80/0x220/0x640/0xB80):

1. **0x0, MSCAL, ~15 cycles:** load constants (matrices, light vectors) from data qwords 1-17 into VF registers; stop.
2. **Dispatcher (MSCNT 0x58/0x70/0x80):** `XTOP` (double-buffer base), `ILWR` a header word, branch on its type
   (`IBEQ` to one of two stages), then a short loop copying header qwords; one stage does the per-object light/colour
   setup (`MUL`, `MINIi` against I = 128.0, `FTOI0`) and XGKICKs a GIF tag; stop; `B` back to the dispatcher.
3. **Vertex stage(s):** a software-pipelined loop of 40-60 pairs per vertex, count from the packet (`IADDI vi7,-1`,
   `IBNE vi7` back), XGKICK at the end, stop.

Families (by structure; counts from the disassembly):

| Family | Programs | Size | Per-vertex work | Data-dependent parts |
|---|---|---|---|---|
| Simple rigid (14) | 2790, 2800, 2830, 2860, 2890, 28C0, 28F0, 2920, 2950, 2980, 29F0, 2B00, 2B70, 2BE0 | 83-209 pairs | 4x4 transform, `DIV` perspective, 1 `CLIP` + `FCAND` (sets the GS ADC "don't draw" bit per strip vertex), `FTOI4` XYZ, colour, ST | vertex-count loop; header-type branch |
| Skinned (7) | 27D0, 29C0, 2A90, 2AD0, 2B40, 2BB0, 2C20 | 353-741 pairs | two bone indices per vertex by `ILW.x/.y` -> two 4x3 matrices by `LQ (vi10)/(vi11)`, weighted blend (`MULA/MADDA`), normal transform, lighting with `ERSQRT`/`RSQRT`/`MFP` and `MAX` 0 clamp, `MINIi` 128/255, projection, CLIP/FCAND | as above, plus gathered bone matrices |
| Rigid with clipper (6, + 2A90 skinned) | 2760, 2A20, 2A50, 2C50, 2C80, 2CB0 | 408-544 pairs | as simple rigid, plus a Sutherland-Hodgman polygon clipper: `BAL` into one routine per plane (+/-x, +/-y, +/-z via `vf31.x` sign), `FCGET`, edge intersection with `DIV`, `JR` returns (5 per program) | variable number of output vertices per triangle |

Example evidence: `D_00212BB0` vertex loop 0x348-0x5B8 (bone gather at 0x290-0x2E8, CLIP at 0x3E0, FCAND at 0x400,
ADC write at 0x588-0x5B0, `IBNE vi7` at 0x5B8); `D_00212C50` clipper 0x6F0-0x870 (12 `BAL`s), `JR` at 0xBB8, 0xC08,
0xC60, 0xDF0, 0xF20. All programs: D/T/M bits 0; no `RINIT/RNEXT`; flag reads are mostly `FCAND`/`FCGET` (clip flags);
`FMAND`/`FSAND` only in `D_00212B00` (1) and `D_00212B40` (2).

Interpretation that is **unverified**: the role of each flag bit (0x80, 0x300, 0x800, 0x20000 ...) and of the 0x4000 vs
0x8000 families (they look like "lit/unlit", "fog", "clip needed"); which programs run in other rooms. Only 6 of the
27 ran in the capture run.

### 1.3 What that means

- **C++ AOT:** data-dependent loops, branches on header words, `JR` return addresses from `BAL` and variable
  output are all ordinary control flow for C++. The `JR` targets are return addresses set by `BAL` inside the same
  program, so the static control-flow graph is closed (the generator must still enumerate `JR` targets from `BAL`
  sites and fall back on an unknown target).
- **Shaders:** the per-vertex body of the simple and skinned families is a fixed function of the vertex, the
  constants and two bone matrices: a vertex shader can do it. Three parts cannot be translated directly: the
  clip-flag ring (`FCAND` tests the clip flags of the current and previous strip vertices, a sequential dependency),
  the clipper (variable output; a GPU clips natively, so a native renderer drops it), and the GS-packet output
  (fixed-point `FTOI4` coordinates, GIF tags, ADC bits). These are reasons to re-implement, not translate.

## 2. AOT C++ design

### 2.1 What must be reproduced

From the VU1 pipeline (general VU knowledge; PCSX2 sources as reference: `pcsx2/x86/microVU_Analyze.inl`,
`microVU_Flags.inl:107` `mVUsetFlags`, `microVU_Misc.h:265` `noFlagOpts`):

- **Pairs:** upper (FMAC) and lower ops of a pair read their sources before either writes; if both write the same VF,
  the upper result wins. The I bit turns the lower word into a float immediate.
- **FMAC latency/stalls:** VF results are interlocked (the hardware stalls), so values follow program order; only the
  cycle count depends on stalls. The C++ result is then straight-line code; stall cycles are a per-block constant.
- **Q and P:** not interlocked except by `WAITQ`/`WAITP` or a following DIV/EFU op. A `MULq` issued before a `DIV`
  completes reads the old Q (DIV 7, RSQRT 13 cycles; EFU ops up to 54+). For a fixed instruction stream with a known
  entry state, which Q value each reader sees is a compile-time fact. The skinned programs depend on this
  (`DIV` at 0x350 and `MULq` at 0x390/0x398 in `D_00212BB0`, `ERSQRT`/`MFP` across loop iterations).
- **Flag instances:** MAC/status/clip flags are written 4 cycles after the FMAC issues; a reader sees the instance of
  its cycle. microVU computes only the flags a later instruction reads; the comparison on our captures shows the
  rest really is dead: STATUS differs in 39 and MAC in 44 captures, all other state, data memory and all 92 PATH1
  packets identical (`reports/2026-10-23-gameplay.md` entry 6-7; `C:\ob\logs\vucompare-0045d.log`: 76/120 bit-exact
  vs the interpreter, 37+39 of the rest are "flag = input value" patterns). AOT can do the same, with dead code
  elimination by the C++ compiler, provided the generator keeps the flags that **another program or the EE** reads
  (cross-program reads are unknown: nothing found in these programs, `FMAND/FSAND` appear 3 times in total).
- **Branches:** one delay slot; the E bit stops after the next pair; integer branches reading a VI written in the
  pair before see the hardware's "VI backup" value (microVU and ARMSX2's IR model this:
  `C:\ob\armsx2\pcsx2\arm64\vuaot\VuAotIR.h:6-21`, "the VI backup slot").
- **Clip ring:** CLIP shifts the 24-bit clip-flag register by 6; `FCAND` masks it. Kept as a variable.
- **PS2 floats:** no Inf/NaN, denormals as zero, results clamped. The verification reference is x86 microVU (the
  golden set `C:\ob\vu1-captures\golden`, 120/120 reproducible, `reports/2026-10-24-android.md` entry 7), so the C++
  must use the same clamp rules as microVU's default clamp mode with FTZ/DAZ on x86 and FPCR.FZ on arm64; NEON
  `fmax/fmin` NaN rules differ from SSE (ARMSX2 fixes #795/#835, `research/2026-10-24-armsx2-microvu-feasibility.md`).
  No fused multiply-add (`-ffp-contract=off`, the lesson of patches 0045/0047).
- **XGKICK:** at the kick, copy the GIF packet from data memory to `PS2Memory::submitGifPacket(Path1)`
  (`docs/ops/build.md:157`), as the current backends do. The kick point is a static pair index.
- **MSCNT:** "resume at the pc after the last E bit" (gameplay entry 4); the generated program has one entry per stop
  (3-4 per program) plus 0.
- **Cycle count:** returned to the EE scheduler. microVU and the interpreter already disagree (130,181 vs 142,123
  cycles over the same runs, `vucompare-0045d.log`); AOT should report microVU's count so EE timing stays the same
  as in today's shipping backend.

### 2.2 Generator

- Input: the ELF (descriptor table and chains, `vu_data.data.s` addresses above), not the captures: all 27 programs
  get generated, including the 21 that the captures do not reach.
- Steps: decode -> control-flow graph from entries {0, every E+16} plus `BAL` return sites -> pipeline pass per
  (block, entry pipeline state) as microVU does: "two entries into the same PC with different pipeline states are
  two nodes" (`C:\ob\armsx2\pcsx2\arm64\vuaot\VuAotGraph.h:6-19`) -> flag/Q/P liveness -> C++ per block with VF/ACC
  lanes in local `__m128`/`float32x4_t` values through a small shared vec4 helper (clamp, FTOI/ITOF, EFU tables
  identical to the interpreter's corrected ones, patch 0032).
- Reuse rather than re-derive: ARMSX2's `VuAotPipe` is "a port of microVU's first pass", checked against microVU's
  live decisions (`VuAotPipe.h:6-17`), and `VuAotIR` models "every piece of VU state as an SSA variable ... flags are
  values ... dead flag work is plain dead code" (`VuAotIR.h:6-21`). A host-side generator can borrow those passes
  (GPL-3.0+, same licence family as the PCSX2 code we already link) and emit C++ instead of arm64 code. Unverified:
  how cleanly they separate from microVU's block manager.
- Output: one `.cpp` per program, `program_<hash>(VU1State&, data, budget)` with a `switch` on the entry pc. Compiled
  once (a build input that changes only when the generator does; no rebuild of the 329 game units).

### 2.3 Runtime seam and fallback

- A third backend behind the external VU1 seam (patch 0033), selected as `PS2X_VU1=aot` / `outbreak.ini vu1 = aot`.
- Key: at MPG time (or lazily at MSCAL) hash the written range [0, size), not 16 KB; look up the table of 27; on a
  miss (unknown program, patched code, a `JR` to a target the generator did not see) hand the run to microVU or the
  interpreter at that boundary and count it in the log. Each stop is a clean hand-over point, since VU state lives in
  the shared state record.

### 2.4 Verification

- `vucompare` (`native\pcsx2_vu\compare\main.cpp`) gets an `aot` side: data memory, VF/VI/ACC/Q/P/I/R/CLIP and every
  PATH1 packet bit-for-bit against the x86 golden on all 120 captures; STATUS/MAC compared only where a reader exists
  (the same rule microVU passes). Same C++ on Android, checked against the same golden there (`vucompare-a64` path).
- Coverage gap: the captures exercise 6 of 27 programs and 30 entry/program combinations. Before trusting the other
  21, record captures in more scenes (other scenarios, cutscenes), and add per-program synthetic runs from the
  ELF (random but bounded vertex counts) compared against microVU.
- Tests that fail without: e.g. a Q-latency case (`MULq` one pair after `DIV` must read old Q).

### 2.5 Expected speed

- Today: microVU 0.67-0.73 ns per VU1 cycle on x86, weighted by in-game cycles 0.73 ns, interpreter 110-112 ns
  (`vucompare-0045d.log`); VU1 was 11.9% of samples in the J's Bar load phase with microVU (`playmvu-pc.csv`,
  gameplay entry 10). Hot program set: `D_00212BB0` + `D_00212C50` = 95% of cycles.
- AOT advantages: the vertex loops (40-60 pairs, hundreds of iterations) keep VF values in registers across
  iterations and across block links, where microVU writes them back at block ends; no per-block dispatch/cycle-check
  prologue; global dead-flag removal; the C++ compiler schedules the whole loop. ARMSX2 built a whole-program tier on
  top of microVU for exactly these reasons ("state can stay in host registers across their links",
  `VuAotRegion.h:6-28`; tier-up after 200 runs, `VuAotLive.h:45-49`), but gives no speed number in the files read.
- Disadvantages: the C++ compiler does not know VU semantics; poorly generated helpers (clamping by branches,
  `memcpy` of state per pair) would make it slower than microVU. MSVC's intrinsic codegen is weaker than clang's
  (general knowledge, unverified for this case): build the generated files with clang-cl.
- **Expectation:** x86 0.4-0.8 ns per VU1 cycle (parity to ~2x; a guess, unmeasured). The frame gain on Windows is
  then ~0-6% of frame time. On arm64 the comparison is against an unmeasured ARMSX2 microVU; AOT's code is the same
  as on x86 and goes through clang's NEON backend. The safe claim is "no JIT memory, one verified code base, no
  dependency on the fresh port". The speed claim needs the Odin (needs your "proceed").

### 2.6 Prior art (GitHub, fetched 2026-10-24)

- **OpenGOAL** (`github.com/open-goal/jak-project`, ISC, pushed 2026-10-22): the closest model. Its decompiler has a
  VU disassembler with C++ output (`decompiler/VuDisasm/VuDisassembler.cpp`, `VuDisassembler::to_cpp`); the
  generated code is a starting point that is then edited by hand: `game/graphics/opengl_renderer/Shadow_PS2.cpp`
  keeps the original VU instructions as comments next to C++ (`run_mscal_vu2c`). The `to_cpp` output does not model
  the pipeline (no delay-slot or Q timing logic seen; `CLIP` emits `ASSERT(false)`), so it is hand-checked per
  program, not exact. The hot paths (Merc2 skinning, TFRAG/TIE backgrounds) went further: native OpenGL renderers
  with bone matrices uploaded to the GPU (`foreground/Merc2.cpp`: "upload 'bones' (skinning matrix) for many draws").
- **N64Recomp** (`github.com/N64Recomp/N64Recomp`, MIT, pushed 2026-05-27): "RSP microcode can also be recompiled with
  this tool" (README) -- the N64's vector coprocessor, statically to C. Zelda64Recomp credits "RSP vector instruction
  reference implementations, used in RSP recompilation" and renders through RT64 (README), i.e. microcode for audio
  recompiled, graphics microcode replaced by a native renderer. Same split as this report recommends.
- **ps2kit** (`github.com/vs-sr-dev/ps2kit`, pushed 2026-10-20): EE to C++ recompiler; VU1 is a modelled interpreter
  on its own thread; "a translator for VU microcode" is listed under Next (README). No AOT yet.
- **PS2Recomp upstream** (`github.com/ran-j/PS2Recomp`): README says "Performance is very bad for VU and GS"; no VU1
  recompiler mentioned.
- **REO** (`github.com/RaccoonRecomp/ResidentEvilOutbreakRecompiled`): README says the runtime emulates the vector
  units; it does not say how. Earlier notes: ~35x slower than real time in 3D (`docs/milestones/m4-overlays-3d.md:55-56`).
- **ARMSX2 `vuaot/`** (local clone `C:\ob\armsx2` at `90126bd`, 18,574 lines): despite the name, not ahead of time
  in our sense. It is a runtime second tier above arm64 microVU: hot programs (200 runs) are rebuilt on a worker
  thread as SSA regions over microVU's own block graph and pipeline decisions, then swapped in; microVU stays the
  fallback at every block boundary; an optional on-disk program cache (`VuAotPersist`) keeps compiled code between
  sessions (`VuAotLive.h:6-29`). Off in our build (`research/2026-10-24-armsx2-microvu-feasibility.md`).
- Searches with no relevant hit: `api.github.com/search/repositories?q=vu1+recompiler` (only ps2kit),
  `q=ps2recomp+vu`, `q=PS2+VU+microcode+GPU` (0), `q=PS2+VU1+shader` (one repo, unrelated modding pipeline with an
  implausible star count, not opened further), `search/issues?q=repo:PCSX2/pcsx2+VU1+compute+shader` (10 unrelated
  game bugs). Not opened: Play! (`jpd002/Play-`) VU JIT, DobieStation, PCSX2 forum/wiki, paraLLEl-GS (GS only, not
  VU), RT64 sources.

## 3. Shaders

- **Prior art for PS2 VU1 on a GPU:** none found that runs VU1 microcode generically on a GPU. The working examples
  (OpenGOAL, and by analogy RT64 for N64) replace the microcode and the rasteriser together with a native renderer
  that knows the game's mesh formats.
- **Why VU1-on-GPU alone does not pay:** XGKICK packets feed PCSX2's GS, which parses them on the CPU (MTGS thread)
  before its own GPU work. Moving VU1 to a compute shader means a GPU-to-CPU readback per draw or batch, a sync point
  of the kind that already costs us at vsync (gameplay entry 10: the per-vsync snapshot readback). So shaders pay off
  only together with a native renderer, and that has to be said plainly: this is a renderer project, not a VU1
  optimisation.
- **Feasibility with a native renderer:** simple rigid and skinned families: yes, as vertex shaders per family (2
  bones per vertex, a bone palette in a uniform/storage buffer; mesh formats 0x221135/0x321135). Clip-flag ADC culling
  becomes ordinary GPU clipping plus strip restart; the clipper family is dropped (the GPU clips). What must stay on
  the CPU: the EE-side packet builders (`func_0019B290` etc.) or a replacement that turns the game's mesh data into
  vertex buffers, the render-state translation (`gfxstate.c` `func_00195970`, 112 commands,
  `docs/milestones/m4-overlays-3d.md:38-39`), texture state from GIF A+D writes, and anything the game reads back.
- **Expected gain:** at most the VU1 share (~12% of a J's Bar frame on Windows with microVU; less after AOT) plus the
  GS emulation cost of those draws, against weeks to months of renderer work and a second correctness reference
  (golden frames). Worth it for upscaled/modern rendering goals, not for VU1 speed.

## 4. Staged plan

| Stage | Work | Measure / done when | Effort (guess) |
|---|---|---|---|
| 0 | Profile now: VU1 share in J's Bar after IOP batching (hostprof, JIT bucket) on Windows; arm64 microVU ns/cycle and share on the Odin | numbers in the report; Odin needs "proceed" | 0.5 day |
| 1 | Capture corpus: more scenes (other scenarios/rooms) to reach more of the 27; list programs hit | program-id table with counts | 1 day + play time |
| 2 | Generator: decode, CFG, pipeline/flag/Q/P pass (reuse VuAotPipe ideas), C++ emission; first exact, then fast | builds 27 `.cpp`; compile time < 1 min | 1-2 weeks |
| 3 | Runtime backend `aot` (PS2Recomp patch with a fail-without test), range-hash dispatch, fallback to mvu/interp, miss counter | `[vu1] backend=aot` and 0 misses in play.script | 2-3 days |
| 4 | `vucompare --aot`: 120/120 vs golden (data, regs, packets; flags where read); synthetic runs for the 21 unseen programs | bit-exact; then gate 3/3 + J's Bar frames | 2-4 days |
| 5 | Speed: ns per VU1 cycle, J's Bar fps; Odin (proceed) | AOT <= microVU on both; else keep microVU as default | 1 day |
| 6 (later) | Native renderer + per-family shaders, only if a native renderer becomes a goal | separate design | months |

## 5. Risks

1. **Unseen programs and entry states.** 21 programs have no capture; a pipeline state at an entry that the
   generator did not enumerate produces silently wrong Q/flag values. Mitigation: range-hash fallback, more
   captures, synthetic runs vs microVU.
2. **Pipeline model errors** (Q/P timing, VI branch backup, flag instance at MSCNT boundaries, XGKICK timing).
   Mitigation: reuse microVU's analysis (as ARMSX2 did) instead of a new model; differential tests per entry.
3. **Float exactness on arm64** (NaN/min/max rules, FTZ, contraction). Mitigation: shared helpers, `-ffp-contract=off`,
   golden comparison on the device.
4. **Speed below microVU** if the generated C++ keeps state in memory per pair. Mitigation: measure in stage 2 on the
   two hot programs first; keep microVU as the default until AOT wins.
5. **Runtime code changes:** none seen (static images, no D/T/M bits, no overlay VU code by heuristic), but
   `patch.c` patches and a full DMA walk of the overlays are unchecked.
6. **Cycle accounting:** AOT must report microVU's cycle counts or EE timing changes (they differ from the
   interpreter by ~8%).

## Sources

Local (opened this session): `docs/ARCHITECTURE.md`, `PROJECT_CONTEXT.md:160,346`, `docs/milestones/m4-overlays-3d.md`,
`docs/android-port-map.md:59`, `docs/ops/build.md:103-162`, `docs/ops/android.md:52`,
`research/2026-10-23-microvu-feasibility.md:320-335`, `research/2026-10-24-armsx2-microvu-feasibility.md:105-120,292-301`,
`reports/2026-10-23-gameplay.md` entries 3-10, 25, `reports/2026-10-24-android.md` entries 6-7,
`C:\ob\vu1-captures\summary.txt` and the 120 `.cap` headers/code images, `C:\ob\logs\vucompare-0045d.log`,
`native/ob_diag/src/vu1_recorder.cpp:137-170`, `native/ob_diag/include/ob_vu1_capture.h:29,58-78`,
`C:\ob\splat\SLPM_654.28\en\main\text.s` (`func_001983E0`, `func_00102058`),
`C:\ob\splat\SLPM_654.28\en\main\data\vu_data.data.s` (chains and descriptor table),
`C:\ob\armsx2\pcsx2\arm64\vuaot\*.h` (headers only), `C:\ob\pcsx2\pcsx2\x86\microVU_Flags.inl:107`.
`C:\ob\decomp-src` holds only boot/loader sources (13 files); the rendering decomp named in the port map was not there.

Web (GitHub only, 2026-10-24): repos and READMEs above; `api.github.com/repos/open-goal/jak-project`,
`.../contents/game/graphics/opengl_renderer`, `.../foreground`, `.../contents/decompiler/VuDisasm`,
`github.com/open-goal/jak-project/blob/master/{decompiler/VuDisasm/VuDisassembler.cpp, game/graphics/opengl_renderer/Shadow_PS2.cpp, game/graphics/opengl_renderer/foreground/Merc2.cpp}`
(read through a summariser, so line-level claims about them are approximate), `api.github.com/repos/N64Recomp/N64Recomp`.

Unverified, stated as guesses: AOT speed range, effort estimates, the meaning of render-flag bits and side records,
clang-cl vs MSVC codegen, whether ARMSX2's passes separate from microVU's block manager.
