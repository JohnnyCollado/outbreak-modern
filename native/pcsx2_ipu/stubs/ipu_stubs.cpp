// ipu_stubs: what PCSX2's IPU sources reference but this library does not use.
#include "Common.h"
#include "SaveState.h"

// IPU.cpp ipuFreeze / IPUdma.cpp ipuDmaFreeze (save states: not used here).
bool SaveStateBase::FreezeTag(const char*)
{
	return false;
}
