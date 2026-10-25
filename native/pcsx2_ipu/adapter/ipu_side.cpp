// ipu_side: PCSX2's IPU behind ob_ipu_bridge.h. It stands in for the parts of PCSX2's EE the IPU touches: the
// event scheduler (R5900.cpp CPU_INT / CPU_SET_DMASTALL, run by _cpuTestInterrupts' TESTINT for DMAC_FROM_IPU,
// DMAC_TO_IPU and IPU_PROCESS), the DMA start path (Dmac.cpp DmaExec / StartQueuedDMA), DMA addressing
// (Dmac.cpp dmaGetAddr, pointed at the caller's RAM and scratchpad), the chain-tag helpers (Hw.cpp
// hwDmacSrcChain / hwDmacSrcTadrInc, copied) and the interrupt calls (Hw.cpp hwIntcIrq / hwDmacIrq).
// Registers live where PCSX2 keeps them: eeHw (ob_pcsx2_gs's host_stubs.cpp), the IPU at 0x2000, channel 3 at
// 0xB000, channel 4 at 0xB400, the DMAC at 0xE000 and 0xF520; cpuRegs is ob_pcsx2_gs's _cpuRegistersPack.
#include "ob_ipu_bridge.h"

#include "Common.h"
#include "Dmac.h"
#include "Hw.h"
#include "R5900.h"
#include "IPU/IPU.h"
#include "IPU/IPUdma.h"

#include <cstring>

void ipu0Interrupt();
void ipu1Interrupt();
void ipuCMDProcess();
void dmaIPU0();
void dmaIPU1();
void ReadFIFO_IPUout(mem128_t* out);      // IPU_Fifo.cpp
void WriteFIFO_IPUin(const mem128_t* value);

namespace
{
	bool s_open = false;
	uint8_t* s_ram = nullptr;
	uint32_t s_ramMask = 0;
	uint8_t* s_spr = nullptr;
	uint32_t s_sprMask = 0;
	uint32_t s_events = 0;
	bool s_queued[2] = {}; // channel 3, channel 4: started while the DMAC was disabled or suspended

	// The events the IPU schedules, in PCSX2's TESTINT order (R5900.cpp _cpuTestInterrupts).
	struct Event
	{
		EE_EventType type;
		void (*run)();
	};
	const Event kEvents[] = {
		{DMAC_FROM_IPU, ipu0Interrupt},
		{DMAC_TO_IPU, ipu1Interrupt},
		{IPU_PROCESS, ipuCMDProcess},
	};

	void SetNow(uint64_t cycle)
	{
		if (cycle > cpuRegs.cycle)
			cpuRegs.cycle = cycle;
	}

	bool DmacRunning()
	{
		return dmacRegs.ctrl.DMAE && !(psHu8(DMAC_ENABLER + 2) & 1);
	}

	// Dmac.cpp DmaExec for channels 3/4: STR can only be cleared while a transfer runs; otherwise the CHCR is
	// written and a start runs now, or waits for the DMAC to be enabled/resumed.
	void ChannelChcrWrite(int channel, uint32_t value)
	{
		DMACh& reg = channel == 3 ? ipu0ch : ipu1ch;
		const tDMA_CHCR chcr(value);
		if (reg.chcr.STR)
		{
			if (!chcr.STR)
			{
				reg.chcr.STR = 0;
				cpuRegs.interrupt &= ~(1u << (channel == 3 ? DMAC_FROM_IPU : DMAC_TO_IPU));
				s_queued[channel - 3] = false;
			}
			return;
		}
		reg.chcr.set(value);
		if (reg.chcr.MOD == 0x3)
			reg.chcr.MOD = 0x1;
		if (reg.chcr.STR && !reg.chcr.MOD && reg.qwc == 0)
			reg.qwc = 0x10000;
		if (reg.chcr.STR && DmacRunning())
			channel == 3 ? dmaIPU0() : dmaIPU1();
		else if (reg.chcr.STR)
			s_queued[channel - 3] = true;
	}

	void StartQueued()
	{
		for (int i = 0; i < 2; i++)
		{
			DMACh& reg = i == 0 ? ipu0ch : ipu1ch;
			if (s_queued[i] && reg.chcr.STR && DmacRunning())
			{
				s_queued[i] = false;
				i == 0 ? dmaIPU0() : dmaIPU1();
			}
		}
	}

