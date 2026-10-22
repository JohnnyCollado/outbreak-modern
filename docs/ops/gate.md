# The gate (phase 1.2)

Source: phase 1.2 plan and research (`docs/history/phase-1.2/`).
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
- `bash tools/ob.sh gate [--selftest [--d N --p F]] [--tolerance PATH]` (`--tolerance` to both; `--d/--p` selftest only,
  full gate with them -> exit 2, T6): stamp vs `C:\ob\r\gate.stamp`; differ or runner exe missing -> recompile, then
  write the pre-recompile stamp (`gate: recompiled in <s> s` / `gate: not recompiled`); boot `gate/boot.script --tag gate`;
  `gate.py selftest`; `gate.py compare` on `C:\ob\frames-gate` + `C:\ob\logs\boot-gate.log`. `--selftest`: selftest only.
- Exit: 0 green, 1 red, 2 refusal (no references, no tolerance, unknown screen in `advisory`, ref sha256/size !=
  manifest, captures per screen != manifest `window`, frame without tick), 3 boot failed, 4 recompile failed,
  5 selftest red.
- Tolerance file (`--tolerance PATH`, default `gate/tolerance.json`): `{d, p, metric, evidence, frozen, advisory?}`;
  `advisory: [screen, ...]` optional (absent = []), names must be in the 6 screens. `--d/--p` override d/p in selftest only
  (compare refuses them, exit 2: the frozen file is the gate, rule P9); advisory comes from the file only.
- `gate.py compare --frames D --log F [--refs C:/ob/ref] [--manifest gate/ref_manifest.json] [--tolerance P]`.
  RGB only, delta = max channel |diff|; best frame = lowest share, max, k.
  Per screen: `<screen> best k=<k> tick=<t> max=<int> share=<float> p=<float> GREEN|RED[ ADVISORY]`; last line
  `gate: <g>/<n> gated green, <m> advisory, exit <code>, wall <s> s` (n = 6 - m; refusals:
  `gate: <reason>, exit 2, wall <s> s`). Exit 0 iff every non-advisory screen GREEN; advisory screens are printed only.
- `gate.py selftest [--tolerance P] [--d N --p F]`: refs a/b sha256/size vs manifest first (exit 2); per screen ref b vs a GREEN; ref a with "missing layer" (top area x
  colour-distance component's bbox filled with its ring median) and "shifted glyph" (max FIND_EDGES-energy 24x24
  window +2 px x) RED. Advisory screens' cases print with suffix ` advisory` and are not counted (real refs:
  title/mainmenu/charselect b-vs-a are RED by design). Temp `.run/1.2/selftest-<pid>/`, deleted.
- `gate.py stamp`: sha256 over `C:/ob/up` HEAD + `diff HEAD` + untracked files; `C:/ob/src` files (not
  `ps2xRuntime/src/runner/`, `*_recompiled_*.h`, build/out dirs); `$OB_T` ps2_analyzer.exe, ps2_recomp.exe;
  `$OB_WORK` SLPM_654.28, main-functions.csv; `$OB_OVL/demo.toml`; `$OB_RUN/PROG/{DEMO,SUBMAIN,NETDNAS}.BIN`;
  `$OB_SPLAT` asm dirs of the 3 overlays; repo `tools/mwo3_to_elf.py`, `tools/splat_functions.py`; text of
  `cmd_recompile` in `tools/ob.sh`; `C:\ob\pcsx2` (`OB_PCSX2`) HEAD + `diff HEAD`; tree `native/pcsx2_gs/` (T8).
- `PS2X_GS=pcsx2-sw|pcsx2-vk` passes through to the boot (default cpu; docs/ops/gs.md).
- `gate.py synth DIR`: synthetic refs/manifest, frames-identical|erased|shifted|advisory (title erased, rest
  identical), boot-*.log, `tol-advisory.json` {d:8,p:0.001,advisory:["title"]}, `tol-none.json` {d:8,p:0.001}
  (tests only, `.run/1.2/`).
- Wall time (T2, 2026-10-22): stale stamp 346 s (recompile 297 s, runner build 273 s); fresh stamp 47.8 s (boot + gate).
  Since `tools/ob/sync_changed.py` (2026-10-22, partner): a stale stamp with unchanged generated code is 70 s
  (recompile 16 s, runner build 3 s); only changed generated files and changed runtime/adapter files rebuild.

## Display-mode probe (T1, 2026-10-22)
GS privileged registers (PMODE, SMODE2, DISPFB1/2, DISPLAY1/2) are not readable without a patch: the script's
read lambda (`C:\ob\up\ps2xRuntime\src\lib\Kernel\EeScheduler.cpp:1897-1906`) reads RDRAM only (`addr & 0x01FFFFFF`),
so `watch 0x12000020` silently reads RDRAM 0x20. No env switch traces them. FBW and SMODE2 (field/frame) unknown.

