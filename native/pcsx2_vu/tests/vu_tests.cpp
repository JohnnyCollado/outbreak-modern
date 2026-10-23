// ob_pcsx2_vu_tests: code-cache placement and the runner's start-up path for PS2X_VU1=mvu
// (reports/2026-10-23-gameplay.md, the "no free 64 MB range within 1 GB of VU1 data" crash).
//  1. Crowd the address space around a heap VU1 data block (every free range within +-2.5 GB reserved, as a
//     window + GPU driver can do): the old placement (code cache searched near the heap block) fails.
//  2. Same crowding: the runner's backend (ps2CreateExternalVu1 -> InitOwned, one reservation for VU1 memory
//     and cache) starts, PS2Memory takes its memory (patch 0041), and a program with constant-address SQ/LQ
//     (the disp32 data references of microVU_Misc.inl:342-360) runs right.
//  3. Every free range big enough for that reservation taken: creation fails with a reason, the runtime's
//     ps2InstallExternalVu1 logs the fallback line once, and the interpreter runs the same program.
// Links the runner's own ps2_runtime.lib (PS2Memory, VU1Interpreter, ps2InstallExternalVu1) and adapter/hook.cpp.
#include "MiniTest.h"
#include "ob_pcsx2_vu.h"

#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_vu1_external.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
    constexpr uint32_t kUpperNop = 0x000002FFu;
    constexpr uint32_t kEbit = 0x40000000u;
    constexpr uint32_t kLowerNop = 0x8000033Cu;

    // VU lower-op encodings (as ps2xTest/src/ps2_vu1_tests.cpp makeVuSq / makeVuLq / makeVuIaddiu).
    uint32_t sq(uint8_t dest, uint8_t fs, uint8_t it, int16_t imm)
    {
        return (0x01u << 25) | (uint32_t(dest & 0xFu) << 21) | (uint32_t(it & 0xFu) << 16) | (uint32_t(fs & 0x1Fu) << 11) |
               (uint32_t(imm) & 0x7FFu);
    }
    uint32_t lq(uint8_t dest, uint8_t ft, uint8_t is, int16_t imm)
    {
        return (uint32_t(dest & 0xFu) << 21) | (uint32_t(ft & 0x1Fu) << 16) | (uint32_t(is & 0xFu) << 11) | (uint32_t(imm) & 0x7FFu);
    }
    uint32_t iaddiu(uint8_t it, uint8_t is, int16_t imm)
    {
        return (0x08u << 25) | (uint32_t(it & 0xFu) << 16) | (uint32_t(is & 0xFu) << 11) | (uint32_t(imm) & 0x7FFu);
    }
    void pair(uint8_t *code, uint32_t pc, uint32_t lower, uint32_t upper)
    {
        std::memcpy(code + pc, &lower, 4);
        std::memcpy(code + pc + 4, &upper, 4);
    }

    // SQ.xyzw vf1, 0x10(vi0); LQ.xyzw vf2, 0x10(vi0); IADDIU vi1, vi0, 5 + E; NOP. Constant addresses: the
    // loads/stores are encoded as disp32 references to VU1 data (microVU_Misc.inl:342-360).
    void writeProgram(uint8_t *code)
    {
        std::memset(code, 0, 0x4000);
        pair(code, 0x00, sq(0xF, 1, 0, 0x10), kUpperNop);
        pair(code, 0x08, lq(0xF, 2, 0, 0x10), kUpperNop);
        pair(code, 0x10, iaddiu(1, 0, 5), kUpperNop | kEbit);
        pair(code, 0x18, kLowerNop, kUpperNop);
    }

    constexpr float kVf1[4] = {1.5f, -2.0f, 3.25f, 4.0f};

    void initialState(VU1State &s)
    {
        s = VU1State{};
        s.vf[0][3] = 1.0f;
        for (int l = 0; l < 4; ++l)
            s.vf[1][l] = kVf1[l];
    }

    bool resultOk(TestCase &t, const VU1State &s, const uint8_t *data, const char *who)
    {
        float stored[4];
        std::memcpy(stored, data + 0x100, sizeof(stored));
        bool ok = true;
        for (int l = 0; l < 4; ++l)
            ok = ok && stored[l] == kVf1[l] && s.vf[2][l] == kVf1[l];
        t.IsTrue(ok, std::string(who) + ": SQ stored vf1 at data+0x100 and LQ read it back into vf2");
        t.Equals(s.vi[1], 5, std::string(who) + ": vi1 = 5 at the E bit");
        return ok;
    }

    // Reserves (MEM_RESERVE, no commit) every free, 64 KB aligned range of at least minSize bytes inside
    // [lo, hi); released on destruction.
    struct Crowd
    {
        std::vector<void *> held;
        uint64_t bytes = 0;

        void fill(uintptr_t lo, uintptr_t hi, size_t minSize)
        {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            const uintptr_t gran = si.dwAllocationGranularity;
            lo = std::max(lo, reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress));
            hi = std::min(hi, reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress));
            uintptr_t addr = lo;
            while (addr < hi)
            {
                MEMORY_BASIC_INFORMATION mbi;
                if (VirtualQuery(reinterpret_cast<void *>(addr), &mbi, sizeof(mbi)) == 0)
                    break;
                const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                const uintptr_t end = base + mbi.RegionSize;
                if (mbi.State == MEM_FREE)
                {
                    const uintptr_t s = (std::max(addr, base) + gran - 1) & ~(gran - 1);
                    const uintptr_t e = std::min(end, hi) & ~(gran - 1);
                    if (e > s && e - s >= minSize)
                        if (void *p = VirtualAlloc(reinterpret_cast<void *>(s), e - s, MEM_RESERVE, PAGE_NOACCESS))
                        {
                            held.push_back(p);
                            bytes += e - s;
                        }
                }
                addr = end;
            }
        }

        ~Crowd()
        {
            for (void *p : held)
                VirtualFree(p, 0, MEM_RELEASE);
        }
    };

    constexpr uint64_t kWindow = 0xA0000000ull; // 2.5 GB: beyond microVU's 2 GB reach
    constexpr size_t kGran = 0x10000;

    // Heap VU1 memory, as PS2Memory allocates it without patch 0041 (ps2_memory.cpp, new[]).
    struct HeapVu1
    {
        std::unique_ptr<uint8_t[]> data{new uint8_t[0x4000]()};
        std::unique_ptr<uint8_t[]> code{new uint8_t[0x4000]()};
    };
    HeapVu1 &heapVu1()
    {
        static HeapVu1 h;
        return h;
    }

    std::vector<std::string> g_lines;
    void captureLine(const char *line) { g_lines.emplace_back(line); }
} // namespace

