// perf_hint: asks Android for the CPU speed the game needs (Android 13+, the platform's performance hint API:
// APerformanceHint in libandroid.so). Without it the phone ran the game thread at about 2.4 GHz on its middle cores
// while that thread was the bottleneck (the developer's OnePlus 13, 2026-10-27, even in its "gamer pro mode"; reports/
// 2026-10-26-touch-controls.md struggle 11).
//
// One hint session holds the game thread (the EE: game code, VU1, IOP, SPU2) and the GS thread, with a target of one
// NTSC frame (16 683 333 ns). Each host frame the main thread reports the game thread's CPU time since the last one;
// when that nears or passes the target, the system raises clocks or moves the threads to faster cores. Nothing runs
// on the game thread but one registration. The functions are looked up at run time (the app supports API 29); older
// systems, or [ui] perf_hint = off, leave it off with one log line.
#include "android_menu.h"

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

namespace
{
	using GetManagerFn = void* (*)();
	using CreateSessionFn = void* (*)(void*, const int32_t*, size_t, int64_t);
	using ReportFn = int (*)(void*, int64_t);

	constexpr int64_t kFrameNs = 16683333; // NTSC 59.94 Hz, as the EE scheduler paces VBlank (PS2Recomp patch 0085)

	std::atomic<bool> s_gameKnown{false};
	pthread_t s_gamePthread{};
	pid_t s_gameTid = 0;

	bool s_tried = false;
	bool s_enabled = true;
	void* s_session = nullptr;
	ReportFn s_report = nullptr;
	clockid_t s_gameClock{};
	int64_t s_lastCpuNs = -1;

	int64_t Ns(const timespec& t) { return static_cast<int64_t>(t.tv_sec) * 1000000000ll + t.tv_nsec; }

	// A thread of this process by its name (/proc/self/task/<tid>/comm), 0 when none.
	pid_t TidNamed(const char* name)
	{
		DIR* d = opendir("/proc/self/task");
		if (!d)
			return 0;
		pid_t found = 0;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] < '0' || e->d_name[0] > '9')
				continue;
			const std::string path = std::string("/proc/self/task/") + e->d_name + "/comm";
			if (FILE* f = std::fopen(path.c_str(), "r"))
			{
				char comm[32] = {};
				if (std::fgets(comm, sizeof(comm), f))
				{
					comm[std::strcspn(comm, "\n")] = 0;
					if (std::strcmp(comm, name) == 0)
						found = static_cast<pid_t>(std::atoi(e->d_name));
				}
				std::fclose(f);
			}
			if (found)
				break;
		}
		closedir(d);
		return found;
	}

	void Start()
	{
		s_tried = true;
		if (!s_enabled)
		{
			std::fprintf(stderr, "[perf] hint off ([ui] perf_hint = off)\n");
			return;
		}
		void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
		if (!lib)
			lib = dlopen("libandroid.so", RTLD_NOW);
		const auto getManager = lib ? reinterpret_cast<GetManagerFn>(dlsym(lib, "APerformanceHint_getManager")) : nullptr;
		const auto create = lib ? reinterpret_cast<CreateSessionFn>(dlsym(lib, "APerformanceHint_createSession")) : nullptr;
		s_report = lib ? reinterpret_cast<ReportFn>(dlsym(lib, "APerformanceHint_reportActualWorkDuration")) : nullptr;
		void* manager = getManager ? getManager() : nullptr;
		if (!manager || !create || !s_report)
		{
			std::fprintf(stderr, "[perf] hint unavailable (needs Android 13 and the platform's support)\n");
			return;
		}
		std::vector<int32_t> tids{static_cast<int32_t>(s_gameTid)};
		const pid_t gs = TidNamed("GSThread");
		if (gs)
			tids.push_back(static_cast<int32_t>(gs));
		if (pthread_getcpuclockid(s_gamePthread, &s_gameClock) != 0)
		{
			std::fprintf(stderr, "[perf] hint off: no CPU clock for the game thread\n");
			return;
		}
		s_session = create(manager, tids.data(), tids.size(), kFrameNs);
		std::fprintf(stderr, "[perf] hint %s: game thread %d%s, target %lld ns\n", s_session ? "on" : "refused by the system",
			static_cast<int>(s_gameTid), gs ? (", GS thread " + std::to_string(gs)).c_str() : "", static_cast<long long>(kFrameNs));
		std::fflush(stderr);
	}
} // namespace

namespace obui::android
{
	void perfHintGameThread()
	{
		if (s_gameKnown.load(std::memory_order_acquire))
			return;
		s_gamePthread = pthread_self();
		s_gameTid = gettid();
		s_gameKnown.store(true, std::memory_order_release);
	}

	void perfHintEnable(bool on) { s_enabled = on; }

	void perfHintFrame()
	{
		if (!s_gameKnown.load(std::memory_order_acquire))
			return; // the game has not read its pad yet
		if (!s_tried)
			Start();
		if (!s_session)
			return;
		timespec t{};
		if (clock_gettime(s_gameClock, &t) != 0)
			return;
		const int64_t cpu = Ns(t);
		if (s_lastCpuNs >= 0 && cpu > s_lastCpuNs)
			s_report(s_session, cpu - s_lastCpuNs);
		s_lastCpuNs = cpu;
	}
} // namespace obui::android
