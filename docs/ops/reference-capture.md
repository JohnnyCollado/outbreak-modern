# Reference capture (PCSX2 2.9.134, SLPM-65428)

Source: phase 1.2 T1; research R1.2-003, R1.2-006 (settings, ini keys), R1.2-005 (display init); boot order from
gate/boot.script and 1.1 T4. The developer launches PCSX2 and shoots (task T4); no agent launches PCSX2.
Images land in `ps2_emu/snaps/` and are copied to `C:\ob\ref\`; game-derived images are never committed.

## Settings (the ini key is authoritative; `PY tools/gate/refs.py check-settings` checks it)
UI paths follow the 2.9.x Qt layout from the R1.2-006 reading; a label that differs on screen does not matter, the key does.
`[EmuCore/GS]` unless noted. Values are compared as strings.
- Settings > Graphics > Renderer: Software -> `Renderer = 13` (today -1)
- Graphics > Media Capture > Screenshot Size: Internal Resolution (Aspect Uncorrected) -> `ScreenshotSize = 2` (today 0)
- Graphics > Media Capture > Screenshot Format: PNG -> `ScreenshotFormat = 0`
- Graphics > Media Capture > organize screenshots by game: off -> `OrganizeScreenshotsByGame = false`
- Graphics > Display > Deinterlacing: No Deinterlacing (Off) -> `deinterlace_mode = 1` (today 0 Auto)
- Graphics > Display > Bilinear Filtering: None -> `linear_present_mode = 0` (today 1 Smooth)
- Graphics > Display > Crop: all 0 -> `CropLeft = 0`, `CropTop = 0`, `CropRight = 0`, `CropBottom = 0`
- Graphics > Display > Screen Offsets: off -> `pcrtc_offsets = false`; Show Overscan: off -> `pcrtc_overscan = false`
- Graphics > Post-Processing: FXAA off -> `fxaa = false`; Shade Boost off -> `ShadeBoost = false`;
  TV Shader None -> `TVShader = 0`; Contrast Adaptive Sharpening None -> `CASMode = 0`
- Graphics > Rendering > Internal Resolution: Native -> `upscale_multiplier = 1` (Software is native anyway)
- Settings > Folders > Snapshots: `snaps` -> `[Folders] Snapshots = snaps`
- Recorded in the manifest, not checked (effect under Software undetermined; T5 weighs them):
  `pcrtc_antiblur` (default true), `dithering_ps2` (today 2), `AspectRatio` (ignored by ScreenshotSize 2)
- No per-game override: `ps2_emu/gamesettings/SLPM-65428*.ini` must not set any key above (check-settings reads it if present).
Why Off/None: deinterlace Off is the pass-through path (Bob adds a half-line offset), bilinear present would blur
the saved frame; the screenshot is taken after merge, so these settings are baked into the file (R1.2-003).

## Hotkeys (read from `ps2_emu/inis/PCSX2.ini [Hotkeys]`, 2026-10-22)
- Screenshot: `Keyboard/F8` (:536). TogglePause: `Keyboard/Space` (:551). OpenPauseMenu: Escape (do not use: the menu overlays the frame).

## Predicted screenshot size
512x448 for every screen, unverified. Software output width = FBW*64 (R1.2-006); the runner's display size is 512x448
(DW/MAGH), but FBW is not readable by a `watch` step (the script reads RDRAM only), so the width is a prediction.
Risk: 640x448 if the game keeps FBW=10 from its 640x448 init (R1.2-005); a height of 224 if PCSX2 emits one field.
The first dry-run below settles it before more shots are taken.

## Procedure
1. Apply the settings; run `PY tools/gate/refs.py check-settings` until it exits 0.
2. `PY tools/gate/refs.py begin` (session marker; earlier snaps, e.g. the five 827x620 ones, are ignored).
3. Boot a: boot the game fresh; shoot the six screens below in order (Space to pause, F8, Space to resume).
4. After the first shot (savedata): `PY tools/gate/refs.py intake --dry-run` -> must show `512x448 OK`. If not, stop
   and report the size; do not shoot on.
5. Boot b: shut the VM down, boot fresh again, shoot the same six.
6. `PY tools/gate/refs.py intake --dry-run` -> 12/12, no REJECT; then `PY tools/gate/refs.py intake`.
7. Open `C:\ob\ref\sheet.png`; confirm every label matches its picture.
Exactly one shot per screen per boot. A stray extra shot shifts the mapping: move it out of `ps2_emu/snaps/`
yourself (the tool never moves or deletes snaps) or start over at step 2.

## Per-screen checklist (boot order; inputs match gate/boot.script so both sides show the same state)
Shoot each when it is fully faded in and still, before any input on that screen.
- savedata: save-data / memory-card prompt, default cursor. Then Left, Circle (as the script does).
  The prompt text depends on the memory-card contents; compare with the runner frame on the contact sheet.
- (content warning: no input, it advances by itself; not shot)
- access: ACCESS logo, fully visible. Short (about 2 s): pause promptly.
- capcom: CAPCOM logo, fully visible.
- title: title screen with PRESS START visible. It pulses (fades out and back, ~1.4 s); any frame where the text
  shows is fine: the runner captures one full pulse (64 frames). Then Start.
- mainmenu: main menu, default cursor, no input yet. Then Circle (single play), Circle on the entry screen,
  Circle on Scenario Select (default scenario).
- charselect: character select as it first appears (within ~2 s), default character, portrait shown, no input.
  The runner captures it ~1.5 s after it appears; on the runner the portrait disappears ~2.3 s in (unverified on PCSX2:
  note it if PCSX2 does the same).
