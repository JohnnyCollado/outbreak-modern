#include "Common.h"
#include "Pad.h"
#include "Pad2.h"

// libpad2 keeps one "socket" per controller (port, slot) instead of libpad's
// port/slot pairs. Each socket here maps onto a libpad port opened in
// DualShock 2 mode, so the host input, the override used by tests and the
// debug snapshot are shared with libpad.
//
// Layout of the data scePad2Read returns (SCE libpad2, DualShock 2 profile):
//   [0..1]  buttons, active-low, big-endian (SELECT..LEFT, L2..SQUARE)
//   [2..5]  right stick x/y, left stick x/y
//   [6..17] button pressures, same order as libpad
// That is libpad's scePadRead buffer from byte 2 on.
namespace ps2_stubs
{
    namespace
    {
        constexpr size_t kPad2SocketCount = 4;
        constexpr size_t kPad2DataSize = 32;
        constexpr int32_t kPad2StateConnected = 1;
        constexpr int32_t kPad2StateDisconnected = 0;
        constexpr int32_t kPad2DualShock2DataLength = 18;
        constexpr int32_t kPad2Error = -1;

        // scePad2SocketParam
        constexpr uint32_t kPad2ParamPortOffset = 4;
        constexpr uint32_t kPad2ParamSlotOffset = 8;

        struct Pad2Socket
        {
            bool open = false;
            int port = 0;
            int slot = 0;
        };

        std::mutex g_pad2Mutex;
        bool g_pad2Initialised = false;
        Pad2Socket g_pad2Sockets[kPad2SocketCount]{};

        Pad2Socket *lookupSocketLocked(uint32_t socket)
        {
            if (socket >= kPad2SocketCount)
            {
                return nullptr;
            }
            return &g_pad2Sockets[socket];
        }
    }

    void sceDbcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void sceDbcEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 1);
    }

    void scePad2Init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        g_pad2Initialised = true;
        for (auto &socket : g_pad2Sockets)
        {
            socket = Pad2Socket{};
        }
        setReturnS32(ctx, 1);
    }

    void scePad2End(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        for (auto &socket : g_pad2Sockets)
        {
            if (socket.open)
            {
                padClosePortForPad2(socket.port, socket.slot);
            }
            socket = Pad2Socket{};
        }
        g_pad2Initialised = false;
        setReturnS32(ctx, 1);
    }

    // s32 scePad2CreateSocket(scePad2SocketParam *param, void *buffer)
    // Returns the socket number, or a negative value.
    void scePad2CreateSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t paramAddr = getRegU32(ctx, 4);
        const uint8_t *param = getMemPtr(rdram, paramAddr);
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        if (!g_pad2Initialised || !param)
        {
            setReturnS32(ctx, kPad2Error);
            return;
        }

        uint32_t port32 = 0u;
        uint32_t slot32 = 0u;
        std::memcpy(&port32, param + kPad2ParamPortOffset, sizeof(port32));
        std::memcpy(&slot32, param + kPad2ParamSlotOffset, sizeof(slot32));
        const int port = static_cast<int>(port32);
        const int slot = static_cast<int>(slot32);

        for (size_t i = 0; i < kPad2SocketCount; ++i)
        {
            Pad2Socket &socket = g_pad2Sockets[i];
            if (socket.open)
            {
                continue;
            }
            if (!padOpenPortForPad2(port, slot))
            {
                break;
            }
            socket.open = true;
            socket.port = port;
            socket.slot = slot;
            setReturnS32(ctx, static_cast<int32_t>(i));
            return;
        }

        setReturnS32(ctx, kPad2Error);
    }

    void scePad2DeleteSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        Pad2Socket *socket = lookupSocketLocked(getRegU32(ctx, 4));
        if (!socket || !socket->open)
        {
            setReturnS32(ctx, kPad2Error);
            return;
        }
        padClosePortForPad2(socket->port, socket->slot);
        *socket = Pad2Socket{};
        setReturnS32(ctx, 1);
    }

    // s32 scePad2GetState(s32 socket): 1 = connected and ready, 0 = not (yet).
    void scePad2GetState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        const Pad2Socket *socket = lookupSocketLocked(getRegU32(ctx, 4));
        setReturnS32(ctx, (socket && socket->open) ? kPad2StateConnected : kPad2StateDisconnected);
    }

    // s32 scePad2Read(s32 socket, u8 *data): the data length, or a negative value.
    void scePad2Read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t dataAddr = getRegU32(ctx, 5);
        uint8_t *data = getMemPtr(rdram, dataAddr);
        Pad2Socket socket;
        {
            std::lock_guard<std::mutex> lock(g_pad2Mutex);
            const Pad2Socket *shared = lookupSocketLocked(getRegU32(ctx, 4));
            if (!shared || !shared->open || !data)
            {
                setReturnS32(ctx, kPad2Error);
                return;
            }
            socket = *shared;
        }

        uint8_t padData[kPadDebugDataSize]{};
        if (!padReadPortForPad2(socket.port, socket.slot, runtime, padData, dataAddr))
        {
            setReturnS32(ctx, kPad2Error);
            return;
        }

        ps2TraceGuestRangeWrite(rdram, dataAddr, static_cast<uint32_t>(kPad2DataSize), "scePad2Read", ctx);
        std::memset(data, 0, kPad2DataSize);
        std::memcpy(data, padData + 2, static_cast<size_t>(kPad2DualShock2DataLength));
        setReturnS32(ctx, kPad2DualShock2DataLength);
    }

    // s32 scePad2GetButtonProfile(s32 socket, u8 profile[4])
    // One bit per button the pad has; a DualShock 2 has them all.
    void scePad2GetButtonProfile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t profileAddr = getRegU32(ctx, 5);
        uint8_t *profile = getMemPtr(rdram, profileAddr);
        std::lock_guard<std::mutex> lock(g_pad2Mutex);
        const Pad2Socket *socket = lookupSocketLocked(getRegU32(ctx, 4));
        if (!socket || !socket->open || !profile)
        {
            setReturnS32(ctx, kPad2Error);
            return;
        }
        ps2TraceGuestRangeWrite(rdram, profileAddr, 4u, "scePad2GetButtonProfile", ctx);
        std::memset(profile, 0xFF, 4);
        setReturnS32(ctx, 4);
    }
}
