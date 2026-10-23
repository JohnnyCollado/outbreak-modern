#ifndef OB_DIAG_PC_SAMPLER_H
#define OB_DIAG_PC_SAMPLER_H

// PS2X_PC_SAMPLE=<path.csv>: guest PC sampling profiler.
//
// A host thread samples, about every 1 ms (the host sleep granularity), the pc of the guest code the EE executor
// is running (PS2DiagHooks::onGuestEnter hands over a pointer to the live pc), the guest thread id, the vsync
// tick, and which engine sections are active (onSectionEnter/Leave). The aggregate is written as CSV
// "tick,tid,pc,count,vu1,iop,gs,gsvsync": one row per (tick bucket, thread, pc), tick = first vsync of a
// kTickBucket-wide bucket, count = samples, the section columns = how many of those samples had it active.
// pc 0 / tid 0 = the executor was not inside guest code. Rewritten every ~2 s (via <path>.tmp + rename) and at
// stop, so a killed run loses at most two seconds. Reads of the pc race with the generated code's plain stores;
// accepted for a profiler (std::atomic_ref keeps each read a real load).

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>

class ObPcSampler
{
public:
    static constexpr uint64_t kTickBucket = 64u;
    static constexpr uint32_t kSections = 4u; // PS2DiagSection::Count

    using TickFn = uint64_t (*)(void *user);
    ObPcSampler(std::string path, TickFn tick, void *tickUser);
    ~ObPcSampler(); // stop()
    void stop();    // joins the thread and writes the final CSV; idempotent

    // EE executor thread.
    void guestEnter(uint32_t *pc, int32_t threadId);
    void guestLeave();
    void sectionEnter(uint32_t section);
    void sectionLeave(uint32_t section);

    void sampleOnce(); // the sampler thread's step (tests call it directly on a stopped sampler)
    void write() const;

    ObPcSampler(const ObPcSampler &) = delete;
    ObPcSampler &operator=(const ObPcSampler &) = delete;

private:
    struct Key
    {
        uint64_t tick = 0u;
        int32_t tid = 0;
        uint32_t pc = 0u;
        bool operator<(const Key &o) const;
    };
    struct Counts
    {
        uint64_t count = 0u;
        uint64_t sections[kSections]{};
    };

    void loop();

    std::string m_path;
    TickFn m_tick;
    void *m_tickUser;
    std::atomic<uint32_t *> m_pc{nullptr};
    std::atomic<int32_t> m_tid{0};
    std::atomic<int32_t> m_depth[kSections]{};
    std::map<Key, Counts> m_counts; // sampler thread only (the final write runs after join)
    std::atomic<bool> m_stopRequested{false};
    bool m_stopped = false;
    std::thread m_thread;
};

#endif
