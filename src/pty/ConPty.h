#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace liney {

// Minimal ConPTY wrapper: spawns a child shell attached to a Windows pseudo
// console and streams its output via a callback.
//
// Implemented and self-contained, but NOT yet wired into the renderer - that
// arrives with libghostty-vt integration (milestone S1). The output bytes will
// be fed to libghostty-vt, which maintains the screen grid the renderer draws.
class ConPty {
public:
    using OutputHandler = std::function<void(const char* data, size_t len)>;

    ConPty() = default;
    ~ConPty();

    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    // Spawn `command` (e.g. L"cmd.exe") attached to a pseudo console of the
    // given size. `onOutput` is invoked from a reader thread.
    bool start(const std::wstring& command, short cols, short rows,
               OutputHandler onOutput);

    void write(const char* data, size_t len);
    void resize(short cols, short rows);
    void stop();

private:
    HPCON hpc_ = nullptr;
    HANDLE inputWrite_ = nullptr;  // we write -> child stdin
    HANDLE outputRead_ = nullptr;  // child stdout -> we read
    PROCESS_INFORMATION procInfo_{};
    std::thread readThread_;
    std::atomic<bool> running_{ false };
    OutputHandler onOutput_;
};

} // namespace liney
