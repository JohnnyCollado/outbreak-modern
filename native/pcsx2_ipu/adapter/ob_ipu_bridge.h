// ob_ipu_bridge: PCSX2's IPU (the EE's MPEG-2 decoder) and its two DMA channels, for PS2Recomp's EE side.
// The caller owns EE RAM and the scratchpad; the IPU reads its input (DMA channel 4, toIPU) from them and writes
// its output (channel 3, fromIPU) into them. Times are the caller's EE cycles (any monotonic 64-bit count).
// One instance, single-threaded: call everything from the thread that runs the EE.
#pragma once

#include <cstdint>

enum : uint32_t
{
	OB_IPU_EV_INTC = 1, // the IPU's INTC interrupt (cause 8): a command finished
	OB_IPU_EV_DMA3 = 2, // channel 3 (fromIPU) finished: D_STAT bit 3
	OB_IPU_EV_DMA4 = 4, // channel 4 (toIPU) finished: D_STAT bit 4
};

// ram_size and spr_size are powers of two (32 MiB and 16 KiB on the PS2).
bool ob_ipu_open(uint8_t* ram, uint32_t ram_size, uint8_t* spr, uint32_t spr_size, uint64_t cycle);
void ob_ipu_close();

// IPU registers 0x10002000-0x1000203F (CMD, CTRL, BP, TOP).
uint32_t ob_ipu_read32(uint32_t addr, uint64_t cycle);
uint64_t ob_ipu_read64(uint32_t addr, uint64_t cycle);
void ob_ipu_write32(uint32_t addr, uint32_t value, uint64_t cycle);
void ob_ipu_write64(uint32_t addr, uint64_t value, uint64_t cycle);

// The FIFOs: one quadword out of 0x10007000 (IPU_out) / into 0x10007010 (IPU_in).
void ob_ipu_fifo_read(uint32_t q[4], uint64_t cycle);
void ob_ipu_fifo_write(const uint32_t q[4], uint64_t cycle);

// Channel registers 0x1000B000-0x1000B0FF (channel 3) and 0x1000B400-0x1000B4FF (channel 4). A CHCR write that
// sets STR starts the transfer (queued while the DMAC is disabled or suspended, as PCSX2's DmaExec).
uint32_t ob_ipu_dma_read(uint32_t addr);
void ob_ipu_dma_write(uint32_t addr, uint32_t value, uint64_t cycle);

// The DMAC's master state: D_CTRL.DMAE and the D_ENABLEW suspend bit (bit 16). Resuming starts queued transfers.
void ob_ipu_dmac_state(bool enabled, bool suspended, uint64_t cycle);

// Runs every IPU/DMA event due by `cycle`; next_event = the cycle of the next one (UINT64_MAX: none).
void ob_ipu_advance(uint64_t cycle);
uint64_t ob_ipu_next_event();
uint32_t ob_ipu_take_events(); // OB_IPU_EV_* since the last call
