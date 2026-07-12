#include "core/TerminalSession.h"

#include <cwchar>
#include <cwctype>

#include "core/RenderSignal.h"

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
    if (active_) pty_.write(data, len);
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
