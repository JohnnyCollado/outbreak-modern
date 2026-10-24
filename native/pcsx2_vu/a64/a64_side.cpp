// ob_pcsx2_vu API (../include/ob_pcsx2_vu.h, unchanged) over ARMSX2's ARM64 microVU, for libob_a64vu.so: VU state
// storage (ARMSX2's VuStateStore), state translation, the 64 MB RWX code cache, FPCR handling, program-cache
// invalidation by code generation, and the PATH1 sink behind the fake GIF unit. Built with ARMSX2 90126bd
// headers only; about two thirds is the x86 lift's ../src/pcsx2_side.cpp.
//
// Differences from x86 (study research/2026-10-24-armsx2-microvu-feasibility.md 1.3-1.4):
//   - VU1 data memory must be vuState.vu1Mem: generated code reaches it at a fixed offset from x19 = &vuRegs[1]
//     (kVU1MemFromState, microVU_Misc-arm64.h:339). So only InitOwned works; Init(vuData, vuCode) fails.
//   - No reach constraint for the code cache (every out-of-range branch/address has a long form,
//     AsmHelpers.cpp:380-450, 518-566): one anonymous mmap(PROT_READ|PROT_WRITE|PROT_EXEC), as PCSX2's
//     LnxHostSys.cpp:244-298 maps its JIT arena; cache flushes are __builtin___clear_cache (common_shims_a64.cpp).
//   - FPCR (rounding chop + flush-to-zero) instead of MXCSR, set around each run (the dispatcher skips its own
//     MSR FPCR when VU1FPCR == FPUFPCR, the default: mvuNeedsFPCRUpdate, microVU-arm64.cpp:509-515).
// The library builds with -fvisibility=hidden; the API (declared first here) is the one default-visible set,
// and exports.map keeps only ObPcsx2Vu::* in the dynamic symbol table.
#pragma GCC visibility push(default)
#include "ob_pcsx2_vu.h"
#include "ob_pcsx2_vu_api.h"
#pragma GCC visibility pop
#include "ob_a64mvu_fakes.h"

#include "Common.h"
#include "Config.h"
#include "R5900.h"
#include "VU.h"
#include "Vif.h"
#include "common/FPControl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <errno.h>
#include <sys/mman.h>

void ObMvuApplyConfigDefaults(); // config_defaults_a64.cpp

namespace ObMvuJit
{
	void Reserve();
	void Reset();
	void Shutdown();
	void Clear();
	void ClearLpState();
	void SetStartPC(u32 startPC);
	void Execute(u32 cycles);
	u32 LastCycles();
	u8* CacheBegin();
	u8* CacheEnd();
	u8* CachePtr();
} // namespace ObMvuJit

// ARMSX2 VU.h:259-265: the VU register files and VU1 data memory in one struct (VUmicroMem.cpp defines it upstream).
alignas(16) VuStateStore vuState;
// MemoryTypes.h:66 / R5900.h:326 (Memory.cpp / R5900.cpp upstream): VIF1 registers live in eeHw (vif1Regs,
// Vif.h:225); cpuRegs only for the cycle field microVU reads on VU0 paths.
alignas(__pagealignsize) u8 eeHw[Ps2MemSize::Hardware];
alignas(16) cpuRegistersPack _cpuRegistersPack;

namespace ObMvu
{
	FakeGifUnit g_gifUnit;
	FakeVuThread g_vu1Thread;

	namespace
	{
		constexpr size_t kCacheSize = HostMemoryMap::mVU1recSize; // 64 MB, ARMSX2 Memory.h:79
		u8* s_cache = nullptr;
		alignas(16) u8 s_vu0Mem[0x1000];
		alignas(16) u8 s_vu0Micro[0x1000];
		alignas(16) u8 s_vu1Micro[0x4000];
		ObPcsx2Vu::Path1Sink s_sink = nullptr;
		void* s_sinkCtx = nullptr;
		uint64_t s_codeGeneration = ~0ull;
		bool s_ready = false;
		bool s_flagHack = true;
		char s_describe[256];

		// mVUallocSFLAGd (microVU_Alloc-arm64.inl, same layout as x86 microVU_Alloc.inl:62-77): architectural
		// STATUS -> mVU's internal flag layout.
		u32 DenormalizeStatus(u32 st)
		{
			return ((st >> 3) & 0x18u) | ((st << 11) & 0x1800u) | ((st << 14) & 0x3cf0000u);
		}
	} // namespace

	void EmitPath1(const uint8_t* data, uint32_t size)
	{
		if (s_sink)
			s_sink(s_sinkCtx, data, size);
	}

