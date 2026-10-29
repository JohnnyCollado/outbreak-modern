// hook: the PS2Recomp half of the runner's external GS, SPU2, IPU, SIO2, DEV9 and settings UI. Implements PS2ExternalGS (runtime/gs/gs_external.h)
// over the plain-types bridge (ob_gs_bridge.h); PCSX2 itself lives behind pcsx2_side.cpp.
#include "runtime/gs/gs_external.h"
#include "runtime/ps2_game_source.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_pnach.h"
#include "runtime/ps2_spu2_external.h"
#include "runtime/ps2_ipu_external.h"
#include "runtime/ps2_sio2_external.h"
#include "runtime/ps2_dev9_external.h"
#include "runtime/ps2_netcnf.h"
#include "ps2_runtime.h"
#include "ps2x/iop/iso9660.h"

#include "ob_gs_bridge.h"
#include "ob_dev9_bridge.h"
#include "ob_spu2_bridge.h"
#include "ob_ipu_bridge.h"
#include "ob_sio2_bridge.h"
#include "ob_ui.h"
#include "memory_card.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
		void settingsChanged() override; // after the settings UI's launcher (PS2Recomp patch 0078)
	};

	// The outbreak.ini options PCSX2's GS opens with (patches 0046-0055), before its first open or a reopen.
	void ApplyGsSettings()
	{
		ob_gs_set_gpu_driver(ps2game::activeSettings().gpuDriver.c_str()); // outbreak.ini, PS2Recomp patch 0046
		ob_gs_set_self_read(ps2game::activeSettings().gsSelfRead.c_str());  // outbreak.ini, PS2Recomp patch 0054
		ob_gs_set_scaling(ps2game::activeSettings().scaling.c_str());       // outbreak.ini, PS2Recomp patch 0055
		// outbreak.ini upscale (PS2Recomp 0050); pnach gsaspectratio (0051), read before the runtime opens the GS.
		const std::string& aspect = ps2game::activePnach().aspect;
		ob_gs_set_display(ps2game::activeSettings().upscale,
			aspect == "16:9" ? ObGsAspect::R16_9 : aspect == "4:3" ? ObGsAspect::R4_3 : ObGsAspect::Auto);
		// outbreak.ini textures + the game's serial (0052): PCSX2 texture replacement, <textures>/<serial>/replacements.
		ob_gs_set_textures(ps2game::activeSettings().textures.string().c_str(), ps2game::activeGameSerial().c_str());
	}

	void Pcsx2ExternalGS::settingsChanged()
	{
		// The launcher opened the GS with the settings it started with; the game's first frame opens it again when
		// one it changed is among them (resolution, scaling, aspect via mods, textures).
		// The launcher opened the GS before the game, its mods (pnach aspect) and its serial (texture pack) were known:
		// reopen whenever what it opened with differs from what the game now asks for, not only after a change in
		// the launcher (a 4:3 picture and no replacements otherwise, 2026-10-26).
		ApplyGsSettings();
		obui::gsOptionsChanged(); // consumed: the comparison below covers it
		ob_gs_reopen_if_changed();
	}
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
	ApplyGsSettings();
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

