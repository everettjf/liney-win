#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace liney {

// Minimal ConPTY wrapper: spawns a child shell attached to a Windows pseudo
// console and streams its output via a callback. The output bytes are fed to
// the terminal core (src/vt), which maintains the screen grid the renderer
// draws; keystrokes are sent back via write().
class ConPty {
public:
    using OutputHandler = std::function<void(const char* data, size_t len)>;
    using ExitHandler = std::function<void()>;

    ConPty() = default;
    ~ConPty();

    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    // Spawn `command` (e.g. L"cmd.exe") attached to a pseudo console of the
    // given size, starting in `cwd` (empty = inherit). `onOutput` is invoked
    // from a reader thread. `onExit` (optional) is invoked once from the
    // reader thread when the child's output stream ends.
    bool start(const std::wstring& command, short cols, short rows,
               const std::wstring& cwd, OutputHandler onOutput,
               ExitHandler onExit = nullptr);

    // Queue bytes for the child's stdin. Non-blocking: a dedicated writer
    // thread drains the queue, so a full pipe (child not reading during a
    // large paste) can't freeze the caller (the UI thread).
    void write(const char* data, size_t len);
    void resize(short cols, short rows);
    void stop();

    // True once the child shell has exited. Checks the child process handle as
    // well as reader EOF — ConPTY often keeps the output pipe open after the
    // child exits (e.g. after `exit`), so EOF alone is not enough.
    bool hasExited() const;

    // True if the shell has any live child process — i.e. it's running a
    // command rather than sitting idle at the prompt. Used to warn before
    // closing a tab/pane that's doing work. Best-effort (a process snapshot).
    bool hasRunningChild() const;

private:
    HPCON hpc_ = nullptr;
    HANDLE inputWrite_ = nullptr;  // we write -> child stdin
    HANDLE outputRead_ = nullptr;  // child stdout -> we read
    PROCESS_INFORMATION procInfo_{};
    std::thread readThread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> exited_{ false };
    OutputHandler onOutput_;
    ExitHandler onExit_;

    // Writer queue: write() appends under writeMutex_, writeThread_ drains.
    std::thread writeThread_;
    std::mutex writeMutex_;
    std::condition_variable writeCv_;
    std::string writeQueue_;
    bool writeStop_ = false;
};

} // namespace liney
