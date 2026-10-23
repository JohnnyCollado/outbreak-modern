#include "pc_sampler.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <tuple>

bool ObPcSampler::Key::operator<(const Key &o) const
{
    return std::tie(tick, tid, pc) < std::tie(o.tick, o.tid, o.pc);
}

ObPcSampler::ObPcSampler(std::string path, TickFn tick, void *tickUser)
    : m_path(std::move(path)), m_tick(tick), m_tickUser(tickUser)
{
    if (!m_path.empty())
        m_thread = std::thread([this] { loop(); });
    else
        m_stopped = true; // tests: no thread, sampleOnce by hand
}

ObPcSampler::~ObPcSampler()
{
    stop();
}

void ObPcSampler::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    m_stopRequested.store(true, std::memory_order_release);
    if (m_thread.joinable())
        m_thread.join();
    write();
}

void ObPcSampler::guestEnter(uint32_t *pc, int32_t threadId)
{
    m_tid.store(threadId, std::memory_order_relaxed);
    m_pc.store(pc, std::memory_order_release);
}

void ObPcSampler::guestLeave()
{
    m_pc.store(nullptr, std::memory_order_release);
}

void ObPcSampler::sectionEnter(uint32_t section)
{
    if (section < kSections)
        m_depth[section].fetch_add(1, std::memory_order_relaxed);
}

void ObPcSampler::sectionLeave(uint32_t section)
{
    if (section < kSections)
        m_depth[section].fetch_sub(1, std::memory_order_relaxed);
}

void ObPcSampler::sampleOnce()
{
    Key key{};
    if (m_tick)
        key.tick = (m_tick(m_tickUser) / kTickBucket) * kTickBucket;
    if (uint32_t *pc = m_pc.load(std::memory_order_acquire))
    {
        key.pc = std::atomic_ref<uint32_t>(*pc).load(std::memory_order_relaxed);
        key.tid = m_tid.load(std::memory_order_relaxed);
    }
    Counts &c = m_counts[key];
    ++c.count;
    for (uint32_t s = 0; s < kSections; ++s)
        if (m_depth[s].load(std::memory_order_relaxed) > 0)
            ++c.sections[s];
}

void ObPcSampler::write() const
{
    if (m_path.empty())
        return;
    const std::string tmp = m_path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::out | std::ios::trunc);
        if (!out.is_open())
            return;
        out << "tick,tid,pc,count,vu1,iop,gs,gsvsync" << std::endl;
        for (const auto &[key, c] : m_counts)
        {
            out << key.tick << ',' << key.tid << ",0x" << std::hex << key.pc << std::dec << ',' << c.count;
            for (uint32_t s = 0; s < kSections; ++s)
                out << ',' << c.sections[s];
            out << '\n';
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, m_path, ec);
}

void ObPcSampler::loop()
{
    using clock = std::chrono::steady_clock;
    auto nextWrite = clock::now() + std::chrono::seconds(2);
    while (!m_stopRequested.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sampleOnce();
        if (clock::now() >= nextWrite)
        {
            write();
            nextWrite = clock::now() + std::chrono::seconds(2);
        }
    }
}
