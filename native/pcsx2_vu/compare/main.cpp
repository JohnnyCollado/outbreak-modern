// ob_pcsx2_vu_compare: offline differential test of VU1 backends on PS2X_VU1_CAPTURE files (ob_diag plugin;
// format: native/ob_diag/include/ob_vu1_capture.h).
//
// For each capture: (0) replay it through PS2Recomp's VU1Interpreter (the runner's own ps2_runtime.lib) and
// check that the replay reproduces the recorded outputs (proves the harness), (a) time the interpreter,
// (b) run and time PCSX2's microVU (ob_pcsx2_vu), then compare microVU against the interpreter bit for bit:
// data memory, VF, VI (16 bits), ACC, Q, P, I, R, STATUS, MAC, CLIP, and the PATH1 (XGKICK) packets.
// Built with PS2Recomp headers only; microVU is reached through the plain-types ob_pcsx2_vu.h.
//
// usage: ob_pcsx2_vu_compare <capture dir> [--repeat N] [--verbose] [--no-flag-hack]
#include "ob_pcsx2_vu.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_diag_hooks.h"

#include "ob_vu1_capture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace cap = ps2x::vu1capture;
namespace fs = std::filesystem;

namespace
{
    using Packets = std::vector<std::vector<uint8_t>>;
    using Clock = std::chrono::steady_clock;
    double nsSince(Clock::time_point t0) { return std::chrono::duration<double, std::nano>(Clock::now() - t0).count(); }

    struct Capture
    {
        fs::path path;
        cap::FileHeader h{};
        std::vector<uint8_t> code, dataBefore, dataAfter;
        cap::StateRecord before{}, after{};
        Packets packets;
    };

    bool loadCapture(const fs::path &path, Capture &c, std::string &err)
    {
        std::ifstream in(path, std::ios::binary);
        const std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        size_t o = 0;
        auto take = [&](void *dst, size_t n) {
            if (o + n > b.size())
                return false;
            std::memcpy(dst, b.data() + o, n);
            o += n;
            return true;
        };
        auto takeVec = [&](std::vector<uint8_t> &v, size_t n) {
            v.resize(n);
            return take(v.data(), n);
        };
        c.path = path;
        if (!take(&c.h, sizeof(c.h)) || std::memcmp(c.h.magic, cap::kMagic, 8) != 0)
            return err = "bad magic", false;
        if (c.h.version != cap::kVersion || c.h.headerBytes != sizeof(cap::FileHeader) ||
            c.h.stateBytes != sizeof(cap::StateRecord) || c.h.codeBytes != 0x4000 || c.h.dataBytes != 0x4000)
            return err = "unsupported version/layout", false;
        if (!takeVec(c.code, c.h.codeBytes) || !takeVec(c.dataBefore, c.h.dataBytes) || !take(&c.before, sizeof(c.before)) ||
            !takeVec(c.dataAfter, c.h.dataBytes) || !take(&c.after, sizeof(c.after)))
            return err = "truncated", false;
        for (uint32_t k = 0; k < c.h.packetCount; ++k)
        {
            uint32_t size = 0;
            std::vector<uint8_t> p;
            if (!take(&size, 4) || !takeVec(p, size))
                return err = "truncated packet", false;
            c.packets.push_back(std::move(p));
        }
        return true;
    }

    // Outputs of one run in a backend-neutral form.
    struct Outputs
    {
        std::vector<uint8_t> data;
        uint32_t vf[32][4];
        uint32_t vi[16];
        uint32_t acc[4];
        uint32_t q, p, i, r, status, mac, clip;
        Packets packets;
    };

