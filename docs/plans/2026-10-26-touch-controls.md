# Android touch controls: on-screen overlay (design)

- **Date:** 2026-10-26
- **Status:** design final (developer, 2026-10-26: "This looks great. Finalize the design"), nothing built. One last
  update follows after research into the current state of the user controls (`input.ini`, the settings menu's
  Controls tab, the Android input path); that research waits for the developer's go.
- **Built 2026-10-26** on branch `android-touch-controls` for the Classic camera (all of T1-T14 except the Modern
  layout, T7 and T9, which waits for the Modern camera); awaiting the developer's device test. How it is wired:
  `docs/ops/android.md` "Touch controls"; report `reports/2026-10-26-touch-controls.md`.
- **Revised 2026-10-26:** the developer found that the left stick gives directional movement in the Classic camera
  too, and that aiming returns to tank-style aiming. Changed: T5, T6, 4.1, 4.3, 4.5, section 5; added T13, T14.
- **Mockups:** shown in the Claude Code session of 2026-10-26 (v1 full overlay, v2 tank pad + minimal set, v3 Modern
  look stick, v4 Classic aim pad = final).
- **Belongs to:** the settings menu plan (`docs/plans/2026-10-26-settings-menu.md`), Controls tab, Android only.

## 1. What it is

On-screen controls drawn over the game on Android phones without a controller: a movement pad on the left, a few
buttons on the right, and a menu button. Players can move, resize, hide and fade every control, and turn the overlay
off. It feeds the same PS2 pad the physical controllers feed (`input.ini` buttons and sticks), so the game sees an
ordinary DualShock 2.

## 2. Every input the game needs

The Japanese disc (SLPM-65428) swaps ○ and ✕ against the US/EU manuals; the device test wins over the listings.

