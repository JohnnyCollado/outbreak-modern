# Settings menu: launcher and in-game menu

- **Date:** 2026-10-26
- **Who:** Johnny (developer), Claude Code (main session, plus two helper agents: `ob_ui` core logic, Android picker bridge)
- **Goal:** Build the approved plan (`docs/plans/2026-10-26-settings-menu.md`) in one go: a launcher before the game
  (disc required and verified, textures and mods optional), an in-game menu on F1 / Esc / hold Back + Start that
  pauses the game, the Controls / Camera / Display / Memory cards / Audio / Graphics driver (Android) / Advanced pages,
  and the Android file picker.
- **Outcome:** the launcher and the in-game menu work on Windows (ImGui, mouse and step-in controller navigation)
  and Android (Kotlin + Compose); PS2Recomp 0077-0080; developer: "everything looks great".
- **Versions:** PS2Recomp `eb8688b` + patches 0001-0076, then 0077-0079 (`C:\ob\up`); PCSX2 v2.9.134 (`C:\ob\pcsx2`,
  its imgui 1.93.0); raylib 5.5; MSVC 19.44; Android NDK 27, build-tools 35.0.0, Android Studio JBR (javac 25.0.3);
  game SLPM-65428 v1.04 + English patch v2.1; Windows 11; AYN Odin 3 Max.

## Summary

The plan was built as approved, then reshaped by the developer's testing the same day: the first ImGui menu was
"horrible", so Android moved to Kotlin + Compose with PlayFieldPortal's controller handling, and Windows got a
restyle with a step-in navigation model. Three runtime patches carry it (0077 hold, 0078 UI hook, 0079 `[ui]`
section). The hard bugs were not in the menu: the left stick lacked an angle because of a float/double ABI mistake in
PS2Recomp's libm stubs (0080), the GS reopen that the launcher needs dropped widescreen and textures and then left
dangling fonts, and Android's window loss on the system file picker gave a black screen. A stick-order "fix" read from
the decomp's names was wrong and was dropped.

## Struggles

### 1. Esc closes the game window today (a claim in the plan was wrong)

- **Seen:** while wiring the hotkeys: `SetExitKey(KEY_NULL)` is called only for a hidden window
  (`ps2_runtime.cpp`, the `if (hidden)` block in window creation).
- **Found by:** reading the window creation code before moving it.
- **Cause:** patch 0035 turned the exit key off for scripted (hidden) runs only; a visible window keeps raylib's
  default, Esc = close. The plan (P1) said Esc "no longer quits".
- **Fix:** with a settings UI present, the exit key is off for every window (patch 0078), so Esc opens the menu.
- **Evidence:** patch 0078 (`OpenHostWindow`).
- **Take-away:** check the condition around a call, not only that the call exists.

### 2. The runner's settings parser rejects every key it does not know

- **Seen:** the menu's own settings (window mode, vsync, volume, ...) would each print a
  `[settings] line N: unknown key` at every start.
- **Found by:** reading `parseSettingsIni` (`ps2_game_source.cpp`) before adding keys.
- **Cause:** one flat key list; anything else is a bad line.
- **Fix:** patch 0079: lines under a `[section]` header belong to other readers; the menu keeps its keys under `[ui]`
  in the same `outbreak.ini`. Test fails without (554/555), passes with (555/555).
- **Evidence:** patch 0079; `ps2_game_source_tests.cpp` "lines under a [section] header".
- **Take-away:** a new consumer of a shared file needs its own namespace in it.

### 3. Heredoc + Python lost an escape level again

- **Seen:** `error C2001: newline in constant` in the new settings test.
- **Cause:** `"\\n"` written through a Bash heredoc into Python into C++ (the memory note "heredoc escaping trap").
- **Fix:** the Edit tool for C string literals.
- **Take-away:** as the note says; also, a long `cmd /c "${VC//\//\\}"` one-liner breaks the shell wrapper: put it
  in a script file.

### 4. The launcher stayed at the window's start size when maximized (developer's screenshot)