    void fromRecord(const cap::StateRecord &s, const std::vector<uint8_t> &data, const Packets &packets, Outputs &o)
    {
        o.data = data;
        std::memcpy(o.vf, s.vf, sizeof(o.vf));
        for (int k = 0; k < 16; ++k)
            o.vi[k] = static_cast<uint32_t>(s.vi[k]) & 0xFFFFu;
        std::memcpy(o.acc, s.acc, sizeof(o.acc));
        o.q = s.q;
        o.p = s.p;
        o.i = s.i;
        o.r = s.r;
        o.status = s.status;
        o.mac = s.mac;
        o.clip = s.clip;
        o.packets = packets;
    }

    // First differing field ("" = equal) and the number of differing fields.
    std::string diff(const Outputs &a, const Outputs &b, const char *nameA, const char *nameB, int &count,
                     std::vector<std::string> *all = nullptr)
    {
        count = 0;
        std::string first;
        auto note = [&](const std::string &s) {
            if (first.empty())
                first = s;
            ++count;
            if (all)
                all->push_back(s);
        };
        char buf[256];
        for (size_t k = 0; k < a.data.size(); k += 16)
            if (std::memcmp(a.data.data() + k, b.data.data() + k, 16) != 0)
            {
                uint32_t wa[4], wb[4];
                std::memcpy(wa, a.data.data() + k, 16);
                std::memcpy(wb, b.data.data() + k, 16);
                int lane = 0;
                while (lane < 3 && wa[lane] == wb[lane])
                    ++lane;
                std::snprintf(buf, sizeof(buf), "data qw 0x%03zx lane %d: %s %08x %s %08x", k / 16, lane, nameA, wa[lane], nameB, wb[lane]);
                note(buf);
            }
        static const char lanes[] = "xyzw";
        for (int r = 0; r < 32; ++r)
            for (int l = 0; l < 4; ++l)
                if (a.vf[r][l] != b.vf[r][l])
                {
                    std::snprintf(buf, sizeof(buf), "VF%02d.%c: %s %08x %s %08x", r, lanes[l], nameA, a.vf[r][l], nameB, b.vf[r][l]);
                    note(buf);
                }
        for (int r = 0; r < 16; ++r)
            if (a.vi[r] != b.vi[r])
            {
                std::snprintf(buf, sizeof(buf), "VI%02d: %s %04x %s %04x", r, nameA, a.vi[r], nameB, b.vi[r]);
                note(buf);
            }
        for (int l = 0; l < 4; ++l)
            if (a.acc[l] != b.acc[l])
            {
                std::snprintf(buf, sizeof(buf), "ACC.%c: %s %08x %s %08x", lanes[l], nameA, a.acc[l], nameB, b.acc[l]);
                note(buf);
            }
        const struct
        {
            const char *n;
            uint32_t x, y;
        } scal[] = {{"Q", a.q, b.q}, {"P", a.p, b.p}, {"I", a.i, b.i}, {"R", a.r, b.r},
                    {"STATUS", a.status, b.status}, {"MAC", a.mac, b.mac}, {"CLIP", a.clip, b.clip}};
        for (const auto &s : scal)
            if (s.x != s.y)
            {
                std::snprintf(buf, sizeof(buf), "%s: %s %08x %s %08x", s.n, nameA, s.x, nameB, s.y);
                note(buf);
            }
        if (a.packets.size() != b.packets.size())
        {
            std::snprintf(buf, sizeof(buf), "PATH1 packets: %s %zu %s %zu", nameA, a.packets.size(), nameB, b.packets.size());
            note(buf);
        }
        for (size_t k = 0; k < std::min(a.packets.size(), b.packets.size()); ++k)
        {
            const auto &pa = a.packets[k], &pb = b.packets[k];
            if (pa.size() != pb.size())
            {
                std::snprintf(buf, sizeof(buf), "PATH1 packet %zu size: %s %zu %s %zu", k, nameA, pa.size(), nameB, pb.size());
                note(buf);
            }
            else if (pa != pb)
            {
                size_t off = 0;
                while (pa[off] == pb[off])
                    ++off;
                std::snprintf(buf, sizeof(buf), "PATH1 packet %zu byte 0x%zx: %s %02x %s %02x", k, off, nameA, pa[off], nameB, pb[off]);
                note(buf);
            }
        }
        return first;
    }

