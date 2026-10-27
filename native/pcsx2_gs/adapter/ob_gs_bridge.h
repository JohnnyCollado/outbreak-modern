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
// outbreak.ini gs_self_read ("auto", empty, "copy", "barrier", "fetch"; PS2Recomp patch 0054), before the first GS
// call: how PCSX2's renderer reads the colour already drawn (gpu_driver.h DecideSelfRead). Auto = a copy of the target
// on Qualcomm's own Adreno driver, PCSX2's choice elsewhere (always on desktop, where no driver is probed).
void ob_gs_set_self_read(const char* mode);
// Before the first GS call: render resolution multiplier (outbreak.ini upscale, 1..8; 0 = unset = the platform
// default: 3 on Android, 1 elsewhere; the Vulkan renderer only) and display aspect (ObGsAspect; a pnach
// gsaspectratio, default Auto).
enum class ObGsAspect : int
{
	Auto, // PCSX2's Auto 4:3/3:2
	R4_3,
	R16_9,
};
void ob_gs_set_display(uint32_t upscale, ObGsAspect aspect);
// Before the first GS call: outbreak.ini scaling ("smooth", "sharp", empty = the platform default: smooth on
// Android, sharp elsewhere; PS2Recomp patch 0055): PCSX2's LinearPresent, bilinear or nearest.
void ob_gs_set_scaling(const char* mode);
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

// ---- Settings menu (native/ob_ui; PS2Recomp patch 0078) ----
// Main thread, while the game is not drawing (the launcher, or the game held): opens the GS if needed, runs build()
// under the GS lock inside the current ImGui frame, then presents the last picture (black before the first) with the
// UI on top. False when the GS cannot open.
bool ob_gs_ui_present(void (*build)(void* user), void* user);
// Main thread, after the settings file was read again (the launcher): closes the GS now if it is open; the next GS
// call opens it with the options set since. Android keeps the Vulkan driver it chose first (one per process).
void ob_gs_reopen();
// Vsync, live: 0 = off, 1 = on (Mailbox, the default), 2 = strict (FIFO). Only for a visible window.
void ob_gs_set_vsync(int mode);
// A small "NN.N fps" readout (guest vsyncs per second) in the top-left corner of every frame. Off by default.
void ob_gs_set_fps_overlay(bool on);
// Windows' menu combo (Back + Start held 5 s): a ring filling clockwise around a gear, top left (below the FPS
// readout), drawn in every present while progress > 0 (0..1; 0 = hidden). Main thread, each host frame.
void ob_gs_set_hold_progress(float progress);
// The menu's font with the Segoe icons merged (an ImFont*), for the ring's gear; set once its fonts are loaded.
void ob_gs_set_overlay_icon_font(void* imfont);
// Main thread, once per host frame. Android: false while the app is in the background (no window: do not present,
// hold the game); when the window comes back as a new one, moves PCSX2's swapchain onto it first. Desktop: true.
bool ob_gs_window_ready();
// Main thread, after the ob_gs_set_* calls for the game (PS2ExternalGS::settingsChanged): closes the GS when it is
// open with other options than these (aspect, upscale, scaling, textures, serial), so the game's first GS call opens
// it again with them. True when it closed it.
bool ob_gs_reopen_if_changed();
// Main thread, Windows: borderless on (frameless, the monitor's size) or off (the frame and rectangle from before)
// for the runner window parent (HWND). Resizes the GS child to match. No-op elsewhere.
void ob_gs_set_borderless(void* parent, bool on);
// Main thread, Windows: switches the window's monitor to w x h for fullscreen (temporary, CDS_FULLSCREEN); 0 x 0 =
// back to the desktop mode. False when Windows refused the mode. No-op (false) elsewhere.
bool ob_gs_set_display_mode(void* parent, uint32_t w, uint32_t h);
