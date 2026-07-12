#include "pty/ConPty.h"

#include <tlhelp32.h>  // process snapshot for hasRunningChild

#include <utility>
#include <vector>

namespace liney {

ConPty::~ConPty() { stop(); }

bool ConPty::start(const std::wstring& command, short cols, short rows,
                   const std::wstring& cwd, OutputHandler onOutput,
                   ExitHandler onExit) {
    onOutput_ = std::move(onOutput);
    onExit_ = std::move(onExit);

    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite_, nullptr, 0)) return false;
    if (!CreatePipe(&outputRead_, &outputWrite, nullptr, 0)) {
        CloseHandle(inputRead);
        CloseHandle(inputWrite_);
        inputWrite_ = nullptr;
        return false;
    }

    const COORD size{ cols, rows };
    HRESULT hr = CreatePseudoConsole(size, inputRead, outputWrite, 0, &hpc_);
    // The pseudo console now owns these ends.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr)) {
        hpc_ = nullptr;
        return false;
    }

    // Build a STARTUPINFOEX that carries the pseudo-console attribute.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);

    size_t attrBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrBytes);
    auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrBytes));
    if (!attrList) return false;
    si.lpAttributeList = attrList;

    const bool attrInit =
        InitializeProcThreadAttributeList(attrList, 1, 0, &attrBytes) != FALSE;
    bool ok = attrInit &&
              UpdateProcThreadAttribute(
                  attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc_,
                  sizeof(hpc_), nullptr, nullptr);

    if (ok) {
        std::wstring cmd = command;  // CreateProcessW may mutate the buffer.
        const wchar_t* workDir = cwd.empty() ? nullptr : cwd.c_str();
        ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT, nullptr, workDir,
                            &si.StartupInfo, &procInfo_) != FALSE;
    }

    if (attrInit) DeleteProcThreadAttributeList(attrList);
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
        exited_ = true;
        // Wake the UI so a dead shell is reaped promptly (the message loop
        // sleeps until something marks the frame dirty).
        if (onExit_) onExit_();
    });

    writeThread_ = std::thread([this]() {
        std::string pending;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(writeMutex_);
                writeCv_.wait(lock,
                              [this] { return writeStop_ || !writeQueue_.empty(); });
                if (writeStop_ && writeQueue_.empty()) return;
                pending.clear();
                pending.swap(writeQueue_);
            }
            size_t off = 0;
            while (off < pending.size()) {
                DWORD written = 0;
                if (!WriteFile(inputWrite_, pending.data() + off,
                               static_cast<DWORD>(pending.size() - off),
                               &written, nullptr) ||
                    written == 0)
                    return;  // pipe broken (child gone) — drop remaining input
                off += written;
            }
        }
    });
    return true;
}

bool ConPty::hasExited() const {
    if (exited_.load()) return true;
    // ConPTY may hold the output pipe open after the child exits, so the reader
    // never sees EOF — poll the child process itself.
    return procInfo_.hProcess &&
           WaitForSingleObject(procInfo_.hProcess, 0) == WAIT_OBJECT_0;
}

bool ConPty::hasRunningChild() const {
    const DWORD shellPid = procInfo_.dwProcessId;
    if (shellPid == 0) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    // A direct child of the shell means it's running a command (idle cmd /
    // pwsh have no children). One level is enough — the child itself may have
    // grandchildren, but its mere presence already means "busy".
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ParentProcessID == shellPid) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

void ConPty::write(const char* data, size_t len) {
    if (!inputWrite_ || len == 0) return;
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (writeStop_) return;
        writeQueue_.append(data, len);
    }
    writeCv_.notify_one();
}

void ConPty::resize(short cols, short rows) {
    if (hpc_) ResizePseudoConsole(hpc_, COORD{ cols, rows });
}

void ConPty::stop() {
    // Robust, deadlock-free teardown. The naive order (ClosePseudoConsole then
    // join the reader) can hang forever: ClosePseudoConsole blocks until the
    // conhost client flushes its output, but that flush blocks on a full pipe
    // if the reader has stopped draining — the UI thread then wedges inside a
    // thread join and the window closes but the process never exits (a zombie).
    //
    // Instead, force both worker threads out of their blocking I/O with
    // CancelIoEx (so the joins can't depend on ClosePseudoConsole), join them,
    // and only THEN close our pipe handles + the pseudoconsole. With our read
    // end already gone, conhost's writes fail fast and ClosePseudoConsole can't
    // block on a flush.
    running_ = false;

    if (outputRead_) CancelIoEx(outputRead_, nullptr);  // unblock reader ReadFile
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        writeStop_ = true;
        writeQueue_.clear();
    }
    writeCv_.notify_one();
    if (inputWrite_) CancelIoEx(inputWrite_, nullptr);  // unblock writer WriteFile

    if (readThread_.joinable()) readThread_.join();
    if (writeThread_.joinable()) writeThread_.join();

    // Workers are gone; close our handles, then the pseudoconsole last.
    if (inputWrite_) {
        CloseHandle(inputWrite_);
        inputWrite_ = nullptr;
    }
    if (outputRead_) {
        CloseHandle(outputRead_);
        outputRead_ = nullptr;
    }
    if (hpc_) {
        ClosePseudoConsole(hpc_);
        hpc_ = nullptr;
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
