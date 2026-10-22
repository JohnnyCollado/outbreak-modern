# docs/ops — operational detail beyond the card

Start with `build.md` (everyday commands, environment, decisions). Edit a file in the same change as whatever
changed it.

- `build.md` — build, run, test commands; environment; paths; decisions still in force
- `workspace.md` — C:\ob layout, game folder (RUN), generated vs hand-edited paths, patch pipeline, vendored copy status
- `pins.md` — toolchain, emulator, game and upstream pins; runner configure flags
- `gotchas.md` — harness and build gotchas (Windows/MSVC, ob.sh, frame dump, PCSX2 debugger quirks)
- `tools.md` — project tools in `tools/` (live, unclear, legacy ARMSX2-mod / device tools)
- `gate.md` — the gate: files, refs.py, display-mode probe result
- `reference-capture.md` — PCSX2 reference settings (UI path, ini key), hotkeys, per-screen capture checklist
- `gs.md` — the GPU GS: PCSX2 GS decision, phase 1.3 stop, where it plugs in
boot.md | Booting the runner | 16
gotchas.md | Harness and build gotchas | 33
pins.md | Pins | 14
tools.md | Project tools (tools/) | 22
workspace.md | Workspace: C:\ob, game data, generated paths | 42
gate.md | The gate (phase 1.2) | 87
reference-capture.md | Reference capture (PCSX2 2.9.134, SLPM-65428) | 62
