# Gate tolerance — FROZEN 2026-10-22 (T5 analysis, T6 self-test; developer approved the freeze)

## Plain-English summary
The gate compares each boot screen our build draws with a PCSX2 screenshot of the same screen. For every pixel it
takes the largest difference in red, green or blue (0 to 255). A pixel counts as wrong when that difference is
more than 8. A screen passes when no more than 0.01% of its pixels are wrong, which is about 23 of the 229,376
pixels in a 512x448 frame. Why 8: the two emulators turn 16-bit colours into 8-bit colours slightly differently
(up to 7 levels), and blending can round by 1 more level; nothing else legitimate was found. Why 0.01%: PCSX2
matches itself exactly on the save-data, ACCESS and CAPCOM screens and our runner matches itself too, while the
smallest deliberate damage we tested (a 48-pixel missing box) is twice the limit. Those three screens decide pass
or fail. The title, main menu and character select screens have movie backgrounds that the runner cannot play
yet, so PCSX2 does not even match itself there; they are printed every run but do not affect the result. Once
frozen, these numbers change only through a plan change the developer approves (rule P9).

Status: draft, awaiting the developer's freeze (T6). d=8, p=0.0001, full frame. GATED (pass/fail, exit code): savedata, access, capcom,
proven (section 5). ADVISORY (printed, not in the exit code; `advisory` in gate/tolerance.json): title, mainmenu,
charselect, UNVERIFIED: backgrounds are Sofdec movies (M5); re-check belongs to the movie generation (section 8).
Line-doubling: runner fault, phase 1.3 (Q2).
Metric (gate.py `metric`): delta = max(|dR|,|dG|,|dB|) per pixel, alpha ignored; share = count(delta > d)/(512*448).
Measured with scratch `.run/1.2/t5_tol.py`, `t5_field.py` (gate.py `metric`, `missing_layer`, `shifted_glyph`);
logs `.run/1.2/logs/t5_tol.log`, `t5_field.log`. 1 px = 4.36e-6 share.

## 1. Noise floor, PCSX2 (ref a vs ref b, C:\ob\ref, manifest gate/ref_manifest.json)
- savedata, access, capcom: max 0, share 0 at every d (byte-identical).
- title: max 90; share d8 0.1425, d16 0.0534, d32 0.0115. Whole frame (bbox full), even/odd rows equal.
- mainmenu: max 114; share d8 0.4366, d16 0.2465, d32 0.0922. Whole frame.
- charselect: max 226; share d8 0.4253, d16 0.2548, d32 0.0952. bbox (0,16)-(512,432).
- Cause (contact sheet a|b|diff, viewed locally, not kept): animated backgrounds. Title fog/water layer, mainmenu
  scrolling eye texture + scanline band, charselect moving background ("GATE" layer differs). UI text identical.
  Not rendering noise: two moments of an animation.

## 2. Noise floor, ours (T3, docs/ops/gate.md "Our-side noise floor")
- run vs run by tick: 11/13 pairs max 0; charselect max 26, share 7.0e-5 at d<=8 (16 px), 3.5e-5 at d16, 0 at d32;
  6x4 box (147,72)-(153,76), uninitialised texels (runner fault, counted as noise until fixed).
- title window sampling floor (n=64): max 12, share>8 0.0026, share>16 0.
- field-phase alternation: savedata/mainmenu/charselect alternate two images 1 line apart (share 0.016/0.029/0.15);
  access/capcom one phase only.

## 3. Legitimate differences (not rendering faults)
- 16->8 expansion. Runner: bit replication `(c<<3)|(c>>2)` (C:\ob\up\ps2xRuntime\src\lib\gs\gs_cpu_backend.cpp:1831-1833,
  read 2026-10-22). PCSX2 2.9.134: zero fill `c<<3` in Expand16To32 (texture) and ReadFrame16 (display readback)
  (GSLocalMemory.h, R1.2-007). Delta = c>>2, max 7 (c=31), uniform, value-dependent, every pixel of a 16-bit source.
