# PS2Recomp's standing and the PS2 recomp ecosystem (2026-10-22)

Purpose: decide whether PS2Recomp is solid ground, what else exists, and how other recomps got
their graphics. Research was done by a web retriever (GitHub only once the web-safety rule
arrived); the partner saved it here.

## Verdict

PS2Recomp is a real, actively maintained, GPL-3.0 project, not an abandoned fan repo, and it is
alpha. The only RE Outbreak result on it is reo, which reaches the first scenario with no sound
at ~35x slower than real time.

## 1. PS2Recomp (github.com/ran-j/PS2Recomp)

- Owner ran-j, an individual (a Brazilian programmer, per timeextension 2026-01-27).
- Created 2025-04-12, last push 2026-10-14. 3,644 stars, 205 forks, 183 open issues,
  169 commits, wiki, Discord.
- The 16 most recent commits (2026-07-21 to 2026-10-14) have 7 authors: ran-j 6, smmathews 3,
  drakolordx7 2, Sinan-Karakaya 2, three others 1 each.
- "Inspired by N64Recomp" only; no formal tie to Wiseguy found.
- Its own README: "Hardware emulation is partial and many paths are stubbed"; "Performance is
  very bad for VU and GS". No compatibility list.
- Built on it (recompedia.com, third-party list, lower trust): Urban Reign (playable, v1.3.0,
  Oct 13), Def Jam Vendetta (in progress, **paraLLEl-GS on Vulkan**), Burnout 3, God of War,
  Shadow of the Colossus, Killzone, reo. Recompedia says "most" PS2 recomps use it.

## 2. Other PS2 recomp and decomp efforts

- **sp00nznet/reo** (Outbreak File #1 US, SLUS-20765): alpha, boots to J's Bar. No SPU2 sound,
  memory card doesn't mount, online "out of scope for now", File #2 builds but doesn't boot. Uses
  the PS2Recomp runtime. GPL-3.0 (fetched README). 33 stars.
- **RaccoonRecomp/ResidentEvilOutbreakRecompiled** (v1.5.1, Oct 13) and **Ninjacornix/reo**:
  search results only, **not opened**. Worth a look.
- **OpenGOAL** (Jak 1 and 2 playable): decompiles GOAL; mature, different approach.
- **BlizzHacker/ps2-web-recomp** (fork of snuri00): its own recompiler "eerecomp", MIT, WebGPU
  graphics, runs GT4 and God of War II. No Outbreak, no online.

## 3. How other recomps got graphics

- **Zelda64Recomp (N64):** reuses RT64, a standalone renderer (GPL-3.0).
- **UnleashedRecomp (Xbox 360):** a new renderer from scratch; emulating Xenos "is not required
  in a recompilation". Shaders converted by XenosRecomp.
- **reo (PS2):** runs the game's VU1 and GS on PS2Recomp's software path; PCSX2 used only as a
  black-box reference. This is why it's slow.
- **paraLLEl-GS** (github.com/Arntzen-Software/parallel-gs): Vulkan compute GS emulation, written
  from scratch, LGPL-3.0+ ("dual-licensing may be possible"), no external contributions, PCSX2
  integration "very experimental".
- Unverified (snippets only): Urban Reign and Ridge Racer V use the PCSX2 GS; a God of War
  recomp uses OpenGL.
- Pattern on PS2 so far: run the runtime's own GS (reo), or adopt a GS emulator (paraLLEl-GS, or
  a PCSX2 GS fork).

## 4. PCSX2's GS as a library

- GPL-3.0, historically a plugin (GSdx), no documented standalone extraction.
- Seen in use as a separate process: gt4-staticrecomp PR #24 "PCSX2 GS in a separate process"
  (gt4-gs.exe), which dropped paraLLEl-GS (PR title and snippet only).
- Linked in-process, GPL-3.0 makes the whole binary GPL-3.0. PS2Recomp is already GPL-3.0, so
  this project is GPL-3.0 either way.

## Outbreak online lobby web app

**Not found** on GitHub. Closest: erizodev/pal-outbreak-2-server (fork of Koosjuh's), a Node.js
DNS-only private server for PAL File #2; real PS2s play online after a DNS change, lobby and room
protocol reverse-engineered, two players confirmed, no license, 0 stars. Not a web app.

## Web-safety compliance

- Not opened: NeoGAF (403), recompsoftware.com, portedgames.com, winbuzzer.com,
  retrohandhelds.gg, dsogaming.com, context7.com.
- Opened before the rule arrived, lower trust: recompedia.com/platform/ps2,
  timeextension.com (2026-01-27).

## Sources (fetched 2026-10-22 unless noted)

- https://github.com/ran-j/PS2Recomp (plus the GitHub API for repo stats and commits)
- https://github.com/sp00nznet/reo
- https://github.com/erizodev/pal-outbreak-2-server
- https://github.com/BlizzHacker/ps2-web-recomp
- https://github.com/Zelda64Recomp/Zelda64Recomp
- https://github.com/hedge-dev/UnleashedRecomp
- https://github.com/Arntzen-Software/parallel-gs
- https://recompedia.com/platform/ps2 (before the rule)
- https://www.timeextension.com/news/2026/01/native-pc-ports-of-ps2-games-could-be-on-the-way-thanks-to-new-recompilation-experiment (2026-01-27, before the rule)
- https://github.com/moesuito/gt4-staticrecomp/pull/24 (search snippet only)
