# A native renderer instead of GS emulation? (2026-10-24)

Question: can Outbreak draw from the game's own rendering intent (render-state commands, meshes, textures, matrices)
instead of emulating the GS (Graphics Synthesizer, the PS2's rasteriser chip) at the register/packet level, without
losing speed or visual accuracy, and what would that unlock (widescreen, high resolution, high frame rate and
interpolation, the 4K pack, Android efficiency)?

Desk research only: local docs, the splat output, the VU1 captures, and github.com pages fetched 2026-10-24. Nothing
was built or run. "Unverified" marks guesses and things not opened.

Terms used below:
- **VU1**: the PS2's second vector unit; runs small "microprograms" that transform/light vertices and send finished
  GS primitives with **XGKICK**.
- **VIF1**: the unit in front of VU1 that unpacks the game's DMA lists into VU1 memory and starts programs
  (**MSCAL**/**MSCNT**).
- **GIF packet / PATH**: the command stream into the GS; PATH1 = from VU1, PATH2 = VIF1 DIRECT, PATH3 = DMA.
- **A+D**: a GS register write inside a packet (blend, Z test, texture registers...).
- **CLUT**: a texture palette.
- **Frame interpolation**: drawing extra frames between two logic ticks by blending object transforms.

## Verdict

- **Yes, for the 3D, and it is the only route to some of the goals; but "no loss of accuracy" means "within a
  tolerance against PCSX2 in the same run", never bit-exact.** PCSX2's own hardware renderer already differs from
  its software renderer (gate baseline: `pcsx2-vk` savedata share 0.003353 vs `pcsx2-sw` 0.000000,
  `gate/baseline-1.3.md:21,28`). A native renderer will differ at least as much.
- **Best cut point: the VU1 boundary in the VIF1 stream** (cut "b-prime" below): recognise each batch the game hands
  to a known VU1 program (by code hash), read its unpacked vertices, matrices and GS state, and draw it as a GPU mesh
  with a vertex shader instead of running the program. Everything else (2D, text, fades, FMV, any not-yet-ported
  program) keeps going to PCSX2's GS. This is what OpenGOAL's bucket renderers, BT3-Recomp's "engine seam" and
  Ace Combat 5's recomp do (Prior art).
- **Challenge to the premise:** widescreen, high resolution and the 4K pack do **not** need a native renderer.
  PCSX2 already upscales (internal resolution), already has texture replacement (stubbed in our build,
  `docs/ops/gs.md:223-224`), and widescreen on PCSX2 is normally a game patch to the projection (EE side, works with
  any GS). What only the native renderer gives: **frame interpolation to 60/120 fps** (needs object transforms, which
  the GS never sees: VU1 sends it screen-space vertices), **no VU1 on the CPU** (the Android win), texture
  replacement keyed at the source instead of by GS-memory hashing (`docs/native-port-design.md:106-120`), and
  upscaling free of GS-memory aliasing tricks.
- **So the order matters:** do widescreen/upscale/texture pack on the PCSX2 path first (cheap), and treat the native
  renderer as the interpolation + Android-efficiency project it really is.

## 1. Where the cut can be

### (a) GS packet level, renderer tuned for this game

What PCSX2 does generically: take the GIF packets and draw them on the GPU. A custom one could drop features
Outbreak never uses. Costs: VU1 must still run (it produces the packets), the input is still screen-space
(no interpolation, no clean widescreen), and the hard part of a GS renderer (GS memory aliasing, texture cache,
CLUT handling) remains. Gains over PCSX2 are small; we already have PCSX2 at 60 fps on Windows
(`reports/2026-10-23-gameplay.md:524-525`). **Not worth it.**

### (b) The game's graphics library level (before DMA)

Hook the game's own calls: `gfxstate.c` `func_00195970(cmd, arg)`, mesh batching in `memory.c`, `fltex.c` uploads.
The richest information, but:
- most of the game is not C (1,660 of ~14,500 functions; `docs/android-port-map.md:16-19`), so these are
  recompiled MIPS functions operating on guest RAM;
- our rule: hook points never live in generated game code (`docs/ARCHITECTURE.md:96`);
- the state these functions set is only half the story: the actual per-draw data (packed vertices, which matrix
  slot, GS A+D state) is assembled into DMA lists further down.
**Use it for knowledge, not as the seam.**

### (b-prime) The VU1 boundary in the VIF1 stream (recommended)

Every 3D mesh reaches VU1 as a VIF1 DMA list: UNPACKs of vertex data into VU memory, matrices and lights copied in,
an A+D/GIFtag template, then MSCAL/MSCNT at a known start address (`docs/android-port-map.md:43-46,59`). The runtime
already parses this stream (`processVIF1Data`, 14.2% of the game thread in J's Bar,
`reports/2026-10-23-gameplay.md:532-535`). At that point a runtime seam (a PS2Recomp patch, like `PS2ExternalVu1`)
can see, per batch: the program (code hash + start pc), the object-space vertices, the matrices, fog/light constants
and the GS state template. That is the "game's intent" for 3D, without touching generated game code, and it works
across overlays (GAME.BIN, SUBMAIN) automatically.

### (c) Per-feature hybrid

In practice (b-prime) is a hybrid: native for batches whose VU1 program is ported, PCSX2 GS for the rest
(2D DIRECT/PATH3 packets, text, fades, Sofdec movies, unported programs). Ace Combat 5's recomp states exactly this:
what the native renderer does not cover "still goes through the emulated GS, into the same frame" (Prior art).

### Outbreak specifics (decomp names, local map)

| What | Where | Notes |
|---|---|---|
| Render state | `gfxstate.c` `func_00195970(cmd, arg)` (decomp `src/SLPM_654.28/main/gfxstate.c`, fetched 2026-10-24) | cmds `0x1A-0x39` store a 4x4 matrix into the scratchpad (32 slots at `0x70003700`, file :54, :62-68); `0x3A-0x59` lights into `D_002767A0` (:69-73); `0x5A-0x5C` a Fog struct into `D_00276660` plus a reciprocal in `D_00276648` (:74-82); `0x0E` colour with PS2 alpha (0xFF -> 128.0, else halved, :182-200: the GS's "0x80 = 1.0" convention); flag words `D_0023BA90` "render flags" and `D_0023BA8C` "GS state flags" (:23-24) pushed through `func_00197330`, `func_00197080`, `func_00197B10`, `func_00196F00`, `func_00197A50` (what each writes: **unverified**). The file writes no GS register by name. |
| GS state packets | `gsstate.c` `state_packet` (port map :57) | A+D per context: these become pipeline state |
| Projection | `projection.c` `func_0018F0E0`, `func_0018F2B0` (port map :58); gfxstate cmd `0x18` calls `func_0018F2B0(0x22, ...)` | the widescreen lever on either path |
| Mesh submission | `memory.c` `func_001983E0` picks one of 27 VU1 programs by flags and mesh format (`0x21035`/`0x121035` plain, `0x221135`/`0x321135` bone-weighted); batching `func_00189A40`, `func_00189D60`, `func_0018BA20`, `func_0018AEA0` (port map :59) | the 3D seam |
| DMA send | `dmaq.c`; `func_0018CF10` (`sceDmaSend`) is the hot sender, 26.6% of samples in loading (`reports/2026-10-23-gameplay.md:194-197`) | |
| Textures | `fltex.c` (`func_0018C0B0`...`func_0019CD70`), `tim2.c` (port map :60) | TIM2, 4/8-bit CLUT + 16/24/32-bit |
| 2D | `sprite2d.c`, `fade.c`, `font.c` (port map :61-62) | stay on PCSX2's GS |
| VU1 code actually run | `C:\ob\vu1-captures\summary.txt` (play.script to J's Bar, 495,616 runs) | **8 distinct micro-memory images, 30 (kind, pc, hash) keys**, starts at 0x0 then MSCNT stages (0x58, 0x70, 0x128, 0x528, 0x640, 0xb80...). Heaviest: image `e6fecedd...` stages 0x640/0xb80 (~458M and ~410M cycles), `8b65d74c...` 0x528 (~398M). Far fewer than "27 programs" to port first. |
| VU1 microcode | `C:\ob\splat\SLPM_654.28\en\main\data\vu_data.data.s` (ELF 0x1d9b00-0x23ba00, not split, `docs/milestones/m4-overlays-3d.md:30-32`) | needs a VU disassembler pass |
| GS readback | splat SDK symbols: `sceGsExecLoadImage` x7, **no `sceGsExecStoreImage`** (grep of `C:\ob\splat\SLPM_654.28`) | no libgraph framebuffer-to-EE readback found; a hand-rolled one is not ruled out (**unverified**) |

**Effects (what is known, what is not):**
- Fog: real, per the Fog struct above; on PS2 fog is a per-vertex value (computed by the VU1 program) blended with
  FOGCOL by the GS. A shader can do this exactly if the VU1 fog formula is copied.
- Alpha blending with PS2 semantics (`(A-B)*C>>7 + D`, alpha up to 0x80 = 1.0, see `0x0E` above), alpha test,
  destination alpha: expressible on Vulkan with fixed blend for the common modes; some modes need shader blending.
- A draw mask "causing high barriers" in the **US** build (PCSX2 GameDB EE patch at `0x19FF60`,
  `research/2026-10-22-ps2-emulator-rendering.md:44-51`): PCSX2 issues texture barriers when a shader must read the
  render target (frame-buffer write masks or blend modes it cannot do in fixed function). Whether JP has it and what
  the mask is: **unverified**. A native renderer would use a colour write mask if it is per-channel.
- 16-bit dithering (DTHE): open 1.4 bug "access logo stippled" (`docs/ops/gs.md:220-221`), 2D, stays emulated.
- No `gsHWFixes` for any Outbreak serial in PCSX2's GameDB (`research/2026-10-22-ps2-emulator-rendering.md:39-47`):
  a sign there are no exotic GS-memory tricks a hardware renderer trips on. Shadows, screen filters, render-to-texture,
  local-to-local copies in gameplay: **not checked**. Stage 0 below measures them.

## 2. Prior art (github.com, fetched 2026-10-24)

- **RT64 + Zelda64Recomp (N64).** RT64 records a whole frame of draw calls before drawing, runs vertex transforms
  in a GPU compute shader, and offers widescreen, ultrawide, higher resolution, DDS texture packs and **frame
  interpolation**: it matches draw calls between consecutive frames (render parameters, textures, position,
  velocity), interpolates the transforms after decomposing them into components, then recombines them; "draw call
  matching is experimental"; MIT (https://github.com/rt64/rt64, latest commit 2026-10-24 "configurable present
  wait"). Zelda64Recomp uses RT64, keeps original framebuffer/depth effects without hacks, renders objects, terrain,
  texture scrolling and most HUD at high frame rates, with game-side patches from the decomp
  (https://github.com/Zelda64Recomp/Zelda64Recomp). N64 is easier: the RSP display list already carries
  object-space vertices and matrices, which is what we only get on PS2 by cutting before VU1. RT64 also exposes an
  "extended GBI" (extra display-list commands) so ports can tag matrix groups for interpolation (README; the exact
  `gEXMatrixGroup` use in Zelda64Recomp not opened).
- **UnleashedRecomp (Xbox 360).** "A new renderer was written from scratch to translate the game's draw calls to
  modern APIs"; Xenos shaders converted by XenosRecomp; D3D12 + Vulkan 1.2; 60 fps default and unlocked, high res,
  ultrawide, pipelines compiled in background workers; GPL-3.0 (https://github.com/hedge-dev/UnleashedRecomp).
  The 360 had real draw calls; the PS2 has no such layer, which is the main difference.
- **OpenGOAL (Jak, PS2): the closest example.** Native x86-64, "shouldn't be emulated, interpreted, or
  transpiled" (https://github.com/open-goal/jak-project). Its OpenGL renderer is split per draw type
  (`game/graphics/opengl_renderer`: background, foreground, sprite, ocean, sky, shadow, eye, warp, depth cue...).
  `foreground/Merc2.cpp` walks the **game's own DMA chain** (handles VIF STCYCL/UNPACK/MSCAL/FLUSHA), unpacks
  vertices on the CPU, uploads bone matrices for many draws at once, takes fog constants and envmap fade from the
  data the game sent, and does blend-shapes ("blerc") on the CPU with SIMD: that is the VU1-boundary cut done by hand
  per renderer. `DirectRenderer.h` interprets raw GIF/A+D packets (ALPHA, TEST, ZBUF, TEX0, PRIM) for debug and
  text: their "small GS for 2D". Dates: not visible on the pages (fetched 2026-10-24). How long each renderer took:
  **not checked**.
- **PS2 recomps with native rendering (2026):**
  - **BT3-Recomp** (z3xox, PS2Recomp fork): "Native Vulkan (engine seam)" draws "at the point where it hands
    geometry to VU1" with host meshes and GPU vertex programs; the GIF stream is interpreted only for the 2D layer;
    `PS2X_SEAMVK_REF=1` keeps paraLLEl-GS running beside it for side-by-side dumps; no native performance numbers;
    known issues include render-to-texture mismatch and shadow blending differences
    (https://github.com/z3xox/BT3-Recomp, latest commits 2026-09-30/10-01).
  - **Ace Combat 5 static recomp** (sal063): VU1 programs for aircraft, terrain, sky, clouds rewritten as native code
    drawn as GPU meshes with vertex shaders, mipmaps and anisotropic filtering; a "census" mode records the
    microprograms the game uploads; `vurecomp` turns the rest into C; what is not covered goes through the emulated
    GS "into the same frame"; pipeline cache on disk (https://github.com/sal063/Ace-Combat-5-Static-recompilation,
    "Reworked renderer" commit 2026-09-18).
  - **God of War recomp** (KIexster): VU1 microprograms compiled ahead of time to C++, an OpenGL GS; README says heavy
    scenes miss 60 fps with VU1 + GS as the critical path (search snippet only, **not opened**).
  - **RaccoonRecomp/ResidentEvilOutbreakRecompiled** (same game, US): D3D11/Vulkan/software renderers, 2x-4x
    internal resolution, MSAA/FXAA, experimental 60 fps and "Aspect Ratio: Expand" widescreen checked in the first
    scenario only; how the 60 fps or widescreen is done is not stated
    (https://github.com/RaccoonRecomp/ResidentEvilOutbreakRecompiled, v1.5.1).
- **Not opened:** ico-recomp, berserk-ps2-recomp (search snippets only); RT64's extended-GBI docs; any OpenGOAL
  write-up of renderer effort; themaister.net.

## 3. Performance and accuracy

### Today (basis)

- Windows J's Bar: 60 vsync/s, pacing-capped (`reports/2026-10-23-gameplay.md:524-525`). Game thread 0.57 cores:
  `processVIF1Data` 14.2%, microVU JIT 9.5%; GS thread 0.22 cores, of which ring spin 37.3%, present 21.0%, actual GS
  draw code ~5% (`reports/2026-10-23-gameplay.md:532-537`).
- Android: J's Bar "very slow" on the Odin with the VU1 interpreter (`reports/2026-10-24-android.md:92`); ARMSX2's
  ARM64 microVU is built but not run on the device yet (same report :214). No on-device frame numbers.

### What a native 3D path changes

- **CPU:** the covered programs no longer run (the 9.5% microVU share on Windows; on Android the interpreter's whole
  share, which today dominates). VIF1 parsing stays but gets lighter (UNPACK data goes to a GPU buffer instead of
  being expanded into VU memory). PCSX2's per-draw GS work for those batches disappears. Windows has headroom already;
  **the CPU win matters on Android**. Size of the win: **unmeasured**.
- **GPU:** at native resolution both are cheap. When upscaling, PCSX2's costs are its texture cache, render-target
  copies and barriers; ARMSX2's numbers show render-target reads can be the frame cost on Adreno (54.7 -> 22.4 ms in
  one game with a feedback-loop path, `research/2026-10-22-gpu-gs-options.md:273-275`). Native 3D draws are ordinary
  meshes: batched, instanced bone matrices, bindless textures (`docs/native-port-design.md:133-150`), no GS-memory
  emulation. Expectation: same or lower GPU cost; **unverified** until measured.
- **Risk to performance:** running both paths during the transition (side-by-side reference) costs double; that is
  a debug mode only.

### How to keep accuracy

- **2D screens keep passing the frozen gate unchanged**, because 2D stays on PCSX2's GS and the gate judges with
  `PS2X_GS=pcsx2-sw` (`gate/baseline-1.3.md:28-31`); the gate runs no VU1 before GAME.BIN anyway
  (`reports/2026-10-23-gameplay.md:166-172`).
- **3D needs its own check, not the frozen tolerance.** Proposal: a "3D compare" mode that, in one run, sends each
  frame both ways (native, and VU1 -> PCSX2 at 1x as reference), like BT3's `PS2X_SEAMVK_REF`. Same-run comparison
  is required because the J's Bar scene is not deterministic run to run (`reports/2026-10-23-gameplay.md:543`).
  Metric: share of pixels over a per-pixel delta, plus a per-draw check on the CPU reference (below) which can be
  exact. "Enhanced" mode (upscaled, widescreen, interpolated) is allowed to differ and is checked by eye.
- **Two-step VU1 replacement for exactness:** first a CPU reimplementation of each program verified **exact**
  against microVU on the existing captures (`native/pcsx2_vu` vucompare, `C:\ob\vu1-captures`), then the shader,
  verified against the CPU version within float tolerance. The PS2's non-IEEE float rules (clamping) are where shader
  results drift (`research/2026-10-22-ps2-architecture.md:46`).
- Expected unavoidable differences: rasterisation edge rules and sub-pixel precision (PS2 12.4 fixed point vs GPU
  rules), 16/24-bit Z precision, PS2 texture-coordinate rounding. Small pixel shares at edges, not visible at 1x
  (guess based on the vk-vs-sw gap above).

## 4. Effort and a staged plan

Smallest proof: **J's Bar room geometry drawn natively, side by side with PCSX2's output, in the same run.**

0. **Census (days).** A diag-plugin pass over J's Bar: per frame, which VU1 program hashes run and how often, which
   GS state combinations (blend/test/fog) they emit, and flags for the hard cases: TEX0 pointing into a frame/Z
   buffer (render-to-texture), local-to-local transfers, local-to-host reads, FBMSK use. Result: a table that says
   which 1-2 programs draw the room and whether any effect needs GS-memory tricks. The VU1 capture summary already
   gives the program list (8 images, 30 keys).
1. **Understand one program (1-2 weeks, guess).** Split the microcode, disassemble the image that draws static room
   geometry (likely the plain `0x21035` format), write its CPU reimplementation (inputs from VU memory, outputs the
   GS primitives it XGKICKs), verified exact against the captures. This is where data-dependent loops, clipping and
   culling inside the program show up (`docs/milestones/m4-overlays-3d.md:58-59`).
2. **Native draw, side by side (1-2 weeks, guess).** A runtime seam at MSCAL for that program hash: copy the
   batch (vertices, matrices, state) to a native Vulkan renderer drawing into its own debug window/target, while
   microVU + PCSX2 still draw the real frame. Compare per frame. Proves: we can draw the room from game intent.
3. **Composite into one frame.** The real design decision. Options: (i) draw native batches into PCSX2's render
   target at the right point in the GS command stream (one ordered stream; needs patches to PCSX2's Vulkan HW
   renderer under `upstream/pcsx2/`); (ii) native renderer owns the frame and draws 2D through a small GIF
   interpreter for sprites (OpenGOAL's DirectRenderer route; needs GS-memory texture decoding, i.e. reuse of PCSX2's
   GSLocalMemory). Fades and HUD that blend over 3D make naive "3D below, PCSX2 on top" compositing wrong.
4. **Cover J's Bar:** characters (bone-weighted programs, GPU skinning), the rest of the census list; per-program
   fallback to microVU + GS for anything not ported (Ace Combat 5's model).
5. **Enhancements:** interpolation (match batches frame to frame by program + mesh address + texture, interpolate
   decomposed matrices, disable on camera cuts, as RT64 does), widescreen via projection (`func_0018F2B0`),
   upscaling, texture pack keyed at `fltex.c`.
6. **Android:** measure on the Odin against the PCSX2 path (with "proceed").

Must stay emulated: Sofdec FMV (until the movies milestone replaces it), 2D menus/text/fades (the gated screens),
any unported VU1 program.

Dependency on VU1: the native path needs each program **understood** (inputs, maths, outputs), not recompiled.
microVU stays as the fallback and as the reference. VU1 AOT (to C) is optional and only helps the fallback.

## 5. Risks

1. **Composition order** (stage 3): native 3D and emulated 2D in one frame, with fades and HUD blending over 3D.
   Biggest design risk.
2. **VU1 program semantics:** per-vertex fog and lighting formulas, clipping/culling inside the program, PS2 float
   clamping; any data-dependent behaviour blocks a plain vertex shader.
3. **GS blend/test semantics** (0x80 = 1.0 alpha, destination alpha, FBA, the "barrier" mask): some need shader
   blending, which is slower on tile-based mobile GPUs.
4. **Unknown effects** (render-to-texture, shadows, screen filters): not checked; stage 0 decides.
5. **Interpolation artefacts:** fixed-camera cuts, objects appearing/disappearing, UV-scroll and particle effects;
   RT64 calls its matching "experimental". 2D stays at 30.
6. **Widescreen with fixed cameras:** the game likely culls to 4:3 on the EE, so edges pop or show unfinished
   geometry (RaccoonRecomp marks theirs experimental, first scenario only). Same risk on the PCSX2 path.
7. **Two renderers to maintain** and the per-PCSX2-bump re-patching if option 3(i) is chosen.
8. **Effort:** months for full 3D coverage (guess; OpenGOAL-scale work per draw type, though Outbreak's 8 images
   are a small set).

## Sources

Local: `docs/ARCHITECTURE.md:96`; `docs/native-port-design.md:100-160`; `docs/android-port-map.md:16-19,41-66`;
`docs/milestones/m4-overlays-3d.md:30-75`; `docs/ops/gs.md:81-96,220-224`; `gate/baseline-1.3.md:15-53`;
`reports/2026-10-23-gameplay.md:166-198,524-537,543`; `reports/2026-10-24-android.md:92,214`;
`research/2026-10-22-gpu-gs-options.md:273-275`; `research/2026-10-22-ps2-emulator-rendering.md:39-51`;
`research/2026-10-22-ps2-architecture.md:44-47`; `research/2026-10-22-other-outbreak-recomps.md:20-50`;
`C:\ob\vu1-captures\summary.txt`; `C:\ob\splat\SLPM_654.28\en\main\data\vu_data.data.s` and SDK-symbol grep.

Web (github.com only, fetched 2026-10-24):
- https://raw.githubusercontent.com/GalaxyNostalgia/Outbreak-decomp/main/src/SLPM_654.28/main/gfxstate.c
- https://github.com/GalaxyNostalgia/Outbreak-decomp
- https://github.com/rt64/rt64 and /commits (2026-10-24 latest)
- https://github.com/Zelda64Recomp/Zelda64Recomp
- https://github.com/hedge-dev/UnleashedRecomp
- https://github.com/open-goal/jak-project, docs/project-overview.md, game/graphics/opengl_renderer (+ foreground/,
  foreground/Merc2.cpp, DirectRenderer.h)
- https://github.com/z3xox/BT3-Recomp and /commits (2026-09-30, 2026-10-01)
- https://github.com/sal063/Ace-Combat-5-Static-recompilation and /commits (2026-09-15 to 2026-09-19)
- https://github.com/RaccoonRecomp/ResidentEvilOutbreakRecompiled

Not opened (search snippets only): KIexster/god-of-war-recomp, nathanialf/ico-recomp, fenrircl/berserk-ps2-recomp,
podulator/GoldenEye64Recomp PR #1, the Zelda64Recomp interpolation PR.
