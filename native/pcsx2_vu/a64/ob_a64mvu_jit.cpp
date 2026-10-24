// ARMSX2 (90126bd) ARM64 microVU, pcsx2/arm64/microVU-arm64.cpp + every .inl it and microVU-arm64.h include, as
// one translation unit, without editing ARMSX2. Same scheme as the x86 lift (../src/ob_mvu_jit.cpp): every
// header that declares one of the three redirected names (and every other non-microVU header the TU includes)
// is included first (all #pragma once), then the names are redirected by macro so only microVU's own text
// sees the fakes:
//   gifUnit   -> ObMvu::g_gifUnit       (XGKICK hand-off, ob_a64mvu_fakes.h)
//   vu1Thread -> ObMvu::g_vu1Thread     (MTVU off; addresses + no-op calls only)
//   SysMemory -> ObMvu::FakeSysMemory   (the VU1 code cache mapped by a64_side.cpp)
// The vu-aot headers come first, as microVU-arm64.cpp:4-10 requires (they name things microVU's headers define
// as macros). Study: research/2026-10-24-armsx2-microvu-feasibility.md sections 1.2-1.6.
#include "vuaot/VuAotBackground.h"
#include "vuaot/VuAotLive.h"
#include "vuaot/VuAotPersist.h"
#include "vuaot/VuAotRegion.h"
#include "vuaot/VuAotTier.h"
#include "vuaot/VuAotMvuTrace.h"

#include "ob_a64mvu_fakes.h"

#include "Common.h"
#include "Config.h"
#include "VU.h"
#include "MTVU.h"
#include "GS.h"
#include "Gif_Unit.h"
#include "Memory.h"
#include "EECycleRate.h"
#include "EeFpuModel.h"
#include "VuEfuModel.h"
#include "VuMulBand.h"
#include "R5900OpcodeTables.h"
#include "SaveState.h"
#include "VU1Trace.h"
#include "vu_capture.h"
#include "vtlb.h"
#include "arm64/AsmHelpers.h"
#include "arm64/iR5900-arm64.h"
#include "arm64/iCore-arm64.h"
#include "common/AlignedMalloc.h"
#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/Path.h"
#include "common/Perf.h"
#include "common/StringUtil.h"
#include "fmt/format.h"
#include "xxhash.h"

#define gifUnit ObMvu::g_gifUnit
#define vu1Thread ObMvu::g_vu1Thread
#define SysMemory ObMvu::FakeSysMemory

#include "arm64/microVU-arm64.cpp"

#undef gifUnit
#undef vu1Thread
#undef SysMemory

// Thin exports for a64_side.cpp (which cannot include microVU-arm64.h: it holds the whole JIT). Same names as
// the x86 lift's ObMvuJit.
namespace ObMvuJit
{
	void Reserve() { CpuMicroVU1.Reserve(); }
	void Reset() { CpuMicroVU1.Reset(); }
	void Shutdown() { CpuMicroVU1.Shutdown(); }
	void Clear() { CpuMicroVU1.Clear(0, 0x4000); }
	void ClearLpState() { std::memset(&microVU1.prog.lpState, 0, sizeof(microVU1.prog.lpState)); }
	// vu1ExecMicro (ARMSX2 VU1micro.cpp:74) calls SetStartPC on every kick (MSCAL and MSCNT): it disarms a
	// parked resume (microVU-arm64.cpp:2397-2401).
	void SetStartPC(u32 startPC) { CpuMicroVU1.SetStartPC(startPC); }
	void Execute(u32 cycles) { CpuMicroVU1.Execute(cycles); }
	u32 LastCycles() { return static_cast<u32>(microVU1.cycles); }
	u8* CacheBegin() { return microVU1.cache; }
	u8* CacheEnd() { return microVU1.prog.x86end; }
	u8* CachePtr() { return microVU1.prog.x86ptr; }
} // namespace ObMvuJit
