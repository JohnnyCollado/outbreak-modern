#include "MiniTest.h"
#include "pad_control.h"
#include "pad_pipe.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    constexpr uint16_t kUp = 0x0010u, kCircle = 0x2000u, kCross = 0x4000u, kStart = 0x0008u;

    struct Rig
    {
        std::vector<uint8_t> ram = std::vector<uint8_t>(0x100, 0u);
        uint64_t tick = 100;
        std::vector<std::string> written;
        std::unique_ptr<ObPadControl> pad;

        Rig()
        {
            pad = std::make_unique<ObPadControl>(
                [this](uint32_t addr, uint32_t width, uint32_t &out)
                {
                    if (addr + width > ram.size())
                        return false;
                    out = 0;
                    std::memcpy(&out, ram.data() + addr, width);
                    return true;
                },
                [this]() { return tick; }, "C:/shots",
                [this](const std::string &path, const uint8_t *, uint32_t, uint32_t)
                {
                    written.push_back(path);
                    return true;
                });
        }
        // One guest vsync: the engine's order (onVsync, then wantsFrame). Returns wantsFrame.
        bool vsync()
        {
            ++tick;
            pad->onVsync(tick);
            return pad->wantsFrame();
        }
        bool held(uint16_t mask) const { return (pad->buttons() & mask) == 0u; }
    };

    std::string now(ObPadControl &pad, const std::string &line)
    {
        std::string reply;
        const ObPadControl::Submit s = pad.submit(line, reply);
        return s == ObPadControl::Submit::Done ? reply : std::string("<pending>");
    }

    bool starts(const std::string &s, const std::string &prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
}