    // ---- interpreter backend (the runner's ps2_runtime.lib) ----
    struct Interp
    {
        PS2Memory mem;
        GS gs;
        GifArbiter arbiter{[](const uint8_t *, uint32_t) {}};
        VU1Interpreter vu{VU1Interpreter::Unit::VU1};
        Packets *sink = nullptr;
        std::vector<uint8_t> lastCode;
        uint8_t primeCode[16]{};
        double lastNs = 0; // time of the execute/resume call alone

        bool init()
        {
            if (!mem.initialize())
                return false;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            mem.setGifArbiter(&arbiter);
            vu.reset();
            // NOP|E, NOP: used to latch the clip history (m_workingClip) from state.clip, see run().
            const uint32_t pair0[2] = {0x8000033Cu, 0x400002FFu}, pair1[2] = {0x8000033Cu, 0x000002FFu};
            std::memcpy(primeCode, pair0, 8);
            std::memcpy(primeCode + 8, pair1, 8);
            return true;
        }

        // The interpreter's XGKICK packets reach the diagnostics hooks (runtime/ps2_diag_hooks.h): collect them.
        struct Path1Tap final : PS2DiagHooks
        {
            Packets *sink = nullptr;
            void onVu1Path1(const uint8_t *d, uint32_t n) override
            {
                if (sink)
                    sink->emplace_back(d, d + n);
            }
        } tap;

        // Returns interpreter cycles of the run.
        uint64_t run(const Capture &c, Packets *packets)
        {
            if (c.code != lastCode)
            {
                std::memcpy(mem.getVU1Code(), c.code.data(), c.code.size());
                uint32_t w0;
                std::memcpy(&w0, c.code.data(), 4);
                mem.write32(PS2_VU1_CODE_BASE, w0); // bumps the code generation: the decode cache rebuilds
                lastCode = c.code;
            }
            std::memcpy(mem.getVU1Data(), c.dataBefore.data(), c.dataBefore.size());
            VU1State &s = vu.state();
            if (c.h.kind == cap::Mscnt)
            {
                // execute() latches the CLIP history from state.clip (resetScheduler); resume() does not. In the
                // game that history is the previous program's, equal to state.clip after its flush. Latch it
                // with a 2-instruction program on a private code buffer, then restore the recorded state.
                cap::fromRecord(c.before, s);
                vu.execute(primeCode, sizeof(primeCode), mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, nullptr, 0, 0, 0, 16);
            }
            // state.cycles mirrors the interpreter's private cycle counter after every run; keep it in sync.
            const uint64_t cyclesBefore = s.cycles;
            cap::fromRecord(c.before, s);
            s.cycles = cyclesBefore;
            sink = packets;
            tap.sink = packets;
            ps2SetDiagHooks(&tap);
            const auto t0 = Clock::now();
            if (c.h.kind == cap::Mscal)
                vu.execute(mem.getVU1Code(), PS2_VU1_CODE_SIZE, mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &mem,
                           c.h.startPC, c.h.top, c.h.itop, c.h.maxCycles);
            else
                vu.resume(mem.getVU1Code(), PS2_VU1_CODE_SIZE, mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &mem,
                          c.h.top, c.h.itop, c.h.maxCycles);
            lastNs = nsSince(t0);
            ps2SetDiagHooks(nullptr);
            tap.sink = nullptr;
            sink = nullptr;
            return vu.state().cycles - cyclesBefore;
        }

        void outputs(Outputs &o, const Packets &packets)
        {
            cap::StateRecord r = cap::toRecord(vu.state());
            std::vector<uint8_t> d(mem.getVU1Data(), mem.getVU1Data() + PS2_VU1_DATA_SIZE);
            fromRecord(r, d, packets, o);
        }
    };

