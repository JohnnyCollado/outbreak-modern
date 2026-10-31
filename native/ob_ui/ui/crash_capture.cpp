// crash_capture: a release's log file and crash dump on Windows (report entry 71). The .exe has no console, so stdout
// and stderr go through a pipe to logs/outbreak-<time>.log; an unhandled exception or abort() adds a small dump (threads
// and stacks only). It only moves lines the game already prints: no per-frame work. Android: the app reads its logcat
// instead (CrashReport.kt).
#include "ob_ui.h"

#include "crash_log.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <fcntl.h>
#include <io.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	constexpr size_t kKeepLogs = 5;
	constexpr uint64_t kMaxLogBytes = 64ull * 1024ull * 1024ull;

	using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
		PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

	fs::path s_logDir;
	wchar_t s_dumpPath[MAX_PATH * 2] = {};
	MiniDumpWriteDumpFn s_writeDump = nullptr;
	HANDLE s_logFile = INVALID_HANDLE_VALUE;
	volatile LONG s_crashed = 0;
	LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;

	fs::path ExeDir()
	{
		std::wstring path(32768, L'\0');
		const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		path.resize(n);
		return fs::path(path).parent_path();
	}

	bool Writable(const fs::path& dir)
	{
		std::error_code ec;
		fs::create_directories(dir, ec);
		const fs::path probe = dir / "outbreak-probe.tmp";
		FILE* f = _wfopen(probe.c_str(), L"wb");
		if (!f)
			return false;
		std::fclose(f);
		fs::remove(probe, ec);
		return true;
	}

	// Beside the .exe, or the user's local app data when that folder is read-only (Program Files).
	fs::path ChooseLogDir()
	{
		const fs::path beside = ExeDir() / "logs";
		if (Writable(beside))
			return beside;
		if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"))
		{
			const fs::path there = fs::path(local) / "OutbreakModern" / "logs";
			if (Writable(there))
				return there;
		}
		return {};
	}

	// The exe's link stamp: dist/symbols/<this>/ holds the map that reads its dumps (tools/dist.sh).
	unsigned long BuildId()
	{
		const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		return reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
	}

	// Called while crashing: no locks, no allocation.
	void WriteToLog(const char* text)
	{
		if (s_logFile == INVALID_HANDLE_VALUE)
			return;
		DWORD written = 0;
		WriteFile(s_logFile, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
	}

	void WriteDump(EXCEPTION_POINTERS* info)
	{
		if (!s_writeDump || !s_dumpPath[0])
			return;
		const HANDLE file = CreateFileW(s_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return;
		MINIDUMP_EXCEPTION_INFORMATION ex{};
		ex.ThreadId = GetCurrentThreadId();
		ex.ExceptionPointers = info;
		ex.ClientPointers = FALSE;
		s_writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, info ? &ex : nullptr, nullptr, nullptr);
		CloseHandle(file);
	}

	LONG WINAPI OnUnhandled(EXCEPTION_POINTERS* info)
	{
		// An uncaught C++ exception: the C runtime's filter calls terminate, whose logger prints the message, then OnAbort.
		// Stopping it here would lose that message, the thing most worth having.
		constexpr DWORD kCppException = 0xE06D7363u;
		if (info->ExceptionRecord->ExceptionCode == kCppException && s_previousFilter)
			return s_previousFilter(info);
		if (InterlockedExchange(&s_crashed, 1) == 0)
		{
			char line[160];
			HMODULE module = nullptr;
			const void* at = info->ExceptionRecord->ExceptionAddress;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(at), &module);
			std::snprintf(line, sizeof(line), "\n[crash] exception 0x%08lx at module 0x%p + 0x%llx (exe 0x%p)\n",
				info->ExceptionRecord->ExceptionCode, static_cast<void*>(module),
				static_cast<unsigned long long>(static_cast<const char*>(at) - reinterpret_cast<const char*>(module)),
				static_cast<void*>(GetModuleHandleW(nullptr)));
			Sleep(150); // what the game printed last is still in the pipe
			WriteToLog(line);
			WriteDump(info);
		}
		return EXCEPTION_CONTINUE_SEARCH; // Windows ends the process as usual
	}

	// abort(): an uncaught C++ exception ends here after main.cpp's terminate logger printed its message.
	void OnAbort(int)
	{
		if (InterlockedExchange(&s_crashed, 1) == 0)
		{
			Sleep(150);
			WriteToLog("\n[crash] abort\n");
			WriteDump(nullptr);
		}
	}

	void Pump(int readFd)
	{
		obui::LogCap cap(kMaxLogBytes);
		bool told = false;
		char buffer[4096];
		for (;;)
		{
			const int n = _read(readFd, buffer, sizeof(buffer));
			if (n <= 0)
				return;
			const size_t take = cap.accept(static_cast<size_t>(n));
			DWORD written = 0;
			if (take)
				WriteFile(s_logFile, buffer, static_cast<DWORD>(take), &written, nullptr);
			else if (!told)
			{
				told = true;
				WriteToLog("\n[log] 64 MiB reached: later lines are not kept\n");
			}
		}
	}
} // namespace

