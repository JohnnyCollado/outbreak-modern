# Gate baseline, phase 1.2 (T7, 2026-10-22)

Today's build scored by the frozen gate. Expected red; it is red. This is the starting point for phase 1.3.

## Build and run
- Runner source: `C:\ob\up` HEAD `ab85c05` (upstream base + `upstream/ps2recomp/0001`-`0020`, 22 commits in log).
- Runner exe: `C:\ob\r\ps2xRuntime\ps2EntryRunner.exe`; gate stamp `86bf37eab05780cd598b0a332edaae52b4e47ae3bd53115fab8df0a03be331a4`
  (unchanged: `gate: not recompiled`).
- Repo HEAD at run: `7fe905b`. Tolerance: `gate/tolerance.json` as frozen in `20233c3` (`git diff 20233c3 -- gate/tolerance.json`
  empty): d 8, p 0.0001, gated savedata/access/capcom, advisory title/mainmenu/charselect.
- Command: `bash tools/ob.sh gate` (no --d/--p). Boot tag `gate`, frames `C:\ob\frames-gate`, log `C:\ob\logs\boot-gate.log`
  (`[run] window hidden=1 flags=0x84`). Boot exit 0, selftest 9/9 ok.
- Run logs: `.run/1.2/logs/t7-gate1.log`, `.run/1.2/logs/t7-gate2.log`.

## Result (run 1; run 2 identical shares, ticks within 2-6)
```
savedata best k=0 tick=177 max=240 share=0.043597 p=0.000100 RED
access best k=0 tick=870 max=255 share=0.248753 p=0.000100 RED
capcom best k=0 tick=1171 max=224 share=0.082450 p=0.000100 RED
title best k=9 tick=1594 max=240 share=0.999865 p=0.000100 RED ADVISORY
mainmenu best k=0 tick=1912 max=255 share=0.998910 p=0.000100 RED ADVISORY
charselect best k=0 tick=2773 max=255 share=0.891462 p=0.000100 RED ADVISORY
gate: 0/3 gated green, 3 advisory, exit 1, wall 54.5 s
```
Run 2: same seven lines but ticks savedata 175, access 868, capcom 1169, title 1592, mainmenu 1906, charselect 2773; wall 54.7 s, exit 1.
No screen is green, so no green needs an M3 frame check.

## What is visibly wrong (ref `C:\ob\ref\<screen>-a.png` vs our best frame, by eye)
- savedata: layout, position and colours (white text, green 1/495, red NO) match; every glyph is dim grey and broken into
  thin strokes (alternate lines missing/doubled: the field bob-doubling of `applyFieldPresentation`).
- access: layout matches; logo and kana/kanji fills are horizontally striped and washed out (navy/black become grey-brown
  stripes), glyph edges ragged. Field presentation plus a colour/blend fault on the filled areas.
- capcom: colours right (yellow on blue outline); whole logo sits ~12-16 px right of the ref (diff is a 1-sided outline,
  i.e. a geometry offset, not colour); stray 1-px vertical line at the left edge (x~40, mid-height).
- title (advisory): movie background black (M5); "PRESS START BUTTON" present but clipped at the right edge ("BUTTON"
  cut) and warm yellow instead of the ref's lavender.
- mainmenu (advisory): movie background and the BIOHAZARD OUTBREAK logo absent; menu items present in the right place but
  each clipped on the right (same right-edge clip as title); a stray blue bar left of COLLECTION; copyright line clipped.
- charselect (advisory): background, header frame, CHARACTER SELECT title, PLAYER marker, side arrows and the bottom
  button legend absent; garbage block where the PLAYER marker should be; the eight roster figures present but dark, the
  selected one on a solid blue rectangle; the large portrait dark, purple-noisy; info panel text dim and broken.

## For phase 1.3
- Shared across all screens: field bob-doubling (decided runner fault, 1.3).
- Text/sprite right-edge clipping (title, mainmenu) and the capcom offset suggest a sprite/scissor or UV geometry fault.
- Missing UI layers on mainmenu/charselect (frames, header, legend; the mainmenu logo possibly part of the movie,
  unverified) look like separate draws failing, not only the movie.
- The gated screens' shares (0.04-0.25) exceed what line-doubling alone explains (T5): more than one fault.
