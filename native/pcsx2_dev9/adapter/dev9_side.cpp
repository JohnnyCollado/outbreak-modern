// dev9_side: PCSX2's DEV9 + SMAP + Sockets adapter behind ob_dev9_bridge.h. It stands in for the parts of PCSX2's IOP
// the DEV9 touches: the address decode (IopMem.cpp: the 0x1000xxxx page at every width; ps2/Iop/IopHwRead.cpp /
// IopHwWrite.cpp: HW_DEV9_DATA 0x1F80146E), DMA 8 (IopDma.cpp psxDma8 / psxDMA8Interrupt), the interrupt
// (IopIrq.cpp dev9Irq = PSX_INT(IopEvt_DEV9, cycles), then dev9Interrupt: iopIntcIrq(13) when DEV9irqHandler says so)
// and the time tick (IopCounters.cpp: DEV9async(1) on every counter update), and sets the network up as PCSX2's
// settings would for "Ethernet: Sockets, DNS: Internal, hosts" (Config.h DEV9Options).
#include "ob_dev9_bridge.h"

#include "Config.h"
#include "DEV9/DEV9.h"
#include "DEV9/Sessions/HostRemap.h"
#include "common/Console.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	bool s_open = false;
	bool s_irq = false;     // INTC line 13 raised, not yet taken
	bool s_dmaDone = false; // channel 8 ended, not yet taken
	// dev9Irq(cycles): IOP cycles left until DEV9's interrupt event fires; < 0 = none pending (PSX_INT keeps one event
	// per source, the later call replacing the earlier). Counted down by the IOP thread only; dev9Irq also comes from
	// PCSX2's receive thread (net.cpp NetRxThread -> rx_process), so it posts into s_irqAsk, which the IOP thread takes
	// (security review C3: both threads wrote s_irqIn, and a request landing mid-countdown could be lost).
	int64_t s_irqIn = -1;
	std::atomic<int64_t> s_irqAsk{-1};

	// The latency trace (ob_dev9_set_trace): when the pending interrupt was asked for (0 = none), and the totals.
	// dev9Irq comes from the receive thread too (net.cpp NetRxThread -> rx_process), hence the atomic.
	bool s_trace = false;
	std::atomic<int64_t> s_irqAskedUs{0};
	uint32_t s_waits = 0, s_windowWaits = 0, s_windowOverFrame = 0; // over one frame: 16.7 ms
	double s_longestMs = 0.0, s_windowSumMs = 0.0, s_windowLongestMs = 0.0;
	int64_t s_windowStartUs = 0;

	int64_t NowUs()
	{
		return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	void TimeTakenIrq()
	{
		const int64_t asked = s_irqAskedUs.exchange(0);
		if (!asked)
			return;
		const int64_t now = NowUs();
		const double ms = (now - asked) / 1000.0;
		s_waits++;
		s_longestMs = std::max(s_longestMs, ms);
		s_windowWaits++;
		if (ms > 1000.0 / 60.0)
			s_windowOverFrame++;
		s_windowSumMs += ms;
		s_windowLongestMs = std::max(s_windowLongestMs, ms);
		if (!s_windowStartUs)
			s_windowStartUs = now;
		if (now - s_windowStartUs >= 10'000'000)
		{
			std::printf("[dev9] interrupt wait: %u in 10 s, average %.2f ms, longest %.2f ms, %u over a frame\n",
				s_windowWaits, s_windowSumMs / s_windowWaits, s_windowLongestMs, s_windowOverFrame);
			std::fflush(stdout);
			s_windowWaits = s_windowOverFrame = 0;
			s_windowSumMs = s_windowLongestMs = 0.0;
			s_windowStartUs = now;
		}
	}

	void FireIrqEvent()
	{
		s_irqIn = -1;
		if (DEV9irqHandler() == 1)
			s_irq = true;
		else
			s_irqAskedUs = 0; // masked or no cause: nothing to take, so the trace's clock does not run on
	}
} // namespace