    // ---- microVU backend ----
    struct Mvu
    {
        std::unique_ptr<uint8_t[]> data{new uint8_t[0x4000]};
        std::unique_ptr<uint8_t[]> code{new uint8_t[0x4000]};
        Packets *sink = nullptr;
        std::vector<uint8_t> lastCode;
        uint64_t generation = 0;
        double lastNs = 0; // time of ObPcsx2Vu::Run alone

        static void tap(void *ctx, const uint8_t *d, uint32_t n)
        {
            if (auto *p = static_cast<Mvu *>(ctx)->sink)
                p->emplace_back(d, d + n);
        }

        bool init(std::string &err)
        {
            char e[512] = {};
            if (!ObPcsx2Vu::Init(data.get(), code.get(), e, sizeof(e)))
                return err = e, false;
            ObPcsx2Vu::SetPath1Sink(&Mvu::tap, this);
            return true;
        }

        ObPcsx2Vu::RunResult run(const Capture &c, Packets *packets)
        {
            if (c.code != lastCode)
            {
                std::memcpy(code.get(), c.code.data(), c.code.size());
                lastCode = c.code;
                ++generation;
            }
            std::memcpy(data.get(), c.dataBefore.data(), c.dataBefore.size());
            ObPcsx2Vu::State s{};
            std::memcpy(s.vf, c.before.vf, sizeof(s.vf));
            s.vf[0][0] = s.vf[0][1] = s.vf[0][2] = 0u; // VF0 is hard-wired (0,0,0,1); execute() also resets it
            s.vf[0][3] = 0x3F800000u;
            for (int k = 0; k < 16; ++k)
                s.vi[k] = static_cast<uint32_t>(c.before.vi[k]) & 0xFFFFu;
            std::memcpy(s.acc, c.before.acc, sizeof(s.acc));
            s.q = c.before.q;
            s.p = c.before.p;
            s.i = c.before.i;
            s.r = c.before.r;
            s.status = c.before.status;
            s.mac = c.before.mac;
            s.clip = c.before.clip;
            s.tpc = c.h.startPC;
            ObPcsx2Vu::SetState(s);
            sink = packets;
            const auto t0 = Clock::now();
            const ObPcsx2Vu::RunResult rr = ObPcsx2Vu::Run(c.h.startPC, c.h.top, c.h.itop, c.h.fbrst, generation);
            lastNs = nsSince(t0);
            sink = nullptr;
            return rr;
        }

        void outputs(Outputs &o, const Packets &packets)
        {
            ObPcsx2Vu::State s{};
            ObPcsx2Vu::GetState(s);
            o.data.assign(data.get(), data.get() + 0x4000);
            std::memcpy(o.vf, s.vf, sizeof(o.vf));
            std::memcpy(o.vi, s.vi, sizeof(o.vi));
            std::memcpy(o.acc, s.acc, sizeof(o.acc));
            o.q = s.q;
            o.p = s.p;
            o.i = s.i;
            o.r = s.r;
            o.status = s.status;
            o.mac = s.mac;
            o.clip = s.clip;
            o.packets = packets;
        }
    };

    double median(std::vector<double> v)
    {
        if (v.empty())
            return 0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }

    // summary.txt key line: "kind pc code_hash first_gen runs cycles files".
    struct KeyWeight
    {
        uint64_t runs = 0, cycles = 0;
    };
    std::map<std::string, KeyWeight> loadWeights(const fs::path &dir, uint64_t &totalCycles)
    {
        std::map<std::string, KeyWeight> w;
        totalCycles = 0;
        std::ifstream in(dir / "summary.txt");
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ss(line);
            std::string kind, pc, hash;
            uint64_t gen, runs, cycles;
            if (!(ss >> kind >> pc >> hash >> gen >> runs >> cycles))
                continue;
            if (kind != "mscal" && kind != "mscnt")
                continue;
            w[kind + " " + pc + " " + hash] = {runs, cycles};
            totalCycles += cycles;
        }
        return w;
    }

    std::string keyOf(const Capture &c)
    {
        char b[96];
        std::snprintf(b, sizeof(b), "%s 0x%04x %016llx", c.h.kind == cap::Mscal ? "mscal" : "mscnt", c.h.startPC,
                      static_cast<unsigned long long>(c.h.codeHash));
        return b;
    }
}

