// hostprof: an external sampling profiler for the runner (or any process), outside the engine.
//
//   hostprof.exe (--pid N | --name ps2EntryRunner.exe) [--secs 30] [--hz 1000] [--out report.txt] [--min-cycles 50000]
//
// Sampling: every tick, for each thread of the target that used CPU since the previous tick (QueryThreadCycleTime
// delta >= --min-cycles; suspending a waiting thread itself costs it some cycles),
// SuspendThread, GetThreadContext (RIP, RSP), ResumeThread. Nothing is allocated, locked or printed between suspend
// and resume (docs/ops/gotchas.md): samples go to a buffer reserved before sampling starts. A thread whose cycle
// counter did not move is counted as idle without being suspended. The thread list is refreshed every 250 ms.
// After the window: symbolize with DbgHelp (the exe's PDB; an OB_PROFILE=1 build has debug info for the host
// libraries, public names only for the generated game units) and write the report: per thread CPU time
// (GetThreadTimes over the window), on-CPU samples, top functions (exclusive) and modules.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Sample {
    uint64_t rip;
    uint64_t rsp;
    uint32_t thread; // index into g_threads
};

struct ThreadRec {
    DWORD tid = 0;
    HANDLE h = nullptr;
    std::string name;
    ULONG64 lastCycles = 0;
    bool primed = false;     // lastCycles valid
    bool gone = false;       // exited during the window (handle kept for GetThreadTimes)
    uint64_t ticksSeen = 0;  // ticks while the thread existed
    uint64_t idleTicks = 0;  // cycle counter unchanged: not suspended
    uint64_t failTicks = 0;  // suspend / context failed
    FILETIME k0{}, u0{};     // times at first sight
};

std::vector<ThreadRec> g_threads;

uint64_t ft64(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

DWORD findPid(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_stricmp(narrow(pe.szExeFile).c_str(), name) == 0) { pid = pe.th32ProcessID; break; }
    }
    CloseHandle(snap);
    return pid;
}

// Adds threads of pid not yet known. Outside any suspension (allocates).
void refreshThreads(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        bool known = false;
        for (const ThreadRec& t : g_threads) if (t.tid == te.th32ThreadID && !t.gone) { known = true; break; }
        if (known) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        ThreadRec r;
        r.tid = te.th32ThreadID;
        r.h = h;
        PWSTR desc = nullptr;
        if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc) { r.name = narrow(desc); LocalFree(desc); }
        if (r.name.empty()) r.name = "(unnamed)";
        FILETIME c, e;
        GetThreadTimes(h, &c, &e, &r.k0, &r.u0);
        g_threads.push_back(std::move(r));
    }
    CloseHandle(snap);
}

struct Args {
    DWORD pid = 0;
    std::string name;
    double secs = 30;
    int hz = 1000;
    std::string out;
    uint64_t minCycles = 50000;
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto val = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "hostprof: %s needs a value\n", what); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (k == "--pid") { if (!(v = val("--pid"))) return false; a.pid = DWORD(std::strtoul(v, nullptr, 10)); }
        else if (k == "--name") { if (!(v = val("--name"))) return false; a.name = v; }
        else if (k == "--secs") { if (!(v = val("--secs"))) return false; a.secs = std::atof(v); }
        else if (k == "--hz") { if (!(v = val("--hz"))) return false; a.hz = std::atoi(v); }
        else if (k == "--out") { if (!(v = val("--out"))) return false; a.out = v; }
        else if (k == "--min-cycles") { if (!(v = val("--min-cycles"))) return false; a.minCycles = std::strtoull(v, nullptr, 10); }
        else { std::fprintf(stderr, "hostprof: unknown argument %s\n", argv[i]); return false; }
    }
    if ((a.pid == 0) == a.name.empty()) { std::fprintf(stderr, "hostprof: give exactly one of --pid N, --name EXE\n"); return false; }
    if (a.secs <= 0 || a.secs > 3600 || a.hz < 1 || a.hz > 10000) { std::fprintf(stderr, "hostprof: --secs 0..3600, --hz 1..10000\n"); return false; }
    return true;
}

// Symbolization -----------------------------------------------------------------------------------------------

struct FuncKey {
    std::string module;
    std::string name;
};

struct Symbolizer {
    HANDLE proc;
    DWORD64 exeBase = 0;
    std::string exeName;
    std::unordered_map<uint64_t, uint32_t> byRip;
    std::vector<FuncKey> funcs;
    std::map<std::pair<std::string, std::string>, uint32_t> ids;

    uint32_t intern(const std::string& m, const std::string& n) {
        auto it = ids.find({m, n});
        if (it != ids.end()) return it->second;
        uint32_t id = uint32_t(funcs.size());
        funcs.push_back({m, n});
        ids[{m, n}] = id;
        return id;
    }