- Dithering. PCSX2 SW ignores `dithering_ps2` (GSConfig.Dithering not read by GSRendererSW); dithers only when
  env.DTHE && 16-bit frame target, at draw time (GSDrawScanline `sel.fpsm == 2 && sel.dthe`) (R1.2-007, R1.2-004).
  Runner: none (DTHE stored only, gs_frontend.cpp:1421-1425). Frame target is 32-bit: R1.2-005 (640x448 PSMCT32 init)
  and the refs: share of channel values with low 3 bits != 0 is access 0.90, title 0.69, mainmenu 0.78, charselect
  0.76, capcom 0.12, savedata 0.024; a 16-bit readback (zero fill) gives exactly 0. So no dither and no display
  expansion on these screens: bound 0. (If a 16-bit target appeared: dither +-1 LSB = 8, plus expansion 7 -> 15.)
- Alpha blend `((A-B)*C)>>7` truncation vs rounding: 1 per blend step (R1.2-004), uniform on blended areas.
- pcrtc_antiblur=true: changes only sub-4-px circuit/display offsets when both circuits read the same source
  (GSState CalculateFramebufferOffset/DisplayOffset, R1.2-007). Refs a==b on static screens; any effect is a shift,
  not a colour delta: not a d term. Unquantified; see Q2 (geometry).
- Field/CRTC merge. PCSX2 deinterlace_mode 1 (Off): refs hold 448 distinct lines (line pairs equal: 0/224 title,
  mainmenu; 0 on all screens except access 48/224, flat rows). Runner: `applyFieldPresentation`
  (gs_cpu_backend.cpp:397-410) copies one field's line over its pair (bob): our frames have every even pair equal
  (access 224/224, charselect 187/187, all screens). Delta at every horizontal edge = adjacent-line difference, up to
  255; share estimated (not measured) as the 1-line-shift share / 2: savedata ~0.016, access ~0.05, capcom ~0.012 (ref a vs a shifted 1 line:
  0.0318 / 0.1026 / 0.0248 at d8). Not bounded by any d < 255 and above every damage share: see Q2.

## 4. Damage shares (selftest generators on ref a, d=8)
- savedata: missing layer 0.000209 (6x13 box, 48 px), shifted glyph 0.001212.
- access: missing 0.007276, shifted 0.001931. capcom: missing 0.145700, shifted 0.000593.
- title: missing 0.003435, shifted 0.001495. mainmenu: missing 0.004464, shifted 0.000767.
- charselect: missing 0.314135, shifted 0.001365.
- Damage shares move little between d=8 and d=16 (e.g. savedata shifted 0.001212 both): the choice of d inside
  [8,16] does not change what the selftest detects.

## 5. Proposal
- d = 8: expansion 7 + one blend rounding step 1. Dither excluded by the 32-bit-target evidence (section 3).
- p = 0.0001 (~23 px), static screens (savedata, access, capcom): PCSX2 noise 0, our noise 0 (charselect garbage
  7.0e-5 = 16 px would also fit) < p < smallest damage 0.000209 (savedata missing layer); margin x2 on the damage side.
  Edge/rounding effects across emulators beyond d: none measured (cannot be, without scoring the build; T7).
- Proven on: savedata, access, capcom (Q2 decided: runner fault).
- title, mainmenu, charselect: same d/p, full frame, best frame in window; UNVERIFIED (section 7).

## Open questions (resolved; kept as history)
- Q1 RESOLVED 2026-10-22 by option A of section 8 (movie screens advisory). Original: animated backgrounds: PCSX2 a vs b 0.14-0.44 at d8 vs smallest damage 0.000767-0.001495 (title, mainmenu, charselect). Options: (A) frame-
  locked references: PCSX2 frame advance to a fixed vsync count after the screen's predicate, a/b must be
  identical, our window must cover that count; (B) mask the animated background (hides the missing title
  background, 1.4 scope); (C) mask it from the strict metric and check it with a coarse statistic (metric change,
  GENERATION_PLAN binding). Recommended: A, then C if A's a/b are not identical.
- Q2 DECIDED 2026-10-22 (developer): runner fault, field presentation in 1.3 scope; refs kept; d/p not widened.
  Today's build is expected RED on every screen for this reason alone (bob-doubled lines, ~0.012-0.05 share est.).
  Original question: refs show the full 448-line frame, the runner bob-doubles one field. Options: (A) runner
  fault: keep refs, add field presentation to fix scope (1.3, it touches ACCESS/CAPCOM first); (B) legitimate:
  recapture refs with deinterlace Bob (4/5) so both sides show one field. Recommended: A (the frame buffer holds 448
  distinct lines; the CRT shows both fields; doubling loses half the image).

