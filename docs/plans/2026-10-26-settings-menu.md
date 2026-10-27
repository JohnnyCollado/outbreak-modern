# Settings menu: launcher and in-game overlay (plan)

- **Date:** 2026-10-26
- **Status:** built 2026-10-26 (report `reports/2026-10-26-settings-menu.md`): Windows ImGui and Android Kotlin +
  Compose, PS2Recomp 0077-0080. Decisions A11-A15 and the later changes in the report supersede parts of this plan.
- **Mockups:** shown in the Claude Code session of 2026-10-26 (settings tabs, launcher + overlay, disc verification).
- **Later addition:** a **Network** page (online play: player name, servers, hosting, saved logins, connection
  test), planned in `docs/plans/2026-10-26-online-play.md` section 3a (decision N5).

## 1. What it is

One settings menu, shown in two places:

1. **Launcher**: a screen in the game's window before the game starts. Pick the disc, optional texture pack and
   mods folder, change any setting, then Play.
2. **Overlay**: the same menu over the paused game, opened by a hotkey (keyboard) or a button combo (controller).
   Only settings that can change while the game runs appear here.

It covers: controller and keyboard bindings, Modern or Classic camera, display (window mode, resolution presets,
vsync), and the memory card save location.

## 2. Decisions

### Approved (developer, 2026-10-26)

| # | Decision |
|---|---|
| A1 | Tabs: Controls, Camera, Display, Memory cards. |
| A2 | Controls: rebind keyboard and controller, each in its own column. |
| A3 | Camera: switch between Modern camera and Classic camera. |
| A4 | Display: windowed / borderless / fullscreen, resolution presets, vsync. |
| A5 | Memory cards: choose the save location. |
| A6 | The menu shows before launch (launcher) and can be opened in game with a hotkey and a controller combo, chosen so they do not interfere with gameplay. |
| A7 | The game disc is **required** and **verified** before Play is enabled. |
| A8 | Texture pack and mods folder are **optional**. |
| A9 | The launcher accepts only `.iso` files. An extracted folder or a bare boot ELF stays possible through `outbreak.ini` only. |
| A10 | Android only: a Graphics driver section where the player picks the Vulkan driver the game draws with (section 5.7). |
| A11 | The menu is 100% controller-navigable (developer, 2026-10-26, after the first build): an in-app file/folder browser replaces Windows' own dialogs (A open / pick, B up, Y use this folder), L1/R1 switch pages, the page list and page share one navigation scope (B lands back on the list), focus on the current page when the menu opens, Start = Play in the launcher, a binding capture ignores the press that started it and times out after 6 s, button hints in the footer. |
| A12 | The Android APK is built with Gradle (Gradle 9.7.1, AGP 9.4.0, as PlayFieldPortal; JDK 17). The C++ stays in CMake; Gradle builds the Java and packages the `.so` CMake made. Replaces "still no Gradle" in section 5.8. |
| A14 | (developer, 2026-10-26, after trying the first build: "horrible") Android: the launcher and the in-game menu are Kotlin + Jetpack Compose, with PlayFieldPortal's controller handling copied and adapted (`GamepadInputHandler`: dead zone with hysteresis, accelerating repeat, duplicate D-pad suppression; `SettingsScaffold`: explicit row focus, footer prompts), credited. SAF picks and imports in Kotlin. The disc check stays the C++ core (JNI). The in-game menu is a Compose dialog window over the game (the game held through JNI). |
| A15 | Windows: ImGui stays (in-process, pauses the game), restyled (bundled font, icons, cards, clear Play; mockup 2026-10-26), mouse first, with natural controller navigation for Windows handhelds: rows (up/down), values (left/right), A act, B back to the page list, L1/R1 pages, accelerating repeat and stick hysteresis as PlayFieldPortal; prompts and the focus highlight only after controller input; Browse = Windows' dialog with the mouse, the in-app browser with a controller. |
| A13 | Android texture packs, mods and drivers import from a folder (system folder picker, copied by Java on a background thread) or a `.zip`, both with a progress bar and Cancel; replacing an existing pack asks first. Replaces question 5's "zip only". |

