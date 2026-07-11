#include "app/Window.h"
#include "app/WindowInternal.h"
#include "util/InputBox.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>
#include <utility>

namespace liney {

void Window::onMouseDown(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);

    if ((sidebarVisible_ && leftBar.contains(x, y)) ||
        (filesPanelVisible_ && rightPanel.contains(x, y))) {
        if (sidebarVisible_ && workspaceAddRect_.contains(x, y)) {
            addWorkspaceFolder();
            return;
        }
        for (const SidebarRow& row : sidebarRows_) {
            if (!row.rect.contains(x, y)) continue;
            switch (row.kind) {
            case RowKind::RepoHeader: {
                auto& repos = workspace_.repos();
                if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
                Repo& repo = repos[row.repo];
                repo.expanded = !repo.expanded;
                if (repo.expanded) workspace_.loadWorktrees(repo);
                break;
            }
            case RowKind::Worktree: {
                auto& repos = workspace_.repos();
                if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
                Repo& repo = repos[row.repo];
                if (row.worktree < static_cast<int>(repo.worktrees.size()))
                    newTab(repo.worktrees[row.worktree].path);
                break;
            }
            case RowKind::FileUp: {
                size_t s = browsePath_.find_last_of(L"\\/");
                if (s != std::wstring::npos) browsePath_ = browsePath_.substr(0, s);
                break;
            }
            case RowKind::FileDir:
                browsePath_ = row.path;  // navigate the panel into the directory
                break;
            case RowKind::FileEntry: {
                // Insert the filename into the focused pane (quote if needed).
                std::wstring name = row.path;
                size_t s = name.find_last_of(L"\\/");
                if (s != std::wstring::npos) name = name.substr(s + 1);
                std::wstring ins = name.find(L' ') != std::wstring::npos
                                       ? L"\"" + name + L"\" "
                                       : name + L" ";
                sendUtf16(ins.c_str(), ins.size());
                break;
            }
            case RowKind::SshHost:
                if (row.repo >= 0 && row.repo < static_cast<int>(sshHosts_.size()))
                    newTabShell(L"ssh " + sshHosts_[row.repo], L"");
                break;
            case RowKind::Agent:
                if (row.repo >= 0 && row.repo < static_cast<int>(agents_.size()))
                    newTabShell(agents_[row.repo].command, agents_[row.repo].cwd);
                break;
            }
            return;
        }
        return;
    }

    if (tabBar.contains(x, y)) {
        if (menuButtonRect_.contains(x, y)) { openMainMenu(); return; }
        if (plusRect_.contains(x, y)) {
            newTab(activeSession() ? activeSession()->cwd() : workspace_.root());
            return;
        }
        for (size_t i = 0; i < tabRects_.size(); ++i) {
            if (tabRects_[i].contains(x, y)) {
                clearSelection();
                activeTab_ = i;
                tabDragIndex_ = static_cast<int>(i);  // start a potential reorder
                SetCapture(hwnd_);
                updateTitle();
                return;
            }
        }
        return;
    }

    if (panes.contains(x, y)) {
        // Clicks on the floating find bar shouldn't start a text selection.
        if (findActive_ && findBarRect_.contains(x, y)) return;
        Tab* t = activeTab();
        if (!t) return;
        // A click near a split divider starts a resize drag instead of a select.
        if (Pane* divider = t->splitDividerAt(x, y, 4.0f)) {
            dragDivider_ = divider;
            SetCapture(hwnd_);
            return;
        }
        Pane* leaf = t->hitTest(x, y);
        if (!leaf) return;
        t->setActive(leaf);

        // Apps that track the mouse (vim :set mouse=a, htop, mc…) get the
        // click; hold Shift to make a local selection instead.
        if (forwardMouse(0 /*press*/, 1 /*left*/, xi, yi)) return;

        int cx = 0, cy = 0;
        if (!paneCellAt(leaf, xi, yi, cx, cy)) return;

        // A third press shortly after a double-click on the same row escalates
        // to whole-line selection (double-click already selected the word).
        if (clickStreak_ == 2 && leaf == selPane_ && cy == lastClickCY_ &&
            GetTickCount() - lastClickTick_ <= GetDoubleClickTime()) {
            clickStreak_ = 3;
            lastClickTick_ = GetTickCount();
            selectLineAt(leaf, cy);
            maybeCopyOnSelect();
            return;
        }

        // Begin a selection drag from this cell (a plain click selects nothing
        // until the mouse leaves the cell — WM_MOUSEMOVE fires spuriously on
        // clicks). The anchor is buffer-tracked by the core.
        clickStreak_ = 1;
        selecting_ = true;
        selDragged_ = false;
        selDragCX_ = cx;
        selDragCY_ = cy;
        if (selPane_ && selPane_ != leaf && selPane_->session)
            selPane_->session->selectionClear();
        selPane_ = leaf;
        if (leaf->session) leaf->session->selectionBegin(cx, cy);
        SetCapture(hwnd_);
    }
}

