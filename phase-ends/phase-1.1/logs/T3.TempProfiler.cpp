// TEMP PROFILER (not for commit): PS2X_PROFILE=<seconds> samples every thread of this process
// about 1000 times a second and prints, every <seconds>, each thread's busiest functions
// (resolved through the PDB) to stderr. Windows only.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
    std::string threadName(HANDLE thread)
    {
        PWSTR wide = nullptr;
        std::string name;
        if (SUCCEEDED(GetThreadDescription(thread, &wide)) && wide)
        {
            char buffer[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr);
            name = buffer;
            LocalFree(wide);
        }
        return name;
    }

    std::string symbolFor(HANDLE process, DWORD64 address)
    {
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol))
        {
            return symbol->Name;
        }
        char fallback[64];
        std::snprintf(fallback, sizeof(fallback), "?%llx", static_cast<unsigned long long>(address & ~0xFFFull));
        return fallback;
    }

    void profilerLoop(int windowSeconds)
    {
        const DWORD self = GetCurrentThreadId();
        const DWORD pid = GetCurrentProcessId();
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);

        std::unordered_map<DWORD, std::unordered_map<DWORD64, uint32_t>> samples;
        std::unordered_map<DWORD, std::string> names;
        std::vector<DWORD> threads;
        auto lastList = std::chrono::steady_clock::time_point{};
        auto windowStart = std::chrono::steady_clock::now();
        uint32_t window = 0;

        for (;;)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastList > std::chrono::milliseconds(500))
            {
                threads.clear();
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                THREADENTRY32 entry{};
                entry.dwSize = sizeof(entry);
                for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry))
                {
                    if (entry.th32OwnerProcessID == pid && entry.th32ThreadID != self)
                        threads.push_back(entry.th32ThreadID);
                }
                CloseHandle(snap);
                lastList = now;
            }

            for (DWORD tid : threads)
            {
                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
                if (!thread)
                    continue;
                if (names.find(tid) == names.end())
                    names[tid] = threadName(thread);
                if (SuspendThread(thread) != static_cast<DWORD>(-1))
                {
                    // No allocation while the target is suspended: it may hold the heap lock.
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL;
                    const bool sampled = GetThreadContext(thread, &context) != 0;
                    ResumeThread(thread);
                    if (sampled)
                        ++samples[tid][context.Rip];
                }
                CloseHandle(thread);
            }

            if (now - windowStart >= std::chrono::seconds(windowSeconds))
            {
                std::fprintf(stderr, "[prof] ===== window %u (%d s) =====\n", window++, windowSeconds);
                for (auto &[tid, perAddress] : samples)
                {
                    std::map<std::string, uint32_t> perFunction;
                    uint32_t total = 0;
                    for (auto &[address, count] : perAddress)
                    {
                        perFunction[symbolFor(process, address)] += count;
                        total += count;
                    }
                    std::vector<std::pair<uint32_t, std::string>> sorted;
                    for (auto &[name, count] : perFunction)
                        sorted.emplace_back(count, name);
                    std::sort(sorted.rbegin(), sorted.rend());
                    std::fprintf(stderr, "[prof] thread %lu '%s' samples=%u\n", tid, names[tid].c_str(), total);
                    for (size_t i = 0; i < sorted.size() && i < 12; ++i)
                        std::fprintf(stderr, "[prof]   %5.1f%% %s\n", 100.0 * sorted[i].first / total, sorted[i].second.c_str());
                }
                std::fflush(stderr);
                samples.clear();
                windowStart = now;
            }
            Sleep(1);
        }
    }

}

void ps2TempProfilerStart()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    if (const char *value = std::getenv("PS2X_PROFILE"))
    {
        const int seconds = (std::max)(1, std::atoi(value)); // parenthesised: windows.h max macro
        std::thread(profilerLoop, seconds).detach();
    }
}
#else
void ps2TempProfilerStart() {}
#endif