- **Seen:** the developer maximized the launcher: the menu stayed a 640x448 box in the corner, the rest black.
- **Cause:** window resizes reach the GS only from the run loop (`ps2ExternalGSWindowResized` in `PS2Runtime::run`),
  which is not running yet during the launcher.
- **Fix:** the launcher loop forwards `IsWindowResized` to `ob_gs_window_resized` (`adapter/ui_hook.cpp`).

### 5. Binding buttons looked empty

- **Seen:** same screenshot: the Controls page's binding buttons showed no text.
- **Cause:** fixed 320 px buttons wider than the page column: ImGui centres the label, outside the clipped area.
- **Fix:** widths clamp to the space left (`std::min(320 * scale, GetContentRegionAvail().x)`), the label column
  to 40% of the page.

### 6. Android: touches missed the buttons; Back did nothing; small text

- **Seen:** on the Odin the launcher drew at 1920x1080 and verified the disc (3/3), but a press on Play did nothing.
- **Found by:** reading raylib 5.5 `rcore_android.c`: touch positions are normalized to the window size the runner
  asked for (640x448), while ImGui draws at the GS surface (the display, 1920x1080). Its key handler records Back in
  the key-pressed queue, but a down and up inside one frame leave the key state up.
- **Fix:** pointer scaled by DisplaySize / window size; presses read from `GetKeyPressed` (the queue); the font size
  pushed explicitly (`PushFont(nullptr, 13 * scale)`: `FontScaleMain` alone did not enlarge it).
- **Not verified yet:** a press on Play after the fix: the device's notification shade was open over the app.

### 7. Android build: two compile errors

- `menu_trigger.h`: `explicit MenuTrigger(Config config = {})` with default member initializers in the nested
  `Config`: clang refuses it inside the class; MSVC accepts. Fix: a separate default constructor.
- `native/pcsx2_spu2/adapter/spu2_side.cpp`: `iopHw` defined `alignas(16)`, PCSX2 declares it
  `alignas(__pagealignsize)` (`MemoryTypes.h:67`); clang checks the match. The SPU2 (and IPU, SIO2) had never been
  built for Android (the last Android build predates them). Fix: the same alignment as the declaration.

### 8. Android: black screen after importing a texture pack (developer on the Odin)

- **Seen:** the developer imported a texture `.zip` from the launcher; afterwards the screen stayed black. The copy
  itself finished (`files/textures/SLPM-65428.zip`, 3,286,561,551 bytes) and the app was still running.
- **Found by:** logcat: `eglSwapBuffersWithDamageKHR ... EGL_BAD_SURFACE` from 16:09:07, when the system picker
  came up, then nothing more from the GS.
- **Cause:** opening the picker sends the app to the background, and Android destroys its window. On return there
  is a new `ANativeWindow`: raylib makes a new EGL surface on it (`rcore_android.c` APP_CMD_INIT_WINDOW), but
  PCSX2's swapchain still pointed at the old one. This was already listed as open: "App pause/resume (window lost) is
  not handled" (`reports/2026-10-24-android.md`). Any trip to the background (Home, the shade) hits it too.
- **Fix:** `ob_gs_window_ready()` (adapter), each host frame from the launcher loop and the in-game hook. With no
  window, the launcher draws nothing and the game is held. With a new window, raylib's EGL surface is released again
  and PCSX2's swapchain is rebuilt on it (`SetRenderWindow` + `GSUpdateDisplayWindow`).
- **Evidence:** Odin, Home then back to the app: `[pcsx2-gs] android window back 1920x1080: swapchain moved (egl
  surface released=1)` and the launcher redrawn (screenshot in the session).
- **Still open:** the import copies on the main thread, so the screen freezes during a 3 GB copy. Not yet tried:
  backgrounding during gameplay (the same code path, game held).

### 9. Windows menu rework: restyle and step-in navigation (developer: "horrible", then the step-in model)

- **Seen:** the developer found the first ImGui look poor (default debug style, tiny bitmap font) and ImGui's spatial
  gamepad navigation unnatural on Windows handhelds.
