# Android (Stage A1)

Target of Stage A1: the arm64 build boots to character select on the developer's AYN Odin 3 Max (Snapdragon 8
Elite, Adreno 830). The Odin is the test device, not the target (`android-reach-and-drivers` note). Nothing here
touches a device by itself: every `adb` command below needs the developer's "proceed".

## What is built

| Piece | Where | Notes |
|---|---|---|
| Runner + game code | `C:\ob\android-r` (NDK tree, RelWithDebInfo, `-O3 -g0`), `ps2xRuntime/libps2EntryRunner.so` | Same generated C++ as Windows (`C:\ob\src`); SSE through sse2neon (`USE_SSE2NEON`). The first build compiles every game unit once; later builds are incremental (same unity/PCH layout as Windows, patch 0028) |
| PCSX2 GS | `native/pcsx2_gs`, Android branch of its `CMakeLists.txt` | Vulkan only; PCSX2's `ARCH_ARM64` lists (SW JIT on vixl, `GSVector4*_arm64.h`); common's Linux files minus `LnxMisc.cpp` (`stubs/android/` has its other functions; `shm_open` = memfd); `upstream/pcsx2/0001` adds `VK_KHR_android_surface` |
| microVU | not built | x86 only (`research/2026-10-23-microvu-feasibility.md`); `PS2X_VU1` stays the interpreter (patch 0041's fallback). VU1 runs only from the GAME overlay on, after character select |
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
APK 22.0 MB: `libps2EntryRunner.so` 65 MB stripped, `libshaderc_shared.so` 7 MB, 28 shader files.

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
adb shell am force-stop com.outbreakmodern.app
```

`adb uninstall com.outbreakmodern.app` deletes the app's files dir too, ISO included (4.7 GB to push again).
