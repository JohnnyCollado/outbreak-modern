// Host-side stubs for ob_pcsx2_gs: the subset of PCSX2's host / core interface that GS + common call.
// Everything here replaces a PCSX2 core file we do not build (Host, VMManager, MTGS, Counters, Memory,
// PerformanceMetrics, BuildVersion, EE globals). GS runs on the caller's thread: there is no MTGS thread.
#include "ob_pcsx2_gs.h"

#include "common/RedtapeWindows.h"

#include "BuildVersion.h"
#include "Config.h"
#include "Counters.h"
#include "GS.h"
#include "GS/GS.h"
#include "Host.h"
#include "Memory.h"
#include "MemoryTypes.h"
#include "MTGS.h"
#include "PerformanceMetrics.h"
#include "R5900.h"
#include "VMManager.h"

#include <cstdarg>
#include <cstdio>

#if !defined(_WIN32)
#include <sys/mman.h>
#endif

// ---- window (ob API) ----
static WindowInfo s_render_window;

void ObPcsx2GS::SetRenderWindow(const WindowInfo& wi) { s_render_window = wi; }

// ---- Host:: (pcsx2/Host.h, pcsx2/GS/GS.h) ----
std::optional<WindowInfo> Host::AcquireRenderWindow(bool) { return s_render_window; }
void Host::ReleaseRenderWindow() {}
void Host::BeginPresentFrame() {}
bool Host::IsFullscreen() { return false; }
void Host::SetFullscreen(bool) {}

std::string_view Host::TranslateToStringView(const std::string_view, const std::string_view msg) { return msg; }
std::string Host::TranslateToString(const std::string_view, const std::string_view msg) { return std::string(msg); }
void Host::AddKeyedOSDMessage(std::string, std::string, float) {}
void Host::AddIconOSDMessage(std::string, const char*, const std::string_view, float) {}
void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	std::fprintf(stderr, "%.*s: %.*s\n", (int)title.size(), title.data(), (int)message.size(), message.data());
}
void Host::ReportFormattedErrorAsync(const std::string_view title, const char* format, ...)
{
	std::va_list ap;
	va_start(ap, format);
	std::fprintf(stderr, "%.*s: ", (int)title.size(), title.data());
	std::vfprintf(stderr, format, ap);
	std::fprintf(stderr, "\n");
	va_end(ap);
}
// No settings store: every lookup gets its default.
bool Host::GetBaseBoolSettingValue(const char*, const char*, bool default_value) { return default_value; }
std::string Host::GetStringSettingValue(const char*, const char*, const char* default_value) { return default_value; }

// ---- VMManager:: (pcsx2/VMManager.h) ----
bool VMManager::HasValidVM() { return false; }
std::string VMManager::GetDiscSerial() { return {}; }
std::string VMManager::GetTitle(bool) { return {}; }
u32 VMManager::GetDiscCRC() { return 0; }
const std::vector<u32>& VMManager::Internal::GetSoftwareRendererProcessorList()
{
	static const std::vector<u32> none; // no thread pinning
	return none;
}

// ---- MTGS:: (pcsx2/MTGS.h): GS calls are synchronous on the caller's thread ----
void MTGS::WaitGS(bool, bool, bool) {}
void MTGS::RunOnGSThread(AsyncCallType func) { func(); }
void MTGS::ApplySettings() {}
void MTGS::ToggleSoftwareRendering() {}

// ---- PerformanceMetrics:: (pcsx2/PerformanceMetrics.h) ----
void PerformanceMetrics::Update(bool, bool, bool) {}
void PerformanceMetrics::OnGPUPresent(float, u64, u64) {}
void PerformanceMetrics::SetGSSWThreadCount(u32) {}
void PerformanceMetrics::SetGSSWThread(u32, Threading::ThreadHandle) {}
PerformanceMetrics::InternalFPSMethod PerformanceMetrics::GetInternalFPSMethod() { return InternalFPSMethod::None; }
void PerformanceMetrics::StartSavingMetrics(u32) {}
bool PerformanceMetrics::IsSavingMetrics() { return false; }
void PerformanceMetrics::DumpSavedMetrics() {}

// ---- Counters (pcsx2/Counters.h, pcsx2/GS.h): stats strings only ----
double GetVerticalFrequency() { return 59.94; }
const char* ReportVideoMode() { return "NTSC"; }
const char* ReportInterlaceMode() { return "Interlaced"; }

// ---- BuildVersion (pcsx2/BuildVersion.h): Vulkan application version ----
int BuildVersion::GitTagHi = 2;
int BuildVersion::GitTagMid = 9;
int BuildVersion::GitTagLo = 134;

// ---- EE globals read by GS.cpp stats / IPU FMV flag (zeroed; no EE here) ----
alignas(__pagealignsize) u8 eeHw[Ps2MemSize::Hardware];
alignas(16) cpuRegistersPack _cpuRegistersPack;
bool FMVstarted = false;

// ---- SysMemory (pcsx2/Memory.h): only the SW-renderer JIT range is ever requested by GS ----
u8* SysMemory::GetCodePtr(size_t offset)
{
#if defined(_WIN32)
	static u8* s_swrec = static_cast<u8*>(
		VirtualAlloc(nullptr, HostMemoryMap::SWrecSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
	// Android/POSIX: one RWX anonymous mapping, as the Win32 branch (the arm64 JIT flushes the icache itself).
	static u8* s_swrec = []() -> u8* {
		void* p = mmap(nullptr, HostMemoryMap::SWrecSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return p == MAP_FAILED ? nullptr : static_cast<u8*>(p);
	}();
#endif
	pxAssertRel(s_swrec, "SW JIT code allocation failed");
	pxAssertRel(offset >= HostMemoryMap::SWrecOffset && offset <= HostMemoryMap::SWrecOffset + HostMemoryMap::SWrecSize,
		"ob_pcsx2_gs: only the SWrec code range exists");
	return s_swrec + (offset - HostMemoryMap::SWrecOffset);
}
