// The few PCSX2 `common` functions microVU-arm64, AsmHelpers and vuaot call (study
// research/2026-10-24-armsx2-microvu-feasibility.md 1.6), for libob_a64vu.so, which links no `common` library
// (ARMSX2's common differs from our PCSX2 pin; mixing them in one .so would break the one-definition rule).
//   - the on-disk program cache and vu-aot are off (config_defaults_a64.cpp): FileSystem/Path report failure;
//   - Perf (jitdump) is a no-op; Log goes to stderr (logcat in the runner, the terminal in vucompare);
//   - HostSys::FlushInstructionCache = __builtin___clear_cache, as PCSX2's common/Linux/LnxHostSys.cpp:352-359;
//   - Threading (vu-aot's background worker, never started): no-ops.
// Built with ARMSX2 headers.
#include "common/AlignedMalloc.h"
#include "common/Assertions.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "common/HostSys.h"
#include "common/Path.h"
#include "common/Perf.h"
#include "common/Threading.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

// ---- assertions / log ----
void pxOnAssertFail(const char* file, int line, const char* func, const char* msg)
{
	std::fprintf(stderr, "ob_a64vu: assertion failed at %s:%d (%s): %s\n", file, line, func ? func : "?", msg ? msg : "");
	std::fflush(stderr);
	std::abort();
}

void Log::Writev(LOGLEVEL /*level*/, ConsoleColors /*color*/, const char* format, va_list ap)
{
	std::fputs("[a64vu] ", stderr);
	std::vfprintf(stderr, format, ap);
	std::fputc('\n', stderr);
}

// ---- memory ----
void* _aligned_malloc(size_t size, size_t align)
{
	void* p = nullptr;
	if (align < sizeof(void*))
		align = sizeof(void*);
	return posix_memalign(&p, align, size) == 0 ? p : nullptr;
}

void _aligned_free(void* pmem) { std::free(pmem); }

// ---- HostSys ----
void HostSys::FlushInstructionCache(void* address, u32 size)
{
	__builtin___clear_cache(static_cast<char*>(address), static_cast<char*>(address) + size);
}

size_t HostSys::GetRuntimePageSize() { return static_cast<size_t>(sysconf(_SC_PAGESIZE)); }

// ---- Perf (jitdump off) ----
namespace Perf
{
	Group any("");
	Group vu0("");
	Group vu1("");
	void Group::Register(const void*, size_t, const char*) {}
	void Group::RegisterPC(const void*, size_t, u32) {}
} // namespace Perf

// ---- Error (only constructed / destroyed by the off cache paths) ----
Error::Error() : m_type(Type::None) {}
Error::~Error() = default;

// ---- FileSystem / Path: the on-disk caches are off, so every query fails ----
namespace FileSystem
{
	s64 GetPathFileSize(const char*) { return -1; }
	bool FileExists(const char*) { return false; }
	bool DirectoryExists(const char*) { return false; }
	bool DeleteFilePath(const char*, Error*) { return false; }
	std::FILE* OpenCFile(const char*, const char*, Error*) { return nullptr; }
	std::optional<std::vector<u8>> ReadBinaryFile(const char*) { return std::nullopt; }
	std::optional<std::string> ReadFileToString(const char*) { return std::nullopt; }
	bool WriteBinaryFile(const char*, const void*, size_t) { return false; }
	bool EnsureDirectoryExists(const char*, bool, Error*) { return false; }
} // namespace FileSystem

namespace Path
{
	std::string Combine(const std::string_view base, const std::string_view next)
	{
		std::string r(base);
		if (!r.empty() && r.back() != '/')
			r.push_back('/');
		r.append(next);
		return r;
	}

	std::string_view GetDirectory(const std::string_view path)
	{
		const size_t pos = path.rfind('/');
		return pos == std::string_view::npos ? std::string_view() : path.substr(0, pos);
	}
} // namespace Path

// ---- Threading (vu-aot's background worker; never started with vu-aot off) ----
namespace Threading
{
	ThreadHandle::ThreadHandle() = default;
	ThreadHandle::~ThreadHandle() = default;
	ThreadHandle ThreadHandle::GetForCallingThread() { return ThreadHandle(); }
	bool ThreadHandle::SetNicePriority(int) const { return false; }
	void SetNameOfCurrentThread(const char*) {}
} // namespace Threading
