// ob_pcsx2_vu: PCSX2 v2.9.134 microVU (VU1 recompiler) as a static library. Plain-types API: this header
// includes no PCSX2 or PS2Recomp header, so either side can include it (the two header sets never meet).
//
// One VU1 only, single-threaded (MTVU off). The caller owns the 16 KB VU1 data and code memories; Init
// points PCSX2's vuRegs[1].Mem / .Micro at them, so no copy is made per run.
#pragma once

#include <cstdint>

namespace ObPcsx2Vu
{
	// Architectural VU1 state between programs (all floats as raw bits).
	struct State
	{
		uint32_t vf[32][4];
		uint32_t vi[16];  // 16-bit integer registers (upper bits ignored on Set, zero on Get)
		uint32_t acc[4];
		uint32_t q, p, i, r;
		uint32_t status;  // architectural STATUS flag (12 bits)
		uint32_t mac;
		uint32_t clip;
		uint32_t tpc;     // byte address of the next instruction (TPC * 8)
	};

	struct RunResult
	{
		uint32_t mvuCycles;   // cycles microVU counted for the run
		bool ended;           // E bit reached (VPU_STAT bit 0x100 cleared)
		bool stoppedByT;      // T bit stop (VPU_STAT bit 0x400)
	};

	using Path1Sink = void (*)(void* ctx, const uint8_t* data, uint32_t size);

	// PCSX2's vuFlagHack speed hack (on by default, Pcsx2Config.cpp:378-387): STATUS is computed only for
	// blocks that read it. Call before Init to change it (diagnostics: --no-flag-hack in the compare tool).
	void SetFlagHack(bool on);

	// Allocates the 64 MB code cache within +-2 GB of vuData and checks the reach of every byte of the
	// cache to every byte of vuData (fails with a message in err otherwise; checked in every build).
	// Sets the PCSX2 config defaults (EmuConfig ctors copied verbatim) and x86Emitter::use_avx.
	bool Init(uint8_t* vuData, uint8_t* vuCode, char* err, uint32_t errSize);
	void Shutdown();
	void SetPath1Sink(Path1Sink sink, void* ctx);

	void SetState(const State& s);
	void GetState(State& s);

	// One MSCAL-style run from startPC (bytes). codeGeneration: when it differs from the previous run's,
	// microVU's program cache is cleared (CpuMicroVU1.Clear(0, 0x4000), as VIF MPG does). MXCSR is set
	// to EmuConfig.Cpu.VU1FPCR for the run and restored after. fbrst = VU0 FBRST (T enable bit 11).
	RunResult Run(uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration,
		uint32_t cycles = 3000000);

	// Cache address, VU data address and the worst-case distance between them, for logs.
	const char* Describe();
} // namespace ObPcsx2Vu
