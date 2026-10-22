# outbreak-modern

A native, statically recompiled build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English
fan patch v2.1) with modern controls: Windows first, then Linux and Android.

The game's code is recompiled from your own disc at build time on PS2Recomp (with our numbered patches in
`upstream/ps2recomp/`), and drawn by PCSX2's GS built in as a library (`native/pcsx2_gs/`). No game data
is in this repository.

- What each part of the repo is for: `docs/ARCHITECTURE.md`
- Goals, stack and early decisions: `PROJECT_CONTEXT.md`; roadmap: `docs/milestones/`
- Build, run and test: `docs/ops/build.md`
- The earlier Modern Camera patch for ARMSX2 that this grew out of: `legacy/README.md`
