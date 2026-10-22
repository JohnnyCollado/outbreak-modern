# GPU GS options: paraLLEl-GS vs PCSX2's GS (2026-10-22)

Purpose: pick a GPU GS for our PS2Recomp build, Windows first, then Android (Adreno/Mali, AYN
Odin). Research was done by a web retriever on github.com only, from desk evidence (no builds
run); the partner saved it here. See also `2026-10-22-gs-seam-in-ps2recomp.md` for where it
plugs in.

## Answer

- **Windows:** both have working evidence. paraLLEl-GS runs in the Def Jam Vendetta recomp
  (jcsix694) with a CPU fallback. PCSX2's GS runs as a separate process in gt4-staticrecomp
  (moesuito), whose owner reports it "identical to PCSX2" at ~60 FPS at 3x scale.
- **Android:** only the PCSX2 lineage has evidence: ARMSX2 (GPL-3.0 PCSX2 fork) has a
  Vulkan/Adreno backend tuned per driver. paraLLEl-GS has no Android, Adreno or Mali evidence,
  and needs Vulkan features unverified on phone GPUs.
- **Texture packs:** PCSX2's GS has documented replacement (DDS, `textures/<serial>/replacements`).
  paraLLEl-GS documents none.
- **License:** paraLLEl-GS is LGPL-3.0+ (Granite MIT); PCSX2's GS is GPL-3.0. Both fit our
  GPL-3.0 project in-process. gt4 split the GS into a process only to keep its own executable
  free of GPL code; we don't need that.

## paraLLEl-GS (github.com/Arntzen-Software/parallel-gs)

- No documented C/C++ API. Inputs are PCSX2 GS dump streams: `parallel-gs-replayer` replays
  PCSX2 dumps (version 8); `parallel-gs-stream` reads a raw stream from a pipe (`GS_STREAM`).
- Library mode: CMake `PARALLEL_GS_STANDALONE` (off by default) drops Granite's renderer and
  runtime shader compiler; the host presents.
- Needs Vulkan descriptorIndexing, timelineSemaphore, 8/16-bit storage, shaderInt16,
  scalarBlockLayout, full subgroups, and subgroup size control for 16-64 threads.
- Upscaling: SSAA 2x-16x plus FSR1/RCAS. Readback and feedback handling not documented.
- Tested on AMD, NVIDIA, Intel Arc and Steam Deck; Intel UHD 620 "too slow". Nothing mobile.
- External contributions not accepted. Dual licensing "may be possible".
- Def Jam Vendetta recomp: paraLLEl-GS as a submodule with a local diff; `VENDETTA_GS=cpu`
  fallback, `VENDETTA_SSAA`. Whether it links in-process isn't stated.

## PCSX2's GS (github.com/PCSX2/pcsx2, `pcsx2/GS`)

- Core: GSState, GSDrawingContext, GSRegs, GSLocalMemory; dump and capture in GSDump/GSCapture.
  Renderers: DX11, DX12, Metal, OpenGL, SW, Vulkan. No GLES backend.
- Texture replacement: DDS with mips, per-serial folders, CLUT-hash wildcards (PR #14584),
  zip packs (PR #14857).
- **gt4-staticrecomp pattern** (fork moesuito/gt4recomp-gs, process `gt4-gs.exe`):
  - Shared-memory IPC with single-producer/single-consumer rings. Forward: GIF, PRIV, VSYNC,
    READ_FIFO, RESET, CONTROL. Back: READY, STATS, ERROR, SNAPSHOT_DONE, WINDOW, EXITING.
    Header `native/gs_stream/gs_stream_protocol.h`.
  - Vulkan backend; 59.9 FPS at 3x; readback wait 0.93 ms/frame. Batching cut wake-ups from 322
    to ~12 per frame. Windows-specific today.
- **ARMSX2:** Vulkan render-target reads chosen from a driver database. On Adreno 650+ with its
  Turnip build, a feedback-loop path is "2-3x faster where read-back is the frame cost".
  SD865: one game went from 54.7 ms to 22.4 ms per frame. Mali: no evidence.

## Side by side

| | paraLLEl-GS | PCSX2's GS |
|---|---|---|
| Integration | lower: library mode, fed a GS stream | higher: no library; fork it, or use gt4's process protocol |
| Windows | proven (Def Jam) | proven (gt4) |
| Android | unknown; Vulkan feature needs unchecked | Adreno proven via ARMSX2; Mali unknown |
| Accuracy | aims for CPU-like accuracy; not bit-exact to PCSX2 | the reference; no Outbreak fixes needed (GameDB) |
| 4K texture pack | none documented | built in |
| Upscaling | SSAA + FSR1 | internal resolution scale |
| License with GPL-3.0 | fits | fits |

## Not checked

paraLLEl-GS headers and source tree; the gt4 GS-STREAM protocol spec; gt4recomp-gs's
`pcsx2-gsrunner` (404); themaister.net (time budget). No builds or benchmarks.

## Sources (fetched 2026-10-22)

- https://github.com/Arntzen-Software/parallel-gs (and raw CMakeLists.txt)
- https://github.com/jcsix694/def-jam-vendetta-recomp
- https://github.com/moesuito/gt4-staticrecomp/pull/24, /pull/26, /pull/28, /pull/34
- https://github.com/PCSX2/pcsx2/tree/master/pcsx2/GS (and Renderers)
- https://github.com/ARMSX2/ARMSX2 and /pull/741
- Snippets only: parallel-gs PR #6; PCSX2 PR #4199, #14584, issue #10748
