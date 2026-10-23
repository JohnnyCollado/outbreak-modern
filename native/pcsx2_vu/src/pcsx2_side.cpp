// ob_pcsx2_vu API (include/ob_pcsx2_vu.h) over PCSX2's microVU: VU state storage, state translation,
// code-cache allocation near VU1 data with a reach check, MXCSR handling, program-cache invalidation by
// code generation, and the PATH1 sink behind the fake GIF unit. Built with PCSX2 headers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ob_pcsx2_vu.h"
#include "ob_mvu_fakes.h"

#include "Common.h"
#include "Config.h"
#include "VU.h"
#include "Vif.h"
#include "GS/MultiISA.h"
#include "common/emitter/x86emitter.h"
#include "x86/iR5900.h"

#include <cstdio>
#include <cstring>
#include <xmmintrin.h>

#include <windows.h>

void ObMvuApplyConfigDefaults(); // config_defaults.cpp

namespace ObMvuJit
{
	void Reserve();
	void Reset();
	void Shutdown();
	void Clear();
	void ClearLpState();
	void Execute(u32 cycles);
	u32 LastCycles();
	u8* CacheBegin();
	u8* CacheEnd();
	u8* CachePtr();
} // namespace ObMvuJit

// PCSX2 VUmicroMem.cpp:8 (VU0/VU1 register files; vuRegs[1].Mem/.Micro point at the caller's memory).
alignas(16) VURegs vuRegs[2];

namespace ObMvu
{
	FakeGifUnit g_gifUnit;
	FakeVuThread g_vu1Thread;

	namespace
	{
		constexpr size_t kCacheSize = HostMemoryMap::mVU1recSize; // 64 MB, Memory.h:79
		u8* s_cache = nullptr;
		u8* s_vuData = nullptr;
		alignas(16) u8 s_vu0Mem[0x1000];
		alignas(16) u8 s_vu0Micro[0x1000];
		Path1Sink s_sink = nullptr;
		void* s_sinkCtx = nullptr;
		uint64_t s_codeGeneration = ~0ull;
		bool s_ready = false;
		bool s_flagHack = true;
		char s_describe[256];

