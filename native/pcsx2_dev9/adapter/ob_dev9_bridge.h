// ob_dev9_bridge: plain-types interface between PS2Recomp's IOP (the external DEV9 device in
// native/pcsx2_gs/adapter/hook.cpp) and PCSX2's DEV9 + SMAP + Sockets adapter (dev9_side.cpp). No PCSX2 header leaks
// through. All calls come from the thread that runs the IOP, except the adapter's own receive thread inside PCSX2.
#pragma once

#include <cstdint>

constexpr uint32_t OB_DEV9_DMA_IGNORED = 0xFFFFFFFFu;

// A name the game looks up, answered by the adapter's internal DNS with this address (PCSX2's EthHosts). The game's
// fixed server names (gate1.jp.dnas.playstation.org, www01.kddi-mmbb.jp) are pointed at the chosen server this way.
struct ob_dev9_host
{
	const char* name;  // matched without case
	uint8_t address[4];
};

struct ob_dev9_config
{
	const char* interface_name;  // host network interface for the Sockets adapter; null or "" = automatic
	const ob_dev9_host* hosts;
	uint32_t host_count;
	// The adapter's own messages (DHCP, DNS answers, sessions; written only when something happens) go to this file,
	// unless PCSX2's log file is already open (one per process: then they go there). null or "" = none.
	const char* log_path;
};

// Starts DEV9 with the Ethernet adapter in Sockets mode, its DHCP on and its internal DNS (no HDD). false: the adapter
// did not start (no usable network interface); DEV9 then still answers its registers, with no network.
bool ob_dev9_open(const ob_dev9_config* config);
void ob_dev9_close();
bool ob_dev9_network_up();  // the adapter started (after ob_dev9_open)

// The SPEED/SMAP/flash page 0x10000000..0x1000FFFF and the DEV9 revision register 0x1F80146E, at the access width
// the IOP made (a 32-bit read of the SMAP receive FIFO pops data, so no width may be built from another).
uint8_t ob_dev9_read8(uint32_t phys);
uint16_t ob_dev9_read16(uint32_t phys);
uint32_t ob_dev9_read32(uint32_t phys);
void ob_dev9_write8(uint32_t phys, uint8_t value);
void ob_dev9_write16(uint32_t phys, uint16_t value);
void ob_dev9_write32(uint32_t phys, uint32_t value);

// DMA channel 8, as IopDma.cpp psxDma8: CHCR 0x01000201 = IOP RAM to DEV9, 0x01000200 = DEV9 to IOP RAM; bcr = block
// size in words (low 16 bits) and block count (high 16). Moves the bytes now; returns 0, or OB_DEV9_DMA_IGNORED for a
// CHCR psxDma8 does not take. The end of the transfer is reported by ob_dev9_take_dma_done.
uint32_t ob_dev9_dma(uint8_t* iop_ram, uint32_t ram_size, uint32_t madr, uint32_t bcr, uint32_t chcr);

// Time passing on the IOP (PCSX2 calls DEV9async on every IOP counter update): runs the SMAP's receive side and counts
// down the interrupt DEV9 asked for (dev9Irq(cycles)).
void ob_dev9_advance(uint32_t iop_cycles);
bool ob_dev9_take_interrupt();  // DEV9 raised INTC line 13 since the last call (dev9Interrupt: cause & mask)
bool ob_dev9_take_dma_done();   // DEV9 ended a channel-8 transfer since the last call (psxDMA8Interrupt)
