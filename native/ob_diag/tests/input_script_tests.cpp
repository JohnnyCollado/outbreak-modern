#include "MiniTest.h"
#include "input_script.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

namespace
{
    struct FakeMemory
    {
        std::array<uint8_t, 0x1000> bytes{};

        void write32(uint32_t addr, uint32_t value) { std::memcpy(bytes.data() + addr, &value, 4u); }

        Ps2InputScript::ReadMem reader()
        {
            return [this](uint32_t addr, uint32_t width, uint32_t &out)
            {
                const uint32_t physical = addr & 0x01FFFFFFu;
                if (physical + width > bytes.size())
                    return false;
                out = 0u;
                std::memcpy(&out, bytes.data() + physical, width);
                return true;
            };
        }
    };

    bool parseOk(Ps2InputScript &script, const std::string &text)
    {
        std::string error;
        return script.parse(text, error);
    }
}

void register_input_script_tests()
{
    MiniTest::Case("InputScript", [](TestCase &tc)
                   {
        tc.Run("parses every step kind, comments and blank lines", [](TestCase &t)
               {
            Ps2InputScript script;
            std::string error;
            const bool ok = script.parse(
                "# title screen\n"
                "timeout 0x3c\n"
                "\n"
                "wait vsync 10   # settle\n"
                "wait mem 0x100 4 == 0x2a\n"
                "wait mem *0x200+0x10 2 != 7\n"
                "press start+cross 3\n"
                "capture title 2\n"
                "watch *0x200+4 1\n"
                "log hello world\n"
                "quit\n",
                error);
            t.IsTrue(ok, "parse succeeds: " + error);
            t.Equals(script.timeoutSeconds(), 60.0, "timeout header");
            const auto &steps = script.steps();
            t.Equals(steps.size(), static_cast<size_t>(8), "eight steps");
            if (steps.size() != 8u)
                return;
            t.IsTrue(steps[0].op == Ps2InputScript::Op::WaitVsync && steps[0].count == 10u, "wait vsync");
            t.Equals(steps[0].line, 4, "line numbers count comments and blanks");
            t.IsTrue(steps[1].op == Ps2InputScript::Op::WaitMem && steps[1].ref.addr == 0x100u && !steps[1].ref.deref &&
                         steps[1].width == 4u && steps[1].equal && steps[1].value == 0x2au,
                     "wait mem");
            t.IsTrue(steps[2].ref.deref && steps[2].ref.addr == 0x200u && steps[2].ref.offset == 0x10u &&
                         steps[2].width == 2u && !steps[2].equal && steps[2].value == 7u,
                     "wait mem deref");
            t.IsTrue(steps[3].op == Ps2InputScript::Op::Press && steps[3].mask == (0x0008u | 0x4000u) && steps[3].count == 3u,
                     "press");
            t.IsTrue(steps[4].op == Ps2InputScript::Op::Capture && steps[4].name == "title" && steps[4].count == 2u, "capture");
            t.IsTrue(steps[5].op == Ps2InputScript::Op::Watch && steps[5].ref.deref && steps[5].ref.offset == 4u &&
                         steps[5].width == 1u,
                     "watch");
            t.IsTrue(steps[6].op == Ps2InputScript::Op::Log && steps[6].name == "hello world", "log text");
            t.IsTrue(steps[7].op == Ps2InputScript::Op::Quit, "quit");
            t.IsTrue(script.hasCaptureSteps(), "has capture steps"); });

        tc.Run("default timeout is 600 seconds", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "quit\n"), "parse");
            t.Equals(script.timeoutSeconds(), 600.0, "default timeout");
            t.IsFalse(script.hasCaptureSteps(), "no capture steps"); });

        tc.Run("parse errors name the line", [](TestCase &t)
               {
            const char *bad[] = {
                "wait vsync\n",
                "\n# c\npress jump 2\n",
                "wait mem 0x100 3 == 1\n",
                "wait mem 0x100 1 < 1\n",
                "wait mem 0x100 1 == 0x100\n",
                "capture title 0\n",
                "frobnicate\n",
                "quit\ntimeout 5\n",
                "wait vsync 12abc\n",
            };
            const int lines[] = {1, 3, 1, 1, 1, 1, 1, 2, 1};
            for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i)
            {
                Ps2InputScript script;
                std::string error;
                t.IsFalse(script.parse(bad[i], error), std::string("rejects: ") + bad[i]);
                const std::string prefix = "line " + std::to_string(lines[i]) + ":";
                t.IsTrue(error.rfind(prefix, 0) == 0u, "error '" + error + "' starts with " + prefix);
            }
        });

        tc.Run("wait vsync and press hold length", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "wait vsync 2\npress circle+up 3\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();

            uint64_t tick = 100;
            script.onVSync(tick, read, log); // step 0 begins
            t.Equals(script.buttons(), static_cast<uint16_t>(0xFFFFu), "nothing held while waiting");
            script.onVSync(++tick, read, log);
            t.Equals(script.buttons(), static_cast<uint16_t>(0xFFFFu), "still waiting");
            script.onVSync(++tick, read, log); // 102: wait done, press begins
            const uint16_t held = static_cast<uint16_t>(0xFFFFu & ~(0x2000u | 0x0010u));
            t.Equals(script.buttons(), held, "press begins at tick 102");
            script.onVSync(++tick, read, log);
            script.onVSync(++tick, read, log);
            t.Equals(script.buttons(), held, "held through tick 104");
            script.onVSync(++tick, read, log); // 105: released
            t.Equals(script.buttons(), static_cast<uint16_t>(0xFFFFu), "released after 3 vsyncs");
            t.IsFalse(script.quitRequested(), "release vsync before next step");
            script.onVSync(++tick, read, log); // 106: quit
            t.IsTrue(script.quitRequested(), "quit after release vsync");
            const std::string text = log.str();
            t.IsTrue(text.find("[script] step 0 line 1 begin tick=100") != std::string::npos, "step 0 logged");
            t.IsTrue(text.find("[script] step 1 line 2 begin tick=102") != std::string::npos, "step 1 logged");
            t.IsTrue(text.find("[script] step 2 line 3 begin tick=106") != std::string::npos, "step 2 logged"); });

        tc.Run("stick holds the left stick, then centres it", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "stick left 0 -1 2\nstick right 1 0.5 1\nquit\n"), "parse");
            Ps2InputScript bad;
            std::string error;
            t.IsFalse(bad.parse("stick left 2 0 5\n", error), "x out of range refused");
            t.IsFalse(bad.parse("stick middle 0 0 5\n", error), "unknown stick refused");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();
            uint8_t st[4] = {};
            script.sticks(st);
            t.IsTrue(st[0] == 0x80 && st[1] == 0x80 && st[2] == 0x80 && st[3] == 0x80, "centred before any step");
            uint64_t tick = 10;
            script.onVSync(tick, read, log); // left stick up
            script.sticks(st);
            t.IsTrue(st[2] == 0x80 && st[3] == 0x00 && st[0] == 0x80 && st[1] == 0x80, "lx centre, ly up (0), right centred");
            script.onVSync(++tick, read, log);
            script.onVSync(++tick, read, log); // 12: centred
            script.sticks(st);
            t.IsTrue(st[2] == 0x80 && st[3] == 0x80, "centred after 2 vsyncs");
            script.onVSync(++tick, read, log); // 13: right stick
            script.sticks(st);
            t.IsTrue(st[0] == 0xFF && st[1] == 0xBF && st[2] == 0x80 && st[3] == 0x80, "rx right, ry half down, left centred"); });

        tc.Run("wait mem with deref and watch", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "watch 0x40 4\nwait mem *0x200+0x8 4 == 5\nlog done\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();

            script.onVSync(1, read, log);
            t.IsFalse(script.quitRequested(), "null pointer is not satisfied");
            mem.write32(0x200, 0x300);
            mem.write32(0x40, 9);
            script.onVSync(2, read, log);
            t.IsFalse(script.quitRequested(), "pointee not yet 5");
            t.IsTrue(log.str().find("[script] watch 0x40 0x0->0x9 tick=2") != std::string::npos, "watch logged change");
            mem.write32(0x308, 5);
            script.onVSync(3, read, log);
            t.IsTrue(script.quitRequested(), "deref satisfied, log and quit run in the same vsync");
            t.IsTrue(log.str().find("[script] log done") != std::string::npos, "log step printed"); });

        tc.Run("capture counts one frame per distinct tick", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "capture title 2\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();
            std::string fileName;
            std::string screen;
            uint32_t k = 99;

            t.IsFalse(script.takeCapture(5, fileName, screen, k), "no capture before the step begins");
            script.onVSync(10, read, log);
            t.IsTrue(script.takeCapture(10, fileName, screen, k), "first frame");
            t.Equals(fileName, std::string("title-0.png"), "k=0 file");
            t.Equals(k, 0u, "k=0");
            script.commitCapture(true);
            t.IsFalse(script.takeCapture(10, fileName, screen, k), "same tick not captured twice");
            script.onVSync(11, read, log);
            t.IsFalse(script.quitRequested(), "one of two frames");
            t.IsTrue(script.takeCapture(11, fileName, screen, k), "second frame");
            script.commitCapture(false);
            t.IsTrue(script.takeCapture(12, fileName, screen, k), "failed write retried");
            t.Equals(fileName, std::string("title-1.png"), "k=1 reused after failure");
            script.commitCapture(true);
            t.IsFalse(script.takeCapture(13, fileName, screen, k), "no more than n frames");
            script.onVSync(12, read, log);
            t.IsTrue(script.quitRequested(), "capture completes after n written frames"); });

        tc.Run("captureNeedsTick: vsync N is held until the frame of vsync N-1 is taken", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "capture title 3\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();
            std::string fileName;
            std::string screen;
            uint32_t k = 0;

            t.IsFalse(script.captureNeedsTick(5), "no capture step yet: nothing held");
            script.onVSync(10, read, log);
            t.IsFalse(script.captureNeedsTick(10), "the step's first vsync is never held (its frame is published after)");
            script.onVSync(11, read, log);
            t.IsTrue(script.captureNeedsTick(11), "vsync 11 waits for frame 10");
            t.IsTrue(script.takeCapture(10, fileName, screen, k), "frame 10 taken");
            t.IsFalse(script.captureNeedsTick(11), "released once frame 10 is taken");
            script.commitCapture(true);
            script.onVSync(12, read, log);
            t.IsTrue(script.captureNeedsTick(12), "vsync 12 waits for frame 11");
            t.IsTrue(script.takeCapture(11, fileName, screen, k), "frame 11");
            script.commitCapture(true);
            t.IsTrue(script.takeCapture(12, fileName, screen, k), "frame 12, the third");
            script.commitCapture(true);
            t.IsFalse(script.captureNeedsTick(13), "all written: nothing held"); });

        tc.Run("capture wants frames only during its step and refuses frames older than the step", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "wait vsync 2\ncapture title 1\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            const auto read = mem.reader();
            std::string fileName;
            std::string screen;
            uint32_t k = 0u;
            script.onVSync(1, read, log);
            t.IsFalse(script.captureWanted(), "no frames wanted while waiting");
            script.onVSync(3, read, log);
            t.IsTrue(script.captureWanted(), "frames wanted once the capture step begins (tick 3)");
            t.IsFalse(script.takeCapture(2, fileName, screen, k), "a frame from before the step is refused");
            t.IsTrue(script.takeCapture(3, fileName, screen, k), "the step's own frame is taken");
            script.commitCapture(true);
            t.IsFalse(script.captureWanted(), "no more frames wanted once n are written");
            script.onVSync(4, read, log);
            t.IsTrue(script.quitRequested(), "script moves on");
            t.IsFalse(script.captureWanted(), "none wanted after the step"); });

        tc.Run("timeout with injected clock", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "timeout 30\nwait vsync 1000\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            script.start(100.0);
            script.onVSync(1, mem.reader(), log);
            std::string message;
            t.IsFalse(script.timedOut(129.5, message), "within timeout");
            t.IsTrue(script.timedOut(130.0, message), "timed out");
            t.Equals(message, std::string("[script] TIMEOUT step 0 line 2: wait vsync 1000"), "timeout message"); });

        tc.Run("loop end before quit exits 4, after quit 0", [](TestCase &t)
               {
            Ps2InputScript script;
            t.IsTrue(parseOk(script, "wait vsync 2\nquit\n"), "parse");
            FakeMemory mem;
            std::ostringstream log;
            std::string message;
            script.onVSync(1, mem.reader(), log);
            t.Equals(script.loopEndExitCode(message), 4, "game ended before quit");
            t.Equals(message, std::string("[script] game ended before quit at step 0 line 1: wait vsync 2"), "end message");
            script.onVSync(3, mem.reader(), log);
            t.IsTrue(script.quitRequested(), "quit reached");
            t.Equals(script.loopEndExitCode(message), 0, "quit exits 0");
            t.IsTrue(message.empty(), "no message after quit"); });
    });
}
