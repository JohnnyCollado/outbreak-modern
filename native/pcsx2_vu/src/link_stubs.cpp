// Link-only stubs: the 49 symbols the linker reported unresolved for ob_mvu_jit.cpp (first link of
// ob_pcsx2_vu_compare, 2026-10-23). They belong to code VU1 never runs here:
//   - the EE recompiler / iCore register allocator and vtlb code generators: used by microVU_Macro.inl
//     (VU0 COP2 macro mode for the EE recompiler) and by microVU_IR.h only in cop2mode;
//   - VU0/VU glue (CpuVU0/1, vu1ExecMicro, VCALLMS, ...) used by PCSX2's VIF/EE side, not by CpuMicroVU1;
//   - hwIntcIrq: recMicroVU1::Execute raises INTC 7 on a T/D-bit interrupt; PS2Recomp reads VPU_STAT instead.
// Functions abort with their name (a call means the assumption above is wrong); data is zeroed storage.
#include "Common.h"
#include "Dmac.h"
#include "R5900OpcodeTables.h"
#include "VUmicro.h"
#include "vtlb.h"
#include "x86/iCore.h"
#include "x86/iR5900.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	[[noreturn]] void StubCalled(const char* name)
	{
		std::fprintf(stderr, "ob_pcsx2_vu: link-only stub called: %s\n", name);
		std::fflush(stderr);
		std::abort();
	}
} // namespace

#define OB_STUB(name) StubCalled(#name)

// ---- data (zeroed) ----
_x86regs x86regs[iREGCNT_GPR];
_xmmregs xmmregs[iREGCNT_XMM];
EEINST* g_pCurInstInfo = nullptr;
u16 g_x86AllocCounter = 0;
bool s_nBlockInterlocked = false;
alignas(16) GPR_reg64 g_cpuConstRegs[32];
u32 g_cpuHasConstReg = 0;
u32 g_cpuFlushedConstReg = 0;
u32 pc = 0;
BaseVUmicroCPU* CpuVU0 = nullptr;
BaseVUmicroCPU* CpuVU1 = nullptr;

// ---- iCore register allocator (x86/iCore.h) ----
int _allocTempXMMreg(XMMSSEType) { OB_STUB(_allocTempXMMreg); }
int _allocVFtoXMMreg(int, int) { OB_STUB(_allocVFtoXMMreg); }
int _allocGPRtoXMMreg(int, int) { OB_STUB(_allocGPRtoXMMreg); }
int _allocIfUsedGPRtoXMM(int, int) { OB_STUB(_allocIfUsedGPRtoXMM); }
int _checkXMMreg(int, int, int) { OB_STUB(_checkXMMreg); }
void _reallocateXMMreg(int, int, int, int, bool) { OB_STUB(_reallocateXMMreg); }
void _freeXMMreg(int) { OB_STUB(_freeXMMreg); }
void _freeXMMregWithoutWriteback(int) { OB_STUB(_freeXMMregWithoutWriteback); }
int _allocX86reg(int, int, int) { OB_STUB(_allocX86reg); }
bool _hasX86reg(int, int, int) { OB_STUB(_hasX86reg); }
int _allocIfUsedGPRtoX86(int, int) { OB_STUB(_allocIfUsedGPRtoX86); }
int _allocIfUsedVItoX86(int, int) { OB_STUB(_allocIfUsedVItoX86); }
void _freeX86reg(const x86Emitter::xRegister32&) { OB_STUB(_freeX86reg); }
void _freeX86reg(int) { OB_STUB(_freeX86reg); }
void _freeX86regWithoutWriteback(int) { OB_STUB(_freeX86regWithoutWriteback); }

// ---- EE recompiler (x86/iR5900.h) ----
bool TrySwapDelaySlot(u32, u32, u32, bool) { OB_STUB(TrySwapDelaySlot); }
u32 scaleblockcycles_clear() { OB_STUB(scaleblockcycles_clear); }
void iFlushCall(int) { OB_STUB(iFlushCall); }
void recCall(void (*)()) { OB_STUB(recCall); }
void _eeMoveGPRtoM(uptr, int) { OB_STUB(_eeMoveGPRtoM); }
void _eeMoveGPRtoR(const x86Emitter::xRegister32&, int, bool) { OB_STUB(_eeMoveGPRtoR); }
void _eeFlushAllDirty() { OB_STUB(_eeFlushAllDirty); }
void _deleteEEreg128(int) { OB_STUB(_deleteEEreg128); }
namespace R5900::Dynarec
{
	void recDoBranchImm(u32, u32*, bool, bool) { OB_STUB(recDoBranchImm); }
} // namespace R5900::Dynarec

// ---- vtlb code generators (vtlb.h) ----
int vtlb_DynGenReadQuad(u32, int, vtlb_ReadRegAllocCallback) { OB_STUB(vtlb_DynGenReadQuad); }
int vtlb_DynGenReadQuad_Const(u32, u32, vtlb_ReadRegAllocCallback) { OB_STUB(vtlb_DynGenReadQuad_Const); }
void vtlb_DynGenWrite(u32, bool, int, int) { OB_STUB(vtlb_DynGenWrite); }
void vtlb_DynGenWrite_Const(u32, bool, u32, int) { OB_STUB(vtlb_DynGenWrite_Const); }

// ---- VU glue (VUmicro.h, R5900OpcodeTables.h) ----
void BaseVUmicroCPU::ExecuteBlockJIT(BaseVUmicroCPU*, bool) { OB_STUB(ExecuteBlockJIT); }
void vu0ResetRegs() { OB_STUB(vu0ResetRegs); }
void vu1ResetRegs() { OB_STUB(vu1ResetRegs); }
void vu1ExecMicro(u32) { OB_STUB(vu1ExecMicro); }
void vu1Finish(bool) { OB_STUB(vu1Finish); }
void _vu0WaitMicro() { OB_STUB(_vu0WaitMicro); }
void _vu0FinishMicro() { OB_STUB(_vu0FinishMicro); }
void VCALLMS() { OB_STUB(VCALLMS); }
void VCALLMSR() { OB_STUB(VCALLMSR); }

// ---- interrupts (Dmac.h): PS2Recomp reads VPU_STAT after the run instead ----
void hwIntcIrq(int) {}
