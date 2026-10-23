// ob_diag plugin tests: frame-time log, PC sampler, VU1 capture recorder, and the plugin entry point driven
// through PS2DiagHooks the way the engine calls it.
#include "MiniTest.h"
#include "frame_time.h"
#include "pc_sampler.h"
#include "runtime/ps2_diag_hooks.h"
#include "runtime/ps2_vu1.h"
#include "vu1_recorder.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" PS2DiagHooks *ps2_diag_create(const PS2DiagHost *host, int *exitCode);

namespace
{
    std::filesystem::path tempPath(const std::string &stem)
    {
        return std::filesystem::path(OB_DIAG_TEST_DIR) /
               (stem + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }

    std::vector<std::string> readLines(const std::filesystem::path &path)
    {
        std::vector<std::string> lines;
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);)
            if (!line.empty())
                lines.push_back(line);
        return lines;
    }

    void setEnv(const char *name, const std::string &value)
    {
#ifdef _MSC_VER
        _putenv_s(name, value.c_str());
#else
        if (value.empty())
            unsetenv(name);
        else
            setenv(name, value.c_str(), 1);
#endif
    }

    void clearDiagEnv()
    {
        for (const char *name : {"PS2X_INPUT_SCRIPT", "PS2X_FRAME_DUMP_DIR", "PS2X_FRAME_TIME", "PS2X_PC_SAMPLE",
                                 "PS2X_VU1_CAPTURE"})
            setEnv(name, "");
    }

    // A fake engine: 4 KB of guest RAM and a tick.
    struct FakeEngine
    {
        std::vector<uint8_t> ram = std::vector<uint8_t>(0x1000, 0u);
        std::atomic<uint64_t> tick{0};

        static bool read(void *user, uint32_t addr, void *dst, uint32_t bytes)
        {
            FakeEngine &e = *static_cast<FakeEngine *>(user);
            const uint32_t physical = addr & 0x01FFFFFFu;
            if (physical + bytes > e.ram.size())
                return false;
            std::memcpy(dst, e.ram.data() + physical, bytes);
            return true;
        }
        static uint64_t vsyncTick(void *user) { return static_cast<FakeEngine *>(user)->tick.load(); }

        PS2DiagHost host()
        {
            return PS2DiagHost{PS2_DIAG_API_VERSION, static_cast<uint32_t>(sizeof(VU1State)), this, &read, &vsyncTick};
        }
    };
}

