// ob_gs_bridge: plain-types seam between hook.cpp (PS2Recomp side) and pcsx2_side.cpp (PCSX2 side).
// No PCSX2 or PS2Recomp includes here: each side keeps its own defines, options and exception model.
#pragma once

#include <cstddef>
#include <cstdint>

enum class ObGsRenderer : int
{
	SW,
	VK,
};

// Records the renderer; GSopen happens lazily on the first ob_gs_* call below (except ob_gs_reset).
void ob_gs_init(ObGsRenderer renderer);
// outbreak.ini gpu_driver ("system", empty, or a driver folder name), before the first GS call. Android picks the
// Vulkan driver from it at GSopen (gpu_driver_android.h); other platforms ignore it.
void ob_gs_set_gpu_driver(const char* name);
// Before the first GS call: render resolution multiplier (outbreak.ini upscale, 1..8; others = 1; the Vulkan
// renderer only) and display aspect (ObGsAspect; a pnach gsaspectratio). Both default to today's: 1, Auto.
enum class ObGsAspect : int
{
	Auto, // PCSX2's Auto 4:3/3:2
	R4_3,
	R16_9,
};
void ob_gs_set_display(uint32_t upscale, ObGsAspect aspect);
// Before the first GS call: texture replacement root (outbreak.ini textures; PCSX2 layout
// <root>/<serial>/replacements) and the game's serial. Empty root = off. Vulkan renderer only; Android: ignored.
void ob_gs_set_textures(const char* root, const char* serial);
// path 1..3. Only whole GIFtag units reach PCSX2; a split tail is carried per path to the next call.
void ob_gs_gif(uint32_t path, const uint8_t* data, uint32_t size_bytes);
void ob_gs_priv_write(uint32_t offset, uint64_t value);
// priv: pmode..bgcolor (15 regs, basemem 0x00..0xE0 step 0x10).
void ob_gs_vsync(const uint64_t priv[15], uint64_t csr, uint64_t imr, uint64_t busdir, uint64_t siglblid);
uint32_t ob_gs_read_local(uint8_t* dst, uint32_t max_bytes);
void ob_gs_reset();
// RGBA8 (alpha 0xFF) of the current display; pointer valid until the next call. Null on failure.
const uint8_t* ob_gs_snapshot(uint32_t* width, uint32_t* height);
// Main thread, before the game thread starts: PCSX2 presents into a child of this native window (HWND).
// visible picks the present mode (Mailbox) vs Disabled for a hidden window.
void ob_gs_attach_window(void* hwnd, uint32_t w, uint32_t h, bool visible);
// Main thread: resizes the child now; PCSX2 sees the new size at the next executed ob_gs_vsync (GS thread).
void ob_gs_window_resized(uint32_t w, uint32_t h);
// 1 while the GS thread runs (PS2X_GS_THREAD unset/1, from the first command until the producer thread exits);
// 0 inline. Call from the producer thread, or after it has exited (tests).
int ob_gs_threaded();