namespace
{
	// A file of the disc: from the ISO image, else the extracted folder (each path part matched without case).
	bool ReadDiscFile(const std::vector<std::string>& parts, std::vector<uint8_t>& out)
	{
		const PS2Runtime::IoPaths& io = PS2Runtime::getIoPaths();
		if (!io.cdImage.empty())
		{
			std::string path;
			for (const std::string& p : parts)
				path += (path.empty() ? "" : "/") + p;
			const std::shared_ptr<ps2x::iop::IsoImage> iso = ps2x::iop::IsoImage::shared(io.cdImage);
			ps2x::iop::IsoEntry e;
			return iso && iso->find(path, e) && !e.directory && iso->readFile(e, out);
		}
		std::filesystem::path at = io.cdRoot;
		for (const std::string& part : parts)
		{
			std::error_code ec;
			bool found = false;
			for (const auto& entry : std::filesystem::directory_iterator(at, ec))
			{
				std::string a = entry.path().filename().string(), b = part;
				std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return char(std::toupper(c)); });
				std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return char(std::toupper(c)); });
				if (a == b)
				{
					at = entry.path();
					found = true;
					break;
				}
			}
			if (!found)
				return false;
		}
		std::ifstream f(at, std::ios::binary);
		out.assign(std::istreambuf_iterator<char>(f), {});
		return static_cast<bool>(f) || f.eof();
	}

	// Online play (PS2X_NETCNF set): slot 1's card gets the BWNETCNF network settings folder before the game opens it,
	// so NET CONNECTION finds "your network configuration" and the game never asks for Sony's setup program, which the
	// port cannot run (reports/2026-10-26-online-play.md entry 57). The settings files are the ones the runtime serves
	// (patch 0097); the icons are the disc's own (NTGUI2/CONF/ENGLISH, as tools/ob/mcd.py netcnf).
	void PrepareNetworkCard(const std::string& cardFolder)
	{
		const std::optional<ps2x::netcnf::NetworkConfig> config = ps2x::netcnf::configFromEnvironment(std::getenv("PS2X_NETCNF"));
		if (!config || cardFolder.empty())
			return;
		std::vector<obui::CardFile> files(2);
		files[0].name = "SYS_NET.ICO";
		files[1].name = "icon.sys";
		if (!ReadDiscFile({"NTGUI2", "CONF", "ENGLISH", "SYS_NET.ICO"}, files[0].data) ||
			!ReadDiscFile({"NTGUI2", "CONF", "ENGLISH", "ICON.SYS"}, files[1].data))
		{
			std::fprintf(stderr, "[netcnf] card: the disc's network icons (NTGUI2/CONF/ENGLISH) not found (image '%s', folder '%s'): BWNETCNF not written\n",
				PS2Runtime::getIoPaths().cdImage.string().c_str(), PS2Runtime::getIoPaths().cdRoot.string().c_str());
			return;
		}
		for (const ps2x::netcnf::SettingsFile& f : ps2x::netcnf::settingsFiles(*config))
			files.push_back({f.name, f.data});
		const std::filesystem::path card = std::filesystem::path(cardFolder) / obui::kSlotCardNames[0];
		std::string err;
		switch (obui::ensureNetworkFolder(card, files, &err))
		{
			case obui::NetworkFolder::AlreadyThere: break;
			case obui::NetworkFolder::Added: std::fprintf(stderr, "[netcnf] card: BWNETCNF added (%s)\n", config->dhcp ? "dhcp" : "static"); break;
			case obui::NetworkFolder::FormattedAndAdded:
				std::fprintf(stderr, "[netcnf] card: formatted, BWNETCNF added (%s)\n", config->dhcp ? "dhcp" : "static");
				break;
			case obui::NetworkFolder::Failed: std::fprintf(stderr, "[netcnf] card: BWNETCNF not written: %s\n", err.c_str()); break;
		}
		std::fflush(stderr);
	}
} // namespace

// ---- SIO2 + memory cards (PS2Recomp patch 0071): PCSX2's SIO2 behind native/pcsx2_sio2/adapter/ob_sio2_bridge.h ----
namespace
{
	// The card opens during the runtime's start, before it knows the disc (PS2Runtime::setIoPaths comes later), and
	// BWNETCNF needs the disc's icons: so the card is prepared at the game's first card access, closed and reopened.
	class Pcsx2Sio2 final : public ps2x::iop::Sio2Device
	{
	public:
		Pcsx2Sio2(std::string folder, bool createSlot1) : m_folder(std::move(folder)), m_createSlot1(createSlot1) {}
		~Pcsx2Sio2() override { ob_sio2_close(); }
		uint32_t read32(uint32_t phys) override { Ready(); return ob_sio2_read32(phys); }
		void write32(uint32_t phys, uint32_t value) override { Ready(); ob_sio2_write32(phys, value); }
		uint8_t read8(uint32_t phys) override { Ready(); return ob_sio2_read8(phys); }
		void write8(uint32_t phys, uint8_t value) override { Ready(); ob_sio2_write8(phys, value); }
		uint32_t dma(uint32_t channel, uint8_t* iopRam, uint32_t ramSize, uint32_t madr, uint32_t bcr, uint32_t chcr) override
		{
			Ready();
			return ob_sio2_dma(channel, iopRam, ramSize, madr, bcr, chcr);
		}
		bool takeInterrupt() override { return ob_sio2_take_interrupt(); }
		// The card driver numbers the card slots after the two pad ports (sio2man ports 2/3); bit 0 is the slot.
		bool authCard(uint32_t port, uint32_t) override { Ready(); return ob_sio2_card_present(port & 1u); }

	private:
		void Ready()
		{
			if (m_ready)
				return;
			m_ready = true;
			if (!std::getenv("PS2X_NETCNF"))
				return;
			ob_sio2_close();
			PrepareNetworkCard(m_folder);
			if (!ob_sio2_open(m_folder.c_str(), m_createSlot1))
				std::fprintf(stderr, "[netcnf] card: reopening %s failed\n", m_folder.c_str());
		}
		std::string m_folder;
		bool m_createSlot1;
		bool m_ready = false;
	};
	static_assert(OB_SIO2_DMA_IGNORED == ps2x::iop::kSio2DmaIgnored);
} // namespace

#if defined(__ANDROID__)
extern "C" void ObHostJniKeep(); // host_jni.cpp: keeps the launcher's host/search JNI functions in the library
#endif

ps2x::iop::Sio2Device* ps2CreateExternalSio2(const std::string& cardFolder, bool createSlot1)
{
#if defined(__ANDROID__)
	ObHostJniKeep();
#endif
	if (!ob_sio2_open(cardFolder.c_str(), createSlot1))
		return nullptr;
	return new Pcsx2Sio2(cardFolder, createSlot1);
}

