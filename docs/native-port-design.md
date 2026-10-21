# Native Windows + Android port: design proposal

Goal: one codebase that builds a native Windows game, a native Android game (Odin 3 first) and a
native Linux game (added 2026-10-21) of
Biohazard Outbreak File #1, from the player's own disc, and that can later play online on Obsrv
**with PS2, PCSX2, Windows and Android players in the same room** (cross-play).

Status: proposal, for discussion. Nothing built yet. Builds on `docs/android-port-map.md` (what
has to be implemented) and the decomp's own plan (its ROADMAP Phase 5/6). Per-milestone plans,
and findings from 2026-10-20 that change parts of this design, are in `docs/milestones/`.

## The requirement that drives everything: cross-play

Obsrv relays the game's in-game packets without reading them, and PS2 peers interpret them
directly (decomp docs/NETWORK.md). So a native peer must:

1. **send exactly what a PS2 sends** -- run the game's own network code unchanged, and
2. **compute what a PS2 computes** wherever peers depend on each other's simulation -- same
   30 Hz tick, same random numbers, **same floating-point results**.

Point 2 is the subtle one. The PS2's FPU is not IEEE: no infinities or NaNs (it clamps), no
denormals, always rounds toward zero, and some instructions differ (we hit one: its `sqrt.s`
takes its operand from another register field than standard MIPS, `docs/phase1-feasibility-report.md`).
A port that runs the game's maths in ordinary x86/ARM floats can drift from a PS2 peer: harmless
in single player, a desync online. So the design has to make **one EE-exact maths layer** that
both Windows and Android use. If both ports share it, they can't drift from each other either.

## Decision 1: how the game's code runs -- static recompilation + decomp, per function

About 88% of File 1 is still assembly (~1,660 of ~14,500 functions in C). Three ways to run it:

| | Wait for the decomp | Embed an interpreter/JIT | **Static recompilation** |
|---|---|---|---|
| What | build only from decompiled C | interpret the MIPS at run time | translate every MIPS function to C at build time, from the user's disc |
| Playable when | years (decomp ROADMAP: comparable projects took 2-6 years) | soon, but it's an emulator again | when the recompiler and the platform layer are done |
| Speed on the Odin | native | interpreter slow; a JIT is a big project per CPU (x86 and ARM) | native (C compiled for each CPU) |
| Cross-platform | yes | a JIT per CPU | yes: the generated C is the same for both |
| EE-exact maths | must be written by hand into all C | natural | natural: the recompiler emits calls to the EE-exact layer |

**Proposal: static recompilation, with decompiled C taking over function by function.** A
recompiler reads the user's ELF and overlays (never stored in the repo, same rule as the decomp),
turns each MIPS function into a C function that works on a modelled CPU state, and links them with
the platform layer. When the decomp has a function in portable C, that one is used instead. The
game runs end to end from day one of the recompiler, and gets more readable as the decomp grows.
This is the approach of N64Recomp-style projects; for the PS2 it means also handling the EE's
128-bit registers, MMI instructions, VU0 macro mode and the FPU's quirks in the generated code.

Trade-off: generated C is unreadable and big, and bugs in the recompiler look like game bugs. The
emulator tests we already have (unicorn + the EE instruction rules learned in `mod/build.py`) are a
start for testing it instruction by instruction.

## Decision 2: guest memory -- every game pointer is a 32-bit PS2 address

The decomp's PC port is a 32-bit build so the game's structs keep 4-byte pointers. Android on the
Odin 3 is 64-bit only, and a 32-bit Windows build is a dead end too. **Proposal: the game's 32 MiB
(plus the scratchpad) live in one block, and every pointer the game stores is a PS2 address.** On
both platforms: reserve a 4 GiB virtual range, map the guest RAM at its start, and a guest address
becomes a host pointer by one addition (`base + addr`); anything outside the mapped RAM faults
instead of corrupting memory (Windows `VirtualAlloc`, Android `mmap`).

