#include "vt/VTEmulator.h"
#include "vt/VTEmulatorInternal.h"

#include <algorithm>

namespace liney {

void VTEmulator::clearCell(Cell& c) const {
    c.ch.clear();
    c.fg = theme_.foreground;
    c.bg = penBg_;  // background-color erase
    c.flags = kFlagNone;
}

void VTEmulator::clearRegion(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1 && y < rows_; ++y) {
        for (int x = x0; x <= x1 && x < cols_; ++x) clearCell(cell(x, y));
        // A fully-cleared row no longer soft-wraps.
        if (x0 == 0 && x1 >= cols_ - 1 && y < static_cast<int>(rowWrapped_.size()))
            rowWrapped_[y] = 0;
    }
}

void VTEmulator::resizeBuffer(std::vector<Cell>& buf, int oldCols, int oldRows,
                              int newCols, int newRows) const {
    std::vector<Cell> next(static_cast<size_t>(newCols) * newRows);
    const int cc = std::min(newCols, oldCols);
    const int rr = buf.empty() ? 0 : std::min(newRows, oldRows);
    for (int y = 0; y < rr; ++y)
        for (int x = 0; x < cc; ++x)
            next[static_cast<size_t>(y) * newCols + x] =
                buf[static_cast<size_t>(y) * oldCols + x];
    buf = std::move(next);
}

void VTEmulator::resize(int cols, int rows) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols == cols_ && rows == rows_ && !cells_.empty()) return;

    const int oldCols = cols_, oldRows = rows_;
    resizeBuffer(cells_, oldCols, oldRows, cols, rows);
    if (!savedPrimary_.empty())
        resizeBuffer(savedPrimary_, oldCols, oldRows, cols, rows);

    // Rewrap scrollback history to the new width (rejoin soft-wrapped runs and
    // re-split). The screen itself is re-drawn by the shell after the resize.
    if (cols != oldCols) reflowScrollback(cols);

    // Resize the screen wrap flags (preserve overlap); reset on width change.
    {
        std::vector<uint8_t> nextWrap(static_cast<size_t>(rows), 0);
        if (cols == oldCols) {
            const int rr = std::min(rows, static_cast<int>(rowWrapped_.size()));
            for (int y = 0; y < rr; ++y) nextWrap[y] = rowWrapped_[y];
        }
        rowWrapped_ = std::move(nextWrap);
    }

    cols_ = cols;
    rows_ = rows;
    scrollTop_ = 0;
    scrollBot_ = rows_ - 1;
    cx_ = clampi(cx_, 0, cols_ - 1);
    cy_ = clampi(cy_, 0, rows_ - 1);
    wrapPending_ = false;
    viewOffset_ = clampi(viewOffset_, 0, static_cast<int>(scrollback_.size()));
}

void VTEmulator::moveTo(int x, int y) {
    cx_ = clampi(x, 0, cols_ - 1);
    cy_ = clampi(y, 0, rows_ - 1);
    wrapPending_ = false;
}

void VTEmulator::shiftWrapFlags(int top, int bot, int n, bool up) {
    if (n <= 0 || top > bot) return;
    const int span = bot - top + 1;
    if (n > span) n = span;
    if (static_cast<int>(rowWrapped_.size()) < rows_) rowWrapped_.assign(rows_, 0);
    if (up) {
        for (int y = top; y <= bot - n; ++y) rowWrapped_[y] = rowWrapped_[y + n];
        for (int y = bot - n + 1; y <= bot; ++y) rowWrapped_[y] = 0;
    } else {
        for (int y = bot; y >= top + n; --y) rowWrapped_[y] = rowWrapped_[y - n];
        for (int y = top; y < top + n; ++y) rowWrapped_[y] = 0;
    }
}

