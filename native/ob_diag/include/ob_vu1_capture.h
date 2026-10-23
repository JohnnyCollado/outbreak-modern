#pragma once

// VU1 capture file format (ob_diag plugin, PS2X_VU1_CAPTURE=<dir>[,N[,K]]); also read by ob.sh vucompare.
//
// When the variable names a directory, every VU1 run started by VIF1 MSCAL/MSCALF/MSCNT
// (PS2DiagHooks::onVu1Begin/End) is offered to a Recorder. A run is written when
//   - fewer than N files were written so far (default 200),
//   - its key (kind, start pc, hash of the 16 KB micro memory) has fewer than K files (default 4),
//   - no run with the same inputs (key + data memory + VU1 state + TOP/ITOP/FBRST) was written.
// One file per written run, "<dir>/vu1-<index6>-<pc4>.cap", little-endian:
//   FileHeader | code[codeBytes] | dataBefore[dataBytes] | StateRecord before |
//   dataAfter[dataBytes] | StateRecord after | packetCount x { u32 size, size bytes }
// The packets are every PATH1 packet (XGKICK) the run sent, in order. "<dir>/summary.txt" is
// rewritten every 4096 runs and at each written file: totals plus one line per key seen
// (pc, code hash, code generation first seen, runs, interpreter cycles), so the stream of
// unwritten runs is still accounted for.

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "runtime/ps2_vu1.h"

namespace ps2x::vu1capture
{
    constexpr char kMagic[8] = {'O', 'B', 'V', 'U', '1', 'C', 'A', 'P'};
    constexpr uint32_t kVersion = 1u;
    constexpr uint32_t kCodeBytes = 16u * 1024u; // VU1 micro memory
    constexpr uint32_t kDataBytes = 16u * 1024u; // VU1 data memory

    enum Kind : uint32_t
    {
        Mscal = 0, // MSCAL / MSCALF: VU1Interpreter::execute(startPC)
        Mscnt = 1  // MSCNT: VU1Interpreter::resume (continues at state.pc)
    };

    enum HeaderFlags : uint32_t
    {
        FlagBudgetHit = 1u << 0 // the run used its whole cycle budget (program not ended)
    };

    // VU1State with floats as raw bits and a fixed layout.
    struct StateRecord
    {
        uint32_t vf[32][4];
        int32_t vi[16];
        uint32_t acc[4];
        uint32_t q, p, i, r;
        uint32_t pc, mac, clip, status;
        uint64_t cycles;
        uint8_t ebit, haltAfterDelaySlot, dBitEnabled, tBitEnabled;
        uint8_t stoppedByD, stoppedByT, branchPending, reserved0;
        uint32_t top, itop, branchTarget, branchDelay;
    };
    static_assert(sizeof(StateRecord) == 512 + 64 + 16 + 16 + 16 + 8 + 8 + 16, "StateRecord layout");

    struct FileHeader
    {
        char magic[8];
        uint32_t version;
        uint32_t headerBytes;   // sizeof(FileHeader)
        uint32_t kind;          // Kind
        uint32_t flags;         // HeaderFlags
        uint32_t runIndex;      // 0-based count of VU1 runs seen before this one
        uint32_t startPC;       // MSCAL: byte address passed to execute; MSCNT: state.pc on entry
        uint32_t top, itop;     // VIF1 TOP/ITOP passed to the run
        uint32_t fbrst;         // EE-side VU0 FBRST (D enable bit 10, T enable bit 11)
        uint32_t vpuStatBefore; // EE-side VPU_STAT before / after the callback
        uint32_t vpuStatAfter;
        uint32_t maxCycles;     // cycle budget passed to the run
        uint64_t cyclesRun;     // interpreter cycles the run took (state.cycles after - before)
        uint64_t codeGeneration;// PS2Memory::getVU1CodeGeneration() at the run
        uint64_t codeHash;      // hash64 of the 16 KB micro memory
        uint64_t inputHash;     // dedupe hash of all inputs
        uint32_t codeBytes, dataBytes, stateBytes, packetCount;
    };
    static_assert(sizeof(FileHeader) == 104, "FileHeader layout");

