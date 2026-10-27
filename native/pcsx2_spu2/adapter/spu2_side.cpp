// spu2_side: PCSX2's SPU2 behind ob_spu2_bridge.h. It stands in for the parts of PCSX2's IOP the SPU2 touches:
// the cycle counter (psxRegs.cycle), counter 6 (the SPU2's tick, IopCounters.cpp:190, :483), the DMA 4/7
// registers (iopHw), IOP RAM (iopMem, pointed at PS2Recomp's) and the interrupt calls (IopDma.cpp spu2DMA4Irq /
// spu2DMA7Irq, IopIrq.cpp spu2Irq), and gives AudioStream a pull backend instead of Cubeb.
#include "ob_spu2_bridge.h"

#include "Host/AudioStream.h"
#include "IopCounters.h"
#include "IopHw.h"
#include "IopMem.h"
#include "R3000A.h"
#include "SPU2/spu2.h"

#include <atomic>

namespace
{
	constexpr u32 kTick = 768; // IOP cycles per 48 kHz sample (PCSX2 counter 6 rate)

	std::atomic<uint32_t> s_events{0};
	bool s_open = false;
	uint64_t s_now = 0;  // the caller's IOP cycle at the last call (its units: ob_spu2_next_event answers in them)
	uint64_t s_base = 0; // added to it for the SPU2's clock, which never runs backwards: PS2Recomp restarts its IOP
	                     // cycle count at 0 when the game reboots the IOP (iop_emulator.cpp reset), and the SPU2's
	                     // TimeUpdate would read that as a huge jump (spu2sys.cpp:317-338)

	// Pull backend: the host audio callback (any thread) reads what the SPU2 wrote, through AudioStream's
	// ring buffer and time stretch (SoundTouch), as Cubeb's data callback does.
	class ObPullStream final : public AudioStream
	{
	public:
		ObPullStream(u32 rate, const AudioStreamParameters& p, bool stretch)
			: AudioStream(rate, p)
		{
			BaseInitialize(&StereoSampleReaderImpl, stretch);
		}
		void Pull(float* frames, u32 count) { ReadFrames(frames, count); }
	};
	std::atomic<ObPullStream*> s_stream{nullptr};
	std::atomic<uint64_t> s_mixed{0};

	void SetNow(uint64_t cycle)
	{
		if (cycle < s_now)
			s_base += s_now - cycle;
		s_now = cycle;
		psxRegs.cycle = static_cast<u32>(cycle + s_base);
	}

	// Counter 6 after it fired (IopCounters.cpp:483-484): the next tick one sample on.
	void RearmTick()
	{
		psxCounters[6].startCycle = psxRegs.cycle;
		psxCounters[6].deltaCycles = kTick;
	}
} // namespace

// ---- PCSX2 IOP globals the SPU2 reads (normally R3000A.cpp, IopCounters.cpp, IopMem.cpp, IopHw.cpp) ----
alignas(16) psxRegisters psxRegs;
psxCounter psxCounters[NUM_COUNTERS];
s32 psxNextDeltaCounter;
u64 psxNextStartCounter;
alignas(__pagealignsize) u8 iopHw[Ps2MemSize::IopHardware]; // as declared in MemoryTypes.h (Android: clang checks it)
IopVM_MemoryAllocMess* iopMem = nullptr;

void spu2Irq()
{
	s_events.fetch_or(OB_SPU2_EV_IRQ, std::memory_order_relaxed);
}

void spu2DMA4Irq()
{
	SPU2interruptDMA4();
	if (HW_DMA4_CHCR & 0x01000000)
	{
		HW_DMA4_CHCR &= ~0x01000000;
		s_events.fetch_or(OB_SPU2_EV_DMA4, std::memory_order_relaxed);
	}
}

void spu2DMA7Irq()
{
	SPU2interruptDMA7();
	if (HW_DMA7_CHCR & 0x01000000)
	{
		HW_DMA7_CHCR &= ~0x01000000;
		s_events.fetch_or(OB_SPU2_EV_DMA7, std::memory_order_relaxed);
	}
}

// AudioStream::CreateStream's Cubeb case (EmuConfig.SPU2.Backend defaults to Cubeb): the pull stream.
std::unique_ptr<AudioStream> AudioStream::CreateCubebAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
	const char*, const char*, bool stretch_enabled, Error*)
{
	auto stream = std::make_unique<ObPullStream>(sample_rate, parameters, stretch_enabled);
	s_stream.store(stream.get(), std::memory_order_release);
	return stream;
}

