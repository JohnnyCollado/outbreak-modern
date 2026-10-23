// microVU (PCSX2 v2.9.134 pcsx2/x86/microVU.cpp + its .inl files) as one translation unit, without
// editing PCSX2. Every header microVU.h and microVU.cpp include is included first (they are #pragma once),
// then three names are redirected by macro so only microVU's own text sees the fakes:
//   gifUnit   -> ObMvu::g_gifUnit   (XGKICK hand-off, ob_mvu_fakes.h)
//   vu1Thread -> ObMvu::g_vu1Thread (MTVU off; addresses + no-op calls only)
//   SysMemory -> ObMvu::FakeSysMemory (VU1 code cache allocated near VU1 data, pcsx2_side.cpp)
// Research: research/2026-10-23-microvu-feasibility.md section 1.4.
#include "ob_mvu_fakes.h"

#include <deque>
#include <algorithm>
#include <memory>
#include "Common.h"
#include "VU.h"
#include "MTVU.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "x86/iR5900.h"
#include "R5900OpcodeTables.h"
#include "common/emitter/x86emitter.h"
#include "common/Perf.h"
#include "common/AlignedMalloc.h"
#include "common/StringUtil.h"

#define gifUnit ObMvu::g_gifUnit
#define vu1Thread ObMvu::g_vu1Thread
#define SysMemory ObMvu::FakeSysMemory

#include "x86/microVU.cpp"

#undef gifUnit
#undef vu1Thread
#undef SysMemory

// Thin exports for pcsx2_side.cpp (which cannot include microVU.h: it holds the whole JIT).
namespace ObMvuJit
{
	void Reserve() { CpuMicroVU1.Reserve(); }
	void Reset() { CpuMicroVU1.Reset(); }
	void Shutdown() { CpuMicroVU1.Shutdown(); }
	void Clear() { CpuMicroVU1.Clear(0, 0x4000); }
	void ClearLpState() { std::memset(&microVU1.prog.lpState, 0, sizeof(microVU1.prog.lpState)); }
	void Execute(u32 cycles) { CpuMicroVU1.Execute(cycles); }
	u32 LastCycles() { return static_cast<u32>(microVU1.cycles); }
	u8* CacheBegin() { return microVU1.cache; }
	u8* CacheEnd() { return microVU1.prog.x86end; }
	u8* CachePtr() { return microVU1.prog.x86ptr; }
} // namespace ObMvuJit
