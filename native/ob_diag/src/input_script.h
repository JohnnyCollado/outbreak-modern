#ifndef OB_DIAG_INPUT_SCRIPT_H
#define OB_DIAG_INPUT_SCRIPT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

// Scripted pad input (PS2X_INPUT_SCRIPT=<file>), part of the ob_diag plugin. One step per line, '#' comments, numbers dec or 0x:
//   timeout <secs>                        header, whole-script wall clock (default 600)
//   wait vsync <n>
//   wait mem <ref> <1|2|4> <==|!=> <value>  ref = <addr> or *<ptraddr>+<off> (null pointer = not satisfied)
//   press <btn[+btn..]> <held vsyncs>       then released for 1 vsync before the next step
//   capture <screen> <n>                  completes after n frames dumped as <screen>-<k>.png
//   watch <ref> <1|2|4>                   non-blocking; logs every change for the rest of the run
//   log <text>
//   quit
// Threads: onVSync (vsync thread) steps; buttons() (game thread) reads; takeCapture/commitCapture and
// timedOut (main thread). No raylib dependency.
class Ps2InputScript
{
public:
    enum class Op
    {
        WaitVsync,
        WaitMem,
        Press,
        Capture,
        Watch,
        Log,
        Quit
    };

    struct MemRef
    {
        uint32_t addr = 0;
        bool deref = false;
        uint32_t offset = 0;
    };

    struct Step
    {
        Op op = Op::Log;
        int line = 0;
        std::string text;
        MemRef ref;
        uint32_t width = 4;
        bool equal = true;
        uint32_t value = 0;
        uint16_t mask = 0;     // press: pressed buttons (bits of the active-low pad word)
        uint32_t count = 0;    // wait vsync n, press held vsyncs, capture n
        std::string name;      // capture screen, log text
    };

    // Reads <width> bytes of guest memory at a guest address; false when out of range.
    using ReadMem = std::function<bool(uint32_t addr, uint32_t width, uint32_t &out)>;

    // Parses a whole script; on failure returns false with "line <L>: <reason>" in error.
    bool parse(const std::string &text, std::string &error);
    bool loadFile(const std::string &path, std::string &error);

    const std::vector<Step> &steps() const { return m_steps; }
    double timeoutSeconds() const { return m_timeoutSeconds; }
    bool hasCaptureSteps() const;

    // Vsync thread: advance the script at guest vsync <tick>.
    void onVSync(uint64_t tick, const ReadMem &read, std::ostream &log);

    // Game thread: active-low pad button word (0xFFFF = nothing held).
    uint16_t buttons() const { return m_buttons.load(std::memory_order_acquire); }
    bool quitRequested() const { return m_quit.load(std::memory_order_acquire); }

    // Main thread: when a capture step is active and <latchedTick> is new, reserves frame k and
    // returns true with the file name "<screen>-<k>.png"; commitCapture(true) counts the frame as
    // written, commitCapture(false) gives k back for the next tick.
    // Frames older than the capture step's first tick are refused (an external GS frame can be stale).
    bool takeCapture(uint64_t latchedTick, std::string &fileName, std::string &screen, uint32_t &k);
    void commitCapture(bool written);
    // Any thread: a capture step is active and still needs frames (the external GS snapshot demand).
    bool captureWanted() const;
    // Vsync thread: a capture step still needs a frame and the frame of vsync <tick> - 1 (published after the
    // previous onVsync) has not been taken yet; never at the step's first vsync. The plugin
    // holds the guest at this vsync until it is false, so captures land on consecutive guest ticks however fast
    // the guest runs (PS2X_PACING=0) relative to the main thread's frames.
    bool captureNeedsTick(uint64_t tick) const;

    // Main thread: wall-clock timeout, measured from start(). On timeout, message holds
    // "[script] TIMEOUT step <i> line <L>: <text>".
    void start(double nowSeconds);
    bool timedOut(double nowSeconds, std::string &message) const;

    // Main thread, after the run loop ends: 0 once quit was reached; otherwise 4 with message
    // "[script] game ended before quit at step <i> line <L>: <text>".
    int loopEndExitCode(std::string &message) const;

private:
    struct Watch
    {
        MemRef ref;
        uint32_t width = 4;
        bool valid = false;
        uint32_t value = 0;
        std::string label;
    };

    bool readRef(const MemRef &ref, uint32_t width, const ReadMem &read, uint32_t &out) const;
    void beginStep(uint64_t tick, const ReadMem &read, std::ostream &log);

    std::vector<Step> m_steps;
    double m_timeoutSeconds = 600.0;
    double m_startSeconds = 0.0;

    mutable std::mutex m_mutex;
    size_t m_index = 0;
    bool m_begun = false;
    uint64_t m_stepTick = 0;
    std::vector<Watch> m_watches;
    bool m_captureActive = false;
    uint32_t m_captureTaken = 0;
    uint32_t m_captureWritten = 0;
    uint64_t m_lastCaptureTick = 0;
    uint64_t m_captureStartTick = 0;
    bool m_hasCaptureTick = false;

    std::atomic<uint16_t> m_buttons{0xFFFFu};
    std::atomic<bool> m_quit{false};
};

#endif
