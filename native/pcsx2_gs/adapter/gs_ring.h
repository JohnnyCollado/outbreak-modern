// gs_ring: the GS thread's command ring (PCSX2 MTGS's scheme, pcsx2/MTGS.cpp). One producer (the game thread) copies
// each GS command into a single-producer/single-consumer ring of 32-byte records; one consumer thread (the GS thread)
// executes them in ring order through a handler. Synchronous commands (readback, snapshot) wait until the GS thread
// has executed every earlier record and the command itself, as MTGS::SendPointerPacket + WaitGS(). Vsync records are
// counted: after posting one, the producer stalls while more than queueAheadVsyncs are queued, as
// MTGS::PostVsyncStart's s_QueuedFrameCount vs EmuConfig.GS.VsyncQueueSize (Config.h, default 2).
// Plain C++20: no PCSX2 or PS2Recomp headers, so the ring is testable on its own (tests/gs_ring_tests.cpp).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <immintrin.h>
#define OB_GS_RING_PAUSE() _mm_pause()
#else
#define OB_GS_RING_PAUSE() std::this_thread::yield()
#endif

namespace obgs
{
	// One record = this header + payload, both padded to a multiple of kRecordAlign. The handler never sees kWrap
	// or kQuit (ring-internal).
	struct RecordHeader
	{
		uint32_t cmd;   // caller's command id (>= kFirstUserCmd)
		uint32_t bytes; // payload bytes (unpadded); with kIndirect, the bytes behind the pointer
		uint32_t flags; // kVsync, kIndirect, kSync
		uint32_t aux;   // caller's
		uint64_t a, b;  // caller's
	};
	static_assert(sizeof(RecordHeader) == 32, "records are 32-byte aligned");

	class Ring
	{
	public:
		static constexpr uint32_t kRecordAlign = 32;
		static constexpr uint32_t kWrap = 0, kQuit = 1, kFirstUserCmd = 2;
		static constexpr uint32_t kVsync = 1u, kIndirect = 2u, kSync = 4u;
		// Executes one record on the GS thread. payload: the copied bytes (or, kIndirect, the producer's own bytes,
		// still valid: the producer waits for an indirect record to finish).
		using Handler = void (*)(void* ctx, const RecordHeader& h, const uint8_t* payload);
		using ThreadInit = void (*)(void* ctx);

		// Producer-side counters (read only by the producer, or after stop()).
		struct Stats
		{
			uint64_t records = 0, bytes = 0, syncWaits = 0, indirect = 0;
			uint64_t vsyncStalls = 0, vsyncStallNs = 0; // queue-ahead limit reached
			uint64_t fullStalls = 0;                     // ring had no room
			uint64_t syncWaitNs = 0;
		};

		Ring() = default;
		Ring(const Ring&) = delete;
		Ring& operator=(const Ring&) = delete;
		~Ring() { stop(); std::free(m_buf); }

