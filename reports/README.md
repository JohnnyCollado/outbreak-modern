# Reports

Logs of the problems hit while working on this project, and how each was worked out. They're
for reviewing the project later and for sharing with other developers: someone picking the work
up should be able to see what went wrong, why, what was done about it, and what's still open,
without replaying the sessions.

`docs/` holds plans and designs (what we intend). `results/` holds measurements and test outputs
(what a tool printed). `reports/` holds the story in between: struggles, causes, fixes, and the
lessons learned.

## How to write one

- Copy [`TEMPLATE.md`](TEMPLATE.md) to `YYYY-MM-DD-<short-topic>.md`. One report per working
  session or per piece of work, whichever is smaller.
- One entry per struggle, in the order they happened. Each entry: what was seen, how it was
  found, the root cause, the fix, the evidence (commit, patch, test, log), and what to take
  from it.
- **Include mistakes.** Wrong claims, wrong estimates, wasted detours: say what was claimed,
  how it was found to be wrong, and where it was corrected. These are the most useful entries
  for a reviewer.
- Mark anything not verified as **Unverified** or **Unknown**, as in the rest of the docs.
- Reference evidence by commit hash, patch number or file path, not "see above".
- **No game data**: no code, disassembly listings, assets or file contents from the disc. Addresses,
  function names, file names, sizes and hashes are fine.

## Index

| Date | Report | Covers |
|---|---|---|
| 2026-10-21 | [Native port bring-up](2026-10-21-native-port-bring-up.md) | Milestone plans, choosing PS2Recomp, nine PS2Recomp fixes, first recompile and boot of SLPM-65428 up to the data archives |
| 2026-10-21 | [Windows bring-up: to the main menu](2026-10-21-windows-port-bank-wall.md) | Patches 0010-0018, the sound bank wall, overlays as guest modules; the boot reaches the main menu |
| 2026-10-21 | [Project Architect groundwork and boot speed](2026-10-21-pa3-groundwork-and-boot-speed.md) | First PA3 plan, `tools/ob.sh`, patch 0019, the profiler deadlock, aggressive logs halving the boot speed |
| 2026-10-22 | [The gate: reference captures and a pixel tolerance](2026-10-22-gate.md) | Phase 1.2: PCSX2 reference capture kit, display-register probe, tolerance and baseline |
| 2026-10-22 | [Phase 1.3: PCSX2 GS renderer](2026-10-22-pcsx2-gs.md) | PCSX2 2.9.134 GS in-process as a static library (SW, Vulkan), the PS2Recomp seam, the PS2X_GS selector |
| 2026-10-23 | [Gameplay bring-up: past character select](2026-10-23-gameplay.md) | Difficulty state fix; GAME/SINGLE overlays never recompiled; NOW LOADING is VU1-bound; VU1 capture (0031) and microVU offline (~200x, interpreter EFU opcode bug); EFU decode fix (0032); depcheck restored; microVU in the runner (0033): first room (J's Bar) in ~100 s, ~27 vsync/s, next limits GS vsync + IOP |
