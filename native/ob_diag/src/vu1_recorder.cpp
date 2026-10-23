#include "vu1_recorder.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace ps2x::vu1capture
{
    namespace
    {
        constexpr uint32_t kSummaryEvery = 4096u;
    }

    struct Recorder::Impl
    {
        std::string dir;
        uint32_t maxFiles = 200;
        uint32_t maxPerKey = 4;

        struct KeyStats
        {
            uint64_t firstGeneration = 0;
            uint64_t runs = 0;
            uint64_t cycles = 0;
            uint32_t files = 0;
        };
        using Key = std::tuple<uint32_t, uint32_t, uint64_t>; // kind, startPC, codeHash
        std::map<Key, KeyStats> keys;
        std::unordered_set<uint64_t> inputs;

        uint64_t runsSeen = 0;
        uint64_t budgetHits = 0;
        uint64_t bytesWritten = 0;
        uint32_t filesWritten = 0;
        uint32_t lastSummaryFiles = 0;

        uint64_t cachedGeneration = ~0ull;
        const uint8_t *cachedCode = nullptr;
        uint64_t cachedCodeHash = 0;

        // The run in flight.
        bool active = false;
        bool recording = false;
        Key key{};
        FileHeader header{};
        std::vector<uint8_t> code, dataBefore;
        StateRecord before{};
        std::vector<std::vector<uint8_t>> packets;

        void writeSummary()
        {
            const std::string path = dir + "/summary.txt";
            const std::string tmp = path + ".tmp";
            FILE *f = std::fopen(tmp.c_str(), "wb");
            if (!f)
                return;
            std::fprintf(f, "runs %llu\nfiles %u\nbytes %llu\nkeys %zu\nbudget_hits %llu\n",
                         static_cast<unsigned long long>(runsSeen), filesWritten,
                         static_cast<unsigned long long>(bytesWritten), keys.size(),
                         static_cast<unsigned long long>(budgetHits));
            std::fprintf(f, "kind pc code_hash first_gen runs cycles files\n");
            for (const auto &[k, s] : keys)
                std::fprintf(f, "%s 0x%04x %016llx %llu %llu %llu %u\n",
                             std::get<0>(k) == Mscal ? "mscal" : "mscnt", std::get<1>(k),
                             static_cast<unsigned long long>(std::get<2>(k)),
                             static_cast<unsigned long long>(s.firstGeneration),
                             static_cast<unsigned long long>(s.runs),
                             static_cast<unsigned long long>(s.cycles), s.files);
            std::fclose(f);
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec)
            {
                std::filesystem::remove(path, ec);
                std::filesystem::rename(tmp, path, ec);
            }
        }
    };

    std::unique_ptr<Recorder> Recorder::fromSpec(const char *env)
    {
        if (!env || !*env)
            return nullptr;
        std::string spec(env);
        uint32_t maxFiles = 200u, maxPerKey = 4u;
        const size_t c1 = spec.find(',');
        std::string dir = spec.substr(0, c1);
        if (c1 != std::string::npos)
        {
            const std::string rest = spec.substr(c1 + 1);
            const size_t c2 = rest.find(',');
            maxFiles = static_cast<uint32_t>(std::strtoul(rest.substr(0, c2).c_str(), nullptr, 10));
            if (c2 != std::string::npos)
                maxPerKey = static_cast<uint32_t>(std::strtoul(rest.substr(c2 + 1).c_str(), nullptr, 10));
        }
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!std::filesystem::is_directory(dir, ec))
        {
            std::cerr << "[vu1-capture] not a directory: " << dir << std::endl;
            return nullptr;
        }
        std::cerr << "[vu1-capture] dir=" << dir << " max_files=" << maxFiles
                  << " max_per_key=" << maxPerKey << std::endl;
        return std::make_unique<Recorder>(dir, maxFiles, maxPerKey);
    }

    Recorder::Recorder(std::string dir, uint32_t maxFiles, uint32_t maxPerKey)
        : m_impl(std::make_unique<Impl>())
    {
        m_impl->dir = std::move(dir);
        m_impl->maxFiles = maxFiles;
        m_impl->maxPerKey = maxPerKey;
    }

    Recorder::~Recorder()
    {
        m_impl->writeSummary();
    }

    void Recorder::path1(const uint8_t *data, uint32_t size)
    {
        if (m_impl->recording)
            m_impl->packets.emplace_back(data, data + size);
    }

    void Recorder::flush() { m_impl->writeSummary(); }

    uint32_t Recorder::filesWritten() const { return m_impl->filesWritten; }
    uint64_t Recorder::runsSeen() const { return m_impl->runsSeen; }

    void Recorder::begin(Kind kind, uint32_t startPC, uint32_t top, uint32_t itop, uint32_t fbrst,
                         uint32_t vpuStat, uint32_t maxCycles, uint64_t codeGeneration,
                         const uint8_t *code, const uint8_t *data, const VU1State &state)
    {
        Impl &m = *m_impl;
        if (m.cachedCode != code || m.cachedGeneration != codeGeneration)
        {
            m.cachedCodeHash = hash64(code, kCodeBytes);
            m.cachedCode = code;
            m.cachedGeneration = codeGeneration;
        }
        m.active = true;
        m.recording = false;
        m.key = Impl::Key{kind, startPC, m.cachedCodeHash};
        Impl::KeyStats &ks = m.keys[m.key];
        if (ks.runs == 0)
            ks.firstGeneration = codeGeneration;

        FileHeader &h = m.header;
        h = FileHeader{};
        std::memcpy(h.magic, kMagic, sizeof(kMagic));
        h.version = kVersion;
        h.headerBytes = sizeof(FileHeader);
        h.kind = kind;
        h.runIndex = static_cast<uint32_t>(m.runsSeen);
        h.startPC = startPC;
        h.top = top;
        h.itop = itop;
        h.fbrst = fbrst;
        h.vpuStatBefore = vpuStat;
        h.maxCycles = maxCycles;
        h.codeGeneration = codeGeneration;
        h.codeHash = m.cachedCodeHash;
        h.codeBytes = kCodeBytes;
        h.dataBytes = kDataBytes;
        h.stateBytes = sizeof(StateRecord);
        m.before = toRecord(state);

        if (m.filesWritten >= m.maxFiles || ks.files >= m.maxPerKey)
            return;
        const uint32_t params[5] = {kind, startPC, top, itop, fbrst};
        uint64_t in = hash64(params, sizeof(params), m.cachedCodeHash);
        in = hash64(data, kDataBytes, in);
        // The cycle counter always differs; MSCAL also overwrites pc/branch/stop state (execute()).
        StateRecord inputState = m.before;
        inputState.cycles = 0;
        if (kind == Mscal)
        {
            inputState.pc = inputState.branchTarget = inputState.branchDelay = 0;
            inputState.ebit = inputState.haltAfterDelaySlot = inputState.branchPending = 0;
            inputState.stoppedByD = inputState.stoppedByT = 0;
            inputState.top = inputState.itop = 0;
        }
        in = hash64(&inputState, sizeof(inputState), in);
        if (!m.inputs.insert(in).second)
            return;
        h.inputHash = in;
        m.code.assign(code, code + kCodeBytes);
        m.dataBefore.assign(data, data + kDataBytes);
        m.packets.clear();
        m.recording = true;
    }

    void Recorder::end(const uint8_t *data, const VU1State &state, uint32_t vpuStat)
    {
        Impl &m = *m_impl;
        if (!m.active)
            return;
        m.active = false;
        FileHeader &h = m.header;
        h.cyclesRun = state.cycles - m.before.cycles;
        if (h.cyclesRun >= h.maxCycles)
        {
            h.flags |= FlagBudgetHit;
            ++m.budgetHits;
        }
        h.vpuStatAfter = vpuStat;
        Impl::KeyStats &ks = m.keys[m.key];
        ++ks.runs;
        ks.cycles += h.cyclesRun;
        ++m.runsSeen;

        if (m.recording)
        {
            m.recording = false;
            h.packetCount = static_cast<uint32_t>(m.packets.size());
            const StateRecord after = toRecord(state);
            char name[64];
            std::snprintf(name, sizeof(name), "/vu1-%06u-%04x.cap", m.filesWritten, h.startPC);
            const std::string path = m.dir + name;
            FILE *f = std::fopen(path.c_str(), "wb");
            if (f)
            {
                uint64_t bytes = 0;
                auto put = [&](const void *p, size_t n)
                {
                    std::fwrite(p, 1, n, f);
                    bytes += n;
                };
                put(&h, sizeof(h));
                put(m.code.data(), m.code.size());
                put(m.dataBefore.data(), m.dataBefore.size());
                put(&m.before, sizeof(m.before));
                put(data, kDataBytes);
                put(&after, sizeof(after));
                for (const auto &pk : m.packets)
                {
                    const uint32_t size = static_cast<uint32_t>(pk.size());
                    put(&size, sizeof(size));
                    put(pk.data(), pk.size());
                }
                std::fclose(f);
                ++m.filesWritten;
                ++ks.files;
                m.bytesWritten += bytes;
            }
            m.packets.clear();
        }
        if (m.filesWritten != m.lastSummaryFiles || (m.runsSeen % kSummaryEvery) == 0)
        {
            m.lastSummaryFiles = m.filesWritten;
            m.writeSummary();
        }
    }
}
