# Android (Stage A1)

Target of Stage A1: the arm64 build boots to character select on the developer's AYN Odin 3 Max (Snapdragon 8
Elite, Adreno 830). The Odin is the test device, not the target (`android-reach-and-drivers` note). Nothing here
touches a device by itself: every `adb` command below needs the developer's "proceed".

## What is built

| Piece | Where | Notes |
|---|---|---|
| Runner + game code | `C:\ob\android-r` (NDK tree, RelWithDebInfo, `-O3 -g0`), `ps2xRuntime/libps2EntryRunner.so` | Same generated C++ as Windows (`C:\ob\src`); SSE through sse2neon (`USE_SSE2NEON`). The first build compiles every game unit once; later builds are incremental (same unity/PCH layout as Windows, patch 0028) |
| PCSX2 GS | `native/pcsx2_gs`, Android branch of its `CMakeLists.txt` | Vulkan only; PCSX2's `ARCH_ARM64` lists (SW JIT on vixl, `GSVector4*_arm64.h`); common's Linux files minus `LnxMisc.cpp` (`stubs/android/` has its other functions; `shm_open` = memfd); `upstream/pcsx2/0001` adds `VK_KHR_android_surface`; `0002` lets the adapter hand PCSX2 a custom driver's libvulkan (GPU driver, below) |
| microVU (VU1 recompiler) | `native/pcsx2_vu/a64` -> `libob_a64vu.so` in the Android tree (`ps2xRuntime/external_vu1/a64/`) | ARMSX2's ARM64 microVU (pinned `90126bd`, `C:\ob\armsx2`), compiled against ARMSX2's own headers into its own shared library (study `research/2026-10-24-armsx2-microvu-feasibility.md`): hidden visibility, exports only the `ObPcsx2Vu::*` API and the `ob_pcsx2_vu_api` table. Not linked to the runner: `adapter/hook.cpp` dlopens it at start-up, so a library that cannot load or start falls back to the interpreter with the reason. Defaults: VU1 recompiler on, MTVU off, vu-aot and the on-disk program cache off. `PS2X_VU1` default on Android = `mvu` (patch 0048; `vu1 = interp` in `outbreak.ini` selects the interpreter). The interpreter (patch 0045: no software quad floats, no fused multiply-add) stays the fall-back |
| Texture replacement | Android branch of `native/pcsx2_gs/CMakeLists.txt` | PCSX2's `GSTextureReplacements*.cpp` (`OB_TEXTURE_REPLACEMENTS`), as on Windows: libpng 1.6.58 and zstd 1.5.7 built static from `C:\ob\pcsx2-deps-build` (`tools/ob/pcsx2-deps*.bat` unpacked them), zlib from the NDK, PCSX2's `3rdparty/libzip`. Pack in `files/textures/<serial>/replacements`, `textures = textures` in `outbreak.ini`; the Odin holds 60 fps in J's Bar with the 3.2 GB pack (`reports/2026-10-24-android.md` entry 16) |
| shaderc | `C:\ob\android-shaderc-build/libshaderc/libshaderc_shared.so` | From the sources `tools/ob/pcsx2-deps.bat` unpacked and patched (`C:\ob\pcsx2-deps-build\shaderc-2026.2`); `android.sh shaderc` re-checks the four archives' SHA-256 |
| Shaders | APK `assets/resources/shaders` + `files.txt` | Copied by the GS adapter to `<internal files>/resources` on first start (and when the list changes): PCSX2 reads resources as files |
| APK | `C:\ob\android-apk\outbreak-modern.apk` | Gradle (`native/android`: `settings.gradle.kts`, `build.gradle.kts`, wrapper Gradle 9.7.1, AGP 9.4.0, JDK 17 = `OB_GRADLE_JDK`, default Microsoft OpenJDK 17; Kotlin 2.4.10 + Compose BOM 2026.08.00, as PlayFieldPortal, all from the local Gradle cache): `android.sh package` strips and stages `lib/arm64-v8a/*.so` and `assets/resources` in `C:\ob\android-apk\stage`, then `gradlew assembleDebug` compiles `kotlin/` (LauncherActivity, OutbreakActivity, the Compose menu), packs (`useLegacyPackaging` = `extractNativeLibs="true"`) and signs (v2) with the debug key `C:\ob\android-keys\debug.keystore` (keytool, first package; never in git). Build tree `C:\ob\android-apk\gradle-build`, log `C:\ob\logs\ob-android-gradle.log`; packaging ~12 s, unchanged ~5-8 s (Gradle daemon); first Compose build 89 s. Debug APK ~79 MB (unshrunk Compose dex stored uncompressed) |