## 6. Q1 follow-up: UI-region split (developer's choice 2026-10-22), measured; premise fails
Plan: strict d/p on UI rectangles where PCSX2 a == b; background (complement) by a coarse statistic
= max over channels of |mean diff| and |std diff| (ImageStat, masked). Scratch `.run/1.2/t5_blocks.py`,
`t5_regions.py`; logs `t5_blocks.log`, `t5_regions.log`.
- 16x16 block map (a vs b, any delta > 8): mainmenu: every block row differs except y 16-31 (flat bar) and
  scattered blocks; title: PRESS START box and most of the frame differ; charselect: only the large portrait
  area is mostly clean.
- Candidate UI rects, a vs b, pixels over d=8 (p = 0.0001 = 23 px): title press (169,320)-(345,340) 1511 px,
  max 26 (text pulses, T3); title corner (0,0)-(160,48) 141. mainmenu logo (50,112)-(460,232) 34628, max 46;
  menu (185,250)-(325,375) 10239, max 99; copyright (120,400)-(400,416) 146, max 15. charselect header
  (88,20)-(425,56) 5662; strip (0,100)-(512,240) 32542; info (30,272)-(300,392) 16560; portrait
  (288,128)-(400,272) 918; portrait low (336,272)-(512,432) 1936; prompts (40,408)-(512,432) 1403.
  No region is within noise: smallest 141 px > p (23 px) by 6x. Cause: UI is translucent over / interleaved with
  the animated layer (mainmenu scanline band crosses the logo and menu).
- Coarse background statistic, a vs b vs a with black background: title 1.10 vs 38.00; mainmenu 5.01 vs 50.22;
  charselect 10.26 vs 34.93. Separates a missing background (margin >= 3x).
- Coarse statistic cannot catch a missing UI layer: whole-frame stat with one UI rect filled by its median vs
  a-vs-b noise: mainmenu menu 0.87 (noise 3.31), copyright 2.28; charselect header 2.36, prompts 2.60, portrait
  4.56 (noise 5.54); title press 4.88 (noise 0.96, the one detectable case).
- Conclusion: the split gives a working background check but no strict UI region on any of the three screens; a
  missing or misdrawn UI layer stays undetected. Not briefed to a coder. Options in the T5 summary.

## 7. SUPERSEDED by section 8 (plan Changes 2026-10-22: best-frame idea dropped; kept as history)
Decision: full-frame d=8, p=0.0001 as on static screens; capture window long enough to cover the background
animation, scored on the best (closest) frame (gate.py compare already does this: lowest share, then max, then k;
no tool change). Region split (section 6) dropped; its numbers are the evidence for this choice.
- Argument: the reference shows one phase of a looping animation; a correct build whose window covers one full loop
  contains a frame of that phase, so its best frame is as close as PCSX2 is to itself on static screens.
- Risk: (1) the loop must fit in the window and before the screen leaves (title idle -> attract after 960 ticks,
  mainmenu idle -> title after 1800 ticks; R1.2-008); (2) on title the PRESS START pulse (decomp: 128 steps,
  R1.2-008; ours ~85 ticks, T3) and the background must both match in one frame; (3) a static UI fault could hide in
  a best frame only if it fits under p, as on static screens.
- Re-check: when the backgrounds render (developer: phase 1.4), measure our loop and confirm a correct-looking build's
  best frame is <= p. Fallback if it fails then: background-only coarse statistic (section 6 numbers: a-vs-b
  title 1.10, mainmenu 5.01, charselect 10.26 vs black background 38.00 / 50.22 / 34.93; known blind spot: missing
  UI layer, e.g. mainmenu menu 0.87 < 3.31).
- Finding against the re-check timing (T5 resume 2): the backgrounds are CRI Sofdec movies, not procedural.
  mainmenu: SUBMAIN movie func_0034A970 (decomp title.c:709-713); charselect: func_0034A3D0 (title.c:1276),
  restart timer 108000 ticks (R1.2-008); SUBMAIN names PS2title1/2_eng.sfd, PS2select.sfd. Movies are M5
  ("not started", PROJECT_CONTEXT.md), so they will not render in 1.4 without movie playback in scope.
