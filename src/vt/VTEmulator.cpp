#include "vt/VTEmulator.h"

#include <algorithm>

namespace liney {

namespace {

constexpr Color kDefaultFg{ 204, 204, 204 };
constexpr Color kDefaultBg{ 0, 0, 0 };

// Standard xterm 16-color palette (0-7 normal, 8-15 bright).
const Color kAnsi[16] = {
    { 0, 0, 0 },     { 205, 0, 0 },   { 0, 205, 0 },   { 205, 205, 0 },
    { 0, 0, 238 },   { 205, 0, 205 }, { 0, 205, 205 }, { 229, 229, 229 },
    { 127, 127, 127 }, { 255, 0, 0 }, { 0, 255, 0 },   { 255, 255, 0 },
    { 92, 92, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 },
};

Color color256(int idx) {
    if (idx < 0) idx = 0;
    if (idx < 16) return kAnsi[idx];
    if (idx < 232) {
        int c = idx - 16;
        int r = c / 36, g = (c / 6) % 6, b = c % 6;
        auto lvl = [](int v) -> uint8_t {
            return static_cast<uint8_t>(v == 0 ? 0 : v * 40 + 55);
        };
        return { lvl(r), lvl(g), lvl(b) };
    }
    if (idx < 256) {
        uint8_t v = static_cast<uint8_t>(8 + 10 * (idx - 232));
        return { v, v, v };
    }
    return kDefaultFg;
}

bool isCombining(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x200B && cp <= 0x200D) ||
           cp == 0xFEFF;
}

bool isWide(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

int charWidth(uint32_t cp) {
    if (isCombining(cp)) return 0;
    if (isWide(cp)) return 2;
    return 1;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void appendUtf16(uint32_t cp, std::wstring& out) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Buffer management
// ---------------------------------------------------------------------------

void VTEmulator::clearCell(Cell& c) const {
    c.ch.clear();
    c.fg = kDefaultFg;
    c.bg = penBg_;  // background-color erase
    c.flags = kFlagNone;
}

void VTEmulator::clearRegion(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1 && y < rows_; ++y)
        for (int x = x0; x <= x1 && x < cols_; ++x) clearCell(cell(x, y));
}

void VTEmulator::resize(int cols, int rows) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols == cols_ && rows == rows_ && !cells_.empty()) return;

    std::vector<Cell> next(static_cast<size_t>(cols) * rows);
    const int cc = std::min(cols, cols_);
    const int rr = cells_.empty() ? 0 : std::min(rows, rows_);
    for (int y = 0; y < rr; ++y)
        for (int x = 0; x < cc; ++x)
            next[static_cast<size_t>(y) * cols + x] = cells_[static_cast<size_t>(y) * cols_ + x];

    cells_ = std::move(next);
    cols_ = cols;
    rows_ = rows;
    scrollTop_ = 0;
    scrollBot_ = rows_ - 1;
    cx_ = clampi(cx_, 0, cols_ - 1);
    cy_ = clampi(cy_, 0, rows_ - 1);
    wrapPending_ = false;
}

void VTEmulator::moveTo(int x, int y) {
    cx_ = clampi(x, 0, cols_ - 1);
    cy_ = clampi(y, 0, rows_ - 1);
    wrapPending_ = false;
}

// Scroll rows [top, bot] up by n; blank the n rows uncovered at the bottom.
static void scrollRegion(std::vector<Cell>& cells, int cols, int top, int bot,
                         int n, bool up, const Cell& blank) {
    if (n <= 0 || top > bot) return;
    const int span = bot - top + 1;
    if (n > span) n = span;
    auto row = [&](int y) { return cells.begin() + static_cast<size_t>(y) * cols; };
    if (up) {
        for (int y = top; y <= bot - n; ++y)
            std::copy(row(y + n), row(y + n) + cols, row(y));
        for (int y = bot - n + 1; y <= bot; ++y)
            std::fill(row(y), row(y) + cols, blank);
    } else {
        for (int y = bot; y >= top + n; --y)
            std::copy(row(y - n), row(y - n) + cols, row(y));
        for (int y = top; y < top + n; ++y)
            std::fill(row(y), row(y) + cols, blank);
    }
}

