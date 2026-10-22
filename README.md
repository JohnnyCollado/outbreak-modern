# outbreak-modern

A native, statically recompiled build of Resident Evil Outbreak File #1 (SLPM-65428, JP v1.04 + English
fan patch v2.1) with modern controls: Windows first, then Linux and Android.

The game's code is recompiled from your own disc at build time on PS2Recomp (with our numbered patches in
`upstream/ps2recomp/`), and drawn by PCSX2's GS built in as a library (`native/pcsx2_gs/`). No game data
is in this repository.

- Goals, stack and decisions: `PROJECT_CONTEXT.md`
- Roadmap and live phase: `GENERATION_PLAN.md`, `phase-ends/current/PHASE_PLAN.md`
- Build, run and test: `HOW_WE_WORK.md`, `docs/ops/`
- The earlier Modern Camera patch for ARMSX2 that this grew out of: `legacy/README.md`
