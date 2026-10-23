#ifndef OB_DIAG_FRAME_TIME_H
#define OB_DIAG_FRAME_TIME_H

#include <cstdint>
#include <fstream>
#include <string>

// PS2X_FRAME_TIME=<path.csv>: per-vsync frame timing, "tick,wall_us,busy_us" (header first). wall_us = host time
// since the previous VBlankStart (0 on the first), busy_us = wall_us minus the scheduler's idle wait (pacing) in
// between. Fed by PS2DiagHooks::onVsync on the EE executor thread; each line is flushed.
class ObFrameTimeLog
{
public:
    bool open(const std::string &path);
    bool isOpen() const { return m_out.is_open(); }
    // nowNs = a steady clock in ns; idleWaitNs = the scheduler's cumulative idle wait.
    void onVsync(uint64_t tick, uint64_t nowNs, uint64_t idleWaitNs);
    void flush();

private:
    std::ofstream m_out;
    bool m_havePrev = false;
    uint64_t m_prevNs = 0;
    uint64_t m_prevWaitNs = 0;
};

#endif
