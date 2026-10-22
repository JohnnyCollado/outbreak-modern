# Where a GPU GS plugs into our PS2Recomp (2026-10-22)

Purpose: size the swap from PS2Recomp's software GS to a GPU GS (paraLLEl-GS or PCSX2's GS).
Mapped by a code retriever in the vendored copy; paths are relative to
`PS2Recomp-main/PS2Recomp-main/ps2xRuntime/`.

## Summary

There is already a clean seam. `GSRasterBackend` (`include/runtime/gs/gs_backend.h:8-34`) is an
abstract backend, and `GS::setRasterBackend` (`src/lib/gs/gs_frontend.cpp:1635-1661`) swaps it.
Today the only implementation is the CPU rasteriser `GSCpuBackend`. A GPU GS would be a second
`GSRasterBackend` implementation. All three GIF paths already meet at one choke point, so a
GS that takes GIF packets can be fed there.

## GIF packets in

- PATH1 (VU1 XGKICK): `src/lib/vu/ps2_vu1_core.cpp:917-927` `finishXgkick()` -> `submitGifPacket(Path1)`.
- PATH2 (VIF1 DIRECT/DIRECTHL): `src/lib/ps2_vif1_interpreter.cpp:323`, `:501`.
- PATH3 (DMA): `src/lib/ps2_memory.cpp:1970-1994` `processGIFPacket`, `submitGifPacket(Path3)` at `:1989`.
- Choke point: `src/lib/ps2_memory.cpp:1946-1968` `submitGifPacket()` -> `GifArbiter::submit`
  (`src/lib/gs/ps2_gif_arbiter.cpp:21`, drain `:35`, drained from `ps2_memory.cpp:1862`).
- Sink: `src/lib/ps2_runtime.cpp:675-677` -> `m_gs.processGIFPacket`.

## Backend interface

`GSRasterBackend` pure virtuals: Initialize, Reset, Submit, LoadClut, BeginTransfer,
UploadImage, Flush, TextureFlush, Sync, Present, ClearFramebuffer, ConsumeLocalToHostBytes,
ReadVram, WriteVram, SnapshotVram, GetTransferSnapshot. Implementation:
`include/runtime/gs/gs_cpu_backend.h:10-34`, `src/lib/gs/gs_cpu_backend.cpp`. Default backend set
in the `GS` constructor (`gs_frontend.cpp:109`).

Caveat: this interface is shaped around the frontend's own draw decoding (Submit, LoadClut).
paraLLEl-GS and PCSX2's GS want the raw GIF stream and register writes instead, so the cut may
sit one level higher, at the `GS` frontend, not at the backend.

## Privileged registers, VRAM, transfers

- GS privileged range `0x12000000`, size `0x2000` (`include/runtime/ps2_memory.h:54-56`);
  `write64` at `ps2_memory.cpp:1014-1028`; CSR handled with atomics (`:100-140`).
- Frontend register decode: `gs_frontend.cpp:1062`; SIGNAL `:1464-1476`; FINISH `:1477-1486`.
- VRAM is owned by the backend (`gs_cpu_backend.h:69-70`).
- BITBLTBUF/TRXPOS/TRXREG/TRXDIR -> BeginTransfer/UploadImage (`gs_frontend.cpp:1387-1394`,
  `:1610`); local-to-local `gs_cpu_backend.cpp:1587`; local-to-host `:1627`, drained by
  ConsumeLocalToHostBytes (`:1697`).

## Display and vsync

- Present: `gs_frontend.cpp:506-550` (reads PMODE/DISPFB, Flush + Sync + Present).
- raylib upload: `src/lib/ps2_runtime.cpp` `UploadFrame` (~`:370-480`), main loop `:2482-2504`.
  A Vulkan GS would replace raylib's present path, not feed it a CPU-side RGBA copy.
- Vblank and IRQ to the EE: `src/lib/Kernel/EeScheduler.cpp:1870-1905`.

## Threading

EE, VU1, VIF, GIF and GS state changes all run on the game thread
(`ps2_runtime.cpp:2424-2443`). The raylib loop on the main thread reads the latched frame. A GPU
GS gets its own submission thread for free with this split.

## Our patches in this area

0019 (frame dump) adds a read-only observer in `UploadFrame`. 0001-0010 touch no `gs/` files;
0011-0020 were not fully checked.
