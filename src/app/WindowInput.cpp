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
    const int lines = (delta / WHEEL_DELTA) * 3;
    if (lines == 0) return;
    if (auto* s = activeSession()) {
        // Full-screen apps (vim/less/htop) run on the alternate screen, which
        // has no scrollback — send arrow keys so the wheel scrolls the app.
        if (s->altScreenActive()) {
            const bool app = s->applicationCursorKeys();
            const char* seq = lines > 0 ? (app ? "\x1bOA" : "\x1b[A")
                                        : (app ? "\x1bOB" : "\x1b[B");
            for (int i = 0, n = lines > 0 ? lines : -lines; i < n; ++i)
                s->sendBytes(seq, 3);
            return;
        }
    }
    scrollActive(lines);
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
    if (findActive_) { onFindChar(unit); return; }  // typing edits the find query
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

    // While the find bar is open it owns Esc / Enter / F3 / Backspace; printable
    // keys reach it via WM_CHAR. Other shortcuts (below) still work. Esc / Enter /
    // Backspace also generate a WM_CHAR, so swallow it to avoid double handling.
    if (findActive_) {
        switch (vk) {
        case VK_ESCAPE: closeFind(); swallowNextChar_ = true; return true;
        case VK_RETURN: findNext(shift); swallowNextChar_ = true; return true;
        case VK_BACK:   findBackspace(); swallowNextChar_ = true; return true;
        case VK_F3:     findNext(shift); return true;  // F3 emits no WM_CHAR
        default: break;
        }
    }

    // Shift+Insert pastes; Ctrl+Insert copies (universal terminal conventions).
    if (vk == VK_INSERT && !alt) {
        if (shift && !ctrl) { paste(); return true; }
        if (ctrl && !shift) { copySelection(); return true; }
    }

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
            case 'F': openFind(); swallowNextChar_ = true; return true;
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': {
                // Ctrl+1..8 jump to that tab; Ctrl+9 jumps to the last tab.
                const size_t idx = static_cast<size_t>(vk - '1');
                if (idx < tabs_.size()) { clearSelection(); activeTab_ = idx; updateTitle(); }
                swallowNextChar_ = true; return true;
            }
            case '9':
                if (!tabs_.empty()) { clearSelection(); activeTab_ = tabs_.size() - 1; updateTitle(); }
                swallowNextChar_ = true; return true;
            case VK_OEM_PLUS:
            case VK_ADD: zoomFont(+1); swallowNextChar_ = true; return true;
            case VK_OEM_MINUS:
            case VK_SUBTRACT: zoomFont(-1); swallowNextChar_ = true; return true;
            case '0':
            case VK_NUMPAD0: zoomFont(0); swallowNextChar_ = true; return true;
            case 'V':  // paste (Windows Terminal convention; ^V is rarely typed)
                paste(); swallowNextChar_ = true; return true;
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
            case 'A': selectAllActive(); swallowNextChar_ = true; return true;
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

    // Keys that produce no WM_CHAR: forward as xterm escape sequences. With
    // DECCKM set (vim/less "application cursor keys"), arrows/Home/End switch
    // to the SS3 (ESC O …) form.
    bool app = false;
    switch (vk) {
    case VK_UP: case VK_DOWN: case VK_RIGHT: case VK_LEFT:
    case VK_HOME: case VK_END:
        if (auto* s = activeSession()) app = s->applicationCursorKeys();
        break;
    default: break;
    }
    const char* seq = nullptr;
    switch (vk) {
    case VK_UP:     seq = app ? "\x1bOA" : "\x1b[A"; break;
    case VK_DOWN:   seq = app ? "\x1bOB" : "\x1b[B"; break;
    case VK_RIGHT:  seq = app ? "\x1bOC" : "\x1b[C"; break;
    case VK_LEFT:   seq = app ? "\x1bOD" : "\x1b[D"; break;
    case VK_HOME:   seq = app ? "\x1bOH" : "\x1b[H"; break;
    case VK_END:    seq = app ? "\x1bOF" : "\x1b[F"; break;
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