		// First free region of `size` bytes whose every byte is within `maxDist` of [lo, hi).
		u8* AllocNear(const u8* lo, const u8* hi, size_t size, size_t maxDist)
		{
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			const uptr gran = si.dwAllocationGranularity;
			const uptr minAddr = reinterpret_cast<uptr>(si.lpMinimumApplicationAddress);
			const uptr maxAddr = reinterpret_cast<uptr>(si.lpMaximumApplicationAddress);
			const uptr from = std::max<uptr>(minAddr, (reinterpret_cast<uptr>(hi) > maxDist) ? reinterpret_cast<uptr>(hi) - maxDist : 0);
			const uptr to = std::min<uptr>(maxAddr, reinterpret_cast<uptr>(lo) + maxDist);
			uptr addr = (from + gran - 1) & ~(gran - 1);
			while (addr + size <= to)
			{
				MEMORY_BASIC_INFORMATION mbi;
				if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
					break;
				const uptr regionEnd = reinterpret_cast<uptr>(mbi.BaseAddress) + mbi.RegionSize;
				if (mbi.State == MEM_FREE)
				{
					const uptr start = (addr + gran - 1) & ~(gran - 1);
					if (start + size <= regionEnd && start + size <= to)
					{
						if (void* p = VirtualAlloc(reinterpret_cast<void*>(start), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
							return static_cast<u8*>(p);
					}
				}
				addr = regionEnd;
			}
			return nullptr;
		}

		// Largest |a - b| over a in [a0, a1) and b in [b0, b1).
		uint64_t WorstDistance(const void* a0, const void* a1, const void* b0, const void* b1)
		{
			const sptr x = reinterpret_cast<sptr>(a1) - reinterpret_cast<sptr>(b0);
			const sptr y = reinterpret_cast<sptr>(b1) - reinterpret_cast<sptr>(a0);
			return static_cast<uint64_t>(std::max(std::abs(x), std::abs(y)));
		}

		// disp32 reach: EmitSibMagic (common/emitter/x86emitter.cpp:298-330) encodes an absolute address
		// rip-relative when (address - end of instruction) fits s32, i.e. within 2 GB of the code; the margin
		// covers the instruction length and the 64 KB allocation granularity.
		constexpr uint64_t kReach = 0x7FFF0000ull;
		// Search distance for AllocNear: every byte of the cache within kReach - 64 KB of every byte of data.
		constexpr size_t kSearch = static_cast<size_t>(kReach - 0x10000ull);

		// One reservation for the VU1 memories and the code cache (InitOwned): [0, 64 KB) data + code (RW),
		// [64 KB, 64 KB + 64 MB) the cache (RWX). Worst cache<->data distance is 64 MB + 16 KB, whatever
		// else the process maps later (window, GPU driver, Vulkan): placement needs no luck.
		constexpr size_t kOwnedHead = 0x10000;
		u8* s_reservation = nullptr;
		u8* s_ownedData = nullptr;
		u8* s_ownedCode = nullptr;

		// mVUallocSFLAGd (microVU_Alloc.inl:62-77): architectural STATUS -> mVU's internal flag layout.
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
			if (s_reservation)
				VirtualFree(s_reservation, 0, MEM_RELEASE); // the cache is part of it
			else if (s_cache)
				VirtualFree(s_cache, 0, MEM_RELEASE);
			s_reservation = s_ownedData = s_ownedCode = nullptr;
			s_cache = nullptr;
		}

		// Shared tail of Init / InitOwned, with s_cache placed: reach check, then microVU up.
		bool Start(uint8_t* vuData, uint8_t* vuCode, char* err, uint32_t errSize)
		{
			ObMvuApplyConfigDefaults();
			EmuConfig.Speedhacks.vuFlagHack = s_flagHack;
			x86Emitter::use_avx = g_cpu.vectorISA >= ProcessorFeatures::VectorISA::AVX; // VMManager.cpp:407
			const uint64_t dData = WorstDistance(s_cache, s_cache + kCacheSize, vuData, vuData + 0x4000);
			// Globals are reached through rbx = &cpuRegs.GPR.r[9] (iR5900.h:31): they must be near it.
			const u8* text = reinterpret_cast<const u8*>(R5900_TEXTPTR);
			const uint64_t dGlobals = std::max({
				WorstDistance(&vuRegs[0], &vuRegs[2], text, text + 16),
				WorstDistance(&g_vu1Thread, &g_vu1Thread + 1, text, text + 16),
				WorstDistance(eeHw, eeHw + sizeof(eeHw), text, text + 16),
				WorstDistance(&EmuConfig, &EmuConfig + 1, text, text + 16)});
			std::snprintf(s_describe, sizeof(s_describe),
				"mvu cache %p-%p, vu1 data %p, worst cache<->data %llu MB, globals<->text %llu MB, vuFlagHack %d",
				s_cache, s_cache + kCacheSize, vuData,
				static_cast<unsigned long long>(dData >> 20), static_cast<unsigned long long>(dGlobals >> 20), s_flagHack ? 1 : 0);
			if (dData >= kReach || dGlobals >= kReach)
			{
				ReleaseCache();
				char msg[320];
				std::snprintf(msg, sizeof(msg), "ob_pcsx2_vu: out of disp32 reach: %s", s_describe);
				return Fail(err, errSize, msg);
			}

			s_vuData = vuData;
			vuRegs[0].idx = 0;
			vuRegs[0].Mem = s_vu0Mem;
			vuRegs[0].Micro = s_vu0Micro;
			vuRegs[1].idx = 1;
			vuRegs[1].Mem = vuData;
			vuRegs[1].Micro = vuCode;
			ObMvuJit::Reserve();
			ObMvuJit::Reset();
			s_codeGeneration = ~0ull;
			s_ready = true;
			return true;
		}
	} // namespace