namespace
{
    // ---- synthetic programs (--synthetic): small hand-written cases, interpreter vs microVU only ----
    uint32_t lowerSpecial(uint8_t op, uint8_t is, uint8_t it = 0, uint8_t dest = 0)
    {
        // Same encoding as ps2xTest makeVuLowerSpecial (ps2_vu1_tests.cpp).
        return (0x40u << 25) | (uint32_t(dest & 0xF) << 21) | (uint32_t(it & 0x1F) << 16) | (uint32_t(is & 0x1F) << 11) |
               (uint32_t(op & 0x7C) << 4) | uint32_t(op & 3) | 0x3Cu;
    }
    uint32_t upper(uint8_t op, uint8_t dest, uint8_t ft, uint8_t fs, uint8_t fd)
    {
        return (uint32_t(dest & 0xF) << 21) | (uint32_t(ft & 0x1F) << 16) | (uint32_t(fs & 0x1F) << 11) | (uint32_t(fd & 0x1F) << 6) |
               uint32_t(op & 0x3F);
    }
    constexpr uint32_t kLowerNop = 0x8000033Cu, kUpperNop = 0x000002FFu, kEbit = 0x40000000u;

    Capture synthetic(const char *name, const std::vector<std::pair<uint32_t, uint32_t>> &pairs, const float vf1[4])
    {
        Capture c;
        c.path = name;
        std::memcpy(c.h.magic, cap::kMagic, 8);
        c.h.version = cap::kVersion;
        c.h.kind = cap::Mscal;
        c.h.maxCycles = 65536;
        c.code.assign(0x4000, 0);
        c.dataBefore.assign(0x4000, 0);
        for (size_t k = 0; k < pairs.size(); ++k)
        {
            std::memcpy(c.code.data() + k * 8, &pairs[k].first, 4);
            std::memcpy(c.code.data() + k * 8 + 4, &pairs[k].second, 4);
        }
        c.before = cap::StateRecord{};
        c.before.vf[0][3] = cap::floatBits(1.0f);
        for (int l = 0; l < 4; ++l)
            c.before.vf[1][l] = cap::floatBits(vf1[l]);
        c.before.q = cap::floatBits(1.0f);
        c.before.r = 0x3F800000u;
        return c;
    }