- **Fix:** `native/ob_ui/ui/settings_ui.cpp` rewritten: Segoe UI / Semibold + Segoe Fluent Icons from
  `C:\Windows\Fonts` (nothing bundled), text 18 px at 720p (x1.15 / x1.3 with Display > Text size), cards, accent Play.
  ImGui's own navigation is off; the menu owns focus: page list (initial) > A into the page's rows > B out; down past
  the last page reaches Quit / Play; left/right change values; held directions repeat 250 ms then 110 -> 50 ms over 5
  steps, stick engages at 0.5 and releases at 0.3 (ported from PlayFieldPortal's GamepadInputHandler). Prompts and the
  focus outline only after controller or keyboard input; the mouse hides them and gets Windows' own Browse dialogs
  (`ui/file_dialog.cpp` restored). The launcher opens at 1280x720 when no `window` is set.
- **Snags:** ImGui 1.93 swapped `AddRect`'s last two parameters (the old order is `= delete`); icons merged only into
  the text font showed a replacement glyph on the bold Play button (merged into both now); the hover colour equalled
  the selected one, so a hovered tab looked selected (hover is a faint neutral tint now); the footer text ran under
  Quit (shortened and clipped).
- **Evidence:** runner builds; `[ui] fonts: text=segoeui bold=seguisb icons=SegoeIcons.ttf`; all suites green
  (ob_ui 76/76); the developer's screenshots.

### 10. Windows: a Mods page (developer: "manage/enable and disable what they want to use")

- **What:** a "Mods" page after Display, in the launcher and the in-game menu. The mods folder card (Browse /
  Clear) moved there from the Game page. Each `.pnach` / `.pnach.disabled` in `<mods>/SLPM-65428/` is a row: its
  name, file and size, On/Off and Delete (confirm, Cancel focused).
- **Off = renamed to `.pnach.disabled`:** the runner loads only files whose extension is `.pnach`
  (PS2Recomp `ps2_pnach.cpp:221`), so the file stays and is skipped. The same convention as the Android launcher.
- **Name shown:** the file's first `[section]`, then `description=`, then the file name. `gametitle=` names the game
  (the same line in every mod), so it is not used, unlike the first brief.
- **The widescreen file is not listed** (developer): `ob-widescreen.pnach` belongs to Display > Aspect ratio only.
- **In game:** the toggles work, with "Changes apply the next time the game starts" (pnach files are read at
  start-up); the folder changes only in the launcher.
- **Not verified by hand yet.**

### 11. Any widescreen pnach is the Aspect ratio switch (developer, both platforms)

- **Seen:** the developer's own `files/mods/SLPM-65428/widescreen.pnach` on the Odin (pushed 2026-10-24; it has
  `gsaspectratio=16:9` and `patch=1,EE,20239d48,extended,3fe38e38`) showed on the Mods page as a normal mod, next
  to the menu's own `ob-widescreen.pnach` written by Display > Aspect ratio: two widescreen patches could load.
- **Fix:** one rule in `native/ob_ui/core/widescreen.{h,cpp}` (`isWidescreenPnach`, `findWidescreenMods`,
  `widescreenEnabled`, `setWidescreen`), used by the Windows menu directly and by the Kotlin launcher through JNI
  (`NativeBridge.isWidescreenModFile / widescreenEnabled / setWidescreen`). A widescreen mod is any `.pnach` /
  `.pnach.disabled` with that patch line or `gsaspectratio=16:9` (case and spaces ignored, `//` and `;` comment lines
  skipped), plus `ob-widescreen.pnach`. The Mods page never lists, counts, toggles or deletes them; Aspect ratio shows
  16:9 when one is enabled. 16:9 renames one disabled widescreen mod back (the player's own before the menu's) or
  writes `ob-widescreen.pnach` when there is none; 4:3 renames every enabled one to `.pnach.disabled`. Nothing is
  deleted. The runner loads only `.pnach` (PS2Recomp `ps2_pnach.cpp:221`).
