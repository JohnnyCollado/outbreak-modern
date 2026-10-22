# The GPU GS

## Decisions

- **GPU GS = PCSX2's GS** (developer, 2026-10-22). It is the most mature, has proven texture
  replacement for the 4K pack, and reaches Android through the ARMSX2/Turnip lineage.
  paraLLEl-GS was rejected (no Android evidence, no texture replacement).
  Evidence: `research/2026-10-22-gpu-gs-options.md`.
- **Phase 1.3 (CPU GS fault fixes) stops** in favour of integrating PCSX2's GS (partner,
  2026-10-22, under the developer's "decide on efficiency" mandate). Generation 1's goal, "draws
  its boot and menu screens as PCSX2 does", is met faster by running PCSX2's GS than by fixing
  the CPU rasteriser fault by fault. The CPU GS stays as a selectable fallback, unfixed.
- **In-process, not a separate GS process.** We are GPL-3.0 through PS2Recomp, so a process
  split buys nothing, and it would be a burden on Android.
- **Windows first.** Android later: Vulkan only, ARMSX2's driver database, and a choice between
  the system driver and a custom (Turnip) driver loaded by the app, aimed at as many Android GPUs
  as possible, Mali included.

## Where it plugs in

See `research/2026-10-22-gs-seam-in-ps2recomp.md` (file:line map of the GIF choke point,
backend interface, privileged registers, readback, present and vblank).

## Build (T7)

- `bash tools/run.sh pcsx2-gs -- bash tools/ob.sh pcsx2-gs`: clones `C:\ob\pcsx2` if missing (tag `v2.9.134` =
  `a81f1aa`, then `git am upstream/pcsx2/*.patch`; none yet), builds deps if `C:\ob\pcsx2-deps\bin\shaderc_shared.dll`
  is missing, configures + builds `native/pcsx2_gs` into `C:\ob\pcsx2-gs-build` (Ninja, Release, MSVC 19.44), stages
  `bin/resources/shaders` + `shaderc_shared.dll` + `zstd.dll` beside `ob_pcsx2_gs_smoke.exe`, runs it. Env overrides:
  `OB_PCSX2`, `OB_PCSX2_DEPS`, `OB_PCSX2_GS_BUILD`. Clean build ~45 s, 104 steps. Log `C:\ob\logs\ob-pcsx2-gs.log`.
- Green = `SW inside=10E0F0 want 10E0F0 ok`, `SW outside=203040 want 203040 ok`, `VK GSopen ok`, `smoke PASS`, exit 0.
- Pin: `docs/ops/pins.md` (PCSX2 line). Pinned, not tracked. Our PCSX2 edits: `upstream/pcsx2/README.md`.
- Output: `ob_pcsx2_gs` STATIC; API header `native/pcsx2_gs/include/ob_pcsx2_gs.h` (`ObPcsx2GS::SetRenderWindow`).

## Deps

- `tools/ob/pcsx2-deps.bat` = the zstd 1.5.7 and shaderc 2026.2 (glslang, SPIRV-Headers, SPIRV-Tools) sections of
  PCSX2's `build-dependencies.bat`, verbatim (URLs, SHA-256, `shaderc-changes.patch`), unzipped with Windows `tar.exe`
  (no 7-Zip). Why only these: zstd for GSLzma/GSDump, shaderc for Vulkan shader compile (loaded as a DLL). Qt, SDL,
  FFmpeg, png/jpeg/webp, ryml, DirectX-Headers are not built: their users are excluded or stubbed.

## Source lists and exclusions

- `native/pcsx2_gs/CMakeLists.txt` reads PCSX2's own lists at configure time (`ob_extract`, FATAL_ERROR on a missing
  anchor): `pcsx2GSSources`, `pcsx2GSSourcesUnshared`, the ARCH_X86 and USE_VULKAN appends; common base + x86
  emitter + WIN32 + FastJmp.asm; 3rdparty imgui and fmt lists; cpuinfo via add_subdirectory. Single ISA: `/arch:AVX2`,
  no `MULTI_ISA_*`. Renderers SW, HW, VK; no DX11/DX12/OpenGL. Core files outside GS/common: 0.
