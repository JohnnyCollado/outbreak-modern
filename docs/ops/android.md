# Android (Stage A1)

Target of Stage A1: the arm64 build boots to character select on the developer's AYN Odin 3 Max (Snapdragon 8
Elite, Adreno 830). The Odin is the test device, not the target (`android-reach-and-drivers` note). Nothing here
touches a device by itself: every `adb` command below needs the developer's "proceed".

## What is built

| Piece | Where | Notes |
|---|---|---|
| Runner + game code | `C:\ob\android-r` (NDK tree, RelWithDebInfo, `-O3 -g0`), `ps2xRuntime/libps2EntryRunner.so` | Same generated C++ as Windows (`C:\ob\src`); SSE through sse2neon (`USE_SSE2NEON`). The first build compiles every game unit once; later builds are incremental (same unity/PCH layout as Windows, patch 0028) |
| PCSX2 GS | `native/pcsx2_gs`, Android branch of its `CMakeLists.txt` | Vulkan only; PCSX2's `ARCH_ARM64` lists (SW JIT on vixl, `GSVector4*_arm64.h`); common's Linux files minus `LnxMisc.cpp` (`stubs/android/` has its other functions; `shm_open` = memfd); `upstream/pcsx2/0001` adds `VK_KHR_android_surface`; `0002` lets the adapter hand PCSX2 a custom driver's libvulkan (GPU driver, below) |
| microVU | not built | x86 only (`research/2026-10-23-microvu-feasibility.md`); `PS2X_VU1` stays the interpreter (patch 0041's fallback). VU1 runs only from the GAME overlay on, after character select. Patch 0045: no software quad `long double` and no fused multiply-add in the VU files (0 `__*tf3` calls, 0 `fmadd` in their objects), so arm64 VU1 results match Windows |
| shaderc | `C:\ob\android-shaderc-build/libshaderc/libshaderc_shared.so` | From the sources `tools/ob/pcsx2-deps.bat` unpacked and patched (`C:\ob\pcsx2-deps-build\shaderc-2026.2`); `android.sh shaderc` re-checks the four archives' SHA-256 |
| Shaders | APK `assets/resources/shaders` + `files.txt` | Copied by the GS adapter to `<internal files>/resources` on first start (and when the list changes): PCSX2 reads resources as files |
| APK | `C:\ob\android-apk\outbreak-modern.apk` | No Gradle: `aapt2 link` (`native/android/AndroidManifest.xml`), `lib/arm64-v8a/*.so` added, `zipalign`, `apksigner` with the debug key `C:\ob\android-keys\debug.keystore` (keytool, first package; never in git) |

Package id: **`com.outbreakmodern.app`**, activity `android.app.NativeActivity` (`android:hasCode="false"`, lib name
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
  folder is the game (patch 0043). Memory cards: `files/mc0`.
- Graphics: no environment on a device, so with no `PS2X_GS` an Android build with the external GS runs PCSX2's
  Vulkan renderer (`pcsx2-vk`, patch 0044; desktop default stays `cpu`). VU1: interpreter (no microVU on arm64).
- Window: the GS adapter takes the activity's `ANativeWindow`, releases raylib's EGL window surface (its GL
  context stays current without a surface) and presents with Vulkan into the window.
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
