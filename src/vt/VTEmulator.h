#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render/Cell.h"

namespace liney {

// A small, self-contained terminal emulator: an xterm-subset VT parser plus a
// screen buffer with a cursor. It is the MVP's terminal core, used when
// libghostty-vt is not compiled in (the default), so liney-win builds and runs
// a real interactive shell with only MSVC.
//
// Scope (enough for cmd.exe / PowerShell and most line-oriented tools):
//   - UTF-8 decoding of the PTY byte stream (ConPTY emits UTF-8)
//   - printable text with wrap, CR/LF/BS/TAB/BEL
//   - CSI cursor movement, erase-in-display / erase-in-line
//   - SGR attributes: bold/italic/underline/inverse, 16 / 256 / truecolor
//   - scroll region (DECSTBM), index / reverse-index, scrolling with a buffer
//   - cursor save/restore, cursor visibility (DECTCEM)
// Not handled (acceptable for the first MVP): alternate screen buffer, mouse
// reporting, full scrollback history, character sets. Unknown sequences are
// swallowed rather than printed.
//
// Not thread-safe by itself; Terminal serializes access with a mutex.
class VTEmulator {
public:
    VTEmulator() = default;

    void resize(int cols, int rows);
    void write(const char* data, size_t len);  // raw UTF-8 PTY bytes
    void snapshotInto(Grid& grid) const;        // copy cells + cursor out

    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    // --- screen buffer -----------------------------------------------------
    Cell& cell(int x, int y) { return cells_[static_cast<size_t>(y) * cols_ + x]; }
    void clearCell(Cell& c) const;
    void clearRegion(int x0, int y0, int x1, int y1);  // inclusive, blanks
    void scrollUp(int n);                              // within scroll region
    void scrollDown(int n);
    void newline();      // LF: move down, scroll if at region bottom
    void putGlyph(const std::wstring& g, int width);
    void moveTo(int x, int y);

    // --- byte / UTF-8 layer ------------------------------------------------
    void consume(uint8_t byte);
    void onCodepoint(uint32_t cp);

    // --- parser ------------------------------------------------------------
    enum class State { Ground, Esc, Csi, Osc };
    void execControl(uint32_t cp);     // C0 controls in Ground
    void csiDispatch(uint32_t finalByte);
    void escDispatch(uint32_t finalByte);
    void applySgr();
    int param(size_t i, int dflt) const;

    int cols_ = 80;
    int rows_ = 24;
    std::vector<Cell> cells_;

    int cx_ = 0, cy_ = 0;          // cursor (cell coords)
    bool cursorVisible_ = true;
    bool wrapPending_ = false;     // deferred wrap at right margin (DEC autowrap)

    int scrollTop_ = 0;            // scroll region, inclusive row indices
    int scrollBot_ = 23;

    // Saved cursor (DECSC / CSI s).
    int savedCx_ = 0, savedCy_ = 0;

    // Current pen.
    Color penFg_{ 204, 204, 204 };
    Color penBg_{ 0, 0, 0 };
    uint32_t penFlags_ = kFlagNone;

    // UTF-8 decode state.
    uint32_t utf8Acc_ = 0;
    int utf8Remaining_ = 0;

    // Parser state + CSI accumulation.
    State state_ = State::Ground;
    bool csiPrivate_ = false;      // leading '?' (or other) marker
    std::vector<int> params_;
    int curParam_ = -1;            // -1 == no digits yet
};

} // namespace liney
