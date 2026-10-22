# outbreak-modern

Native recompilation of Resident Evil Outbreak File #1 (SLPM-65428) with modern controls: Windows first,
then Linux and Android.

- What each part of the repo is for, and every source used so far: `docs/ARCHITECTURE.md`
- Build, run, test, environment and decisions in force: `docs/ops/build.md` (index: `docs/ops/INDEX.md`)
- Graphics (PCSX2's GS in-process): `docs/ops/gs.md`
- Past phase plans, research notes (R-ids) and task summaries: `docs/history/`

Working rules:
- Every PS2Recomp change is a numbered patch in `upstream/ps2recomp/` (made as a commit in `C:\ob\up`), with a
  test that fails without it.
- The gate decides correctness (`docs/ops/gate.md`); its tolerance is frozen.
- Log every issue and its fix in the phase's struggle report in `reports/` as it happens.
- Game data never enters git. Never push.
- Long commands go through `tools/run.sh`; long builds and gates run in the background.