void Window::onMouseDoubleClick(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
    if (tabBar.contains(x, y)) {
        // Double-click empty tab-strip space opens a new tab (common convention);
        // on a tab / + / ☰ it's just a click.
        bool onTab = false;
        for (const Rect& tr : tabRects_) if (tr.contains(x, y)) { onTab = true; break; }
        if (!onTab && !plusRect_.contains(x, y) && !menuButtonRect_.contains(x, y))
            newTab(activeSession() ? activeSession()->cwd() : workspace_.root());
        else
            onMouseDown(xi, yi);
        return;
    }
    if (!panes.contains(x, y)) { onMouseDown(xi, yi); return; }  // other chrome: plain click
    Tab* t = activeTab();
    if (!t) return;
    Pane* leaf = t->hitTest(x, y);
    if (!leaf) return;
    t->setActive(leaf);
    // The second press of a double-click arrives here instead of BUTTONDOWN;
    // mouse-tracking apps still just get the press.
    if (forwardMouse(0 /*press*/, 1 /*left*/, xi, yi)) return;
    int cx = 0, cy = 0;
    if (!paneCellAt(leaf, xi, yi, cx, cy)) return;
    selectWordAt(leaf, cx, cy);
    maybeCopyOnSelect();
    // Arm triple-click detection: a further press on this row escalates to line.
    clickStreak_ = 2;
    lastClickTick_ = GetTickCount();
    lastClickCY_ = cy;
}

void Window::onMouseDownRight(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);

    // Right-click a tab → context menu (acts on that tab).
    if (tabBar.contains(x, y)) {
        for (size_t i = 0; i < tabRects_.size(); ++i) {
            if (!tabRects_[i].contains(x, y)) continue;
            activeTab_ = i;
            clearSelection();
            updateTitle();
            openTabMenu(xi, yi);
            return;
        }
        return;
    }

    // Right-click inside a terminal pane → the app when it tracks the mouse
    // (mc, vim…; Shift bypasses), else the copy / paste / find menu. There is
    // no WM_RBUTTONUP handler, so send the release right away.
    if (panes.contains(x, y)) {
        if (forwardMouse(0 /*press*/, 2 /*right*/, xi, yi)) {
            forwardMouse(1 /*release*/, 2, xi, yi);
            return;
        }
        openPaneMenu(xi, yi);
        return;
    }

    if (!sidebarVisible_ || !leftBar.contains(x, y)) return;

    for (const SidebarRow& row : sidebarRows_) {
        if (!row.rect.contains(x, y)) continue;
        auto& repos = workspace_.repos();
        if (row.repo < 0 || row.repo >= static_cast<int>(repos.size())) return;
        Repo& repo = repos[row.repo];

        if (row.worktree < 0) {
            // Repo header: a context menu (new worktree / set icon / remove).
            POINT pt{ xi, yi };
            ClientToScreen(hwnd_, &pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"New worktree…");
            AppendMenuW(menu, MF_STRING, 2, L"Set icon…");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 3, L"Remove from workspace");
            const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                           pt.x, pt.y, 0, hwnd_, nullptr);
            DestroyMenu(menu);
            if (cmd == 1) {
                std::wstring name = inputBox(hwnd_, L"New worktree",
                                             L"New branch / worktree name:", L"");
                if (name.empty()) return;
                std::wstring err;
                std::wstring path = workspace_.addWorktree(repo, name, &err);
                if (!path.empty()) newTab(path);
                else {
                    std::wstring msg = L"git worktree add failed.";
                    if (!err.empty()) msg += L"\n\n" + err;
                    MessageBoxW(hwnd_, msg.c_str(), L"liney-win",
                                MB_OK | MB_ICONERROR);
                }
            } else if (cmd == 2) {
                setProjectIcon(repo);
            } else if (cmd == 3) {
                removeProject(repo);  // erases `repo`; nothing used after
            }
        } else if (row.worktree < static_cast<int>(repo.worktrees.size())) {
            // Worktree row: confirm and remove.
            const Worktree& wt = repo.worktrees[row.worktree];
            std::wstring msg = L"Remove worktree?\n\n" + wt.path;
            if (MessageBoxW(hwnd_, msg.c_str(), L"Remove worktree",
                            MB_YESNO | MB_ICONWARNING) == IDYES) {
                std::wstring err;
                if (!workspace_.removeWorktree(repo, wt.path, &err)) {
                    std::wstring m = L"git worktree remove failed.";
                    if (!err.empty()) m += L"\n\n" + err;  // e.g. "use --force"
                    MessageBoxW(hwnd_, m.c_str(), L"liney-win",
                                MB_OK | MB_ICONERROR);
                }
            }
        }
        return;
    }
}