Package id: **`com.outbreakmodern.app`**, activities (Kotlin, `native/android/kotlin`, plan decisions A13-A14): `LauncherActivity` (MAIN/LAUNCHER: the Compose settings launcher; disc pick and imports through the Storage Access Framework; the C++ ini editor, disc check and card reader through `NativeBridge` = JNI in `libps2EntryRunner.so`, `native/ob_ui/android/android_menu.cpp`) and `OutbreakActivity` (the game, a `NativeActivity` subclass: opens the saved disc and passes `PS2X_GAME_PATH=/proc/self/fd/<n>` and `PS2X_LAUNCHER=0`; the in-game menu is a Compose dialog it shows when the runner calls `showMenu`), lib name
`ps2EntryRunner`), minSdk 29, targetSdk 35, sensor landscape, `android.hardware.gamepad` optional, Vulkan 1.1
required.

## Commands (Git Bash, repo root)

| What | Command | Notes |
|---|---|---|
| Everything | `bash tools/ob.sh android` | shaderc, configure, build, package, verify; each skips what is up to date |
| One step | `bash tools/ob.sh android <shaderc\|configure\|build\|package\|verify>` | Logs `C:\ob\logs\ob-android-<step>.log` |

After a game-code or runtime change: `bash tools/ob.sh recompile` (Windows; refreshes the generated sources in
`C:\ob\src`), then `bash tools/ob.sh android` (only changed units rebuild).

Build cost (2026-10-24, 16 jobs): shaderc 198 s once; full runner build 249 s (542 steps, 329 game unity units;
only when the generated code or game-facing headers change); one runtime file changed: 4 s (link 0.5 s); package
~5 s. The tree is `RelWithDebInfo` with `-O3 -DNDEBUG -g0`, as Windows: `Release` turns on ThinLTO
(`ReleaseMode.cmake`) and the NDK's `-g` made each link 140-237 s (`reports/2026-10-24-android.md` entry 5).
APK 22.1 MB (2026-10-24, with 0045-0047 and the GPU driver choice): `libps2EntryRunner.so` 65 MB stripped, `libshaderc_shared.so` 7 MB, libadrenotools' hooks `libhook_impl.so` 278 KB + `libmain_hook.so` 4 KB, 28 shader files.

`verify` reads the stripped `.so` files with the NDK's `llvm-readelf`/`llvm-nm`: every `NEEDED` library must exist
in the NDK sysroot for API 29 (or be one of ours), every undefined dynamic symbol must be defined by one of them.

## How it runs on the device

- Start: NativeActivity loads `libps2EntryRunner.so`; raylib's Android layer (`rcore_android.c`, native app glue)
  calls the runner's `main()`. stdout/stderr go to logcat, tag `ps2x` (`main.cpp`).
- Settings and game: `/sdcard/Android/data/com.outbreakmodern.app/files/` (the activity's external files dir).
  `outbreak.ini` there is read if present (`native/outbreak.ini.example`); without a `game` key the one `.iso` in that
  folder is the game (patch 0043). Memory cards: PCSX2 `.ps2` files in `files/memcards` (`Mcd001.ps2` made when
  missing; `outbreak.ini` `saves = <folder>` picks another; patches 0071-0072). Not yet built or run on the Odin.
- Graphics: no environment on a device, so with no `PS2X_GS` an Android build with the external GS runs PCSX2's
  Vulkan renderer (`pcsx2-vk`, patch 0044; desktop default stays `cpu`). VU1: ARMSX2's ARM64 microVU (patch 0048:
  `PS2X_VU1` unset = `mvu` on Android; `outbreak.ini` `vu1 = mvu|interp`, the variable wins). logcat shows
  `[vu1] mvu from the default` (or `settings`), then `[vu1] mvu active: a64 mvu ...` or `[vu1] mvu unavailable
  (<reason>); falling back to interp` (for example `dlopen libob_a64vu.so: ...` or the RWX code-cache mmap), and
  `[vu1] backend=<mode used>`.
- Window: the GS adapter takes the activity's `ANativeWindow`, releases raylib's EGL window surface (its GL
  context stays current without a surface), resets the window's buffers to its own size (raylib had shrunk them to
  its 640x448 screen: 796x448 on a 1080p display) and presents with Vulkan into the window at the display's
  resolution (`attach android window 1920x1080` on the Odin). `upscale = 2..3` in `outbreak.ini` then shows its
  detail: 3 holds 60 fps in J's Bar on the Odin (`reports/2026-10-24-android.md` entry 14).
