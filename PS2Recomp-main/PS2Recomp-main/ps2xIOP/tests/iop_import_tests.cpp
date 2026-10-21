#include "emulator/core/iop_cpu.h"
#include "emulator/core/iop_kernel.h"
#include "emulator/core/iop_memory.h"
#include "emulator/imports/iop_cdvd.h"
#include "emulator/imports/iop_imports.h"
#include "emulator/imports/iop_loadcore.h"
#include "emulator/imports/iop_timrman.h"
#include "emulator/services/iop_rpc.h"
#include "ps2x/iop/iop_host.h"

#include <cstdint>
#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{
    using namespace ps2x::iop;
    using namespace ps2x::iop::detail;

    constexpr uint32_t kExportMagic = 0x41C00000u;
    constexpr int32_t kLibraryNotFound = -213;
    constexpr int32_t kIllegalLibrary = -214;

    class NullHost : public IopHost
    {
    public:
        bool readGuest(uint32_t, void *, size_t) const override { return false; }
        bool writeGuest(uint32_t, const void *, size_t) override { return false; }
        bool zeroGuest(uint32_t, size_t) override { return false; }
        bool normalizeGuestAddress(uint32_t, uint32_t &) const override { return false; }
        uint32_t allocateIopHandle(IopHandleKind) override { return 1u; }
        uint32_t allocateGuest(uint32_t, uint32_t) override { return 0u; }
        void freeGuest(uint32_t) override {}
        void audioCommand(uint32_t, uint32_t, GuestBuffer, GuestBuffer) override {}
        std::string hostPath(HostPathKind) const override { return {}; }
        std::string translateGuestPath(std::string_view path) const override { return std::string(path); }
        uint64_t openHostFile(std::string_view) override { return 0u; }
        bool hostFileSize(uint64_t, uint64_t &) const override { return false; }
        bool readHostFile(uint64_t, uint64_t, void *, size_t, size_t &) override { return false; }
        void closeHostFile(uint64_t) override {}
        int32_t memoryCard(const MemoryCardRequest &) override { return 0; }
        bool hasGuestFunction(uint32_t) const override { return false; }
        bool invokeGuestFunction(uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *) override { return false; }
        void log(LogLevel, std::string_view) override {}
    };

    class CdRootHost final : public NullHost
    {
    public:
        explicit CdRootHost(std::filesystem::path rootPath)
            : root(std::move(rootPath))
        {
        }

        std::string hostPath(HostPathKind kind) const override
        {
            return kind == HostPathKind::CdRoot ? root.string() : std::string{};
        }

    private:
        std::filesystem::path root;
    };

    class RecordingExecutor final : public IopGuestExecutor
    {
    public:
        uint32_t executeGuestFunction(uint32_t address,
                                      uint32_t a0,
                                      uint32_t,
                                      uint32_t,
                                      uint32_t,
                                      uint32_t gp) override
        {
            ++calls;
            lastAddress = address;
            lastArgument = a0;
            lastGp = gp;
            return callbackResult;
        }

        uint32_t callbackResult = 0u;
        uint32_t calls = 0u;
        uint32_t lastAddress = 0u;
        uint32_t lastArgument = 0u;
        uint32_t lastGp = 0u;
    };

    bool expect(bool condition, std::string_view message)
    {
        if (condition)
            return true;
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }

    bool testLoadcoreRebootLibraryMode()
    {
        IopMemory memory;
        IopImportRegistry imports(memory);
        IopLoadcore loadcore(memory, imports);

        IopCpuState cpu{};
        cpu.gpr[4] = 0u;
        cpu.gpr[5] = 2u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) == kIllegalLibrary,
                    "loadcore:27 did not reject a null export table"))
            return false;

        constexpr uint32_t table = 0x1000u;
        memory.write32(table, kExportMagic);
        memory.write16(table + 8u, 0x0101u);
        memory.write16(table + 10u, 0x1234u);
        const char name[8] = {'t', 'e', 's', 't', 'l', 'i', 'b', '\0'};
        (void)memory.writeRam(table + 12u, name, sizeof(name));
        memory.write32(table + 20u, 0u);

        cpu = {};
        cpu.gpr[4] = table;
        cpu.gpr[5] = 2u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 rejected a valid export table") ||
            !expect(cpu.gpr[2] == 0u, "loadcore:27 returned an error for a valid export table") ||
            !expect(memory.read16(table + 10u) == 0x1232u,
                    "loadcore:27 did not replace only export mode bits 1 and 2"))
            return false;

        constexpr uint32_t invalidTable = 0x1100u;
        memory.write32(invalidTable, 0xDEADBEEFu);
        cpu = {};
        cpu.gpr[4] = invalidTable;
        cpu.gpr[5] = 6u;
        if (!expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 did not consume an invalid-table call") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) == kLibraryNotFound,
                    "loadcore:27 returned the wrong invalid-table error"))
            return false;

        if (!expect(imports.registerExportTable(table), "test export table did not register"))
            return false;
        memory.write32(table, 0u);
        cpu = {};
        cpu.gpr[4] = table;
        cpu.gpr[5] = 6u;
        return expect(loadcore.dispatchImport(27u, cpu), "loadcore:27 rejected a registered table") &&
               expect(cpu.gpr[2] == 0u, "loadcore:27 returned an error for a registered table") &&
               expect(memory.read16(table + 10u) == 0x1236u,
                      "loadcore:27 did not update a registered table's mode");
    }

    bool pollEvent(IopKernel &kernel, int eventId, uint32_t bits, uint32_t resultAddress, int32_t expected)
    {
        IopCpuState cpu{};
        cpu.gpr[4] = static_cast<uint32_t>(eventId);
        cpu.gpr[5] = bits;
        cpu.gpr[6] = 0u; // WEF_AND
        cpu.gpr[7] = resultAddress;
        return expect(kernel.dispatchEventImport(11u, cpu), "PollEventFlag was not handled") &&
               expect(static_cast<int32_t>(cpu.gpr[2]) == expected, "PollEventFlag returned an unexpected result");
    }

    // thmsgbx: CreateMbx 4, DeleteMbx 5, SendMbx 6, ReceiveMbx 8, PollMbx 9, ReferMbxStatus 11.
    bool testMessageBox()
    {
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();

        const auto call = [&](uint16_t ordinal, uint32_t a0, uint32_t a1 = 0u)
        {
            IopCpuState cpu{};
            cpu.gpr[4] = a0;
            cpu.gpr[5] = a1;
            const bool handled = kernel.dispatchMessageBoxImport(ordinal, cpu);
            return handled ? static_cast<int32_t>(cpu.gpr[2]) : 0x7FFFFFFF;
        };
        const auto createBox = [&](uint32_t attr)
        {
            memory.write32(0x2000u, attr);
            memory.write32(0x2004u, 0u);
            return call(4u, 0x2000u);
        };
        const auto message = [&](uint32_t address, uint8_t priority)
        {
            memory.write32(address, 0u);
            memory.write8(address + 4u, priority);
            return address;
        };
        constexpr uint32_t kMsgvar = 0x2100u;
        const auto poll = [&](int32_t id)
        {
            memory.write32(kMsgvar, 0u);
            const int32_t result = call(9u, kMsgvar, static_cast<uint32_t>(id));
            return result == 0 ? memory.read32(kMsgvar) : static_cast<uint32_t>(result);
        };

        const int32_t fifo = createBox(0u);
        if (!expect(fifo > 0, "CreateMbx did not return an id") ||
            !expect(call(9u, kMsgvar, static_cast<uint32_t>(fifo)) == -424, "PollMbx on an empty box must return KE_MBOX_NOMSG") ||
            !expect(call(6u, 0x7777u, 0x3000u) == -410, "SendMbx to an unknown id must return KE_UNKNOWN_MBXID") ||
            !expect(createBox(0x8u) == -401, "CreateMbx must reject unknown attributes"))
            return false;

        call(6u, static_cast<uint32_t>(fifo), message(0x3000u, 9u));
        call(6u, static_cast<uint32_t>(fifo), message(0x3100u, 1u));
        if (!expect(poll(fifo) == 0x3000u && poll(fifo) == 0x3100u, "A FIFO box must deliver in sending order"))
            return false;

        const int32_t byPriority = createBox(0x4u); // MBA_MSPRI
        call(6u, static_cast<uint32_t>(byPriority), message(0x3200u, 5u));
        call(6u, static_cast<uint32_t>(byPriority), message(0x3300u, 1u));
        call(6u, static_cast<uint32_t>(byPriority), message(0x3400u, 5u));
        memory.write32(0x2200u, 0xFFFFFFFFu);
        if (!expect(call(11u, static_cast<uint32_t>(byPriority), 0x2200u) == 0 && memory.read32(0x220Cu) == 3u &&
                        memory.read32(0x2210u) == 0x3300u,
                    "ReferMbxStatus must report 3 messages with the priority-1 one on top") ||
            !expect(poll(byPriority) == 0x3300u && poll(byPriority) == 0x3200u && poll(byPriority) == 0x3400u,
                    "An MBA_MSPRI box must deliver lowest priority first, equal priorities in sending order"))
            return false;

        // A thread that waits in ReceiveMbx gets the next message sent.
        memory.write32(0x2300u, 0u);      // attr
        memory.write32(0x2304u, 0u);      // option
        memory.write32(0x2308u, 0x1000u); // entry
        memory.write32(0x230Cu, 0x400u);  // stack size
        memory.write32(0x2310u, 0x20u);   // priority
        IopCpuState create{};
        create.gpr[4] = 0x2300u;
        if (!expect(kernel.dispatchThreadImport(4u, create, 0u) && static_cast<int32_t>(create.gpr[2]) > 0, "CreateThread failed"))
            return false;
        IopCpuState start{};
        start.gpr[4] = create.gpr[2];
        if (!expect(kernel.dispatchThreadImport(6u, start, 0u), "StartThread failed"))
            return false;
        IopThread *waiter = kernel.beginNextReady(0u);
        if (!expect(waiter != nullptr, "The thread did not become ready"))
            return false;
        memory.write32(kMsgvar, 0u);
        waiter->cpu.gpr[4] = kMsgvar;
        waiter->cpu.gpr[5] = static_cast<uint32_t>(fifo);
        if (!expect(kernel.dispatchMessageBoxImport(8u, waiter->cpu) && waiter->cpu.yielded &&
                        waiter->state == IopThreadState::MessageBox,
                    "ReceiveMbx on an empty box must make the thread wait"))
            return false;
        waiter->cpu.gpr[2] = 0x55u;
        if (!expect(call(6u, static_cast<uint32_t>(fifo), message(0x3500u, 0u)) == 0, "SendMbx to a waiting thread failed") ||
            !expect(waiter->state == IopThreadState::Ready && memory.read32(kMsgvar) == 0x3500u && waiter->cpu.gpr[2] == 0u,
                    "SendMbx must hand the message to the waiting thread and wake it with KE_OK") ||
            !expect(poll(fifo) == static_cast<uint32_t>(-424), "A message handed to a waiter must not also be queued"))
            return false;

        // DeleteMbx wakes waiters with KE_WAIT_DELETE.
        waiter = kernel.beginNextReady(0u);
        waiter->cpu.gpr[4] = kMsgvar;
        waiter->cpu.gpr[5] = static_cast<uint32_t>(fifo);
        (void)kernel.dispatchMessageBoxImport(8u, waiter->cpu);
        return expect(call(5u, static_cast<uint32_t>(fifo)) == 0, "DeleteMbx failed") &&
               expect(waiter->state == IopThreadState::Ready && static_cast<int32_t>(waiter->cpu.gpr[2]) == -425,
                      "DeleteMbx must wake waiters with KE_WAIT_DELETE") &&
               expect(call(9u, kMsgvar, static_cast<uint32_t>(fifo)) == -410, "A deleted box must be unknown");
    }

    bool testCdvdSpecialControl()
    {
        NullHost host;
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();
        IopCdvd cdvd(host, memory, kernel);
        cdvd.reset();

        constexpr uint32_t param = 0x2000u;
        constexpr uint32_t eventResult = 0x2010u;
        IopCpuState cpu{};
        cpu.gpr[4] = static_cast<uint32_t>(-11); // sceCdSC: return cdvdman interrupt event flag
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "cdvdman:50 was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) > 0, "sceCdSC(-11) did not return a valid event flag"))
            return false;
        const int eventId = static_cast<int>(cpu.gpr[2]);

        if (!pollEvent(kernel, eventId, 0x29u, eventResult, 0) ||
            !expect(memory.read32(eventResult) == 0x29u, "cdvdman event flag did not start with bits 0x29"))
            return false;

        IopCpuState clear{};
        clear.gpr[4] = static_cast<uint32_t>(eventId);
        clear.gpr[5] = ~0x29u;
        if (!expect(kernel.dispatchEventImport(8u, clear), "ClearEventFlag was not handled") ||
            !pollEvent(kernel, eventId, 0x29u, eventResult, -418))
            return false;

        cpu = {};
        cpu.gpr[4] = 0x12345u;
        if (!expect(cdvd.dispatchImport(7u, cpu), "sceCdSeek was not handled") ||
            !pollEvent(kernel, eventId, 0x29u, eventResult, 0))
            return false;

        memory.write8(param, 0x30u);
        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-2);
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(-2) was not handled") ||
            !expect(cpu.gpr[2] == 0x30u, "sceCdSC(-2) did not store the low-byte error"))
            return false;

        memory.write32(param, 0u);
        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-1);
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(-1) was not handled") ||
            !expect(cpu.gpr[2] == 0u, "sceCdSC(-1) returned the wrong initial stream state") ||
            !expect(memory.read32(param) == 0x30u, "sceCdSC(-1) did not publish the last error"))
            return false;

        cpu = {};
        cpu.gpr[4] = 2u;
        cpu.gpr[5] = param;
        if (!expect(cdvd.dispatchImport(50u, cpu), "sceCdSC(2) was not handled") ||
            !expect(cpu.gpr[2] == 2u, "sceCdSC(2) did not update the stream state"))
            return false;

        cpu = {};
        cpu.gpr[4] = static_cast<uint32_t>(-1);
        cpu.gpr[5] = param;
        return expect(cdvd.dispatchImport(50u, cpu), "second sceCdSC(-1) was not handled") &&
               expect(cpu.gpr[2] == 2u, "sceCdSC(-1) did not preserve the stream state");
    }

    bool testCdvdSearchFile()
    {
        const auto suffix = std::to_string(
            static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() / ("ps2x-iop-cdvd-search-" + suffix);
        const std::filesystem::path movieDirectory = root / "MOVIE";
        const std::filesystem::path moviePath = movieDirectory / "OPENING.PSS";
        std::error_code error;
        std::filesystem::create_directories(movieDirectory, error);
        if (!expect(!error, "could not create the temporary CD root"))
            return false;
        {
            std::ofstream movie(moviePath, std::ios::binary);
            movie.write("PSS!", 4);
        }

        CdRootHost host(root);
        IopMemory memory;
        IopKernel kernel(memory);
        kernel.reset();
        IopCdvd cdvd(host, memory, kernel);
        cdvd.reset();

        constexpr uint32_t resultAddress = 0x2400u;
        constexpr uint32_t pathAddress = 0x2480u;
        const char path[] = "cdrom0:\\movie\\opening.pss;1";
        (void)memory.writeRam(pathAddress, path, sizeof(path));

        IopCpuState cpu{};
        cpu.gpr[4] = resultAddress;
        cpu.gpr[5] = pathAddress;
        const bool handled = cdvd.dispatchImport(10u, cpu);
        const bool passed =
            expect(handled, "cdvdman:10 was not handled") &&
            expect(cpu.gpr[2] == 1u, "sceCdSearchFile did not find a case-insensitive ISO path") &&
            expect(memory.read32(resultAddress) >= 20u, "sceCdSearchFile returned an invalid LSN") &&
            expect(memory.read32(resultAddress + 4u) == 4u, "sceCdSearchFile returned the wrong size") &&
            expect(memory.readString(resultAddress + 8u, 16u) == "OPENING.PSS",
                   "sceCdSearchFile returned the wrong file name");

        std::filesystem::remove_all(root, error);
        return passed;
    }

    bool testTimrmanPeriodicCallback()
    {
        IopTimrman timrman;
        timrman.reset();
        IopCpuState cpu{};

        cpu.gpr[4] = 1u; // SYSCLK
        cpu.gpr[5] = 32u;
        cpu.gpr[6] = 1u;
        if (!expect(timrman.dispatchImport(4u, cpu, 100u), "AllocHardTimer was not handled") ||
            !expect(static_cast<int32_t>(cpu.gpr[2]) > 0, "AllocHardTimer did not allocate a 32-bit timer"))
            return false;
        const uint32_t timerId = cpu.gpr[2];

        cpu = {};
        cpu.gpr[4] = timerId;
        cpu.gpr[5] = 100u;
        cpu.gpr[6] = 0x12340u;
        cpu.gpr[7] = 0x45670u;
        cpu.gpr[28] = 0x89AB0u;
        if (!expect(timrman.dispatchImport(20u, cpu, 100u), "SetTimerHandler was not handled") ||
            !expect(cpu.gpr[2] == 0u, "SetTimerHandler failed"))
            return false;

        cpu = {};
        cpu.gpr[4] = timerId;
        cpu.gpr[5] = 1u;
        cpu.gpr[6] = 0u;
        cpu.gpr[7] = 1u;
        if (!expect(timrman.dispatchImport(22u, cpu, 100u), "SetupHardTimer was not handled") ||
            !expect(cpu.gpr[2] == 0u, "SetupHardTimer failed"))
            return false;

        cpu = {};
        cpu.gpr[4] = timerId;
        if (!expect(timrman.dispatchImport(23u, cpu, 100u), "StartHardTimer was not handled") ||
            !expect(cpu.gpr[2] == 0u, "StartHardTimer failed") ||
            !expect(timrman.nextEventCycle(1000u) == 200u, "timer compare was scheduled at the wrong cycle"))
            return false;

        RecordingExecutor executor;
        executor.callbackResult = 100u;
        timrman.serviceDue(199u, executor);
        if (!expect(executor.calls == 0u, "timer callback ran too early"))
            return false;
        timrman.serviceDue(200u, executor);
        return expect(executor.calls == 1u, "timer callback did not run") &&
               expect(executor.lastAddress == 0x12340u, "timer called the wrong handler") &&
               expect(executor.lastArgument == 0x45670u, "timer passed the wrong common argument") &&
               expect(executor.lastGp == 0x89AB0u, "timer callback lost the registering module GP") &&
               expect(timrman.nextEventCycle(1000u) == 300u, "timer callback return did not rearm compare");
    }
}

int main()
{
    if (!testMessageBox() || !testLoadcoreRebootLibraryMode() || !testCdvdSpecialControl() || !testCdvdSearchFile() ||
        !testTimrmanPeriodicCallback())
        return 1;
    std::cout << "ps2xIOP import tests passed\n";
    return 0;
}
