# Android touch controls

- **Date:** 2026-10-26
- **Who:** developer (Johnny) and a Claude Code session (design, comparison, implementation)
- **Goal:** build the on-screen touch controls of `docs/plans/2026-10-26-touch-controls.md` (decisions T1-T14) for
  the Classic camera, with the settings toggle, size and opacity, and editing; all suites green, no warnings.
- **Outcome:** built on branch `android-touch-controls`; see Summary for what is verified and what waits for the
  developer's device test.
- **Versions:** outbreak-modern `main` f386a8a + this branch; PS2Recomp patches 0001-0080 (no new patch); AGP 9.4.0,
  Gradle 9.7.1, Kotlin 2.4.10, Compose BOM 2026.08.00, JUnit 4.13.2 (local Gradle cache); NDK as `docs/ops/pins.md`.

## Summary

The touch controls need no runtime patch: PS2Recomp patch 0078 already lets the settings UI filter every pad read
(`PS2ExternalUi::filterPad`), so the overlay's state is merged there as a second controller
(`native/ob_ui/core/touch_pad.*`, lock-free). The overlay is a never-focused dialog window over the NativeActivity, so
a controller's keys keep reaching the game while the window takes the touches. The Modern camera does not exist in
the native port yet (both menus say "coming later"), so only the Classic layouts are built; T7 and T9 wait for it.

## Comparison with the design (before building)

| Design | Code before | Built |
|---|---|---|
| Overlay over the game | none (Android: only the Kotlin menu dialog) | `TouchOverlay.kt`: never-focused full-screen dialog window |
| Touch into the game's pad | `filterPad` hook (patch 0078, `ui_hook.cpp:384`) | `obui::touchPad().merge` first in `obui::filterPad` (port 0), before the menu gate |
| Auto: hide on controller use | nothing told Kotlin | physical-use counter in the same merge, polled every 250 ms (`NativeBridge.physicalPadReads`) |
| Menu > Settings from the overlay | menu opened only by Back / F1 / Esc | `NativeBridge.requestMenu` -> `takeMenuRequest` in `hostFrame` |
| Settings rows (section 4.5) | Controls page: bindings, dead zone | "Touch controls" section on the Controls page, launcher and in game |
| Edit layout over the held game | - | the menu dialog steps aside, the game stays held, Done brings it back |
| Modern layout (T7, T9) | Modern camera "coming later" | not built |

## Struggles

### 1. The CRLF test file did not take a scripted edit

- **Seen:** a Python string replace into `native/ob_ui/tests/ob_ui_tests.cpp` reported nothing changed.
- **Found by:** `grep -c TouchTests` = 0, `file` = "with CRLF line terminators".
- **Cause:** the search strings had LF line ends; the file has CRLF (as the other ob_ui core files).
- **Fix:** made the edit with the Edit tool; the new `touch_pad.*` files converted to CRLF to match.
- **Evidence:** ob_ui 108/108 (99 before + 9 touch tests).
- **Take-away:** check `file <path>` before a scripted edit; prefer the Edit tool (memory "heredoc escaping trap").

### 2. A third quick tap would fire a second quick turn

- **Seen:** while writing the double-tap test: the second tap's release counted as a fresh tap, so tap-tap-tap turned
  her twice.
- **Found by:** writing `doubleTapNeedsTwoShortTaps` in `TouchLogicTest.kt`.
- **Cause:** `DoubleTapDetector.up` recorded every short tap, the pair's second one included.
- **Fix:** the second tap of a pair never starts a new pair.
- **Evidence:** `TouchLogicTest.doubleTapNeedsTwoShortTaps`.
- **Take-away:** gesture detectors need a test for the repeat case, not only the first fire.

### 3. Copying the disc image to the phone: Git Bash rewrote the phone path

- **Seen:** `adb push ... /sdcard/roms/...` failed: "failed to copy ... to 'C:/Program Files/Git/sdcard/roms/...':
  remote secure_mkdirs() failed".
- **Found by:** the error text (a Windows path on the phone side).
- **Cause:** MSYS path conversion turns an argument starting with `/` into a Windows path.
- **Fix:** `MSYS_NO_PATHCONV=1 adb ...`. The retry then found the phone unplugged; nothing reached it either time
  (checked with `find /sdcard -name "Biohazard*"`).