void Window::onMouseMove(int xi, int yi) {
    if (tabDragIndex_ >= 0) {
        const float x = static_cast<float>(xi), y = static_cast<float>(yi);
        for (size_t i = 0; i < tabRects_.size(); ++i) {
            if (static_cast<int>(i) == tabDragIndex_) continue;
            if (!tabRects_[i].contains(x, y)) continue;
            // Move the dragged tab to position i, keeping it active.
            auto moved = std::move(tabs_[tabDragIndex_]);
            tabs_.erase(tabs_.begin() + tabDragIndex_);
            tabs_.insert(tabs_.begin() + i, std::move(moved));
            tabDragIndex_ = static_cast<int>(i);
            activeTab_ = i;
            break;
        }
        return;
    }
    if (dragDivider_) {
        Pane* s = dragDivider_;
        const float x = static_cast<float>(xi), y = static_cast<float>(yi);
        float r = s->ratio;
        if (s->dir == SplitDir::Cols && s->rect.w > 1.0f)
            r = (x - s->rect.x) / s->rect.w;
        else if (s->dir == SplitDir::Rows && s->rect.h > 1.0f)
            r = (y - s->rect.y) / s->rect.h;
        s->ratio = r < 0.05f ? 0.05f : (r > 0.95f ? 0.95f : r);
        return;
    }
    if (selecting_ && selPane_ && selPane_->session) {
        // Auto-scroll when the drag runs past the top/bottom edge of the pane,
        // so a selection can extend into scrollback (or back toward live
        // output); the anchor is buffer-tracked so it stays put.
        const Rect& pr = selPane_->rect;
        if (yi < static_cast<int>(pr.y)) scrollActive(1);
        else if (yi > static_cast<int>(pr.bottom())) scrollActive(-1);
        int cx = 0, cy = 0;
        if (!paneCellAt(selPane_, xi, yi, cx, cy)) return;
        if (!selDragged_ && cx == selDragCX_ && cy == selDragCY_)
            return;  // still inside the press cell: not a drag yet
        selDragged_ = true;
        selPane_->session->selectionDragTo(cx, cy);
        return;
    }
    // Not a local gesture: motion goes to mouse-tracking apps (the encoder
    // only emits what the app's tracking mode asked for, deduped per cell).
    forwardMouse(2 /*motion*/, (mouseButtonsDown_ & (1 << 1)) ? 1 : 0, xi, yi);
}