## Our-side noise floor and capture windows (T3, 2026-10-22)
- Tool: scratch `.run/1.2/t3_noise.py` (gate.py `metric`, `read_ticks`); boots hidden, same build, `gate/boot.script` n=3.
- Run vs run (tags t3a, t3b), paired by tick (13 of 18 ticks in both; 5 shifted +-1 tick): 11 pairs max=0 share=0.
  charselect (2 pairs): max=26, share 0.000070/0.000052 at d=0..8, 0.000026/0.000035 at d=16, 0 at d=32; all of it in
  a 6x4 box (147,72)-(153,76) holding garbage texels that change frame to frame (runner self-noise, uninit texture).
- Probe (tag t3p, `.run/1.2/probe120.script` = boot.script with `capture <screen> 120`, timeout 900; exit 0, wall 62 s):
  - field alternation (whole frame or text drawn 1 line apart on alternate fields): savedata A,B,B every 4 ticks
    (max 183, share>8 0.016); mainmenu same, but only after a single-phase span of up to 60 ticks at 15 fps (max 243,
    share 0.029); charselect A,B,B (max 253, share 0.15). access/capcom latch every 2 ticks: one field phase only
    (access phase change at the window edge: max 254, share 0.16).
  - title: only PRESS START box (169,320)-(345,340) changes: field flicker + pulse, mean luma 1..67, period ~85 ticks
    (troughs t=1789, 1874). Window sampling floor (probe frame j>=N vs best of frames 0..N-1): N=64 max 12, share>8
    worst 0.0026, share>16 0; same max at N=32/48.
  - access static 117 ticks from window start, then fade; capcom static 77 ticks, then fade; charselect: after ~49
    ticks of window (~140 after state 9) portrait vanishes and a white box shows (one-time state change, kept out).
- Windows set in `gate/boot.script`: savedata 6, access 6, capcom 6, title 64, mainmenu 32, charselect 6 (was 3 each).
  6 = 2 alternation periods inside each stable span; title 64 = one full pulse; mainmenu 32 reaches the alternation.
- Verify (tag t3w, n as above): exit 0, `window hidden=1`, 120 frames, wall 50 s (420 s script timeout unchanged);
  every screen inside its stable span; savedata, mainmenu, charselect hold both field phases; title luma 1..68.
- For T5: our static access/capcom frames show one field phase only; a PCSX2 shot of the other phase differs by a
  1-line shift. Title text clipped at x=345 ("BUTTO") and black title/menu backgrounds: compare with the refs.

## Tolerance (T5 draft, 2026-10-22; frozen in T6) — `gate/TOLERANCE.md`
- d=8, p=0.0001 (~23 px), full frame, all six screens; no region split (measured, dropped: TOLERANCE.md section 6).
- savedata/access/capcom proven: PCSX2 a==b, ours run==run, smallest damage 0.000209.
- title/mainmenu/charselect: best frame in the capture window (existing compare logic); UNVERIFIED: backgrounds are
  Sofdec movies (M5), loop length unknown (>= 213 ticks); fallback = coarse background statistic (section 7).
- Runner line-doubling (applyFieldPresentation) is a runner fault for 1.3: every screen RED until fixed.
- Evidence kept: the `.run/1.2/...` scripts and logs cited in TOLERANCE.md, this file and the 1.2 summaries are copied
  to `gate/evidence/1.2/` (same file names; `.run/1.2/` was deleted at the phase close): t3_noise.py, t3-probe.log,
  probe120.script, t5_tol.py/.log, t5_field.py/.log, t5_blocks.py/.log, t5_sfd2.py/.log, t6-selftest.log,
  t7-gate1.log, t7-gate2.log. Scratch not cited was dropped.

## Current baseline (phase 1.3, 2026-10-22) — `gate/baseline-1.3.md`
- `C:\ob\up` 00e03cd (patches 0001-0029). pcsx2-vk: savedata 0.003353, access 0.000000 GREEN, capcom 0.001251
  (1/3 gated green); cpu: 0.042668, 0.050319, 0.018507 (0/3). Frame time: pcsx2-vk busy median ~4.85 ms, cpu ~10.2 ms.
- Compare new runs against this file; the 1.2 baseline below is history.

## Baseline (T7, 2026-10-22) — `gate/baseline-1.2.md`
- `C:\ob\up` ab85c05 (patches 0001-0020), frozen tolerance 20233c3: exit 1, 0/3 gated green, wall ~54.5 s, hidden=1.
- Shares: savedata 0.043597, access 0.248753, capcom 0.082450 RED; title 0.999865, mainmenu 0.998910, charselect 0.891462
  RED ADVISORY. Two runs, identical shares (ticks shift 2-6). Per-screen visible faults in the baseline file.
