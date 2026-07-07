#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <string>

namespace liney {

std::wstring Window::resolveRepoIcon(const Repo& repo) const {
    // 1) explicit config mapping (repo name -> icon path)
    for (const auto& pi : projectIcons_)
        if (pi.first == repo.name && !pi.second.empty()) return pi.second;
    // 2) a repo-local icon file
    static const wchar_t* kCandidates[] = {
        L"\\icon.png", L"\\icon.ico", L"\\logo.png", L"\\.liney\\icon.png" };
    for (const wchar_t* c : kCandidates) {
        std::wstring p = repo.path + c;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return L"";
}

void Window::drawLeftSidebar(const Rect& r) {
    renderer_->fillRect(r.x, r.y, r.w, r.h, kSidebarBg);

    const float pad = 8.0f;
    const float rowH = metrics_.rowH();
    float y = r.y + 6.0f;

    // Draw a small vector icon at ix, then the label after it.
    auto iconRow = [&](IconKind k, float ix, float ty, const std::wstring& txt,
                       const Color& tc, const Color& ic) {
        const float isz = metrics_.cellH * 0.78f;
        renderer_->drawIcon(k, ix, ty + (rowH - isz) * 0.5f, isz, ic);
        const float tx = ix + isz + 5.0f;
        renderer_->drawText(txt, tx, ty, r.x + r.w - tx - pad, rowH, tc, false);
    };

    renderer_->drawText(L"WORKSPACE", r.x + pad, y, r.w - pad, rowH, kSidebarHdr,
                        true);
    // "+" add-project button at the right of the header row.
    {
        const float bw = rowH;
        const float bx = r.x + r.w - bw - 2.0f;
        workspaceAddRect_ = { bx, y - 2.0f, bw, rowH };
        renderer_->drawText(L"+", bx + bw * 0.28f, y, bw, rowH, kAccent, true);
    }
    y += rowH + 2.0f;

    auto& repos = workspace_.repos();
    if (repos.empty()) {
        renderer_->drawText(L"(no git repos found)", r.x + pad, y, r.w - pad,
                            rowH, kDim, false);
        y += rowH;
    }
    const float iconSz = metrics_.cellH;  // square project icon
    for (int i = 0; i < static_cast<int>(repos.size()); ++i) {
        if (y > r.bottom()) break;
        Repo& repo = repos[i];
        // expand chevron, then a project icon, then the name.
        renderer_->drawText(repo.expanded ? L"v" : L">", r.x + pad, y,
                            metrics_.cellW * 1.5f, rowH, kDim, true);
        const float iconX = r.x + pad + metrics_.cellW * 1.5f;
        const float iconY = y + (rowH - iconSz) * 0.5f;
        std::wstring iconPath = resolveRepoIcon(repo);
        if (iconPath.empty() ||
            !renderer_->drawImage(iconPath, iconX, iconY, iconSz, iconSz)) {
            // Default: a folder glyph (tinted by repo) instead of a blank box.
            static const Color kRepoTints[] = {
                { 120, 200, 160 }, { 130, 170, 230 }, { 220, 170, 110 },
                { 200, 140, 200 }, { 210, 130, 130 }, { 150, 190, 120 } };
            renderer_->drawIcon(IconKind::Folder, iconX, iconY, iconSz,
                                kRepoTints[i % 6]);
        }
        const float nameX = iconX + iconSz + 6.0f;
        renderer_->drawText(repo.name, nameX, y, r.x + r.w - nameX - pad, rowH,
                            kText, true);
        sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::RepoHeader, i, -1, L"" });
        y += rowH;

        if (repo.expanded) {
            for (int w = 0; w < static_cast<int>(repo.worktrees.size()); ++w) {
                iconRow(IconKind::Branch, r.x + pad + metrics_.cellW * 2.0f, y,
                        repo.worktrees[w].label, kDim, kAccent);
                sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::Worktree, i, w, L"" });
                y += rowH;
            }
        }
    }

    // ---- SSH: configured hosts; click to open `ssh <host>` in a new tab -----
    if (!sshHosts_.empty()) {
        y += rowH * 0.5f;
        renderer_->drawText(L"SSH", r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
        y += rowH + 2.0f;
        for (int i = 0; i < static_cast<int>(sshHosts_.size()); ++i) {
            if (y > r.bottom()) break;
            iconRow(IconKind::Globe, r.x + pad, y, sshHosts_[i], kText, kAccent);
            sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::SshHost, i, -1, L"" });
            y += rowH;
        }
    }

    // ---- AGENTS: configured agent sessions; click to open in a new tab ------
    if (!agents_.empty()) {
        y += rowH * 0.5f;
        renderer_->drawText(L"AGENTS", r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
        y += rowH + 2.0f;
        for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
            if (y > r.bottom()) break;
            iconRow(IconKind::Spark, r.x + pad, y, agents_[i].name, kText, kAccent);
            sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::Agent, i, -1, L"" });
            y += rowH;
        }
    }
}

