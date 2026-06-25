#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "render/Cell.h"
#include "vt/Notification.h"

namespace liney {

// One line of scrollback history. `wrapped` is true when the line was soft-
// wrapped into the next (so reflow can rejoin and re-split logical lines).
struct ScrollLine {
    std::vector<Cell> cells;
    bool wrapped = false;
};

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
//   - alternate screen buffer (?1049/?47/?1047) for full-screen TUIs
//   - scrollback history for the primary screen, with a scrollable viewport
// Not handled (acceptable for now): mouse reporting, character sets, resize
// reflow (long lines are not rewrapped). Unknown sequences are swallowed.
//
// Not thread-safe by itself; Terminal serializes access with a mutex.
class VTEmulator {
public:
    VTEmulator() = default;

    void resize(int cols, int rows);
    void write(const char* data, size_t len);  // raw UTF-8 PTY bytes
    void snapshotInto(Grid& grid) const;        // copy viewport + cursor out

    // Set the color theme (default fg/bg + ANSI palette). Resets the pen.
    void setTheme(const Theme& theme);

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    // Viewport scrolling over scrollback history (primary screen only).
    void scrollViewport(int deltaLines);  // +up into history, -down toward live
    void scrollToBottom();
    int scrollbackLines() const { return static_cast<int>(scrollback_.size()); }
    bool atBottom() const { return viewOffset_ == 0; }

    // Whether the app enabled bracketed paste (DEC mode ?2004).
    bool bracketedPaste() const { return bracketedPaste_; }

    // OSC-driven metadata. The caller serializes access via Terminal's mutex.
    const std::wstring& oscTitle() const { return oscTitle_; }
    bool takeCwd(std::wstring& out);                 // true if cwd changed
    void drainNotifications(std::vector<Notification>& out);

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

    // --- alternate screen / scrollback ------------------------------------
    void enterAlt(bool saveCursor);
    void leaveAlt(bool restoreCursor);
    void resizeBuffer(std::vector<Cell>& buf, int oldCols, int oldRows,
                      int newCols, int newRows) const;
    void shiftWrapFlags(int top, int bot, int n, bool up);
    void reflowScrollback(int newCols);  // rejoin + re-split history at new width

    // --- byte / UTF-8 layer ------------------------------------------------
    void consume(uint8_t byte);
    void onCodepoint(uint32_t cp);

    // --- parser ------------------------------------------------------------
    enum class State { Ground, Esc, Csi, Osc };
    void execControl(uint32_t cp);     // C0 controls in Ground
    void csiDispatch(uint32_t finalByte);
    void escDispatch(uint32_t finalByte);
    void oscDispatch();                // parse the collected OSC string
    void applySgr();
    Color color256(int idx) const;     // resolve a 256-color index via the theme
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

    // Alternate screen: while active, cells_ is the alt buffer and the primary
    // screen (with its scrollback) is stashed in savedPrimary_.
    bool altScreen_ = false;
    std::vector<Cell> savedPrimary_;
    std::vector<uint8_t> savedWrapped_;
    int altSavedCx_ = 0, altSavedCy_ = 0;

    // Per-row soft-wrap flags for the screen (1 == row wrapped into the next).
    std::vector<uint8_t> rowWrapped_;

    // Scrollback history for the primary screen (each entry is one row that
    // scrolled off the top, with its wrap flag), and the viewport offset from
    // the live bottom (0 == live). Alternate screen has no scrollback.
    std::deque<ScrollLine> scrollback_;
    size_t maxScrollback_ = 5000;
    int viewOffset_ = 0;

    bool bracketedPaste_ = false;  // DEC mode ?2004

    // Color theme + current pen.
    Theme theme_;
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

    // OSC accumulation + results.
    std::string oscBuf_;                       // UTF-8 OSC payload
    std::wstring oscTitle_;                     // OSC 0/2 window title
    std::wstring oscCwd_;                       // OSC 7 working directory
    bool cwdDirty_ = false;
    std::vector<Notification> notifications_;   // OSC 9 / 777
};

} // namespace liney
