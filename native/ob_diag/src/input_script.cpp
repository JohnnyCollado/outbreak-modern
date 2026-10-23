#include "input_script.h"

#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>

namespace
{
    struct ButtonName
    {
        const char *name;
        uint16_t mask;
    };

    // Active-low pad bits, as ps2_pad.cpp.
    constexpr ButtonName kButtons[] = {
        {"up", 0x0010u},
        {"down", 0x0040u},
        {"left", 0x0080u},
        {"right", 0x0020u},
        {"start", 0x0008u},
        {"select", 0x0001u},
        {"cross", 0x4000u},
        {"circle", 0x2000u},
        {"square", 0x8000u},
        {"triangle", 0x1000u},
        {"l1", 0x0400u},
        {"r1", 0x0800u},
        {"l2", 0x0100u},
        {"r2", 0x0200u},
        {"l3", 0x0002u},
        {"r3", 0x0004u},
    };

    bool parseNumber(const std::string &text, uint32_t &out)
    {
        if (text.empty())
            return false;
        uint64_t value = 0;
        size_t pos = 0;
        unsigned base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            pos = 2;
        }
        for (; pos < text.size(); ++pos)
        {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[pos])));
            unsigned digit = 0;
            if (c >= '0' && c <= '9')
                digit = static_cast<unsigned>(c - '0');
            else if (base == 16 && c >= 'a' && c <= 'f')
                digit = static_cast<unsigned>(c - 'a' + 10);
            else
                return false;
            value = value * base + digit;
            if (value > std::numeric_limits<uint32_t>::max())
                return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    }

    bool parseRef(const std::string &text, Ps2InputScript::MemRef &ref)
    {
        ref = {};
        if (!text.empty() && text[0] == '*')
        {
            ref.deref = true;
            const std::string body = text.substr(1);
            const size_t plus = body.find('+');
            if (plus == std::string::npos)
                return parseNumber(body, ref.addr);
            return parseNumber(body.substr(0, plus), ref.addr) && parseNumber(body.substr(plus + 1), ref.offset);
        }
        return parseNumber(text, ref.addr);
    }

    bool parseWidth(const std::string &text, uint32_t &width)
    {
        return parseNumber(text, width) && (width == 1u || width == 2u || width == 4u);
    }

    bool parseButtons(const std::string &text, uint16_t &mask)
    {
        mask = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            const size_t plus = text.find('+', start);
            const std::string name = text.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
            bool found = false;
            for (const ButtonName &button : kButtons)
            {
                if (name == button.name)
                {
                    mask |= button.mask;
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
            if (plus == std::string::npos)
                break;
            start = plus + 1;
        }
        return mask != 0;
    }

    std::string hex(uint32_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << value;
        return out.str();
    }
}

