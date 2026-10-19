# Outbreak Modern Camera on ARMSX2 (Android)

Research project: run a modern over-the-shoulder camera for Resident Evil Outbreak
File #1 / #2 (JP) on ARMSX2 on the AYN Odin 3 Max.

- `docs/phase1-feasibility-report.md`: Phase 1 research and Phase 2 test plan
- `tools/phase2_check.py`: automated Phase 2 checks (PC → ADB → ARMSX2 PINE). Needs Python 3.8+ and adb
- `tools/pine_diag.py`: read-only PINE diagnostic client (`--bench N`, `--read ADDR`)

Quick start (PC with the Odin attached over USB, PINE enabled, game booted, save state made):

    python tools/phase2_check.py

Results land in `results/`.
