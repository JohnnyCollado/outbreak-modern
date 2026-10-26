// Link stubs for ob_pcsx2_sio2: what PCSX2's SIO2, Sio.cpp and memory-card files call outside themselves that the
// runner has no use for. The IOP-side globals (iopIntcIrq) are in adapter/sio2_side.cpp.
#include "Config.h"
#include "Counters.h"
#include "Host.h"
#include "SIO/Memcard/MemoryCardFile.h"
#include "SIO/Memcard/MemoryCardFolder.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadNotConnected.h"
#include "SIO/Sio0.h"
#include "StateWrapper.h"
#include "VMManager.h"

#include "common/Path.h"

// Pcsx2Config.cpp (not built: config_stubs.cpp owns EmuConfig): the card folder and Pcsx2Config.cpp:2105-2108.
std::string EmuFolders::MemoryCards;
std::string Pcsx2Config::FullpathToMcd(uint slot) const
{
	return Path::Combine(EmuFolders::MemoryCards, Mcd[slot].Filename);
}

// Counters.cpp: Sio.cpp's MemcardBusy stamps the frame of the last card write (save-state warning only).
uint g_FrameCount = 0;

// Pads reach the game through PS2Recomp's EE-side libpad2 HLE, not SIO2: on the SIO2 every pad port answers as
// PCSX2's "not connected" pad (Sio2::Pad sets the port's missing bit and replies 0xFF).
PadBase* Pad::GetPad(u8 port, u8 slot)
{
	static PadNotConnected none(0);
	return &none;
}

// Sio0.cpp (PS1 serial port) is not built: MemoryCardProtocol's PS1 card paths call only SetAcknowledge.
Sio0 g_Sio0;
Sio0::Sio0() = default;
Sio0::~Sio0() = default;
void Sio0::SetAcknowledge(bool) {}

// MemoryCardFile.cpp: Mcd swap writes and reapplies settings (FileMcd_Swap, never called here).
void VMManager::ApplySettings() {}
void Host::SetBaseStringSettingValue(const char*, const char*, const char*) {}
void Host::CommitBaseSettingChanges() {}

// Save states (StateWrapper.cpp, not built): Sio2/Multitap DoState are never called.
bool StateWrapper::DoMarker(const char*) { return false; }
void StateWrapper::Do(bool*) {}
void StateWrapper::DoBytes(void*, size_t) {}

// PCSX2 folder memory cards (MemoryCardFolder.cpp needs rapidyaml) are not built: ob_sio2_open sets every slot's
// type to File or Empty, so MemoryCardFile.cpp never routes a slot here.
FileAccessHelper::FileAccessHelper() = default;
FileAccessHelper::~FileAccessHelper() = default;
FolderMemoryCard::FolderMemoryCard() = default;
void FolderMemoryCard::Open(std::string, const Pcsx2Config::McdOptions&, const u32, const bool, std::string, bool) {}
void FolderMemoryCard::Close(bool) {}
bool FolderMemoryCard::IsFormatted() const { return false; }
FolderMemoryCardAggregator::FolderMemoryCardAggregator() = default;
void FolderMemoryCardAggregator::Open() {}
void FolderMemoryCardAggregator::Close() {}
void FolderMemoryCardAggregator::SetFiltering(const bool) {}
s32 FolderMemoryCardAggregator::IsPresent(uint) { return 0; }
void FolderMemoryCardAggregator::GetSizeInfo(uint, McdSizeInfo& outways) { outways = {}; }
bool FolderMemoryCardAggregator::IsPSX(uint) { return false; }
s32 FolderMemoryCardAggregator::Read(uint, u8*, u32, int) { return 0; }
s32 FolderMemoryCardAggregator::Save(uint, const u8*, u32, int) { return 0; }
s32 FolderMemoryCardAggregator::EraseBlock(uint, u32) { return 0; }
u64 FolderMemoryCardAggregator::GetCRC(uint) { return 0; }
void FolderMemoryCardAggregator::NextFrame(uint) {}
bool FolderMemoryCardAggregator::ReIndex(uint, const bool, const std::string&) { return false; }
