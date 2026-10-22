# The GPU GS

## Decisions

- **GPU GS = PCSX2's GS** (developer, 2026-10-22). It is the most mature, has proven texture
  replacement for the 4K pack, and reaches Android through the ARMSX2/Turnip lineage.
  paraLLEl-GS was rejected (no Android evidence, no texture replacement).
  Evidence: `research/2026-10-22-gpu-gs-options.md`.
- **Phase 1.3 (CPU GS fault fixes) stops** in favour of integrating PCSX2's GS (partner,
  2026-10-22, under the developer's "decide on efficiency" mandate). Generation 1's goal, "draws
  its boot and menu screens as PCSX2 does", is met faster by running PCSX2's GS than by fixing
  the CPU rasteriser fault by fault. The CPU GS stays as a selectable fallback, unfixed.
- **In-process, not a separate GS process.** We are GPL-3.0 through PS2Recomp, so a process
  split buys nothing, and it would be a burden on Android.
- **Windows first.** Android later: Vulkan only, ARMSX2's driver database, and a choice between
  the system driver and a custom (Turnip) driver loaded by the app, aimed at as many Android GPUs
  as possible, Mali included.

## Where it plugs in

See `research/2026-10-22-gs-seam-in-ps2recomp.md` (file:line map of the GIF choke point,
backend interface, privileged registers, readback, present and vblank).
