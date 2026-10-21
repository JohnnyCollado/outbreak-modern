#include "MiniTest.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "Stubs/Pad.h"
#include "ps2_runtime_calls.h"

#include <cstring>
#include <vector>
#include <cstdint>
#include <string>


namespace
{
    constexpr uint32_t kPadDataAddr = 0x1000;

    constexpr uint16_t kPadBtnSelect = 1u << 0;
    constexpr uint16_t kPadBtnL3 = 1u << 1;
    constexpr uint16_t kPadBtnR3 = 1u << 2;
    constexpr uint16_t kPadBtnStart = 1u << 3;
    constexpr uint16_t kPadBtnUp = 1u << 4;
    constexpr uint16_t kPadBtnRight = 1u << 5;
    constexpr uint16_t kPadBtnDown = 1u << 6;
    constexpr uint16_t kPadBtnLeft = 1u << 7;
    constexpr uint16_t kPadBtnL2 = 1u << 8;
    constexpr uint16_t kPadBtnR2 = 1u << 9;
    constexpr uint16_t kPadBtnL1 = 1u << 10;
    constexpr uint16_t kPadBtnR1 = 1u << 11;
    constexpr uint16_t kPadBtnTriangle = 1u << 12;
    constexpr uint16_t kPadBtnCircle = 1u << 13;
    constexpr uint16_t kPadBtnCross = 1u << 14;
    constexpr uint16_t kPadBtnSquare = 1u << 15;

    void setRegU32(R5900Context &ctx, int reg, uint32_t value)
    {
        ctx.r[reg] = _mm_set_epi64x(0, static_cast<int64_t>(value));
    }

    void openPadPort(R5900Context &ctx, std::vector<uint8_t> &rdram, uint32_t port = 0, uint32_t slot = 0)
    {
        setRegU32(ctx, 4, port);
        setRegU32(ctx, 5, slot);
        setRegU32(ctx, 6, kPadDataAddr + 0x200u);
        ps2_stubs::scePadPortOpen(rdram.data(), &ctx, nullptr);
    }

    void closePadPort(R5900Context &ctx, std::vector<uint8_t> &rdram, uint32_t port = 0, uint32_t slot = 0)
    {
        setRegU32(ctx, 4, port);
        setRegU32(ctx, 5, slot);
        ps2_stubs::scePadPortClose(rdram.data(), &ctx, nullptr);
    }

    void runPadRead(R5900Context &ctx, std::vector<uint8_t> &rdram)
    {
        setRegU32(ctx, 4, 0u);
        setRegU32(ctx, 5, 0u);
        setRegU32(ctx, 6, kPadDataAddr); // a2
        ps2_stubs::scePadRead(rdram.data(), &ctx, nullptr);
    }

    uint16_t readButtons(const std::vector<uint8_t> &rdram)
    {
        const uint8_t *data = rdram.data() + kPadDataAddr;
        return static_cast<uint16_t>(data[2] | (data[3] << 8));
    }
}