Why this is right for cross-play too: the game's data layout is then identical on PS2, Windows and
Android, byte for byte, which is what packets and save files are made of.

Consequence for decompiled C: it has to follow the same model (guest addresses, not native
pointers) to be mixed with recompiled code. That is a rule to agree with the decomp project, or a
mechanical rewrite on our side (their `PS2_RAM()` macro is already this idea for fixed buffers).

Bonus: the Modern Camera mod (`mod/`) is already written as reads and writes of game addresses
(`MEM(type, addr)`); in the native build it compiles unchanged with `MEM` pointing into guest RAM.

## Decision 3: one platform layer for both

| Need | Choice | Why |
|---|---|---|
| Window, input, controllers, rumble, audio device, file dialogs, Android activity | **SDL3** | one API on Windows, Android and Linux (SDL3's Android support covers the activity, game controllers and storage access; on Linux it covers X11 and Wayland) |
| Graphics | **Vulkan** on all three | Windows, Linux (Mesa and vendor drivers) and the Odin's Adreno have it: one renderer, no D3D/GL split |
| Audio output | SDL3 audio (or miniaudio) | WASAPI and AAudio behind one API |
| Network | a thin sockets shim (Winsock / POSIX) + **mbedTLS** | the KDDI/DNAS pages need legacy TLS ciphers that modern OS stacks refuse |
| Build | **CMake** for everything; Gradle + the NDK only wraps it for Android | one build description |

## Decision 4: replace the system libraries at the call (HLE), share all of it

Everything in `docs/android-port-map.md` becomes one shared library, written once:

- **Sony libraries and the EE kernel** (`sce*`, threads, semaphores, interrupts): the recompiler
  routes calls to their known addresses (the decomp's symbol files name them) to native code.
- **Rendering**: GS packets and VU1. Start with a GS-packet interpreter on Vulkan (the decomp's
  suggested reference path), and run the **27 VU1 microprograms recompiled to C** first (exact and
  shared), then move them to shaders one by one. 2D (logos, title, text) works before 3D.
- **Sound**: a native reimplementation of Tamsoft's sound driver commands (they're in `sdr.c`) on a
  software mixer; ADX and VAG decoders.
- **Files**: ISO + AFS reader. **Saves**: files behind the memory-card API. **Movies**: FFmpeg.
- **IOP**: never emulated; each module is replaced by the native service it stood for.

## Decision 5: the game's clock

The logic runs at its original 30 Hz tick everywhere (online play needs it), driven by the
platform's display timing; rendering may later interpolate on top (the decomp's plan too).

## Decision 6: visuals and performance (4K texture pack, high frame rates)

The rule that keeps cross-play safe: **everything here is presentation only**. Nothing changes
the 30 Hz logic, the game's memory or what it sends; online, peers can't tell (decomp
NETWORK.md: PC-only options must be off online or purely visual -- these are purely visual).

### Texture replacement, at the source

An emulator has to guess which texture a draw uses from GS memory (PCSX2 hashes the texture's
pixels and palette as they sit in GS memory, plus its format and size, `GSTextureReplacements.cpp`
`TextureName`; that's why its packs need texture caching and miss textures that move). The native
port knows exactly: every texture goes through Capcom's texture manager (decomp `fltex.c`, TIM2
pictures). So:

