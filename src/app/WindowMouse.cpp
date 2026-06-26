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
        // until the mouse moves).
        clickStreak_ = 1;
        selecting_ = true;
        selPane_ = leaf;
        selAX_ = selBX_ = cx;
        selAY_ = selBY_ = cy;
        hasSelection_ = false;
        SetCapture(hwnd_);
    }
}

void Window::onMouseDoubleClick(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
    if (!panes.contains(x, y)) { onMouseDown(xi, yi); return; }  // chrome: plain click
    Tab* t = activeTab();
    if (!t) return;
    Pane* leaf = t->hitTest(x, y);
    if (!leaf) return;
    t->setActive(leaf);
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

    // Right-click inside a terminal pane → copy / paste / select-all / find menu.
    if (panes.contains(x, y)) { openPaneMenu(xi, yi); return; }

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
                std::wstring path = workspace_.addWorktree(repo, name);
                if (!path.empty()) newTab(path);
                else MessageBoxW(hwnd_, L"git worktree add failed.", L"liney-win",
                                 MB_OK | MB_ICONERROR);
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
                if (!workspace_.removeWorktree(repo, wt.path))
                    MessageBoxW(hwnd_,
                                L"git worktree remove failed (the main worktree "
                                L"can't be removed).",
                                L"liney-win", MB_OK | MB_ICONERROR);
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
    if (!selecting_ || !selPane_) return;
    // Auto-scroll when the drag runs past the top/bottom edge of the pane, so a
    // selection can extend into scrollback (or back toward live output).
    const Rect& pr = selPane_->rect;
    if (yi < static_cast<int>(pr.y)) scrollActive(1);
    else if (yi > static_cast<int>(pr.bottom())) scrollActive(-1);
    int cx = 0, cy = 0;
    if (!paneCellAt(selPane_, xi, yi, cx, cy)) return;
    selBX_ = cx;
    selBY_ = cy;
    if (selBX_ != selAX_ || selBY_ != selAY_) hasSelection_ = true;
}

void Window::onMouseUp(int /*xi*/, int /*yi*/) {
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
    hasSelection_ = false;
    selPane_ = nullptr;
    dragDivider_ = nullptr;
    tabDragIndex_ = -1;
}

void Window::applySelectionToGrid() {
    if (!hasSelection_ || !selPane_ || !selPane_->session) return;
    int sx = selAX_, sy = selAY_, ex = selBX_, ey = selBY_;
    if (sy > ey || (sy == ey && sx > ex)) { std::swap(sx, ex); std::swap(sy, ey); }
    Grid& g = selPane_->session->grid();
    g.hasSelection = true;
    g.selStartX = sx; g.selStartY = sy;
    g.selEndX = ex; g.selEndY = ey;
}

std::wstring Window::selectionText() const {
    if (!hasSelection_ || !selPane_ || !selPane_->session) return L"";
    const Grid& g = selPane_->session->grid();
    int sx = selAX_, sy = selAY_, ex = selBX_, ey = selBY_;
    if (sy > ey || (sy == ey && sx > ex)) { std::swap(sx, ex); std::swap(sy, ey); }

    std::wstring out;
    for (int y = sy; y <= ey && y < g.rows; ++y) {
        const int x0 = (y == sy) ? sx : 0;
        const int x1 = (y == ey) ? ex : g.cols - 1;
        std::wstring line;
        for (int x = x0; x <= x1 && x < g.cols; ++x) {
            const Cell& c = g.at(x, y);
            line += c.ch.empty() ? L" " : c.ch;
        }
        size_t last = line.find_last_not_of(L' ');
        if (last == std::wstring::npos) line.clear();
        else line.erase(last + 1);
        out += line;
        if (y < ey) out += L"\r\n";
    }
    return out;
}

void Window::copySelection() {
    const std::wstring text = selectionText();
    if (text.empty() || !OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
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

bool Window::isWordChar(const std::wstring& ch) {
    if (ch.empty()) return false;
    const wchar_t c = ch[0];
    if (c == L' ') return false;
    if (iswalnum(c)) return true;
    // Keep common path / identifier punctuation as part of a "word" so a
    // double-click grabs whole filenames, URLs and flags.
    return wcschr(L"_-./\\:~+@", c) != nullptr;
}

void Window::selectWordAt(Pane* p, int cx, int cy) {
    if (!p || !p->session) return;
    const Grid& g = p->session->grid();
    if (cx < 0 || cx >= g.cols || cy < 0 || cy >= g.rows) return;
    selPane_ = p;
    selAY_ = selBY_ = cy;
    if (!isWordChar(g.at(cx, cy).ch)) {
        // Not on a word: select just the single cell.
        selAX_ = selBX_ = cx;
        hasSelection_ = true;
        return;
    }
    int a = cx, b = cx;
    while (a > 0 && isWordChar(g.at(a - 1, cy).ch)) --a;
    while (b < g.cols - 1 && isWordChar(g.at(b + 1, cy).ch)) ++b;
    selAX_ = a;
    selBX_ = b;
    hasSelection_ = true;
}

void Window::selectLineAt(Pane* p, int cy) {
    if (!p || !p->session) return;
    const Grid& g = p->session->grid();
    if (cy < 0 || cy >= g.rows) return;
    int last = g.cols - 1;
    while (last > 0) {
        const std::wstring& ch = g.at(last, cy).ch;
        if (!ch.empty() && ch != L" ") break;
        --last;
    }
    selPane_ = p;
    selAY_ = selBY_ = cy;
    selAX_ = 0;
    selBX_ = last;
    hasSelection_ = true;
}

void Window::selectAllActive() {
    Tab* t = activeTab();
    if (!t || !t->active() || !t->active()->session) return;
    const Grid& g = t->active()->session->grid();
    if (g.cols < 1 || g.rows < 1) return;
    selPane_ = t->active();
    selAX_ = 0;
    selAY_ = 0;
    selBX_ = g.cols - 1;
    selBY_ = g.rows - 1;
    hasSelection_ = true;
}

void Window::maybeCopyOnSelect() {
    if (copyOnSelect_ && hasSelection_) copySelection();  // keep the highlight
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
    AppendMenuW(m, MF_STRING | (hasSelection_ ? 0 : MF_GRAYED), 1,
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