- **Take-away:** every adb command with a device path from Git Bash needs `MSYS_NO_PATHCONV=1`.

### 4. Menu research: a run at 2-4 fps, and a boot that waited through the opening movie

- **Seen:** live-pad commands timed out after the map opened; the boot driver timed out waiting for game state 2.
- **Found by:** `[fps]` lines in `C:\ob\logs\boot-menu1.log` (2-4 fps); a `peek` of `0x48BF60` = 14.
- **Cause:** the Windows default VU1 is the interpreter (memory "speed runs need mvu"); after J's Bar loads, the game
  first plays its opening movie (state 14), which only Start skips.
- **Fix:** `PS2X_VU1=mvu` for the second run; the driver presses Start / ○ until state 2 instead of waiting.
- **Evidence:** run `menu3`: every open/close of status, map and pause matched `0x48BF7E` (design section 8).
- **Take-away:** use `PS2X_VU1=mvu` for any live-pad run that reaches gameplay.

### 5. A warning hidden by incremental builds: `u8path` is deprecated in C++20 (Android)

- **Seen:** after patch 0081 changed `ps2_ui_external.h`, the Android build printed
  `main.cpp:154: warning: 'u8path<char *, 0>' is deprecated [-Wdeprecated-declarations]`.
- **Found by:** grepping `ob-android-build.log` for warnings after the build; `git blame` in `C:\ob\up`: the line came
  from our own patch 0043, and five more `u8path` calls sit in `ps2_diag.cpp`, `ps2_game_source.cpp` (3) and
  `ps2_input_bindings.cpp`, silent only because those files had not recompiled since.
- **Cause:** libc++ (NDK 27) marks `std::filesystem::u8path` deprecated with C++20's `char8_t`; incremental builds
  only show warnings for files they recompile, so the earlier "0 warnings" counts were not a full picture.
- **Fix:** PS2Recomp patch 0082: `runtime/ps2_path_utf8.h` `ps2path::fromUtf8` (the bytes as `char8_t`, the same
  conversion) for all six calls; then the whole runtime and our native code were recompiled for Android to check.
- **Take-away:** to claim "no warnings", force a recompile of the touched libraries once; incremental counts miss old
  warnings.

### 6. OnePlus (Android 16): a picked disc fails "This file can't be opened"

- **Seen:** the developer's screenshot (2026-10-27): the disc picked in the launcher failed step 1, "This file can't be
  opened. Check that it isn't in use, then choose it again."; no `game-uri.txt` was written (only verified discs are).
- **Found by:** reading the path: Kotlin opens the content URI (it worked, so `checkDiscUri` was not null) and hands C++
  `/proc/self/fd/<n>`; `checkDisc` passed `exists()` (a stat follows the link) and failed at `fopen`. The runtime's
  `IsoImage::open` does the same `fopen` (and a `file_size` by name), so the game would have failed next.
- **Cause:** reopening `/proc/self/fd/<n>` opens the target by its path again; on Android 16 an app given a file by the
  system picker has no access to that path, only the descriptor.
- **Fix:** use the held descriptor through `dup` + `fdopen` (and measure it from the open file): `native/ob_ui/core/
  disc_check.cpp` (ob_ui test "a held descriptor verified", 118/118) and PS2Recomp patch 0084 (`iso9660.cpp`, test
  fails without). The earlier Odin runs used a disc in the app's own files dir, which never takes this path.
- **Take-away:** never reopen `/proc/self/fd/<n>` by name on Android; use the descriptor.

### 7. The navigation bar covered the launcher's right edge

- **Seen:** the same screenshot: Android's three-button bar over the Play button and the page's right side.
- **Cause:** the theme `Theme.Material.NoActionBar.Fullscreen` hides only the status bar.
- **Fix:** immersive mode (`Immersive.kt`: bars hidden, a swipe from the edge shows them for a moment), on the
  launcher, the game window (under the cutout), the in-game menu and the touch overlay; again whenever a window
  regains focus (the file picker and other dialogs bring the bars back).
- **Take-away:** every dialog window has its own system-bar state; hide the bars on each one.

### 8. Quit game left the game half-closed and the relaunch froze

- **Seen:** the developer (2026-10-27): after Quit game in the in-game menu "the game does not fully close", and the
  next launch froze.