// IopIrq.cpp: DEV9 asks for its interrupt in `cycles` IOP cycles (PSX_INT(IopEvt_DEV9, cycles)).
void dev9Irq(int cycles)
{
	s_irqAsk.store(cycles < 0 ? 0 : cycles);
	if (s_trace)
	{
		int64_t none = 0;
		s_irqAskedUs.compare_exchange_strong(none, NowUs()); // the first ask since the last take
	}
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
	if (const char* trace = std::getenv("PS2X_NET_TRACE"); trace && trace[0] == '1')
		ob_dev9_set_trace(true);
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
	if (config && (config->dns1[0] | config->dns1[1] | config->dns1[2] | config->dns1[3]))
	{
		d.ModeDNS1 = Pcsx2Config::DEV9Options::DnsMode::Manual;
		std::memcpy(d.DNS1, config->dns1, 4);
	}
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
	// Destinations the host socket reaches elsewhere, and from which address (upstream/pcsx2 0003-0004,
	// TCP_Session_Out.cpp SendConnect).
	std::vector<Sessions::HostRemap::Entry> remaps;
	for (uint32_t i = 0; config && i < config->remap_count; i++)
	{
		const ob_dev9_remap& r = config->remaps[i];
		Sessions::HostRemap::Entry e{};
		std::memcpy(e.from.bytes, r.from, 4);
		e.fromPort = r.from_port;
		std::memcpy(e.to.bytes, r.to, 4);
		e.toPort = r.to_port;
		std::memcpy(e.source.bytes, r.source, 4);
		remaps.push_back(e);
	}
	Sessions::HostRemap::Set(std::move(remaps));
	s_irq = s_dmaDone = false;
	s_irqIn = -1;
	s_irqAsk = -1;
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
	if (const int64_t ask = s_irqAsk.exchange(-1); ask >= 0)
		s_irqIn = ask; // the latest request replaces a pending one, as PSX_INT
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
	if (r && s_trace)
		TimeTakenIrq();
	return r;
}

void ob_dev9_set_trace(bool on)
{
	s_trace = on;
	s_irqAskedUs = 0;
	s_waits = s_windowWaits = s_windowOverFrame = 0;
	s_longestMs = s_windowSumMs = s_windowLongestMs = 0.0;
	s_windowStartUs = 0;
}

void ob_dev9_irq_wait_stats(uint32_t* count, double* longest_ms)
{
	if (count)
		*count = s_waits;
	if (longest_ms)
		*longest_ms = s_longestMs;
}

bool ob_dev9_take_dma_done()
{
	const bool r = s_dmaDone;
	s_dmaDone = false;
	return r;
}

// "a.b.c.d[:port]": the address and the port (0 when absent); false when malformed.
static bool ParseEndpoint(const std::string& s, uint8_t ip[4], uint16_t* port)
{
	unsigned a, b, c, d, p = 0;
	char tail = 0;
	const size_t colon = s.find(':');
	const std::string addr = s.substr(0, colon);
	if (std::sscanf(addr.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 ||
		d > 255)
		return false;
	if (colon != std::string::npos &&
		(std::sscanf(s.c_str() + colon + 1, "%u%c", &p, &tail) != 1 || p == 0 || p > 65535))
		return false;
	ip[0] = static_cast<uint8_t>(a);
	ip[1] = static_cast<uint8_t>(b);
	ip[2] = static_cast<uint8_t>(c);
	ip[3] = static_cast<uint8_t>(d);
	*port = static_cast<uint16_t>(p);
	return true;
}

uint32_t ob_dev9_parse_remaps(const char* text, ob_dev9_remap* out, uint32_t max, uint32_t* bad)
{
	uint32_t n = 0, skipped = 0;
	std::string all = text ? text : "";
	size_t start = 0;
	while (start <= all.size())
	{
		const size_t end = std::min(all.find(';', start), all.size());
		std::string entry = all.substr(start, end - start);
		entry.erase(0, entry.find_first_not_of(' '));
		entry.erase(entry.find_last_not_of(' ') + 1);
		start = end + 1;
		if (entry.empty())
			continue;
		const size_t eq = entry.find('=');
		const size_t at = entry.find('@');
		ob_dev9_remap r{};
		uint16_t sourcePort = 0;
		const bool sourceOk = at == std::string::npos ||
			(at > eq && ParseEndpoint(entry.substr(at + 1), r.source, &sourcePort) && sourcePort == 0);
		if (eq == std::string::npos || n >= max || !sourceOk ||
			!ParseEndpoint(entry.substr(0, eq), r.from, &r.from_port) ||
			!ParseEndpoint(entry.substr(eq + 1, at == std::string::npos ? std::string::npos : at - eq - 1), r.to,
				&r.to_port))
		{
			skipped++;
			continue;
		}
		out[n++] = r;
	}
	if (bad)
		*bad = skipped;
	return n;
}
