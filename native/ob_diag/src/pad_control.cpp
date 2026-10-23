#include "pad_control.h"

#include "frame_dump.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
    struct ButtonName
    {
        const char *name;
        uint16_t mask; // libpad bit (the pad word is active-low)
    };
    constexpr ButtonName kButtons[] = {
        {"up", 0x0010u},     {"down", 0x0040u},   {"left", 0x0080u},     {"right", 0x0020u},
        {"start", 0x0008u},  {"select", 0x0001u}, {"cross", 0x4000u},    {"circle", 0x2000u},
        {"square", 0x8000u}, {"triangle", 0x1000u}, {"l1", 0x0400u},     {"r1", 0x0800u},
        {"l2", 0x0100u},     {"r2", 0x0200u},     {"l3", 0x0002u},       {"r3", 0x0004u},
    };

    constexpr uint32_t kDefaultPressFrames = 6u;
    constexpr uint32_t kShotRetryVsyncs = 120u; // no frame this long after a demand: ask again
    constexpr uint32_t kShotMaxDemands = 4u;

    std::vector<std::string> tokens(const std::string &line)
    {
        std::vector<std::string> out;
        std::stringstream ss(line);
        std::string t;
        while (ss >> t)
            out.push_back(t);
        return out;
    }

    std::string lower(std::string s)
    {
        for (char &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    bool parseU32(const std::string &text, uint32_t &out)
    {
        if (text.empty())
            return false;
        char *end = nullptr;
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        const unsigned long long v = std::strtoull(text.c_str(), &end, hex ? 16 : 10);
        if (*end != '\0' || v > 0xFFFFFFFFull)
            return false;
        out = static_cast<uint32_t>(v);
        return true;
    }

    bool parseAxis(const std::string &text, uint8_t &out)
    {
        char *end = nullptr;
        const float v = std::strtof(text.c_str(), &end);
        if (text.empty() || *end != '\0' || !(v >= -1.0f && v <= 1.0f))
            return false;
        out = static_cast<uint8_t>(128 + v * 127); // the engine's axis conversion
        return true;
    }

    std::string hex(uint32_t v, int digits)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%0*X", digits, v);
        return buf;
    }

    bool cleanName(const std::string &name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        for (char c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
                return false;
        return name.find("..") == std::string::npos;
    }

    const char *kUsage = "press hold release stick wait tick shot peek quit";
}

ObPadControl::ObPadControl(ReadFn read, TickFn tick, std::string shotDir, WriteFn write)
    : m_read(std::move(read)), m_tick(std::move(tick)), m_shotDir(std::move(shotDir)), m_write(std::move(write))
{
    if (!m_write)
        m_write = [](const std::string &path, const uint8_t *rgba, uint32_t w, uint32_t h)
        {
            const std::filesystem::path p(path);
            return obDumpFrameNamed(p.parent_path().string().c_str(), p.filename().string().c_str(), rgba, w, h);
        };
}

bool ObPadControl::parseButtons(const std::string &text, uint16_t &mask)
{
    mask = 0;
    std::stringstream ss(lower(text));
    std::string name;
    while (std::getline(ss, name, '+'))
    {
        bool found = false;
        for (const ButtonName &b : kButtons)
            if (name == b.name)
            {
                mask |= b.mask;
                found = true;
            }
        if (!found)
            return false;
    }
    return mask != 0;
}

void ObPadControl::sticks(uint8_t out[4]) const
{
    const uint32_t s = m_sticks.load(std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<uint8_t>(s >> (8 * i));
}

void ObPadControl::setStick(int stick, uint8_t x, uint8_t y)
{
    uint32_t s = m_sticks.load(std::memory_order_relaxed);
    const int shift = stick == 0 ? 16 : 0; // lx/ly at bytes 2-3, rx/ry at 0-1
    s &= ~(0xFFFFu << shift);
    s |= (static_cast<uint32_t>(x) | (static_cast<uint32_t>(y) << 8)) << shift;
    m_sticks.store(s, std::memory_order_relaxed);
}

void ObPadControl::publishDown()
{
    m_down.store(static_cast<uint32_t>(m_holdMask | m_pressMask), std::memory_order_relaxed);
}

void ObPadControl::finishLocked(const std::string &reply)
{
    m_cmd = Command{};
    m_reply = reply;
    m_replyReady = true;
    m_shotOpen.store(false);
    m_demand.store(false);
    m_busy.store(false, std::memory_order_release);
    m_cv.notify_all();
}

ObPadControl::Submit ObPadControl::submit(const std::string &line, std::string &reply)
{
    const std::vector<std::string> tok = tokens(line);
    if (tok.empty())
    {
        reply = std::string("err empty command (") + kUsage + ")";
        return Submit::Done;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_busy.load())
    {
        reply = "err busy: one command at a time";
        return Submit::Done;
    }
    const std::string op = lower(tok[0]);
    Command c;
    c.text = line;
    const size_t n = tok.size();

    if (op == "press" || op == "hold")
    {
        const bool press = op == "press";
        if (n < 2 || n > 3 || (!press && n != 3))
        {
            reply = press ? "err usage: press <btn> [frames]" : "err usage: hold <btn> <frames>";
            return Submit::Done;
        }
        if (!parseButtons(tok[1], c.mask))
        {
            reply = "err unknown button '" + tok[1] + "'";
            return Submit::Done;
        }
        c.frames = kDefaultPressFrames;
        if (n == 3 && !parseU32(tok[2], c.frames))
        {
            reply = "err bad frames '" + tok[2] + "'";
            return Submit::Done;
        }
        if (press && c.frames == 0u)
        {
            reply = "err press needs frames >= 1";
            return Submit::Done;
        }
        c.kind = press ? Kind::Press : Kind::Hold;
    }
    else if (op == "release")
    {
        if (n != 2)
        {
            reply = "err usage: release <btn>|all";
            return Submit::Done;
        }
        if (lower(tok[1]) == "all")
        {
            m_holdMask = m_pressMask = 0;
            m_sticks.store(0x80808080u, std::memory_order_relaxed);
        }
        else
        {
            uint16_t mask = 0;
            if (!parseButtons(tok[1], mask))
            {
                reply = "err unknown button '" + tok[1] + "'";
                return Submit::Done;
            }
            m_holdMask = static_cast<uint16_t>(m_holdMask & ~mask);
        }
        publishDown();
        reply = "ok release " + lower(tok[1]);
        return Submit::Done;
    }
    else if (op == "stick")
    {
        if (n != 5)
        {
            reply = "err usage: stick <left|right> <x> <y> <frames>";
            return Submit::Done;
        }
        const std::string which = lower(tok[1]);
        if (which != "left" && which != "right")
        {
            reply = "err stick must be left or right";
            return Submit::Done;
        }
        c.stick = which == "left" ? 0 : 1;
        if (!parseAxis(tok[2], c.x) || !parseAxis(tok[3], c.y))
        {
            reply = "err x and y must be numbers in -1..1";
            return Submit::Done;
        }
        if (!parseU32(tok[4], c.frames))
        {
            reply = "err bad frames '" + tok[4] + "'";
            return Submit::Done;
        }
        c.kind = Kind::Stick;
    }
    else if (op == "wait")
    {
        if (n != 2 || !parseU32(tok[1], c.frames))
        {
            reply = "err usage: wait <frames>";
            return Submit::Done;
        }
        c.kind = Kind::Wait;
    }
    else if (op == "tick")
    {
        reply = "ok tick=" + std::to_string(m_tick ? m_tick() : 0u);
        return Submit::Done;
    }
    else if (op == "shot")
    {
        if (n > 2 || (n == 2 && !cleanName(tok[1])))
        {
            reply = "err usage: shot [name] (letters, digits, - _ .)";
            return Submit::Done;
        }
        if (m_shotDir.empty())
        {
            reply = "err no shot directory (PS2X_PAD_SHOTS)";
            return Submit::Done;
        }
        c.name = n == 2 ? tok[1] : std::string();
        c.kind = Kind::Shot;
    }
    else if (op == "peek")
    {
        uint32_t width = 0;
        if (n != 3 || !parseU32(tok[2], width) || (width != 1u && width != 2u && width != 4u))
        {
            reply = "err usage: peek <addr|*ptr+off> <1|2|4>";
            return Submit::Done;
        }
        std::string a = tok[1];
        uint32_t addr = 0, off = 0;
        bool ok = true;
        if (!a.empty() && a[0] == '*')
        {
            const size_t plus = a.find('+');
            uint32_t ptr = 0;
            ok = parseU32(a.substr(1, plus == std::string::npos ? std::string::npos : plus - 1), ptr) &&
                 (plus == std::string::npos || parseU32(a.substr(plus + 1), off)) && m_read && m_read(ptr, 4u, addr);
            addr += off;
        }
        else
            ok = parseU32(a, addr);
        uint32_t value = 0;
        if (!ok || !m_read || !m_read(addr, width, value))
        {
            reply = "err cannot read " + a;
            return Submit::Done;
        }
        reply = "ok peek " + hex(addr, 8) + " " + std::to_string(width) + " = " + hex(value, static_cast<int>(width * 2));
        return Submit::Done;
    }
    else if (op == "quit")
    {
        m_quit.store(true, std::memory_order_release);
        reply = "ok quit";
        return Submit::Done;
    }
    else
    {
        reply = "err unknown command '" + tok[0] + "' (" + kUsage + ")";
        return Submit::Done;
    }

    m_cmd = c;
    m_replyReady = false;
    m_busy.store(true, std::memory_order_release);
    return Submit::Pending;
}

bool ObPadControl::pollReply(std::string &reply)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_replyReady)
        return false;
    reply = m_reply;
    m_replyReady = false;
    return true;
}