- **Found by:** reading the quit path: Kotlin finished the activity and its task while the runner was told to quit;
  the runner reaches its `_Exit(0)` only after `run()` has stopped the game thread and closed the window. The launcher
  and the game shared one process, so anything left of a half-ended runner was still there when Play started the
  NativeActivity again in that process. The exact stuck step was not seen (no phone log of it).
- **Fix:** the game runs in its own process (`android:process=":game"`). Quit game starts the launcher (developer:
  back to the launch menu, not out of the app), tells the runner to quit, finishes the activity and ends the
  `:game` process itself if the runner has not after 1.5 s. Play first ends any `:game` process still alive, so every
  game starts fresh. The launcher's Quit closes the whole app (task, any game process, then its own process).
  "Skip the launcher" does not start the game again when the player has just quit it.
- **Take-away:** a NativeActivity whose native code keeps process-wide state belongs in a process of its own.

### 9. The saved master volume was ignored at game start

- **Seen:** the developer (2026-10-27, OnePlus 13): volume set to 0 in the launcher, the game still played sound
  until the dial was moved again in game.
- **Found by:** reading the order: `obui::prepare` (in the UI's launch, before the game) calls `host.setVolume` ->
  raylib `SetMasterVolume`; the runtime opens the audio device later (`PS2Runtime::initialize`, `InitAudioDevice`,
  ps2_runtime.cpp:1177). raylib's `SetMasterVolume` sets the miniaudio device's volume, which does not exist yet, and
  the new device starts at full volume.
- **Fix:** `ui_hook.cpp` keeps the volume until `IsAudioDeviceReady()` and applies it at the first host frame
  (`[ui] volume N% applied (audio device ready)`). Windows check: a hidden run with `[ui] volume = 0` logs
  "[ui] volume 0% applied" right after "AUDIO: Device initialized".
- **Take-away:** a setting applied "at start-up" must wait for the device it drives.

### 10. Game speed followed the frame rate (catch-up bursts)

- **Seen:** the developer (OnePlus 13): the frame rate swings between about 45 and above 60, and the game's speed
  with it (caught in a window by a zombie, then healed almost at once).
- **Found by:** reading the pacing: each VBlank's host deadline is the last one + 16 667 us (`EeScheduler.cpp`
  1933-1939), an absolute schedule; after a slow stretch the missed VBlanks are all due and run back to back, faster
  than 60 per second, until the schedule is caught up (no limit on the debt). The game's logic counts VBlanks, so it
  runs fast during the burst. The period is also 60.00 Hz, not NTSC's 59.94 (0.1 %).
- **Fix:** PS2Recomp patch 0085 (developer: "Do the pacing fix first"): a VBlank more than two periods late restarts the
  schedule from now (a slow stretch slows the game, as on a PS2, and is never made up); VBlank at 59.94 Hz. Test: after a
  300 ms guest stall, 21 VBlanks ran in the next 100 ms without the patch, at most 9 with it.
- **Still open:** the dips themselves (below 50 fps on the OnePlus 13): to be measured on the phone.

### 11. Dips on the OnePlus 13: one maxed game thread on cores the phone keeps slow

- **Seen:** the developer (2026-10-27): dips into the 40s with audio problems, at the start of J's Bar (characters
  moving, zombies at the window); full speed again in the first stairwell.
