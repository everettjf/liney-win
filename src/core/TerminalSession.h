#pragma once

#include <string>

#include "pty/ConPty.h"
#include "render/Cell.h"
#include "vt/Terminal.h"

namespace liney {

// One terminal: a shell (ConPTY) feeding a terminal core (Terminal) whose
// screen is snapshotted into a Grid for rendering. This is the unit a pane
// hosts; tabs/splits compose many of these.
//
// Not movable/copyable (owns a reader thread and a mutex); always heap-owned
// via unique_ptr. terminal_ is declared before pty_ so the reader thread, which
// calls terminal_.write, is joined before terminal_ is destroyed.
class TerminalSession {
public:
    TerminalSession() = default;

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

    // Start the shell in `cwd` (empty = inherit) at the given cell size.
    bool start(const std::wstring& shell, const std::wstring& cwd, int cols,
               int rows);

    void sendBytes(const char* data, size_t len);
    void resize(int cols, int rows, int cellWidthPx, int cellHeightPx);

    // Refresh the renderable snapshot into the session's grid.
    void snapshot();
    const Grid& grid() const { return grid_; }

    bool exited() const { return pty_.hasExited(); }
    const std::wstring& cwd() const { return cwd_; }
    const std::wstring& title() const { return title_; }

private:
    Terminal terminal_;
    ConPty pty_;
    Grid grid_;
    std::wstring cwd_;
    std::wstring title_;
    int cols_ = 0, rows_ = 0;
    bool active_ = false;
};

} // namespace liney
