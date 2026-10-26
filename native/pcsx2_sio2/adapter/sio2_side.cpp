// sio2_side: PCSX2's SIO2 and memory cards behind ob_sio2_bridge.h. It stands in for the parts of PCSX2's IOP the
// SIO2 touches: the page-8 register decode (ps2/Iop/IopHwRead.cpp / IopHwWrite.cpp iopHw*32_Page8, the byte
// DATAIN/FIFO paths at :131 / :165), DMA 11/12 (IopDma.cpp psxDma11 / psxDma12) and the interrupt call
// (IopDma.h iopIntcIrq), and sets up the cards as PCSX2's settings would (Pcsx2Config.cpp:1979-1982 defaults,
// EmuFolders::MemoryCards).
#include "ob_sio2_bridge.h"

#include "Config.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Sio.h"
#include "SIO/Sio2.h"

#include "common/FileSystem.h"
#include "common/Path.h"

#include <cstdio>

namespace
{
	constexpr u32 kCtrl = 0x268, kCmdStat = 0x26C, kPortStat = 0x270, kFifoStat = 0x274, kFifoTx = 0x278, kFifoRx = 0x27C,
				  kIntr = 0x280;

	bool s_open = false;
	bool s_irq = false;
	u32 s_other[0x40] = {}; // the page's other words (PCSX2: psxHu32), 0x200..0x2FF

	// The runner ends with _Exit (no destructors, no stdio flush): PCSX2's FileMemoryCard writes through a buffered
	// FILE*, so a transfer that wrote or erased a card (MemoryCardProtocol WriteData/EraseBlock -> SetBusy) is
	// flushed before the IOP sees its result.
	void FlushIfWritten()
	{
		if (!MemcardBusy::IsBusy())
			return;
		std::fflush(nullptr);
		MemcardBusy::ClearBusy();
	}
} // namespace

// IopDma.h: the SIO2's only interrupt is line 17 (Sio2.cpp Sio2::Interrupt).
void iopIntcIrq(uint irqType)
{
	if (irqType == 17)
		s_irq = true;
}

bool ob_sio2_open(const char* card_folder, bool create_slot1)
{
	if (s_open || !card_folder || !*card_folder)
		return false;
	EmuFolders::MemoryCards = card_folder;
	if (!FileSystem::EnsureDirectoryExists(EmuFolders::MemoryCards.c_str(), true))
		return false;
	for (uint slot = 0; slot < 8; ++slot)
	{
		EmuConfig.Mcd[slot].Enabled = false;
		EmuConfig.Mcd[slot].Filename.clear();
		EmuConfig.Mcd[slot].Type = MemoryCardType::Empty;
	}
	// A slot whose file is there (slot 1 also when create_slot1: FileMemoryCard::Open creates a missing file). Files,
	// not PCSX2 folder cards (MemoryCardFolder.cpp, YAML, is not built).
	for (uint slot = 0; slot < 2; ++slot)
	{
		const std::string name = FileMcd_GetDefaultName(slot);
		if ((slot == 0 && create_slot1) || FileSystem::FileExists(Path::Combine(EmuFolders::MemoryCards, name).c_str()))
			EmuConfig.Mcd[slot] = {name, true, MemoryCardType::File};
	}
	FileMcd_EmuOpen();
	if (create_slot1 && FileMcd_IsPresent(0, 0) == 0)
	{
		FileMcd_EmuClose();
		return false;
	}
	g_Sio2.Initialize();
	s_irq = false;
	s_open = true;
	return true;
}

void ob_sio2_close()
{
	if (!s_open)
		return;
	FileMcd_EmuClose();
	s_open = false;
}