void Window::drawFilesPanel(const Rect& r) {
    renderer_->fillRect(r.x, r.y, r.w, r.h, kSidebarBg);
    const float pad = 8.0f;
    const float rowH = metrics_.rowH();
    float y = r.y + 6.0f;

    refreshFileList();
    std::wstring header = L"FILES";
    if (!browsePath_.empty()) {
        size_t s = browsePath_.find_last_of(L"\\/");
        header += L"  " + (s == std::wstring::npos ? browsePath_
                                                   : browsePath_.substr(s + 1));
    }
    renderer_->drawText(header, r.x + pad, y, r.w - pad, rowH, kSidebarHdr, true);
    y += rowH + 2.0f;

    const float isz = metrics_.cellH * 0.78f;
    auto iconRow = [&](IconKind k, const std::wstring& txt, const Color& tc,
                       const Color& ic) {
        renderer_->drawIcon(k, r.x + pad, y + (rowH - isz) * 0.5f, isz, ic);
        const float tx = r.x + pad + isz + 5.0f;
        renderer_->drawText(txt, tx, y, r.x + r.w - tx - pad, rowH, tc, false);
    };

    if (!browsePath_.empty()) {
        iconRow(IconKind::Up, L"..", kDim, kDim);
        sidebarRows_.push_back({ { r.x, y, r.w, rowH }, RowKind::FileUp, -1, -1, L"" });
        y += rowH;
    }
    for (const FileEntry& e : fileEntries_) {
        if (y > r.bottom()) break;
        iconRow(e.isDir ? IconKind::Folder : IconKind::File, e.name,
                e.isDir ? kText : kDim, e.isDir ? Color{ 220, 190, 110 } : kDim);
        sidebarRows_.push_back(
            { { r.x, y, r.w, rowH },
              e.isDir ? RowKind::FileDir : RowKind::FileEntry, -1, -1, e.path });
        y += rowH;
    }
}

