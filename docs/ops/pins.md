# Pins

Source: R0-003, upstream/ps2recomp/README.md, tools/ob.sh; 2026-10-21.

- OS: Windows 11. MSVC 19.44 / VS 2022 Community (vcvars64 via `OB_VCVARS`), CMake 3.30, Ninja.
- MSYS2 GCC 16.1 + gdb 17.2 (C:\ob\g, probes only).
- Python: `C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe` (3.14.5).
- PCSX2 `v2.9.134` = `a81f1aa` (GS in-process, `native/pcsx2_gs/`; microVU in the runner behind `PS2X_VU1=mvu` and offline in `ob.sh vucompare`, `native/pcsx2_vu/`) + `upstream/pcsx2/` patches (0001: Android Vulkan surface, 0002: Android host-preloaded libvulkan handle, branch `ob-patches` in `C:\ob\pcsx2`; no effect off Android). Pinned, not tracked: the gate refs were made by this GS. Deps: zstd 1.5.7 + shaderc 2026.2 (glslang 275822a, SPIRV-Headers 58006c9, SPIRV-Tools 6337eb6) in `C:\ob\pcsx2-deps` via `tools/ob/pcsx2-deps.bat` (subset of PCSX2's pinned script); zlib 1.3.2 + libpng 1.6.58 (no APNG patch) for texture replacement via `tools/ob/pcsx2-deps-png.bat` (same URLs and SHA-256 pins, run by `ob.sh` when `libpng16.dll` is missing); libzip from PCSX2's own `3rdparty/libzip`.
- PS2Recomp upstream `eb8688b` + `upstream/ps2recomp/0001-0051` (`git am`). Decomp: Outbreak-decomp `eb6af38`. Policy: track upstream HEAD; check at each phase start, rebase our patches when it moves; pin only for a bug that breaks it (developer, 2026-10-22). Checked 2026-10-22: HEAD is still eb8688b.
- Runner configure (tools/ob.sh): RelWithDebInfo `/O2 /Zi /DNDEBUG`, STUDIO/TEST/ANALYZER/RECOMP/FFMPEG/DEBUG_UI OFF,
  `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF` always (upstream default ON; std::cerr traces halved boot speed:
  reports/2026-10-21-pa3-groundwork-and-boot-speed.md struggle 5).
- Oracle: PCSX2 2.9.134 portable in `ps2_emu/` (git-ignored) with BIOS `ps2-0200j-20040614` and the English v2.1 iso.
- Game: SLPM-65428 v1.04 + English patch v2.1.
- Linux/Android (not re-run this generation): GCC 13.3, Clang 18.1 (last ran at 0006, 450/450), Ubuntu 24.04, NDK r27c API 29.
- Android (Stage A1, `docs/ops/android.md`): NDK `27.0.12077973` (clang, arm64-v8a, API 29, `c++_static`), SDK
  build-tools `35.0.0` (aapt2, zipalign, apksigner), platform `android-35`, JDK = Android Studio's `jbr` (keytool),
  CMake 3.30 + Ninja on the host; sse2neon `v1.9.1` = `92f6de1` (PS2Recomp's FetchContent pin); shaderc for arm64
  from the same pinned sources as Windows (`tools/ob/android.sh shaderc`, SHA-256 re-checked).
- libadrenotools (Android custom Vulkan driver loader, BSD-2-Clause): github.com/bylaws/libadrenotools master `8fae8ce` (2024-09-10, the last push per the GitHub API, 2026-10-24; not archived) + submodule liblinkernsbypass `aa39758` (2024-09-07), cloned by `tools/ob/android.sh configure` into `C:\ob\adrenotools` (`OB_ADRENOTOOLS`); built from source in the Android tree (static `adrenotools` + `linkernsbypass`, hooks `libhook_impl.so` + `libmain_hook.so` in the APK). No GPU driver is bundled.
- ARMSX2 (Android VU1: its ARM64 microVU in `libob_a64vu.so`, `native/pcsx2_vu/a64`): github.com/ARMSX2/ARMSX2
  `90126bdfea7a3ffb1fd288b2f7f0ae0b033af2d6` (2026-10-22), blob-less sparse clone in `C:\ob\armsx2` (`OB_ARMSX2`,
  `gc.auto 0`). Pinned, not tracked (65 commits on `microVU-arm64.cpp` in four months): the a64 CMakeLists refuses
  another HEAD. A bump is deliberate: re-copy `config_defaults_a64.cpp` (Pcsx2Config.cpp constructors) and
  `GetGSPacketSize` in `ob_a64mvu_fakes.h`, re-check the external symbol list, re-run vucompare on the device.
  vixl from `C:\ob\pcsx2\3rdparty\vixl` (same sources as ARMSX2's, study 1.4).