uint32_t ob_sio2_read32(uint32_t phys)
{
	const u32 a = phys & 0xFFF;
	if (a < 0x240)
		return g_Sio2.CmdQueue[(a - 0x200) / 4];
	if (a < 0x260)
		return (a & 4) ? g_Sio2.PortCtrl1[(a - 0x240) / 8] : g_Sio2.PortCtrl0[(a - 0x240) / 8];
	switch (a)
	{
		case kCtrl: return g_Sio2.ctrl;
		case kCmdStat: return g_Sio2.CmdStat;
		case kPortStat: return g_Sio2.PortStat;
		case kFifoStat: return g_Sio2.FifoStat;
		case kFifoTx: return g_Sio2.FifoTxPos;
		case kFifoRx: return g_Sio2.FifoRxPos;
		case kIntr: return g_Sio2.iStat;
		default: return s_other[(a - 0x200) / 4 & 0x3F];
	}
}

void ob_sio2_write32(uint32_t phys, uint32_t value)
{
	const u32 a = phys & 0xFFF;
	if (a < 0x240)
		g_Sio2.SetCmd((a - 0x200) / 4, value);
	else if (a < 0x260)
		((a & 4) ? g_Sio2.PortCtrl1 : g_Sio2.PortCtrl0)[(a - 0x240) / 8] = value;
	else
		switch (a)
		{
			case 0x260: case 0x264: break; // 32-bit DATAIN / FIFO writes: ignored, as PCSX2 (it only warns)
			case kCtrl: g_Sio2.SetCtrl(value); break;
			case kCmdStat: g_Sio2.CmdStat = value; break;
			case kPortStat: g_Sio2.PortStat = value; break;
			case kFifoStat: g_Sio2.FifoStat = value; break;
			case kFifoTx: g_Sio2.FifoTxPos = value; break;
			case kFifoRx: g_Sio2.FifoRxPos = value; break;
			case kIntr: g_Sio2.iStat &= ~value; break;
			default: s_other[(a - 0x200) / 4 & 0x3F] = value; break;
		}
	FlushIfWritten();
}

uint8_t ob_sio2_read8(uint32_t phys)
{
	if ((phys & 0xFFF) == 0x264)
		return g_Sio2.Read();
	return static_cast<u8>(ob_sio2_read32(phys & ~3u) >> ((phys & 3u) * 8u));
}

void ob_sio2_write8(uint32_t phys, uint8_t value)
{
	if ((phys & 0xFFF) == 0x260)
	{
		g_Sio2.Write(value);
		FlushIfWritten();
	}
}

uint32_t ob_sio2_dma(uint32_t channel, uint8_t* iop_ram, uint32_t ram_size, uint32_t madr, uint32_t bcr, uint32_t chcr)
{
	const u32 mask = ram_size - 1;
	if (channel == 11)
	{
		// psxDma11: the block size counts the command bytes (Sio2::Write), even for a CHCR it then ignores.
		g_Sio2.dmaBlockSize = (bcr & 0xffff) * 4;
		if (chcr != 0x01000201)
			return OB_SIO2_DMA_IGNORED;
		for (u32 i = 0; i < (bcr >> 16); i++)
			for (u32 j = 0; j < (bcr & 0xFFFF) * 4; j++)
				g_Sio2.Write(iop_ram[madr++ & mask]);
		FlushIfWritten();
		return ((bcr >> 16) * (bcr & 0xffff)) >> 2; // PSX_INT(IopEvt_Dma11, size >> 2), size in words
	}
	if (channel == 12)
	{
		const u32 size = ((bcr >> 16) * (bcr & 0xFFFF)) * 4;
		if (chcr != 0x41000200)
			return OB_SIO2_DMA_IGNORED;
		for (u32 n = 0; n < size; n++)
			iop_ram[madr++ & mask] = g_Sio2.Read();
		return size >> 2; // PSX_INT(IopEvt_Dma12, size >> 2), size in bytes
	}
	return OB_SIO2_DMA_IGNORED;
}

bool ob_sio2_take_interrupt()
{
	const bool raised = s_irq;
	s_irq = false;
	return raised;
}

bool ob_sio2_card_present(uint32_t slot)
{
	return s_open && slot < 2 && FileMcd_IsPresent(slot, 0) != 0;
}