    uint32_t resolve(uint64_t rip) {
        auto it = byRip.find(rip);
        if (it != byRip.end()) return it->second;
        uint32_t id = lookup(rip);
        byRip[rip] = id;
        return id;
    }

    uint32_t lookup(uint64_t rip) {
        IMAGEHLP_MODULE64 mi{};
        mi.SizeOfStruct = sizeof(mi);
        std::string mod = "?";
        DWORD64 base = SymGetModuleBase64(proc, rip);
        if (base && SymGetModuleInfo64(proc, base, &mi)) mod = mi.ModuleName;
        if (!base) return intern("(no module)", "(no module: JIT code, e.g. microVU cache)");
        alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 1024];
        SYMBOL_INFO* si = reinterpret_cast<SYMBOL_INFO*>(buf);
        std::memset(buf, 0, sizeof(buf));
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 1024;
        DWORD64 disp = 0;
        bool have = SymFromAddr(proc, rip, &disp, si) != FALSE;
        const bool inExe = base == exeBase;
        if (have && si->Size > 0 && disp < si->Size) return intern(mod, si->Name); // function with debug info
        if (inExe) {
            // No debug info: a generated game unit (or a host object built without /Z7). Accept the nearest public
            // name only when it starts the function that holds rip (the unwind table entry from .pdata).
            auto* rf = static_cast<PIMAGE_RUNTIME_FUNCTION_ENTRY>(SymFunctionTableAccess64(proc, rip));
            if (have && rf && base + rf->BeginAddress == si->Address) return intern(mod + " (generated)", si->Name);
            return intern(mod + " (generated)", "game code (generated)");
        }
        if (have && disp < 0x10000) return intern(mod, si->Name); // export-only system module: nearest export
        return intern(mod, "(unknown in " + mod + ")");
    }
};