- **Found by:** two captures on the phone over wireless adb (developer's go): the runner's `[fps]` lines (logcat tag
  `ps2x`), the phone's thermal status, battery temperature and CPU clocks every 5 s, then `top -H` per thread once a
  second (an on-device loop, the game's `:game` process).
  - Capture 1 (03:09-03:12): 37-48 fps for 40 s (03:10:51-03:11:32); thermal status 0 throughout, battery 31.7-32.6 C;
    the 2.4 GHz cores at their top; the GS thread's own counter: about 1 600 records per vsync in that window against
    about 200 before (3.65 M vs 0.74 M records), with only 2 queue-ahead stalls (58 ms): the renderer kept up.
  - Capture 2 (03:21-03:24, J's Bar again): in every second under 56 fps `GameThread` (the EE: game code + the VU1
    recompiler) ran at 85-103 % of one core; `GSThread` 15-30 %; the Android UI thread 30-45 % while the touch
    controls were in use.
  - CPU limits (`/sys/devices/system/cpu/cpuN/cpufreq`, SM8750): hardware max 3.53 GHz (cpu0-5) and 4.32 GHz (cpu6-7);
    the phone's caps 3.07 and 3.51 GHz (its balanced mode); seen while slow: cpu0-5 about 2.4 GHz, cpu6 1.0-2.0 GHz.
- **Cause:** the game is bound by one thread (the EE), and Android's governor does not run that thread near the
  chip's speed: nothing tells it the app is a game with a 16.7 ms frame deadline. Under full speed the SPU2 makes
  sound slower than it plays, so the audio runs dry. Not heat, not the renderer.
  - Run 3 (03:27:40-03:30:19, the developer in OnePlus "gamer pro mode"): worse, 37-44 fps for 50 s (03:29:14-03:30:02,
    J's Bar again; the GS thread's counter again about 1 600 records per vsync); the per-thread sampler had already
    ended, so the clocks during it are not known. After it: thermal 0, battery 32 C, caps unchanged (3.07 / 3.51 GHz)
    with the game closed. Quit game at 03:30:19 left no game process (the quit fix works; the process that lingered
    after run 1 came from leaving without Quit game, and Play ended it).
  - Run 4, profiled (developer: "profiler first"; 03:39:13-03:41:48, gamer pro mode): `simpleperf record --app ... -p
    <game pid> -e cpu-clock -f 2000` from an on-phone script (the Wi-Fi link drops), 156 s, 296 214 samples, none
    lost; symbolized on the PC with the NDK's host simpleperf and the unstripped `libps2EntryRunner.so` (`--symfs`).
    The game's own clock (the `[fps]` lines) mapped onto the samples by the quit time; 38 seconds at 20-50 fps.
    `GameThread` busy 88 % of each slow second (66 % at full speed); its cores 0-5, median 2.4 GHz, never above the
    3.07 GHz cap: gamer pro mode did not raise it, and the thread never ran on cpu6-7 during a dip.
    Where its time went in the slow seconds (full-speed seconds in brackets): game code 18 % (16), EE timer
    bookkeeping + scheduler checkpoints 14 % (19; `PS2Memory::advanceEeTimers` alone 8-11 %), the IOP interpreter
    16 % (13), C++ exception unwinding 5 % (12; `libunwind::findUnwindSectionsByPhdr` + the linker's
    `dl_iterate_phdr`: something throws in the hot path), SPU2 mixing + time stretch 5 % (12), guest memory helpers
    6 % (8), movie decoding 5 % (7), PLT stubs 4 % (4), VU1 JIT 3 % (0), VIF1/GIF 4 % (0), the rest about 9 %.
- **Root cause:** the emulator's own overhead takes most of each frame on a core the phone runs at about 2.4 GHz; the
  game's code is under a fifth of it. J's Bar's opening adds geometry and sound work, and the thread goes over 100 %.
- **Next (proposed to the developer):** see the summary of options in the session; not built yet.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| (design) Classic stick = mostly tank | the left stick is directional in the Classic camera too | the developer, from another session | design T5, T13, T14 |
| (design) the aim pad may need a directional variant | aiming returns to tank-style aiming from either scheme | the developer | design T8 |
| First build: "0 native warnings" | true only for the files that recompiled; six deprecated `u8path` calls were latent | patch 0081 made `main.cpp` recompile | patch 0082, struggle 5 |
| (menu research) the map does not close with ✕ | it does; the 2-4 fps run lost the input | the full-speed run `menu3` | design section 8 |

## Still open

- Device test by the developer (the Odin or a phone): feel of the zones and sizes, Auto hiding, Edit layout, the call
  wheel (whether calls need R3 as well as the right stick), L2's role, the quick-turn timings.
- Section 5 of the design: the directional stick's "walk and turn" range and camera cuts; take the stick session's
  findings first.
- Modern camera layout (T7, T9) when that camera exists.

## Useful for other developers

- A second input source can join the game's pad without touching the runtime: merge it in `obui::filterPad`
  (`native/ob_ui/ui/settings_ui.cpp`) before the menu gate, so the menu's release rules cover it too.
- A never-focused dialog (`FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL`) over a NativeActivity takes touches while keys
  and gamepad events keep going to the native input queue.
- Kotlin unit tests run in `android.sh package` (`gradlew testDebugUnitTest assembleDebug`); the step prints
  "kotlin tests: n/n, kotlin warnings: n".