void VTEmulator::scrollUp(int n) {
    Cell blank; clearCell(blank);
    scrollRegion(cells_, cols_, scrollTop_, scrollBot_, n, true, blank);
}

void VTEmulator::scrollDown(int n) {
    Cell blank; clearCell(blank);
    scrollRegion(cells_, cols_, scrollTop_, scrollBot_, n, false, blank);
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
    if (wrapPending_) { cx_ = 0; newline(); wrapPending_ = false; }
    if (cx_ + width > cols_) { cx_ = 0; newline(); }

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

void VTEmulator::write(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) consume(static_cast<uint8_t>(data[i]));
}

void VTEmulator::consume(uint8_t byte) {
    if (utf8Remaining_ > 0) {
        if ((byte & 0xC0) == 0x80) {
            utf8Acc_ = (utf8Acc_ << 6) | (byte & 0x3F);
            if (--utf8Remaining_ == 0) onCodepoint(utf8Acc_);
            return;
        }
        utf8Remaining_ = 0;  // malformed: drop partial, reparse this byte
    }
    if (byte < 0x80) {
        onCodepoint(byte);
    } else if ((byte & 0xE0) == 0xC0) {
        utf8Acc_ = byte & 0x1F; utf8Remaining_ = 1;
    } else if ((byte & 0xF0) == 0xE0) {
        utf8Acc_ = byte & 0x0F; utf8Remaining_ = 2;
    } else if ((byte & 0xF8) == 0xF0) {
        utf8Acc_ = byte & 0x07; utf8Remaining_ = 3;
    } else {
        onCodepoint(0xFFFD);
    }
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

void VTEmulator::onCodepoint(uint32_t cp) {
    switch (state_) {
    case State::Ground:
        if (cp == 0x1B) { state_ = State::Esc; return; }
        if (cp < 0x20 || cp == 0x7F) { execControl(cp); return; }
        {
            int w = charWidth(cp);
            std::wstring g;
            appendUtf16(cp, g);
            putGlyph(g, w);
        }
        return;

    case State::Esc:
        if (cp == '[') {
            state_ = State::Csi;
            params_.clear();
            curParam_ = -1;
            csiPrivate_ = false;
            return;
        }
        if (cp == ']') { state_ = State::Osc; return; }
        escDispatch(cp);
        state_ = State::Ground;
        return;

    case State::Csi:
        if (cp >= '0' && cp <= '9') {
            curParam_ = (curParam_ < 0 ? 0 : curParam_) * 10 + static_cast<int>(cp - '0');
            return;
        }
        if (cp == ';') { params_.push_back(curParam_); curParam_ = -1; return; }
        if (cp == '?' || cp == '<' || cp == '=' || cp == '>') { csiPrivate_ = true; return; }
        if (cp >= 0x20 && cp <= 0x2F) return;  // intermediates: ignore
        if (cp >= 0x40 && cp <= 0x7E) {
            if (curParam_ >= 0 || !params_.empty()) params_.push_back(curParam_);
            csiDispatch(cp);
            state_ = State::Ground;
            return;
        }
        return;

    case State::Osc:
        if (cp == 0x07) { state_ = State::Ground; return; }  // BEL ends OSC
        if (cp == 0x1B) { state_ = State::Esc; return; }      // ESC \ (ST)
        return;                                                // swallow content
    }
}

void VTEmulator::execControl(uint32_t cp) {
    switch (cp) {
    case 0x08:  // BS
        if (cx_ > 0) --cx_;
        wrapPending_ = false;
        break;
    case 0x09: {  // HT
        int next = ((cx_ / 8) + 1) * 8;
        cx_ = clampi(next, 0, cols_ - 1);
        wrapPending_ = false;
        break;
    }
    case 0x0A:  // LF
    case 0x0B:  // VT
    case 0x0C:  // FF
        newline();
        break;
    case 0x0D:  // CR
        cx_ = 0;
        wrapPending_ = false;
        break;
    default:
        break;  // BEL and others ignored
    }
}

int VTEmulator::param(size_t i, int dflt) const {
    if (i >= params_.size()) return dflt;
    return params_[i] < 0 ? dflt : params_[i];
}

void VTEmulator::escDispatch(uint32_t finalByte) {
    switch (finalByte) {
    case 'D': newline(); break;                 // IND: index
    case 'M':                                    // RI: reverse index
        if (cy_ == scrollTop_) scrollDown(1);
        else if (cy_ > 0) --cy_;
        break;
    case 'E': cx_ = 0; newline(); break;        // NEL
    case '7': savedCx_ = cx_; savedCy_ = cy_; break;  // DECSC
    case '8': moveTo(savedCx_, savedCy_); break;      // DECRC
    case 'c':                                     // RIS: full reset
        penFg_ = kDefaultFg; penBg_ = kDefaultBg; penFlags_ = kFlagNone;
        scrollTop_ = 0; scrollBot_ = rows_ - 1;
        cursorVisible_ = true;
        clearRegion(0, 0, cols_ - 1, rows_ - 1);
        moveTo(0, 0);
        break;
    default: break;  // including ESC '\' (ST) -> no-op
    }
}

void VTEmulator::csiDispatch(uint32_t finalByte) {
    switch (finalByte) {
    case 'A': moveTo(cx_, cy_ - std::max(1, param(0, 1))); break;
    case 'B': moveTo(cx_, cy_ + std::max(1, param(0, 1))); break;
    case 'C': moveTo(cx_ + std::max(1, param(0, 1)), cy_); break;
    case 'D': moveTo(cx_ - std::max(1, param(0, 1)), cy_); break;
    case 'E': moveTo(0, cy_ + std::max(1, param(0, 1))); break;
    case 'F': moveTo(0, cy_ - std::max(1, param(0, 1))); break;
    case 'G':
    case '`': moveTo(param(0, 1) - 1, cy_); break;
    case 'd': moveTo(cx_, param(0, 1) - 1); break;
    case 'H':
    case 'f': moveTo(param(1, 1) - 1, param(0, 1) - 1); break;

    case 'J': {  // erase in display
        int mode = param(0, 0);
        if (mode == 0) {
            clearRegion(cx_, cy_, cols_ - 1, cy_);
            if (cy_ + 1 < rows_) clearRegion(0, cy_ + 1, cols_ - 1, rows_ - 1);
        } else if (mode == 1) {
            if (cy_ > 0) clearRegion(0, 0, cols_ - 1, cy_ - 1);
            clearRegion(0, cy_, cx_, cy_);
        } else {  // 2 or 3
            clearRegion(0, 0, cols_ - 1, rows_ - 1);
        }
        break;
    }
    case 'K': {  // erase in line
        int mode = param(0, 0);
        if (mode == 0) clearRegion(cx_, cy_, cols_ - 1, cy_);
        else if (mode == 1) clearRegion(0, cy_, cx_, cy_);
        else clearRegion(0, cy_, cols_ - 1, cy_);
        break;
    }

    case '@': {  // ICH: insert blanks
        int n = clampi(std::max(1, param(0, 1)), 1, cols_ - cx_);
        Cell blank; clearCell(blank);
        auto rowBegin = cells_.begin() + static_cast<size_t>(cy_) * cols_;
        std::copy_backward(rowBegin + cx_, rowBegin + (cols_ - n), rowBegin + cols_);
        std::fill(rowBegin + cx_, rowBegin + cx_ + n, blank);
        break;
    }
    case 'P': {  // DCH: delete chars
        int n = clampi(std::max(1, param(0, 1)), 1, cols_ - cx_);
        Cell blank; clearCell(blank);
        auto rowBegin = cells_.begin() + static_cast<size_t>(cy_) * cols_;
        std::copy(rowBegin + cx_ + n, rowBegin + cols_, rowBegin + cx_);
        std::fill(rowBegin + (cols_ - n), rowBegin + cols_, blank);
        break;
    }
    case 'X': {  // ECH: erase chars (no shift)
        int n = std::max(1, param(0, 1));
        clearRegion(cx_, cy_, std::min(cx_ + n - 1, cols_ - 1), cy_);
        break;
    }
    case 'L':  // IL: insert lines at cursor (within scroll region)
        if (cy_ >= scrollTop_ && cy_ <= scrollBot_) {
            Cell blank; clearCell(blank);
            scrollRegion(cells_, cols_, cy_, scrollBot_, std::max(1, param(0, 1)),
                         false, blank);
        }
        break;
    case 'M':  // DL: delete lines at cursor
        if (cy_ >= scrollTop_ && cy_ <= scrollBot_) {
            Cell blank; clearCell(blank);
            scrollRegion(cells_, cols_, cy_, scrollBot_, std::max(1, param(0, 1)),
                         true, blank);
        }
        break;
    case 'S': scrollUp(std::max(1, param(0, 1))); break;
    case 'T': scrollDown(std::max(1, param(0, 1))); break;

    case 'r': {  // DECSTBM: set scroll region
        int top = param(0, 1) - 1;
        int bot = param(1, rows_) - 1;
        top = clampi(top, 0, rows_ - 1);
        bot = clampi(bot, 0, rows_ - 1);
        if (top < bot) { scrollTop_ = top; scrollBot_ = bot; }
        moveTo(0, 0);
        break;
    }
    case 's': savedCx_ = cx_; savedCy_ = cy_; break;
    case 'u': moveTo(savedCx_, savedCy_); break;

    case 'h':
    case 'l':
        if (csiPrivate_ && param(0, 0) == 25) cursorVisible_ = (finalByte == 'h');
        break;  // other modes (alt screen, mouse, bracketed paste) swallowed

    case 'm': applySgr(); break;
    default: break;
    }
}

void VTEmulator::applySgr() {
    if (params_.empty()) {  // CSI m == CSI 0 m
        penFg_ = kDefaultFg; penBg_ = kDefaultBg; penFlags_ = kFlagNone;
        return;
    }
    for (size_t i = 0; i < params_.size(); ++i) {
        int v = params_[i] < 0 ? 0 : params_[i];
        switch (v) {
        case 0: penFg_ = kDefaultFg; penBg_ = kDefaultBg; penFlags_ = kFlagNone; break;
        case 1: penFlags_ |= kFlagBold; break;
        case 3: penFlags_ |= kFlagItalic; break;
        case 4: penFlags_ |= kFlagUnderline; break;
        case 7: penFlags_ |= kFlagInverse; break;
        case 22: penFlags_ &= ~kFlagBold; break;
        case 23: penFlags_ &= ~kFlagItalic; break;
        case 24: penFlags_ &= ~kFlagUnderline; break;
        case 27: penFlags_ &= ~kFlagInverse; break;
        case 39: penFg_ = kDefaultFg; break;
        case 49: penBg_ = kDefaultBg; break;
        case 38:
        case 48: {
            int kind = (i + 1 < params_.size()) ? params_[i + 1] : -1;
            Color col = kDefaultFg;
            if (kind == 5 && i + 2 < params_.size()) {
                col = color256(params_[i + 2]);
                i += 2;
            } else if (kind == 2 && i + 4 < params_.size()) {
                col = { static_cast<uint8_t>(clampi(params_[i + 2], 0, 255)),
                        static_cast<uint8_t>(clampi(params_[i + 3], 0, 255)),
                        static_cast<uint8_t>(clampi(params_[i + 4], 0, 255)) };
                i += 4;
            }
            if (v == 38) penFg_ = col; else penBg_ = col;
            break;
        }
        default:
            if (v >= 30 && v <= 37) penFg_ = kAnsi[v - 30];
            else if (v >= 40 && v <= 47) penBg_ = kAnsi[v - 40];
            else if (v >= 90 && v <= 97) penFg_ = kAnsi[8 + (v - 90)];
            else if (v >= 100 && v <= 107) penBg_ = kAnsi[8 + (v - 100)];
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------

void VTEmulator::snapshotInto(Grid& grid) const {
    grid.resize(cols_, rows_);
    std::copy(cells_.begin(), cells_.end(), grid.cells.begin());
    grid.cursorX = clampi(cx_, 0, cols_ - 1);
    grid.cursorY = clampi(cy_, 0, rows_ - 1);
    grid.cursorVisible = cursorVisible_;
}

} // namespace liney
