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
  Done for the custom driver (2026-10-24): `gpu_driver` in outbreak.ini, libadrenotools, Adreno only for now
  (`docs/ops/android.md` "GPU driver"). ARMSX2's driver database (per-driver render-target read paths) is not ported.

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

## Runner integration (T8)

- Selector: `PS2X_GS=cpu|pcsx2-sw|pcsx2-vk`, read once at startup (`ps2_runtime.cpp:752-775`); unset = cpu; unknown =
  `[gs] unknown PS2X_GS=...`, error exit. The gate passes it through: `PS2X_GS=pcsx2-sw bash tools/ob.sh gate`.
- Seam = PS2Recomp patches 0021-0023 (`upstream/ps2recomp/`): `PS2ExternalGS` (`runtime/gs/gs_external.h`):
  `gifPacket(path, data, size)` from the GIF arbiter sink, `privWrite(offset, value)`, `vsync(tick, regs)`,
  `readLocalToHost`, `reset`, `snapshot`; null = unchanged CPU behaviour. CMake `PS2X_EXTERNAL_GS_DIR` links
  `native/pcsx2_gs`; refuses `PS2X_ENABLE_DEBUG_UI=ON` with it (one imgui). 0022: native GIF DMA chains tee their
  PATH3 bytes; packed external frames latch into the 640-stride host frame. 0023: public `GS::writeRegister` (HLE
  libgraph stubs: sceGsPutDrawEnv, sceGsSwapDBuff, clear packets) tees a 32 B PACKED A+D PATH3 packet.
  The CPU GS still runs underneath (CSR/SIGNAL/FINISH, FIELD stay ours); PCSX2 only draws.
- Two targets: `hook.cpp` (built with PS2Recomp headers) implements `PS2ExternalGS` over a plain-types bridge
  `ob_gs_bridge.h`; `pcsx2_side.cpp` (built with PCSX2 headers) calls the GS API. Header sets never meet.
- Per-path carry (`ob_gs_gif`): only whole GIFtag units go to `GSgifTransfer` (the arbiter hands DMA slices that
  split tag from data; `GSState::Transfer` overreads on a call ending right after a PACKED tag). The tail carries to
  the next packet of that path only if its open unit is IMAGE; any other cut unit is dropped, as the CPU GS drops it
  (one malformed packet would otherwise swallow the path's later packets).
- Synthetic-tag strip: the VIF1 interpreter resubmits the rest of a PATH2 IMAGE behind a synthesized IMAGE tag
  (`ps2_vif1_interpreter.cpp:307-323`); PCSX2 still has the original IMAGE open, so the adapter drops that 16 B tag
  (PATH2, carry holds open IMAGE, tag = FLG 2, NLOOP = qwc-1, other bits 0). Without it: black frames.
- Vsync: privileged regs mirror into the 8 KB basemem, then `GSvsync(field = CSR bit 13)`.
- Snapshot (frame dump/capture), only on demand since patch 0034: the scheduler calls it at a VBlankStart only when
  `ps2ExternalGSFrameWanted(tick)` (the diagnostics plugin's `wantsFrame`: a capture step, or a plain frame dump);
  a play run never reads the GS back. 1:1 crop of the displayed rect read from `g_gs_renderer->m_mem` (circuit 1 if
  PMODE EN1 else 2; DISPFB FBP/FBW/PSM, DBX/DBY; size (DW+1)/(MAGH+1) x (DH+1)) after `Flush` +
  `InvalidateLocalMem` (SW sync). `GSSaveSnapshotToMemory` returns the whole 640x960 texture, not the display rect.
- GS thread (default; `PS2X_GS_THREAD=0` = inline as before): PCSX2's MTGS scheme (`pcsx2/MTGS.cpp`). The game
  thread only copies each command into a single-producer/single-consumer ring (`adapter/gs_ring.h`, 8 MiB = MTGS's
  `1 << 19` qwords, 32-byte records: GIF with its path, PRIV, VSYNC with the 15 privileged regs + CSR/IMR/BUSDIR/
  SIGLBLID, READ_LOCAL, RESET, SNAPSHOT); the GS thread `GSThread` runs every PCSX2 call in ring order, so the
  arbiter's order is kept. Per-path carry and synthetic-tag strip run there (`ExecGif`, one place). Readback and
  snapshot post a record and wait until the GS thread has run it (MTGS `SendPointerPacket` + `WaitGS`); a packet over
  2 MiB goes by pointer and waits the same way. Frame pacing: after a vsync the game thread waits while more than 2
  vsyncs are queued (`MTGS::PostVsyncStart`, `EmuConfig.GS.VsyncQueueSize`, default 2 in `Config.h`). CSR
  SIGNAL/FINISH/FIELD stay ours (game thread). Window attach and resize stay on the main thread (the child window
  must belong to the thread that pumps the parent); the resize is applied at the next vsync the GS thread runs.
  Sleep/wake: each side spins, then sleeps on a condition variable; the waker clears the sleeper's flag with
  `exchange` (a plain store lost a wake-up and froze the game, report entry 27); a 20 ms timeout is a safety
  valve, `timeout wakes` in the stats line must stay 0.
  Shutdown: the child's `WM_DESTROY` (the runner's `CloseWindow`, after it joined the game thread) drains the ring
  and joins the GS thread; later calls run inline. Not a thread_local destructor at the game thread's exit: that
  runs under the loader lock and deadlocks with the GS thread's own exit. Tests: `ob_gs_ring_tests` (the ring alone),
  `ob_pcsx2_gs_adapter_tests` (the adapter end to end on the SW renderer).
- Pitfall: never `GSReadLocalMemoryUnsync` for diagnostics or snapshots: it mutates PCSX2's transfer state
  (`GSState.cpp:3304-3333`) and broke the run. Readback (`readLocalToHost`) uses `GSInitAndReadFIFO`.
- Staging: `cmd_recompile` configures with `-DPS2X_EXTERNAL_GS_DIR=native/pcsx2_gs -DOB_PCSX2 -DOB_PCSX2_DEPS` and
  copies `shaderc_shared.dll`, `zstd.dll` and shaders beside `ps2EntryRunner.exe`; boots run with the Vulkan SDK off
  PATH. Needs `ob.sh pcsx2-gs` once (deps). `gate.py stamp` hashes `C:\ob\pcsx2` HEAD + diff and `native/pcsx2_gs/`.

## Later phases

- Debt before Android (partner decision): the synthetic-tag strip couples the adapter to
  `ps2_vif1_interpreter.cpp:307-323`. Clean fix: a PS2Recomp patch flagging those packets in the arbiter (tee without
  the tag).
- Lead for 1.4: an untagged 135,280 B PATH2 DIRECT of unknown origin (q0 `952f7627748d5850`, after a 96 B A+D +
  IMAGE tag and its 1024 B data) at the title -> main menu transition; possible VIF1 parser desync. The CPU GS also
  reads it as garbage; may explain faults shared by both GSes.
- 1.4: access logo colours stippled (navy/black shown as dithered grey-purple; suspect `Dithering` or a 16-bit
  path); savedata glyphs thin/broken strokes.

- 4K texture pack: `GSTextureReplacements` (+ `GSTextureReplacementLoaders.cpp`, `common/Image.cpp`) is stubbed now
  and must be un-stubbed, which brings libpng/libjpeg/libwebp (and zlib) back into `pcsx2-deps.bat`.
- GS dumps (GSDump/GSLzma) are stubbed; un-stub only if dumps become a debugging need (zstd is already built).
