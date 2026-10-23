// ob_gs_ring_tests: the GS thread's command ring (adapter/gs_ring.h) on its own, with a recording handler instead of
// PCSX2. Checks: records execute in post order with their bytes intact across wraps and the by-pointer path; a
// synchronous call (the readback) runs only after every earlier record; the queue-ahead limit holds the producer to
// 2 queued vsyncs while still letting it run ahead; stop() executes what is queued. Output as ps2x_tests
// ("[Passed]"/"[Failed]" lines, "Passed: N", "Total Tests: N") for ob.sh test.
#include "gs_ring.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace
{
	constexpr uint32_t kData = obgs::Ring::kFirstUserCmd, kRead = kData + 1, kVsyncCmd = kData + 2;

	int g_total = 0, g_passed = 0;
	void Check(const char* name, bool ok, const char* detail = "")
	{
		g_total++;
		if (ok)
			g_passed++;
		std::printf("[%s] %s %s\n", ok ? "Passed" : "Failed", name, detail);
	}

	// Busy wait: sleep_for rounds up to the 15.6 ms Windows timer tick.
	void SpinFor(std::chrono::microseconds us)
	{
		const auto end = std::chrono::steady_clock::now() + us;
		while (std::chrono::steady_clock::now() < end)
			OB_GS_RING_PAUSE();
	}

	uint32_t Fill(uint32_t seq, uint32_t i) { return seq * 2654435761u ^ (i * 40503u); }

	// ---- ordering + payload integrity ----
	struct OrderCtx
	{
		uint64_t next = 0, bad_seq = 0, bad_bytes = 0, executed = 0;
		std::thread::id thread;
		bool other_thread = true;
	};
	void OrderHandler(void* c, const obgs::RecordHeader& h, const uint8_t* p)
	{
		OrderCtx& x = *static_cast<OrderCtx*>(c);
		if (std::this_thread::get_id() == x.thread)
			x.other_thread = false;
		if (h.a != x.next)
			x.bad_seq++;
		x.next = h.a + 1;
		for (uint32_t i = 0; i + 4 <= h.bytes; i += 4)
		{
			uint32_t v;
			std::memcpy(&v, p + i, 4);
			if (v != Fill(static_cast<uint32_t>(h.a), i))
			{
				x.bad_bytes++;
				break;
			}
		}
		x.executed++;
	}

	void TestOrdering()
	{
		OrderCtx ctx;
		ctx.thread = std::this_thread::get_id();
		obgs::Ring ring;
		const size_t cap = 64 * 1024; // small: thousands of wraps; > 16 KiB payloads go by pointer
		ring.start(&OrderHandler, nullptr, &ctx, cap, -1);
		std::vector<uint8_t> buf(40 * 1024);
		uint32_t rng = 12345;
		const uint64_t n = 20000;
		for (uint64_t s = 0; s < n; s++)
		{
			rng = rng * 1103515245u + 12345u;
			uint32_t bytes = (rng >> 8) % 3000;
			if (s % 97 == 0)
				bytes = 20 * 1024 + (rng % 15000); // by pointer (> cap/4)
			for (uint32_t i = 0; i + 4 <= bytes; i += 4)
			{
				const uint32_t v = Fill(static_cast<uint32_t>(s), i);
				std::memcpy(buf.data() + i, &v, 4);
			}
			ring.post(kData, 0, 0, s, 0, buf.data(), bytes);
			if (s % 97 == 0)
				std::memset(buf.data(), 0xEE, bytes); // a by-pointer post must have finished with the bytes already
		}
		ring.drain();
		char d[160];
		std::snprintf(d, sizeof(d), "(%llu executed, %llu out of order, %llu corrupt, %llu by pointer, %llu ring-full stalls)",
			(unsigned long long)ctx.executed, (unsigned long long)ctx.bad_seq, (unsigned long long)ctx.bad_bytes,
			(unsigned long long)ring.stats().indirect, (unsigned long long)ring.stats().fullStalls);
		Check("ordering: 20000 records in post order, bytes intact across wraps", ctx.executed == n && ctx.bad_seq == 0 && ctx.bad_bytes == 0, d);
		Check("ordering: big payloads went by pointer", ring.stats().indirect == (n + 96) / 97, "");
		Check("ordering: the handler runs on the ring's thread, not the producer's", ctx.other_thread, "");
		ring.stop();
	}

	// ---- synchronous readback drains first ----
	struct SyncCtx
	{
		uint64_t draws_done = 0;
	};
	void SyncHandler(void* c, const obgs::RecordHeader& h, const uint8_t*)
	{
		SyncCtx& x = *static_cast<SyncCtx*>(c);
		if (h.cmd == kData)
		{
			SpinFor(std::chrono::microseconds(2000)); // a slow GS: the ring is still full when the read is posted
			x.draws_done++;
		}
		else if (h.cmd == kRead)
		{
			// The readback reports how many draws it saw executed before it, into the caller's buffer.
			*reinterpret_cast<uint64_t*>(h.a) = x.draws_done;
		}
	}

	void TestSyncReadback()
	{
		SyncCtx ctx;
		obgs::Ring ring;
		ring.start(&SyncHandler, nullptr, &ctx, 1 << 20, -1);
		const uint64_t draws = 25;
		uint64_t seen = ~0ull;
		const uint8_t pkt[64] = {};
		for (int round = 0; round < 3; round++)
		{
			for (uint64_t i = 0; i < draws; i++)
				ring.post(kData, 0, 0, 0, 0, pkt, sizeof(pkt));
			seen = ~0ull;
			ring.call(kRead, 0, reinterpret_cast<uint64_t>(&seen), 0);
			char d[96];
			std::snprintf(d, sizeof(d), "(round %d: read saw %llu of %llu draws)", round, (unsigned long long)seen,
				(unsigned long long)(draws * (round + 1)));
			Check("readback: runs after every earlier draw, result back before call() returns", seen == draws * (round + 1), d);
		}
		ring.stop();
	}

	// ---- queue-ahead limit ----
	struct VsyncCtx
	{
		std::atomic<int> executed{0};
	};
	void VsyncHandler(void* c, const obgs::RecordHeader&, const uint8_t*)
	{
		SpinFor(std::chrono::microseconds(8000)); // present slower than the producer
		static_cast<VsyncCtx*>(c)->executed++;
	}

	void TestQueueAhead(int limit, int& max_ahead, uint64_t& stalls)
	{
		VsyncCtx ctx;
		obgs::Ring ring;
		ring.start(&VsyncHandler, nullptr, &ctx, 1 << 20, limit);
		const int n = 12;
		max_ahead = 0;
		for (int i = 0; i < n; i++)
		{
			const uint8_t regs[152] = {};
			ring.post(kVsyncCmd, obgs::Ring::kVsync, 0, 0, 0, regs, sizeof(regs));
			// Vsyncs posted but not yet executed, as the producer leaves post().
			max_ahead = std::max(max_ahead, (i + 1) - ctx.executed.load());
		}
		stalls = ring.stats().vsyncStalls;
		ring.stop();
	}

	void TestQueueAhead()
	{
		int ahead = 0;
		uint64_t stalls = 0;
		TestQueueAhead(2, ahead, stalls);
		char d[96];
		std::snprintf(d, sizeof(d), "(max %d vsyncs queued after post, %llu stalls)", ahead, (unsigned long long)stalls);
		Check("queue-ahead 2: producer never more than 2 vsyncs ahead", ahead <= 2, d);
		Check("queue-ahead 2: producer does run ahead (2 queued) and stalls past it", ahead == 2 && stalls > 0, d);
		TestQueueAhead(-1, ahead, stalls);
		std::snprintf(d, sizeof(d), "(no limit: max %d queued)", ahead);
		Check("queue-ahead off: the limit is what holds the producer", ahead > 2, d);
	}

	// ---- stop executes what is queued ----
	void CountHandler(void* c, const obgs::RecordHeader&, const uint8_t*)
	{
		SpinFor(std::chrono::microseconds(200));
		(*static_cast<int*>(c))++;
	}

	void TestStop()
	{
		int count = 0;
		{
			obgs::Ring ring;
			ring.start(&CountHandler, nullptr, &count, 1 << 16, -1);
			for (int i = 0; i < 200; i++)
				ring.post(kData, 0, 0, 0, 0, nullptr, 0);
			ring.stop();
		}
		char d[64];
		std::snprintf(d, sizeof(d), "(%d of 200)", count);
		Check("stop: every queued record executes before the thread ends", count == 200, d);
	}
	// ---- sleep/wake stress: both sides keep falling asleep; a lost wake-up hangs (the watchdog fails it) ----
	struct StressCtx
	{
		uint32_t rng = 777;
		uint64_t done = 0;
	};
	void StressHandler(void* c, const obgs::RecordHeader& h, const uint8_t*)
	{
		StressCtx& x = *static_cast<StressCtx*>(c);
		x.rng = x.rng * 1103515245u + 12345u;
		const uint32_t r = (x.rng >> 16) % 100;
		if (r < 3)
			SpinFor(std::chrono::microseconds(150 + r * 40)); // past the producer's spin: it sleeps
		x.done++;
		if (h.cmd == kRead)
			*reinterpret_cast<uint64_t*>(h.a) = x.done;
	}

	void TestStress()
	{
		StressCtx ctx;
		obgs::Ring ring;
		ring.start(&StressHandler, nullptr, &ctx, 1 << 16, 2);
		std::atomic<bool> finished{false};
		std::thread watchdog([&] {
			const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (!finished.load() && std::chrono::steady_clock::now() < end)
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			if (!finished.load())
			{
				std::printf("[Failed] stress: producer or GS thread still asleep after 20 s (lost wake-up)\nPassed: %d\nTotal Tests: %d\n",
					g_passed, g_total + 1);
				std::fflush(stdout);
				std::_Exit(1);
			}
		});
		// CPU hogs (2 per core): the producer and the GS thread get preempted between their wake-up checks, as the
		// game thread is under load in a real run (the lost wake-up of entry 27 needs exactly that).
		std::atomic<bool> hog_stop{false};
		std::vector<std::thread> hogs;
		for (unsigned i = 0; i < 2 * std::max(1u, std::thread::hardware_concurrency()); i++)
			hogs.emplace_back([&] { while (!hog_stop.load(std::memory_order_relaxed)) OB_GS_RING_PAUSE(); });
		uint32_t rng = 4242;
		uint64_t posted = 0, bad = 0;
		const uint8_t pkt[96] = {};
		const auto t0 = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(1500))
		{
			rng = rng * 1103515245u + 12345u;
			const uint32_t r = (rng >> 16) % 100;
			if (r < 4)
				SpinFor(std::chrono::microseconds(100 + r * 50)); // past the GS thread's spin: it sleeps
			if (r < 10)
			{
				uint64_t seen = 0;
				ring.call(kRead, 0, reinterpret_cast<uint64_t>(&seen), 0);
				posted++;
				bad += seen != posted;
			}
			else if (r < 20)
			{
				ring.post(kVsyncCmd, obgs::Ring::kVsync, 0, 0, 0, pkt, sizeof(pkt));
				posted++;
			}
			else
			{
				ring.post(kData, 0, 0, 0, 0, pkt, (r * 13) % sizeof(pkt));
				posted++;
			}
		}
		hog_stop = true;
		for (std::thread& t : hogs)
			t.join();
		ring.stop();
		finished = true;
		watchdog.join();
		char d[192];
		std::snprintf(d, sizeof(d), "(%llu records, %llu executed, %llu reads saw the wrong count, %llu sync waits, %llu vsync stalls, %llu timeout wakes)",
			(unsigned long long)posted, (unsigned long long)ctx.done, (unsigned long long)bad,
			(unsigned long long)ring.stats().syncWaits, (unsigned long long)ring.stats().vsyncStalls,
			(unsigned long long)ring.timeoutWakes());
		Check("stress: 1.5 s of sleeps and wakes on both sides, no lost wake-up, reads exact",
			ctx.done == posted && bad == 0 && ring.timeoutWakes() == 0, d);
	}
} // namespace

int main()
{
	const auto t0 = std::chrono::steady_clock::now();
	TestOrdering();
	TestSyncReadback();
	TestQueueAhead();
	TestStop();
	TestStress();
	std::printf("time %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	std::printf("Passed: %d\nTotal Tests: %d\n", g_passed, g_total);
	return g_passed == g_total ? 0 : 1;
}
