#pragma once

#include "ps2_stubs.h"

#include <cstdint>

// libpad2 (scePad2*) and libdbc (sceDbc*): the socket-based controller library
// used by later SCE SDKs. It is served by the same host input as libpad.
namespace ps2_stubs
{
    void sceDbcInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceDbcEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2Init(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2End(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2CreateSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2DeleteSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2GetState(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2Read(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void scePad2GetButtonProfile(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