void VTEmulator::scrollUp(int n) {
    if (n <= 0) return;
    // Lines scrolling off the top of the full primary screen enter scrollback.
    if (!altScreen_ && scrollTop_ == 0) {
        const int span = scrollBot_ - scrollTop_ + 1;
        const int cap = std::min(n, span);
        for (int i = 0; i < cap; ++i) {
            auto begin = cells_.begin() + static_cast<size_t>(i) * cols_;
            ScrollLine sl;
            sl.cells.assign(begin, begin + cols_);
            sl.wrapped = i < static_cast<int>(rowWrapped_.size()) && rowWrapped_[i];
            scrollback_.push_back(std::move(sl));
        }
        while (scrollback_.size() > maxScrollback_) scrollback_.pop_front();
        // If the user is viewing history, keep the same content in view.
        if (viewOffset_ > 0) viewOffset_ += cap;
        viewOffset_ = clampi(viewOffset_, 0, static_cast<int>(scrollback_.size()));
    }
    Cell blank; clearCell(blank);
    scrollRegion(cells_, cols_, scrollTop_, scrollBot_, n, true, blank);
    shiftWrapFlags(scrollTop_, scrollBot_, n, true);
}

void VTEmulator::scrollDown(int n) {
    Cell blank; clearCell(blank);
    scrollRegion(cells_, cols_, scrollTop_, scrollBot_, n, false, blank);
    shiftWrapFlags(scrollTop_, scrollBot_, n, false);
}

void VTEmulator::newline() {
    wrapPending_ = false;
    if (cy_ == scrollBot_)
        scrollUp(1);
    else if (cy_ < rows_ - 1)
        ++cy_;
}

void VTEmulator::putGlyph(const std::wstring& g, int width) {
    if (width == 0) {  // combining mark: attach to the previous cell
        int tx = cx_ > 0 ? cx_ - 1 : 0;
        cell(tx, cy_).ch += g;
        return;
    }
    if (wrapPending_) {
        if (cy_ < static_cast<int>(rowWrapped_.size())) rowWrapped_[cy_] = 1;
        cx_ = 0; newline(); wrapPending_ = false;
    }
    if (cx_ + width > cols_) {
        if (cy_ < static_cast<int>(rowWrapped_.size())) rowWrapped_[cy_] = 1;
        cx_ = 0; newline();
    }

    Cell& c = cell(cx_, cy_);
    c.ch = g;
    c.fg = penFg_;
    c.bg = penBg_;
    c.flags = penFlags_;
    if (width == 2 && cx_ + 1 < cols_) {
        Cell& spacer = cell(cx_ + 1, cy_);
        spacer.ch.clear();
        spacer.fg = penFg_;
        spacer.bg = penBg_;
        spacer.flags = penFlags_;
    }
    cx_ += width;
    if (cx_ >= cols_) { cx_ = cols_ - 1; wrapPending_ = true; }
}

// ---------------------------------------------------------------------------
// Byte / UTF-8 layer
// ---------------------------------------------------------------------------


void VTEmulator::setTheme(const Theme& theme) {
    theme_ = theme;
    penFg_ = theme_.foreground;
    penBg_ = theme_.background;
}


void VTEmulator::snapshotInto(Grid& grid) const {
    grid.resize(cols_, rows_);
    const int sb = static_cast<int>(scrollback_.size());
    const int start = sb - viewOffset_;  // global index of the first visible row

    for (int r = 0; r < rows_; ++r) {
        const int gi = start + r;
        Cell* dst = &grid.cells[static_cast<size_t>(r) * cols_];
        if (gi < 0) {
            for (int x = 0; x < cols_; ++x) dst[x] = Cell{};
        } else if (gi < sb) {
            const std::vector<Cell>& row = scrollback_[gi].cells;
            for (int x = 0; x < cols_; ++x)
                dst[x] = (x < static_cast<int>(row.size())) ? row[x] : Cell{};
        } else {
            const Cell* src = &cells_[static_cast<size_t>(gi - sb) * cols_];
            for (int x = 0; x < cols_; ++x) dst[x] = src[x];
        }
    }

    // Cursor is shown only at the live bottom (not while viewing history).
    grid.cursorVisible = cursorVisible_ && viewOffset_ == 0;
    grid.cursorX = clampi(cx_, 0, cols_ - 1);
    grid.cursorY = clampi(cy_, 0, rows_ - 1);
}

