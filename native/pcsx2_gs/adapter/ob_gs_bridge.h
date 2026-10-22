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
// path 1..3. Only whole GIFtag units reach PCSX2; a split tail is carried per path to the next call.
void ob_gs_gif(uint32_t path, const uint8_t* data, uint32_t size_bytes);
void ob_gs_priv_write(uint32_t offset, uint64_t value);
// priv: pmode..bgcolor (15 regs, basemem 0x00..0xE0 step 0x10).
void ob_gs_vsync(const uint64_t priv[15], uint64_t csr, uint64_t imr, uint64_t busdir, uint64_t siglblid);
uint32_t ob_gs_read_local(uint8_t* dst, uint32_t max_bytes);
void ob_gs_reset();
// RGBA8 (alpha 0xFF) of the current display; pointer valid until the next call. Null on failure.
const uint8_t* ob_gs_snapshot(uint32_t* width, uint32_t* height);
