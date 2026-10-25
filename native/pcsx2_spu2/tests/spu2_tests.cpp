// ob_pcsx2_spu2 tests (ob.sh test): PCSX2's SPU2 behind ob_spu2_bridge.h, without the IOP.
// - one emulated second (36,864,000 IOP cycles) advanced in sample-sized steps mixes 48,000 samples;
// - a voice keyed on over a square-wave ADPCM block in SPU2 RAM (uploaded by DMA) makes non-silent output;
// - a core 0 DMA upload finishes with the DMA4 event.
#include "ob_spu2_bridge.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int s_fail = 0, s_total = 0;
static void Check(bool ok, const char* what)
{
	++s_total;
	if (!ok)
	{
		++s_fail;
		std::printf("FAIL: %s\n", what);
	}
}

// Core 0 registers (PCSX2 SPU2/regs.h; Sony's libsd register map).
constexpr uint32_t kBase = 0x1F900000u;
constexpr uint32_t kAttr = kBase + 0x19Au;   // core attributes (bit 15 enable, bits 4-5 DMA mode)
constexpr uint32_t kTsaH = kBase + 0x1A8u, kTsaL = kBase + 0x1AAu; // transfer start address (halfwords)
constexpr uint32_t kMvolL = kBase + 0x760u, kMvolR = kBase + 0x762u;
constexpr uint32_t kKonL = kBase + 0x1A0u;  // key on, voices 0-15
constexpr uint32_t kVmixLL = kBase + 0x188u, kVmixRL = kBase + 0x190u; // dry voice mixes, voices 0-15
constexpr uint32_t kV0 = kBase + 0x000u;    // voice 0: VOLL, VOLR, PITCH, ADSR1, ADSR2
constexpr uint32_t kV0Ssa = kBase + 0x1C0u; // voice 0 start address (high, low)

int main()
{
	std::vector<uint8_t> ram(0x200000, 0);
	uint64_t t = 0;
	Check(ob_spu2_open(ram.data(), static_cast<uint32_t>(ram.size()), t), "open");

	// One second in 768-cycle steps, drained 256 frames at a time as a host audio callback would.
	std::vector<float> out(256 * 2);
	const uint64_t before = ob_spu2_samples_mixed();
	for (int i = 0; i < 48000; i++)
	{
		t += 768;
		ob_spu2_advance(t);
		if ((i & 255) == 255)
			ob_spu2_pull(out.data(), 256);
	}
	Check(ob_spu2_samples_mixed() - before == 48000, "one IOP second mixes 48000 samples");
	Check(ob_spu2_next_event() > t && ob_spu2_next_event() <= t + 768, "next event within one sample");

	// Square wave in SPU2 ADPCM: 16-byte blocks (shift/filter byte, flags, 14 data bytes = 28 nibbles), looping.
	std::vector<uint8_t> adpcm(16 * 4, 0);
	for (int b = 0; b < 4; b++)
	{
		uint8_t* blk = &adpcm[b * 16];
		blk[0] = 0x00; // shift 0, filter 0
		blk[1] = static_cast<uint8_t>(b == 0 ? 0x04 : b == 3 ? 0x03 : 0x02); // loop start / loop + end / loop
		for (int k = 2; k < 16; k++)
			blk[k] = (b & 1) ? 0x99 : 0x77; // nibble 7 or -7, alternating per block
	}
	const uint32_t madr = 0x100000;
	std::memcpy(&ram[madr], adpcm.data(), adpcm.size());
	const uint32_t spuAddr = 0x5000; // halfword address in SPU2 RAM, past the reserved area
	ob_spu2_write(kAttr, 0x8000, t);
	ob_spu2_write(kTsaH, static_cast<uint16_t>(spuAddr >> 16), t);
	ob_spu2_write(kTsaL, static_cast<uint16_t>(spuAddr & 0xFFFF), t);
	ob_spu2_write(kAttr, 0x8020, t); // DMA write mode
	ob_spu2_take_events();
	ob_spu2_dma(0, madr, static_cast<uint32_t>(adpcm.size() / 2), true, t);
	uint32_t ev = 0;
	for (int i = 0; i < 2000 && !(ev & OB_SPU2_EV_DMA4); i++)
	{
		t += 768;
		ob_spu2_advance(t);
		ev |= ob_spu2_take_events();
	}
	Check((ev & OB_SPU2_EV_DMA4) != 0, "core 0 DMA upload raises the DMA4 event");
	ob_spu2_write(kAttr, 0x8000, t);

	// Voice 0 of core 1 (registers +0x400, master volume +0x28): core 1's mix is the chip's output.
	constexpr uint32_t c1 = 0x400, m1 = 0x28;
	ob_spu2_write(kAttr + c1, 0x8000, t);
	ob_spu2_write(kMvolL + m1, 0x3FFF, t);
	ob_spu2_write(kMvolR + m1, 0x3FFF, t);
	ob_spu2_write(kVmixLL + c1, 0x0001, t); // voice 0 into the dry left and right mixes
	ob_spu2_write(kVmixRL + c1, 0x0001, t);
	ob_spu2_write(kV0 + c1 + 0, 0x3FFF, t); // VOLL
	ob_spu2_write(kV0 + c1 + 2, 0x3FFF, t); // VOLR
	ob_spu2_write(kV0 + c1 + 4, 0x1000, t); // PITCH 1.0 (48 kHz)
	ob_spu2_write(kV0 + c1 + 6, 0x00FF, t); // ADSR1: fastest attack, slowest decay, sustain level max
	ob_spu2_write(kV0 + c1 + 8, 0x0000, t); // ADSR2
	ob_spu2_write(kV0Ssa + c1, static_cast<uint16_t>(spuAddr >> 16), t);
	ob_spu2_write(kV0Ssa + c1 + 2, static_cast<uint16_t>(spuAddr & 0xFFFF), t);
	ob_spu2_write(kKonL + c1, 0x0001, t);

	// 0.25 s, pulled 256 frames at a time as a host audio callback would.
	float peak = 0.0f;
	for (int i = 0; i < 12000; i++)
	{
		t += 768;
		ob_spu2_advance(t);
		if ((i & 255) == 255)
		{
			ob_spu2_pull(out.data(), 256);
			for (float s : out)
				peak = std::fmax(peak, std::fabs(s));
		}
	}
	std::printf("spu2: peak after key-on %.4f\n", peak);
	Check(peak > 0.01f, "a keyed-on voice reaches the output stream");

	// The IOP's cycle count starting over (PS2Recomp resets it when the game reboots the IOP): the SPU2 keeps
	// mixing one sample per 768 cycles of the new count, and the next event is in the new count's units.
	const uint64_t m0 = ob_spu2_samples_mixed();
	uint64_t r = 1000;
	ob_spu2_advance(r);
	for (int i = 0; i < 480; i++)
	{
		r += 768;
		ob_spu2_advance(r);
		if ((i & 255) == 255)
			ob_spu2_pull(out.data(), 256);
	}
	const uint64_t restarted = ob_spu2_samples_mixed() - m0;
	std::printf("spu2: %llu samples after the count restarted\n", static_cast<unsigned long long>(restarted));
	Check(restarted >= 480 && restarted <= 482, "an IOP cycle count that starts over keeps one sample per 768 cycles");
	Check(ob_spu2_next_event() > r && ob_spu2_next_event() <= r + 768, "next event in the restarted count's units");

	ob_spu2_close();
	std::printf("pcsx2_spu2: %d/%d\n", s_total - s_fail, s_total);
	return s_fail ? 1 : 0;
}