double pct(uint64_t a, uint64_t b) { return b ? 100.0 * double(a) / double(b) : 0.0; }

} // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parseArgs(argc, argv, a)) {
        std::fprintf(stderr, "usage: hostprof (--pid N | --name EXE) [--secs 30] [--hz 1000] [--out FILE] [--min-cycles 50000]\n");
        return 2;
    }
    if (!a.name.empty()) {
        a.pid = findPid(a.name.c_str());
        if (!a.pid) { std::fprintf(stderr, "hostprof: no process named %s\n", a.name.c_str()); return 1; }
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, a.pid);
    if (!proc) { std::fprintf(stderr, "hostprof: OpenProcess(%lu) failed: %lu\n", a.pid, GetLastError()); return 1; }
    wchar_t exePathW[MAX_PATH * 2] = {};
    DWORD exeLen = DWORD(std::size(exePathW));
    QueryFullProcessImageNameW(proc, 0, exePathW, &exeLen);
    const std::string exePath = narrow(exePathW);

    refreshThreads(a.pid);
    if (g_threads.empty()) { std::fprintf(stderr, "hostprof: no threads opened in pid %lu\n", a.pid); return 1; }

    // Reserve the sample buffer up front: ticks * (threads now * 2 + 16) samples; sampling stops if it fills.
    const uint64_t ticks = uint64_t(a.secs * a.hz + 0.5);
    const size_t cap = size_t(ticks * (g_threads.size() * 2 + 16));
    Sample* samples = static_cast<Sample*>(VirtualAlloc(nullptr, cap * sizeof(Sample), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!samples) { std::fprintf(stderr, "hostprof: cannot reserve %zu samples\n", cap); return 1; }
    size_t n = 0;
    bool full = false;

    std::printf("hostprof: pid %lu (%s), %zu threads, %.0f s at %d Hz\n", a.pid, exePath.c_str(), g_threads.size(), a.secs, a.hz);
    std::fflush(stdout);

    timeBeginPeriod(1);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    LARGE_INTEGER qf, q0, qnow;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&q0);
    const double tickQ = double(qf.QuadPart) / a.hz;
    LONGLONG nextRefresh = q0.QuadPart + qf.QuadPart / 4;
    uint64_t tick = 0, overruns = 0;
    double busyQ = 0; // time spent inside the per-tick loop
    CONTEXT ctx;
    bool exited = false;
    const uint64_t minCycles = a.minCycles;

    // Ticks are at least one period apart: a late tick re-bases the schedule instead of bursting (a burst would
    // see near-zero cycle deltas and call busy threads idle).
    const LONGLONG endQ = q0.QuadPart + LONGLONG(a.secs * double(qf.QuadPart));
    double due = double(q0.QuadPart);
    for (; !full; ++tick) {
        QueryPerformanceCounter(&qnow);
        if (qnow.QuadPart >= endQ) break;
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        const size_t nt = g_threads.size();
        for (size_t i = 0; i < nt; ++i) {
            ThreadRec& t = g_threads[i];
            if (t.gone) continue;
            ++t.ticksSeen;
            ULONG64 cyc = 0;
            if (!QueryThreadCycleTime(t.h, &cyc)) { ++t.failTicks; continue; }
            // Suspending a waiting thread wakes it briefly in the kernel (the suspend APC), which moves its cycle
            // counter: a small delta is that, not work. Below --min-cycles since the last tick = idle.
            const bool ran = !t.primed || cyc - t.lastCycles >= minCycles;
            t.lastCycles = cyc;
            t.primed = true;
            if (!ran) { ++t.idleTicks; continue; }
            if (n >= cap) { full = true; break; }
            // ---- suspended: no allocation, no locks, no output ----
            if (SuspendThread(t.h) == DWORD(-1)) { ++t.failTicks; continue; }
            ctx.ContextFlags = CONTEXT_CONTROL;
            const BOOL got = GetThreadContext(t.h, &ctx); // also waits until the suspension took effect
            ResumeThread(t.h);
            // ---- resumed ----
            if (!got) { ++t.failTicks; continue; }
            samples[n++] = Sample{ctx.Rip, ctx.Rsp, uint32_t(i)};
        }
        QueryPerformanceCounter(&qnow);
        busyQ += double(qnow.QuadPart - t0.QuadPart);
        if (qnow.QuadPart >= nextRefresh) {
            if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) { exited = true; break; }
            for (ThreadRec& t : g_threads) {
                DWORD code = 0;
                if (!t.gone && GetExitCodeThread(t.h, &code) && code != STILL_ACTIVE) t.gone = true;
            }
            refreshThreads(a.pid);
            nextRefresh = qnow.QuadPart + qf.QuadPart / 4;
        }
        due += tickQ;
        QueryPerformanceCounter(&qnow);
        if (double(qnow.QuadPart) >= due) { ++overruns; due = double(qnow.QuadPart) + tickQ; }
        LARGE_INTEGER rel;
        rel.QuadPart = -LONGLONG((due - double(qnow.QuadPart)) * 1e7 / double(qf.QuadPart)); // 100 ns units
        if (rel.QuadPart == 0) continue;
        if (timer && SetWaitableTimer(timer, &rel, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
        else Sleep(1);
    }
    QueryPerformanceCounter(&qnow);
    const double wall = double(qnow.QuadPart - q0.QuadPart) / double(qf.QuadPart);
    timeEndPeriod(1);

    // Thread CPU time over the window.
    std::vector<double> cpuSec(g_threads.size(), 0.0);
    for (size_t i = 0; i < g_threads.size(); ++i) {
        FILETIME c, e, k, u;
        if (GetThreadTimes(g_threads[i].h, &c, &e, &k, &u))
            cpuSec[i] = double((ft64(k) - ft64(g_threads[i].k0)) + (ft64(u) - ft64(g_threads[i].u0))) / 1e7;
    }
    std::printf("hostprof: %zu samples in %.1f s (%llu ticks, %llu late, sampler %.1f us/tick)%s%s\n", n, wall,
                (unsigned long long)tick, (unsigned long long)overruns, tick ? busyQ / double(qf.QuadPart) / double(tick) * 1e6 : 0.0,
                full ? ", buffer full" : "", exited ? ", target exited" : "");
    std::fflush(stdout);
    if (exited) { std::fprintf(stderr, "hostprof: target exited during sampling; symbols unavailable\n"); return 1; }

    // Symbolize (target still running; DbgHelp reads its module list).
    std::string exeDir = exePath.substr(0, exePath.find_last_of("\\/"));
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    if (!SymInitialize(proc, exeDir.c_str(), TRUE)) { std::fprintf(stderr, "hostprof: SymInitialize failed: %lu\n", GetLastError()); return 1; }
    Symbolizer sym{proc};
    {
        // The exe's base: the module whose image file is exePath.
        IMAGEHLP_MODULE64 mi{};
        mi.SizeOfStruct = sizeof(mi);
        struct Ctx { const std::string* path; DWORD64 base; std::string name; } c{&exePath, 0, {}};
        SymEnumerateModules64(proc, [](PCSTR name, DWORD64 base, PVOID user) -> BOOL {
            auto* c = static_cast<Ctx*>(user);
            const std::string& p = *c->path;
            std::string file = p.substr(p.find_last_of("\\/") + 1);
            std::string stem = file.substr(0, file.find_last_of('.'));
            if (_stricmp(name, stem.c_str()) == 0) { c->base = base; c->name = name; return FALSE; }
            return TRUE;
        }, &c);
        sym.exeBase = c.base;
        sym.exeName = c.name;
        if (c.base && SymGetModuleInfo64(proc, c.base, &mi)) {
            static const char* types[] = {"none", "coff", "codeview", "pdb", "export", "deferred", "sym", "dia", "virtual"};
            SymLoadModuleEx(proc, nullptr, nullptr, nullptr, c.base, 0, nullptr, 0);
            SymGetModuleInfo64(proc, c.base, &mi);
            std::printf("hostprof: %s symbols: %s (%s)\n", mi.ModuleName, mi.SymType < 9 ? types[mi.SymType] : "?", mi.LoadedPdbName);
        }
    }

    const size_t T = g_threads.size();
    std::vector<uint32_t> fid(n);
    for (size_t i = 0; i < n; ++i) fid[i] = sym.resolve(samples[i].rip);

    // Report.
    FILE* out = a.out.empty() ? stdout : std::fopen(a.out.c_str(), "w");
    if (!out) { std::fprintf(stderr, "hostprof: cannot write %s\n", a.out.c_str()); out = stdout; }
    std::fprintf(out, "hostprof report: pid %lu %s\n", a.pid, exePath.c_str());
    std::fprintf(out, "window %.1f s, %d Hz, %llu ticks (%llu late), %zu on-CPU samples, sampler %.1f us/tick\n", wall, a.hz,
                 (unsigned long long)tick, (unsigned long long)overruns, n,
                 tick ? busyQ / double(qf.QuadPart) / double(tick) * 1e6 : 0.0);
    std::fprintf(out, "on-CPU sample = the thread's cycle counter moved by >= %llu since the previous tick; the thread was\n"
                      "then suspended and its RIP read. Exclusive (self) samples only. 'game code (generated)' = recompiled\n"
                      "game units.\n\n", (unsigned long long)a.minCycles);

    std::vector<uint64_t> perThread(T, 0);
    for (size_t i = 0; i < n; ++i) ++perThread[samples[i].thread];
    std::vector<size_t> order(T);
    for (size_t i = 0; i < T; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return cpuSec[x] > cpuSec[y]; });

    double cpuTotal = 0;
    for (double c : cpuSec) cpuTotal += c;
    std::fprintf(out, "threads (by CPU time over the window; total %.2f CPU-s = %.2f cores):\n", cpuTotal, wall > 0 ? cpuTotal / wall : 0.0);
    std::fprintf(out, "  %-6s %-28s %9s %7s %9s %7s %6s\n", "tid", "name", "cpu s", "cores", "on-CPU", "%ticks", "fail");
    for (size_t i : order) {
        const ThreadRec& t = g_threads[i];
        if (cpuSec[i] < 0.005 && perThread[i] == 0) continue;
        std::fprintf(out, "  %-6lu %-28s %9.2f %7.2f %9llu %6.1f%% %6llu%s\n", t.tid, t.name.substr(0, 28).c_str(), cpuSec[i],
                     wall > 0 ? cpuSec[i] / wall : 0.0, (unsigned long long)perThread[i], pct(perThread[i], t.ticksSeen),
                     (unsigned long long)t.failTicks, t.gone ? " (exited)" : "");
    }
    std::fprintf(out, "\n");

    for (size_t i : order) {
        if (perThread[i] < 50) continue;
        const ThreadRec& t = g_threads[i];
        std::unordered_map<uint32_t, uint64_t> fc;
        std::map<std::string, uint64_t> mc;
        for (size_t s = 0; s < n; ++s) {
            if (samples[s].thread != i) continue;
            ++fc[fid[s]];
            ++mc[sym.funcs[fid[s]].module];
        }
        std::vector<std::pair<uint32_t, uint64_t>> top(fc.begin(), fc.end());
        std::sort(top.begin(), top.end(), [](auto& x, auto& y) { return x.second > y.second; });
        std::vector<std::pair<std::string, uint64_t>> mods(mc.begin(), mc.end());
        std::sort(mods.begin(), mods.end(), [](auto& x, auto& y) { return x.second > y.second; });
        std::fprintf(out, "== thread %lu \"%s\": %llu on-CPU samples, %.2f CPU-s\n", t.tid, t.name.c_str(),
                     (unsigned long long)perThread[i], cpuSec[i]);
        std::fprintf(out, "   modules:");
        for (size_t m = 0; m < mods.size() && m < 8; ++m)
            std::fprintf(out, "%s %s %.1f%%", m ? "," : "", mods[m].first.c_str(), pct(mods[m].second, perThread[i]));
        std::fprintf(out, "\n");
        for (size_t f = 0; f < top.size() && f < 30; ++f) {
            const FuncKey& k = sym.funcs[top[f].first];
            std::fprintf(out, "   %5.1f%% %7llu  %-28s %s\n", pct(top[f].second, perThread[i]), (unsigned long long)top[f].second,
                         k.module.substr(0, 28).c_str(), k.name.c_str());
        }
        std::fprintf(out, "\n");
    }
    if (out != stdout) {
        std::fclose(out);
        std::printf("hostprof: report %s\n", a.out.c_str());
    }
    SymCleanup(proc);
    return 0;
}