bool Ps2InputScript::parse(const std::string &text, std::string &error)
{
    m_steps.clear();
    m_timeoutSeconds = 600.0;
    bool sawTimeout = false;

    std::istringstream input(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(input, raw))
    {
        ++lineNo;
        const size_t hash = raw.find('#');
        std::string line = hash == std::string::npos ? raw : raw.substr(0, hash);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back())))
            line.pop_back();
        size_t first = 0;
        while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first])))
            ++first;
        line = line.substr(first);
        if (line.empty())
            continue;

        std::istringstream words(line);
        std::vector<std::string> tok;
        for (std::string w; words >> w;)
            tok.push_back(w);

        auto fail = [&](const std::string &why)
        {
            error = "line " + std::to_string(lineNo) + ": " + why + ": " + line;
            m_steps.clear();
            return false;
        };

        Step step;
        step.line = lineNo;
        step.text = line;
        const std::string &cmd = tok[0];
        if (cmd == "timeout")
        {
            uint32_t secs = 0;
            if (tok.size() != 2 || !parseNumber(tok[1], secs) || secs == 0u)
                return fail("expected 'timeout <secs>'");
            if (sawTimeout || !m_steps.empty())
                return fail("timeout must be a single header before the first step");
            sawTimeout = true;
            m_timeoutSeconds = static_cast<double>(secs);
            continue;
        }
        if (cmd == "wait" && tok.size() == 3 && tok[1] == "vsync")
        {
            step.op = Op::WaitVsync;
            if (!parseNumber(tok[2], step.count))
                return fail("bad vsync count");
        }
        else if (cmd == "wait" && tok.size() == 6 && tok[1] == "mem")
        {
            step.op = Op::WaitMem;
            if (!parseRef(tok[2], step.ref))
                return fail("bad address");
            if (!parseWidth(tok[3], step.width))
                return fail("width must be 1, 2 or 4");
            if (tok[4] != "==" && tok[4] != "!=")
                return fail("operator must be == or !=");
            step.equal = tok[4] == "==";
            if (!parseNumber(tok[5], step.value))
                return fail("bad value");
            if (step.width < 4u && step.value >= (1u << (step.width * 8u)))
                return fail("value does not fit the width");
        }
        else if (cmd == "wait")
        {
            return fail("expected 'wait vsync <n>' or 'wait mem <addr> <width> <op> <value>'");
        }
        else if (cmd == "press")
        {
            step.op = Op::Press;
            if (tok.size() != 3)
                return fail("expected 'press <btn[+btn..]> <held vsyncs>'");
            if (!parseButtons(tok[1], step.mask))
                return fail("unknown button");
            if (!parseNumber(tok[2], step.count))
                return fail("bad held vsync count");
        }
        else if (cmd == "capture")
        {
            step.op = Op::Capture;
            if (tok.size() != 3)
                return fail("expected 'capture <screen> <n>'");
            step.name = tok[1];
            if (step.name.find_first_of("/\\:") != std::string::npos)
                return fail("screen name must not contain path separators");
            if (!parseNumber(tok[2], step.count) || step.count == 0u)
                return fail("bad frame count");
        }
        else if (cmd == "watch")
        {
            step.op = Op::Watch;
            if (tok.size() != 3)
                return fail("expected 'watch <addr|*ptr+off> <width>'");
            if (!parseRef(tok[1], step.ref))
                return fail("bad address");
            if (!parseWidth(tok[2], step.width))
                return fail("width must be 1, 2 or 4");
            step.name = tok[1];
        }
        else if (cmd == "log")
        {
            step.op = Op::Log;
            step.name = line.size() > 3 ? line.substr(4) : std::string();
        }
        else if (cmd == "quit")
        {
            step.op = Op::Quit;
            if (tok.size() != 1)
                return fail("quit takes no arguments");
        }
        else
        {
            return fail("unknown command '" + cmd + "'");
        }
        m_steps.push_back(step);
    }
    error.clear();
    return true;
}

bool Ps2InputScript::loadFile(const std::string &path, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return parse(text.str(), error);
}

bool Ps2InputScript::hasCaptureSteps() const
{
    for (const Step &step : m_steps)
    {
        if (step.op == Op::Capture)
            return true;
    }
    return false;
}

bool Ps2InputScript::readRef(const MemRef &ref, uint32_t width, const ReadMem &read, uint32_t &out) const
{
    uint32_t addr = ref.addr;
    if (ref.deref)
    {
        uint32_t pointer = 0;
        if (!read(ref.addr, 4u, pointer) || pointer == 0u)
            return false;
        addr = pointer + ref.offset;
    }
    return read(addr, width, out);
}

void Ps2InputScript::beginStep(uint64_t tick, const ReadMem &read, std::ostream &log)
{
    const Step &step = m_steps[m_index];
    log << "[script] step " << m_index << " line " << step.line << " begin tick=" << tick << std::endl;
    m_begun = true;
    m_stepTick = tick;
    switch (step.op)
    {
    case Op::Press:
        m_buttons.store(static_cast<uint16_t>(0xFFFFu & ~step.mask), std::memory_order_release);
        break;
    case Op::Capture:
        m_captureActive = true;
        m_captureTaken = 0;
        m_captureWritten = 0;
        m_hasCaptureTick = false;
        m_captureStartTick = tick;
        break;
    case Op::Watch:
    {
        Watch watch;
        watch.ref = step.ref;
        watch.width = step.width;
        watch.label = step.name;
        watch.valid = readRef(watch.ref, watch.width, read, watch.value);
        m_watches.push_back(watch);
        break;
    }
    case Op::Log:
        log << "[script] log " << step.name << std::endl;
        break;
    case Op::Quit:
        m_quit.store(true, std::memory_order_release);
        break;
    default:
        break;
    }
}