	// R5900.cpp TESTINT: run each due event (its bit cleared first, as cpuClearInt), until none is due. Events
	// wait while the DMAC is disabled or suspended (_cpuTestInterrupts).
	void RunDue()
	{
		for (int guard = 0; guard < 100000 && DmacRunning(); guard++)
		{
			bool ran = false;
			for (const Event& e : kEvents)
			{
				const u32 bit = 1u << e.type;
				if ((cpuRegs.interrupt & bit) &&
					cpuRegs.cycle - cpuRegs.sCycle[e.type] >= static_cast<u64>(static_cast<s32>(cpuRegs.eCycle[e.type])))
				{
					cpuRegs.interrupt &= ~bit;
					e.run();
					ran = true;
				}
			}
			if (!ran)
				return;
		}
	}
} // namespace

// ---- PCSX2 EE pieces the IPU calls (normally R5900.cpp, Hw.cpp, Dmac.cpp) ----
void CPU_INT(EE_EventType n, s32 ecycle)
{
	cpuRegs.interrupt |= 1u << n;
	cpuRegs.sCycle[n] = cpuRegs.cycle;
	cpuRegs.eCycle[n] = ecycle;
}

void CPU_SET_DMASTALL(EE_EventType n, bool set)
{
	if (set)
		cpuRegs.dmastall |= 1u << n;
	else
		cpuRegs.dmastall &= ~(1u << n);
}

void hwIntcIrq(int n)
{
	psHu32(INTC_STAT) |= 1u << n;
	if (n == INTC_IPU)
		s_events |= OB_IPU_EV_INTC;
}

void hwDmacIrq(int n)
{
	psHu32(DMAC_STAT) |= 1u << n;
	if (n == DMAC_FROM_IPU)
		s_events |= OB_IPU_EV_DMA3;
	else if (n == DMAC_TO_IPU)
		s_events |= OB_IPU_EV_DMA4;
}

void throwBusError(const char*)
{
	hwDmacIrq(DMAC_BUS_ERROR);
}

void setDmacStat(u32 num)
{
	hwDmacIrq(num);
}

// Dmac.cpp dmaGetAddr: the SPR bit (31) selects the scratchpad; otherwise a physical address in main RAM.
tDMA_TAG* dmaGetAddr(u32 addr, bool)
{
	if (addr & 0x80000000u)
		return reinterpret_cast<tDMA_TAG*>(s_spr + (addr & s_sprMask & ~0xFu));
	addr &= 0x1ffffff0u;
	if (addr <= s_ramMask)
		return reinterpret_cast<tDMA_TAG*>(s_ram + addr);
	return nullptr;
}

bool DMACh::transfer(const char* s, tDMA_TAG* ptag)
{
	if (ptag == nullptr)
	{
		throwBusError(s);
		return false;
	}
	chcrTransfer(ptag);
	qwcTransfer(ptag);
	return true;
}

// Hw.cpp, verbatim apart from comments (PCSX2, GPL-3.0+).
void hwDmacSrcTadrInc(DMACh& dma)
{
	if (dma.chcr.STR == 0)
		return;
	if (dma.chcr.MOD != 1)
		return;
	const u16 tagid = (dma.chcr.TAG >> 12) & 0x7;
	if (tagid == TAG_CNT)
		dma.tadr = dma.madr;
}

bool hwDmacSrcChain(DMACh& dma, int id)
{
	u32 temp;
	switch (id)
	{
		case TAG_REFE:
			dma.tadr += 16;
			return true;
		case TAG_CNT:
			dma.madr = dma.tadr + 16;
			dma.tadr = dma.madr;
			return false;
		case TAG_NEXT:
			temp = dma.madr;
			dma.madr = dma.tadr + 16;
			dma.tadr = temp;
			return false;
		case TAG_REF:
		case TAG_REFS:
			dma.tadr += 16;
			return false;
		case TAG_END:
			dma.madr = dma.tadr + 16;
			return true;
		default:
			return true;
	}
}

