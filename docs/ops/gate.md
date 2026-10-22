# The gate (phase 1.2)

Source: phase-ends/current/PHASE_PLAN.md Context/Interfaces; GENERATION_PLAN.md `## The gate`.
Compares the runner's six scripted captures against PCSX2 reference screenshots, per screen: max per-channel delta
and share of pixels with delta > d; green = share <= p. d and p are frozen in `gate/tolerance.json` (rule P9).

## Files
- References: `C:\ob\ref\<screen>-<a|b>.png` (+ `sheet.png`, `session.json`); never committed.
- `gate/ref_manifest.json`: PCSX2 version, effective settings, `window` ({screen: captures} from `gate/boot.script`),
  `session` (capture session time), sha256 per reference.
- `tools/gate/refs.py`: `check-settings` (PCSX2.ini vs the reference settings), `begin` (session marker),
  `intake [--dry-run]` (snaps newer than the marker -> screens in boot order, 512x448 asserted, copy + manifest + sheet).
- Capture procedure and settings: `docs/ops/reference-capture.md`.
- Screens (boot order): savedata, access, capcom, title, mainmenu, charselect; runner side `C:\ob\frames-<T>\<screen>-<k>.png`.

## gate.py and ob.sh gate
- `bash tools/ob.sh gate [--d N --p F]`: stamp vs `C:\ob\r\gate.stamp`; differ or runner exe missing -> recompile, then
  write the pre-recompile stamp (`gate: recompiled in <s> s` / `gate: not recompiled`); boot `gate/boot.script --tag gate`;
  `gate.py selftest`; `gate.py compare` on `C:\ob\frames-gate` + `C:\ob\logs\boot-gate.log`. `--selftest`: selftest only.
- Exit: 0 green, 1 red, 2 refusal (no references, no tolerance, ref sha256/size != manifest, captures per screen !=
  manifest `window`, frame without tick), 3 boot failed, 4 recompile failed, 5 selftest red.
- `gate.py compare --frames D --log F [--refs C:/ob/ref] [--manifest gate/ref_manifest.json] [--d N --p F]`; d/p default
  from `gate/tolerance.json`. RGB only, delta = max channel |diff|; best frame = lowest share, max, k.
  Per screen: `<screen> best k=<k> tick=<t> max=<int> share=<float> p=<float> GREEN|RED`; last line
  `gate: <n>/6 green, exit <code>, wall <s> s` (refusals: `gate: <reason>, exit 2, wall <s> s`).
- `gate.py selftest`: per screen ref b vs a GREEN; ref a with "missing layer" (top area x colour-distance component's
  bbox filled with its ring median) and "shifted glyph" (max FIND_EDGES-energy 24x24 window +2 px x) RED. Temp
  `.run/1.2/selftest-<pid>/`, deleted.
- `gate.py stamp`: sha256 over `C:/ob/up` HEAD + `diff HEAD` + untracked files; `C:/ob/src` files (not
  `ps2xRuntime/src/runner/`, `*_recompiled_*.h`, build/out dirs); `$OB_T` ps2_analyzer.exe, ps2_recomp.exe;
  `$OB_WORK` SLPM_654.28, main-functions.csv; `$OB_OVL/demo.toml`; `$OB_RUN/PROG/{DEMO,SUBMAIN,NETDNAS}.BIN`;
  `$OB_SPLAT` asm dirs of the 3 overlays; repo `tools/mwo3_to_elf.py`, `tools/splat_functions.py`; text of
  `cmd_recompile` in `tools/ob.sh`.
- `gate.py synth DIR`: synthetic refs/manifest, frames-identical|erased|shifted, boot-*.log (tests only, `.run/1.2/`).

## Display-mode probe (T1, 2026-10-22)
GS privileged registers (PMODE, SMODE2, DISPFB1/2, DISPLAY1/2) are not readable without a patch: the script's
read lambda (`C:\ob\up\ps2xRuntime\src\lib\Kernel\EeScheduler.cpp:1897-1906`) reads RDRAM only (`addr & 0x01FFFFFF`),
so `watch 0x12000020` silently reads RDRAM 0x20. No env switch traces them. FBW and SMODE2 (field/frame) unknown.