bool ObPadControl::waitReply(std::string &reply, uint32_t timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_replyReady; }))
        return false;
    reply = m_reply;
    m_replyReady = false;
    return true;
}

std::string ObPadControl::execute(const std::string &line)
{
    std::string reply;
    if (submit(line, reply) == Submit::Done)
        return reply;
    while (!waitReply(reply, 1000u))
    {
    }
    return reply;
}

void ObPadControl::onVsync(uint64_t tick)
{
    if (!m_busy.load(std::memory_order_acquire))
        return; // idle
    std::lock_guard<std::mutex> lock(m_mutex);
    Command &c = m_cmd;
    if (c.kind == Kind::None)
        return;
    if (!c.started)
    {
        c.started = true;
        c.startTick = tick;
        c.remaining = c.frames;
        switch (c.kind)
        {
        case Kind::Press:
            m_pressMask = static_cast<uint16_t>(m_pressMask | c.mask);
            publishDown();
            return; // frames >= 1
        case Kind::Hold:
            m_holdMask = static_cast<uint16_t>(m_holdMask | c.mask);
            publishDown();
            break;
        case Kind::Stick:
            setStick(c.stick, c.x, c.y);
            break;
        case Kind::Wait:
            break;
        case Kind::Shot:
            if (c.name.empty())
                c.name = "shot-" + std::to_string(tick);
            c.demandTick = tick;
            c.demands = 1;
            m_shotOpen.store(true);
            m_demand.store(true);
            return;
        default:
            break;
        }
        if (c.remaining == 0u)
        {
            finishLocked("ok " + c.text + " tick=" + std::to_string(tick));
            return;
        }
        return;
    }
    if (c.kind == Kind::Shot)
    {
        if (tick - c.demandTick >= kShotRetryVsyncs)
        {
            if (c.demands >= kShotMaxDemands)
            {
                finishLocked("err shot: no frame after " + std::to_string(c.demands) + " demands");
                return;
            }
            ++c.demands;
            c.demandTick = tick;
            m_demand.store(true);
        }
        return;
    }
    if (c.remaining > 0u)
        --c.remaining;
    if (c.remaining > 0u)
        return;
    if (c.kind == Kind::Press)
    {
        m_pressMask = static_cast<uint16_t>(m_pressMask & ~c.mask);
        publishDown();
    }
    else if (c.kind == Kind::Stick)
        setStick(c.stick, 0x80u, 0x80u);
    finishLocked("ok " + c.text + " tick=" + std::to_string(tick));
}

bool ObPadControl::wantsFrame()
{
    if (!m_demand.load(std::memory_order_acquire))
        return false;
    return m_demand.exchange(false);
}

void ObPadControl::onFrame(uint64_t tick, const uint8_t *rgba, uint32_t width, uint32_t height)
{
    if (!m_shotOpen.load(std::memory_order_acquire))
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    Command &c = m_cmd;
    if (c.kind != Kind::Shot || !c.started || tick < c.demandTick)
        return; // a frame from before the demand
    std::string name = c.name;
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".png") != 0)
        name += ".png";
    const std::string path = (std::filesystem::path(m_shotDir) / name).string();
    if (!m_write(path, rgba, width, height))
    {
        finishLocked("err shot: cannot write " + path);
        return;
    }
    m_framesWritten.fetch_add(1u);
    finishLocked("ok shot " + path + " tick=" + std::to_string(tick) + " " + std::to_string(width) + "x" +
                 std::to_string(height));
}
