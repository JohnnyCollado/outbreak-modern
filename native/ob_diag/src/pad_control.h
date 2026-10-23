#ifndef OB_DIAG_PAD_CONTROL_H
#define OB_DIAG_PAD_CONTROL_H

// Live mock controller (PS2X_PAD_PIPE): line commands from a client drive pad port 0 on guest vsyncs. The pipe
// server (pad_pipe.h) feeds lines in; this class holds the pad state and the one command in flight.
//
// Commands (one reply line each, "ok ..." or "err ..."; frames = guest vsyncs; <btn> = up down left right cross
// circle square triangle l1 r1 l2 r2 l3 r3 start select, or several joined with '+'):
//   press <btn> [frames]        held from the next vsync for frames (default 6), then released; replies when released
//   hold <btn> <frames>         held from the next vsync; replies after frames; stays held until release
//   release <btn>|all           releases at once (all = every button and both sticks centred)
//   stick <left|right> <x> <y> <frames>   x, y in -1..1 (y -1 = up) from the next vsync for frames, then centred;
//                               frames 0 = stays until release all
//   wait <frames>               replies after frames
//   tick                        the current vsync tick
//   shot [name]                 asks the engine for exactly one frame and writes <shot dir>/<name>.png
//                               (default shot-<tick>); replies with the path
//   peek <addr> <width>         guest RAM read, width 1/2/4; addr may be *ptr+off (reads the u32 at ptr first)
//   quit                        replies, then ends the run (exit 0)
// Threads: submit/pollReply/waitReply on the pipe thread, onVsync/wantsFrame on the EE executor, buttons/sticks on
// the game thread, onFrame on the main thread. Idle (no command in flight) onVsync is one atomic load.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

class ObPadControl
{
public:
    using ReadFn = std::function<bool(uint32_t addr, uint32_t width, uint32_t &out)>;
    using TickFn = std::function<uint64_t()>;
    using WriteFn = std::function<bool(const std::string &path, const uint8_t *rgba, uint32_t w, uint32_t h)>;

    ObPadControl(ReadFn read, TickFn tick, std::string shotDir, WriteFn write = {});

    enum class Submit
    {
        Done,   // reply already set
        Pending // runs on vsyncs (or waits for a frame); pollReply / waitReply
    };
    // Pipe thread. One command at a time: a submit while one is pending is refused ("err busy").
    Submit submit(const std::string &line, std::string &reply);
    bool pollReply(std::string &reply);
    bool waitReply(std::string &reply, uint32_t timeoutMs);
    // submit + waitReply until done (the pipe thread's call).
    std::string execute(const std::string &line);

    // EE executor thread.
    void onVsync(uint64_t tick);
    bool wantsFrame(); // true exactly once per shot demand
    // Main thread: a delivered frame; written only for a shot that asked for one.
    void onFrame(uint64_t tick, const uint8_t *rgba, uint32_t width, uint32_t height);

    // Game thread.
    uint16_t buttons() const { return static_cast<uint16_t>(~m_down.load(std::memory_order_relaxed)); }
    void sticks(uint8_t out[4]) const; // rx, ry, lx, ly

    bool quitRequested() const { return m_quit.load(std::memory_order_acquire); }
    uint32_t framesWritten() const { return m_framesWritten.load(); }
    const std::string &shotDir() const { return m_shotDir; }

    static bool parseButtons(const std::string &text, uint16_t &mask);

private:
    enum class Kind
    {
        None,
        Press,
        Hold,
        Stick,
        Wait,
        Shot
    };
    struct Command
    {
        Kind kind = Kind::None;
        uint16_t mask = 0;
        int stick = 0; // 0 left, 1 right
        uint8_t x = 0x80, y = 0x80;
        uint32_t frames = 0;
        std::string name;
        std::string text; // the command as given, for the reply
        bool started = false;
        uint32_t remaining = 0;
        uint64_t startTick = 0;
        uint64_t demandTick = 0;
        uint32_t demands = 0;
    };

    void finishLocked(const std::string &reply);
    void setStick(int stick, uint8_t x, uint8_t y);
    void publishDown();

    ReadFn m_read;
    TickFn m_tick;
    std::string m_shotDir;
    WriteFn m_write;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    Command m_cmd;
    bool m_replyReady = false;
    std::string m_reply;

    std::atomic<bool> m_busy{false};      // a command runs on vsyncs or waits for a frame
    std::atomic<bool> m_demand{false};    // a frame demand for wantsFrame
    std::atomic<bool> m_shotOpen{false};  // onFrame may write
    std::atomic<uint32_t> m_down{0};      // pressed buttons (active-high), hold | press
    uint16_t m_holdMask = 0, m_pressMask = 0; // under m_mutex
    std::atomic<uint32_t> m_sticks{0x80808080u}; // rx | ry << 8 | lx << 16 | ly << 24
    std::atomic<bool> m_quit{false};
    std::atomic<uint32_t> m_framesWritten{0};
};

#endif
