#include "pad_pipe.h"

#include "pad_control.h"

#include <iostream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

namespace
{
    // "D:P(A;;GA;;;<current user SID>)": a protected DACL that lets only this user open the pipe.
    bool currentUserOnly(SECURITY_ATTRIBUTES &sa, std::string &error)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            error = "OpenProcessToken failed, error " + std::to_string(GetLastError());
            return false;
        }
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::string buffer(size, '\0');
        const bool got = size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size);
        CloseHandle(token);
        LPWSTR sid = nullptr;
        if (!got || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
        {
            error = "cannot read the user's SID, error " + std::to_string(GetLastError());
            return false;
        }
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;") + sid + L")";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr))
        {
            error = "cannot build the pipe's security descriptor, error " + std::to_string(GetLastError());
            return false;
        }
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd; // kept for the life of the process
        sa.bInheritHandle = FALSE;
        return true;
    }

    bool writeLine(HANDLE pipe, const std::string &line)
    {
        const std::string out = line + "\n";
        DWORD written = 0;
        return WriteFile(pipe, out.data(), static_cast<DWORD>(out.size()), &written, nullptr) &&
               written == out.size();
    }

    void serve(HANDLE pipe, ObPadControl *control)
    {
        for (;;)
        {
            if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
            {
                Sleep(100);
                continue;
            }
            std::string pending;
            char chunk[512];
            bool open = true;
            while (open)
            {
                DWORD got = 0;
                if (!ReadFile(pipe, chunk, sizeof(chunk), &got, nullptr) || got == 0)
                    break; // client gone
                pending.append(chunk, got);
                size_t nl;
                while ((nl = pending.find('\n')) != std::string::npos)
                {
                    std::string line = pending.substr(0, nl);
                    pending.erase(0, nl + 1);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (line.find_first_not_of(" \t") == std::string::npos)
                        continue;
                    const std::string reply = control->execute(line);
                    std::cout << "[pad] " << line << " -> " << reply << std::endl;
                    if (!writeLine(pipe, reply))
                    {
                        open = false;
                        break;
                    }
                    if (control->quitRequested())
                    {
                        FlushFileBuffers(pipe);
                        open = false;
                        break;
                    }
                }
            }
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
        }
    }
}

bool obStartPadPipe(const std::string &name, ObPadControl &control, std::string &error)
{
    if (name.empty() || name.find_first_of("\\/") != std::string::npos || name.size() > 200)
    {
        error = "bad pipe name '" + name + "'";
        return false;
    }
    SECURITY_ATTRIBUTES sa{};
    if (!currentUserOnly(sa, error))
        return false;
    const std::string full = "\\\\.\\pipe\\" + name;
    HANDLE pipe = CreateNamedPipeA(full.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                   4096, 4096, 0, &sa);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        error = "CreateNamedPipe " + full + " failed, error " + std::to_string(e) +
                (e == ERROR_ACCESS_DENIED ? " (name in use?)" : "");
        return false;
    }
    std::thread(serve, pipe, &control).detach();
    std::cout << "[pad] listening on " << full << " (shots: " << control.shotDir() << ")" << std::endl;
    return true;
}

#else

bool obStartPadPipe(const std::string &, ObPadControl &, std::string &error)
{
    error = "PS2X_PAD_PIPE: named pipes are Windows only";
    return false;
}

#endif
