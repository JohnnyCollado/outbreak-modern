// Link-only stubs for libob_a64vu.so: the symbols llvm-nm -u reported unresolved for ob_a64mvu_jit.o + AsmHelpers.o
// + the 11 vuaot objects (2026-10-24, study research/2026-10-24-armsx2-microvu-feasibility.md 1.6) that belong to
// code VU1 never runs here:
//   - EE recompiler register allocator / COP2 pins: microVU-arm64.cpp:2669-2758 (VU0 COP2 macro mode only);
//   - CpuVU0/CpuVU1, hwIntcIrq: PCSX2's VIF/EE glue (PS2Recomp reads VPU_STAT after the run instead);
//   - the FPU/EFU "hardware model" (EeFpuModel.h, VuEfuModel.h, VuMulBand.h): called only at vuClampMode 4
//     (CHECK_VU_EXACT), which our defaults keep off; the stubs that reach them are always emitted
//     (mVUGenerateModelStubs, microVU-arm64.cpp:1153-1161), so the symbols must link;
//   - EECycleRate: VU0-only cycle scaling (_Compile-arm64.inl:507).
// Functions that must never run abort with their name; data is zeroed storage. Built with ARMSX2 headers.
#include "Common.h"
#include "EECycleRate.h"
#include "EeFpuModel.h"
#include "VUmicro.h"
#include "VuEfuModel.h"
#include "VuMulBand.h"
#include "vtlb.h"
#include "arm64/AsmHelpers.h"
#include "arm64/iCore-arm64.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	[[noreturn]] void StubCalled(const char* name)
	{
		std::fprintf(stderr, "ob_a64vu: link-only stub called: %s\n", name);
		std::fflush(stderr);
		std::abort();
	}
} // namespace

#define OB_STUB(name) StubCalled(#name)

// ---- data (zeroed) ----
BaseVUmicroCPU* CpuVU0 = nullptr;
BaseVUmicroCPU* CpuVU1 = nullptr;
VuMulBandSlot g_vuMulBand[2];
namespace vtlb_private
{
	alignas(64) MapData vtlbdata;
} // namespace vtlb_private

// ---- EE recompiler (arm64/iCore-arm64.h, AsmHelpers.h:213-214): COP2 macro mode ----
void _freeArm64GPR(int) { OB_STUB(_freeArm64GPR); }
void _freeArm64GPRregs() { OB_STUB(_freeArm64GPRregs); }
void _freeNEONreg(int) { OB_STUB(_freeNEONreg); }
void _freeNEONregs() { OB_STUB(_freeNEONregs); }
void armEmitEEClobberedPinFlushForCOP2() { OB_STUB(armEmitEEClobberedPinFlushForCOP2); }
void armEmitEEClobberedPinReloadForCOP2() { OB_STUB(armEmitEEClobberedPinReloadForCOP2); }

// ---- interrupts: recMicroVU1::Execute raises INTC 7 on a T/D-bit stop; PS2Recomp reads VPU_STAT instead ----
void hwIntcIrq(int) {}

// ---- EECycleRate (VU0 only) ----
namespace EECycleRate
{
	s8 GetEffective() { return 0; }
	void NoteVuReset(int) {}
} // namespace EECycleRate

// ---- FPU / EFU hardware model (exact mode, off) ----
namespace EeFpuModel
{
	EEFPU_MODEL_CALL u32 Divide(u32, u32) { OB_STUB(EeFpuModel::Divide); }
	EEFPU_MODEL_CALL u32 SqrtBits(u32) { OB_STUB(EeFpuModel::SqrtBits); }
	EEFPU_MODEL_CALL u32 RecipSqrt(u32, u32) { OB_STUB(EeFpuModel::RecipSqrt); }
} // namespace EeFpuModel

namespace VuEfuModel
{
	EEFPU_MODEL_CALL u32 Sum(u32, u32, u32, u32) { OB_STUB(VuEfuModel::Sum); }
	EEFPU_MODEL_CALL u32 SquareSum(u32, u32, u32) { OB_STUB(VuEfuModel::SquareSum); }
	EEFPU_MODEL_CALL u32 RecipSquareSum(u32, u32, u32) { OB_STUB(VuEfuModel::RecipSquareSum); }
	EEFPU_MODEL_CALL u32 Length(u32, u32, u32) { OB_STUB(VuEfuModel::Length); }
	EEFPU_MODEL_CALL u32 RecipLength(u32, u32, u32) { OB_STUB(VuEfuModel::RecipLength); }
	EEFPU_MODEL_CALL u32 Recip(u32) { OB_STUB(VuEfuModel::Recip); }
	EEFPU_MODEL_CALL u32 Sqrt(u32) { OB_STUB(VuEfuModel::Sqrt); }
	EEFPU_MODEL_CALL u32 RecipSqrt(u32) { OB_STUB(VuEfuModel::RecipSqrt); }
	EEFPU_MODEL_CALL u32 Sin(u32) { OB_STUB(VuEfuModel::Sin); }
	EEFPU_MODEL_CALL u32 Exp(u32) { OB_STUB(VuEfuModel::Exp); }
	EEFPU_MODEL_CALL u32 Atan(u32) { OB_STUB(VuEfuModel::Atan); }
	EEFPU_MODEL_CALL u32 AtanRatio(u32, u32) { OB_STUB(VuEfuModel::AtanRatio); }
} // namespace VuEfuModel

EEFPU_MODEL_CALL void vuMulShortTailBandVu0() { OB_STUB(vuMulShortTailBandVu0); }
EEFPU_MODEL_CALL void vuMulShortTailBandVu1() { OB_STUB(vuMulShortTailBandVu1); }