- When `fltex.c` loads a TIM2 picture, compute its **PCSX2 key** (pixel hash, palette hash,
  format, size) from the TIM2 data, once, and look the replacement up then. **PCSX2-format packs
  then drop in unchanged** -- the same pack works in ARMSX2/PCSX2 and in both native ports. (The
  hash has to match PCSX2's exactly; verified per texture by dumping keys from both.)
- The replacement is bound wherever the game draws that texture; the renderer samples it with
  normalised coordinates, so a 4K texture fits where a 256x256 one was.
- Palette animations (one picture, changing CLUT) are keyed by palette hash, as in PCSX2.

### 4K textures without running out of memory

A 4096x4096 RGBA texture is 64 MiB uncompressed (85 MiB with mipmaps). A room uses dozens. So:

- **Convert the pack once, offline** (a tool, at install time) to GPU-compressed formats with full
  mip chains: **BC7** for Windows, **ASTC** for Android (Adreno supports ASTC; mobile GPUs don't do
  BC). ASTC 6x6 is ~3.6 bits/pixel: a 4K texture becomes ~7 MiB (~10 MiB with mips).
- **Load per room, ahead of time.** The game loads textures per room (`charload.c`, `enemyload.c`,
  room archives); start reading the room's replacements when the game starts loading the room, on a
  background thread, and fall back to the original texture until a replacement is ready -- never
  stall a frame.
- **A memory budget** with least-recently-used eviction (PCSX2 has one too); on the Odin a
  smaller mip can be dropped first.
- **Bindless textures** (Vulkan descriptor indexing) so switching textures costs nothing per draw.

### Frame rate and resolution

- **Interpolated rendering**: the logic stays at 30 Hz; the renderer draws at the display's rate
  (60/120 Hz) by interpolating object transforms and the camera between the last two logic ticks.
  The camera mod's own camera can run at the display rate directly (it's ours).
- **Internal resolution scale** (1x-4x of 640x448) and widescreen (the projection is ours to set),
  MSAA/FXAA; aspect handled in the projection, not by stretching.
- **VU1 programs on the GPU**: once each microprogram is a vertex shader, the CPU no longer
  transforms vertices; skinned meshes (`0x221135`) become GPU skinning.
- **Batching**: the game's many small draws collapse into few Vulkan draws per pipeline state
  (pipelines created ahead and cached to disk, so no stutter on first use).
- **CPU**: the recompiled code is native; the hot paths (VU0 maths, 128-bit copies) get SSE/NEON
  helpers.
- **Android specifics**: frame pacing through the swapchain (Android Frame Pacing / Swappy) and
  performance hints (ADPF), so the Odin holds clocks without overheating; lower-resolution mips
  when the device runs hot.

### Order

Texture replacement comes with the first textured renderer (milestone 3); compression and per-room
streaming with the 3D milestone (4); interpolation, resolution scale and widescreen after single
player works (6), each switchable and off by default online if in doubt.

## Hardware tiers (targets, to be measured)

Estimates from the design's requirements, not measurements: nothing is built yet. What sets the
floor: a 64-bit CPU (guest memory reserves a 4 GiB range), Vulkan with descriptor indexing
(bindless textures), and memory for textures. The game itself is light (a 294 MHz CPU's work at
30 Hz, 32 MiB of guest RAM); the 4K pack is what costs.

| | Minimum (original textures, 1-2x resolution, 60 fps) | 4K texture pack (2-3x resolution) |
|---|---|---|
| Android | Android 10+, 64-bit, Vulkan 1.1 + descriptor indexing (Adreno 6xx / Mali-G72 class, e.g. Snapdragon 660-730), 3 GB RAM | Snapdragon 865 / 8 Gen 1 class GPU or newer, 8 GB RAM, ~2-4 GB storage for the converted pack |
| Linux | **Unknown, not estimated yet.** Unverified guess: near the Windows row, since it runs the same x86-64 code with Vulkan through Mesa or vendor drivers | Unknown |
| Windows | Windows 10 64-bit, x64 with SSE4.1, Vulkan 1.2 GPU (GTX 650 / Radeon HD 7000 / Intel HD 500 or newer), 4 GB RAM | 8 GB RAM, 4 GB VRAM (GTX 970 / RX 470 class or newer) |

Every tier runs the same logic and maths, so tiers never affect cross-play. Ways to widen later: a
GLES 3.0/OpenGL fallback renderer (older Android; no bindless), and 1K/2K tiers converted from the
same pack.

