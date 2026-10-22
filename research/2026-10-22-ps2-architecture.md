# PS2 architecture primer for the Outbreak recompile (2026-10-22)

Purpose: understand the hardware a recompile has to stand in for, block by block. Research was
done by a web retriever; the partner saved it here. psdevwiki and wiki.pcsx2.net returned 403,
so this is partial. One source (themaister.net) was opened before the web-safety rule; it is
the blog of Hans-Kristian Arntzen, a well-known emulator/Vulkan developer, and is kept.

## Blocks and what each means for a recompile

- **EE (R5900, ~294.9 MHz):** 32 x 128-bit GPRs, 2-way superscalar, 16 KB I$, 8 KB D$, 16 KB
  scratchpad. Recompiles natively as integer/MMI code; the 128-bit MMI ops need hand-mapped
  SSE/NEON. The FPU (COP1) is not IEEE 754: no infinities, overflow clamps to max-float. That has
  to be emulated, or results diverge.
- **VU0/VU1:** VU0 runs in micro or macro mode (COP2 instructions in macro). VU1 is micro-only,
  has 4x VU0's memory and an EFU, and feeds the GS through the GIF. It is the usual geometry
  stage. Its microcode can be recompiled as separate kernels, with synced timing.
- **VIF/GIF/DMAC:** the DMAC moves 128-bit batches, with slice mode. PATH1 = VU1 XGKICK,
  PATH2/PATH3 = DMA to the GIF. VIF1 stalls on the microprogram and on GIF drain (PCSX2 issue
  #3024). FIFO and arbitration have to be modelled: the order of PATH1 vs PATH2/3 is visible to
  the game.
- **GS (4 MiB VRAM, 147.46 MHz):** fixed function, no shaders, no multitexture. VRAM is 8 KiB
  pages of 32 blocks each, Z-order swizzled; PSMCT32 = 64x32 px per page; 24-bit colour leaves
  the top 8 bits usable for textures. Palettes: PSMT8, and 4HL/4HH through spare MSBs. The CLUT is
  a 1 KiB cache that must be loaded explicitly. Blending goes above 1.0 (0x80 = 1.0, max ~2.0).
  The destination-alpha test works as a pseudo-stencil; the alpha test can discard colour but
  keep the Z write. Texture/framebuffer feedback (drawing over pixels being sampled) is the
  hardest case.
- **IOP (R3000A, 37.5 MHz, 2 MB RAM):** runs sound, CD and pads, and talks to the EE over SIF
  (DMA on both sides). Later models use a PPC440 "DECKARD" to run the MIPS code.
- **SPU2 (~36.86 MHz, 2 MB, 48 voices):** replaceable with a native audio backend, but timing,
  envelopes and ADPCM must match. It is also a sync point for game logic.
- **CDVD:** MechaCon controller; 48x CD (3.69 MB/s), 4x DVD (5.54 MB/s). Replace with ISO
  sector reads; seek latency matters for loading.
- **Timing:** the hardware is cycle-based across blocks. PCSX2 needs per-game timing hacks
  (XGKickHack, EETimingHack, instantVU1, VUSyncHack, FullVU0SyncHack; GameIndex.md). Games
  expect specific stall and sync behaviour, so the units cannot be treated as free-running.

## Top hard problems

1. GS feedback/readback and blending above 1.0 do not map to UNORM GPU targets: they need
   programmable blending (ROV or per-primitive barriers). Valkyrie Profile 2 went from 500+
   barriers to 18 with hazard tracking (themaister).
2. GS memory layout: the swizzled, paged VRAM has to be emulated for readback and post-FX.
   PCSX2's linear 32-bit approach breaks those effects.
3. EE/VU non-IEEE floats: clamping choices change geometry (the "spiky polygon" bugs).
4. VU/EE/GIF timing and sync: XGKICK and VIF stalls are visible to game logic.
5. Audio/CD timing: SIF and IOP latency affect loading and sync.

## Lead to verify: paraLLEl-GS

The themaister.net post describes **paraLLEl-GS**, a GS renderer written as Vulkan compute by
Hans-Kristian Arntzen. Checked by the ecosystem retriever (see
`2026-10-22-ps2recomp-and-ecosystem.md`): github.com/Arntzen-Software/parallel-gs, written from
scratch (not GSdx), LGPL-3.0+ ("dual-licensing may be possible"), no external contributions,
PCSX2 integration "very experimental". A PS2Recomp project (Def Jam Vendetta) already uses it.
Its fit for Android (compute-heavy Vulkan on Adreno/Mali) is unknown.

## Source gaps

- psdevwiki.com/ps2 (VIF, VU, GS, EE pages): 403, not read.
- wiki.pcsx2.net "Nightmare on Floating-Point Street": 403, search snippet only.
- hotchips.org Kunimatsu EE paper (PDF): not opened (download, outside the rules).
- VU macro-mode details, PSMT8/CLUT blend equations, SPU2 specifics: not found.

## Sources (fetched 2026-10-22)

- https://www.copetti.org/writings/consoles/playstation-2/ (changelog through 2026-09-06)
- https://themaister.net/blog/2024/07/03/playstation-2-gs-emulation-the-final-frontier-of-vulkan-compute-emulation (post dated 2024-07-03)
- https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Docs/GameIndex.md
- Search results only: https://github.com/PCSX2/pcsx2/pull/4726, https://github.com/PCSX2/pcsx2/issues/3024
