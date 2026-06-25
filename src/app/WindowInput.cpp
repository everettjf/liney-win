#include "app/Window.h"
#include "app/WindowInternal.h"

#include <imm.h>

#include <string>

namespace liney {

void Window::sendToActive(const char* data, size_t len) {
    if (auto* s = activeSession()) {
        s->scrollToBottom();  // typing snaps the viewport back to live output
        s->sendBytes(data, len);
    }
}

void Window::scrollActive(int lines) {
    if (auto* s = activeSession()) s->scrollViewport(lines);
}

int Window::activePaneRows() const {
    Tab* t = activeTab();
    if (!t || !t->active()) return 24;
    int r = static_cast<int>(t->active()->rect.h / metrics_.cellH);
    return r < 1 ? 1 : r;
}

void Window::onWheel(int delta) {
    // One notch (WHEEL_DELTA) scrolls 3 lines into history (+) or toward live.
    scrollActive((delta / WHEEL_DELTA) * 3);
}

void Window::sendUtf16(const wchar_t* s, size_t len) {
    if (len == 0) return;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len),
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, static_cast<int>(len), utf8.data(), bytes,
                        nullptr, nullptr);
    sendToActive(utf8.data(), utf8.size());
}

void Window::cursorPixelPos(int& px, int& py) const {
    Tab* t = activeTab();
    if (t && t->active() && t->active()->session) {
        const Grid& g = t->active()->session->grid();
        const Rect r = t->active()->rect;
        px = static_cast<int>(r.x + g.cursorX * metrics_.cellW);
        py = static_cast<int>(r.y + g.cursorY * metrics_.cellH);
    } else {
        px = 0;
        py = 0;
    }
}

void Window::positionIme() {
    int px = 0, py = 0;
    cursorPixelPos(px, py);
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) return;
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = { px, py };
    ImmSetCompositionWindow(himc, &cf);
    CANDIDATEFORM caf{};
    caf.dwStyle = CFS_CANDIDATEPOS;
    caf.ptCurrentPos = { px, py + static_cast<int>(metrics_.cellH) };
    ImmSetCandidateWindow(himc, &caf);
    ImmReleaseContext(hwnd_, himc);
}

void Window::onChar(wchar_t unit) {
    if (swallowNextChar_) { swallowNextChar_ = false; return; }
    if (unit >= 0xD800 && unit <= 0xDBFF) { pendingHighSurrogate_ = unit; return; }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (pendingHighSurrogate_) {
            wchar_t pair[2] = { pendingHighSurrogate_, unit };
            sendUtf16(pair, 2);
            pendingHighSurrogate_ = 0;
        }
        return;
    }
    pendingHighSurrogate_ = 0;
    sendUtf16(&unit, 1);
}