| Input | In game | Source |
|---|---|---|
| D-pad | Tank movement in the Classic camera: ↑ forward, ↓ back, ←/→ turn on the spot; menu cursor | psxdatacenter (SLPM/SLES/SLUS pages, read 2026-10-26); `legacy/phase1-feasibility-report.md:493` |
| Left stick | **Directional movement, Classic camera included**: she heads where the stick points on screen (against the camera), not tank style (see 4.1) | developer, 2026-10-26 (found while working on the controller stick in another session); `legacy/phase1-feasibility-report.md:492`, `:496`; `legacy/mod/game.h:30` |
| ○ | Action, pick up, confirm; fire while aiming | `legacy/phase1-feasibility-report.md:493` (device; fire inferred) |
| ✕ | Run; back out of menus; character special (Kevin's kick) while aiming | `legacy/phase1-feasibility-report.md:493-494` |
| ↓ + run | Quick turn | psxdatacenter |
| R1 | Aim with lock-on | report `:493`, psxdatacenter ("attack stance") |
| R2 | Aim forward, no target | report `:493`, psxdatacenter ("forward attack stance") |
| L1 | Reload, shift target | psxdatacenter, EU listing only |
| L2 | Change call set | psxdatacenter, EU listing only; unverified on our disc |
| □ | Ad-lib, talk | psxdatacenter |
| △ | Map | psxdatacenter |
| Right stick (+ R3) | Calls to partners | psxdatacenter; `legacy/mod/mod.c:316` |
| Start / Select | Status screen / options | psxdatacenter |
| While aiming, ↑ / ↓ | Gun up / down (three levels: level, about +45°, about -45°); she does not walk | `legacy/phase1-feasibility-report.md:487` (device scan) |
| Port | Settings menu: Android Back button (P2, P4) | settings menu plan `:50-52` |
| Modern camera | L2 aim, R2 fire, R1 run, ✕ kick / back, right stick camera, L1 + right stick calls | `legacy/README.md:22-32` |

## 3. Decisions (final, developer 2026-10-26)

| # | Decision |
|---|---|
| T1 | Setting **Show touch controls: Auto / On / Off**, default Auto. Auto hides the overlay on any controller input and shows it again on the next screen touch; Off never shows it (controller players, handhelds such as the Odin). |
| T2 | Two button sets: **Minimal** (default) and **Full** (every PS2 button, for players who want them all). |
| T3 | Layouts are per camera mode (Classic, Modern) and per button set, matching P6. |
| T4 | Controls change with context, driven by the overlay's own state (no game memory read): while aiming, extra buttons appear and the left control aims instead of moving (T8, T9). |
| T5 | (Changed 2026-10-26, after the developer confirmed the game's directional stick in the Classic camera.) Classic movement offers both of the game's own schemes as equals: **Directional** (drives the left stick: push where she should go on screen) and **Tank** (drives the D-pad). Default: see T13. |
| T6 | Tank = an 8-way D-pad with drawn, tunable zones (4.1); outer ring = run; quick flick from the centre into the back zone = quick turn (↓ + ✕). Both switchable. Directional = an analog stick with the same run ring. Its quick turn is a **double tap on the stick** (sends D-pad ↓ + ✕), not a flick: on a direction stick "down" means "walk toward the camera", so a flick would fire by accident (the legacy mod met the same clash, `legacy/phase1-feasibility-report.md:500`). |
| T13 | Default Classic movement on touch: **Directional** (approved, developer 2026-10-26). Touch players without RE experience expect "push where you want to go"; Tank stays one tap away for veterans. |
| T14 | Full set, Classic: a small D-pad (tank) beside the directional stick, so players can mix the two as RE players do on a pad (D-pad for exact lines, stick for dodges). Minimal set: one control only, the chosen scheme. |
| T7 | Modern movement: an analog stick, camera-relative; run at the outer ring (switchable). |
| T8 | Classic aiming, from either movement scheme (the game returns to tank-style aiming, developer 2026-10-26): the pad becomes the **aim pad** (↑ gun up, ↓ gun down, ←/→ turn; no walk, no run ring, no flick). R1 locks on; L1 shifts target / reloads. |
| T9 | Modern aiming: the stick becomes a **look stick** (turns the camera, which the gun follows); dragging the right half of the screen also aims, 1:1 with the finger. Option "While aiming, stick: Looks / Does nothing". |
| T10 | Talk button merges □, the calls and L2: tap = ad-lib (□), hold and drag = call wheel (right stick), swipe the wheel = next call set (L2, pending its check). |
| T11 | Menu button merges Start, △, Select and the port menu into a fan: Status, Map, Options, Settings. |
| T12 | Customizing: global size and opacity; Edit layout (drag, per-control size, hide); Reset per layout; vibrate on press. |

## 4. Details

### 4.1 Movement

**Tank pad** (Classic option, not aiming; drives the D-pad). Angles from straight up on the pad, both sides mirrored:

| Zone | Default | Game sees |
|---|---|---|
| Straight ahead | 0-18° (slider 8-40°) | ↑ |
| Steer while moving | to 18 + 47° (slider 15-70°) | ↑ + ←/→ (running arcs) |
| Turn on the spot | to 135° | ←/→ |
| Back and turn | 135-158° | ↓ + ←/→ |
| Back | 158-180° | ↓ |
| Centre | under 20% of the radius | nothing |
| Run ring | over 68% of the radius, any zone with ↑ | adds ✕ |
| Flick | within 200 ms, from under 35% to over 60%, within ~31° of straight back | ↓ + ✕ quick turn |

Why: fast tank movement means never stopping to turn: hold forward + run and add a little turn to arc round corners,
and quick turn instead of a slow 180 (speedrun.com Resident Evil HD forum thread `remake_hd/forums/pid4s`; Steam
discussion `app/304240/discussions/0/600765159912278181`; the community advice is for RE HD and assumed to carry
over). So the steer-while-moving wedges are wide. The game reads the D-pad as digital bits only (report `:493`), so the
pad loses nothing against a real D-pad.

**Directional stick** (Classic default, T13; Modern always). Drives the left stick, which the game itself treats
as directional movement in the Classic camera too (developer, 2026-10-26). The game compares the stick's direction
(against the camera, `0x306338`/`0x306344`) with her heading: straight walks, a bit off walks and turns, far off turns
on the spot first (`func_006307E0`, report `:492`, `:496`). The sector sizes are not measured yet (section 5).

- Run ring over 68% of the radius adds ✕ (switchable), as on the tank pad.
- Double tap = quick turn (D-pad ↓ + ✕). No flick (see T6).
- No zones are drawn: a plain ring with a "screen up" mark, because the stick's meaning follows the camera, not her.
- Camera cuts: a fixed-camera cut changes what "up" means. Players hold the stick through a cut (Steam thread
  above); whether the game itself keeps her heading until the stick moves is item 3 in section 5. If it does not, a
  touch-side fix is possible only with the game's camera angle, so it waits for that check.

**Both at once** (Full set, T14): the directional stick plus a small tank D-pad. Each sends its own PS2 input (left
stick, D-pad), exactly as on a real pad, so the game decides; no overlay logic merges them.

### 4.2 Aiming

**Classic aim pad:** up ±35° = ↑ (gun up), down ±35° = ↓ (gun down), the sides = ←/→ (turn). No diagonals, so a
tilt cannot turn her by accident. Used with both movement schemes: aiming puts the game back in tank-style aiming even
when she was moved with the directional stick (developer, 2026-10-26), so Directional and Tank share this aim pad. Run ring and flick are off. Buttons while aiming (Minimal): ○ Fire, L1 Shift/reload,
✕ Special, R1 lit.

**Modern look stick:** rate control, the push sets turn speed. Look speed 30-300°/s (default 120); fine control near
the centre (speed rises with the square of the push, default on); invert up/down (default off); dead zone 8%. Sends
the right stick: in the Modern camera the gun follows the camera (`legacy/mod/mod.c:299-306`), and her walking runs
only while standing or walking (report `:504`), so the stick is free while aiming. Buttons while aiming (Minimal): R2
Fire, L1 Reload, ✕ Kick, L2 lit.

### 4.3 Button sets

| Context | Classic, Minimal | Modern, Minimal |
|---|---|---|
| Walking | directional stick or tank pad (T5), ○ Action, R1 Aim, ✕ Run (small), Talk, Menu | stick, ○ Interact, L2 Aim, R1 Run (small), Talk, Menu |
| Aiming | aim pad, ○ Fire, R1 (lit), ✕ Special, L1 Shift/reload, Menu | look stick, R2 Fire, L2 (lit), ✕ Kick, L1 Reload, Menu |
| Menus | through the Menu fan; menu-only layout (D-pad, ○, ✕) later, see section 6 | same |

Full set: every PS2 button as on the pad (Classic, with the directional stick and a small tank D-pad side by side,
T14) or the Modern action names, plus Call (wheel) and Menu.

Aim is a toggle on touch (tap to aim, tap again to lower), so one thumb is free for Fire.

### 4.4 Customizing

- Size (all): 60-160%, default 100%. Opacity: 10-100%, default 60%.
- Edit layout (opens over the paused game): drag any control; tap one to give it its own size (60-180%) or hide it;
  Reset restores the current layout's defaults. Positions are kept as fractions of the screen, so they survive a
  resolution change.
- Vibrate on press: on by default.

### 4.5 Settings menu (Controls tab, Android)

Rows under "Touch controls": Show touch controls (Auto / On / Off), Buttons (Minimal / Full), Classic movement
(Directional / Tank; Tank shows the two zone sliders), Run at stick edge, Quick turn gesture (flick back on the tank
pad, double tap on the directional stick; on / off), Modern: stick looks while aiming
(and look speed, fine control, invert), Size, Opacity, Vibrate on press, Edit layout.

Proposed `outbreak.ini` `[ui]` keys (names to be settled in the last update): `touch = auto|on|off`,
`touch_buttons = minimal|full`, `touch_move = directional|tank`, `touch_forward_zone`, `touch_steer_zone`,
`touch_run_edge`, `touch_quick_turn`, `touch_aim_look`, `touch_look_speed`, `touch_look_curve`, `touch_invert_y`,
`touch_scale`, `touch_opacity`, `touch_vibrate`; per-control positions, sizes and hidden flags in a separate
`touch_layout.ini` beside it.

## 5. Not verified yet

To check on the game before building (a short scripted run, no code change):

1. How wide the directional stick's "walk and turn" range is in Classic (the mockup's sector sizes are guesses).
   The developer's other session on the controller stick may already answer this; take its findings first.
2. Whether D-pad diagonals arc while running as assumed.
3. What a camera cut does to the directional stick: does she keep her heading until the stick moves?
4. That ←/→ turn her on the spot while aiming, that R1 turns her to the target, and how L1 picks reload or shift.
   (Settled: aiming returns to tank-style aiming from either movement scheme, developer 2026-10-26.)
5. What L2 does on SLPM-65428, and whether calls need R3.
6. Whether the game's own options have a control-type setting that moves buttons.

## 6. Later, not in this design

- Aim assist on touch for Modern free aim (new behaviour, not a control change).
- A one-time hint card the first time the overlay shows (the merged and gesture controls are not obvious).
- A menu-only layout chosen from the game's own "menu open" state (needs a game memory address not yet found).

## 8. Menu screens: a D-pad layout (research 2026-10-26; T15-T19 approved and built the same day)

Request (developer, 2026-10-26): on the status and menu screens, show a D-pad so players navigate them as usual.

Findings:

1. **The stick already moves menu cursors.** The game turns the movement stick into direction bits `0xF0000` at 16
   angles past magnitude 0x25 (`main/padread.c` `func_001A5420`, `func_001A5A90`), and the menus' control block folds
   them onto the D-pad bits (`main/ctrl.c` `func_001AFE90`: `cur |= (cur & 0xF0000) >> 16`, auto-repeat 12 then 3
   frames on `0xF000F`). The decomp names that stick "right"; the device test found the movement stick in those bytes
   (`legacy/phase1-feasibility-report.md:491`). Decomp: GalaxyNostalgia/Outbreak-decomp (CC0), first drafts
   (`NON_MATCHING`). So the D-pad layout is about showing the right controls, not about input that fails today.
2. **One byte says "a menu screen is open": GameState `0x48BF60 + 0x1E` (= `0x48BF7E`).** Measured on Windows
   (hidden runs `menu1`, `menu3`, live pad + `peek`, `PS2X_VU1=mvu` for the second): in J's Bar it is 0 in play, in the
   opening movie (game state 14) and its in-engine scene, and after examining with ○; 1 on the status screen (Start:
   ITEM / SPECIAL / FILE), the map (△) and the pause menu (Select); back to 0 when each closes (✕ for status and map,
   Select again for pause). Every open and close in `menu3` matched. The decomp leaves it unnamed (`unk1E`).
3. Also seen: `+0xC4` = 1 only with the pause menu (the decomp: "the offline overlay's per-frame work is paused");
   game state stays 2 behind every menu (the game runs on behind status and map).
4. The map's own help: left stick or D-pad moves the map, right stick zooms, L1/R1 change floor, ✕ cancels
   (screenshot `C:\ob\frames-menu1\o_triangle.png`, game data, not in git).
5. **The game has its own control types.** The pause menu shows "Controller settings: TYPE A (□ ad-lib, ✕
   cancel/dash, ○ OK/attack)". The type is `Options` `0x306590 + 0x2C` (0..5), which picks the confirm/cancel buttons
   from the table at `0x2168C0` (`main/ctrl.c:210`). The touch labels assume Type A today.
