#include "app/Window.h"
#include "app/WindowInternal.h"

#include <imm.h>

#include <string>

#include "core/RenderSignal.h"
#include "vt/KeyEncoder.h"

namespace liney {

void Window::sendToActive(const char* data, size_t len) {
    welcomeVisible_ = false;
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
    // Match the grid sizing (Tab::layoutRec): the terminal fills the pane
    // minus its inner padding, so page scrolling stays a true page.
    const float usable = t->active()->rect.h - metrics_.panePad() * 2.0f;
    int r = static_cast<int>(usable / metrics_.cellH);
    return r < 1 ? 1 : r;
}

void Window::onWheel(int delta, int xi, int yi) {
    // One notch (WHEEL_DELTA) scrolls 3 lines into history (+) or toward live.
    const int lines = (delta / WHEEL_DELTA) * 3;
    if (lines == 0) return;

    // Wheel priority: an app tracking the mouse gets wheel events (buttons
    // 4/5) > alt-screen apps get arrow keys > the scrollback scrolls.
    const int notches = (lines > 0 ? lines : -lines) / 3;
    bool forwarded = false;
    for (int i = 0; i < notches; ++i)
        forwarded = forwardMouse(0 /*press*/, lines > 0 ? 4 : 5, xi, yi) ||
                    forwarded;
    if (forwarded) return;

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
        const float pad = metrics_.panePad();
        px = static_cast<int>(r.x + pad + g.cursorX * metrics_.cellW);
        py = static_cast<int>(r.y + pad + g.cursorY * metrics_.cellH);
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
    if (paletteActive_) { onPaletteChar(unit); return; }
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

    if (paletteActive_ && onPaletteKey(vk)) {
        swallowNextChar_ = vk == VK_BACK || vk == VK_RETURN;
        return true;
    }
    if (!paletteActive_ && executeConfiguredBinding(static_cast<int>(vk), ctrl,
                                                     shift, alt)) {
        swallowNextChar_ = true;
        return true;
    }

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
        case 'B':
            sidebarVisible_ = !sidebarVisible_;
            markRenderDirty();
            swallowNextChar_ = true;
            return true;
        case 'K':
            openKeepAwakeMenu();
            swallowNextChar_ = true;
            return true;
        case 'L':
            if (tabOverflowRect_.w > 0.0f)
                openTabOverflowMenu(
                    static_cast<int>(tabOverflowRect_.right()),
                    static_cast<int>(tabOverflowRect_.bottom()));
            else
                openCommandPalette();
            swallowNextChar_ = true;
            return true;
        case 'M':
            openMainMenu();
            swallowNextChar_ = true;
            return true;
        case 'N':
            executePaletteAction(1);
            swallowNextChar_ = true;
            return true;
        case 'O':
            openDirectoryMenu();
            swallowNextChar_ = true;
            return true;
        case VK_LEFT:  if (t) t->focusDir(SplitDir::Cols, false); swallowNextChar_ = true; return true;
        case VK_RIGHT: if (t) t->focusDir(SplitDir::Cols, true);  swallowNextChar_ = true; return true;
        case VK_UP:    if (t) t->focusDir(SplitDir::Rows, false); swallowNextChar_ = true; return true;
        case VK_DOWN:  if (t) t->focusDir(SplitDir::Rows, true);  swallowNextChar_ = true; return true;
        case 'D':  // Alt+D side by side; Shift+Alt+D stacked
            executePaletteAction(shift ? 3 : 2);
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
                if (paneHasSelection()) {
                    copySelection();
                    clearSelection();
                    swallowNextChar_ = true;
                    return true;
                }
                break;  // no selection: let WM_CHAR deliver ^C to the shell
            case 'F': executePaletteAction(8); swallowNextChar_ = true; return true;
            case VK_OEM_COMMA:  // Ctrl+, opens Settings (VS Code convention)
                executePaletteAction(9); swallowNextChar_ = true; return true;
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
            case 'P': openCommandPalette(); swallowNextChar_ = true; return true;
            case 'T': executePaletteAction(1); swallowNextChar_ = true; return true;
            case 'W': closeActivePaneConfirming(); swallowNextChar_ = true; return true;
            case 'B': executePaletteAction(4); swallowNextChar_ = true; return true;
            case 'F': executePaletteAction(5); swallowNextChar_ = true; return true;
            case 'K': executePaletteAction(12); swallowNextChar_ = true; return true;
            case 'U': executePaletteAction(11); swallowNextChar_ = true; return true;
            case 'A': selectAllActive(); swallowNextChar_ = true; return true;
            case 'C': copySelection(); swallowNextChar_ = true; return true;
            case 'V': paste(); swallowNextChar_ = true; return true;
            case 'Z': executePaletteAction(6); swallowNextChar_ = true; return true;
            case 'E': executePaletteAction(7); swallowNextChar_ = true; return true;
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
    TerminalKey key{};
    bool special = true;
    switch (vk) {
    case VK_UP: key = TerminalKey::Up; break;
    case VK_DOWN: key = TerminalKey::Down; break;
    case VK_RIGHT: key = TerminalKey::Right; break;
    case VK_LEFT: key = TerminalKey::Left; break;
    case VK_HOME: key = TerminalKey::Home; break;
    case VK_END: key = TerminalKey::End; break;
    case VK_PRIOR: key = TerminalKey::PageUp; break;
    case VK_NEXT: key = TerminalKey::PageDown; break;
    case VK_INSERT: key = TerminalKey::Insert; break;
    case VK_DELETE: key = TerminalKey::DeleteKey; break;
    case VK_F1: key = TerminalKey::F1; break;
    case VK_F2: key = TerminalKey::F2; break;
    case VK_F3: key = TerminalKey::F3; break;
    case VK_F4: key = TerminalKey::F4; break;
    case VK_F5: key = TerminalKey::F5; break;
    case VK_F6: key = TerminalKey::F6; break;
    case VK_F7: key = TerminalKey::F7; break;
    case VK_F8: key = TerminalKey::F8; break;
    case VK_F9: key = TerminalKey::F9; break;
    case VK_F10: key = TerminalKey::F10; break;
    case VK_F11: key = TerminalKey::F11; break;
    case VK_F12: key = TerminalKey::F12; break;
    default: special = false; break;
    }
    if (!special) return false;  // let WM_CHAR handle character keys
    const std::string seq =
        encodeTerminalKey(key, {shift, alt, ctrl}, app);
    if (!seq.empty()) sendToActive(seq.data(), seq.size());
    return true;
}


} // namespace liney