void register_pad_input_tests()
{
    MiniTest::Case("PadInput", [](TestCase &tc)
                   {
        tc.Run("scePadRead uses override state", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            const uint16_t buttons = static_cast<uint16_t>(0xFFFFu & ~kPadBtnCross & ~kPadBtnStart);
            ps2_stubs::setPadOverrideState(buttons, 0x00, 0xFF, 0x10, 0xEE);

            runPadRead(ctx, rdram);

            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadRead should return 1");
            t.Equals(readButtons(rdram), buttons, "button bitmask should match override state");
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[4], static_cast<uint8_t>(0x10), "rx should match override");
            t.Equals(data[5], static_cast<uint8_t>(0xEE), "ry should match override");
            t.Equals(data[6], static_cast<uint8_t>(0x00), "lx should match override");
            t.Equals(data[7], static_cast<uint8_t>(0xFF), "ly should match override");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("scePadRead button bits are active-low", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            struct ButtonCase
            {
                uint16_t mask;
                const char *name;
            };

            const ButtonCase cases[] = {
                {kPadBtnSelect, "select"},
                {kPadBtnL3, "l3"},
                {kPadBtnR3, "r3"},
                {kPadBtnStart, "start"},
                {kPadBtnUp, "up"},
                {kPadBtnRight, "right"},
                {kPadBtnDown, "down"},
                {kPadBtnLeft, "left"},
                {kPadBtnL2, "l2"},
                {kPadBtnR2, "r2"},
                {kPadBtnL1, "l1"},
                {kPadBtnR1, "r1"},
                {kPadBtnTriangle, "triangle"},
                {kPadBtnCircle, "circle"},
                {kPadBtnCross, "cross"},
                {kPadBtnSquare, "square"}};

            for (const auto &entry : cases)
            {
                const uint16_t buttons = static_cast<uint16_t>(0xFFFFu & ~entry.mask);
                ps2_stubs::setPadOverrideState(buttons, 0x80, 0x80, 0x80, 0x80);
                runPadRead(ctx, rdram);

                t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadRead should succeed for opened ports");
                const uint16_t mask = readButtons(rdram);
                t.IsTrue((mask & entry.mask) == 0, std::string("button should be active-low: ").append(entry.name));
            }

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("scePadGetButtonMask returns all buttons", [](TestCase &t)
               {
            R5900Context ctx;
            ps2_stubs::scePadGetButtonMask(nullptr, &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0xFFFF), "button mask should be 0xFFFF");
        });

        tc.Run("basic pad init/port/state functions return expected values", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInit should succeed");

            ps2_stubs::scePadInit2(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInit2 should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "closed port should report DISCONNECTED");

            openPadPort(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadPortOpen should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "scePadGetState should return STABLE");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetReqState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadGetReqState should return completed");

            ps2_stubs::scePadGetPortMax(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(2), "scePadGetPortMax should be 2");

            setRegU32(ctx, 4, 0u);
            ps2_stubs::scePadGetSlotMax(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadGetSlotMax should be 1");

            ps2_stubs::scePadGetModVersion(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0x0200), "scePadGetModVersion should be 0x0200");

            closePadPort(ctx, rdram);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadPortClose should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "closed port should return DISCONNECTED after close");
        });

        tc.Run("pad command state reports EXECCMD once before returning STABLE", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(5), "first state after mode command should be EXECCMD");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "second state after mode command should return STABLE");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadEnterPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnterPressMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(5), "first state after press-mode command should be EXECCMD");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "second state after press-mode command should return STABLE");

            closePadPort(ctx, rdram);
        });

        tc.Run("pad info and mode helpers return consistent values", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 6, static_cast<uint32_t>(-1));
            ps2_stubs::scePadInfoAct(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<uint32_t>(getRegU32(&ctx, 2)) >= 1u, "scePadInfoAct should report at least one actuator descriptor");

            ps2_stubs::scePadInfoComb(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadInfoComb should return 0");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "scePadInfoMode CURID should return digital at open");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 4);
            setRegU32(ctx, 7, static_cast<uint32_t>(-1));
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<uint32_t>(getRegU32(&ctx, 2)) >= 1u, "scePadInfoMode table count should be non-zero");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadInfoPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadInfoPressMode should report pressure support");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should accept digital mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 0u);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "CURID should switch to digital mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should accept analog mode");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 4);
            setRegU32(ctx, 7, 0u);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(7), "mode table entry should return DualShock in analog mode");

            closePadPort(ctx, rdram);
        });

        tc.Run("pads open in digital mode and switch to analog on scePadSetMainMode", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(6), "freshly opened port should report STABLE");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(4), "scePadInfoMode CURID should return digital at open");

            runPadRead(ctx, rdram);
            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[1], static_cast<uint8_t>(0x41), "mode byte should be 0x41 (digital) at open");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 3);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should succeed switching to analog");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1);
            setRegU32(ctx, 7, 0);
            ps2_stubs::scePadInfoMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(7), "scePadInfoMode CURID should return analog after SetMainMode");

            // scePadSetMainMode queues a one-shot EXECCMD transient state; pump scePadGetState
            // once so the port settles back to STABLE before reading, mirroring the existing
            // "pad command state reports EXECCMD once before returning STABLE" test.
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadGetState(rdram.data(), &ctx, nullptr);

            runPadRead(ctx, rdram);
            t.Equals(data[1], static_cast<uint8_t>(0x73), "mode byte should be 0x73 (analog) after SetMainMode");

            closePadPort(ctx, rdram);
        });

        tc.Run("pad setters return success", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetActAlign(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetActAlign should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetActDirect(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetActDirect should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0xFFFFu);
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 1u);
            setRegU32(ctx, 7, 3u);
            ps2_stubs::scePadSetMainMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetMainMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetReqState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetReqState should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetVrefParam(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetVrefParam should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadSetWarningLevel(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(0), "scePadSetWarningLevel should return 0");

            ps2_stubs::scePadEnd(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnd should succeed");

            openPadPort(ctx, rdram);
            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadEnterPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnterPressMode should succeed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadExitPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadExitPressMode should succeed");

            closePadPort(ctx, rdram);
        });

        tc.Run("scePadRead fills pressure bytes and honors button info mask", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            ps2_stubs::scePadInit(rdram.data(), &ctx, nullptr);
            openPadPort(ctx, rdram);

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, 0xFFFFu);
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should accept all buttons");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            ps2_stubs::scePadEnterPressMode(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadEnterPressMode should enable pressure data");

            const uint16_t pressedButtons = static_cast<uint16_t>(0xFFFFu &
                                                                   ~kPadBtnLeft &
                                                                   ~kPadBtnUp &
                                                                   ~kPadBtnTriangle &
                                                                   ~kPadBtnCross &
                                                                   ~kPadBtnL1 &
                                                                   ~kPadBtnR2);
            ps2_stubs::setPadOverrideState(pressedButtons, 0x80, 0x80, 0x80, 0x80);
            runPadRead(ctx, rdram);

            const uint8_t *data = rdram.data() + kPadDataAddr;
            t.Equals(data[8], static_cast<uint8_t>(0x00), "right pressure should be clear when not pressed");
            t.Equals(data[9], static_cast<uint8_t>(0xFF), "left pressure should be populated when pressed");
            t.Equals(data[10], static_cast<uint8_t>(0xFF), "up pressure should be populated when pressed");
            t.Equals(data[11], static_cast<uint8_t>(0x00), "down pressure should be clear when not pressed");
            t.Equals(data[12], static_cast<uint8_t>(0xFF), "triangle pressure should be populated when pressed");
            t.Equals(data[13], static_cast<uint8_t>(0x00), "circle pressure should be clear when not pressed");
            t.Equals(data[14], static_cast<uint8_t>(0xFF), "cross pressure should be populated when pressed");
            t.Equals(data[15], static_cast<uint8_t>(0x00), "square pressure should be clear when not pressed");
            t.Equals(data[16], static_cast<uint8_t>(0xFF), "L1 pressure should be populated when pressed");
            t.Equals(data[17], static_cast<uint8_t>(0x00), "L2 pressure should be clear when not pressed");
            t.Equals(data[18], static_cast<uint8_t>(0x00), "R1 pressure should be clear when not pressed");
            t.Equals(data[19], static_cast<uint8_t>(0xFF), "R2 pressure should be populated when pressed");

            setRegU32(ctx, 4, 0u);
            setRegU32(ctx, 5, 0u);
            setRegU32(ctx, 6, static_cast<uint32_t>(kPadBtnL1 | kPadBtnR2));
            ps2_stubs::scePadSetButtonInfo(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), static_cast<uint32_t>(1), "scePadSetButtonInfo should narrow the enabled pressure mask");

            runPadRead(ctx, rdram);

            t.Equals(data[9], static_cast<uint8_t>(0x00), "masked-out direction pressure should clear");
            t.Equals(data[10], static_cast<uint8_t>(0x00), "masked-out direction pressure should clear");
            t.Equals(data[12], static_cast<uint8_t>(0x00), "masked-out face-button pressure should clear");
            t.Equals(data[14], static_cast<uint8_t>(0x00), "masked-out face-button pressure should clear");
            t.Equals(data[16], static_cast<uint8_t>(0xFF), "enabled L1 pressure should remain populated");
            t.Equals(data[19], static_cast<uint8_t>(0xFF), "enabled R2 pressure should remain populated");

            ps2_stubs::clearPadOverrideState();
            closePadPort(ctx, rdram);
        });

        tc.Run("pad string helpers map state codes", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            setRegU32(ctx, 4, 1);
            setRegU32(ctx, 5, kPadDataAddr);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr)).find("FINDPAD") != std::string::npos,
                     "state 1 should map to FINDPAD");

            setRegU32(ctx, 4, 0);
            setRegU32(ctx, 5, kPadDataAddr + 64);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 64)).find("DISCONNECTED") != std::string::npos,
                     "state 0 should map to DISCONNECTED");

            setRegU32(ctx, 4, 1);
            setRegU32(ctx, 5, kPadDataAddr + 128);
            ps2_stubs::scePadReqIntToStr(rdram.data(), &ctx, nullptr);
            t.IsTrue(std::string(reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 128)).find("BUSY") != std::string::npos,
                     "req state 1 should map to BUSY");
        });
        tc.Run("scePadGetFrameCount increments", [](TestCase &t)
               {
            R5900Context ctx;
            ps2_stubs::scePadGetFrameCount(nullptr, &ctx, nullptr);
            const uint32_t first = getRegU32(&ctx, 2);
            ps2_stubs::scePadGetFrameCount(nullptr, &ctx, nullptr);
            const uint32_t second = getRegU32(&ctx, 2);
            t.Equals(second, first + 1, "frame count should increment");
        });

        tc.Run("scePadStateIntToStr and scePadReqIntToStr write strings", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;

            setRegU32(ctx, 4, 6);
            setRegU32(ctx, 5, kPadDataAddr);
            ps2_stubs::scePadStateIntToStr(rdram.data(), &ctx, nullptr);
            const char *stateStr = reinterpret_cast<const char *>(rdram.data() + kPadDataAddr);
            t.IsTrue(std::string(stateStr).find("STABLE") != std::string::npos, "state string should include STABLE");

            setRegU32(ctx, 4, 0);
            setRegU32(ctx, 5, kPadDataAddr + 64);
            ps2_stubs::scePadReqIntToStr(rdram.data(), &ctx, nullptr);
            const char *reqStr = reinterpret_cast<const char *>(rdram.data() + kPadDataAddr + 64);
            t.IsTrue(std::string(reqStr).find("COMPLETE") != std::string::npos, "req string should include COMPLETE");
        });

        // libpad2: Outbreak (SLPM-65428) opens one socket per port with
        // scePad2CreateSocket, waits for scePad2GetState == 1, reads the
        // button profile once and then scePad2Read every frame; a profile with
        // bits in 0xFFF00000 makes it treat the pad as a DualShock 2 and read
        // the 12 pressure bytes after the sticks.
        tc.Run("libpad2: sockets open, connect, and read DualShock 2 data from the host pad", [](TestCase &t)
               {
            std::vector<uint8_t> rdram(PS2_RAM_SIZE, 0);
            R5900Context ctx;
            constexpr uint32_t kParamAddr = kPadDataAddr + 0x100u;
            constexpr uint32_t kProfileAddr = kPadDataAddr + 0x140u;

            t.IsTrue(ps2_runtime_calls::isStubName("scePad2Read") && ps2_runtime_calls::isStubName("sceDbcInit"),
                     "the recompiler must route scePad2*/sceDbc* to the runtime");

            ps2_stubs::sceDbcInit(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 1u, "sceDbcInit returns 1");
            setRegU32(ctx, 4, 0u);
            ps2_stubs::scePad2Init(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 1u, "scePad2Init returns 1");

            // scePad2SocketParam { number, port, slot, flag }
            uint32_t param[4] = {0u, 1u, 0u, 2u};
            std::memcpy(rdram.data() + kParamAddr, param, sizeof(param));
            setRegU32(ctx, 4, kParamAddr);
            setRegU32(ctx, 5, kPadDataAddr + 0x400u);
            ps2_stubs::scePad2CreateSocket(rdram.data(), &ctx, nullptr);
            const int32_t socket = static_cast<int32_t>(getRegU32(&ctx, 2));
            t.IsTrue(socket >= 0, "scePad2CreateSocket returns a socket");

            setRegU32(ctx, 4, static_cast<uint32_t>(socket));
            ps2_stubs::scePad2GetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 1u, "scePad2GetState is 1 (connected)");

            setRegU32(ctx, 4, static_cast<uint32_t>(socket));
            setRegU32(ctx, 5, kProfileAddr);
            ps2_stubs::scePad2GetButtonProfile(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<int32_t>(getRegU32(&ctx, 2)) >= 0, "scePad2GetButtonProfile succeeds");
            uint32_t profile = 0u;
            std::memcpy(&profile, rdram.data() + kProfileAddr, sizeof(profile));
            t.IsTrue((profile & 0xFFF00000u) != 0u, "the profile reports a DualShock 2");

            const uint16_t buttons = static_cast<uint16_t>(0xFFFFu & ~kPadBtnCross & ~kPadBtnLeft);
            ps2_stubs::setPadOverrideState(buttons, 0x20, 0x30, 0x40, 0x50);
            setRegU32(ctx, 4, static_cast<uint32_t>(socket));
            setRegU32(ctx, 5, kPadDataAddr);
            ps2_stubs::scePad2Read(rdram.data(), &ctx, nullptr);
            t.IsTrue(static_cast<int32_t>(getRegU32(&ctx, 2)) >= 0, "scePad2Read succeeds");
            const uint8_t *d = rdram.data() + kPadDataAddr;
            const uint16_t guestButtons = static_cast<uint16_t>((d[0] << 8) | d[1]);
            t.Equals(guestButtons, static_cast<uint16_t>((buttons & 0xFFu) << 8 | (buttons >> 8)),
                     "buttons are the libpad bytes in order (SELECT..LEFT, then L2..SQUARE), active-low");
            t.Equals(d[2], static_cast<uint8_t>(0x40), "d[2] is the right stick x");
            t.Equals(d[3], static_cast<uint8_t>(0x50), "d[3] is the right stick y");
            t.Equals(d[4], static_cast<uint8_t>(0x20), "d[4] is the left stick x");
            t.Equals(d[5], static_cast<uint8_t>(0x30), "d[5] is the left stick y");
            t.Equals(d[6 + 1], static_cast<uint8_t>(0xFF), "pressure byte 1 (left) is full when LEFT is held");
            t.Equals(d[6 + 6], static_cast<uint8_t>(0xFF), "pressure byte 6 (cross) is full when CROSS is held");
            t.Equals(d[6 + 5], static_cast<uint8_t>(0x00), "pressure byte 5 (circle) is zero when released");
            ps2_stubs::clearPadOverrideState();

            setRegU32(ctx, 4, static_cast<uint32_t>(socket));
            ps2_stubs::scePad2DeleteSocket(rdram.data(), &ctx, nullptr);
            setRegU32(ctx, 4, static_cast<uint32_t>(socket));
            ps2_stubs::scePad2GetState(rdram.data(), &ctx, nullptr);
            t.Equals(static_cast<uint32_t>(getRegU32(&ctx, 2)), 0u, "a deleted socket is disconnected");
            ps2_stubs::scePad2End(rdram.data(), &ctx, nullptr);
        });
    });
}