    std::vector<Capture> syntheticSet()
    {
        const float v[4] = {0.5f, 3.0f, 2.0f, 0.0f};
        std::vector<Capture> s;
        // P written by ESIN and never read, Q by DIV, both still in flight at the E bit.
        s.push_back(synthetic("syn-esin-div-pending-at-ebit", {{lowerSpecial(0x79, 1, 0, 0), kUpperNop},          // ESIN P, vf1.x
                                                                {lowerSpecial(0x38, 1, 1, (2 << 2) | 1), kUpperNop | kEbit}, // DIV Q, vf1.y/vf1.z
                                                                {kLowerNop, kUpperNop}},
                              v));
        // P read back through WAITP + MFP before the E bit.
        s.push_back(synthetic("syn-esin-waitp-mfp", {{lowerSpecial(0x79, 1, 0, 0), kUpperNop},
                                                      {lowerSpecial(0x7B, 0), kUpperNop},         // WAITP
                                                      {lowerSpecial(0x64, 0, 2, 0x8), kUpperNop | kEbit}, // MFP.x vf2
                                                      {kLowerNop, kUpperNop}},
                              v));
        // FMAC flags set by the last instructions and read by nothing: SUB.xyzw vf3 = vf1 - vf1 (Z on all lanes).
        s.push_back(synthetic("syn-sub-zero-flags-unread", {{kLowerNop, upper(0x2C, 0xF, 1, 1, 3) | kEbit}, {kLowerNop, kUpperNop}}, v));
        // Same, but the flags are read: IADDIU vi2 = 0x7FF, then FMAND vi1 = MAC & vi2 after 4 cycles.
        s.push_back(synthetic("syn-sub-zero-flags-read", {{(0x08u << 25) | (2u << 16) | 0x7FFu, upper(0x2C, 0xF, 1, 1, 3)},
                                                           {kLowerNop, kUpperNop},
                                                           {kLowerNop, kUpperNop},
                                                           {kLowerNop, kUpperNop},
                                                           {(0x1Au << 25) | (1u << 16) | (2u << 11), kUpperNop | kEbit}, // FMAND vi1, vi2
                                                           {kLowerNop, kUpperNop}},
                              v));
        return s;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <capture dir> [--repeat N] [--verbose] [--no-flag-hack]\n", argv[0]);
        return 2;
    }
    const fs::path dir = argv[1];
    int repeat = 20;
    bool verbose = false;
    for (int k = 2; k < argc; ++k)
    {
        if (!std::strcmp(argv[k], "--repeat") && k + 1 < argc)
            repeat = std::max(1, std::atoi(argv[++k]));
        else if (!std::strcmp(argv[k], "--verbose"))
            verbose = true;
        else if (!std::strcmp(argv[k], "--no-flag-hack"))
            ObPcsx2Vu::SetFlagHack(false);
    }

    const bool synth = dir == "--synthetic";
    std::vector<fs::path> files;
    std::vector<Capture> synthCaps;
    std::error_code ec;
    if (synth)
    {
        synthCaps = syntheticSet();
        for (const auto &c : synthCaps)
            files.push_back(c.path);
    }
    else
        for (const auto &e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".cap")
                files.push_back(e.path());
    std::sort(files.begin(), files.end());
    if (files.empty())
    {
        std::fprintf(stderr, "no .cap files in %s\n", dir.string().c_str());
        return 2;
    }

    auto interp = std::make_unique<Interp>();
    if (!interp->init())
    {
        std::fprintf(stderr, "PS2Memory init failed\n");
        return 1;
    }
    auto mvu = std::make_unique<Mvu>();
    std::string err;
    if (!mvu->init(err))
    {
        std::fprintf(stderr, "FATAL: %s\n", err.c_str());
        return 1;
    }
    std::printf("%s\n", ObPcsx2Vu::Describe());
    std::printf("captures: %zu, repeat %d\n", files.size(), repeat);
    std::printf("%-26s %-5s %-6s %8s %9s %9s %7s %7s  %s\n", "file", "kind", "pc", "icycles", "interp/c", "mvu/c", "speedup",
                "replay", "mvu vs interp");

    struct KeyTime
    {
        double interpNsPerCycle = 0, mvuNsPerCycle = 0;
        int n = 0;
    };
    std::map<std::string, KeyTime> perKey;
    int loaded = 0, replayOk = 0, match = 0, skipped = 0, matchExceptKnown = 0;
    std::map<std::string, int> firstFieldKinds, anyFieldKinds, knownPattern;
    double sumInterpNs = 0, sumMvuNs = 0, sumCycles = 0;
    uint64_t sumMvuCycles = 0;