	u8* FakeSysMemory::GetVU1Rec() { return s_cache; }
	u8* FakeSysMemory::GetVU1RecEnd() { return s_cache + kCacheSize; }
	u8* FakeSysMemory::GetVU0Rec() { return nullptr; }
	u8* FakeSysMemory::GetVU0RecEnd() { return nullptr; }
} // namespace ObMvu

namespace ObPcsx2Vu
{
	using namespace ObMvu;

	namespace
	{
		bool Fail(char* err, uint32_t errSize, const char* msg)
		{
			if (err && errSize)
				std::snprintf(err, errSize, "%s", msg);
			return false;
		}

		void ReleaseCache()
		{
			if (s_cache)
				munmap(s_cache, kCacheSize);
			s_cache = nullptr;
		}
	} // namespace

	bool Init(uint8_t* /*vuData*/, uint8_t* /*vuCode*/, char* err, uint32_t errSize)
	{
		return Fail(err, errSize,
			"ob_a64vu: Init(vuData, vuCode) is not supported on arm64 (VU1 data must be vuState.vu1Mem, at a fixed "
			"offset from the register file): use InitOwned + OwnedData()/OwnedCode()");
	}

	bool InitOwned(char* err, uint32_t errSize)
	{
		if (s_ready)
			return Fail(err, errSize, "ob_a64vu: Init called twice");
		void* p = mmap(nullptr, kCacheSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED)
		{
			char msg[160];
			std::snprintf(msg, sizeof(msg), "ob_a64vu: cannot map %zu MB RWX for the VU1 code cache (errno %d: %s)",
				kCacheSize >> 20, errno, std::strerror(errno));
			return Fail(err, errSize, msg);
		}
		s_cache = static_cast<u8*>(p);

		ObMvuApplyConfigDefaults();
		EmuConfig.Speedhacks.vuFlagHack = s_flagHack;
		// Study 1.4: REC_VU1 false would make GetGSPacketSize return sizes with bit 31 set (ARMSX2 Config.h:1856-1859).
		if (!REC_VU1 || THREAD_VU1 || EmuConfig.Cpu.Recompiler.UseVUProgramCache())
		{
			ReleaseCache();
			return Fail(err, errSize, "ob_a64vu: config defaults wrong (need EnableVU1, no vuThread, no VU program cache/vu-aot)");
		}

		std::memset(&vuState, 0, sizeof(vuState));
		vuRegs[0].idx = 0;
		vuRegs[0].Mem = s_vu0Mem;
		vuRegs[0].Micro = s_vu0Micro;
		vuRegs[1].idx = 1;
		vuRegs[1].Mem = vuState.vu1Mem;
		vuRegs[1].Micro = s_vu1Micro;
		ObMvuJit::Reserve();
		ObMvuJit::Reset();
		s_codeGeneration = ~0ull;
		std::snprintf(s_describe, sizeof(s_describe),
			"a64 mvu (ARMSX2 90126bd) cache %p-%p RWX, vu1 data %p (vuState.vu1Mem), vuFlagHack %d, FPCR vu1 %08llx ee %08llx",
			s_cache, s_cache + kCacheSize, vuState.vu1Mem, s_flagHack ? 1 : 0,
			static_cast<unsigned long long>(EmuConfig.Cpu.VU1FPCR.bitmask),
			static_cast<unsigned long long>(EmuConfig.Cpu.FPUFPCR.bitmask));
		s_ready = true;
		return true;
	}

	uint8_t* OwnedData() { return s_ready ? vuState.vu1Mem : nullptr; }
	uint8_t* OwnedCode() { return s_ready ? s_vu1Micro : nullptr; }

	void Shutdown()
	{
		if (!s_ready)
			return;
		ObMvuJit::Shutdown();
		ReleaseCache();
		s_ready = false;
	}

	void SetFlagHack(bool on) { s_flagHack = on; }

	void SetPath1Sink(Path1Sink sink, void* ctx)
	{
		s_sink = sink;
		s_sinkCtx = ctx;
	}

	const char* Describe() { return s_describe; }