// ---- DEV9 network adapter (PS2X_DEV9): PCSX2's DEV9 + Sockets adapter behind native/pcsx2_dev9/adapter/ob_dev9_bridge.h ----
namespace
{
	class Pcsx2Dev9 final : public ps2x::iop::Dev9Device
	{
	public:
		~Pcsx2Dev9() override { ob_dev9_close(); }
		uint8_t read8(uint32_t phys) override { return ob_dev9_read8(phys); }
		uint16_t read16(uint32_t phys) override { return ob_dev9_read16(phys); }
		uint32_t read32(uint32_t phys) override { return ob_dev9_read32(phys); }
		void write8(uint32_t phys, uint8_t value) override { ob_dev9_write8(phys, value); }
		void write16(uint32_t phys, uint16_t value) override { ob_dev9_write16(phys, value); }
		void write32(uint32_t phys, uint32_t value) override { ob_dev9_write32(phys, value); }
		bool dma(uint8_t* iopRam, uint32_t ramSize, uint32_t madr, uint32_t bcr, uint32_t chcr) override
		{
			return ob_dev9_dma(iopRam, ramSize, madr, bcr, chcr) != OB_DEV9_DMA_IGNORED;
		}
		void advance(uint64_t cycle) override
		{
			if (cycle > m_cycle)
				ob_dev9_advance(static_cast<uint32_t>(std::min<uint64_t>(cycle - m_cycle, UINT32_MAX)));
			m_cycle = std::max(m_cycle, cycle);
		}
		// Frames arrive from the adapter's own thread at any time (PCSX2 checks on every IOP counter update): look
		// every kPollCycles of IOP time, ~0.25 ms; DEV9's own interrupt delays (0-100 cycles) land on the next look.
		uint64_t nextEventCycle() override { return m_cycle + kPollCycles; }
		uint32_t takeEvents() override
		{
			uint32_t events = 0;
			if (ob_dev9_take_interrupt())
				events |= EventIrq;
			if (ob_dev9_take_dma_done())
				events |= EventDma8;
			return events;
		}

	private:
		static constexpr uint64_t kPollCycles = 9216; // 36,864,000 IOP cycles per second / 4000
		uint64_t m_cycle = 0;
	};
} // namespace

ps2x::iop::Dev9Device* ps2CreateExternalDev9(const std::string& interfaceName, const std::vector<PS2Dev9Host>& hosts,
	std::string* error)
{
	std::vector<ob_dev9_host> list;
	for (const PS2Dev9Host& h : hosts)
		list.push_back(ob_dev9_host{h.name.c_str(), {h.address[0], h.address[1], h.address[2], h.address[3]}});
	// Destinations the host socket reaches elsewhere, PS2X_NET_REMAP ("192.0.2.2=127.0.0.1" for this device's own
	// server: the game cannot use 127.x, reports/2026-10-26-online-play.md entry 34; upstream/pcsx2 0003).
	ob_dev9_remap remaps[16];
	uint32_t bad = 0;
	const uint32_t remapCount = ob_dev9_parse_remaps(std::getenv("PS2X_NET_REMAP"), remaps, 16, &bad);
	if (bad)
		std::fprintf(stderr, "[dev9] PS2X_NET_REMAP: %u entr%s ignored (from[:port]=to[:port])\n", bad, bad == 1 ? "y" : "ies");
	// The adapter's messages (DNS host-list answers, socket errors) -> pcsx2-dev9.log in the working dir, as PCSX2's
	// own log goes to pcsx2-gs.log (pcsx2_side.cpp) when that one is open first.
	ob_dev9_config config{interfaceName.c_str(), list.data(), static_cast<uint32_t>(list.size()), "pcsx2-dev9.log",
		remaps, remapCount, {}};
	// A fan server's DNS, PS2X_NET_DNS=a.b.c.d (the game's DNS1, as PCSX2 players set it; report entry 65).
	if (const char* dns = std::getenv("PS2X_NET_DNS"); dns && *dns)
	{
		unsigned a = 0, b = 0, c = 0, d = 0;
		char tail = 0;
		if (std::sscanf(dns, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4 && (a | b | c | d) <= 255)
		{
			const uint8_t bytes[4] = {uint8_t(a), uint8_t(b), uint8_t(c), uint8_t(d)};
			std::memcpy(config.dns1, bytes, 4);
			std::fprintf(stderr, "[dev9] DNS server %s (PS2X_NET_DNS)\n", dns);
		}
		else
			std::fprintf(stderr, "[dev9] PS2X_NET_DNS=%s ignored (a.b.c.d)\n", dns);
	}
	if (!ob_dev9_open(&config))
	{
		ob_dev9_close();
		if (error)
			*error = "no usable network interface" + (interfaceName.empty() ? std::string() : " named '" + interfaceName + "'");
		return nullptr;
	}
	return new Pcsx2Dev9();
}