void register_pad_control_tests()
{
    MiniTest::Case("PadControl", [](TestCase &tc)
    {
        tc.Run("command parsing: replies, errors, immediate commands", [](TestCase &t)
        {
            Rig r;
            r.ram[0x10] = 0x09;
            r.ram[0x20] = 0x10; // pointer 0x10 at 0x20
            t.Equals(now(*r.pad, "tick"), std::string("ok tick=100"), "tick");
            t.Equals(now(*r.pad, "peek 0x10 1"), std::string("ok peek 0x00000010 1 = 0x09"), "peek u8");
            t.Equals(now(*r.pad, "peek *0x20+0 1"), std::string("ok peek 0x00000010 1 = 0x09"), "peek through a pointer");
            t.Equals(now(*r.pad, "peek 0x10 4"), std::string("ok peek 0x00000010 4 = 0x00000009"), "peek u32");
            t.IsTrue(starts(now(*r.pad, "peek 0x10 3"), "err usage: peek"), "width 3 refused");
            t.IsTrue(starts(now(*r.pad, "peek 0x1000 4"), "err cannot read"), "out of range");
            t.IsTrue(starts(now(*r.pad, "jump"), "err unknown command 'jump'"), "unknown command");
            t.IsTrue(starts(now(*r.pad, ""), "err empty"), "empty line");
            t.IsTrue(starts(now(*r.pad, "press bogus"), "err unknown button"), "unknown button");
            t.IsTrue(starts(now(*r.pad, "press up 0"), "err press needs"), "press 0 frames");
            t.IsTrue(starts(now(*r.pad, "hold up"), "err usage: hold"), "hold needs frames");
            t.IsTrue(starts(now(*r.pad, "stick middle 0 0 5"), "err stick must"), "stick side");
            t.IsTrue(starts(now(*r.pad, "stick left 2 0 5"), "err x and y"), "stick range");
            t.IsTrue(starts(now(*r.pad, "shot ../x"), "err usage: shot"), "shot name sanitised");
            t.IsTrue(starts(now(*r.pad, "wait x"), "err usage: wait"), "wait frames");
            uint16_t m = 0;
            t.IsTrue(ObPadControl::parseButtons("CROSS+start", m), "combo, any case");
            t.Equals(m, static_cast<uint16_t>(kCross | kStart), "combo mask");
            t.Equals(now(*r.pad, "press circle"), std::string("<pending>"), "press is pending");
            t.Equals(now(*r.pad, "tick"), std::string("err busy: one command at a time"), "one command at a time");
            t.IsFalse(r.pad->quitRequested(), "no quit yet");
            for (int i = 0; i < 10; ++i)
                r.vsync();
            std::string reply;
            t.IsTrue(r.pad->pollReply(reply), "press done");
            t.Equals(now(*r.pad, "quit"), std::string("ok quit"), "quit");
            t.IsTrue(r.pad->quitRequested(), "quit requested");
        });

        tc.Run("press/hold/release/stick/wait timing on fake vsyncs; idle vsyncs change nothing", [](TestCase &t)
        {
            Rig r;
            t.Equals(r.pad->buttons(), static_cast<uint16_t>(0xFFFFu), "nothing held");
            t.IsFalse(r.vsync(), "idle vsync: no frame wanted");
            std::string reply;

            // press up 3: down from the next vsync for 3 vsyncs, the reply at the release.
            t.Equals(now(*r.pad, "press up 3"), std::string("<pending>"), "queued");
            t.IsFalse(r.held(kUp), "not before the next vsync");
            int downFrames = 0;
            uint64_t replyTick = 0;
            for (int i = 0; i < 6; ++i)
            {
                r.vsync();
                if (r.pad->pollReply(reply))
                    replyTick = r.tick;
                if (r.held(kUp))
                    ++downFrames; // held through the frame after this vsync
            }
            t.Equals(downFrames, 3, "up held for exactly 3 frames");
            t.Equals(replyTick, static_cast<uint64_t>(105u), "reply at the release vsync (pressed at 102, + 3)");
            t.Equals(reply, std::string("ok press up 3 tick=105"), "reply text");

            // press default = 6 frames.
            now(*r.pad, "press cross");
            downFrames = 0;
            for (int i = 0; i < 10; ++i)
            {
                r.vsync();
                downFrames += r.held(kCross) ? 1 : 0;
            }
            t.Equals(downFrames, 6, "default press is 6 frames");
            t.IsTrue(r.pad->pollReply(reply), "replied");

            // hold circle 4: replies after 4 vsyncs and stays held.
            now(*r.pad, "hold circle 4");
            int vs = 0;
            while (!r.pad->pollReply(reply) && vs < 20)
            {
                r.vsync();
                ++vs;
            }
            t.Equals(vs, 5, "start vsync + 4");
            for (int i = 0; i < 5; ++i)
                r.vsync();
            t.IsTrue(r.held(kCircle), "still held after the reply");
            t.IsTrue(starts(now(*r.pad, "release circle"), "ok release"), "release");
            t.IsFalse(r.held(kCircle), "released at once");

            // stick left 0 -1 for 5 frames, then centred.
            now(*r.pad, "stick left 0 -1 5");
            uint8_t s[4];
            r.vsync();
            r.pad->sticks(s);
            t.Equals(s[2], static_cast<uint8_t>(128u), "lx centre (128 + 0 * 127)");
            t.Equals(s[3], static_cast<uint8_t>(1u), "ly up (128 - 127)");
            t.Equals(s[0], static_cast<uint8_t>(0x80u), "rx untouched");
            for (int i = 0; i < 5; ++i)
                r.vsync();
            t.IsTrue(r.pad->pollReply(reply), "stick replied");
            r.pad->sticks(s);
            t.Equals(s[3], static_cast<uint8_t>(0x80u), "centred after 5 frames");

            // stick frames 0 stays until release all.
            now(*r.pad, "stick right 1 0 0");
            r.vsync();
            t.IsTrue(r.pad->pollReply(reply), "stick 0 replies at once");
            r.pad->sticks(s);
            t.Equals(s[0], static_cast<uint8_t>(255u), "rx right");
            now(*r.pad, "release all");
            r.pad->sticks(s);
            t.Equals(s[0], static_cast<uint8_t>(0x80u), "release all centres");

            // wait 7.
            now(*r.pad, "wait 7");
            vs = 0;
            while (!r.pad->pollReply(reply) && vs < 20)
            {
                r.vsync();
                ++vs;
            }
            t.Equals(vs, 8, "start vsync + 7");
            t.IsTrue(starts(reply, "ok wait 7 tick="), "wait reply");
            t.Equals(r.pad->buttons(), static_cast<uint16_t>(0xFFFFu), "all released at the end");
        });

        tc.Run("shot asks for exactly one frame and writes it once", [](TestCase &t)
        {
            Rig r;
            std::string reply;
            uint8_t rgba[16] = {};
            for (int i = 0; i < 3; ++i)
                t.IsFalse(r.vsync(), "no shot: no frame wanted");
            r.pad->onFrame(r.tick, rgba, 2, 2);
            t.Equals(r.written.size(), static_cast<size_t>(0), "a frame without a shot is not written");

            t.Equals(now(*r.pad, "shot title"), std::string("<pending>"), "queued");
            int demands = 0;
            for (int i = 0; i < 50; ++i)
                demands += r.vsync() ? 1 : 0;
            t.Equals(demands, 1, "exactly one frame demanded over 50 vsyncs");
            r.pad->onFrame(r.tick - 60, rgba, 2, 2);
            t.Equals(r.written.size(), static_cast<size_t>(0), "a frame older than the demand is refused");
            r.pad->onFrame(r.tick, rgba, 2, 2);
            r.pad->onFrame(r.tick, rgba, 2, 2);
            t.Equals(r.written.size(), static_cast<size_t>(1), "written once");
            t.IsTrue(r.written.size() == 1 && r.written[0].find("title.png") != std::string::npos, "name.png");
            t.IsTrue(r.pad->pollReply(reply), "replied");
            t.IsTrue(starts(reply, "ok shot ") && reply.find("title.png tick=") != std::string::npos, "reply = path");
            for (int i = 0; i < 300; ++i)
                demands += r.vsync() ? 1 : 0;
            t.Equals(demands, 1, "no more demands after the shot");

            // Default name; a demand that gets no frame is repeated after 120 vsyncs.
            now(*r.pad, "shot");
            demands = 0;
            for (int i = 0; i < 121; ++i)
                demands += r.vsync() ? 1 : 0;
            t.Equals(demands, 2, "re-demanded once after 120 vsyncs without a frame");
            r.pad->onFrame(r.tick, rgba, 2, 2);
            t.IsTrue(r.pad->pollReply(reply), "replied");
            t.IsTrue(reply.find("shot-") != std::string::npos, "default name shot-<tick>");
            t.Equals(r.pad->framesWritten(), 2u, "two shots, two frames");
        });

#if defined(_WIN32)
        tc.Run("named pipe: one line in, one reply out; one client at a time", [](TestCase &t)
        {
            static Rig r; // the server thread lives until the process exits
            const std::string name = "ob_pad_test_" + std::to_string(GetCurrentProcessId());
            std::string error;
            t.IsTrue(obStartPadPipe(name, *r.pad, error), "server starts: " + error);
            std::string error2;
            t.IsFalse(obStartPadPipe(name, *r.pad, error2), "a second server on the same name is refused");
            const std::string full = "\\\\.\\pipe\\" + name;
            t.IsTrue(WaitNamedPipeA(full.c_str(), 2000) != 0, "pipe ready");
            HANDLE c = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            t.IsTrue(c != INVALID_HANDLE_VALUE, "client connects");
            HANDLE c2 = CreateFileA(full.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            t.IsTrue(c2 == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY, "second client: pipe busy");
            const char cmd[] = "tick\npeek 0x0 1\n";
            DWORD n = 0;
            t.IsTrue(WriteFile(c, cmd, sizeof(cmd) - 1, &n, nullptr) != 0, "write");
            std::string got;
            char buf[256];
            while (std::count(got.begin(), got.end(), '\n') < 2)
            {
                if (!ReadFile(c, buf, sizeof(buf), &n, nullptr) || n == 0)
                    break;
                got.append(buf, n);
            }
            t.Equals(got, std::string("ok tick=100\nok peek 0x00000000 1 = 0x00\n"), "two replies");
            CloseHandle(c);
        });
#endif
    });
}
