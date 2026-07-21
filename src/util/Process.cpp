#include "util/Process.h"

#include <windows.h>

#include <string>
#include <vector>

namespace liney {

std::wstring runCapture(const std::wstring& commandLine, const std::wstring& cwd,
                        bool* ok, unsigned long timeoutMs) {
    if (ok) *ok = false;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return L"";
    // The child inherits only the write end; keep our read end private.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;  // CreateProcessW may mutate the buffer.
    const wchar_t* workDir = cwd.empty() ? nullptr : cwd.c_str();
    BOOL launched = CreateProcessW(
        nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        workDir, &si, &pi);

    CloseHandle(writePipe);  // we only read; child owns its copy
    if (!launched) {
        CloseHandle(readPipe);
        return L"";
    }

    constexpr size_t kMaxCaptureBytes = 4 * 1024 * 1024;
    std::string out;
    char buf[4096];
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    bool timedOut = false;
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) &&
            available > 0) {
            DWORD read = 0;
            const DWORD wanted = available < sizeof(buf) ? available : sizeof(buf);
            if (ReadFile(readPipe, buf, wanted, &read, nullptr) && read > 0) {
                const size_t room = kMaxCaptureBytes - out.size();
                out.append(buf, read < room ? read : room);
            }
        }
        if (WaitForSingleObject(pi.hProcess, 25) == WAIT_OBJECT_0) break;
        if (GetTickCount64() >= deadline) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 124);
            WaitForSingleObject(pi.hProcess, 2000);
            break;
        }
    }
    // Drain bytes written immediately before process exit.
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) ||
            available == 0) break;
        DWORD read = 0;
        const DWORD wanted = available < sizeof(buf) ? available : sizeof(buf);
        if (!ReadFile(readPipe, buf, wanted, &read, nullptr) || read == 0) break;
        const size_t room = kMaxCaptureBytes - out.size();
        out.append(buf, read < room ? read : room);
    }
    CloseHandle(readPipe);

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (ok) *ok = (!timedOut && code == 0);

    if (out.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, out.data(),
                                   static_cast<int>(out.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, out.data(), static_cast<int>(out.size()),
                        wide.data(), wlen);
    return wide;
}

void runDetached(const std::wstring& commandLine, const std::wstring& cwd) {
    if (commandLine.empty()) return;
    std::wstring cmd = L"cmd /c " + commandLine;  // CreateProcessW may mutate it
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const wchar_t* workDir = cwd.empty() ? nullptr : cwd.c_str();
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, workDir, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

} // namespace liney
