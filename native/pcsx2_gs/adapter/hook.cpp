// hook: the PS2Recomp half of the runner's external GS and SPU2. Implements PS2ExternalGS (runtime/gs/gs_external.h)
// over the plain-types bridge (ob_gs_bridge.h); PCSX2 itself lives behind pcsx2_side.cpp.
#include "runtime/gs/gs_external.h"
#include "runtime/ps2_game_source.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_pnach.h"
#include "runtime/ps2_spu2_external.h"
#include "runtime/ps2_ipu_external.h"

#include "ob_gs_bridge.h"
#include "ob_spu2_bridge.h"
#include "ob_ipu_bridge.h"

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
	ob_gs_set_gpu_driver(ps2game::activeSettings().gpuDriver.c_str()); // outbreak.ini, PS2Recomp patch 0046
	ob_gs_set_self_read(ps2game::activeSettings().gsSelfRead.c_str());  // outbreak.ini, PS2Recomp patch 0054
	ob_gs_set_scaling(ps2game::activeSettings().scaling.c_str());       // outbreak.ini, PS2Recomp patch 0055
	// outbreak.ini upscale (PS2Recomp 0050); pnach gsaspectratio (0051), read before the runtime opens the GS.
	const std::string& aspect = ps2game::activePnach().aspect;
	ob_gs_set_display(ps2game::activeSettings().upscale,
		aspect == "16:9" ? ObGsAspect::R16_9 : aspect == "4:3" ? ObGsAspect::R4_3 : ObGsAspect::Auto);
	// outbreak.ini textures + the game's serial (0052): PCSX2 texture replacement, <textures>/<serial>/replacements.
	ob_gs_set_textures(ps2game::activeSettings().textures.string().c_str(), ps2game::activeGameSerial().c_str());
	return new Pcsx2ExternalGS();
}

// ---- External SPU2 (PS2Recomp patch 0056): PCSX2's SPU2 (native/pcsx2_spu2) as the IOP's Spu2Device ----
namespace
{
	class Pcsx2Spu2 final : public ps2x::iop::Spu2Device
	{
	public:
		~Pcsx2Spu2() override { ob_spu2_close(); }
		uint16_t read16(uint32_t phys, uint64_t cycle) override { return ob_spu2_read(phys, cycle); }
		void write16(uint32_t phys, uint16_t value, uint64_t cycle) override { ob_spu2_write(phys, value, cycle); }
		void dma(uint32_t core, uint32_t madr, uint32_t halfwords, bool toSpu2, uint64_t cycle) override
		{
			ob_spu2_dma(core, madr, halfwords, toSpu2, cycle);
		}
		void advance(uint64_t cycle) override { ob_spu2_advance(cycle); }
		uint64_t nextEventCycle() override { return ob_spu2_next_event(); }
		uint32_t takeEvents() override { return ob_spu2_take_events(); } // OB_SPU2_EV_* = Spu2Device::Event bits
	};
	static_assert(OB_SPU2_EV_IRQ == ps2x::iop::Spu2Device::EventIrq && OB_SPU2_EV_DMA4 == ps2x::iop::Spu2Device::EventDma4 &&
		OB_SPU2_EV_DMA7 == ps2x::iop::Spu2Device::EventDma7);
} // namespace

ps2x::iop::Spu2Device* ps2CreateExternalSpu2(uint8_t* iopRam, uint32_t ramSize)
{
	if (!ob_spu2_open(iopRam, ramSize, 0))
		return nullptr;
	return new Pcsx2Spu2();
}

void ps2ExternalSpu2Pull(float* frames, uint32_t count)
{
	ob_spu2_pull(frames, count);
}

uint32_t ps2ExternalSpu2SampleRate()
{
	return ob_spu2_sample_rate();
}

// ---- IPU (PS2Recomp patch 0060): PCSX2's IPU behind native/pcsx2_ipu/adapter/ob_ipu_bridge.h ----
namespace
{
	class Pcsx2Ipu final : public ps2x::IpuDevice
	{
	public:
		~Pcsx2Ipu() override { ob_ipu_close(); }
		uint32_t read32(uint32_t address, uint64_t cycle) override { return ob_ipu_read32(address, cycle); }
		void write32(uint32_t address, uint32_t value, uint64_t cycle) override { ob_ipu_write32(address, value, cycle); }
		void fifoRead(uint32_t quad[4], uint64_t cycle) override { ob_ipu_fifo_read(quad, cycle); }
		void fifoWrite(const uint32_t quad[4], uint64_t cycle) override { ob_ipu_fifo_write(quad, cycle); }
		uint32_t dmaRead(uint32_t address) override { return ob_ipu_dma_read(address); }
		void dmaWrite(uint32_t address, uint32_t value, uint64_t cycle) override { ob_ipu_dma_write(address, value, cycle); }
		void dmacState(bool enabled, bool suspended, uint64_t cycle) override { ob_ipu_dmac_state(enabled, suspended, cycle); }
		void advance(uint64_t cycle) override { ob_ipu_advance(cycle); }
		uint64_t nextEvent() override { return ob_ipu_next_event(); }
		uint32_t takeEvents() override { return ob_ipu_take_events(); } // OB_IPU_EV_* = IpuDevice::Event bits
	};
	static_assert(OB_IPU_EV_INTC == ps2x::IpuDevice::EventIntc && OB_IPU_EV_DMA3 == ps2x::IpuDevice::EventDma3 &&
		OB_IPU_EV_DMA4 == ps2x::IpuDevice::EventDma4);
} // namespace

ps2x::IpuDevice* ps2CreateExternalIpu(uint8_t* rdram, uint32_t ramSize, uint8_t* scratchpad, uint32_t scratchpadSize)
{
	if (!ob_ipu_open(rdram, ramSize, scratchpad, scratchpadSize, 0))
		return nullptr;
	return new Pcsx2Ipu();
}
