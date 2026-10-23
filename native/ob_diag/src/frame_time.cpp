#include "frame_time.h"

bool ObFrameTimeLog::open(const std::string &path)
{
    m_out.open(path, std::ios::out | std::ios::trunc);
    if (!m_out.is_open())
        return false;
    m_out << "tick,wall_us,busy_us" << std::endl;
    m_havePrev = false;
    return true;
}

void ObFrameTimeLog::onVsync(uint64_t tick, uint64_t nowNs, uint64_t idleWaitNs)
{
    if (!m_out.is_open())
        return;
    uint64_t wallUs = 0u;
    uint64_t busyUs = 0u;
    if (m_havePrev)
    {
        const uint64_t wallNs = nowNs - m_prevNs;
        const uint64_t waitNs = idleWaitNs - m_prevWaitNs;
        wallUs = wallNs / 1000u;
        busyUs = (wallNs > waitNs ? wallNs - waitNs : 0u) / 1000u;
    }
    m_out << tick << ',' << wallUs << ',' << busyUs << '\n';
    m_out.flush();
    m_prevNs = nowNs;
    m_prevWaitNs = idleWaitNs;
    m_havePrev = true;
}

void ObFrameTimeLog::flush()
{
    if (m_out.is_open())
        m_out.flush();
}
