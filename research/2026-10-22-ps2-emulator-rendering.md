# PS2 emulator rendering on PC and Android (2026-10-22)

Purpose: find proven GS (and VU1) rendering work to reuse, instead of improving PS2Recomp's
scalar software rasteriser by hand. Research was done by a web retriever under the web-safety
rules (GitHub and well-known PS2 sources only, no APK/mirror/download sites). The partner saved
it here and added the local GameDB lookup. wiki.pcsx2.net and psdevwiki returned 403.

## Ranked candidates

1. **PCSX2** (GPL-3.0; `pcsx2/GS`). `Renderers/` holds Common, DX11, DX12, HW, Metal, Null,
   OpenGL, SW and Vulkan. `HW/` has GSRendererHW, GSTextureCache, GSHwHack and
   GSTextureReplacements. The GS core (GSState, GSLocalMemory, GSDrawingContext) is tied to the
   EE-side GS registers and the pcsx2 build (ARM64 GSVector variants and MultiISA exist). Lifting
   it means lifting a large part of pcsx2: usable as a reference or a fork, not as a drop-in
   library fed GIF packets.
2. **Play!** (jpd002/Play-, BSD 2-clause style, no source-disclosure requirement). Multi-platform
   including Android; Vulkan appears in its build docs. Renderer layout not confirmed. Best
   license fit.
3. **ARMSX2** (GPL-3.0; fork of PCSX2_ARM64 / PCSX2 2.x). Android/ARM64 focus, Adreno and Mali
   renderer work. Roadmap: Mali crashes (highest priority), Xclipse GPUs (high). Claims ARM64 JITs
   for VU0/VU1 (self-reported).
4. **DobieStation** (GPL-3.0, Qt5). Young, Android only planned. Low reuse value.
5. **AetherSX2 / NetherSX2**: closed source (CC BY-NC-ND notices). Not license-clean; reference
   only, no code.

## Licensing

- PCSX2, ARMSX2, DobieStation: GPL-3.0. Linking their GS code makes the distributed combined
  work GPL-3.0.
- Play!: permissive (BSD-style); embeddable with notices kept.
- AetherSX2: no redistributable source; do not copy.

## VU1 handling

- PCSX2 x86-64: microVU recompiler for VU0/VU1; interpreter as the reference.
- Upstream PCSX2 ARM64: VU interpreter only. ARMSX2 adds ARM64 VU JITs (self-reported complete).
- Play!, DobieStation: not verified.

## Outbreak in PCSX2's GameDB (local `ps2_emu/resources/GameIndex.yaml`, PCSX2 2.9.134)

| Serial | Game | compat | Fixes |
|---|---|---|---|
| SLPM-65428 (ours) | BioHazard Outbreak (JP) | 5 (perfect) | none; memcard filters only |
| SLUS-20765 | Resident Evil Outbreak (US) | 5 | EE patch by Refraction: "Removes silly mask causing high barriers on draws", `0019FF60 = 24050000` |
| SLUS-20984 | Outbreak File #2 (US) | 5 | the same patch at `001EE550` |

- No `gsHWFixes` for any Outbreak entry: PCSX2's hardware renderer draws it correctly with no
  per-game hacks. Good news for a GPU GS.
- The US "high barriers" patch suggests the game sets a draw mask that forces many
  texture-barrier flushes on a hardware renderer (a performance cost, not a correctness one). The
  JP entry has no patch. Whether the same code exists in SLPM-65428 is **unverified**; check by
  finding the function at US `0x19FF60` in the JP ELF via the decomp.

## Dead ends

- wiki.pcsx2.net/GSDX and wiki.pcsx2.net/Resident_Evil_Outbreak: 403.
- Wrong paths (404): the pcsx2 VU tree, Play!'s `Source/ps2`.
- Vulkan/D3D12 status claims came only from news articles, not primary sources.

## Sources (fetched 2026-10-22)

- https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS
- https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS/Renderers
- https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS/Renderers/HW
- https://github.com/jpd002/Play-
- https://github.com/jpd002/Play-/blob/master/License.txt
- https://github.com/ARMSX2/ARMSX2
- https://github.com/Trixarian/ARMSX2
- https://github.com/PSI-Rockin/DobieStation
- Search snippets only, not relied on: wiki.pcsx2.net Outbreak File #2 page, emulation.gametechwiki.com AetherSX2, androidauthority.com ARMSX2 Mali update, tech-insider.org (low authority).
