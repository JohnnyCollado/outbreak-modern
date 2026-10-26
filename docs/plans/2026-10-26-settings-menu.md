# Settings menu: launcher and in-game overlay (plan)

- **Date:** 2026-10-26
- **Status:** plan only, nothing built. All decisions A1-A10 and P1-P12 approved, and the open questions in
  section 7 answered (2026-10-26). Ready for step 1 (research) when the developer says go.
- **Mockups:** shown in the Claude Code session of 2026-10-26 (settings tabs, launcher + overlay, disc verification).

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

### Approved proposals (developer, 2026-10-26: "Approve P1-P12 as written")

Kept under their P numbers, which the sections below refer to.

| # | Decision | Why |
|---|---|---|
| P1 | Keyboard: **F1** and **Esc** open/close the overlay; both are reserved (the binding screen refuses them). | No default game binding uses an F-key or Esc (`native/input.ini.example:16-34`); Esc no longer quits (`SetExitKey(KEY_NULL)`, patch 0035). |
| P2 | Controller: **hold Back + Start for 1 s**, with hold-back (section 5.3). | The game uses every PS2 button, so only a held combo is free. Different from the Modern camera's own Select + R3 (`legacy/README.md:32`). |
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
- **Unknown:** no ELF hash is recorded in the repo yet. `32088394` (PCSX2's CRC of the ELF, in the pnach name) may
  belong to the English v2.1 disc or to the Japanese original; unverified. The build will record its own hash.
- **Unknown:** if the English patch leaves the ELF unchanged, check 3 cannot tell the patched disc from the
  original Japanese one. Then a fourth check on one patched data file is needed. To find out in step 1.
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
| Memory cards | Save folder | `saves` | Launch | patch 0072 (in progress) |
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
  - **Unknown:** whether the GS device can present before any guest frame exists (the launcher runs before the
    game). If not, the launcher draws with raylib (and its raygui widgets) before the GS attaches. Step 1 decides.
- Closed overlay cost: one key/combo check per frame, nothing else, so the game's speed is unchanged.

### 5.2 Pausing

- Stop the game at a vsync: the EE scheduler, the IOP, and the SPU2 audio stream; the GS thread keeps presenting the
  last frame with the overlay on top.
- On resume: every button reads as released for one frame, then live input returns.
- **Unknown:** the cleanest stop point across the game, IOP and timer threads. Pacing (patch 0051) and the vsync
  tick are the likely places; to research in step 1.
- Online play (milestone M7) cannot pause. The overlay then only stops sending input.

### 5.3 The controller combo with hold-back (P2)

- When Back or Start goes down, the runner holds it back from the game for up to ~6 frames (100 ms).
- If the other button arrives in that time and both stay down 1 s: the overlay opens and the game never sees either.
- If not: the press is passed to the game late, which a player should not notice. To check in play.

### 5.4 Display

- Window modes: raylib's fullscreen and borderless-window calls (from memory: `ToggleFullscreen`,
  `ToggleBorderlessWindowed` in raylib 5; unverified against the pinned raylib). Size changes already reach the GS
  through `windowResized` (patch 0024).
- Vsync: the picture is presented by PCSX2's Vulkan device (`gs_stubs.cpp`: `GetPreferredRenderer` = VK), so vsync is
  its present mode; today host settings are PCSX2 defaults (`host_stubs.cpp`). The exact switch is unverified.
- Render resolution needs the Vulkan renderer (`outbreak.ini.example`, `upscale`). On the software renderer the
  preset list shows why it is unavailable.

### 5.5 Memory cards

- Built on patch 0072: cards are PCSX2 `.ps2` files in the `saves` folder, default `<settings folder>/memcards`.
  That work is in progress (`reports/2026-10-26-memory-cards.md`); this menu waits for it to land.
- Changing the folder asks: move, copy or leave the existing cards. Launcher only, never while the game is running.
- Shows each card's size and whether it holds a File #1 save (reading the card's directory; to design).

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
| Game disc (`.iso`) | one file, permission kept | Keeps it open as the disc for the whole session. Patch 0043's ISO reader opens a path today; it needs an "open from descriptor" entry (a new PS2Recomp patch with a test), or `/proc/self/fd/<n>` as the path (simpler; works on Linux-based Android, unverified for this case). The disc check (section 3) runs on it. |
| Driver package (`.zip`, P10) | one file, once | Unpacks into `files/drivers/<name>/`, then the existing driver code takes over (it already copies to internal storage, because Android does not load libraries from shared storage). |
| Texture pack, mods | one `.zip`, once | Copies it into the app's files folder (decided, section 7 question 5). Folder access through SAF gives one URI per file, so reading a pack of thousands of files in place would be slow. |

Build changes, sizes unmeasured:
- `tools/ob/android.sh` gains `javac` (against `android.jar`, already located at line 36) and `d8` (Android
  build-tools) to make `classes.dex`, added to the APK. Still no Gradle. Needs a JDK on the build machine: whether one
  is installed is unchecked (installing one would be asked first).
- The Java is compiled once and cached like the rest of the Android build, so a C++-only change does not rebuild it.

Sources: developer's note (2026-10-26) on SAF + JNI + descriptors; `native/android/AndroidManifest.xml`;
`tools/ob/android.sh`. From memory, not opened: Android API docs for `ACTION_OPEN_DOCUMENT`,
`ParcelFileDescriptor.detachFd`, `takePersistableUriPermission`, and that `NativeActivity` does not pass
`onActivityResult` to native code.

## 6. Build order

Each step ends with tests green, the gate 3/3, J's Bar speed unchanged, and a struggle report entry.

1. **Research** (no code): GS present before the first guest frame; the pause point; vsync switch; the ELF hash on
   the English and Japanese discs; raylib window calls in the pinned version; for Android, a JDK on the build
   machine, and whether the ISO reader can use `/proc/self/fd/<n>` or needs a descriptor entry (section 5.8).
2. **ini read/write** that keeps comments, with tests.
3. **Disc check** (A7, A9), tested on a small synthetic image like patch 0043's test, plus the real disc by hand.
4. **Launcher**: Game tab and Play, then the other tabs for Launch settings.
5. **Pause, hotkey and combo** (P1, P2, P5), with tests for the combo detector and button release.
6. **Overlay** with the Live settings: Controls, Display, Audio.
7. **Memory cards tab**, after patch 0072 lands.
8. **Camera tab**, after the Modern camera is in the native build.
9. **Android** pass on the Odin (each device step asked for first): the Java picker bridge (5.8) first, then the Graphics driver section (5.7):
   test with the system driver, a working Turnip package, and a deliberately broken package to see the fall-back
   reason shown.

## 7. Open questions

1. (Answered 2026-10-26: P1-P12 approved.)
2. (Answered 2026-10-26: allow it, with a note.) If the unpatched Japanese disc passes the code check, Play stays
   enabled and the launcher notes that the text will be Japanese and that this build is tested on English patch
   v2.1. If the ELFs differ, the code check rejects it anyway.
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
- `reports/2026-10-26-memory-cards.md`: memory card work in progress.
- `legacy/README.md`: Modern camera controls and its Select + R3 switch.
- `docs/ops/android.md` "GPU driver (system or custom)": driver folder layout, fall-back checks, no bundled drivers.
- `PROJECT_CONTEXT.md:62`: code recompiled ahead of time from the disc.
- From memory, not opened: RetroArch's menu combos (Start+Select, hold Start); XInput not reporting Guide; raylib 5
  window-mode calls.