- Excluded (REMOVE_ITEM): GS `GSCapture.cpp` (FFmpeg), `GSDump.cpp`, `GSLzma.cpp`, `GSPng.cpp`,
  `Renderers/HW/GSTextureReplacements.cpp`, `GSTextureReplacementLoaders.cpp`; common `PrecompiledHeader.cpp`,
  `Image.cpp`, `YAML.cpp`, `HTTPDownloader*.cpp`; imgui `imgui_demo.cpp`, `imgui_freetype.cpp` (stb_truetype instead).
  `Pcsx2Config.cpp` is not built (pulls CDVD/memcard/Pad/USB/audio).

## Stubs (`native/pcsx2_gs/stubs/`)

- `host_stubs.cpp`: `Host::` (render window, present, fullscreen, translate, OSD, errors, settings = defaults),
  `VMManager::` (HasValidVM, disc ids, empty SW-renderer processor list), `MTGS::` (RunOnGSThread inline, rest no-op),
  `PerformanceMetrics::`, counters (59.94 Hz), `BuildVersion`, `eeHw`, `_cpuRegistersPack`, `FMVstarted`,
  `SysMemory::GetCodePtr` (64 MB RWX range for the SW JIT).
- `config_stubs.cpp`: `EmuFolders`, `EmuConfig`, empty option-struct ctors. GSOptions ctor, `GetRendererName`,
  `RestartOptionsAreEqual`, `ShouldDump` and friends are copied verbatim from `Pcsx2Config.cpp` v2.9.134 with line
  cites: **re-copy them on every PCSX2 bump**.
- `gs_stubs.cpp`: GSCapture, GSDumpBase/GSDumpReplayer, `GSPng::Save`, GSTextureReplacements (10 fns), `D3D::`
  (`GetPreferredRenderer` = VK, so SW also presents through the Vulkan device).
- `ui_stubs.cpp`: `ImGuiFreeType::GetFontLoader`, `ImGuiManager` (Initialize makes a real empty ImGui context +
  default font: `GSDeviceVK::RenderImGui` runs every present), `FullscreenUI::Render`, `RGBA8Image`.
- D3D shim headers `native/pcsx2_gs/include/GS/Renderers/DX11/{D3D.h,GSDevice11.h}`, `DX12/GSDevice12.h`, searched
  before the PCSX2 tree: GS.cpp/GSUtil.cpp include them unconditionally on _WIN32 (would need DirectX-Headers);
  `GSDevice1x` aliases `GSDeviceVK` in dead branches. No PCSX2 source edit.

## imgui (binding for T8)

- PCSX2's 3rdparty imgui 1.93.0 is the only imgui compiled, into `ob_pcsx2_gs`. PS2Recomp fetches imgui 1.92.7 only
  when `PS2X_ENABLE_DEBUG_UI=ON`; the runner keeps it OFF. T8's seam patch must make CMake refuse
  `PS2X_ENABLE_DEBUG_UI=ON` together with the external GS (two imgui versions in one link = ODR break).

## Gotchas

- Vulkan SDK DLL shadowing: `C:\VulkanSDK\<ver>\Bin` on PATH has its own `shaderc_shared.dll`; with the staged DLL
  removed the smoke still passed. `cmd_pcsx2_gs` strips every `vulkansdk` PATH entry before running the smoke.
  Control: staged DLL removed + SDK off PATH = "Failed to compile utility pipelines", exit 1. The runner (T8+) must
  stage the DLL beside the exe and not rely on PATH. Cookbook entry: C0001.
- Smoke basemem sets SMODE1 CMOD=2 (NTSC): `GSState::GetVideoMode` reads it, else Unknown. Smoke sets
  `DisableShaderCache=true` so every run compiles through shaderc.

## Later phases

- 4K texture pack: `GSTextureReplacements` (+ `GSTextureReplacementLoaders.cpp`, `common/Image.cpp`) is stubbed now
  and must be un-stubbed, which brings libpng/libjpeg/libwebp (and zlib) back into `pcsx2-deps.bat`.
- GS dumps (GSDump/GSLzma) are stubbed; un-stub only if dumps become a debugging need (zstd is already built).