std::unique_ptr<AudioStream> AudioStream::CreateSDLAudioStream(u32 sample_rate, const AudioStreamParameters& parameters,
	bool stretch_enabled, Error* error)
{
	return CreateCubebAudioStream(sample_rate, parameters, nullptr, nullptr, stretch_enabled, error);
}

bool ob_spu2_open(uint8_t* iop_ram, uint32_t ram_size, uint64_t cycle)
{
	if (s_open || !iop_ram || ram_size == 0 || (ram_size & (ram_size - 1)) != 0)
		return false;
	// iopPhysMem(addr) = &iopMem->Main[addr & (ExposedIopRam - 1)] (IopMem.h:40-43): only Main is used, and the
	// mask keeps every address inside PS2Recomp's ram_size bytes.
	iopMem = reinterpret_cast<IopVM_MemoryAllocMess*>(iop_ram);
	Ps2MemSize::ExposedIopRam = ram_size;
	SetNow(cycle);
	psxNextStartCounter = psxRegs.cycle;
	psxNextDeltaCounter = kTick;
	RearmTick();
	s_events.store(0, std::memory_order_relaxed);
	s_mixed.store(0, std::memory_order_relaxed);
	s_open = SPU2::Open();
	return s_open;
}

void ob_spu2_close()
{
	if (!s_open)
		return;
	s_stream.store(nullptr, std::memory_order_release);
	SPU2::Close();
	s_open = false;
}

uint16_t ob_spu2_read(uint32_t phys, uint64_t cycle)
{
	if (!s_open)
		return 0;
	SetNow(cycle);
	return SPU2read(phys);
}

void ob_spu2_write(uint32_t phys, uint16_t value, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	SPU2write(phys, value);
}

void ob_spu2_dma(uint32_t core, uint32_t madr, uint32_t halfwords, bool to_spu2, uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	// As IopDma.cpp psxDmaGeneric: the channel busy (CHCR bit 24, cleared by spu2DMA4Irq/7Irq), then the SPU2's
	// DMA entry with the RAM pointer.
	u16* mem = reinterpret_cast<u16*>(iopPhysMem(madr));
	if (core == 0)
	{
		HW_DMA4_MADR = madr;
		HW_DMA4_CHCR |= 0x01000000;
		to_spu2 ? SPU2writeDMA4Mem(mem, halfwords) : SPU2readDMA4Mem(mem, halfwords);
	}
	else
	{
		HW_DMA7_MADR = madr;
		HW_DMA7_CHCR |= 0x01000000;
		to_spu2 ? SPU2writeDMA7Mem(mem, halfwords) : SPU2readDMA7Mem(mem, halfwords);
	}
}

void ob_spu2_advance(uint64_t cycle)
{
	if (!s_open)
		return;
	SetNow(cycle);
	const u64 before = lClocks;
	SPU2async(); // TimeUpdate(psxRegs.cycle): mixes every whole sample up to now
	if (lClocks > before)
		s_mixed.fetch_add((lClocks - before) / kTick, std::memory_order_relaxed);
	if (static_cast<s32>(psxRegs.cycle - static_cast<u32>(psxCounters[6].startCycle)) >= psxCounters[6].deltaCycles)
		RearmTick();
}

uint64_t ob_spu2_next_event()
{
	if (!s_open)
		return UINT64_MAX;
	// counter 6's deadline relative to now (u32 arithmetic, as CounterUpdate), as an absolute 64-bit cycle.
	const s32 left = static_cast<s32>(static_cast<u32>(psxCounters[6].startCycle) + psxCounters[6].deltaCycles - psxRegs.cycle);
	return s_now + static_cast<uint64_t>(left > 0 ? left : 0);
}

uint32_t ob_spu2_take_events()
{
	return s_events.exchange(0, std::memory_order_relaxed);
}

uint32_t ob_spu2_sample_rate()
{
	return SPU2::SAMPLE_RATE;
}

void ob_spu2_pull(float* frames, uint32_t count)
{
	if (ObPullStream* s = s_stream.load(std::memory_order_acquire))
		s->Pull(frames, count);
	else
		for (uint32_t i = 0; i < count * 2; i++)
			frames[i] = 0.0f;
}

uint64_t ob_spu2_samples_mixed()
{
	return s_mixed.load(std::memory_order_relaxed);
}
