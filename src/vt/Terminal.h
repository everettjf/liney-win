#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "render/Cell.h"

#ifdef LINEY_WITH_LIBGHOSTTY
extern "C" {
#include <ghostty/vt.h>
}
#endif

namespace liney {

// C++ wrapper around libghostty-vt. Owns the terminal emulation state: feed it
// PTY bytes via write(), then pull a renderable snapshot via snapshotInto().
// libghostty-vt maintains the screen grid, scrollback, reflow and Unicode; we
// only translate its render-state snapshot into our Grid.
//
// Built with real calls only when LINEY_WITH_LIBGHOSTTY is defined (requires
// the Zig-built ghostty-vt library, wired via CMake's LINEY_WITH_LIBGHOSTTY
// option). Otherwise create() returns false and callers fall back to the
// scaffold's demo grid.
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

private:
    std::mutex mutex_;
#ifdef LINEY_WITH_LIBGHOSTTY
    GhosttyTerminal terminal_ = nullptr;
    GhosttyRenderState state_ = nullptr;
    GhosttyRenderStateRowIterator rowIter_ = nullptr;
    GhosttyRenderStateRowCells rowCells_ = nullptr;
#endif
};

} // namespace liney
