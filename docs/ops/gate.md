# The gate (phase 1.2)

Source: phase-ends/current/PHASE_PLAN.md Context/Interfaces; GENERATION_PLAN.md `## The gate`.
Compares the runner's six scripted captures against PCSX2 reference screenshots, per screen: max per-channel delta
and share of pixels with delta > d; green = share <= p. d and p are frozen in `gate/tolerance.json` (rule P9).

## Files
- References: `C:\ob\ref\<screen>-<a|b>.png` (+ `sheet.png`, `session.json`); never committed.
- `gate/ref_manifest.json`: PCSX2 version, effective settings, capture session, sha256 per reference.
- `tools/gate/refs.py`: `check-settings` (PCSX2.ini vs the reference settings), `begin` (session marker),
  `intake [--dry-run]` (snaps newer than the marker -> screens in boot order, 512x448 asserted, copy + manifest + sheet).
- Capture procedure and settings: `docs/ops/reference-capture.md`.
- Screens (boot order): savedata, access, capcom, title, mainmenu, charselect; runner side `C:\ob\frames-<T>\<screen>-<k>.png`.

## Display-mode probe (T1, 2026-10-22)
GS privileged registers (PMODE, SMODE2, DISPFB1/2, DISPLAY1/2) are not readable without a patch: the script's
read lambda (`C:\ob\up\ps2xRuntime\src\lib\Kernel\EeScheduler.cpp:1897-1906`) reads RDRAM only (`addr & 0x01FFFFFF`),
so `watch 0x12000020` silently reads RDRAM 0x20. No env switch traces them. FBW and SMODE2 (field/frame) unknown.