    inline uint32_t floatBits(float f)
    {
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return u;
    }
    inline float bitsFloat(uint32_t u)
    {
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    inline StateRecord toRecord(const VU1State &s)
    {
        StateRecord r{};
        for (int reg = 0; reg < 32; ++reg)
            for (int lane = 0; lane < 4; ++lane)
                r.vf[reg][lane] = floatBits(s.vf[reg][lane]);
        for (int k = 0; k < 16; ++k)
            r.vi[k] = s.vi[k];
        for (int lane = 0; lane < 4; ++lane)
            r.acc[lane] = floatBits(s.acc[lane]);
        r.q = floatBits(s.q);
        r.p = floatBits(s.p);
        r.i = floatBits(s.i);
        r.r = s.r;
        r.pc = s.pc;
        r.mac = s.mac;
        r.clip = s.clip;
        r.status = s.status;
        r.cycles = s.cycles;
        r.ebit = s.ebit;
        r.haltAfterDelaySlot = s.haltAfterDelaySlot;
        r.dBitEnabled = s.dBitEnabled;
        r.tBitEnabled = s.tBitEnabled;
        r.stoppedByD = s.stoppedByD;
        r.stoppedByT = s.stoppedByT;
        r.branchPending = s.branchPending;
        r.top = s.top;
        r.itop = s.itop;
        r.branchTarget = s.branchTarget;
        r.branchDelay = s.branchDelay;
        return r;
    }

    inline void fromRecord(const StateRecord &r, VU1State &s)
    {
        for (int reg = 0; reg < 32; ++reg)
            for (int lane = 0; lane < 4; ++lane)
                s.vf[reg][lane] = bitsFloat(r.vf[reg][lane]);
        for (int k = 0; k < 16; ++k)
            s.vi[k] = r.vi[k];
        for (int lane = 0; lane < 4; ++lane)
            s.acc[lane] = bitsFloat(r.acc[lane]);
        s.q = bitsFloat(r.q);
        s.p = bitsFloat(r.p);
        s.i = bitsFloat(r.i);
        s.r = r.r;
        s.pc = r.pc;
        s.mac = r.mac;
        s.clip = r.clip;
        s.status = r.status;
        s.cycles = r.cycles;
        s.ebit = r.ebit != 0;
        s.haltAfterDelaySlot = r.haltAfterDelaySlot != 0;
        s.dBitEnabled = r.dBitEnabled != 0;
        s.tBitEnabled = r.tBitEnabled != 0;
        s.stoppedByD = r.stoppedByD != 0;
        s.stoppedByT = r.stoppedByT != 0;
        s.branchPending = r.branchPending != 0;
        s.top = r.top;
        s.itop = r.itop;
        s.branchTarget = r.branchTarget;
        s.branchDelay = r.branchDelay;
    }

    inline uint64_t hash64(const void *data, size_t bytes, uint64_t seed = 0x9E3779B97F4A7C15ull)
    {
        // 8 bytes per step multiply-xorshift; not cryptographic, only for dedupe/keys.
        const uint8_t *p = static_cast<const uint8_t *>(data);
        uint64_t h = seed ^ (bytes * 0xFF51AFD7ED558CCDull);
        size_t i = 0;
        for (; i + 8 <= bytes; i += 8)
        {
            uint64_t v;
            std::memcpy(&v, p + i, 8);
            h ^= v;
            h *= 0x9FB21C651E98DF25ull;
            h ^= h >> 29;
        }
        for (; i < bytes; ++i)
        {
            h ^= p[i];
            h *= 0x100000001B3ull;
        }
        h ^= h >> 32;
        return h;
    }
}