    for (const auto &f : files)
    {
        Capture c;
        if (synth)
            c = *std::find_if(synthCaps.begin(), synthCaps.end(), [&](const Capture &x) { return x.path == f; });
        else if (!loadCapture(f, c, err))
        {
            std::printf("%-26s load error: %s\n", f.filename().string().c_str(), err.c_str());
            continue;
        }
        ++loaded;
        if (c.h.flags & cap::FlagBudgetHit)
        {
            ++skipped;
            std::printf("%-26s skipped: budget hit (pending pipeline state cannot be translated)\n", f.filename().string().c_str());
            continue;
        }

        // (0) replay check: the interpreter on the recorded inputs must give the recorded outputs.
        Packets ip;
        const uint64_t icycles = interp->run(c, &ip);
        Outputs iOut, recOut;
        interp->outputs(iOut, ip);
        fromRecord(c.after, c.dataAfter, c.packets, recOut);
        int nRep = 0;
        const std::string rep = synth ? std::string() : diff(recOut, iOut, "capture", "replay", nRep);
        if (synth)
            c.h.cyclesRun = icycles; // no recorded outputs: the replay check does not apply
        if (rep.empty() && icycles == c.h.cyclesRun)
            ++replayOk;

        // (b) microVU, first run (compiles) then compare.
        Packets mp;
        const ObPcsx2Vu::RunResult rr = mvu->run(c, &mp);
        const double mvuFirstNs = mvu->lastNs;
        Outputs mOut;
        mvu->outputs(mOut, mp);
        int nDiff = 0;
        std::vector<std::string> allDiffs;
        const std::string d = diff(iOut, mOut, "interp", "mvu", nDiff, &allDiffs);
        if (d.empty() && rr.ended)
            ++match;
        else
            firstFieldKinds[d.empty() ? "not ended" : d.substr(0, d.find_first_of(" :."))]++;
        {
            std::map<std::string, int> kinds; // count each field kind once per capture
            for (const auto &x : allDiffs)
                kinds[x.substr(0, x.find_first_of(" :."))] = 1;
            for (const auto &[k, one] : kinds)
                anyFieldKinds[k] += one;
            // Known patterns: microVU left STATUS/MAC at their input value (flags no instruction reads are not
            // computed, microVU_Flags.inl:113-135, noFlagOpts = false microVU_Misc.h:265); P equals Q.
            if (kinds.count("STATUS") && mOut.status == c.before.status)
                ++knownPattern["STATUS = input value (mvu)"];
            if (kinds.count("MAC") && mOut.mac == c.before.mac)
                ++knownPattern["MAC = input value (mvu)"];
            if (kinds.count("P") && mOut.p == iOut.q)
                ++knownPattern["P = final Q (mvu)"];
            bool onlyKnown = !kinds.empty();
            for (const auto &[k, one] : kinds)
                if (!((k == "STATUS" && mOut.status == c.before.status) || (k == "MAC" && mOut.mac == c.before.mac) ||
                      (k == "P" && mOut.p == iOut.q)))
                    onlyKnown = false;
            if (onlyKnown && rr.ended)
                ++matchExceptKnown;
            else if (!(d.empty() && rr.ended))
                std::printf("    UNEXPLAINED %s: mvu STATUS %03x MAC %04x, input STATUS %03x MAC %04x\n", f.filename().string().c_str(),
                            mOut.status, mOut.mac, c.before.status, c.before.mac);
        }
        if (synth)
            std::printf("    %s: interp P %08x Q %08x STATUS %03x MAC %04x VF2.x %08x VI1 %04x | mvu P %08x Q %08x STATUS %03x MAC %04x VF2.x %08x VI1 %04x\n",
                        f.string().c_str(), iOut.p, iOut.q, iOut.status, iOut.mac, iOut.vf[2][0], iOut.vi[1], mOut.p, mOut.q,
                        mOut.status, mOut.mac, mOut.vf[2][0], mOut.vi[1]);

        // Timing: warm runs of each backend on identical inputs.
        std::vector<double> ti, tm;
        for (int k = 0; k < repeat; ++k)
        {
            interp->run(c, nullptr);
            ti.push_back(interp->lastNs);
            mvu->run(c, nullptr);
            tm.push_back(mvu->lastNs);
        }
        const double mi = median(ti), mm = median(tm);
        const double cyc = static_cast<double>(std::max<uint64_t>(1, icycles));
        KeyTime &kt = perKey[keyOf(c)];
        kt.interpNsPerCycle += mi / cyc;
        kt.mvuNsPerCycle += mm / cyc;
        ++kt.n;
        sumInterpNs += mi;
        sumMvuNs += mm;
        sumCycles += cyc;
        sumMvuCycles += rr.mvuCycles;

        std::printf("%-26s %-5s 0x%04x %8llu %9.2f %9.2f %6.1fx %7s  %s%s\n", f.filename().string().c_str(),
                    c.h.kind == cap::Mscal ? "mscal" : "mscnt", c.h.startPC, static_cast<unsigned long long>(icycles),
                    mi / cyc, mm / cyc, mi / std::max(1.0, mm), (rep.empty() && icycles == c.h.cyclesRun) ? "ok" : "DIFF",
                    d.empty() ? (rr.ended ? "match" : "NOT ENDED") : ("MISMATCH " + d).c_str(),
                    nDiff > 1 ? (" (+" + std::to_string(nDiff - 1) + " more)").c_str() : "");
        if (!rep.empty() && verbose)
            std::printf("    replay: %s (%d fields)\n", rep.c_str(), nRep);
        if (verbose)
            for (size_t k = 0; k < allDiffs.size() && k < 12; ++k)
                std::printf("    %s\n", allDiffs[k].c_str());
        if (verbose)
            std::printf("    mvu: first run %.0f ns (includes compile), mvu cycles %u, interp cycles %llu\n", mvuFirstNs,
                        rr.mvuCycles, static_cast<unsigned long long>(icycles));
    }