void obuiStartCrashCapture()
{
	s_logDir = ChooseLogDir();
	if (s_logDir.empty())
		return;
	obui::pruneLogs(s_logDir, kKeepLogs - 1);
	std::time_t now = std::time(nullptr);
	std::tm when{};
	localtime_s(&when, &now);
	const fs::path log = s_logDir / fs::u8path(obui::logFileName(when));
	s_logFile = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (s_logFile == INVALID_HANDLE_VALUE)
		return;
	// PCSX2's own log, per run; the GS opens after this and reads it (pcsx2_side.cpp).
	const std::string gsLog = (s_logDir / fs::u8path(obui::gsLogName(obui::logFileName(when)))).string();
	_putenv_s("PS2X_GS_LOG", gsLog.c_str());
	fs::path dump = log;
	dump.replace_extension(".dmp");
	wcsncpy_s(s_dumpPath, dump.c_str(), _TRUNCATE);

	// Loaded now: loading a library in the middle of a crash can hang.
	if (const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"))
		s_writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));

	int fds[2];
	if (_pipe(fds, 64 * 1024, _O_BINARY) == 0)
	{
		// A /SUBSYSTEM:WINDOWS program starts with no stdout or stderr: open them, then point them at the pipe.
		FILE* unused = nullptr;
		freopen_s(&unused, "NUL", "w", stdout);
		freopen_s(&unused, "NUL", "w", stderr);
		_dup2(fds[1], _fileno(stdout));
		_dup2(fds[1], _fileno(stderr));
		std::setvbuf(stdout, nullptr, _IONBF, 0);
		std::setvbuf(stderr, nullptr, _IONBF, 0);
		std::thread([readFd = fds[0]] { Pump(readFd); }).detach();
	}
	std::fprintf(stderr, "[build] %08lx\n", BuildId());
	s_previousFilter = SetUnhandledExceptionFilter(OnUnhandled);
	std::signal(SIGABRT, OnAbort);
	std::atexit([] { Sleep(50); }); // the last lines reach the file
}

bool obuiSaveCrashReport(std::string& savedTo, std::string& error)
{
	if (s_logDir.empty())
	{
		error = "No log folder could be created";
		return false;
	}
	std::time_t now = std::time(nullptr);
	std::tm when{};
	localtime_s(&when, &now);
	std::string name = obui::logFileName(when);
	name.replace(0, 8, "crash-report");
	name.replace(name.size() - 4, 4, ".zip");
	const fs::path out = s_logDir / fs::u8path(name);
	std::string home;
	if (const char* profile = std::getenv("USERPROFILE"))
		home = profile;
	if (!obui::writeCrashReport(s_logDir, {}, home, out, error))
		return false;
	const auto u8 = out.u8string();
	savedTo.assign(u8.begin(), u8.end());
	return true;
}
#else
void obuiStartCrashCapture() {}

bool obuiSaveCrashReport(std::string&, std::string& error)
{
	error = "Not on this system";
	return false;
}
#endif