### Approved proposals (developer, 2026-10-26: "Approve P1-P12 as written")

Kept under their P numbers, which the sections below refer to.

| # | Decision | Why |
|---|---|---|
| P1 | Keyboard: **F1** and **Esc** open/close the overlay; both are reserved (the binding screen refuses them). | No default game binding uses an F-key or Esc (`native/input.ini.example:16-34`); Esc no longer quits (`SetExitKey(KEY_NULL)`, patch 0035). |
| P2 | ~~Controller: hold Back + Start for 1 s, with hold-back (section 5.3).~~ **Changed 2026-10-26 (developer): Start/Select go straight to the game (no hold-back). Windows: Back (View) + Start held together for 2 s opens the menu (5 s at first; shortened 2026-10-26 by the developer), with a gear ring filling top left (below the FPS readout). Android: no combo; the Back button opens the menu. F1/Esc everywhere.** | The 100 ms hold-back felt laggy in play; on Windows the long hold keeps the combo from firing by accident. |
| P3 | Xbox Guide button as an opt-in extra, off by default. | Probably not readable through standard XInput, and Steam / Game Bar take it. Unverified. |
| P4 | Android: short press of the system Back button opens the overlay. | Android convention. How the Odin's Back button reaches raylib is unverified. |
| P5 | The game pauses fully while the overlay is open (game thread, sound, timers); all buttons released on resume. | No lost time or stuck buttons. |
| P6 | Separate binding layouts per camera mode: Modern binds actions (Aim, Fire, Run...), Classic binds PS2 buttons. | The Modern camera reassigns buttons (`legacy/README.md` "Playing"). |
| P7 | Extras: stick dead zone, 16:9 switch, smooth/sharp scaling, show FPS, memory card backup, Audio tab (volume, device). | Most map to existing settings (section 4). |
| P8 | Launcher option "Show this screen at start-up" (on by default). | Lets players boot straight into the game. |
| P9 | A click on the disabled Play button jumps to the disc field and repeats the reason. | A greyed button with no reason confuses. |
| P10 | Graphics driver: an "Add driver" button that imports a driver package (.zip or folder) the player already has, through Android's file picker. Never downloads one. | Today the player copies the folder over USB/adb into `files/drivers/` by hand (`docs/ops/android.md` "GPU driver" steps 1-2). |
| P11 | Graphics driver: show what actually ran last time (driver name, version) and, after a fall-back, the reason in plain words. | The game always starts, but a fall-back is only visible in logcat today. |
| P12 | Graphics driver: an "Advanced" fold with the existing self-read choice (auto / copy / barrier / fetch). | It exists only as an ini key for testing phones and drivers (`outbreak.ini.example`, `gs_self_read`). |

## 3. The disc check (A7, A9)

Play is enabled only when all three pass, in order. A failure stops the later checks and says what to do.

| Check | How | Failure message (example) |
|---|---|---|
| 1. The file is a readable disc image | ISO 9660 primary volume descriptor at sector 16 (the reader already exists: `ps2x/iop/iso9660.h`, patch 0043) | "This file can't be read as a disc image. It may be incomplete. Dump or copy it again." |
| 2. The disc boots `SLPM_654.28` | Read `SYSTEM.CNF`, its `BOOT2` line (`cdrom0:\SLPM_654.28;1`, patch 0043 README row) | "This disc is a different game (SLUS-20228)." / "This is another release of Outbreak (SLUS-20765)." |
| 3. The game code matches this build | A hash of the boot ELF compared with a hash stored in the build at recompile time | "This copy of Outbreak differs from the one this build was made from." |

- Why check 3 matters: the game's code is translated to native code ahead of time from one specific ELF
  (`PROJECT_CONTEXT.md:62`). A different ELF passes check 2 and then misbehaves.
