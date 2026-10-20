# Milestone 4: overlays and 3D

## Goal

From the design: "SUBMAIN/GAME loaded, VU1 programs recompiled; a room on screen." Texture
compression and per-room streaming of the 4K pack also come here (design Decision 6, "Order").

## Done when

- SUBMAIN.BIN and GAME.BIN (and DEMO.BIN, if M3 didn't do it) are recompiled and run, each
  selected by which overlay is resident.
- The first room of the File #1 offline scenario draws on both platforms and matches PCSX2
  golden frames within the M3 threshold.
- The 27 VU1 programs selected by `memory.c` `func_001983E0` (port map, Rendering) each pass a
  test against a reference.

## Depends on M0, M2, M3

## Work

1. **Overlays.** File 1 has 8 overlays in `MWo3` format (decomp LAYOUT.md: id, load address,
   .text/.data/.bss sizes, static-init range). Several share addresses: DEMO/GAME/NETWK at
   0x540000, NETDNAS/NETHTTP at 0x6C0000, SINGLE/NETAQ at 0x820000. So:
   - recompiled functions are keyed by (overlay, address), not by address alone;
   - an indirect call into an overlay range dispatches to whichever overlay is resident;
   - the overlay's .data and .bss are loaded into guest RAM as on PS2 (the game expects them at
     those addresses), and its static initialisers run where `overlay.c` runs them;
   - `patch.c` `func_001D9810` patches applied after load: if they hit code, the recompiled
     version must include them. Which records exist on the disc is **unknown**.
2. **Split the VU microcode out of the main ELF** (0x1d9b00 onward, "upper-NOP 0x000002FF pairs";
   decomp LAYOUT.md). The decomp lists this as still to do (ROADMAP Phase 1). Find the 27
   programs `D_00212760`…`D_00212CB0` refer to.
3. **VU1 programs.** The design: recompile each microprogram to C first (exact and shared by both
   platforms), then move them one by one to vertex shaders. Needs a VU1 instruction decoder, its
   paired upper/lower instructions, the VU's float rules, XGKICK (VU1 sends GS packets
   straight to the GS), and the VIF1 unpack commands that feed VU memory. The decomp's ROADMAP
   says the opposite of emulating ("never emulate VU1"). See README, corrections.
4. **3D state in the renderer.** 112 render-state commands in `gfxstate.c` `func_00195970`,
   matrices in the scratchpad (0x70003700-0x70003FFF), projection (`projection.c`).
5. **Texture pack at scale.** An offline converter to BC7 (Windows) and ASTC (Android) with
   mipmaps, per-room loading on a background thread, an LRU memory budget, bindless textures
   (Decision 6).

## What's needed

- Reference VU1 results to test against. PCSX2 (its VU interpreter, or captures of VU memory
  before and after a program) is the practical source. REO uses PCSX2 captures the same way.
- A save state or scripted input that reaches the first room the same way every time (a pad
  script like REO's `PS2X_PAD_SCRIPT`).
- The Odin for 3D measurements (your "proceed").

## Limitations

- **This milestone hides most of the rendering work.** The overlays, VU1 and 3D state are each
  large. REO's experience with the same game is the clearest data point: in 3D it is ~35x slower
  than real time on a PC, with VU1 and GS emulated (its README). Whether recompiling the VU1
  programs avoids that slowdown is **unverified**.
- **Shaders per microprogram** only work if each program's behaviour is fixed per draw (no
  data-dependent loops that a shader can't do). Unknown until each is read.
- **GPU skinning** (mesh format `0x221135`/`0x321135` with bone weights) depends on the VU1
  program for skinned meshes being turned into a shader.

## Unknowns

- **What each of the 27 VU1 programs does**, how long it is, and whether any modify themselves
  or depend on VU state left by the previous one. Not read yet. The microcode isn't even split
  out yet.
- **Whether the game uses VU0 microprograms** (not just VU0 macro mode). The port map only
  mentions macro mode (`vu0.h`, 45 functions). **Unknown**: search the code for `vcallms` /
  `vcallmsr`.
- **Whether recompiled VU1 code is fast enough on the Odin** before the shader step. Not
  measured.
- **How the texture pack's memory budget fits the Odin**: how much RAM your Odin 3 has is
  **unknown** here.
- **The ASTC block size**: the design suggests 6x6. The quality and speed trade-off on the Odin
  is unmeasured.
