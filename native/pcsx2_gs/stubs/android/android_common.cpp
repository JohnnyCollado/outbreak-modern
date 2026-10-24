// Android stand-ins for PCSX2's common/Linux/LnxMisc.cpp (not built: it needs X11 and D-Bus) and the POSIX shared
// memory LnxHostSys.cpp uses (ob_android_compat.h). The functions without a desktop dependency are PCSX2 v2.9.134's
// own bodies (LnxMisc.cpp:33-97, 369-380); the screensaver, mouse and sound ones have no Android meaning here and
// report "not done".
#include "ob_android_compat.h"

#include "common/HostSys.h"
#include "common/Pcsx2Types.h"
#include "common/Threading.h"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/system_properties.h>
#include <unistd.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

// memfd_create is a libc call only from API 30; the syscall exists on every arm64 kernel Android 10 runs on.
extern "C" int shm_open(const char* name, int, mode_t)
{
	return static_cast<int>(syscall(__NR_memfd_create, name ? name : "pcsx2", MFD_CLOEXEC));
}

extern "C" int shm_unlink(const char*)
{
	return 0; // a memfd has no name to remove
}

u64 GetPhysicalMemory()
{
	u64 pages = 0;
#ifdef _SC_PHYS_PAGES
	pages = sysconf(_SC_PHYS_PAGES);
#endif
	return pages * getpagesize();
}

u64 GetAvailablePhysicalMemory()
{
	struct sysinfo info = {};
	if (sysinfo(&info) != 0)
		return 0;
	return (static_cast<u64>(info.freeram) + static_cast<u64>(info.bufferram)) * static_cast<u64>(info.mem_unit);
}

u64 GetTickFrequency()
{
	return 1000000000; // nanoseconds
}

u64 GetCPUTicks()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (static_cast<u64>(ts.tv_sec) * 1000000000ULL) + ts.tv_nsec;
}

std::string GetOSVersionString()
{
	char release[PROP_VALUE_MAX] = {};
	__system_property_get("ro.build.version.release", release);
	return std::string("Android ") + release;
}

bool Common::InhibitScreensaver(bool)
{
	return false;
}

void Common::SetMousePosition(int, int)
{
}

bool Common::AttachMousePositionCb(std::function<void(int, int)>)
{
	return false;
}

void Common::DetachMousePositionCb()
{
}

bool Common::PlaySoundAsync(const char*)
{
	return false;
}

void Threading::Sleep(int ms)
{
	usleep(1000 * ms);
}

void Threading::SleepUntil(u64 ticks)
{
	struct timespec ts;
	ts.tv_sec = static_cast<time_t>(ticks / 1000000000ULL);
	ts.tv_nsec = static_cast<long>(ticks % 1000000000ULL);
	clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}
