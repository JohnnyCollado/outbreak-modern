// ob_pcsx2_ipu tests: PCSX2's IPU through ob_ipu_bridge.h with no EE, driving registers, the FIFO and DMA
// channels 3/4 as a game would. The decode test uses a hand-built bitstream (no game data): one MPEG-2 intra
// macroblock whose six blocks carry only a DC difference and end-of-block.
#include "ob_ipu_bridge.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
	int s_pass = 0, s_total = 0;
	uint64_t s_cycle = 1000;

	void check(bool ok, const char* what)
	{
		s_total++;
		if (ok)
			s_pass++;
		else
			std::printf("FAIL: %s\n", what);
	}

	constexpr uint32_t kCmd = 0x10002000, kCtrl = 0x10002010;
	constexpr uint32_t kD3Chcr = 0x1000B000, kD3Madr = 0x1000B010, kD3Qwc = 0x1000B020;
	constexpr uint32_t kD4Chcr = 0x1000B400, kD4Madr = 0x1000B410, kD4Qwc = 0x1000B420;

	std::vector<uint8_t> s_ram(32u << 20), s_spr(16u << 10);

	uint32_t ctrl() { return ob_ipu_read32(kCtrl, s_cycle); }

	// Advance in steps until `done` or the budget runs out; returns the OB_IPU_EV_* bits seen.
	template <typename F>
	uint32_t run_until(F done, uint64_t budget = 2000000)
	{
		uint32_t seen = 0;
		for (uint64_t spent = 0; spent < budget && !done(); spent += 64)
		{
			s_cycle += 64;
			ob_ipu_advance(s_cycle);
			seen |= ob_ipu_take_events();
		}
		return seen | ob_ipu_take_events();
	}

	void reopen()
	{
		ob_ipu_close();
		std::memset(s_ram.data(), 0, s_ram.size());
		check(ob_ipu_open(s_ram.data(), static_cast<uint32_t>(s_ram.size()), s_spr.data(),
				  static_cast<uint32_t>(s_spr.size()), s_cycle),
			"open");
		ob_ipu_write32(kCtrl, 0x40000000u, s_cycle); // RST
		ob_ipu_write32(kCmd, 0x00000000u, s_cycle);  // BCLR, bit offset 0
		ob_ipu_take_events();
	}

	// Channel 4, normal mode, from RAM: CHCR DIR=1 (from memory), MOD=0, STR.
	void feed(uint32_t addr, uint32_t qwc)
	{
		ob_ipu_dma_write(kD4Madr, addr, s_cycle);
		ob_ipu_dma_write(kD4Qwc, qwc, s_cycle);
		ob_ipu_dma_write(kD4Chcr, 0x101u, s_cycle);
	}

	void test_registers()
	{
		reopen();
		check((ctrl() & 0x80000000u) == 0, "IPU idle after reset");
		check((ctrl() & 0xFu) == 0, "input FIFO empty after reset");
		ob_ipu_write32(kCmd, 0x90200020u, s_cycle); // SETTH: runs inline
		check((ctrl() & 0x80000000u) == 0 && (ob_ipu_take_events() & OB_IPU_EV_INTC),
			"SETTH completes at once and raises the IPU interrupt");
		const uint32_t q[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
		ob_ipu_fifo_write(q, s_cycle);
		check((ctrl() & 0xFu) == 1, "a FIFO write adds one quadword to IFC");
	}

	void test_dma_in()
	{
		reopen();
		const uint8_t bytes[16] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
		std::memcpy(&s_ram[0x1000], bytes, sizeof(bytes));
		std::memcpy(&s_ram[0x1010], bytes, sizeof(bytes));
		feed(0x1000, 2);
		const uint32_t ev = run_until([] { return (ob_ipu_dma_read(kD4Chcr) & 0x100u) == 0; });
		check((ob_ipu_dma_read(kD4Chcr) & 0x100u) == 0 && (ev & OB_IPU_EV_DMA4), "channel 4 ends and raises D_STAT 4");
		check(ob_ipu_dma_read(kD4Qwc) == 0 && ob_ipu_dma_read(kD4Madr) == 0x1020, "channel 4 moved 2 quadwords");
		check((ctrl() & 0xFu) == 2, "the IPU input FIFO holds them");
		check(ob_ipu_read32(kCmd, s_cycle) == 0x12345678u, "IPU_CMD shows the next 32 bits, big-endian");
		ob_ipu_write32(kCmd, 0x40000008u, s_cycle); // FDEC, skip 8 bits
		run_until([] { return (ctrl() & 0x80000000u) == 0; });
		check(ob_ipu_read32(kCmd, s_cycle) == 0x3456789Au, "FDEC after an 8-bit skip returns the following 32 bits");
	}

	void test_suspend()
	{
		reopen();
		std::memset(&s_ram[0x1000], 0xAB, 32);
		ob_ipu_dmac_state(true, true, s_cycle);
		feed(0x1000, 2);
		run_until([] { return false; }, 2000);
		check((ctrl() & 0xFu) == 0, "a start while the DMAC is suspended waits");
		ob_ipu_dmac_state(true, false, s_cycle);
		run_until([] { return (ob_ipu_dma_read(kD4Chcr) & 0x100u) == 0; });
		check((ctrl() & 0xFu) == 2, "resuming the DMAC runs the queued transfer");
	}

	void test_decode()
	{
		reopen();
		// MPEG-2 (CTRL.MP1 = 0), 8-bit intra DC, B.14 tables. Luma dct_dc_size: 0 '100', 2 '01'; chroma: 0 '00',
		// 1 '01'; a DC difference of n bits is positive if its first bit is 1, else bits - (2^n - 1); EOB '10'.
		//   Y0 '01' '11' '10' (+3: 131)  Y1 '100' '10' (131)  Y2 '100' '10' (131)  Y3 '01' '00' '10' (-3: 128)
		//   Cb '00' '10' (128)  Cr '01' '1' '10' (+1: 129); 31 bits, then 1s.
		const uint8_t bits[4] = {0x7A, 0x52, 0x48, 0x9D};
		std::memset(&s_ram[0x1000], 0xFF, 64);
		std::memcpy(&s_ram[0x1000], bits, sizeof(bits));
		feed(0x1000, 4);
		run_until([] { return (ob_ipu_dma_read(kD4Chcr) & 0x100u) == 0; });
		// Channel 3, normal mode, to RAM: the 768-byte RAW16 macroblock = 48 quadwords.
		ob_ipu_dma_write(kD3Madr, 0x4000, s_cycle);
		ob_ipu_dma_write(kD3Qwc, 48, s_cycle);
		ob_ipu_dma_write(kD3Chcr, 0x100u, s_cycle);
		ob_ipu_write32(kCmd, 0x2C010000u, s_cycle); // BDEC: MBI (intra), DCR (reset DC), QSC 1, frame DCT
		check((ctrl() & 0x80000000u) != 0, "BDEC is busy right after the command");
		const uint64_t next = ob_ipu_next_event();
		check(next != UINT64_MAX && next > s_cycle, "the decode completes at a scheduled later cycle");
		const uint32_t ev = run_until([] {
			return (ctrl() & 0x80000000u) == 0 && (ob_ipu_dma_read(kD3Chcr) & 0x100u) == 0;
		});
		check((ctrl() & 0x80000000u) == 0, "BDEC finishes");
		check((ob_ipu_dma_read(kD3Chcr) & 0x100u) == 0 && (ev & OB_IPU_EV_DMA3), "channel 3 ends and raises D_STAT 3");
		const uint16_t* mb = reinterpret_cast<const uint16_t*>(&s_ram[0x4000]);
		bool y = true, cb = true, cr = true;
		for (int r = 0; r < 16; r++)
			for (int c = 0; c < 16; c++)
				y &= mb[r * 16 + c] == ((r >= 8 && c >= 8) ? 128 : 131);
		for (int i = 0; i < 64; i++)
		{
			cb &= mb[256 + i] == 128;
			cr &= mb[320 + i] == 129;
		}
		check(y, "luma: three quarters at 131, the last at 128");
		check(cb, "Cb at 128");
		check(cr, "Cr at 129");
		if (!(y && cb && cr))
			std::printf("  Y[0]=%u Y[8*16+8]=%u Cb[0]=%u Cr[0]=%u\n", mb[0], mb[136], mb[256], mb[320]);
	}
} // namespace

int main()
{
	test_registers();
	test_dma_in();
	test_suspend();
	test_decode();
	ob_ipu_close();
	std::printf("pcsx2_ipu: %d/%d\n", s_pass, s_total);
	return s_pass == s_total ? 0 : 1;
}