bool Window::onKeyDown(WPARAM vk) {
    const bool ctrl = keyDown(VK_CONTROL);
    const bool shift = keyDown(VK_SHIFT);
    const bool alt = keyDown(VK_MENU);

    // Alt + arrows: move pane focus.  Alt+D / Shift+Alt+D: split panes.
    if (alt && !ctrl) {
        Tab* t = activeTab();
        switch (vk) {
        case VK_LEFT:  if (t) t->focusDir(SplitDir::Cols, false); swallowNextChar_ = true; return true;
        case VK_RIGHT: if (t) t->focusDir(SplitDir::Cols, true);  swallowNextChar_ = true; return true;
        case VK_UP:    if (t) t->focusDir(SplitDir::Rows, false); swallowNextChar_ = true; return true;
        case VK_DOWN:  if (t) t->focusDir(SplitDir::Rows, true);  swallowNextChar_ = true; return true;
        case 'D':  // Alt+D side by side; Shift+Alt+D stacked
            splitActive(shift ? SplitDir::Rows : SplitDir::Cols);
            swallowNextChar_ = true; return true;
        default: break;
        }
    }

    // Ctrl(+Shift) app shortcuts.
    if (ctrl) {
        if (vk == VK_TAB) { switchTab(shift ? -1 : 1); swallowNextChar_ = true; return true; }
        if (!shift) {
            switch (vk) {
            case 'C':  // copy if text is selected; otherwise fall through to ^C (interrupt)
                if (hasSelection_) {
                    copySelection();
                    clearSelection();
                    swallowNextChar_ = true;
                    return true;
                }
                break;  // no selection: let WM_CHAR deliver ^C to the shell
            case VK_OEM_PLUS:
            case VK_ADD: zoomFont(+1); swallowNextChar_ = true; return true;
            case VK_OEM_MINUS:
            case VK_SUBTRACT: zoomFont(-1); swallowNextChar_ = true; return true;
            case '0':
            case VK_NUMPAD0: zoomFont(0); swallowNextChar_ = true; return true;
            default: break;
            }
        }
        if (shift) {
            switch (vk) {
            case 'T': newTab(activeSession() ? activeSession()->cwd() : workspace_.root()); swallowNextChar_ = true; return true;
            case 'W': closeActivePane(); swallowNextChar_ = true; return true;
            case 'B': sidebarVisible_ = !sidebarVisible_; swallowNextChar_ = true; return true;
            case 'F': filesPanelVisible_ = !filesPanelVisible_; swallowNextChar_ = true; return true;
            case 'K': toggleKeepAwake(); swallowNextChar_ = true; return true;
            case 'U': checkForUpdates(); swallowNextChar_ = true; return true;
            case 'C': copySelection(); swallowNextChar_ = true; return true;
            case 'V': paste(); swallowNextChar_ = true; return true;
            case 'L':  // git history for the active pane's repo (pager view)
                if (auto* s = activeSession()) {
                    const std::wstring cwd = s->cwd();
                    if (!cwd.empty())
                        newTabShell(L"git -C \"" + cwd +
                                        L"\" log --oneline --graph --decorate -300",
                                    cwd);
                }
                swallowNextChar_ = true; return true;
            case 'G':  // git diff for the active pane's repo (pager view)
                if (auto* s = activeSession()) {
                    const std::wstring cwd = s->cwd();
                    if (!cwd.empty())
                        newTabShell(L"git -C \"" + cwd + L"\" diff", cwd);
                }
                swallowNextChar_ = true; return true;
            default: break;
            }
        }
    }

    // Shift + navigation keys scroll the viewport over scrollback history.
    if (shift && !ctrl && !alt) {
        const int page = activePaneRows() - 1;
        switch (vk) {
        case VK_PRIOR: scrollActive(page > 0 ? page : 1); return true;   // PgUp
        case VK_NEXT:  scrollActive(-(page > 0 ? page : 1)); return true; // PgDn
        case VK_HOME:  scrollActive(1000000); return true;               // to oldest
        case VK_END:   if (auto* s = activeSession()) s->scrollToBottom(); return true;
        default: break;
        }
    }

    // Keys that produce no WM_CHAR: forward as xterm escape sequences.
    const char* seq = nullptr;
    switch (vk) {
    case VK_UP:     seq = "\x1b[A"; break;
    case VK_DOWN:   seq = "\x1b[B"; break;
    case VK_RIGHT:  seq = "\x1b[C"; break;
    case VK_LEFT:   seq = "\x1b[D"; break;
    case VK_HOME:   seq = "\x1b[H"; break;
    case VK_END:    seq = "\x1b[F"; break;
    case VK_PRIOR:  seq = "\x1b[5~"; break;
    case VK_NEXT:   seq = "\x1b[6~"; break;
    case VK_INSERT: seq = "\x1b[2~"; break;
    case VK_DELETE: seq = "\x1b[3~"; break;
    case VK_F1:  seq = "\x1bOP"; break;
    case VK_F2:  seq = "\x1bOQ"; break;
    case VK_F3:  seq = "\x1bOR"; break;
    case VK_F4:  seq = "\x1bOS"; break;
    case VK_F5:  seq = "\x1b[15~"; break;
    case VK_F6:  seq = "\x1b[17~"; break;
    case VK_F7:  seq = "\x1b[18~"; break;
    case VK_F8:  seq = "\x1b[19~"; break;
    case VK_F9:  seq = "\x1b[20~"; break;
    case VK_F10: seq = "\x1b[21~"; break;
    case VK_F11: seq = "\x1b[23~"; break;
    case VK_F12: seq = "\x1b[24~"; break;
    default: return false;  // let WM_CHAR handle character keys
    }
    sendToActive(seq, std::char_traits<char>::length(seq));
    return true;
}


} // namespace liney