- **Evidence:** `ob_ui` 89/89 (13 new: detection, create-when-none, 4:3 disables all and keeps other mods,
  16:9 re-enables the player's file only). Device: the file's two lines confirmed with `grep`; the APK with the
  change installed. The pages were not opened on the device: PlayFieldPortal was in front (the developer's).

### 12. Tab rows: icon and text centred, not the list (developer correction)

- **Seen:** "When I said to center align the tabs, I was talking about their icon and text. vertically." The list
  had been centred in the column instead (entry 9).
- **Cause:** Segoe Fluent Icons' glyphs sit higher and taller in the line than Segoe UI's letters, so drawing
  `icon + text` as one string puts them on a shared top, not a shared middle.
- **Fix:** the list starts at the top again. `IconText` draws the icon and the text separately, each centred on the
  row's middle line by its own ink (the baked glyph's `Y0..Y1`; a capital `H` for text). Used for the tabs, the
  header icon, the "Disc verified" pill, the disc check's steps, the browser's entries and the Play button.
  Kotlin: tab rows are a `Box(contentAlignment = CenterStart)` of fixed minimum height with the text's font padding
  trimmed (`LineHeightStyle.Trim.Both`).
- **Not verified by hand yet** (Windows rebuilt and relaunched; the Kotlin change is not in an APK yet, see 13).

### 13. Android build: `android_native_app_glue.h` not found in `ui_hook.cpp`

- **Seen:** `ob.sh android` after a change to `native/pcsx2_gs/adapter/ui_hook.cpp` (catching KEYCODE_BACK through
  `app->onInputEvent`): `ui_hook.cpp:20:10: fatal error: 'android_native_app_glue.h' file not found`.
- **Cause:** `ps2x_external_gs` (which compiles `ui_hook.cpp`) has no `${CMAKE_ANDROID_NDK}/sources/android/
  native_app_glue` include directory; `ob_pcsx2_gs_adapter` and `ob_ui` have it.
- **Status:** fixed by the coordinator (`native/pcsx2_gs/CMakeLists.txt`: the NDK `native_app_glue` include on
  `ps2x_external_gs` for Android); the next `ob.sh android` built.

### 14. Start and Select felt laggy; the menu combo changed (developer, three steps the same day)

- **Seen:** in play, Start and Select reacted late: the menu combo's 100 ms hold-back (plan P2, section 5.3) held
  each press back in case the other half of Back + Start followed.
- **Decisions:** first "No delay" (hold-back 0), then no combo at all, then per platform: Windows keeps Back (View) +
  Start but held together for 5 s, with a gear ring showing the hold; Android has no combo, its Back button opens the
  menu. F1 / Esc everywhere.
- **Fix:** `MenuTrigger::Config` gained `combo` (default on, so the existing tests are unchanged) and the trigger
  `comboProgress(now)` (0..1). The menu builds it with `Config{5.0, 0.0, false, true}` on Windows and
  `Config{5.0, 0.0, false, false}` on Android. One core change was needed for hold-back 0: the pair check ran before
  the buttons were marked passed, so Select + Start pressed on the same read were still hidden together; it now only
  runs with a hold-back (`holdBackSec > 0`). The ring: `hostFrame` sets `ob_gs_set_hold_progress` each frame (0 when
  not held or the menu is open); `pcsx2_side.cpp` `ExecVsync` draws it on the GS thread inside the present like the
  FPS readout (below it when that is on): a dark rounded box, a dim full circle, the blue arc clockwise from 12
  o'clock, and the Segoe gear U+E713 from the menu's font (`ob_gs_set_overlay_icon_font`, set when its fonts load).
  PCSX2's imgui is 1.92.8+: `PathStroke(col, thickness, flags)` (the old argument order is deleted).
- **Texts:** Android: "The Back button still opens the menu"; Windows: "F1, Esc or holding Back + Start for 5 s".
- **Evidence:** `ob_ui` 99/99 (no hold-back: Start alone and Select + Start pass on the first read, no combo toggle;
  5 s combo: progress half way at 2.5 s, reset on letting go, toggle 5 s after holding again, ring hidden once fired).
- **Not verified by hand yet.** The ring's gear only appears once the menu's fonts have loaded (the launcher draws
  them); with "Show this screen at start-up" off, the ring is drawn without the gear.

### 15. Left stick "moves wrongly" in tank controls (developer, Windows and Odin): not reproduced in the pad pipeline

- **Seen:** the developer reports the left stick does not drive the character correctly in Classic (tank) controls on
  both platforms; buttons and the D-pad are fine.
- **Found by:**
  - Odin, a temporary log in `ui_hook.cpp` `filterPad`: the values in `PSPadBackend::readState`'s data follow raylib
    exactly (full left lx=1, right 255, up ly small, down large, centre 128; dead zone as configured). libpad2's HLE
    copies them unchanged (`Pad2.cpp:188-189`). The Odin has one stick device ("Xbox Wireless Controller"), so nothing
    re-centres the axes.
  - Windows, a scripted run through the new `stick` step (below), current build, SIO2 on: `gate/stick.script` (check
    route to J's Bar, Start x2 to skip the opening, idle pair, `stick left 0 -1 120`, `stick left -1 0 60`). Kevin
    stands still between `idle-a`/`idle-b`, walks forward toward the bar in `walk` (camera reframed) and turns to face
    the camera in `turn` (`C:\ob\frames-stick-now2`). Same with port 2 given the same sticks as port 1, as the host
    path does (`C:\ob\frames-stick-mirror`; the experiment switch was removed afterwards).
- **Result:** the game accepts the analog stick through the pad HLE in the current build; the SIO2/dbc hypothesis
  (patches 0071/0073) is ruled out, and the host path's stick bytes are correct where they enter the pad HLE. Not
  found yet: what "moves wrongly" is in play. Untested: the host-controller path end to end (a scripted run cannot
  inject a real gamepad; the diagnostics plugin replaces the host input), and the settings menu's pad filter on it.
- **Added:** input script `stick <left|right> <x> <y> <vsyncs>` (`native/ob_diag/src/input_script.{h,cpp}`, the plugin's
  `padSticks` now serves the script on port 1); ob_diag test "stick holds the left stick, then centres it" (24/24);
  `gate/stick.script`. Unpaced runs need the Start presses: the J's Bar opening still plays at tick ~16300 without them.
- **Note:** a timing-sensitive ob_diag test (PcSampler) failed once in a full run and passed 3/3 alone.

### 16. Left stick: no angle (patch 0080), and a stick swap that was wrong

- **Seen:** the left stick did not walk (tank controls) and set off ad-libs; in PCSX2 the D-pad is tank movement and
  the left stick direction-relative movement, the right stick ad-libs only (developer).
- **Found by:** a temporary Android log in `ui_hook.cpp` showed correct stick bytes and no held button reaching the
  pad read; memory watches in J's Bar (`gate/stick-mem.script`) showed the game's own stick record filled (x/y,
  magnitude 127). The decomp then: the angle is `atan2(-y, x)` on doubles (Outbreak-decomp `padread.c` 100-134), and
  PS2Recomp's `atan2` stub read floats from f12/f14 and wrote f0, while the EE passes 64-bit soft-float doubles in
  `$a0`/`$a1` and expects `$v0` (the recompiled caller at 0x1A5990/0x1A5998).
- **Fix:** patch 0080: `sqrt`, `atan`, `atan2`, `floor` (the four this game binds) use the double ABI. Test fails
  without: 555/556.
- **Wrong turn:** before that, a patch put the left stick first in `scePad2Read` (from the decomp's names). It only
  moved the ad-libs to the right stick and left the left stick dead; once the angle worked the sticks were swapped
  (developer). PCSX2's DualShock 2 sends the right stick first (`PadDualshock2.cpp` 268-279), as the HLE always did:
  the decomp's left/right names are swapped. That patch was dropped and 0080 renumbered.

### 17. Windows: 4:3 and no texture pack after the launcher

- **Seen:** black bars at the sides on Windows with Aspect ratio at 16:9.
- **Cause:** the launcher opens the GS before the game, its mods (pnach aspect) and its serial are known
  (`open ... aspect=Auto 4:3/3:2 ... serial=- replacements=none`); the reopen ran only after a change made in the
  launcher. So 16:9 and the texture pack were lost whenever the launcher was shown.
- **Fix:** `ob_gs_reopen_if_changed`: the GS keeps what it opened with; `settingsChanged` reopens when the game's
  options differ (`options differ from the open (aspect Auto 4:3/3:2 -> 16:9, serial '' -> 'SLPM-65428')`).

### 18. Windows: Start + Select (the menu) crashed after that reopen

- **Cause:** `GSclose` destroys ImGui's context with its fonts (`ImGuiManager::Shutdown`); the menu and the hold ring
  kept pointers to them.
- **Fix:** the menu reloads its fonts and style on a new context; the reopen clears the ring's icon font.

### 19. Windows: borderless and fullscreen stayed black

- **Seen:** switching to Borderless or Fullscreen gave a black window; Windowed brought the picture back.
- **Likely cause (not confirmed):** raylib's toggles make the window exactly the monitor's size, which Windows takes as
  fullscreen and flips raylib's black GL picture straight to the screen, hiding the GS child window.
- **Fix:** both by hand (`ob_gs_set_borderless`, `ob_gs_set_display_mode`): frameless, the monitor's size plus one
  pixel; fullscreen first switches the display mode (CDS_FULLSCREEN, restored on exit). Developer: works.

### 20. Android: the Back button, the FPS size, drivers, quitting

- **Back:** raylib drops keys from gamepad-class devices it cannot map (the Odin's Back); `ui_hook.cpp` now catches
  `AKEYCODE_BACK` ahead of raylib (`app->onInputEvent`).
- **FPS readout:** sized from the display height.
- **Drivers:** a package made for a newer Android (`minApi` 36 on API 35) can be chosen, with a note that it may
  have bugs (`MinApiWarning`; the game logs and falls back as before). Imported driver folders get names the game
  accepts (a space made it fall back to the system driver silently).
- **Quit:** "Quit game" finishes the activity and its task, so the front end that started the app gets control back
  cleanly (developer: quitting disturbed it).

### 21. Start/Select lag; the menu buttons

- **Seen:** Start and Select felt laggy in play.
- **Cause:** the 100 ms hold-back that kept the menu combo out of the game.
- **Fix:** no hold-back. Android: the Back button opens the menu, no combo. Windows: Back + Start held 2 s with a
  gear ring (5 s at first). Steam Input was turning View + Menu into Windows' task switching (developer).

### 22. App icons

- The developer's icons (`docs/assets/`): the circle for Android's round and adaptive slots, the rounded square for
  the classic slot and Windows (window icon `outbreak-icon.png`, `outbreak.ico`). `tools/ob/make_icons.py` makes
  every size; the adaptive layer keeps the circle inside the 72 dp every mask shows.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Plan P1: "Esc no longer quits (SetExitKey(KEY_NULL), patch 0035)" | Only for hidden windows; a visible window closed on Esc | Reading the window code for patch 0078 | Patch 0078; this report entry 1 |
| Entry 15: the stick regression comes from the SIO2/dbc work | The stick never had an angle (libm ABI) | Memory watches, then the decomp's `atan2` | Entry 16, patch 0080 |
| Widescreen by default "only until the player chooses" | It also ran in hidden scripted runs and wrote `mods`/`[ui]` into `gate/defaults.ini` and a pnach into `gate/mods/` | `git status` before the commit | The default only with a visible window; the files restored; gate 3/3 rerun |
| The game reads its left stick from `scePad2Read`'s d[2..3] (decomp names) | PCSX2 sends right first; the HLE's order was right | The developer: after the swap the sticks were reversed | Entry 16; the swap patch dropped |

## Still open

- Other libm stubs (sin, cos, tan, pow, exp, log, ceil) keep the float form; this game does not bind them.
- Windows fullscreen and borderless done by hand: the cause of the black screen is a likely explanation, not proven.
- The hold ring shows no gear until the menu has opened once after a GS reopen.
- No unit tests for the menu navigation; the Kotlin UI has none.
- The Windows `.exe` itself has no icon resource yet (the window and taskbar have it).

## Useful for other developers

- `EeScheduler::setHeld` (patch 0077): a host pause that parks the guest at its next checkpoint or wait and moves
  the paced deadlines past the pause. Any host overlay can use it.