    std::printf("\nloaded %d, skipped %d, replay reproduces capture %d/%d, mvu == interp bit for bit %d/%d\n", loaded, skipped,
                replayOk, loaded - skipped, match, loaded - skipped);
    for (const auto &[k, n] : firstFieldKinds)
        std::printf("  first differing field %-10s: %d captures\n", k.c_str(), n);
    for (const auto &[k, n] : anyFieldKinds)
        std::printf("  field %-10s differs in %d captures\n", k.c_str(), n);
    for (const auto &[k, n] : knownPattern)
        std::printf("  pattern %-28s: %d captures\n", k.c_str(), n);
    std::printf("mismatching only in those patterns: %d; anything else: %d\n", matchExceptKnown,
                loaded - skipped - match - matchExceptKnown);
    std::printf("timing (median of %d warm runs, per interpreter cycle): interp %.2f ns, mvu %.2f ns, %.1fx\n", repeat,
                sumInterpNs / std::max(1.0, sumCycles), sumMvuNs / std::max(1.0, sumCycles),
                sumInterpNs / std::max(1.0, sumMvuNs));
    std::printf("mvu counted %llu cycles vs interpreter %.0f over the same runs\n", static_cast<unsigned long long>(sumMvuCycles), sumCycles);

    // Weighted by the boot's own cycle counts per program (summary.txt), the share of VU1 time each backend needs.
    uint64_t total = 0;
    const auto weights = loadWeights(dir, total);
    if (total)
    {
        double wi = 0, wm = 0, covered = 0;
        for (const auto &[k, kt] : perKey)
        {
            auto it = weights.find(k);
            if (it == weights.end())
                continue;
            const double cyc = static_cast<double>(it->second.cycles);
            wi += cyc * kt.interpNsPerCycle / kt.n;
            wm += cyc * kt.mvuNsPerCycle / kt.n;
            covered += cyc;
        }
        std::printf("weighted by in-game cycles (summary.txt, %.1f%% of %llu cycles covered): interp %.2f ns/cycle, mvu %.2f ns/cycle, %.1fx\n",
                    100.0 * covered / total, static_cast<unsigned long long>(total), wi / std::max(1.0, covered),
                    wm / std::max(1.0, covered), wi / std::max(1.0, wm));
    }
    ObPcsx2Vu::Shutdown();
    return match == loaded - skipped ? 0 : 1;
}