- Loop length: not measurable on our side (t3p frames: no background change over 120 frames; title/mainmenu mean RGB
  ~1-2.6), not in the decomp (movie only), not resolvable to a named stream: the ISO's NETBIO*.DAT holds ~200 Sofdec
  streams (`.run/1.2/t5_sfd2.py`; durations 3.55 s .. 376.9 s), no sector table found in SUBMAIN/ELF
  (`t5_map.py`). Lower bound for any movie loop: 3.55 s = 213 ticks. Windows left unchanged (title 64, mainmenu 32,
  charselect 6) pending the developer's answer.
- Line-doubling (Q2): runner fault, phase 1.3; today's build expected RED on every screen until then.

## 8. Decision 2026-10-22 (developer, option A): movie screens advisory in generation 1
- Gated: savedata, access, capcom: d=8, p=0.0001; noise 0 (both sides) < p < smallest damage 0.000209.
- Advisory: title, mainmenu, charselect: same d/p, best frame in window, printed every run, excluded from the exit
  code; UI fixes on them judged by side-by-side review (1.4/1.5). Re-check (window covers the movie loop, a correct
  build's best frame <= p; else background-only fallback, section 6 numbers) belongs to the generation that adds
  Sofdec playback, not 1.4.
- Windows stay title 64, mainmenu 32, charselect 6 (T3): they cover what our frames can show today (title pulse,
  mainmenu single-phase span to alternation, charselect two field periods inside its 49-tick stable span); the movie
  loop is unknown (>= 213 ticks) and cannot be sized until movies render; title idle limit 960 ticks and mainmenu
  1800 bound any future window (R1.2-008).

## 9. Self-test on the real references (T6, 2026-10-22)
`bash tools/ob.sh gate --selftest` with the draft gate/tolerance.json (d 8, p 0.0001, advisory title/mainmenu/
charselect); refs a/b sha256 and size verified against gate/ref_manifest.json (12/12). Exit 0, `selftest 9/9 ok`;
two runs, output identical except wall time (logs `.run/1.2/logs/t6-selftest.log`, `t6-selftest2.log`). Damaged
copies of ref a generated in `.run/1.2/selftest-<pid>/` and deleted (game-derived, never committed).
- Gated, counted (9): savedata b-vs-a max 0 share 0 GREEN; missing layer 0.000209 RED; shifted glyph 0.001212 RED.
  access b-vs-a 0 GREEN; missing 0.007276 RED; shifted 0.001931 RED. capcom b-vs-a 0 GREEN; missing 0.145700 RED;
  shifted 0.000593 RED.
- Advisory, printed, not counted (9): title b-vs-a max 90 share 0.142526 RED; missing 0.003435 RED; shifted 0.001495
  RED. mainmenu b-vs-a 114 / 0.436646 RED; missing 0.004464 RED; shifted 0.000767 RED. charselect b-vs-a 226 /
  0.425297 RED; missing 0.314135 RED; shifted 0.001365 RED. (b-vs-a RED = the movie backgrounds, section 1.)
- Margins on the gated screens: noise 0 vs p; smallest damage 0.000209 = 2.09 x p (savedata missing layer, 48 px).
- Plan done-when wording "all six b-vs-a green, all twelve damaged red" applies to the 3 gated screens under
  option A (6 cases/3 b-vs-a): the advisory screens' b-vs-a cannot be green while their backgrounds are movies.
- Tool (T6, commit 3f75886): `gate.py compare` and the full `ob.sh gate` refuse `--d/--p` (exit 2); d/p come from
  gate/tolerance.json only; `--d/--p` stay for selftest. Selftest refuses refs whose sha256/size != manifest.

## 10. Freeze
- Values: d = 8, p = 0.0001, advisory = [title, mainmenu, charselect]; metric and evidence as in gate/tolerance.json.
- Freeze commit: 20233c3 (2026-10-22, developer: "Freeze the tolerance as drafted").
- After the freeze, d, p, the metric and the advisory list change only through a developer-approved plan change
  (rule P9); a red gate is fixed in the runner, never by moving the tolerance.
