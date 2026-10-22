// hook: the PS2Recomp half of the runner's external GS. Implements PS2ExternalGS (runtime/gs/gs_external.h)
// over the plain-types bridge (ob_gs_bridge.h); PCSX2 itself lives behind pcsx2_side.cpp.
#include "runtime/gs/gs_external.h"
#include "runtime/ps2_memory.h"

#include "ob_gs_bridge.h"

#include <cstring>
#include <string>

namespace
{
	class Pcsx2ExternalGS final : public PS2ExternalGS
	{
	public:
		void gifPacket(GifPathId path, const uint8_t* data, uint32_t sizeBytes) override
		{
			ob_gs_gif(static_cast<uint32_t>(path), data, sizeBytes);
		}
		void privWrite(uint32_t offset, uint64_t value) override { ob_gs_priv_write(offset, value); }
		void vsync(uint64_t, const GSRegisters& r) override
		{
			const uint64_t priv[15] = {r.pmode, r.smode1, r.smode2, r.srfsh, r.synch1, r.synch2, r.syncv, r.dispfb1,
				r.display1, r.dispfb2, r.display2, r.extbuf, r.extdata, r.extwrite, r.bgcolor};
			ob_gs_vsync(priv, r.csr.load(), r.imr, r.busdir, r.siglblid);
		}
		uint32_t readLocalToHost(uint8_t* dst, uint32_t maxBytes) override { return ob_gs_read_local(dst, maxBytes); }
		void reset() override { ob_gs_reset(); }
		bool snapshot(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height) override
		{
			uint32_t w = 0, h = 0;
			const uint8_t* p = ob_gs_snapshot(&w, &h);
			if (!p)
				return false;
			rgba.assign(p, p + (size_t)w * h * 4);
			width = w;
			height = h;
			return true;
		}
		void attachWindow(void* nativeHandle, uint32_t w, uint32_t h, bool visible) override
		{
			ob_gs_attach_window(nativeHandle, w, h, visible);
		}
		void windowResized(uint32_t w, uint32_t h) override { ob_gs_window_resized(w, h); }
	};
} // namespace

PS2ExternalGS* ps2CreateExternalGS(const char* mode)
{
	const std::string m = mode ? mode : "";
	if (m == "pcsx2-sw")
		ob_gs_init(ObGsRenderer::SW);
	else if (m == "pcsx2-vk")
		ob_gs_init(ObGsRenderer::VK);
	else
		return nullptr;
	return new Pcsx2ExternalGS();
}