void Window::onMouseUp(int xi, int yi) {
    if (tabDragIndex_ >= 0) {
        tabDragIndex_ = -1;
        ReleaseCapture();
        return;
    }
    if (dragDivider_) {
        dragDivider_ = nullptr;
        ReleaseCapture();
        return;
    }
    if (selecting_) {
        selecting_ = false;
        ReleaseCapture();
        maybeCopyOnSelect();  // PuTTY-style copy-on-select (when enabled)
        return;
    }
    // Close out a press that was forwarded to a mouse-tracking app. If the
    // forward is refused (Shift now held / tracking turned off), still drop
    // the button bit so it can't wedge.
    if (mouseButtonsDown_ & (1 << 1)) {
        if (!forwardMouse(1 /*release*/, 1, xi, yi))
            mouseButtonsDown_ &= ~(1 << 1);
    }
}

bool Window::paneCellAt(const Pane* p, int px, int py, int& cx, int& cy) const {
    if (!p || !p->session) return false;
    const Grid& g = p->session->grid();
    if (g.cols < 1 || g.rows < 1) return false;
    int x = static_cast<int>((px - p->rect.x) / metrics_.cellW);
    int y = static_cast<int>((py - p->rect.y) / metrics_.cellH);
    cx = x < 0 ? 0 : (x >= g.cols ? g.cols - 1 : x);
    cy = y < 0 ? 0 : (y >= g.rows ? g.rows - 1 : y);
    return true;
}

void Window::clearSelection() {
    selecting_ = false;
    if (selPane_ && selPane_->session) selPane_->session->selectionClear();
    selPane_ = nullptr;
    dragDivider_ = nullptr;
    tabDragIndex_ = -1;
}

bool Window::paneHasSelection() const {
    return selPane_ && selPane_->session && selPane_->session->hasSelection();
}

std::wstring Window::selectionText() const {
    if (!selPane_ || !selPane_->session) return L"";
    return utf8ToWide(selPane_->session->selectionUtf8());
}

