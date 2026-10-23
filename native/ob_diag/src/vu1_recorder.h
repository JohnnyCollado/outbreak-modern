#pragma once

// PS2X_VU1_CAPTURE=<dir>[,N[,K]] recorder (format and rules: include/ob_vu1_capture.h). Fed by the plugin's
// onVu1Begin / onVu1Path1 / onVu1End on the EE executor thread.

#include <cstdint>
#include <memory>
#include <string>

#include "ob_vu1_capture.h"

namespace ps2x::vu1capture
{
    class Recorder
    {
    public:
        // Null unless spec ("<dir>[,N[,K]]") names a directory (created if missing).
        static std::unique_ptr<Recorder> fromSpec(const char *spec);
        Recorder(std::string dir, uint32_t maxFiles, uint32_t maxPerKey);
        ~Recorder();

        // Around one VU1 run. begin() decides whether the run is written; path1() collects its XGKICK packets;
        // end() writes it.
        void begin(Kind kind, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst,
                   uint32_t vpuStat, uint32_t maxCycles, uint64_t codeGeneration,
                   const uint8_t *code, const uint8_t *data, const VU1State &state);
        void path1(const uint8_t *data, uint32_t size);
        void end(const uint8_t *data, const VU1State &state, uint32_t vpuStat);

        void flush(); // rewrites summary.txt
        uint32_t filesWritten() const;
        uint64_t runsSeen() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
