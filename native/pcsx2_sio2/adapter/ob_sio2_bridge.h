// ob_sio2_bridge: plain-types interface between PS2Recomp's IOP (the external SIO2 device in
// native/pcsx2_gs/adapter/hook.cpp) and PCSX2's SIO2 + memory cards (sio2_side.cpp). No PCSX2 header leaks through.
//
// The cards are PCSX2 .ps2 files (8 MB + ECC, 8,650,752 bytes) in one folder: Mcd001.ps2 = slot 1, Mcd002.ps2 = slot 2,
// each when its file exists; create_slot1 makes a missing Mcd001.ps2 (unformatted, as PCSX2's FileMemoryCard::Open).
// All calls come from the thread that runs the IOP.
#pragma once

#include <cstdint>

constexpr uint32_t OB_SIO2_DMA_IGNORED = 0xFFFFFFFFu; // = ps2x::iop::kSio2DmaIgnored

// false: no folder, or (create_slot1) slot 1's card cannot be created or opened. Without create_slot1 a folder with
// no cards opens with both slots empty.
bool ob_sio2_open(const char* card_folder, bool create_slot1);
void ob_sio2_close();                       // closes the card files (PCSX2 FileMcd_EmuClose)

// Registers 0x1F808200..0x1F8082FF as PCSX2's iopHwRead32_Page8 / iopHwWrite32_Page8; the byte-wide DATAIN
// (0x1F808260, a command byte in) and FIFO (0x1F808264, the next byte out).
uint32_t ob_sio2_read32(uint32_t phys);
void ob_sio2_write32(uint32_t phys, uint32_t value);
uint8_t ob_sio2_read8(uint32_t phys);
void ob_sio2_write8(uint32_t phys, uint8_t value);

// DMA channel 11 (IOP RAM to SIO2) or 12 (SIO2 to IOP RAM), as IopDma.cpp psxDma11 / psxDma12: moves the bytes now,
// returns the IOP cycles until the channel interrupt, or OB_SIO2_DMA_IGNORED for a CHCR those do not take.
uint32_t ob_sio2_dma(uint32_t channel, uint8_t* iop_ram, uint32_t ram_size, uint32_t madr, uint32_t bcr, uint32_t chcr);

bool ob_sio2_take_interrupt();           // SIO2 raised INTC line 17 (Sio2::Interrupt) since the last call
bool ob_sio2_card_present(uint32_t slot); // slot 0 = memory card slot 1, 1 = slot 2