	// As the x86 lift: valid between programs only (no pending pipeline state).
	void SetState(const State& s)
	{
		VURegs& v = vuRegs[1];
		for (int r = 0; r < 32; ++r)
			for (int l = 0; l < 4; ++l)
				v.VF[r].UL[l] = s.vf[r][l];
		for (int r = 0; r < 16; ++r)
			v.VI[r].UL = s.vi[r] & 0xFFFFu;
		for (int l = 0; l < 4; ++l)
			v.ACC.UL[l] = s.acc[l];
		v.VI[REG_Q].UL = s.q;
		v.pending_q = s.q;
		v.q.UL = s.q;
		v.VI[REG_P].UL = s.p;
		v.pending_p = s.p;
		v.p.UL = s.p;
		v.VI[REG_I].UL = s.i;
		v.VI[REG_R].UL = s.r;
		v.VI[REG_STATUS_FLAG].UL = s.status;
		v.VI[REG_MAC_FLAG].UL = s.mac;
		v.VI[REG_CLIP_FLAG].UL = s.clip;
		// What mVU itself stores at an E bit: every flag instance = the final flag.
		const u32 st = DenormalizeStatus(s.status);
		for (int k = 0; k < 4; ++k)
		{
			v.micro_macflags[k] = s.mac;
			v.micro_clipflags[k] = s.clip;
			v.micro_statusflags[k] = st;
		}
		v.VI[REG_TPC].UL = s.tpc >> 3;
		v.start_pc = s.tpc;
		v.nextBlockCycles = 0;
		std::memset(v.VIWrites, 0, sizeof(v.VIWrites)); // interpreter bookkeeping (ARMSX2 VU.h:199-204), not state
		ObMvuJit::ClearLpState();
	}

	void GetState(State& s)
	{
		const VURegs& v = vuRegs[1];
		for (int r = 0; r < 32; ++r)
			for (int l = 0; l < 4; ++l)
				s.vf[r][l] = v.VF[r].UL[l];
		for (int r = 0; r < 16; ++r)
			s.vi[r] = v.VI[r].UL & 0xFFFFu;
		for (int l = 0; l < 4; ++l)
			s.acc[l] = v.ACC.UL[l];
		s.q = v.VI[REG_Q].UL;
		s.p = v.VI[REG_P].UL;
		s.i = v.VI[REG_I].UL;
		s.r = v.VI[REG_R].UL;
		s.status = v.VI[REG_STATUS_FLAG].UL;
		s.mac = v.VI[REG_MAC_FLAG].UL;
		s.clip = v.VI[REG_CLIP_FLAG].UL;
		s.tpc = v.VI[REG_TPC].UL << 3;
	}

	uint32_t Tpc() { return (vuRegs[1].VI[REG_TPC].UL << 3) & 0x3FF8u; }

	RunResult Run(uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration, uint32_t cycles)
	{
		RunResult res{};
		if (!s_ready)
			return res;
		if (codeGeneration != s_codeGeneration)
		{
			ObMvuJit::Clear(); // Vif_Codes.cpp MPG calls CpuVU1->Clear before an upload
			s_codeGeneration = codeGeneration;
		}
		// vu1ExecMicro (ARMSX2 VU1micro.cpp:58-80): busy bit, TPC, SetStartPC; VU1 FBRST/VPU_STAT live in VU0.VI.
		VU0.VI[REG_FBRST].UL = fbrst;
		VU0.VI[REG_VPU_STAT].UL = (VU0.VI[REG_VPU_STAT].UL & ~0xFF00u) | 0x100u;
		VU1.VI[REG_TPC].UL = (startPC & 0x3FF8u) >> 3;
		ObMvuJit::SetStartPC(startPC & 0x3FF8u);
		vif1Regs.top = top;
		vif1Regs.itop = itop;
		// FPCR for the run, restored after (the x86 lift's MXCSR rule).
		const FPControlRegister saved = FPControlRegister::GetCurrent();
		FPControlRegister::SetCurrent(EmuConfig.Cpu.VU1FPCR);
		ObMvuJit::Execute(cycles);
		FPControlRegister::SetCurrent(saved);
		res.mvuCycles = ObMvuJit::LastCycles();
		res.ended = (VU0.VI[REG_VPU_STAT].UL & 0x100u) == 0u;
		res.stoppedByT = (VU0.VI[REG_VPU_STAT].UL & 0x400u) != 0u;
		return res;
	}
} // namespace ObPcsx2Vu

// The runner loads this library with dlopen (adapter/hook.cpp on Android) and takes the API from this table, so a
// library that fails to load is a logged fall-back to the interpreter, not an app that cannot start.
extern "C" __attribute__((visibility("default"))) const ObPcsx2Vu::Api* ob_pcsx2_vu_api(uint32_t callerVersion)
{
	static const ObPcsx2Vu::Api api = ObPcsx2Vu::LinkedApi();
	return callerVersion == ObPcsx2Vu::kApiVersion ? &api : nullptr;
}