6. Before the game task: the title and select screens are known states (`gate/boot.script`: `TitleWork*` at
   `0x23BE34`, `SelectWork*` at `0x3065FC`); the game task is installed when `0x2F8654` = `0x5CD580`.
7. Reading guest RAM from the touch code: the settings-UI hook (patch 0078) has no memory access. The external IPU is
   handed the RAM pointer in the same library (`native/pcsx2_gs/adapter/hook.cpp:157`), but only while `PS2X_IPU` is
   on. A small PS2Recomp patch giving the UI hook a read-only view of guest RAM is the clean way (with a test).

Not covered yet: item boxes, the "take it?" style prompts and text windows, Game Over / continue, the files screen
inside status, rooms other than J's Bar, and what each control type number means.

Proposal:

| # | Proposal |
|---|---|
| T15 | A **menu layout**: a large D-pad, ○, ✕, △, small L1 / R1 and Start / Select, and the Menu button; the gameplay pad, Aim and Talk hidden. |
| T16 | It shows by itself while `0x48BF7E` = 1 in the game task, and on the title and select screens before it; the gameplay layout otherwise. Read once per pad read on the game thread (one byte), handed to the overlay like `physicalPadReads`. |
| T17 | The read goes through a new PS2Recomp patch (read-only guest RAM for the UI hook), not the IPU's pointer. |
| T18 | A small **D-pad toggle** stays on the gameplay layout as a fallback for screens the flag does not cover (prompts, item boxes) until they are researched. |
| T19 | Labels follow the game's control type (`0x306590 + 0x2C`); until its values are mapped, Type A labels, and glyphs only for other types. Value 0 = TYPE A: confirmed live (run `ctype`: the byte reads 0 while the pause menu shows TYPE A; the menu byte 1 with the pause menu open, 0 after; the game task `0x5CD580` in its slot). |

