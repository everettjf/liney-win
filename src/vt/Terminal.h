#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "render/Cell.h"
#include "vt/ModeScanner.h"
#include "vt/Notification.h"

extern "C" {
#include <ghostty/vt.h>
}

namespace liney {

// C++ wrapper around libghostty-vt (the terminal core). Feed it PTY bytes via
// write(), then pull a renderable snapshot via snapshotInto(). libghostty-vt
// maintains the screen grid, scrollback, reflow and Unicode; we translate its
// render-state snapshot into our Grid, and pull title/pwd via the C API.
//
// The library is built from Ghostty via Zig (see CMake) — Zig is required to
// build liney-win.
//
// Thread-safety: write() runs on the PTY reader thread while snapshotInto()
// runs on the UI thread; both take the same lock.
class Terminal {
public:
    Terminal() = default;
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // Create the terminal + render-state objects. Returns false if libghostty-vt
    // is not compiled in or initialization fails. `scrollback` is the max number
    // of history lines to retain.
    bool create(int cols, int rows, int scrollback);

    // Feed raw bytes coming from the PTY.
    void write(const char* data, size_t len);

    // Notify the terminal of a new size (in cells and per-cell pixels).
    void resize(int cols, int rows, int cellWidthPx, int cellHeightPx);

    // Apply a color theme (built-in backend only).
    void setTheme(const Theme& theme);

    // Refresh the render snapshot and copy it into `grid`. Returns false when no
    // terminal is active (caller should keep its previous/demo grid).
    bool snapshotInto(Grid& grid);

    // Scroll the viewport over scrollback history (+ up into history, - down).
    // No-op for the libghostty backend (it owns its own viewport).
    void scrollViewport(int deltaLines);
    void scrollToBottom();

    // Whether the app enabled bracketed paste (DEC mode ?2004).
    bool bracketedPaste() const;

    // OSC-driven metadata (built-in backend only; libghostty returns empty).
    std::wstring oscTitle();
    bool takeCwd(std::wstring& out);                  // true if cwd changed
    void drainNotifications(std::vector<Notification>& out);

private:
    std::mutex mutex_;
    GhosttyTerminal terminal_ = nullptr;
    GhosttyRenderState state_ = nullptr;
    GhosttyRenderStateRowIterator rowIter_ = nullptr;
    GhosttyRenderStateRowCells rowCells_ = nullptr;
    std::wstring lastCwd_;  // dedup OSC 7 cwd reports (takeCwd returns changes)
    // Recovers the bracketed-paste (?2004) bit from the output stream; read on
    // the UI thread (paste) while write() runs on the reader thread, so the
    // published flag is atomic. The scanner itself is touched only under mutex_.
    BracketedPasteScanner bracketScan_;
    std::atomic<bool> bracketedPaste_{ false };
};

} // namespace liney