- Cost: only small files are read (the ELF is a few MB), so it should finish in well under a second. Unmeasured.
- **Found (step 1 research, 2026-10-26): the English patch changes the ELF**, so check 3 alone tells the releases apart.
  - `game_files/SLPM_654.28` (the disc this build is made from: `SYSTEM.CNF` `VER = 1.04`): 1,294,104 bytes, SHA-256
    `975dd77d1e1c81f0d8f5f072b35c487c5ad6befbb99b339641a4c9573a334480`, PCSX2 CRC `32088394`. The PCSX2 CRC is
    the XOR of every 32-bit word of the ELF; recomputed here, it matches the pnach file name.
  - PCSX2 knows three ELFs for SLPM-65428 (`ps2_emu/resources/patches.zip`): `03C8F393` "BioHazard Outbreak
    [NTSC-J]" (the original Japanese disc), and `198EFDC1` and `32088394`, both "(ENGLISH PATCHED)". So a second
    English patch release exists. That `32088394` is v2.1 rests on our disc's file name ("English v2.1",
    `outbreak.ini.example`), not on a published list.
  - The build stores the SHA-256 (the real check) and the CRC (for naming what was found). A failing check 3 names
    the release when the CRC is known: `03C8F393` = "the original Japanese disc, without the English patch";
    `198EFDC1` = "a different English patch release; this build needs v2.1"; anything else = "an unknown version".
  - Consequence for question 2 (section 7): the unpatched Japanese disc never passes check 3, so its "allow with a
    note" case does not arise with this build. It would only matter for a build made from the Japanese disc.
  - Redump (the disc-preservation database) lists the Japanese v1.04 disc as 4,698,767,360 bytes, MD5
    `f79fbe8fd8229eca319c5d609d350cc2` (`ps2_emu/resources/RedumpDatabase.yaml`). It isn't needed for the check,
    which reads only the ELF (1.3 MB).
- The result is remembered (path, size, modified time) so the check does not rerun at every launch unless the file changed.

Texture pack and mods folder (A8): never block Play. If the folder has no `SLPM-65428` content, a note says so.

## 4. Settings: where each one lives and when it applies

"Launch" = launcher only, applies on the next start. "Live" = also in the overlay, applies at once.

| Tab | Setting | Stored as | When | Built on |
|---|---|---|---|---|
| Game | Disc (`.iso`) | `outbreak.ini` `game` | Launch | patch 0043 |
| Game | Texture pack (optional) | `textures` | Launch | 0043, Vulkan renderer only |
| Game | Mods folder (optional) | `mods` | Launch | 0050 |
| Controls | Keyboard / controller bindings | `input.ini` | Live | 0038, 0040 |
| Controls | Stick dead zone (P7) | `input.ini` `[gamepad] deadzone*` | Live | 0040 |
| Camera | Modern / Classic | new key | Live (if the camera code allows) | Modern camera not yet in the native build |
| Camera | Sensitivity, invert, quick-switch combo | new keys | Live | same |
| Display | Window mode | new key `window_mode` | Live | raylib window calls (section 5.4) |
| Display | Output resolution (window size; monitor mode in fullscreen) | `window` + new key | Live | 0050 |
| Display | Render resolution preset (Native, 720p 2x, 1080p 3x, 1440p 4x, 4K 6x) | `upscale` | Live if PCSX2's GS can resize its targets in place, else Launch; to check | 0050, Vulkan only |
| Display | Vsync | new key | Live | section 5.4 |
| Display | 16:9 (P7) | new key, replaces the hand-made pnach | Live | 0050 pnach writes |
| Display | Smooth / sharp scaling (P7) | `scaling` | Live | existing |
| Display | Show FPS (P7) | new key | Live | frame counter, patch 0035 |
| Memory cards | Save folder | `saves` | Launch | patch 0072 (committed `a35bc7b`) |
| Memory cards | Backup (P7) | new key | Launch | new |
| Audio (P7) | Volume, device | new keys | Live | SPU2 stream, patch 0056 |
| Advanced | Renderer, VU1 engine | `vu1`, renderer key | Launch | existing |
| Graphics driver (Android only) | System or a custom driver; Add driver (P10); last-run status (P11); self-read (P12) | `gpu_driver`, `gs_self_read` | Launch | patch 0046, PCSX2 patch 0002 |

Writing the files back: the menu rewrites only the lines it changed and keeps comments and unknown lines, so a
hand-edited `outbreak.ini` survives. Bad values keep today's rule: logged once and ignored.