Built (developer, 2026-10-26: "Approved: build T15-T19 ... including the PS2Recomp patch for read-only guest RAM"):
PS2Recomp 0081 (`PS2ExternalUi::attachGuestRam`), `native/ob_ui/core/game_screen.*` (`classifyScreen`; read by
`NativeBridge.gameScreen` ten times a second, nothing per frame), `ButtonSets.MENU` and `LayoutChooser` in
`TouchLogic.kt` (the toggle flips the game's choice until the screen changes), the menu set's own placements
(`touch_layout.ini [menu]`) and a third Edit layout view ("menu screens").

## 7. Sources

- Repo: `legacy/phase1-feasibility-report.md:487`, `:493`, `:494`, `:496`, `:504`; `legacy/mod/game.h:30`;
  `legacy/mod/mod.c:299-306`, `:316`; `legacy/README.md:22-32`; `native/input.ini.example`;
  `docs/plans/2026-10-26-settings-menu.md` (A14, P2, P4, P6).
- Web (read 2026-10-26): psxdatacenter.com game pages SLPM-65428, SLES-51589, SLUS-20765 (controls lists);
  speedrun.com `remake_hd/forums/pid4s` (tank vs alternate controls, quick turns); steamcommunity.com
  `app/304240/discussions/0/600765159912278181` (holding the stick through camera cuts).
- Not opened: the Outbreak manual page on the Resident Evil wiki (`REOF1Files_Playing_Manual_1_09.png`, HTTP 402);
  its search summary agreed that the D-pad and the stick move differently.
