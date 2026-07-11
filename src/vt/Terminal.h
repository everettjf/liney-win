#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "render/Cell.h"
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

    // Sink for bytes the core writes back to the PTY (query responses like
    // DSR/CPR or DA). Invoked from inside write() on the PTY reader thread.
    using PtyWriter = std::function<void(const char* data, size_t len)>;
    void setPtyWriter(PtyWriter writer);

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

    // Terminal modes the UI reacts to, queried straight from the core.
    bool bracketedPaste();          // DEC ?2004: wrap pastes in ESC[200~ / 201~
    bool applicationCursorKeys();   // DECCKM:    arrows send SS3 (ESC O A) form
    bool altScreenActive();         // vim/less…: wheel should send arrows, not scroll

    // Selection, owned by the terminal core so it stays anchored to the text
    // across scrolling, new output, and resize/reflow. Coordinates are
    // viewport cells. The active selection is rendered via snapshotInto().
    void selectionBegin(int vx, int vy);   // anchor a drag; clears the old selection
    bool selectionDragTo(int vx, int vy);  // extend anchor → point; true once selecting
    void selectionWord(int vx, int vy);    // double-click word selection
    void selectionLine(int vx, int vy);    // triple-click line selection
    void selectionAll();                   // whole buffer (scrollback included)
    void selectionClear();
    bool hasSelection();
    std::string selectionUtf8();           // active selection as plain text (UTF-8)

    // Scrollback-wide find support: dump the whole buffer as text (one line
    // per row, top of scrollback first) and jump the viewport by absolute row.
    bool dumpBufferUtf8(std::string& out);
    uint64_t viewportRow();                // first visible row (scrollbar offset)
    void scrollToRow(uint64_t row);

    // Mouse reporting: true when the app enabled any tracking mode; encode
    // turns one event into the escape sequence the app expects (empty = drop).
    // action: 0 press, 1 release, 2 motion. button: 0 none, 1 left, 2 right,
    // 3 middle, 4 wheel-up, 5 wheel-down. px/py are pane-relative pixels.
    bool mouseTracking();
    std::string encodeMouse(int action, int button, float px, float py,
                            bool shift, bool ctrl, bool alt, bool anyButtonDown,
                            unsigned cellW, unsigned cellH,
                            unsigned screenW, unsigned screenH);

    // OSC-driven metadata (built-in backend only; libghostty returns empty).
    std::wstring oscTitle();
    bool takeCwd(std::wstring& out);                  // true if cwd changed
    void drainNotifications(std::vector<Notification>& out);

private:
    bool modeGet(uint16_t mode, bool ansi);  // query a DEC/ANSI mode (locks mutex_)
    // Format `sel` (or the active selection when null) as plain UTF-8.
    // Caller must hold mutex_.
    std::string formatSelectionLocked(const GhosttySelection* sel, bool unwrap);

    std::mutex mutex_;
    GhosttyTerminal terminal_ = nullptr;
    GhosttyRenderState state_ = nullptr;
    GhosttyRenderStateRowIterator rowIter_ = nullptr;
    GhosttyRenderStateRowCells rowCells_ = nullptr;
    std::wstring lastCwd_;  // dedup OSC 7 cwd reports (takeCwd returns changes)
    // Drag-selection anchor: a tracked grid ref so it survives new output,
    // scrolling and reflow while the user is still dragging.
    GhosttyTrackedGridRef selAnchor_ = nullptr;
    // Mouse-reporting encoder + reusable event (created on first use).
    GhosttyMouseEncoder mouseEnc_ = nullptr;
    GhosttyMouseEvent mouseEvt_ = nullptr;
    // Reusable buffer for per-cell grapheme codepoints; sized to the cell's
    // real cluster length (unbounded) before GRAPHEMES_BUF writes into it.
    std::vector<uint32_t> graphemeBuf_;
    // Query-response sink; the WRITE_PTY trampoline reads this member.
    PtyWriter ptyWriter_;
};

} // namespace liney