**Reference devices**: one low-end device per platform (e.g. a Snapdragon 7-series phone, a
GTX 1050 PC) plus the Odin 3, measured at every milestone (frame time, memory, temperature on
Android), so the published minimum is a tested fact.

## Beta testing

When single player works end to end (milestone 6), open a beta:

- **Builds**: Windows zip and an Android APK from the same commit, by CI; the player supplies
  their own disc image (nothing from the game is distributed, same rule as the decomp).
- **Who**: testers across the tiers above, so the minimum is confirmed or corrected on real
  hardware (GPU/driver families matter more than speed on Android: Adreno, Mali, Xclipse,
  PowerVR).
- **What they send**: an in-game report (device, GPU/driver, settings, frame-time and memory
  stats, the last seconds of log) -- the same idea as `f1_aimlog.py`, built in.
- **Online beta later** (milestone 7), on a local server first, then on Obsrv only with the
  admins' agreement; mixed rooms (PS2/PCSX2 + Windows + Android) are the cross-play test.

## Linux (added 2026-10-21)

Linux is a third target. It costs little because every choice above already covers it: SDL3,
Vulkan, CMake, POSIX sockets (shared with Android) and `mmap` for guest memory (shared with
Android). PS2Recomp, the chosen base (`docs/milestones/m0-decision.md`), builds on Linux with GCC
and Clang once SSE4.1 is enabled; that fix is in `upstream/ps2recomp/` (2026-10-21; 445/445 of
its tests pass with both compilers). Linux is also where CI and the cross-play test server
(BioServer, which runs in Docker) are easiest to run.

What is not known yet: how SteamOS / Steam Deck and Flatpak packaging would fit (user-built
binaries can't go on Flathub, same distribution rule as the other platforms), and whether a
Linux arm64 build is wanted.

## Repository layout (a new repository)

```
outbreak-native/
  recomp/            the MIPS -> C recompiler (reads the user's ELF/overlays; output not committed)
  runtime/           guest memory, EE CPU state, EE-exact FPU/MMI/VU0 helpers
  hle/               Sony libs, kernel, IOP services, sound driver, CRI ADX/Sofdec
  gfx/               GS packet interpreter, VU1 programs, Vulkan backend
  platform/          SDL3 glue: window, input, audio, storage
  net/               sockets + TLS for Obsrv (later)
  game/              decompiled C (the decomp as a submodule) and our mods (Modern Camera)
  apps/windows/      CMake target
  apps/android/      Gradle + NDK wrapper around the same CMake
  apps/linux/        CMake target
  tests/             recompiler instruction tests (EE semantics), golden-frame tests
```

This repository (`outbreak-modern-camera-mod-android`) stays the ARMSX2 patch and its tools.

## Milestones (each ends with something that runs on both platforms)

1. **Skeleton**: CMake + SDL3 + Vulkan window on Windows, Linux and the Odin; guest memory; ISO/AFS reader.
2. **Recompiler, main ELF**: boots `main()` to the first `sceGsSyncV`; EE instruction tests pass
   (FPU rules included).
3. **2D**: GS packet interpreter -- boot image, logos, title screen; pad input.
4. **Overlays + 3D**: SUBMAIN/GAME loaded, VU1 programs recompiled; a room on screen.
5. **Sound**: sound driver commands, ADX music; then saves and movies.
6. **Single player end to end** (File #1 scenario offline), Modern Camera built in.
7. **Network**: Obsrv lobby, then a room; cross-play test PS2/PCSX2 + Windows + Android against a
   local server first (the decomp's Phase 6 plan), live only with the Obsrv admins' agreement.

## Questions to settle before building

1. Separate repository (`outbreak-native`, proposed) or a folder here?
2. Static recompilation as the base (proposed), or contribute to the decomp's own PC port and wait
   for coverage? (We couldn't read Outbreak-PC from here; if it's available, its platform layer
   could be reused instead of written.)
3. Talk to the decomp project about the guest-memory rule for C, so its C can be mixed in?