void Window::copySelection() {
    const std::wstring text = selectionText();
    if (text.empty()) return;
    // The core emits LF line breaks; the Windows clipboard wants CRLF.
    std::wstring crlf;
    crlf.reserve(text.size() + 16);
    for (wchar_t c : text) {
        if (c == L'\n') crlf += L"\r\n";
        else crlf.push_back(c);
    }
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const size_t bytes = (crlf.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, crlf.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

void Window::paste() {
    auto* s = activeSession();
    if (!s || !OpenClipboard(hwnd_)) return;
    std::wstring text;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            text = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (text.empty()) return;

    // Normalize CRLF / LF to CR (what shells expect from "Enter").
    std::wstring norm;
    norm.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (c == L'\r') {
            norm.push_back(L'\r');
            if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
        } else if (c == L'\n') {
            norm.push_back(L'\r');
        } else {
            norm.push_back(c);
        }
    }

    // Multi-line pastes execute every embedded newline as "Enter"; confirm
    // first so a stray copy can't run commands (config: multiLinePasteWarning).
    if (multiLinePasteWarning_ && norm.find(L'\r') != std::wstring::npos) {
        int newlines = 0;
        for (wchar_t c : norm) if (c == L'\r') ++newlines;
        const std::wstring msg =
            L"The clipboard contains " + std::to_wstring(newlines + 1) +
            L" lines; each line break runs as Enter.\n\nPaste anyway?";
        if (MessageBoxW(hwnd_, msg.c_str(), L"liney-win — paste",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
            return;
    }

    int bytes = WideCharToMultiByte(CP_UTF8, 0, norm.data(),
                                    static_cast<int>(norm.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, norm.data(), static_cast<int>(norm.size()),
                        utf8.data(), bytes, nullptr, nullptr);

    s->scrollToBottom();
    if (s->bracketedPaste()) {
        const std::string out = "\x1b[200~" + utf8 + "\x1b[201~";
        s->sendBytes(out.data(), out.size());
    } else {
        s->sendBytes(utf8.data(), utf8.size());
    }
}

void Window::selectWordAt(Pane* p, int cx, int cy) {
    if (!p || !p->session) return;
    if (selPane_ && selPane_ != p && selPane_->session)
        selPane_->session->selectionClear();
    selPane_ = p;
    p->session->selectionWord(cx, cy);  // core word rules (CJK-aware)
}

void Window::selectLineAt(Pane* p, int cy) {
    if (!p || !p->session) return;
    if (selPane_ && selPane_ != p && selPane_->session)
        selPane_->session->selectionClear();
    selPane_ = p;
    p->session->selectionLine(0, cy);
}

void Window::selectAllActive() {
    Tab* t = activeTab();
    if (!t || !t->active() || !t->active()->session) return;
    if (selPane_ && selPane_ != t->active() && selPane_->session)
        selPane_->session->selectionClear();
    selPane_ = t->active();
    selPane_->session->selectionAll();  // whole buffer, scrollback included
}

void Window::maybeCopyOnSelect() {
    if (copyOnSelect_ && paneHasSelection()) copySelection();  // keep highlight
}

bool Window::forwardMouse(int action, int button, int xi, int yi) {
    if (keyDown(VK_SHIFT)) return false;  // Shift bypasses to local selection
    Tab* t = activeTab();
    if (!t) return false;
    Pane* leaf = t->hitTest(static_cast<float>(xi), static_cast<float>(yi));
    if (!leaf || !leaf->session) return false;
    TerminalSession* s = leaf->session.get();
    if (!s->mouseTracking()) {
        mouseButtonsDown_ = 0;
        return false;
    }
    const Rect& pr = leaf->rect;
    const std::string seq = s->encodeMouse(
        action, button, static_cast<float>(xi) - pr.x,
        static_cast<float>(yi) - pr.y, false, keyDown(VK_CONTROL),
        keyDown(VK_MENU), mouseButtonsDown_ != 0,
        static_cast<unsigned>(metrics_.cellW),
        static_cast<unsigned>(metrics_.cellH), static_cast<unsigned>(pr.w),
        static_cast<unsigned>(pr.h));
    if (action == 0 && button >= 1 && button <= 3)
        mouseButtonsDown_ |= 1 << button;
    else if (action == 1)
        mouseButtonsDown_ &= ~(1 << button);
    if (!seq.empty()) s->sendBytes(seq.data(), seq.size());
    return true;
}

bool Window::updateCursor() {
    POINT pt{};
    if (!GetCursorPos(&pt)) return false;
    ScreenToClient(hwnd_, &pt);
    const float x = static_cast<float>(pt.x), y = static_cast<float>(pt.y);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);

    if (panes.contains(x, y)) {
        if (Tab* t = activeTab()) {
            if (Pane* d = t->splitDividerAt(x, y, 4.0f)) {
                SetCursor(LoadCursorW(nullptr,
                    d->dir == SplitDir::Cols ? IDC_SIZEWE : IDC_SIZENS));
                return true;
            }
        }
        if (findActive_ && findBarRect_.contains(x, y)) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return true;
        }
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));  // over terminal text
        return true;
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));      // chrome
    return true;
}

void Window::openPaneMenu(int xi, int yi) {
    // Focus the pane under the cursor so copy/paste target it.
    if (Tab* t = activeTab())
        if (Pane* leaf = t->hitTest(static_cast<float>(xi), static_cast<float>(yi)))
            t->setActive(leaf);

    POINT pt{ xi, yi };
    ClientToScreen(hwnd_, &pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (paneHasSelection() ? 0 : MF_GRAYED), 1,
                L"Copy\tCtrl+Shift+C");
    AppendMenuW(m, MF_STRING, 2, L"Paste\tShift+Insert");
    AppendMenuW(m, MF_STRING, 3, L"Select all\tCtrl+Shift+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4, L"Find…\tCtrl+F");
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y,
                                   0, hwnd_, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case 1: copySelection(); clearSelection(); break;
    case 2: paste(); break;
    case 3: selectAllActive(); break;
    case 4: openFind(); break;
    default: break;
    }
}

} // namespace liney