## 5. How it works (technical detail)

### 5.1 Where the code goes

- The menu is a player feature, not a diagnostic, so it does not go in `ob_diag`
  (`docs/ARCHITECTURE.md` section 4).
- Proposed: a new `native/ob_ui/` library (menu screens, ini read/write, disc check, combo detector) plus thin hook
  points in the runtime as numbered PS2Recomp patches, each with a test that fails without it.
- Drawing: Dear ImGui. PCSX2's GS already makes an ImGui context and renders it on every present
  (`docs/ops/gs.md`, `ui_stubs.cpp` / `GSDeviceVK::RenderImGui`). ImGui has built-in controller navigation.
  - **Found (step 1):** the GS can present with no game frame. `GSPresentCurrentFrame()` (PCSX2 `GS/GS.cpp:541`,
    used by PCSX2 to redraw while paused, `MTGS.cpp:962-978`) skips the picture when there is none
    (`GSRenderer.cpp:986-1008`), and every present ends in `EndPresentFrame` -> `GSDeviceVK::EndPresent` ->
    `RenderImGui` (`GSRenderer.cpp:570-578`, `GSDeviceVK.cpp:2554`). So the launcher and the overlay can both be
    ImGui on the GS: one toolkit.
  - Today our adapter opens the GS lazily, at the game's first GS packet (`EnsureOpen`,
    `native/pcsx2_gs/adapter/pcsx2_side.cpp:267`). The launcher needs it opened at window attach instead.
  - The renderer, upscale and texture folder are inputs to `GSopen`. If the launcher changes them, Play applies them
    with PCSX2's `GSreopen` (`GS/GS.cpp:243`) before the game starts. The Android driver is the exception: the
    Vulkan library is loaded once per process (libadrenotools), so a driver change needs an app restart, and the
    launcher says so.
- Closed overlay cost: one key/combo check per frame, nothing else, so the game's speed is unchanged.

### 5.2 Pausing

- Stop the game at a vsync: the EE scheduler, the IOP, and the SPU2 audio stream; the GS thread keeps presenting the
  last frame with the overlay on top.
- On resume: every button reads as released for one frame, then live input returns.
- **Found (step 1):** the threads involved, in `C:\ob\up` `ps2xRuntime`:
  - The game runs on one host thread (`gameThread`, `src/lib/ps2_runtime.cpp:2872`). Guest time (VBlank, alarms,
    EE timers) comes from `EeScheduler` (`include/runtime/ee_scheduler.h`; wait in `EeScheduler::waitForEvent`,
    `src/lib/Kernel/EeScheduler.cpp` ~2060-2100), and the IOP and SPU2 run at event cycles folded into the same
    next-event checks (patches 0042/0049/0056).
  - The main thread runs the raylib loop: window, input, resize forwarding (`ps2_runtime.cpp` ~2894-2960).
  - So the pause is a **hold in `EeScheduler`**: while held, it dispatches no event, and the game thread blocks at its
    next wait, within a frame because the game waits for VBlank every frame. The main thread keeps running, so the
    overlay's input still works.
  - While held, the main loop asks the GS thread for `GSPresentCurrentFrame()` each frame (through the ring, as other
    GS calls), so the last picture plus the overlay keep drawing.
  - Sound: the raylib audio stream pulls from SPU2 and would run dry, so the overlay pauses the stream
    (`PauseAudioStream`, raylib 5.5) and resumes it with the game.
  - On resume the scheduler's host deadlines are moved forward by the paused time, so the game does not "catch up"
    (fire a burst of VBlanks). The exact field is to read when building; pacing already keeps an epoch (patch 0051).
  - This is a runtime change: a numbered PS2Recomp patch with a test (scheduler held -> no event fires; released ->
    deadlines shifted, guest ticks unchanged).
- Hotkey and combo detection live on the main thread, where raylib input is read; the hold-back (5.3) goes in the
  pad read that feeds the game (`PSPadBackend`, patches 0020/0038).
- Online play (milestone M7) cannot pause. The overlay then only stops sending input.

### 5.3 The controller combo (P2) - changed 2026-10-26

