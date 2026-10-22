# Gate tolerance — DRAFT (T5, 2026-10-22; not frozen, freeze is T6 with the developer)

Status: draft. d proposed; p proposed for static screens only. Q2 decided (runner fault, 1.3); Q1 region split
measured and fails its premise (section 6): the freeze stays blocked on Q1.
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
- Holds on: savedata, access, capcom, provided Q2 is answered "runner fault".
- No single p works on: title, mainmenu, charselect (Q1), and on no screen if field duplication is legitimate (Q2).

## Open questions (for T6 / the developer; no p is widened to fit)
- Q1 animated backgrounds: PCSX2 a vs b 0.14-0.44 at d8 vs smallest damage 0.000767-0.001495 (title, mainmenu, charselect). Options: (A) frame-
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