void Ps2InputScript::onVSync(uint64_t tick, const ReadMem &read, std::ostream &log)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (Watch &watch : m_watches)
    {
        uint32_t value = 0;
        if (!readRef(watch.ref, watch.width, read, value))
            continue;
        if (watch.valid && value != watch.value)
        {
            log << "[script] watch " << watch.label << ' ' << hex(watch.value) << "->" << hex(value)
                << " tick=" << tick << std::endl;
        }
        watch.value = value;
        watch.valid = true;
    }

    while (m_index < m_steps.size())
    {
        if (!m_begun)
            beginStep(tick, read, log);
        const Step &step = m_steps[m_index];
        const uint64_t elapsed = tick - m_stepTick;
        bool done = false;
        switch (step.op)
        {
        case Op::WaitVsync:
            done = elapsed >= step.count;
            break;
        case Op::WaitMem:
        {
            uint32_t value = 0;
            done = readRef(step.ref, step.width, read, value) && ((value == step.value) == step.equal);
            break;
        }
        case Op::Press:
            if (elapsed >= step.count)
                m_buttons.store(0xFFFFu, std::memory_order_release);
            done = elapsed >= static_cast<uint64_t>(step.count) + 1u;
            break;
        case Op::Capture:
            done = m_captureWritten >= step.count;
            if (done)
                m_captureActive = false;
            break;
        case Op::Quit:
            return;
        default:
            done = true;
            break;
        }
        if (!done)
            return;
        ++m_index;
        m_begun = false;
    }
}

bool Ps2InputScript::takeCapture(uint64_t latchedTick, std::string &fileName, std::string &screen, uint32_t &k)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureActive || m_index >= m_steps.size())
        return false;
    const Step &step = m_steps[m_index];
    if (m_captureTaken >= step.count || latchedTick < m_captureStartTick ||
        (m_hasCaptureTick && m_lastCaptureTick == latchedTick))
        return false;
    k = m_captureTaken++;
    m_lastCaptureTick = latchedTick;
    m_hasCaptureTick = true;
    screen = step.name;
    fileName = step.name + "-" + std::to_string(k) + ".png";
    return true;
}

bool Ps2InputScript::captureWanted() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_captureActive && m_index < m_steps.size() && m_captureWritten < m_steps[m_index].count;
}

void Ps2InputScript::commitCapture(bool written)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (written)
        ++m_captureWritten;
    else if (m_captureTaken > 0u)
        --m_captureTaken;
}

void Ps2InputScript::start(double nowSeconds)
{
    m_startSeconds = nowSeconds;
}

bool Ps2InputScript::timedOut(double nowSeconds, std::string &message) const
{
    if (nowSeconds - m_startSeconds < m_timeoutSeconds)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream out;
    if (m_index < m_steps.size())
    {
        const Step &step = m_steps[m_index];
        out << "[script] TIMEOUT step " << m_index << " line " << step.line << ": " << step.text;
    }
    else
    {
        out << "[script] TIMEOUT step " << m_index << " line 0: end of script without quit";
    }
    message = out.str();
    return true;
}

int Ps2InputScript::loopEndExitCode(std::string &message) const
{
    message.clear();
    if (quitRequested())
        return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ostringstream out;
    if (m_index < m_steps.size())
    {
        const Step &step = m_steps[m_index];
        out << "[script] game ended before quit at step " << m_index << " line " << step.line << ": " << step.text;
    }
    else
    {
        out << "[script] game ended before quit at step " << m_index << " line 0: end of script without quit";
    }
    message = out.str();
    return 4;
}