- **No hold-back:** Start and Select reach the game on their first read (the 100 ms hold-back felt laggy).
- **Windows:** Back (View) + Start held together for 5 s opens (and closes) the menu. While both are held a ring fills
  clockwise around a gear (Segoe Fluent Icons U+E713) top left, below the FPS readout when it is on, sized from the
  display; it disappears when either is let go. `obui::MenuTrigger` `Config{5.0, 0.0, false, true}`;
  `comboProgress(now)` -> `ob_gs_set_hold_progress` each host frame; drawn on the GS thread in the present
  (`pcsx2_side.cpp` `ExecVsync`, as the FPS readout).
- **Android:** no combo (`Config{5.0, 0.0, false, false}`); the Back button (system or the Odin's) opens the menu.
- The core keeps the hold-back mechanism (default on) and its tests; the menu turns it off.

### 5.4 Display

- Window modes (found, step 1): the runtime pins raylib **5.5** (`C:\ob\up\ps2xRuntime\CMakeLists.txt:89`), which
  has `ToggleFullscreen` ("resizes monitor to match window resolution"), `ToggleBorderlessWindowed` ("resizes window
  to match monitor resolution"), `SetWindowMonitor`, `GetCurrentMonitor` and `GetMonitorWidth/Height`
  (`raylib.h:981-1004`). Size changes already reach the GS through `windowResized` (patch 0024). Borderless is the
  recommended full-screen option. "Fullscreen" changes the monitor's resolution to the chosen preset.
  - **To try when building:** the GS draws with Vulkan into a child window of raylib's OpenGL window. Whether
    `ToggleFullscreen`'s monitor mode switch behaves with that child on Windows was not tested.
- Vsync (found, step 1): our adapter opens the GS with `GSVSyncMode::Mailbox` when the window is visible, else
  `Disabled` (`native/pcsx2_gs/adapter/pcsx2_side.cpp:293`). PCSX2 has three modes (`Config.h:283`: Disabled,
  FIFO, Mailbox) and switches live with `GSSetVSyncMode(mode, allow_present_throttle)` (`GS/GS.cpp:589`), run on the
  GS thread. The menu offers:
  - **On** = Mailbox, today's default: no tearing, little delay.
  - **On (strict)** = FIFO: classic vsync, steadier on some screens, a frame more delay.
  - **Off** = Disabled: may tear.
  - Mailbox is not available on every driver. PCSX2 then falls back and logs the choice to `pcsx2-gs.log`. Whether
    the menu should show the fallback is to decide when building.
- Render resolution needs the Vulkan renderer (`outbreak.ini.example`, `upscale`). On the software renderer the
  preset list shows why it is unavailable.

### 5.5 Memory cards

Built on patches 0071-0072, committed in `a35bc7b` (2026-10-26; `reports/2026-10-26-memory-cards.md`): the game
formats, saves and loads through PCSX2 `.ps2` cards. Not yet verified there: Android, and a card round trip with
PCSX2.

What exists (`native/outbreak.ini.example` `saves`, patch 0072 header):
- The folder holds PCSX2's own 8 MB card files: `Mcd001.ps2` = slot 1, `Mcd002.ps2` = slot 2 (only if placed there).
  The same files PCSX2 keeps, so cards copy between PCSX2 and this game either way (with PCSX2 closed: a card in use
  by one cannot be opened by the other).
- Default folder: `memcards` beside the exe (Windows), or in the app's files dir (Android).
- Read once at start-up, so a change is Launch-only and takes effect on the next start.
- A missing `Mcd001.ps2` is created blank in the new folder.
- `PS2X_SAVES=<folder>` wins over `saves`. Only scripted runs (`ob.sh boot`) set it, so players are not affected; the
  menu still shows a note when it is set ("this run's cards come from PS2X_SAVES"), so a developer is not confused.

What the tab does:
- Save folder with Browse (Windows) or the picker (Android, section 5.8). Changing it asks: move, copy or leave the
  existing cards. Launcher only, never while the game runs.
- Wording for a new or blank card (the game's behaviour, from the memory-card work): a new player sees **no**
  memory-card screen at boot. With a card in the slot, even a blank one, the game skips the "no data" screen and
  offers to format the card the first time it saves. So the tab says "A new card is created here. The game asks to
  format it the first time you save", not "you will be asked at start-up".
- Per card: present or not, and whether it holds File #1 data (its system save is the `BISLPM-65428NETBIO`
  folder). `tools/ob/mcd.py info` already reads a card's directory in Python; the menu needs the same reading in
  C++ (or PCSX2's own card code in `native/pcsx2_sio2`; to choose in step 1).
- Import from PCSX2 (follows from the shared format; small): pick a `.ps2` file, copy it in as `Mcd001.ps2` after
  asking whether to replace the current one.
- Backup (P7): copy `Mcd001.ps2` to a dated file at each launch, keep the last few.

### 5.6 Android

- Same menu; the launcher fits a landscape handheld screen and works with the built-in controller only.
- Disc picking: Android's file picker through a small Java bridge (section 5.8). The one `.iso` in the app's files
  dir keeps working as today when nothing was picked.

### 5.7 Graphics driver (Android only, A10)

What the player sees: a "Graphics driver" section in the launcher, present only in the Android build (the Windows
build has no such section; desktop ignores `gpu_driver` today and logs that it did).

- A list: "System driver (recommended)" first, then every driver package found in `files/drivers/<name>/`, each with
  the name and version from its `meta.json`.
- Packages that cannot work on this phone stay in the list, greyed, with the reason ("needs an Adreno GPU",
  "needs Android API 33", "meta.json missing"). The reasons are the existing checks in `obgpu::CustomDriverBlocker`
  (`native/pcsx2_gs/adapter/gpu_driver*.cpp`, host-tested in `ob_gpu_driver_tests`), so the menu shows the same
  verdict the game would reach. Only the checks that need no Vulkan probe can run in the menu; the probe itself
  runs at start-up as today.
- Launch only: the driver is loaded once before the GS opens (`docs/ops/android.md` "How it works"), so a change
  applies on the next start, and the overlay does not offer it.
- Safety stays as built: a driver that fails falls back to the system driver and the game still starts. With P11 the
  launcher then says so on the next visit, instead of only logcat.
- No driver is bundled or downloaded by the app (`docs/ops/android.md`: "players bring their own package"). P10 only
  imports a package the player already has.
- Stored as `gpu_driver = system | <name>` in `outbreak.ini` (patch 0046), so hand edits and the menu agree.
- File picking (P10, and the disc on Android): section 5.8.

### 5.8 Android file picking: Storage Access Framework through a small Java bridge

Android's file picker, the Storage Access Framework (SAF), lives in the Java framework (`Intent`,
`ContentResolver`, `DocumentFile`) and cannot be driven from C++ alone. The approach (developer, 2026-10-26): pick
in Java, open the file there, and hand C++ a plain file descriptor (an open-file number), so the C++ side keeps
using ordinary file reads.

Today the app has no Java at all: `native/android/AndroidManifest.xml` has `android:hasCode="false"` and the stock
`android.app.NativeActivity`, and `tools/ob/android.sh` builds the APK with aapt2 only (no Gradle, no dex step).
So this adds:

1. **One small Java class**, `OutbreakActivity extends NativeActivity` (manifest: `hasCode="true"`, activity name
   changed, `android.app.lib_name` kept). It only does picking; the game still starts in C++ as today.
2. **C++ asks, Java picks:** the launcher calls a Java method through JNI (the Java Native Interface; the
   `JavaVM` and activity object come from `android_app->activity` in the native app glue raylib already uses). Java
   starts `ACTION_OPEN_DOCUMENT` (one file: `.iso`, `.zip`) or `ACTION_OPEN_DOCUMENT_TREE` (a folder).
3. **Java answers, C++ reads:** `onActivityResult` (which NativeActivity does not forward to C++, hence the
   subclass) opens the chosen file with `ContentResolver.openFileDescriptor(uri, "r")`, takes ownership of the
   number with `ParcelFileDescriptor.detachFd()`, and calls a native method with it. C++ then reads it like any file
   (`fdopen` / `read`), and closes it when done.
4. **Remember the disc:** `takePersistableUriPermission` keeps access to the chosen `.iso` across restarts; at each
   start Java reopens the saved URI and passes a fresh descriptor, so the player picks the disc once.

Per use:

| What | Picker | What C++ does with the descriptor |
|---|---|---|
| Game disc (`.iso`) | one file, permission kept | Keeps it open as the disc for the whole session and passes `/proc/self/fd/<n>` as the `game` path. The disc check (section 3) runs on it. See the research note below. |
| Driver package (`.zip`, P10) | one file, once | Unpacks into `files/drivers/<name>/`, then the existing driver code takes over (it already copies to internal storage, because Android does not load libraries from shared storage). |
| Texture pack, mods | one `.zip`, once | Copies it into the app's files folder (decided, section 7 question 5). Folder access through SAF gives one URI per file, so reading a pack of thousands of files in place would be slow. |

Research note, the disc through a descriptor (step 1, 2026-10-26): patch 0043's `IsoImage` takes a path.
`probe` checks `is_regular_file` and then opens it, `open` uses `fopen` / `_wfopen` "rb" with a 64 KiB buffer, and
`shared()` caches one open image per normalized path (`upstream/ps2recomp/0043-*.patch`, lines ~310-505). A
`/proc/self/fd/<n>` path fits all three: on Linux, opening it reopens the file behind the descriptor, and
`is_regular_file` follows it to that file. So **no PS2Recomp change is expected**, as long as Java keeps the
descriptor open for the whole session. The limit: this only works when the picked document is a real file (the
device's own storage or an SD card). A cloud document (for example Google Drive) can come back as a pipe, and the
ISO reader needs to seek. Then the launcher says "copy the disc to this device first". This is from how Linux
`/proc` works and was not tried on the Odin; it gets checked on the device in step 9, and an "open from descriptor"
entry (a new patch) is the fallback.

Build changes, sizes unmeasured:
- `tools/ob/android.sh` gains `javac` (against `android.jar`, already located at line 36) and `d8` (Android
  build-tools) to make `classes.dex`, added to the APK. Still no Gradle.
- **Found (step 1): nothing to install.**
  - The build already uses a JDK: `OB_JDK` = Android Studio's `jbr` (`tools/ob/android.sh:32`; today it is used for
    `keytool` and `apksigner`). Its `javac` is 25.0.3.
  - `d8` is in the build-tools the script already uses (`35.0.0`, `android.sh:31`; 34-37 are installed).
  - Other JDKs on this machine: Microsoft OpenJDK 17.0.19 (`JAVA_HOME`) and 21.0.12 (first on `PATH`). The
    PlayFieldPortal repo (a separate project of the developer's) builds with that JDK 17 (`JAVA_HOME`; its CI uses
    JDK 17).
  - Compile with `javac --release 11`-level output (`-source 11 -target 11 -bootclasspath android.jar`): d8 35
    may not read class files from JDK 25's newest format. To confirm on the first build.
- The Java is compiled once and cached like the rest of the Android build, so a C++-only change does not rebuild it.

Sources: developer's note (2026-10-26) on SAF + JNI + descriptors; `native/android/AndroidManifest.xml`;
`tools/ob/android.sh`. From memory, not opened: Android API docs for `ACTION_OPEN_DOCUMENT`,
`ParcelFileDescriptor.detachFd`, `takePersistableUriPermission`, and that `NativeActivity` does not pass
`onActivityResult` to native code.

## 6. Build order

Each step ends with tests green, the gate 3/3, J's Bar speed unchanged, and a struggle report entry.

1. **Research** (no code). **Done 2026-10-26.** Results are in sections 3, 5.1, 5.2, 5.4 and 5.8. Left for the
   building steps: fullscreen with the Vulkan child window (5.4), the scheduler's deadline field (5.2), `/proc/self/fd`
   on the Odin (5.8), and C++ reading of a card's save list (5.5: port `mcd.py info`; PCSX2's `MemoryCardFile`
   does raw pages only, unverified).
2. **ini read/write** that keeps comments, with tests.
3. **Disc check** (A7, A9), tested on a small synthetic image like patch 0043's test, plus the real disc by hand.
4. **Launcher**: Game tab and Play, then the other tabs for Launch settings.
5. **Pause, hotkey and combo** (P1, P2, P5), with tests for the combo detector and button release.
6. **Overlay** with the Live settings: Controls, Display, Audio.
7. **Memory cards tab** (unblocked: patch 0072 committed in `a35bc7b`).
8. **Camera tab**, after the Modern camera is in the native build.
9. **Android** pass on the Odin (each device step asked for first): the Java picker bridge (5.8) first, then the Graphics driver section (5.7):
   test with the system driver, a working Turnip package, and a deliberately broken package to see the fall-back
   reason shown.

## 7. Open questions

1. (Answered 2026-10-26: P1-P12 approved.)
2. (Answered 2026-10-26: allow it, with a note.) If the unpatched Japanese disc passes the code check, Play stays
   enabled and the launcher notes that the text will be Japanese and that this build is tested on English patch
   v2.1. If the ELFs differ, the code check rejects it anyway. Research (section 3): the ELFs do differ
   (`03C8F393` vs `32088394`), so with this build the unpatched disc is rejected with a message naming it.
3. (Answered 2026-10-26: show it.) Until the Modern camera is in the native build, the Camera tab shows Classic
   selected and Modern camera greyed out as "coming later"; its options (sensitivity, invert, quick switch) are
   hidden until then.
4. (Answered 2026-10-26: one disc.) One Game disc field, stored as `outbreak.ini` `game`; choosing another
   replaces it.
5. (Answered 2026-10-26: import a `.zip`.) On Android, texture packs and mods are picked as a `.zip` and copied
   into the app's files folder; no read-in-place of picked folders. A folder already placed there by hand (as the
   Odin's `files/textures` today) keeps working.

All open questions are answered.

## 8. Sources

- `native/outbreak.ini.example`: existing settings and their rules (read once at start-up, Vulkan-only upscale).
- `native/input.ini.example`, `docs/ops/boot.md:66-90`: default bindings, dead zones.
- `upstream/ps2recomp/README.md`: patches 0024, 0035, 0038, 0040, 0043, 0050, 0051, 0056, 0071, 0072.
- `docs/ops/gs.md`: ImGui context in the GS, host stubs.
- `docs/ARCHITECTURE.md` section 4: engine vs diagnostics.
- `reports/2026-10-26-memory-cards.md`, commit `a35bc7b`: memory cards through PCSX2's SIO2 (patches 0071-0076).
- Memory-card session's hand-off notes (2026-10-26): restart needed for `saves`, no boot card screen, `PS2X_SAVES` wins.
- `tools/ob/mcd.py`: `.ps2` card make / format / info.
- `legacy/README.md`: Modern camera controls and its Select + R3 switch.
- `docs/ops/android.md` "GPU driver (system or custom)": driver folder layout, fall-back checks, no bundled drivers.
- `PROJECT_CONTEXT.md:62`: code recompiled ahead of time from the disc.
- Step 1 research (2026-10-26, read only): `ps2_emu/resources/patches.zip` (SLPM-65428 pnach CRCs and titles),
  `ps2_emu/resources/RedumpDatabase.yaml`, `game_files/SLPM_654.28` and `SYSTEM.CNF`; PCSX2 v2.9.134 source in
  `C:\ob\pcsx2` (`GS/GS.cpp`, `GS/Renderers/Common/GSRenderer.cpp`, `MTGS.cpp`, `Config.h`, `GSDeviceVK.cpp`);
  PS2Recomp in `C:\ob\up` (`ps2xRuntime/src/lib/ps2_runtime.cpp`, `Kernel/EeScheduler.cpp`, `CMakeLists.txt`);
  raylib 5.5 `C:\ob\g\_deps\raylib-src\src\raylib.h`; `tools/ob/android.sh`; PlayFieldPortal `gradle.properties`,
  `.github/workflows/unit-tests.yml`.
- From memory, not opened: RetroArch's menu combos (Start+Select, hold Start); XInput not reporting Guide; raylib 5
  window-mode calls.
