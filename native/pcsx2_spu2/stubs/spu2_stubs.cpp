// Link stubs for ob_pcsx2_spu2: what PCSX2's SPU2 and Host/AudioStream.cpp call outside themselves that the
// runner has no use for. The IOP globals and the output stream are in adapter/spu2_side.cpp.
#include "GS/GSCapture.h"
#include "Host.h"
#include "Host/AudioStream.h"
#include "SPU2/defs.h"
#include "VMManager.h"

namespace Ps2MemSize
{
	u32 ExposedIopRam = 0x200000; // set by ob_spu2_open to PS2Recomp's IOP RAM size (2 MB)
}

// The VM always runs at normal speed: no fast-forward volume, nominal rate 1 (spu2.cpp:158-206).
VMState VMManager::GetState() { return VMState::Running; }
float VMManager::GetTargetSpeed() { return 1.0f; }
bool VMManager::IsTargetSpeedAdjustedToHost() { return false; }

// spu2.cpp:535: audio packets for video capture (FFmpeg, not built).
void GSCapture::DeliverAudioPacket(const float*) {}

// AudioStream's device lists (settings UI): no Cubeb here.
std::vector<std::pair<std::string, std::string>> AudioStream::GetCubebDriverNames() { return {}; }
std::vector<AudioStream::DeviceInfo> AudioStream::GetCubebOutputDevices(const char*) { return {}; }

// SPU2freeze (save states, spu2freeze.cpp not built): nothing to save.
s32 SPU2Savestate::FreezeIt(DataBlock&) { return -1; }
s32 SPU2Savestate::ThawIt(DataBlock&) { return -1; }
s32 SPU2Savestate::SizeIt() { return 0; }