// ---- bridge ----
bool ob_ipu_open(uint8_t* ram, uint32_t ram_size, uint8_t* spr, uint32_t spr_size, uint64_t cycle)
{
	if (s_open || !ram || !spr || ram_size == 0 || (ram_size & (ram_size - 1)) != 0 || spr_size == 0 ||
		(spr_size & (spr_size - 1)) != 0)
		return false;
	s_ram = ram;
	s_ramMask = ram_size - 1;
	s_spr = spr;
	s_sprMask = spr_size - 1;
	std::memset(&cpuRegs, 0, sizeof(cpuRegs));
	cpuRegs.cycle = cycle;
	std::memset(&eeHw[0x2000], 0, 0x100);
	std::memset(&eeHw[0xB000], 0, 0x100);
	std::memset(&eeHw[0xB400], 0, 0x100);
	dmacRegs.ctrl.DMAE = 1;
	psHu32(DMAC_ENABLER) = 0;
	s_events = 0;
	s_queued[0] = s_queued[1] = false;
	ipuReset();
	s_open = true;
	return true;
}

void ob_ipu_close()
{
	s_open = false;
	s_ram = s_spr = nullptr;
}

uint32_t ob_ipu_read32(uint32_t addr, uint64_t cycle)
{
	if (!s_open)
		return 0;
	SetNow(cycle);
	RunDue();
	return ipuRead32(0x10002000u | (addr & 0xffu));
}

uint64_t ob_ipu_read64(uint32_t addr, uint64_t cycle)
{
	if (!s_open)
		return 0;
	SetNow(cycle);
	RunDue();
	return ipuRead64(0x10002000u | (addr & 0xffu));
}

void ob_ipu_write32(uint32_t addr, uint32_t value, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
	const u32 mem = 0x10002000u | (addr & 0xfffu);
	if (ipuWrite32(mem, value))
		psHu32(mem) = value;
}

void ob_ipu_write64(uint32_t addr, uint64_t value, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
	const u32 mem = 0x10002000u | (addr & 0xfffu);
	if (ipuWrite64(mem, value))
		psHu64(mem) = value;
}

void ob_ipu_fifo_read(uint32_t q[4], uint64_t cycle)
{
	std::memset(q, 0, 16);
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
	ReadFIFO_IPUout(reinterpret_cast<mem128_t*>(q));
}

void ob_ipu_fifo_write(const uint32_t q[4], uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
	WriteFIFO_IPUin(reinterpret_cast<const mem128_t*>(q));
}

uint32_t ob_ipu_dma_read(uint32_t addr)
{
	if (!s_open)
		return 0;
	return psHu32(addr & 0xfffcu);
}

void ob_ipu_dma_write(uint32_t addr, uint32_t value, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
	const u32 mem = addr & 0xfffcu;
	const int channel = (mem & 0xff00u) == 0xb000u ? 3 : 4;
	switch (mem & 0xffu)
	{
		case 0x00: // CHCR
			ChannelChcrWrite(channel, value);
			break;
		case 0x20: // QWC: 16 bits (Dmac.cpp dmacWrite32)
			psHu32(mem) = static_cast<u16>(value);
			break;
		default: // MADR, TADR, ASR0/1, SADR
			psHu32(mem) = value;
			break;
	}
}

void ob_ipu_dmac_state(bool enabled, bool suspended, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	dmacRegs.ctrl.DMAE = enabled ? 1 : 0;
	psHu8(DMAC_ENABLER + 2) = suspended ? 1 : 0;
	StartQueued();
	RunDue();
}

void ob_ipu_advance(uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	RunDue();
}

uint64_t ob_ipu_next_event()
{
	if (!s_open || !DmacRunning())
		return UINT64_MAX;
	uint64_t next = UINT64_MAX;
	for (const Event& e : kEvents)
	{
		if (cpuRegs.interrupt & (1u << e.type))
		{
			const s32 delay = static_cast<s32>(cpuRegs.eCycle[e.type]);
			const uint64_t at = cpuRegs.sCycle[e.type] + static_cast<uint64_t>(delay > 0 ? delay : 0);
			if (at < next)
				next = at;
		}
	}
	return next;
}

uint32_t ob_ipu_take_events()
{
	const uint32_t e = s_events;
	s_events = 0;
	return e;
}
