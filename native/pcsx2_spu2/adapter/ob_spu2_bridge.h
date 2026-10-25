// ob_spu2_bridge: plain-types interface between PS2Recomp's IOP (the external SPU2 device in
// native/pcsx2_gs/adapter/hook.cpp) and PCSX2's SPU2 (spu2_side.cpp). No PCSX2 header leaks through here.
//
// Time is the IOP's cycle count (36.864 MHz); PCSX2's SPU2 mixes one 48 kHz sample every 768 cycles.
// All calls except ob_spu2_pull come from the thread that runs the IOP.
#pragma once

#include <cstdint>

enum : uint32_t
{
	OB_SPU2_EV_IRQ = 1u,  // the SPU2's own interrupt (IOP INTC line 9)
	OB_SPU2_EV_DMA4 = 2u, // core 0 transfer finished (IOP DMA channel 4)
	OB_SPU2_EV_DMA7 = 4u, // core 1 transfer finished (IOP DMA channel 7)
};

// iop_ram: the IOP's main RAM (ram_size bytes, a power of two); the SPU2's DMA reads and writes it directly.
bool ob_spu2_open(uint8_t* iop_ram, uint32_t ram_size, uint64_t cycle);
void ob_spu2_close();

// Registers 0x1F900000..0x1F900FFF (16-bit), as PCSX2's SPU2read/SPU2write.
uint16_t ob_spu2_read(uint32_t phys, uint64_t cycle);
void ob_spu2_write(uint32_t phys, uint16_t value, uint64_t cycle);

// A transfer started on IOP DMA channel 4 (core 0) or 7 (core 1): halfwords at madr, to the SPU2 or from it.
void ob_spu2_dma(uint32_t core, uint32_t madr, uint32_t halfwords, bool to_spu2, uint64_t cycle);

// Run the SPU2 up to cycle. Due at ob_spu2_next_event() (absolute IOP cycle, at most one sample ahead).
void ob_spu2_advance(uint64_t cycle);
uint64_t ob_spu2_next_event();
uint32_t ob_spu2_take_events(); // OB_SPU2_EV_* raised since the last call

// Audio thread: count interleaved stereo float frames at ob_spu2_sample_rate(); silence when closed.
uint32_t ob_spu2_sample_rate();
void ob_spu2_pull(float* frames, uint32_t count);
uint64_t ob_spu2_samples_mixed(); // 48 kHz samples the SPU2 has mixed since open