	bool Init(uint8_t* vuData, uint8_t* vuCode, char* err, uint32_t errSize)
	{
		if (s_ready)
			return Fail(err, errSize, "ob_pcsx2_vu: Init called twice");
		if (!vuData || !vuCode)
			return Fail(err, errSize, "ob_pcsx2_vu: null VU1 memory");
		// Caveat 1 (study section 1, Memory.cpp:98-117): generated code addresses VU data by disp32
		// (rip-relative or from the rbx text pointer); out of reach is only a pxAssertMsg (compiled out).
		s_cache = AllocNear(vuData, vuData + 0x4000, kCacheSize, kSearch);
		if (!s_cache)
			return Fail(err, errSize, "ob_pcsx2_vu: no free 64 MB range within 2 GB of VU1 data");
		return Start(vuData, vuCode, err, errSize);
	}

	bool InitOwned(char* err, uint32_t errSize)
	{
		if (s_ready)
			return Fail(err, errSize, "ob_pcsx2_vu: Init called twice");
		void* base = VirtualAlloc(nullptr, kOwnedHead + kCacheSize, MEM_RESERVE, PAGE_NOACCESS);
		if (!base)
		{
			char msg[160];
			std::snprintf(msg, sizeof(msg), "ob_pcsx2_vu: cannot reserve %zu MB for VU1 memory + code cache (error %lu)",
				(kOwnedHead + kCacheSize) >> 20, static_cast<unsigned long>(GetLastError()));
			return Fail(err, errSize, msg);
		}
		u8* b = static_cast<u8*>(base);
		if (!VirtualAlloc(b, kOwnedHead, MEM_COMMIT, PAGE_READWRITE) ||
			!VirtualAlloc(b + kOwnedHead, kCacheSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE))
		{
			const unsigned long e = GetLastError();
			VirtualFree(base, 0, MEM_RELEASE);
			char msg[160];
			std::snprintf(msg, sizeof(msg), "ob_pcsx2_vu: cannot commit VU1 memory + code cache (error %lu)", e);
			return Fail(err, errSize, msg);
		}
		s_reservation = b;
		s_ownedData = b;          // 16 KB VU1 data
		s_ownedCode = b + 0x4000; // 16 KB VU1 code (not reached by generated code; here for one owner)
		s_cache = b + kOwnedHead;
		return Start(s_ownedData, s_ownedCode, err, errSize);
	}

	uint8_t* OwnedData() { return s_ready ? s_ownedData : nullptr; }
	uint8_t* OwnedCode() { return s_ready ? s_ownedCode : nullptr; }

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

	// Study section 2.2: valid between programs only (no pending pipeline state).
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
		// What mVU itself stores at an E bit (microVU_Branch.inl:101-113): every flag instance = the final flag.
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
		ObMvuJit::ClearLpState(); // mVU clears it at the E bit (microVU_Branch.inl:56-59)
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
			ObMvuJit::Clear(); // Vif_Codes.cpp:337-365 calls CpuVU1->Clear before an MPG upload
			s_codeGeneration = codeGeneration;
		}
		// vu1ExecMicro (VU1micro.cpp:49-80): busy bit, TPC, start pc; VU1 FBRST/VPU_STAT live in VU0.VI.
		VU0.VI[REG_FBRST].UL = fbrst;
		VU0.VI[REG_VPU_STAT].UL = (VU0.VI[REG_VPU_STAT].UL & ~0x0600u) | 0x100u;
		VU1.VI[REG_TPC].UL = (startPC & 0x3FF8u) >> 3;
		VU1.start_pc = startPC & 0x3FF8u;
		vif1Regs.top = top;
		vif1Regs.itop = itop;
		// Study 1.7: the dispatcher emits no LDMXCSR when VU1FPCR == FPUFPCR (the defaults), so set it here.
		const unsigned int savedCsr = _mm_getcsr();
		_mm_setcsr(EmuConfig.Cpu.VU1FPCR.bitmask);
		ObMvuJit::Execute(cycles);
		_mm_setcsr(savedCsr);
		res.mvuCycles = ObMvuJit::LastCycles();
		res.ended = (VU0.VI[REG_VPU_STAT].UL & 0x100u) == 0u;
		res.stoppedByT = (VU0.VI[REG_VPU_STAT].UL & 0x400u) != 0u;
		return res;
	}
} // namespace ObPcsx2Vu