void register_diag_tests()
{
    MiniTest::Case("FrameTime", [](TestCase &tc)
    {
        tc.Run("one tick,wall_us,busy_us line per vsync; idle wait counts as pacing, not busy", [](TestCase &t)
        {
            const std::filesystem::path path = tempPath("frame-time");
            std::filesystem::create_directories(path.parent_path());
            {
                ObFrameTimeLog log;
                t.IsTrue(log.open(path.string()), "opens");
                log.onVsync(1u, 1'000'000'000ull, 0u);
                log.onVsync(2u, 1'016'700'000ull, 10'000'000ull);  // 16.7 ms, 10 ms of it idle
                log.onVsync(3u, 1'050'000'000ull, 10'000'000ull);  // 33.3 ms, none idle
                log.onVsync(4u, 1'051'000'000ull, 30'000'000ull);  // idle > wall (clock skew): busy 0
            }
            const std::vector<std::string> lines = readLines(path);
            t.IsTrue(lines == std::vector<std::string>{"tick,wall_us,busy_us", "1,0,0", "2,16700,6700", "3,33300,33300",
                                                       "4,1000,0"},
                     "header, first line 0,0, then wall and wall minus idle");
            std::error_code ec;
            std::filesystem::remove(path, ec);
        });
    });

    MiniTest::Case("PcSampler", [](TestCase &tc)
    {
        tc.Run("writes a pc histogram whose top entry is the pc the guest sits on, with its sections", [](TestCase &t)
        {
            const std::filesystem::path path = tempPath("pc-sample").replace_extension(".csv");
            std::filesystem::create_directories(path.parent_path());
            FakeEngine engine;
            engine.tick = 130u; // bucket 128
            uint32_t pc = 0x00171010u;
            {
                ObPcSampler sampler(path.string(), &FakeEngine::vsyncTick, &engine);
                sampler.guestEnter(&pc, 7);
                sampler.sectionEnter(static_cast<uint32_t>(PS2DiagSection::Iop));
                std::this_thread::sleep_for(std::chrono::milliseconds(600)); // ~38 samples even at the 15.6 ms default timer
                sampler.sectionLeave(static_cast<uint32_t>(PS2DiagSection::Iop));
                sampler.guestLeave();
                sampler.stop();
            }
            const std::vector<std::string> lines = readLines(path);
            t.IsTrue(!lines.empty() && lines[0] == "tick,tid,pc,count,vu1,iop,gs,gsvsync", "header");
            uint64_t bestCount = 0u, bestIop = 0u, bestVu1 = 1u;
            std::string bestKey;
            for (size_t i = 1; i < lines.size(); ++i)
            {
                std::istringstream ss(lines[i]);
                std::string tick, tid, pcText, count, vu1, iop;
                std::getline(ss, tick, ',');
                std::getline(ss, tid, ',');
                std::getline(ss, pcText, ',');
                std::getline(ss, count, ',');
                std::getline(ss, vu1, ',');
                std::getline(ss, iop, ',');
                const uint64_t n = std::strtoull(count.c_str(), nullptr, 10);
                if (n > bestCount)
                {
                    bestCount = n;
                    bestKey = tick + "," + tid + "," + pcText;
                    bestIop = std::strtoull(iop.c_str(), nullptr, 10);
                    bestVu1 = std::strtoull(vu1.c_str(), nullptr, 10);
                }
            }
            t.Equals(bestKey, std::string("128,7,0x171010"), "top entry = bucket 128, thread 7, the guest's pc");
            t.IsTrue(bestCount >= 20u, "at least 20 samples in 600 ms (got " + std::to_string(bestCount) + ")");
            t.Equals(bestIop, bestCount, "every sample inside the IOP section counts it");
            t.Equals(bestVu1, 0ull, "no VU1 section");
            std::error_code ec;
            std::filesystem::remove(path, ec);
        });
    });

    MiniTest::Case("Vu1Capture", [](TestCase &tc)
    {
        tc.Run("writes inputs, outputs and the PATH1 packet of a run, once per distinct input", [](TestCase &t)
        {
            namespace cap = ps2x::vu1capture;
            const std::filesystem::path dir = tempPath("vu1-capture");
            const std::string spec = dir.string() + ",8,2";
            std::vector<uint8_t> code(cap::kCodeBytes, 0u), data(cap::kDataBytes, 0u), packet(32u, 0u);
            code[0x40] = 0x6C;
            for (uint32_t i = 0; i < 32u; ++i)
                packet[i] = data[i] = static_cast<uint8_t>(0xA0u + i);
            VU1State before{};
            VU1State after{};
            after.vi[1] = 5;
            after.cycles = 12u;
            {
                std::unique_ptr<cap::Recorder> rec = cap::Recorder::fromSpec(spec.c_str());
                t.IsTrue(rec != nullptr, "recorder from spec");
                if (!rec)
                    return;
                for (int run = 0; run < 2; ++run) // same inputs twice: the second is not written
                {
                    rec->begin(cap::Mscal, 0x40u, 0x10u, 0x20u, 0u, 0u, 65536u, 3u, code.data(), data.data(), before);
                    rec->path1(packet.data(), 32u);
                    rec->end(data.data(), after, 0u);
                }
                t.Equals(rec->runsSeen(), 2ull, "two runs seen");
                t.Equals(rec->filesWritten(), 1u, "one written");
            }
            std::vector<std::filesystem::path> files;
            std::error_code ec;
            for (const auto &e : std::filesystem::directory_iterator(dir, ec))
                if (e.path().extension() == ".cap")
                    files.push_back(e.path());
            t.Equals(files.size(), static_cast<size_t>(1u), "one capture file");
            t.IsTrue(std::filesystem::exists(dir / "summary.txt"), "summary.txt is written");
            if (files.size() != 1u)
                return;
            std::ifstream in(files[0], std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            cap::FileHeader h{};
            t.IsTrue(bytes.size() >= sizeof(h), "file holds a header");
            if (bytes.size() < sizeof(h))
                return;
            std::memcpy(&h, bytes.data(), sizeof(h));
            t.IsTrue(std::memcmp(h.magic, cap::kMagic, 8) == 0, "magic");
            t.Equals(h.version, cap::kVersion, "version");
            t.Equals(h.kind, static_cast<uint32_t>(cap::Mscal), "kind = MSCAL");
            t.Equals(h.startPC, 0x40u, "start pc");
            t.Equals(h.top, 0x10u, "TOP");
            t.Equals(h.itop, 0x20u, "ITOP");
            t.Equals(h.codeGeneration, 3ull, "code generation");
            t.Equals(h.cyclesRun, 12ull, "cycles = after - before");
            t.Equals(h.packetCount, 1u, "one PATH1 packet");
            const size_t base = sizeof(h) + h.codeBytes + 2u * h.dataBytes + 2u * h.stateBytes;
            t.Equals(bytes.size(), base + 4u + 32u, "file size = sections + one 32-byte packet");
            if (bytes.size() != base + 4u + 32u)
                return;
            cap::StateRecord rb{}, ra{};
            std::memcpy(&rb, bytes.data() + sizeof(h) + h.codeBytes + h.dataBytes, sizeof(rb));
            std::memcpy(&ra, bytes.data() + sizeof(h) + h.codeBytes + 2u * h.dataBytes + h.stateBytes, sizeof(ra));
            t.Equals(rb.vi[1], 0, "state before: vi1 = 0");
            t.Equals(ra.vi[1], 5, "state after: vi1 = 5");
            t.Equals(bytes[sizeof(h) + 0x40u], static_cast<uint8_t>(0x6C), "code section holds micro memory");
            t.IsTrue(std::memcmp(bytes.data() + base + 4u, packet.data(), 32u) == 0, "packet bytes");
            std::filesystem::remove_all(dir, ec);
        });
    });

    MiniTest::Case("ObDiagPlugin", [](TestCase &tc)
    {
        tc.Run("declines without a diagnostic variable, refuses a mismatched host", [](TestCase &t)
        {
            clearDiagEnv();
            FakeEngine engine;
            PS2DiagHost host = engine.host();
            int code = 99;
            t.IsTrue(ps2_diag_create(&host, &code) == nullptr, "nothing to do: no hooks");
            t.Equals(code, 0, "and no error");
            setEnv("PS2X_FRAME_DUMP_DIR", tempPath("dump").string());
            host.apiVersion = PS2_DIAG_API_VERSION + 1u;
            t.IsTrue(ps2_diag_create(&host, &code) == nullptr, "other API version: refused");
            t.Equals(code, 2, "exit 2");
            host = engine.host();
            host.vu1StateBytes += 4u;
            t.IsTrue(ps2_diag_create(&host, &code) == nullptr, "other VU1State size: refused");
            t.Equals(code, 2, "exit 2");
            clearDiagEnv();
        });

        tc.Run("a script with capture steps needs PS2X_FRAME_DUMP_DIR", [](TestCase &t)
        {
            clearDiagEnv();
            const std::filesystem::path script = tempPath("script-capture").replace_extension(".txt");
            std::filesystem::create_directories(script.parent_path());
            std::ofstream(script) << "capture title 1\nquit\n";
            setEnv("PS2X_INPUT_SCRIPT", script.string());
            FakeEngine engine;
            const PS2DiagHost host = engine.host();
            int code = 0;
            t.IsTrue(ps2_diag_create(&host, &code) == nullptr, "refused");
            t.Equals(code, 2, "exit 2");
            clearDiagEnv();
            std::error_code ec;
            std::filesystem::remove(script, ec);
        });

        tc.Run("a script drives the pad, waits on guest memory, wants frames only in capture steps", [](TestCase &t)
        {
            clearDiagEnv();
            const std::filesystem::path script = tempPath("script").replace_extension(".txt");
            const std::filesystem::path dumps = tempPath("script-dumps");
            std::filesystem::create_directories(script.parent_path());
            std::ofstream(script) << "wait mem 0x100 4 == 0x1234\npress circle 2\ncapture shot 1\nquit\n";
            setEnv("PS2X_INPUT_SCRIPT", script.string());
            setEnv("PS2X_FRAME_DUMP_DIR", dumps.string());
            FakeEngine engine;
            const PS2DiagHost host = engine.host();
            int code = 0;
            PS2DiagHooks *hooks = ps2_diag_create(&host, &code);
            clearDiagEnv();
            t.IsTrue(hooks != nullptr, "active");
            if (!hooks)
                return;
            uint16_t buttons = 0u;
            t.IsTrue(hooks->padButtons(0, buttons), "the script owns the pad");
            t.Equals(buttons, static_cast<uint16_t>(0xFFFFu), "nothing held while waiting");
            hooks->onVsync(1u, 0u);
            t.IsFalse(hooks->wantsFrame(1u), "no frame wanted while waiting");
            const uint32_t value = 0x1234u;
            std::memcpy(engine.ram.data() + 0x100, &value, 4u);
            hooks->onVsync(2u, 0u); // wait mem satisfied, press begins
            hooks->padButtons(0, buttons);
            t.Equals(buttons, static_cast<uint16_t>(0xFFFFu & ~0x2000u), "circle held (active low)");
            hooks->onVsync(3u, 0u);
            hooks->onVsync(4u, 0u); // released
            hooks->onVsync(5u, 0u); // capture begins
            t.IsTrue(hooks->wantsFrame(5u), "frame wanted in the capture step");
            t.Equals(hooks->onHostFrame(0.0), 0, "keeps running");
            const uint8_t px[4] = {1u, 2u, 3u, 255u};
            hooks->onFrame(4u, px, 1u, 1u); // older than the step: refused
            t.IsFalse(std::filesystem::exists(dumps / "shot-0.png"), "stale frame not written");
            hooks->onFrame(5u, px, 1u, 1u);
            t.IsTrue(std::filesystem::exists(dumps / "shot-0.png"), "shot-0.png written");
            t.IsFalse(hooks->wantsFrame(5u), "no more frames wanted");
            hooks->onVsync(6u, 0u); // capture done, quit
            t.Equals(hooks->onHostFrame(0.0), 1, "quit stops the run");
            t.Equals(hooks->onShutdown(), 0, "exit 0 after quit");
            std::error_code ec;
            std::filesystem::remove(script, ec);
            std::filesystem::remove_all(dumps, ec);
        });

        tc.Run("PS2X_FRAME_TIME alone: one line per onVsync, every frame wanted only with a dump dir", [](TestCase &t)
        {
            clearDiagEnv();
            const std::filesystem::path path = tempPath("plugin-frame-time").replace_extension(".csv");
            std::filesystem::create_directories(path.parent_path());
            setEnv("PS2X_FRAME_TIME", path.string());
            FakeEngine engine;
            const PS2DiagHost host = engine.host();
            int code = 0;
            PS2DiagHooks *hooks = ps2_diag_create(&host, &code);
            clearDiagEnv();
            t.IsTrue(hooks != nullptr, "active");
            if (!hooks)
                return;
            uint16_t buttons = 0u;
            t.IsFalse(hooks->padButtons(0, buttons), "no script: the host pad stays");
            t.IsFalse(hooks->wantsFrame(1u), "no dump dir: no frames");
            for (uint64_t tick = 1; tick <= 3; ++tick)
                hooks->onVsync(tick, 0u);
            hooks->onRunEnd();
            const std::vector<std::string> lines = readLines(path);
            t.Equals(lines.size(), static_cast<size_t>(4u), "header + 3 lines");
            t.IsTrue(lines.size() == 4u && lines[1] == "1,0,0" && lines[3].rfind("3,", 0) == 0, "ticks 1..3");
        });
    });
}
