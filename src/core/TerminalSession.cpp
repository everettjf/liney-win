#include "core/TerminalSession.h"

#include <cwchar>
#include <cwctype>
#include <cstdlib>

#include "core/RenderSignal.h"
#include "core/CommandHistory.h"

namespace liney {

namespace {
// Last path component, for a short tab/pane title.
std::wstring basename(const std::wstring& path) {
    if (path.empty()) return L"shell";
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    size_t start = path.find_last_of(L"\\/", end ? end - 1 : 0);
    start = (start == std::wstring::npos) ? 0 : start + 1;
    std::wstring name = path.substr(start, end - start);
    return name.empty() ? path : name;
}
} // namespace

bool TerminalSession::start(const std::wstring& shell, const std::wstring& cwd,
                            int cols, int rows, int scrollback) {
    cwd_ = cwd;
    shell_ = shell;
    title_ = basename(cwd);
    cols_ = cols;
    rows_ = rows;

    if (!terminal_.create(cols, rows, scrollback)) return false;
    // Query responses the core emits (DSR/CPR, DA, DECRQM…) go back to the
    // child via the PTY, like a real terminal.
    terminal_.setPtyWriter(
        [this](const char* data, size_t len) { pty_.write(data, len); });
    const bool ok = pty_.start(
        shell, static_cast<short>(cols), static_cast<short>(rows), cwd,
        [this](const char* data, size_t len) {
            terminal_.write(data, len);
            markRenderDirty();  // wake the UI thread to repaint the new output
        },
        [] { markRenderDirty(); });  // exited: wake the UI so it reaps the pane
    active_ = ok;
    return ok;
}

void TerminalSession::sendBytes(const char* data, size_t len) {
    if (active_) {
        capturePromptInput(data, len);
        pty_.write(data, len);
    }
}

void TerminalSession::capturePromptInput(const char* data, size_t len) {
    if (!atPrompt_) return;
    for (size_t i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data[i]);
        if (promptEscape_) {
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                ch == '~') promptEscape_ = false;
            continue;
        }
        if (ch == 0x1b) { promptEscape_ = true; continue; }
        if (ch == 0x08 || ch == 0x7f) {
            if (!promptInputUtf8_.empty()) promptInputUtf8_.pop_back();
        } else if (ch == '\r' || ch == '\n') {
            // Keep the accepted line until OSC 133;C starts command output.
            pendingCommandStartedAt_ = std::chrono::steady_clock::now();
        } else if (ch >= 0x20 && ch != 0x7f) {
            if (promptInputUtf8_.size() < 64 * 1024)
                promptInputUtf8_.push_back(static_cast<char>(ch));
        }
    }
}

void TerminalSession::processSemanticEvents() {
    auto startBlock = [&]() {
        if (!commandBlocks_.empty() &&
            commandBlocks_.back().state == CommandState::Running) return;
        CommandBlock block;
        block.id = nextCommandId_++;
        block.cwd = cwd_;
        block.startedAt = pendingCommandStartedAt_.time_since_epoch().count() != 0
                              ? pendingCommandStartedAt_
                              : std::chrono::steady_clock::now();
        block.startRow = pendingCommandRow_;
        if (!promptInputUtf8_.empty()) {
            const int count = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, promptInputUtf8_.data(),
                static_cast<int>(promptInputUtf8_.size()), nullptr, 0);
            if (count > 0) {
                block.command.resize(static_cast<size_t>(count));
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    promptInputUtf8_.data(),
                                    static_cast<int>(promptInputUtf8_.size()),
                                    block.command.data(), count);
            }
        }
        commandBlocks_.push_back(std::move(block));
        if (commandBlocks_.size() > 1000) commandBlocks_.erase(commandBlocks_.begin());
        promptInputUtf8_.clear();
        pendingCommandStartedAt_ = {};
    };
    for (auto& event : terminal_.drainSemanticEvents()) {
        switch (event.type) {
        case SemanticEventType::PromptStart:
            atPrompt_ = false;
            break;
        case SemanticEventType::CommandStart:
            atPrompt_ = true;
            pendingCommandRow_ = event.row;
            promptEscape_ = false;
            pendingCommandStartedAt_ = {};
            promptInputUtf8_.clear();
            break;
        case SemanticEventType::OutputStart:
            atPrompt_ = false;
            startBlock();
            break;
        case SemanticEventType::CommandEnd:
            // Shells with partial integration may omit C. Still retain the
            // command and complete it when the next prompt emits D.
            if (!promptInputUtf8_.empty()) startBlock();
            if (!commandBlocks_.empty() &&
                commandBlocks_.back().state == CommandState::Running) {
                CommandBlock& block = commandBlocks_.back();
                wchar_t* end = nullptr;
                const std::wstring code(event.value.begin(), event.value.end());
                const long parsed = event.value.empty() ? 0 : wcstol(code.c_str(), &end, 10);
                block.exitCode = event.value.empty()
                                     ? 0
                                     : (end && *end == L'\0'
                                            ? static_cast<int>(parsed)
                                            : -1);
                block.state = block.exitCode == 0 ? CommandState::Succeeded
                                                  : CommandState::Failed;
                block.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - block.startedAt);
                block.endRow = event.row;
                commandNavigation_ = commandBlocks_.size();
                FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
                ULARGE_INTEGER ticks{}; ticks.HighPart = ft.dwHighDateTime;
                ticks.LowPart = ft.dwLowDateTime;
                appendCommandHistory({block.command, block.cwd, block.exitCode,
                                      ticks.QuadPart});
            }
            break;
        case SemanticEventType::ClipboardRequest:
            // UI must explicitly approve before decoding/writing the clipboard.
            clipboardRequest_ = std::move(event.value);
            break;
        case SemanticEventType::HyperlinkStart:
        case SemanticEventType::HyperlinkEnd:
            break;
        case SemanticEventType::AgentStatus:
            if (event.value == "running") reportedAgentActivity_ = AgentActivity::Running;
            else if (event.value == "waiting") reportedAgentActivity_ = AgentActivity::Waiting;
            else if (event.value == "needs-input") reportedAgentActivity_ = AgentActivity::NeedsInput;
            else if (event.value == "done") reportedAgentActivity_ = AgentActivity::Done;
            else if (event.value == "failed") reportedAgentActivity_ = AgentActivity::Failed;
            break;
        }
    }
}

