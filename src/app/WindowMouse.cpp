#include "app/Window.h"
#include "app/WindowInternal.h"
#include "util/InputBox.h"

#include <algorithm>
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
        // Begin a selection drag from this cell (a plain click selects nothing
        // until the mouse moves).
        int cx = 0, cy = 0;
        if (paneCellAt(leaf, xi, yi, cx, cy)) {
            selecting_ = true;
            selPane_ = leaf;
            selAX_ = selBX_ = cx;
            selAY_ = selBY_ = cy;
            hasSelection_ = false;
            SetCapture(hwnd_);
        }
    }
}

void Window::onMouseDownRight(int xi, int yi) {
    const float x = static_cast<float>(xi), y = static_cast<float>(yi);
    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
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

} // namespace liney
