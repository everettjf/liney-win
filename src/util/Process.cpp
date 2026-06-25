#include "util/Process.h"

#include <windows.h>

#include <string>
#include <vector>

namespace liney {

std::wstring runCapture(const std::wstring& commandLine, const std::wstring& cwd,
                        bool* ok) {
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

    std::string out;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
        out.append(buf, read);
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (ok) *ok = (code == 0);

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