		// capacityBytes: a power of two >= 4 KiB. queueAheadVsyncs < 0 = no limit.
		bool start(Handler handler, ThreadInit init, void* ctx, size_t capacityBytes, int queueAheadVsyncs)
		{
			if (m_running || capacityBytes < 4096 || (capacityBytes & (capacityBytes - 1)) != 0)
				return false;
			m_buf = static_cast<uint8_t*>(std::malloc(capacityBytes + 64));
			if (!m_buf)
				return false;
			m_data = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(m_buf) + 63) & ~uintptr_t(63));
			m_cap = capacityBytes;
			m_mask = capacityBytes - 1;
			m_handler = handler;
			m_init = init;
			m_ctx = ctx;
			m_queueAhead = queueAheadVsyncs;
			m_write.store(0);
			m_read.store(0);
			m_wpos = 0;
			m_running = true;
			m_thread = std::thread([this] { consumerLoop(); });
			return true;
		}

		bool running() const { return m_running; }
		size_t capacity() const { return m_cap; }
		int queueAhead() const { return m_queueAhead; }
		const Stats& stats() const { return m_stats; }
		int queuedVsyncs() const { return m_queued.load(std::memory_order_acquire); }
		// Sleeps that ended by the 20 ms timeout with their condition met and no wake-up: a lost wake-up. Expect 0.
		uint64_t timeoutWakes() const { return m_timeoutWakes.load(std::memory_order_relaxed); }

		// Producer: copies payload into the ring and returns at once. A payload larger than a quarter of the ring
		// goes by pointer instead and the call waits for it (the bytes stay valid). flags may hold kVsync.
		void post(uint32_t cmd, uint32_t flags, uint32_t aux, uint64_t a, uint64_t b, const void* payload, uint32_t bytes)
		{
			if (static_cast<size_t>(bytes) + kRecordAlign > m_cap / 4)
			{
				m_stats.indirect++;
				const void* p = payload;
				send(cmd, flags | kIndirect | kSync, aux, a, b, &p, sizeof(p), bytes);
				return;
			}
			send(cmd, flags, aux, a, b, payload, bytes, bytes);
		}

		// Producer: posts the record and waits until the GS thread has executed it (and so every earlier record).
		void call(uint32_t cmd, uint32_t aux, uint64_t a, uint64_t b, const void* payload = nullptr, uint32_t bytes = 0)
		{
			send(cmd, kSync, aux, a, b, payload, bytes, bytes);
		}

		// Producer: waits until every posted record has been executed.
		void drain() { waitRead(m_wpos); }

		// Producer (or the owner, once the producer is gone): executes everything queued, ends the thread.
		void stop()
		{
			if (!m_running)
				return;
			send(kQuit, 0, 0, 0, 0, nullptr, 0, 0);
			m_thread.join();
			m_running = false;
		}

	private:
		static uint32_t padded(uint32_t n) { return (n + kRecordAlign - 1) & ~(kRecordAlign - 1); }

		void send(uint32_t cmd, uint32_t flags, uint32_t aux, uint64_t a, uint64_t b, const void* payload,
			uint32_t copyBytes, uint32_t declaredBytes)
		{
			const uint64_t need = sizeof(RecordHeader) + padded(copyBytes);
			uint64_t off = m_wpos & m_mask;
			if (off + need > m_cap)
			{
				// Not enough room before the end: a wrap record (always fits: positions are 32-aligned) sends the
				// reader to the start, then the record goes at offset 0.
				waitSpace(m_cap - off);
				RecordHeader w{};
				w.cmd = kWrap;
				std::memcpy(m_data + off, &w, sizeof(w));
				m_wpos += m_cap - off;
				off = 0;
			}
			waitSpace(need);
			RecordHeader h{cmd, declaredBytes, flags, aux, a, b};
			std::memcpy(m_data + off, &h, sizeof(h));
			if (copyBytes)
				std::memcpy(m_data + off + sizeof(h), payload, copyBytes);
			m_wpos += need;
			const uint64_t end = m_wpos;
			m_stats.records++;
			m_stats.bytes += need;
			if (flags & kVsync)
				m_queued.fetch_add(1, std::memory_order_seq_cst); // before publishing: the consumer only ever decrements a counted one
			publish();
			if (flags & kSync)
			{
				m_stats.syncWaits++;
				const auto t0 = std::chrono::steady_clock::now();
				waitRead(end);
				m_stats.syncWaitNs += ns(t0);
			}
			if ((flags & kVsync) && m_queueAhead >= 0 && m_queued.load(std::memory_order_seq_cst) > m_queueAhead)
			{
				// MTGS::PostVsyncStart: fetch_add(1) < VsyncQueueSize returns; else wait for the GS thread to finish a
				// vsync. Ours: wait until at most queueAhead vsyncs are queued.
				m_stats.vsyncStalls++;
				const auto t0 = std::chrono::steady_clock::now();
				waitUntil([this] { return m_queued.load(std::memory_order_seq_cst) <= m_queueAhead; });
				m_stats.vsyncStallNs += ns(t0);
			}
		}

		static uint64_t ns(std::chrono::steady_clock::time_point t0)
		{
			return static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
		}

		void publish()
		{
			m_write.store(m_wpos, std::memory_order_seq_cst);
			wake(m_consumerSleeping, m_consumerCv);
		}

		// Sleep/wake (only when a side has run out of spinning). The sleeper sets its flag, re-checks its condition,
		// then waits on a condition variable whose predicate is re-read under the mutex; the waker clears the flag
		// with exchange (never a plain store: a blind clear could erase a flag set for a later sleep, which then
		// misses its wake-up: the hang of report entry 27) and notifies under the same mutex. A 20 ms timeout is
		// a safety valve (PCSX2's MTGS keeps one too); a wake that only the timeout delivered is counted.
		void wake(std::atomic<bool>& flag, std::condition_variable& cv)
		{
			if (flag.exchange(false, std::memory_order_seq_cst))
			{
				std::lock_guard<std::mutex> lk(m_sleepMu);
				cv.notify_one();
			}
		}

		template <typename Cond>
		void sleepUntil(std::atomic<bool>& flag, std::condition_variable& cv, Cond cond)
		{
			std::unique_lock<std::mutex> lk(m_sleepMu);
			flag.store(true, std::memory_order_seq_cst);
			while (!cond())
			{
				if (!flag.load(std::memory_order_seq_cst))
					return; // woken: the caller re-checks
				if (cv.wait_for(lk, std::chrono::milliseconds(20)) == std::cv_status::timeout && flag.load() && cond())
					m_timeoutWakes.fetch_add(1, std::memory_order_relaxed);
			}
			flag.store(false, std::memory_order_seq_cst);
		}

		void waitSpace(uint64_t need)
		{
			if (m_cap - (m_wpos - m_read.load(std::memory_order_acquire)) >= need)
				return;
			m_stats.fullStalls++;
			// The GS thread must see what is already queued to make room.
			waitUntil([this, need] { return m_cap - (m_wpos - m_read.load(std::memory_order_acquire)) >= need; });
		}

		void waitRead(uint64_t pos)
		{
			waitUntil([this, pos] { return m_read.load(std::memory_order_acquire) >= pos; });
		}

		// Producer wait: spin, then sleep until the consumer's next m_read bump (after every record, after any vsync
		// count change) wakes it to re-check.
		template <typename Cond>
		void waitUntil(Cond cond)
		{
			for (int i = 0; i < 4000; i++)
			{
				if (cond())
					return;
				OB_GS_RING_PAUSE();
			}
			while (!cond())
				sleepUntil(m_producerSleeping, m_producerCv, cond);
		}

		void consumerLoop()
		{
			if (m_init)
				m_init(m_ctx);
			uint64_t r = 0;
			for (;;)
			{
				uint64_t w = m_write.load(std::memory_order_acquire);
				if (w == r)
				{
					for (int i = 0; i < 2000 && w == r; i++)
					{
						OB_GS_RING_PAUSE();
						w = m_write.load(std::memory_order_acquire);
					}
					while (w == r)
					{
						sleepUntil(m_consumerSleeping, m_consumerCv,
							[this, r] { return m_write.load(std::memory_order_seq_cst) != r; });
						w = m_write.load(std::memory_order_acquire);
					}
				}
				while (r != w)
				{
					const uint64_t off = r & m_mask;
					RecordHeader h;
					std::memcpy(&h, m_data + off, sizeof(h));
					if (h.cmd == kWrap)
					{
						r += m_cap - off;
						continue; // no wake: a wrap completes nothing the producer waits on that the next record won't
					}
					const uint32_t copyBytes = (h.flags & kIndirect) ? static_cast<uint32_t>(sizeof(void*)) : h.bytes;
					const uint8_t* payload = m_data + off + sizeof(h);
					if (h.flags & kIndirect)
					{
						const void* p;
						std::memcpy(&p, payload, sizeof(p));
						payload = static_cast<const uint8_t*>(p);
					}
					if (h.cmd != kQuit)
						m_handler(m_ctx, h, payload);
					if (h.flags & kVsync)
						m_queued.fetch_sub(1, std::memory_order_seq_cst);
					r += sizeof(h) + padded(copyBytes);
					m_read.store(r, std::memory_order_seq_cst);
					wake(m_producerSleeping, m_producerCv);
					if (h.cmd == kQuit)
						return;
				}
			}
		}

		// Producer-owned.
		uint64_t m_wpos = 0;
		Stats m_stats;
		// Shared.
		alignas(64) std::atomic<uint64_t> m_write{0};
		alignas(64) std::atomic<uint64_t> m_read{0};
		alignas(64) std::atomic<int> m_queued{0};
		std::atomic<bool> m_consumerSleeping{false};
		std::atomic<bool> m_producerSleeping{false};
		std::atomic<uint64_t> m_timeoutWakes{0};
		std::mutex m_sleepMu;
		std::condition_variable m_consumerCv, m_producerCv;
		// Set at start.
		uint8_t* m_buf = nullptr;
		uint8_t* m_data = nullptr;
		size_t m_cap = 0, m_mask = 0;
		Handler m_handler = nullptr;
		ThreadInit m_init = nullptr;
		void* m_ctx = nullptr;
		int m_queueAhead = 2;
		bool m_running = false;
		std::thread m_thread;
	};
} // namespace obgs
