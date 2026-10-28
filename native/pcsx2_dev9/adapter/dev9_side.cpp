// dev9_side: PCSX2's DEV9 + SMAP + Sockets adapter behind ob_dev9_bridge.h. It stands in for the parts of PCSX2's IOP
// the DEV9 touches: the address decode (IopMem.cpp: the 0x1000xxxx page at every width; ps2/Iop/IopHwRead.cpp /
// IopHwWrite.cpp: HW_DEV9_DATA 0x1F80146E), DMA 8 (IopDma.cpp psxDma8 / psxDMA8Interrupt), the interrupt
// (IopIrq.cpp dev9Irq = PSX_INT(IopEvt_DEV9, cycles), then dev9Interrupt: iopIntcIrq(13) when DEV9irqHandler says so)
// and the time tick (IopCounters.cpp: DEV9async(1) on every counter update), and sets the network up as PCSX2's
// settings would for "Ethernet: Sockets, DNS: Internal, hosts" (Config.h DEV9Options).
#include "ob_dev9_bridge.h"

#include "Config.h"
#include "DEV9/DEV9.h"
#include "common/Console.h"

#include <cstring>

namespace
{
	bool s_open = false;
	bool s_irq = false;     // INTC line 13 raised, not yet taken
	bool s_dmaDone = false; // channel 8 ended, not yet taken
	// dev9Irq(cycles): IOP cycles left until DEV9's interrupt event fires; < 0 = none pending (PSX_INT keeps one event
	// per source, the later call replacing the earlier).
	int64_t s_irqIn = -1;

	void FireIrqEvent()
	{
		s_irqIn = -1;
		if (DEV9irqHandler() == 1)
			s_irq = true;
	}
} // namespace

// IopIrq.cpp: DEV9 asks for its interrupt in `cycles` IOP cycles (PSX_INT(IopEvt_DEV9, cycles)).
void dev9Irq(int cycles)
{
	s_irqIn = cycles < 0 ? 0 : cycles;
}

// IopDma.cpp: the end of a channel-8 transfer (CHCR busy bit cleared, DMA interrupt 2 bit 1 on the IOP side).
void psxDMA8Interrupt()
{
	s_dmaDone = true;
}

bool ob_dev9_open(const ob_dev9_config* config)
{
	if (s_open)
		ob_dev9_close();
	auto& d = EmuConfig.DEV9;
	d = Pcsx2Config::DEV9Options();
	d.EthEnable = true;
	d.EthApi = Pcsx2Config::DEV9Options::NetApi::Sockets;
	d.EthDevice = (config && config->interface_name && config->interface_name[0]) ? config->interface_name : "Auto";
	d.InterceptDHCP = true;
	d.AutoMask = true;
	d.AutoGateway = true;
	d.ModeDNS1 = Pcsx2Config::DEV9Options::DnsMode::Internal;
	d.ModeDNS2 = Pcsx2Config::DEV9Options::DnsMode::Internal;
	d.HddEnable = false;
	d.EthHosts.clear();
	for (uint32_t i = 0; config && i < config->host_count; i++)
	{
		Pcsx2Config::DEV9Options::HostEntry e{};
		std::string name = config->hosts[i].name ? config->hosts[i].name : "";
		// DNS_Server.cpp lowers the question's name but not the stored one (:183): store it lowered.
		for (char& c : name)
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		e.Url = name;
		e.Desc = name;
		std::memcpy(e.Address, config->hosts[i].address, 4);
		e.Enabled = true;
		d.EthHosts.push_back(e);
	}
	// DEV9's messages go through PCSX2's one log file (common/Console.cpp); open it only if nothing else has. Info
	// level: DNS host-list answers, session and socket errors; not DevCon's line per packet.
	if (config && config->log_path && config->log_path[0] && !Log::IsFileOutputEnabled())
		Log::SetFileOutputLevel(LOGLEVEL_INFO, config->log_path);
	s_irq = s_dmaDone = false;
	s_irqIn = -1;
	if (DEV9init() != 0)
		return false;
	DEV9open();
	s_open = true;
	// InitNet clears EthEnable when no adapter starts (net.cpp InitNet).
	return d.EthEnable;
}

void ob_dev9_close()
{
	if (!s_open)
		return;
	DEV9close();
	DEV9shutdown();
	s_open = false;
}

bool ob_dev9_network_up()
{
	return s_open && EmuConfig.DEV9.EthEnable;
}

uint8_t ob_dev9_read8(uint32_t phys) { return s_open ? DEV9read8(phys) : 0; }
uint16_t ob_dev9_read16(uint32_t phys) { return s_open ? DEV9read16(phys) : 0; }
uint32_t ob_dev9_read32(uint32_t phys) { return s_open ? DEV9read32(phys) : 0; }

void ob_dev9_write8(uint32_t phys, uint8_t value)
{
	if (s_open)
		DEV9write8(phys, value);
}

void ob_dev9_write16(uint32_t phys, uint16_t value)
{
	if (s_open)
		DEV9write16(phys, value);
}

void ob_dev9_write32(uint32_t phys, uint32_t value)
{
	if (s_open)
		DEV9write32(phys, value);
}

uint32_t ob_dev9_dma(uint8_t* iop_ram, uint32_t ram_size, uint32_t madr, uint32_t bcr, uint32_t chcr)
{
	const uint64_t bytes = uint64_t(bcr >> 16) * (bcr & 0xFFFFu) * 4u;
	const uint32_t addr = madr & 0x1FFFFCu;
	if (!s_open || addr + bytes > ram_size)
		return OB_DEV9_DMA_IGNORED;
	switch (chcr & 0x01000201u)
	{
		case 0x01000201u: // IOP RAM to DEV9
			DEV9writeDMA8Mem(reinterpret_cast<u32*>(iop_ram + addr), static_cast<int>(bytes));
			return 0;
		case 0x01000200u: // DEV9 to IOP RAM
			DEV9readDMA8Mem(reinterpret_cast<u32*>(iop_ram + addr), static_cast<int>(bytes));
			return 0;
		default:
			return OB_DEV9_DMA_IGNORED;
	}
}

void ob_dev9_advance(uint32_t iop_cycles)
{
	if (!s_open)
		return;
	DEV9async(iop_cycles);
	if (s_irqIn >= 0)
	{
		s_irqIn -= iop_cycles;
		if (s_irqIn <= 0)
			FireIrqEvent();
	}
}

bool ob_dev9_take_interrupt()
{
	const bool r = s_irq;
	s_irq = false;
	return r;
}

bool ob_dev9_take_dma_done()
{
	const bool r = s_dmaDone;
	s_dmaDone = false;
	return r;
}