void VTEmulator::enterAlt(bool saveCursor) {
    if (altScreen_) return;
    if (saveCursor) { altSavedCx_ = cx_; altSavedCy_ = cy_; }
    savedPrimary_ = std::move(cells_);
    savedWrapped_ = std::move(rowWrapped_);
    Cell blank; clearCell(blank);
    cells_.assign(static_cast<size_t>(cols_) * rows_, blank);
    rowWrapped_.assign(static_cast<size_t>(rows_), 0);
    altScreen_ = true;
    viewOffset_ = 0;
    scrollTop_ = 0;
    scrollBot_ = rows_ - 1;
}

void VTEmulator::leaveAlt(bool restoreCursor) {
    if (!altScreen_) return;
    cells_ = std::move(savedPrimary_);
    savedPrimary_.clear();
    if (static_cast<int>(cells_.size()) != cols_ * rows_)
        cells_.assign(static_cast<size_t>(cols_) * rows_, Cell{});
    rowWrapped_ = std::move(savedWrapped_);
    savedWrapped_.clear();
    if (static_cast<int>(rowWrapped_.size()) != rows_)
        rowWrapped_.assign(static_cast<size_t>(rows_), 0);
    altScreen_ = false;
    if (restoreCursor) {
        cx_ = clampi(altSavedCx_, 0, cols_ - 1);
        cy_ = clampi(altSavedCy_, 0, rows_ - 1);
    }
    scrollTop_ = 0;
    scrollBot_ = rows_ - 1;
    wrapPending_ = false;
}

void VTEmulator::scrollViewport(int deltaLines) {
    if (altScreen_) return;  // no scrollback on the alternate screen
    viewOffset_ = clampi(viewOffset_ + deltaLines, 0,
                         static_cast<int>(scrollback_.size()));
}

void VTEmulator::scrollToBottom() { viewOffset_ = 0; }

void VTEmulator::reflowScrollback(int newCols) {
    if (newCols < 1 || scrollback_.empty()) return;

    // 1) Rejoin soft-wrapped runs into logical lines.
    std::vector<std::vector<Cell>> logical;
    std::vector<Cell> cur;
    for (const ScrollLine& sl : scrollback_) {
        if (sl.wrapped) {
            cur.insert(cur.end(), sl.cells.begin(), sl.cells.end());
        } else {
            // Line end: append, trimming trailing blank cells (padding).
            int last = static_cast<int>(sl.cells.size()) - 1;
            while (last >= 0 && sl.cells[last].ch.empty()) --last;
            cur.insert(cur.end(), sl.cells.begin(), sl.cells.begin() + (last + 1));
            logical.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) logical.push_back(std::move(cur));

    // 2) Re-split each logical line to newCols, marking soft-wrap flags.
    std::deque<ScrollLine> next;
    for (auto& L : logical) {
        if (L.empty()) {
            next.push_back(ScrollLine{ std::vector<Cell>(newCols), false });
            continue;
        }
        for (size_t i = 0; i < L.size(); i += newCols) {
            const size_t end = std::min(i + newCols, L.size());
            ScrollLine sl;
            sl.cells.assign(L.begin() + i, L.begin() + end);
            sl.cells.resize(static_cast<size_t>(newCols));  // pad to width
            sl.wrapped = (end < L.size());
            next.push_back(std::move(sl));
        }
    }

    while (next.size() > maxScrollback_) next.pop_front();
    scrollback_ = std::move(next);
    viewOffset_ = clampi(viewOffset_, 0, static_cast<int>(scrollback_.size()));
}


bool VTEmulator::takeCwd(std::wstring& out) {
    if (!cwdDirty_) return false;
    out = oscCwd_;
    cwdDirty_ = false;
    return true;
}

void VTEmulator::drainNotifications(std::vector<Notification>& out) {
    for (auto& n : notifications_) out.push_back(std::move(n));
    notifications_.clear();
}

} // namespace liney
