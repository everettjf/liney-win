#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "render/Cell.h"
#include "vt/Notification.h"

#ifdef LINEY_WITH_LIBGHOSTTY
extern "C" {
#include <ghostty/vt.h>
}
#else
#include "vt/VTEmulator.h"
#endif

namespace liney {

// C++ wrapper around libghostty-vt. Owns the terminal emulation state: feed it
// PTY bytes via write(), then pull a renderable snapshot via snapshotInto().
// libghostty-vt maintains the screen grid, scrollback, reflow and Unicode; we
// only translate its render-state snapshot into our Grid.
//
// When LINEY_WITH_LIBGHOSTTY is defined this delegates to the Zig-built
// ghostty-vt library (wired via CMake's LINEY_WITH_LIBGHOSTTY option).
// Otherwise (the default) it delegates to the self-contained VTEmulator, so
// liney-win builds and runs a real interactive shell with only MSVC.
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
    // is not compiled in or initialization fails.
    bool create(int cols, int rows);

    // Feed raw bytes coming from the PTY.
    void write(const char* data, size_t len);

    // Notify the terminal of a new size (in cells and per-cell pixels).
    void resize(int cols, int rows, int cellWidthPx, int cellHeightPx);

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
#ifdef LINEY_WITH_LIBGHOSTTY
    GhosttyTerminal terminal_ = nullptr;
    GhosttyRenderState state_ = nullptr;
    GhosttyRenderStateRowIterator rowIter_ = nullptr;
    GhosttyRenderStateRowCells rowCells_ = nullptr;
#else
    VTEmulator emu_;
    bool active_ = false;
#endif
};

} // namespace liney