- Input: the Odin's built-in controller through raylib's Android gamepad events and the patch 0038/0040 bindings.
- Speed: `[fps] tick N | R fps` once per second in logcat (patch 0044).
- GPU driver: `[gpu] driver=...` once at GS start (below).

## GPU driver (system or custom)

The player picks the Vulkan driver PCSX2's GS runs on: the phone's own (default), or a driver they supply, such
as Mesa Turnip on Adreno, which is often faster (ARMSX2's Turnip path, `research/2026-10-22-gpu-gs-options.md`).
No driver is bundled or downloaded by the app or by us; players bring their own package.

Steps for the player:

1. Get a driver package (a folder or zip with `meta.json` and the driver `.so`, the libadrenotools/Yuzu layout,
   `C:\ob\adrenotools\tools\ADPKG.md`). Unzip it.
2. Put the folder at `/sdcard/Android/data/com.outbreakmodern.app/files/drivers/<name>/`, so that
   `drivers/<name>/meta.json` exists. `<name>` = letters, digits, `.`, `_`, `-` (up to 64).
3. In `outbreak.ini` (same files dir): `gpu_driver = <name>`. `gpu_driver = system` (or no line) = the phone's
   driver.
4. Start the game. Logcat (tag `ps2x`) shows the driver in use:
   `[gpu] driver=custom:<name> vendor=Qualcomm(0x5143) deviceName=... driverVersion=... (driverName driverInfo) api=...`
   or, when the custom driver cannot be used, `[gpu] custom driver <name> unavailable (<reason>); using system
   driver` followed by `[gpu] driver=system ...`. The game always starts.

How it works (`native/pcsx2_gs/adapter/gpu_driver*.{h,cpp}`, PCSX2 patch `upstream/pcsx2/0002`, PS2Recomp patch
0046 for the setting):

- At the first GS call, before `GSopen`, the adapter copies `drivers/<name>/` from the external files dir to
  `<internal files>/drivers/<name>/` when a file's size or time changed (`.ob-stamps`): Android does not load
  libraries from shared storage (libadrenotools `driver.h`). A folder put straight into the internal dir is used
  as is.
- Fall-back reasons, decided by `obgpu::CustomDriverBlocker` (host-tested, `ob_gpu_driver_tests`): no folder, no or
  bad `meta.json`, the library (`libraryName`, else the one `vulkan.*.so`) missing, not Adreno (`/dev/kgsl-3d0`
  absent), the device's API below the package's `minApi`, APK libraries not extracted, hooks missing. Then
  libadrenotools `adrenotools_open_libvulkan` (hooks `libhook_impl.so` + `libmain_hook.so` in the APK's `lib/`,
  found from our own `.so`'s directory = `nativeLibraryDir`, `extractNativeLibs="true"`), then a probe (instance
  with `VK_KHR_surface` + `VK_KHR_android_surface`, at least one Vulkan 1.1 device). Only a handle that passes is
  handed to PCSX2 (`Vulkan::SetPreloadedVulkanLibrary`). If PCSX2's `GSopen` still fails with it, the adapter
  logs the fall-back line, returns PCSX2 to the system library and opens once more.
- The system driver is not loaded at all when a custom one is chosen (no system probe first).
- Desktop: `gpu_driver` is read and logged, a non-system value logs `[settings] gpu_driver=<v> ignored: custom GPU
  drivers are Android only`.
- libadrenotools pin and license: `docs/ops/pins.md`, `CREDITS.md`.

## Destination read (`gs_self_read`, PS2Recomp patch 0054)

Some draws blend with the colour already drawn (J's Bar's light glows, the in-game menu's backdrop). PCSX2 2.9.134
reads it in-pass on every device (framebuffer fetch where offered, else a texture barrier). Qualcomm's own Adreno
driver returns stale colour there: on the Odin the glows came out as black disks and the menu backdrop black
(`reports/2026-10-24-android.md` entries 11-12; ARMSX2 documents the same driver bug). The adapter decides per driver
(`native/pcsx2_gs/adapter/gpu_driver.h` `DecideSelfRead`, host-tested in `ob_gpu_driver_tests`) before GSopen:

- `gs_self_read = auto` (default): Qualcomm's proprietary driver (vendor 0x5143, driverName with "Qualcomm", not
  Mesa/Turnip; no driverName counts as proprietary) -> `copy`; any other driver, and desktop (no probe) -> PCSX2's
  choice. Logcat: `[gpu] self-read=copy (auto: Qualcomm's driver)` after the `[gpu] driver=` line.
- `copy`: `OverrideTextureBarriers = 0` + `DisableFramebufferFetch` (the renderer reads a copy of the target);
  `barrier`: `OverrideTextureBarriers = 1` + `DisableFramebufferFetch`; `fetch`: PCSX2's choice. Logged as
  `(outbreak.ini)`. A bad value logs `[settings] line N: ...` and auto is used.
- Verified on the Odin (2026-10-24, Adreno 830, driver 512.800.72): J's Bar glows and windows and the menu backdrop
  match Windows; 60.0 fps mean over 4 min (`C:\ob\logs\odin-run3-copy.log`). Turnip and Mali keep PCSX2's choice
  (not tested: no device); the Mali gaps of unmodified PCSX2 (push descriptors required) are open (entry 11).

## VU1 on arm64: vucompare on the device (each command needs the developer's "proceed")

`tools/ob/vucompare_a64.sh` builds `native/pcsx2_vu/a64` standalone into `C:\ob\android-vu-build`: `libob_a64vu.so`
and `ob_vucompare_a64` (the compare tool `native/pcsx2_vu/compare/main.cpp` for arm64; the interpreter from the
Android tree's `libps2_runtime.a`, so run `ob.sh android build` first), stripped into `stage\`, symcheck'd. The
Windows tool writes x86 microVU's outputs per capture as golden files (`bash tools/ob.sh vucompare --write-golden
'C:\ob\vu1-captures\golden'`; `--synthetic --write-golden 'C:\ob\vu1-captures\golden-synthetic'`). On the device the
tool runs arm64 microVU and the arm64 interpreter on each capture, compares microVU with the interpreter and (with
`--golden`) with x86 microVU bit for bit (data memory, VF/VI/ACC/Q/P/I/R/STATUS/MAC/CLIP, PATH1 packets), and prints
ns per VU1 cycle for both. A plain executable in `/data/local/tmp`, no APK involved.

```
adb -s <odin-ip>:33537 shell mkdir -p /data/local/tmp/obvu
adb -s <odin-ip>:33537 push C:\ob\android-vu-build\stage\ob_vucompare_a64 C:\ob\android-vu-build\stage\libob_a64vu.so /data/local/tmp/obvu/
adb -s <odin-ip>:33537 push C:\ob\vu1-captures /data/local/tmp/obvu/
adb -s <odin-ip>:33537 shell chmod 755 /data/local/tmp/obvu/ob_vucompare_a64
adb -s <odin-ip>:33537 shell "cd /data/local/tmp/obvu && LD_LIBRARY_PATH=. ./ob_vucompare_a64 --synthetic --repeat 20 --golden vu1-captures/golden-synthetic > synthetic.log 2>&1; echo exit $?"
adb -s <odin-ip>:33537 shell "cd /data/local/tmp/obvu && LD_LIBRARY_PATH=. ./ob_vucompare_a64 vu1-captures --repeat 20 --golden vu1-captures/golden > vucompare-a64.log 2>&1; echo exit $?"
adb -s <odin-ip>:33537 pull /data/local/tmp/obvu/synthetic.log C:\ob\logs\vucompare-a64-odin-synthetic.log
adb -s <odin-ip>:33537 pull /data/local/tmp/obvu/vucompare-a64.log C:\ob\logs\vucompare-a64-odin.log
adb -s <odin-ip>:33537 logcat -d -v time -s DEBUG:V libc:F > C:\ob\logs\vucompare-a64-odin-crash.log   (only after exit 134/139)
```

Exit 1 is expected (the STATUS/MAC flag-elimination differences from the interpreter, as on x86); the lines to read
are `golden (...): mvu == x86 golden bit for bit N/120`, the per-field counts under it, and the `timing` / `weighted`
ns-per-cycle lines. About 9 MB is pushed (captures 6.2 MB, goldens 2.5 MB); captures are game-derived: never in git.

## Device commands (each needs the developer's "proceed")

`adb` = `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`.

```
adb devices
adb install -r C:\ob\android-apk\outbreak-modern.apk
adb shell mkdir -p /sdcard/Android/data/com.outbreakmodern.app/files
adb push "D:\NEXTJJEN\repos\outbreak-modern\ps2_emu\roms\Biohazard - Outbreak (English v2.1).iso" /sdcard/Android/data/com.outbreakmodern.app/files/
adb push D:\NEXTJJEN\repos\outbreak-modern\native\outbreak.ini.example /sdcard/Android/data/com.outbreakmodern.app/files/outbreak.ini   (optional; edit game= first or remove that line)
adb logcat -c
adb shell am start -n com.outbreakmodern.app/android.app.NativeActivity
adb logcat -v time -s ps2x:V raylib:V vulkan:V AndroidRuntime:E DEBUG:V libc:F
adb shell mkdir -p /sdcard/Android/data/com.outbreakmodern.app/files/drivers   (custom GPU driver, optional)
adb push <unzipped driver folder> /sdcard/Android/data/com.outbreakmodern.app/files/drivers/<name>
adb logcat -v time -s ps2x:V hook_impl:V | findstr /C:"[gpu]" /C:"[fps]" /C:"[settings]" /C:"[pcsx2-gs] open"
adb shell am force-stop com.outbreakmodern.app
```

`adb uninstall com.outbreakmodern.app` deletes the app's files dir too, ISO included (4.7 GB to push again).

## Touch controls (2026-10-26)

Design: `docs/plans/2026-10-26-touch-controls.md`; report `reports/2026-10-26-touch-controls.md`. Classic camera only.
- `TouchOverlay.kt`: a full-screen dialog window over `OutbreakActivity`, `FLAG_NOT_FOCUSABLE`, so the controller's
  keys and Back still go to the game's NativeActivity input queue. Off = no window at all (touches reach the game as
  before). Hidden while the in-game menu is open; Touch controls > Edit layout (its own page in the menu) shows it over the held game.
- `TouchLogic.kt`: zones, gestures, button sets, settings (no Android imports); tested by
  `native/android/test/kotlin/.../TouchLogicTest.kt`, which `android.sh package` runs (`gradlew testDebugUnitTest
  assembleDebug`) and reports as `kotlin tests: n/n, kotlin warnings: n`.
- Into the game: `NativeBridge.touchPad(pressed, rx, ry, lx, ly)` -> `obui::touchPad()` (`native/ob_ui/core/touch_pad.h`,
  one atomic), merged into port 0 by `obui::filterPad` before the menu gate (PS2Recomp patch 0078's pad hook; no new
  runtime patch). Auto hides on `NativeBridge.physicalPadReads()` rising (reads with a pad button down or a stick past
  0x30 from centre). Menu > Settings calls `NativeBridge.requestMenu()` (opened in `hostFrame` as by Back).
- Settings: `[ui] touch*` keys (`native/outbreak.ini.example`); the dragged layout in `touch_layout.ini` in the files
  dir (`[minimal]` / `[full]` / `[menu]`, `id = x, y, scale, hidden`).
- Menu layout (design section 8): a D-pad set while the game's status screen, map or pause menu is open, and on the
  title and select screens. `NativeBridge.gameScreen()` reads the game's RAM (PS2Recomp patch 0081 hands it to the UI
  hook; `native/ob_ui/core/game_screen.h` has the addresses), polled ten times a second; a toggle button switches by
  hand until the screen changes.

## What's installed (launcher, 2026-10-26)

The Kotlin launcher lists what sits in the app's files dir, with its size (measured in the background, `Installed.kt`)
and a Delete that asks first (Cancel focused):
- Game page: the disc image copy in the files dir (Delete) or a disc linked through the system picker (Forget: drops
  `game-uri.txt` and the kept permission, never the user's file); the texture pack folder `textures/SLPM-65428` and
  zip `textures/SLPM-65428.zip` as separate entries (PCSX2 loads both; the folder's copy wins where they overlap:
  `GSTextureReplacements.cpp` scans `<serial>/replacements` first and keeps the first entry per texture); a storage
  line (used by the app, free on the device).
- Mods page: each `.pnach` in `mods/SLPM-65428/` with On/Off (Off = renamed `.pnach.disabled`; the runner loads only
  `.pnach`, `ps2_pnach.cpp:221`), size, Delete; Delete all; import from a folder or zip. The menu's own
  `ob-widescreen.pnach` is not listed: Display > Aspect ratio alone manages it.
- Graphics driver page: several drivers side by side, an In use badge, Select and Delete (both the external
  `drivers/<name>/` and the GS's internal copy). A changed choice shows "Restart needed" with Restart now
  (`RestartActivity`, its own `:restart` process: the app's process ends, the launcher starts in a fresh one, since a
  Vulkan driver loads once per process).