void Window::refreshFileList() {
    // Follow the focused pane's cwd unless the user navigated manually.
    if (TerminalSession* s = activeSession()) {
        if (s->cwd() != lastActiveCwd_) {
            lastActiveCwd_ = s->cwd();
            browsePath_ = s->cwd();
        }
    }
    if (browsePath_ == listedDir_) return;  // already listed
    listedDir_ = browsePath_;
    fileEntries_.clear();
    if (browsePath_.empty()) return;

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((browsePath_ + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        fileEntries_.push_back({ name, browsePath_ + L"\\" + name, dir });
        if (fileEntries_.size() >= 500) break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(fileEntries_.begin(), fileEntries_.end(),
              [](const FileEntry& a, const FileEntry& b) {
                  if (a.isDir != b.isDir) return a.isDir;  // dirs first
                  return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
              });
}

void Window::drawTabBar(const Rect& r) {
    tabRects_.clear();
    renderer_->fillRect(r.x, r.y, r.w, r.h, kTabBg);

    const float pad = metrics_.cellW;
    float x = r.x;
    for (size_t i = 0; i < tabs_.size(); ++i) {
        std::wstring title = tabs_[i]->title();
        if (title.size() > 18) title = title.substr(0, 17) + L"…";
        const float tw = (static_cast<float>(title.size()) + 3.0f) * metrics_.cellW;
        const bool active = (i == activeTab_);
        if (active) renderer_->fillRect(x, r.y, tw, r.h, kTabActiveBg);
        renderer_->drawText(title, x + pad, r.y + 5.0f, tw - pad, metrics_.cellH,
                            active ? kAccent : kDim, active);
        if (active)
            renderer_->fillRect(x, r.bottom() - 2.0f, tw, 2.0f, kAccent);
        tabRects_.push_back({ x, r.y, tw, r.h });
        x += tw;
    }

    // "+" new-tab button.
    const float plusW = metrics_.cellW * 3.0f;
    plusRect_ = { x, r.y, plusW, r.h };
    renderer_->drawText(L"+", x + metrics_.cellW, r.y + 5.0f, plusW,
                        metrics_.cellH, kDim, true);

    // ---- top-right "☰" menu button (opens a native popup) ------------------
    const float bw = r.h;                 // square, tab-bar tall
    const float isz = bw * 0.46f;
    const float bx = r.x + r.w - bw;
    menuButtonRect_ = { bx, r.y, bw, r.h };
    // Highlight if keep-awake is on (a persistent state worth surfacing).
    if (keepAwake_) renderer_->fillRect(bx, r.y, bw, r.h, kTabActiveBg);
    renderer_->drawIcon(IconKind::Menu, bx + (bw - isz) * 0.5f,
                        r.y + (r.h - isz) * 0.5f, isz,
                        keepAwake_ ? kAccent : kText);
}

void Window::drawPanes(const Rect& r) {
    Tab* t = activeTab();
    if (!t) return;
    t->layout(r, metrics_);

    // Refresh find highlights on the owning pane; clear elsewhere. (Selection
    // highlights arrive with the snapshot — the terminal core owns them.)
    // Also stamp keyboard focus: only the active pane of a focused window gets
    // a solid cursor (others draw it hollow).
    const bool winFocused = GetFocus() == hwnd_;
    for (Pane* leaf : t->leaves())
        if (leaf->session) {
            Grid& g = leaf->session->grid();
            g.findMatches.clear();
            g.findCurrent = -1;
            g.focused = winFocused && leaf == t->active();
        }
    if (findActive_) {
        stampFindMatches();
        if (t->active() && t->active()->session) {
            Grid& g = t->active()->session->grid();
            g.findMatches = findMatches_;
            g.findCurrent = findIndex_;
        }
    }

    for (Pane* leaf : t->leaves()) {
        if (!leaf->session) continue;
        const Rect& pr = leaf->rect;
        // Fill the pane background, then clip the grid to the pane so a grid that
        // is momentarily wider than the pane (resize lag / wide output) can never
        // bleed into the sidebar or the right files panel.
        renderer_->fillRect(pr.x, pr.y, pr.w, pr.h, theme_.background);
        renderer_->pushClip(pr.x, pr.y, pr.w, pr.h);
        renderer_->drawGrid(leaf->session->grid(), pr.x, pr.y);
        renderer_->popClip();
        const bool focused = (leaf == t->active());
        renderer_->strokeRect(pr.x, pr.y, pr.w, pr.h, focused ? kAccent : kBorder,
                              focused ? 1.5f : 1.0f);
    }

    // The find bar floats over the focused pane's top-right corner.
    if (findActive_ && t->active()) drawFindBar(t->active()->rect);
}


} // namespace liney
