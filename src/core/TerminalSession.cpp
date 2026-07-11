#include "core/TerminalSession.h"

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

} // namespace liney