AgentActivity TerminalSession::agentActivity() const {
    if (context_.role != SessionRole::Agent) return AgentActivity::Idle;
    if (!pty_.hasExited()) {
        return reportedAgentActivity_ == AgentActivity::Waiting ||
                       reportedAgentActivity_ == AgentActivity::NeedsInput
                   ? reportedAgentActivity_
                   : AgentActivity::Running;
    }
    unsigned long code = 0;
    if (pty_.exitCode(code)) return code == 0 ? AgentActivity::Done
                                              : AgentActivity::Failed;
    return reportedAgentActivity_ == AgentActivity::Failed
               ? AgentActivity::Failed
               : AgentActivity::Done;
}

std::string TerminalSession::takeClipboardRequest() {
    std::string out;
    out.swap(clipboardRequest_);
    return out;
}

std::string TerminalSession::commandOutputUtf8(size_t index) {
    if (index >= commandBlocks_.size()) return {};
    std::string buffer;
    if (!terminal_.dumpBufferUtf8(buffer)) return {};
    const CommandBlock& block = commandBlocks_[index];
    std::string output;
    uint64_t row = 0;
    size_t pos = 0;
    while (pos <= buffer.size()) {
        size_t end = buffer.find('\n', pos);
        if (end == std::string::npos) end = buffer.size();
        if (row >= block.startRow && row <= block.endRow) {
            output.append(buffer, pos, end - pos);
            output.push_back('\n');
        }
        if (end == buffer.size()) break;
        pos = end + 1;
        ++row;
    }
    return output;
}

bool TerminalSession::jumpPreviousCommand() {
    if (commandBlocks_.empty()) return false;
    if (commandNavigation_ == 0 || commandNavigation_ > commandBlocks_.size())
        commandNavigation_ = commandBlocks_.size();
    --commandNavigation_;
    terminal_.scrollToRow(commandBlocks_[commandNavigation_].startRow);
    return true;
}

bool TerminalSession::jumpNextCommand() {
    if (commandBlocks_.empty()) return false;
    if (commandNavigation_ + 1 >= commandBlocks_.size()) {
        commandNavigation_ = commandBlocks_.size();
        terminal_.scrollToBottom();
        return true;
    }
    ++commandNavigation_;
    terminal_.scrollToRow(commandBlocks_[commandNavigation_].startRow);
    return true;
}

void TerminalSession::toggleBookmarkLastCommand() {
    if (!commandBlocks_.empty())
        commandBlocks_.back().bookmarked = !commandBlocks_.back().bookmarked;
}

void TerminalSession::resize(int cols, int rows, int cellWidthPx,
                             int cellHeightPx) {
    if (!active_ || cols <= 0 || rows <= 0) return;
    // Same grid but a new cell pixel size (font/DPI change) still needs to
    // reach the core: pixel metrics feed mouse reporting and size reports.
    if (cols == cols_ && rows == rows_ && cellWidthPx == cellW_ &&
        cellHeightPx == cellH_)
        return;
    cols_ = cols;
    rows_ = rows;
    cellW_ = cellWidthPx;
    cellH_ = cellHeightPx;
    terminal_.resize(cols, rows, cellWidthPx, cellHeightPx);
    pty_.resize(static_cast<short>(cols), static_cast<short>(rows));
}

void TerminalSession::snapshot() {
    if (active_) terminal_.snapshotInto(grid_);
}

std::wstring TerminalSession::prettifyTitle(const std::wstring& t) const {
    // Lower-cased copy for case-insensitive matching (titles come from the
    // console host with whatever casing the shell used).
    std::wstring low = t;
    for (wchar_t& ch : low) ch = static_cast<wchar_t>(towlower(ch));

    // Does the title start with a path to a known shell executable?
    static const wchar_t* kShells[] = { L"cmd.exe", L"powershell.exe",
                                        L"pwsh.exe", L"wsl.exe" };
    for (const wchar_t* exe : kShells) {
        const size_t pos = low.find(exe);
        if (pos == std::wstring::npos) continue;
        // Only treat it as "the shell's own path" when the exe name ends the
        // path token (start of string or after a separator, and followed by
        // end / " - <command>").
        const size_t end = pos + wcslen(exe);
        const bool pathLike =
            pos == 0 || low[pos - 1] == L'\\' || low[pos - 1] == L'/';
        if (!pathLike) continue;
        if (end == low.size()) {
            // Bare shell path (idle prompt): show the directory name instead.
            return basename(cwd_.empty() ? shell_ : cwd_);
        }
        // "…cmd.exe - <command>": show just the command.
        const std::wstring rest = t.substr(end);
        const size_t dash = rest.find(L" - ");
        if (dash != std::wstring::npos) {
            std::wstring cmd = rest.substr(dash + 3);
            // Trim leading spaces (cmd double-spaces some titles).
            size_t s = cmd.find_first_not_of(L' ');
            if (s != std::wstring::npos) return cmd.substr(s);
        }
    }
    return t;  // an app-provided title (vim, ssh, …): keep as-is
}

} // namespace liney
