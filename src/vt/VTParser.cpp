#include "vt/VTEmulator.h"
#include "vt/VTEmulatorInternal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace liney {

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
        if (cp == ']') { state_ = State::Osc; oscBuf_.clear(); return; }
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
        if (cp == 0x07) { oscDispatch(); state_ = State::Ground; return; }  // BEL
        if (cp == 0x1B) { oscDispatch(); state_ = State::Esc; return; }     // ST
        appendUtf8String(cp, oscBuf_);
        return;
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
        penFg_ = theme_.foreground; penBg_ = theme_.background; penFlags_ = kFlagNone;
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
            const int n = std::max(1, param(0, 1));
            scrollRegion(cells_, cols_, cy_, scrollBot_, n, false, blank);
            shiftWrapFlags(cy_, scrollBot_, n, false);
        }
        break;
    case 'M':  // DL: delete lines at cursor
        if (cy_ >= scrollTop_ && cy_ <= scrollBot_) {
            Cell blank; clearCell(blank);
            const int n = std::max(1, param(0, 1));
            scrollRegion(cells_, cols_, cy_, scrollBot_, n, true, blank);
            shiftWrapFlags(cy_, scrollBot_, n, true);
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
    case 'l': {
        if (!csiPrivate_) break;
        const bool set = (finalByte == 'h');
        switch (param(0, 0)) {
        case 25: cursorVisible_ = set; break;                       // DECTCEM
        case 47:
        case 1047: set ? enterAlt(false) : leaveAlt(false); break;  // alt screen
        case 1049: set ? enterAlt(true) : leaveAlt(true); break;    // alt + cursor
        case 2004: bracketedPaste_ = set; break;                    // bracketed paste
        default: break;  // other modes (e.g. mouse reporting) swallowed
        }
        break;
    }

    case 'm': applySgr(); break;
    default: break;
    }
}

Color VTEmulator::color256(int idx) const {
    if (idx < 0) idx = 0;
    if (idx < 16) return theme_.ansi[idx];
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
    return theme_.foreground;
}


void VTEmulator::applySgr() {
    if (params_.empty()) {  // CSI m == CSI 0 m
        penFg_ = theme_.foreground; penBg_ = theme_.background; penFlags_ = kFlagNone;
        return;
    }
    for (size_t i = 0; i < params_.size(); ++i) {
        int v = params_[i] < 0 ? 0 : params_[i];
        switch (v) {
        case 0: penFg_ = theme_.foreground; penBg_ = theme_.background; penFlags_ = kFlagNone; break;
        case 1: penFlags_ |= kFlagBold; break;
        case 3: penFlags_ |= kFlagItalic; break;
        case 4: penFlags_ |= kFlagUnderline; break;
        case 7: penFlags_ |= kFlagInverse; break;
        case 22: penFlags_ &= ~kFlagBold; break;
        case 23: penFlags_ &= ~kFlagItalic; break;
        case 24: penFlags_ &= ~kFlagUnderline; break;
        case 27: penFlags_ &= ~kFlagInverse; break;
        case 39: penFg_ = theme_.foreground; break;
        case 49: penBg_ = theme_.background; break;
        case 38:
        case 48: {
            int kind = (i + 1 < params_.size()) ? params_[i + 1] : -1;
            Color col = theme_.foreground;
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
            if (v >= 30 && v <= 37) penFg_ = theme_.ansi[v - 30];
            else if (v >= 40 && v <= 47) penBg_ = theme_.ansi[v - 40];
            else if (v >= 90 && v <= 97) penFg_ = theme_.ansi[8 + (v - 90)];
            else if (v >= 100 && v <= 107) penBg_ = theme_.ansi[8 + (v - 100)];
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------


void VTEmulator::oscDispatch() {
    const std::string& s = oscBuf_;
    size_t semi = s.find(';');
    const std::string num = (semi == std::string::npos) ? s : s.substr(0, semi);
    const std::string rest =
        (semi == std::string::npos) ? std::string() : s.substr(semi + 1);

    if (num == "0" || num == "2") {            // set window/icon title
        oscTitle_ = utf8ToWide(rest);
    } else if (num == "7") {                   // report cwd: file://host/path
        std::string path = rest;
        const std::string fp = "file://";
        if (path.rfind(fp, 0) == 0) {
            size_t slash = path.find('/', fp.size());
            path = (slash == std::string::npos) ? "" : path.substr(slash);
        }
        if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
            path = path.substr(1);             // /D:/x -> D:/x
        for (char& c : path) if (c == '/') c = '\\';
        if (!path.empty()) { oscCwd_ = utf8ToWide(path); cwdDirty_ = true; }
    } else if (num == "9") {                    // OSC 9 ; message  (notification)
        if (!rest.empty())
            notifications_.push_back({ L"liney-win", utf8ToWide(rest) });
    } else if (num == "777") {                  // OSC 777 ; notify ; title ; body
        std::vector<std::string> parts = splitChar(rest, ';');
        if (!parts.empty() && parts[0] == "notify") {
            std::wstring title = parts.size() > 1 ? utf8ToWide(parts[1]) : L"liney-win";
            std::wstring body = parts.size() > 2 ? utf8ToWide(parts[2]) : L"";
            notifications_.push_back({ title, body });
        }
    }
    if (notifications_.size() > 32)
        notifications_.erase(notifications_.begin(),
                             notifications_.end() - 32);
    oscBuf_.clear();
}


} // namespace liney
