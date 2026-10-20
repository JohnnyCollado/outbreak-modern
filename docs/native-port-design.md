# Native Windows + Android port: design proposal

Goal: one codebase that builds a native Windows game and a native Android game (Odin 3 first) of
Biohazard Outbreak File #1, from the player's own disc, and that can later play online on Obsrv
**with PS2, PCSX2, Windows and Android players in the same room** (cross-play).

Status: proposal, for discussion. Nothing built yet. Builds on `docs/android-port-map.md` (what
has to be implemented) and the decomp's own plan (its ROADMAP Phase 5/6).

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
| Window, input, controllers, rumble, audio device, file dialogs, Android activity | **SDL3** | one API on Windows and Android (SDL3's Android support covers the activity, game controllers and storage access) |
| Graphics | **Vulkan** on both | Windows and the Odin's Adreno both have it: one renderer, no D3D/GL split |
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
  tests/             recompiler instruction tests (EE semantics), golden-frame tests
```

This repository (`outbreak-modern-camera-mod-android`) stays the ARMSX2 patch and its tools.

## Milestones (each ends with something that runs on both platforms)

1. **Skeleton**: CMake + SDL3 + Vulkan window on Windows and the Odin; guest memory; ISO/AFS reader.
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
