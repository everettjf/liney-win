#include "pty/ConPty.h"

#include <vector>

namespace liney {

ConPty::~ConPty() { stop(); }

bool ConPty::start(const std::wstring& command, short cols, short rows,
                   OutputHandler onOutput) {
    onOutput_ = std::move(onOutput);

    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite_, nullptr, 0)) return false;
    if (!CreatePipe(&outputRead_, &outputWrite, nullptr, 0)) {
        CloseHandle(inputRead);
        return false;
    }

    const COORD size{ cols, rows };
    HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &hpc_);
    // The pseudo console now owns these ends.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr)) return false;

    // Build a STARTUPINFOEX that carries the pseudo-console attribute.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);

    size_t attrBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrBytes));
    if (!attrList) return false;
    si.lpAttributeList = attrList;

    bool ok = InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes) &&
              UpdateProcThreadAttribute(
                  attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc_,
                  sizeof(hpc_), nullptr, nullptr);

    if (ok) {
        std::wstring cmd = command;  // CreateProcessW may mutate the buffer.
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                            &si.StartupInfo, &procInfo_) != FALSE;
    }

    DeleteProcThreadAttributeList(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);
    if (!ok) return false;

    running_ = true;
    readThread_ = std::thread([this]() {
        std::vector<char> buf(4096);
        DWORD read = 0;
        while (running_) {
            if (!ReadFile(outputRead_, buf.data(),
                          static_cast<DWORD>(buf.size()), &read, nullptr) ||
                read == 0) {
                break;
            }
            if (onOutput_) onOutput_(buf.data(), read);
        }
    });
    return true;
}

void ConPty::write(const char* data, size_t len) {
    if (!inputWrite_) return;
    DWORD written = 0;
    WriteFile(inputWrite_, data, static_cast<DWORD>(len), &written, nullptr);
}

void ConPty::resize(short cols, short rows) {
    if (hpc_) ResizePseudoConsole(hpc_, COORD{ cols, rows });
}

void ConPty::stop() {
    running_ = false;
    // Closing the pseudo console unblocks the reader's ReadFile.
    if (hpc_) {
        ClosePseudoConsole(hpc_);
        hpc_ = nullptr;
    }
    if (readThread_.joinable()) readThread_.join();
    if (inputWrite_) {
        CloseHandle(inputWrite_);
        inputWrite_ = nullptr;
    }
    if (outputRead_) {
        CloseHandle(outputRead_);
        outputRead_ = nullptr;
    }
    if (procInfo_.hProcess) {
        CloseHandle(procInfo_.hProcess);
        procInfo_.hProcess = nullptr;
    }
    if (procInfo_.hThread) {
        CloseHandle(procInfo_.hThread);
        procInfo_.hThread = nullptr;
    }
}

} // namespace liney
