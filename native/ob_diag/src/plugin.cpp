// ob_diag: the diagnostics plugin. Implements PS2DiagHooks (runtime/ps2_diag_hooks.h) over the tools that used
// to live in the engine: the scripted input player (PS2X_INPUT_SCRIPT), the frame dump (PS2X_FRAME_DUMP_DIR), the
// frame-time log (PS2X_FRAME_TIME), the guest PC sampler (PS2X_PC_SAMPLE) and the VU1 capture
// (PS2X_VU1_CAPTURE) and the live mock controller on a named pipe (PS2X_PAD_PIPE, pad_control.h). With none of
// those set, ps2_diag_create returns null and the engine runs without hooks.

#include "runtime/ps2_diag_hooks.h"
#include "runtime/ps2_vu1.h"

#include "frame_dump.h"
#include "frame_time.h"
#include "input_script.h"
#include "pad_control.h"
#include "pad_pipe.h"
#include "pc_sampler.h"
#include "vu1_recorder.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#define OB_DIAG_EXPORT extern "C" __declspec(dllexport)
#else
#define OB_DIAG_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace
{
    const char *envOrNull(const char *name)
    {
        const char *v = std::getenv(name);
        return (v && *v) ? v : nullptr;
    }

    double nowSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint64_t nowNs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    class ObDiag final : public PS2DiagHooks
    {
    public:
        explicit ObDiag(const PS2DiagHost &host) : m_host(host) {}

        // Reads the environment. Returns false with exitCode != 0 on a fatal error; active() = something to do.
        bool init(int &exitCode)
        {
            exitCode = 0;
            m_dumpDir = envOrNull("PS2X_FRAME_DUMP_DIR");
            if (const char *path = envOrNull("PS2X_INPUT_SCRIPT"))
            {
                m_script = std::make_unique<Ps2InputScript>();
                std::string error;
                if (!m_script->loadFile(path, error))
                {
                    std::cerr << "[script] parse error in " << path << ": " << error << std::endl;
                    exitCode = 2;
                    return false;
                }
                if (m_script->hasCaptureSteps() && m_dumpDir == nullptr)
                {
                    std::cerr << "[script] capture steps need PS2X_FRAME_DUMP_DIR" << std::endl;
                    exitCode = 2;
                    return false;
                }
                m_script->start(nowSeconds());
                std::cout << "[script] loaded " << path << " steps=" << m_script->steps().size()
                          << " timeout=" << m_script->timeoutSeconds() << "s" << std::endl;
            }
            if (const char *pipe = envOrNull("PS2X_PAD_PIPE"))
            {
                if (m_script)
                {
                    std::cerr << "[pad] PS2X_PAD_PIPE and PS2X_INPUT_SCRIPT both drive the pad: set one" << std::endl;
                    exitCode = 2;
                    return false;
                }
                // Shots: PS2X_PAD_SHOTS, else the frame dump dir (ob.sh boot --pad sets both to frames-<tag>).
                const char *shots = envOrNull("PS2X_PAD_SHOTS");
                const std::string shotDir = shots ? shots : (m_dumpDir ? m_dumpDir : "");
                const PS2DiagHost &host = m_host;
                m_pad = std::make_unique<ObPadControl>(
                    [&host](uint32_t addr, uint32_t width, uint32_t &out)
                    {
                        out = 0u;
                        return host.readGuest(host.user, addr, &out, width);
                    },
                    [&host]() { return host.vsyncTick(host.user); }, shotDir);
                std::string error;
                if (!obStartPadPipe(pipe, *m_pad, error))
                {
                    std::cerr << "[pad] " << error << std::endl;
                    exitCode = 2;
                    return false;
                }
            }
            if (const char *path = envOrNull("PS2X_FRAME_TIME"))
            {
                m_frameTime = std::make_unique<ObFrameTimeLog>();
                if (!m_frameTime->open(path))
                {
                    std::cerr << "[diag] PS2X_FRAME_TIME: cannot open " << path << std::endl;
                    m_frameTime.reset();
                }
            }
            if (const char *path = envOrNull("PS2X_PC_SAMPLE"))
                m_sampler = std::make_unique<ObPcSampler>(path, m_host.vsyncTick, m_host.user);
            m_vu1 = ps2x::vu1capture::Recorder::fromSpec(envOrNull("PS2X_VU1_CAPTURE"));
            return true;
        }

        bool active() const { return m_dumpDir || m_script || m_pad || m_frameTime || m_sampler || m_vu1; }

        // EE executor thread.
        void onVsync(uint64_t tick, uint64_t idleWaitNs) override
        {
            if (m_frameTime)
                m_frameTime->onVsync(tick, nowNs(), idleWaitNs);
            if (m_script)
            {
                const PS2DiagHost &host = m_host;
                m_script->onVSync(tick,
                                  [&host](uint32_t addr, uint32_t width, uint32_t &out)
                                  {
                                      out = 0u;
                                      return host.readGuest(host.user, addr, &out, width);
                                  },
                                  std::cout);
                // Capture steps: hold the guest at this vsync until the main thread has taken its frame, so the
                // captures are tick-exact even unpaced (PS2X_PACING=0, several guest vsyncs per host frame).
                // 2 s cap: a frame that never comes must not hang the run (the step then continues as before).
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (m_script->captureNeedsTick(tick) && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (m_script->captureNeedsTick(tick))
                    std::cerr << "[script] capture hold at tick " << tick << " timed out (2 s): no frame" << std::endl;
            }
            if (m_pad)
                m_pad->onVsync(tick);
        }

        bool wantsFrame(uint64_t) override
        {
            if (m_pad)
                return m_pad->wantsFrame(); // the pad pipe: one frame per shot command, never per frame
            if (m_dumpDir == nullptr)
                return false;
            return m_script ? m_script->captureWanted() : true; // a script captures only in capture steps
        }

        void onGuestEnter(uint32_t *pc, int32_t threadId) override
        {
            if (m_sampler)
                m_sampler->guestEnter(pc, threadId);
        }
        void onGuestLeave() override
        {
            if (m_sampler)
                m_sampler->guestLeave();
        }
        void onSectionEnter(PS2DiagSection section) override
        {
            if (m_sampler)
                m_sampler->sectionEnter(static_cast<uint32_t>(section));
        }
        void onSectionLeave(PS2DiagSection section) override
        {
            if (m_sampler)
                m_sampler->sectionLeave(static_cast<uint32_t>(section));
        }

        void onVu1Begin(const PS2DiagVu1Run &run) override
        {
            if (m_vu1)
                m_vu1->begin(run.kind == 0u ? ps2x::vu1capture::Mscal : ps2x::vu1capture::Mscnt, run.startPc,
                             run.top, run.itop, run.fbrst, run.vpuStat, run.cycleBudget, run.codeGeneration,
                             run.code, run.data, *run.state);
        }
        void onVu1Path1(const uint8_t *packet, uint32_t bytes) override
        {
            if (m_vu1)
                m_vu1->path1(packet, bytes);
        }
        void onVu1End(const uint8_t *data, const VU1State &after, uint32_t vpuStat) override
        {
            if (m_vu1)
                m_vu1->end(data, after, vpuStat);
        }

        void onRunEnd() override
        {
            if (m_sampler)
                m_sampler->stop(); // final CSV; the process may exit before destruction
            if (m_frameTime)
                m_frameTime->flush();
            if (m_vu1)
                m_vu1->flush();
        }

        // Game thread: the script owns the pad (only its held buttons, no host pad or keys).
        // The pad pipe owns port 0 (buttons and sticks); port 1 stays unplugged-idle (all released).
        bool padButtons(int port, uint16_t &buttons) override
        {
            if (m_pad)
            {
                buttons = port == 0 ? m_pad->buttons() : 0xFFFFu;
                return true;
            }
            if (!m_script)
                return false;
            buttons = m_script->buttons();
            return true;
        }
        bool padSticks(int port, uint8_t sticks[4]) override
        {
            if (!m_pad || port != 0)
                return false;
            m_pad->sticks(sticks);
            return true;
        }

        // Main thread.
        void onFrame(uint64_t tick, const uint8_t *rgba, uint32_t width, uint32_t height) override
        {
            if (m_pad)
            {
                m_pad->onFrame(tick, rgba, width, height);
                return;
            }
            if (m_dumpDir == nullptr)
                return;
            if (!m_script)
            {
                obDumpFrame(m_dumpDir, tick, rgba, width, height);
                return;
            }
            // Script mode: dump only while a capture step is active, one <screen>-<k>.png per frame tick.
            std::string fileName;
            std::string screen;
            uint32_t k = 0u;
            if (!m_script->takeCapture(tick, fileName, screen, k))
                return;
            const bool written = obDumpFrameNamed(m_dumpDir, fileName.c_str(), rgba, width, height);
            m_script->commitCapture(written);
            if (written)
                std::cout << "[script] capture " << screen << " k=" << k << " tick=" << tick << std::endl;
            else
                std::cerr << "[script] capture " << screen << " k=" << k << " write failed" << std::endl;
        }

        int onHostFrame(double now) override
        {
            if (m_pad)
                return m_pad->quitRequested() ? 1 : 0;
            if (!m_script)
                return 0;
            std::string message;
            if (m_script->timedOut(now, message))
            {
                std::cout.flush();
                std::cerr << message << std::endl;
                return 3;
            }
            return m_script->quitRequested() ? 1 : 0;
        }

        int onShutdown() override
        {
            if (!m_script)
                return 0;
            std::string message;
            const int code = m_script->loopEndExitCode(message);
            if (code != 0)
            {
                std::cout.flush();
                std::cerr << message << std::endl;
            }
            return code;
        }

    private:
        PS2DiagHost m_host;
        const char *m_dumpDir = nullptr;
        std::unique_ptr<Ps2InputScript> m_script;
        std::unique_ptr<ObPadControl> m_pad;
        std::unique_ptr<ObFrameTimeLog> m_frameTime;
        std::unique_ptr<ObPcSampler> m_sampler;
        std::unique_ptr<ps2x::vu1capture::Recorder> m_vu1;
    };
}

OB_DIAG_EXPORT PS2DiagHooks *ps2_diag_create(const PS2DiagHost *host, int *exitCode)
{
    int code = 0;
    if (host == nullptr || host->apiVersion != PS2_DIAG_API_VERSION || host->vu1StateBytes != sizeof(VU1State) ||
        host->readGuest == nullptr || host->vsyncTick == nullptr)
    {
        std::cerr << "[diag] ob_diag: host API mismatch (api " << (host ? host->apiVersion : 0u) << ", want "
                  << PS2_DIAG_API_VERSION << "; VU1State " << (host ? host->vu1StateBytes : 0u) << " bytes, want "
                  << sizeof(VU1State) << ")" << std::endl;
        if (exitCode)
            *exitCode = 2;
        return nullptr;
    }
    auto diag = std::make_unique<ObDiag>(*host);
    const bool ok = diag->init(code);
    if (exitCode)
        *exitCode = code;
    if (!ok || !diag->active())
        return nullptr;
    std::cout << "[diag] ob_diag active" << std::endl;
    return diag.release(); // lives until the process exits
}
