# Other Outbreak recomps: RaccoonRecomp and Ninjacornix/reo (2026-10-22)

Purpose: find out whether other Outbreak recomps are ahead of us and what we can learn or
legally reuse. Research was done by a web retriever on GitHub only (no binaries or installers
opened); the partner saved it here.

## Answer

RaccoonRecomp is the furthest along by its own claims: a shipped Windows installer (v1.5.1,
2026-10-13), MIT license, menus, gameplay, memory card and mods. It runs only the US disc
SLUS-20765 v2.00 and rejects JP. Ninjacornix/reo is a GPL-3.0 fork of sp00nznet/reo on
PS2Recomp with paraLLEl-GS, alpha, also US-only. Neither supports our SLPM-65428 + English patch.
Neither mentions a web lobby app.

## RaccoonRecomp/ResidentEvilOutbreakRecompiled

- Owner: a single user account, RaccoonRecomp. Repo created 2026-10-06, last push 2026-10-13,
  12 stars, 1 contributor, 8 commits. The README credits Claude (Anthropic) for AI-assisted
  programming.
- License: MIT. Bundles LLVM 23.1.2 (Apache-2.0 with LLVM Exceptions).
- Base: never names PS2Recomp or a pin; describes its own "PC runtime written for the project".
  **Unverified.** If it is in fact derived from PS2Recomp (GPL-3.0), the MIT license would not
  hold for that code, so treat it as unclear.
- Method: reads the executable and overlays from the user's disc and translates them "instruction
  by instruction into C++ that is compiled for the PC", a one-time 3-6 minute step on first run.
- Target: SLUS-20765 US v2.00 only. US 1.01, EU and JP recognised but unsupported. No File #2.
- Status (README and release notes, not verified): boot, menus and gameplay "working"; 30 fps
  with experimental 60 fps; widescreen checked in the first scenario only. No online or lobbies.
- Sound: claims the game's own IOP sound driver with SPU2 emulation.
- Memory card: virtual card in PCSX2 `.ps2` layout, with backups.
- Graphics: D3D11 by default, a Vulkan renderer, a software reference renderer; upscaling, MSAA,
  FXAA.
- Platforms: Windows 10/11 x64 only.
- Current wall: issue #2 (2026-10-21) "Major lag/slowdown ingame (sub-10 fps)". Issue #1 asks
  for modern TPS controls.
- Mods: a sandboxed mod framework, with separate mod repos including **REO-ModernCam** (a
  Modern Camera mod), REO-FirstPerson and REO-Cheats. This overlaps our project's modern-controls
  goal directly.

## Ninjacornix/reo (fork of sp00nznet/reo)

- Fork created 2026-10-08, last push 2026-10-22; 38 commits ahead and 73 behind upstream. The
  file-level diff was not obtained (too large).
- Renderer: GPU rendering through **paraLLEl-GS**, Vulkan required (README).
- Built on a pinned PS2Recomp submodule with local patches; pin not retrieved.
- Target: SLUS-20765 only (checks the boot executable's SHA-256).
- Status: v1.0.12 experimental pre-release. Boots to menus and gameplay; rendering, performance
  and cutscene/audio sync in progress. Release page calls online co-op experimental, while
  upstream says online is out of scope.
- Windows x64 only (.NET 8 runtime, Vulkan); Linux "in development".
- License: GPL-3.0.

## sp00nznet/reo (upstream)

Created 2026-02-21, last push 2026-10-21, GPL-3.0, 33 stars, 99 commits. Reaches J's Bar
headless at ~35x slower than real time; no sound, no memory card mount, FMV green blocks.

## What this means for us

- Ahead on polish: RaccoonRecomp. Ahead on transparency: Ninjacornix/reo (PS2Recomp plus
  paraLLEl-GS, the same stack we are weighing).
- Reuse: Ninjacornix/reo is GPL-3.0 and so compatible with our GPL-3.0 project. RaccoonRecomp is
  MIT on paper, with the base question above open.
- Neither handles JP SLPM-65428 + the English patch; that stays ours.

## Not opened

heysnippy.com and the Inno Setup site (off GitHub); the RaccoonRecomp mod repos; any release
binaries or installers.

## Sources (fetched 2026-10-22)

- https://github.com/RaccoonRecomp/ResidentEvilOutbreakRecompiled (page, README, releases, and the GitHub API for repo, contributors, issues and commits)
- https://github.com/Ninjacornix/reo (page, README, and the GitHub API for repo and the compare against sp00nznet:main, truncated)
- https://github.com/sp00nznet/reo (page and GitHub API)