void register_vu_tests()
{
    MiniTest::Case("Mvu placement", [](TestCase &tc)
    {
        tc.Run("crowded +-2.5 GB around heap VU1 data: the code cache cannot be placed near it", [](TestCase &t)
        {
            uint8_t *const heapData = heapVu1().data.get();
            const uintptr_t heapAt = reinterpret_cast<uintptr_t>(heapData);
            Crowd crowd;
            crowd.fill(heapAt > kWindow ? heapAt - kWindow : 0, heapAt + kWindow, kGran);
            t.IsTrue(!crowd.held.empty(), "the crowd reserved the free ranges around the heap");
            char err[512] = {};
            const bool ok = ObPcsx2Vu::Init(heapData, heapVu1().code.get(), err, sizeof(err));
            t.IsTrue(!ok, "Init(heap data) fails: no free range in reach");
            t.IsTrue(std::strstr(err, "no free 64 MB range") != nullptr, std::string("with the reason: ") + err);
            if (ok)
                ObPcsx2Vu::Shutdown();
        });

        tc.Run("same crowding: the runner's backend starts on its own reservation and runs constant-address SQ/LQ", [](TestCase &t)
        {
            uint8_t *const heapData = heapVu1().data.get();
            const uintptr_t heapAt = reinterpret_cast<uintptr_t>(heapData);
            Crowd crowd;
            crowd.fill(heapAt > kWindow ? heapAt - kWindow : 0, heapAt + kWindow, kGran);
            char err[512] = {};
            PS2ExternalVu1 *vu1 = ps2CreateExternalVu1("mvu", err, sizeof(err));
            t.IsTrue(vu1 != nullptr, std::string("ps2CreateExternalVu1 starts: ") + err);
            if (!vu1)
                return;
            t.IsTrue(ps2InstallExternalVu1(vu1, "mvu", err, &captureLine), "and is installed");
            {
                PS2Memory mem;
                t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed");
                t.IsTrue(mem.getVU1Data() == ObPcsx2Vu::OwnedData() && mem.getVU1Data() != nullptr,
                         "PS2Memory's VU1 data is the backend's");
                const uint64_t dist = mem.getVU1Data() > heapData
                                          ? uint64_t(mem.getVU1Data() - heapData)
                                          : uint64_t(heapData - mem.getVU1Data());
                std::cout << "  " << ObPcsx2Vu::Describe() << " (" << (dist >> 20) << " MB from the crowded heap block)" << std::endl;
                VU1State s;
                initialState(s);
                vu1->setState(s);
                writeProgram(mem.getVU1Code());
                mem.write32(PS2_VU1_CODE_BASE, sq(0xF, 1, 0, 0x10)); // bumps the code generation
                vu1->mscal(mem, 0u, 0u, 0u, 0u);
                VU1State after{};
                vu1->readState(after);
                resultOk(t, after, mem.getVU1Data(), "microVU");
            }
            ps2SetExternalVu1(nullptr);
            ObPcsx2Vu::Shutdown();
            delete vu1;
        });

        tc.Run("no range free for the reservation: creation fails with a reason, one fallback line, the interpreter runs", [](TestCase &t)
        {
            std::string why;
            {
                Crowd crowd;
                crowd.fill(0, ~uintptr_t(0), 0x10000 + 0x4000000); // every range that could hold 64 KB + 64 MB
                char err[512] = {};
                PS2ExternalVu1 *vu1 = ps2CreateExternalVu1("mvu", err, sizeof(err));
                t.IsTrue(vu1 == nullptr, "ps2CreateExternalVu1 fails");
                why = err;
                g_lines.clear();
                t.IsTrue(!ps2InstallExternalVu1(vu1, "mvu", err, &captureLine), "nothing is installed");
            }
            t.IsTrue(why.find("cannot reserve") != std::string::npos, "the reason names the reservation: " + why);
            t.IsTrue(ps2ExternalVu1() == nullptr, "the VU1 interpreter is the backend");
            t.Equals(g_lines.size(), size_t(1), "one log line");
            if (!g_lines.empty())
                t.IsTrue(g_lines[0] == "[vu1] mvu unavailable (" + why + "); falling back to interp", "line: " + g_lines[0]);

            PS2Memory mem;
            t.IsTrue(mem.initialize(), "PS2Memory initialize should succeed (heap VU1 memory)");
            GS gs;
            gs.init(mem.getGSVRAM(), static_cast<uint32_t>(PS2_GS_VRAM_SIZE), &mem.gs());
            VU1Interpreter vu;
            vu.reset();
            initialState(vu.state());
            writeProgram(mem.getVU1Code());
            mem.write32(PS2_VU1_CODE_BASE, sq(0xF, 1, 0, 0x10));
            vu.execute(mem.getVU1Code(), PS2_VU1_CODE_SIZE, mem.getVU1Data(), PS2_VU1_DATA_SIZE, gs, &mem, 0u);
            resultOk(t, vu.state(), mem.getVU1Data(), "interpreter");
        });
    });
}

int main()
{
    register_vu_tests();
    const int res = MiniTest::Run();
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(res);
}
