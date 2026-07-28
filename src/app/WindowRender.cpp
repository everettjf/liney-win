#include "app/Window.h"
#include "app/WindowInternal.h"
#include "app/TabStripLayout.h"
#include "app/BuiltinIcons.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace liney {
namespace {

Color blendColor(const Color& from, const Color& to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    auto channel = [amount](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(std::lround(
            static_cast<float>(a) +
            (static_cast<float>(b) - static_cast<float>(a)) * amount));
    };
    return {channel(from.r, to.r), channel(from.g, to.g),
            channel(from.b, to.b)};
}

bool isLightColor(const Color& color) {
    return static_cast<int>(color.r) * 299 +
               static_cast<int>(color.g) * 587 +
               static_cast<int>(color.b) * 114 >
           155000;
}

std::wstring elideTabTitle(const std::wstring& title, size_t maxChars) {
    if (title.size() <= maxChars) return title;
    if (maxChars < 2) return L"…";
    size_t cut = maxChars - 1;
    if (cut > 0 && title[cut - 1] >= 0xD800 && title[cut - 1] <= 0xDBFF)
        --cut;
    return title.substr(0, cut) + L"…";
}

} // namespace

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
    renderer_->fillRect(r.x, r.y, r.w, r.h, uiTheme_.sidebarBg);
    renderer_->fillRect(r.right() - 1.0f, r.y, 1.0f, r.h, uiTheme_.border);

    const float pad = metrics_.sidebarPad();
    const float rowH = metrics_.rowH();
    const float th = metrics_.cellH;               // text glyph height
    const float tDY = (rowH - th) * 0.5f;          // vertical-center text in a row
    float y = r.y + 10.0f;

    auto hot = [&](const Rect& row) {
        return row.contains(static_cast<float>(lastMouseX_),
                            static_cast<float>(lastMouseY_));
    };
    auto rowBackground = [&](const Rect& row, bool selected = false) {
        if (selected || hot(row))
            renderer_->fillRoundedRect(
                row.x + 4.0f, row.y + 1.0f, row.w - 8.0f, row.h - 2.0f,
                std::min(6.0f * dpiScale_, row.h * 0.24f),
                uiTheme_.tabActiveBg);
        if (selected)
            renderer_->fillRoundedRect(row.x + 4.0f, row.y + 5.0f,
                                       3.0f * dpiScale_, row.h - 10.0f,
                                       1.5f * dpiScale_, uiTheme_.accent);
    };

    // A section header ("WORKSPACE" / "SSH" / "AGENTS"), vertically centered.
    auto header = [&](const wchar_t* txt, float lineRight) {
        renderer_->drawText(txt, r.x + pad, y + tDY, r.w - pad * 2.0f, th,
                            uiTheme_.sidebarHdr, true);
        const float lineX = r.x + pad + metrics_.cellW *
            (static_cast<float>(std::wstring(txt).size()) + 2.0f);
        lineRight = std::min(lineRight, r.right() - pad);
        if (lineX < lineRight)
            renderer_->fillRect(lineX, y + rowH * 0.5f,
                                lineRight - lineX, 1.0f, uiTheme_.border);
    };
    // Draw a small vector icon at ix, then the label after it — both centered
    // vertically against the (roomier) row height.
    auto iconRow = [&](IconKind k, float ix, float ty, const std::wstring& txt,
                       const Color& tc, const Color& ic) {
        const float isz = th * 0.78f;
        renderer_->drawIcon(k, ix, ty + (rowH - isz) * 0.5f, isz, ic);
        const float tx = ix + isz + 7.0f;
        renderer_->drawText(txt, tx, ty + tDY, r.x + r.w - tx - pad, th, tc,
                            false);
    };

    const float workspaceAddW = rowH;
    const float workspaceAddX =
        r.x + r.w - workspaceAddW - pad * 0.5f;
    header(L"WORKSPACE", workspaceAddX - 6.0f * dpiScale_);
    // "+" add-project button at the right of the header row.
    {
        const float bw = workspaceAddW;
        const float bx = workspaceAddX;
        workspaceAddRect_ = { bx, y, bw, rowH };
        if (hot(workspaceAddRect_))
            renderer_->fillRoundedRect(
                bx + 4.0f, y + 3.0f, bw - 8.0f, rowH - 6.0f,
                std::min(6.0f * dpiScale_, rowH * 0.22f),
                uiTheme_.tabActiveBg);
        renderer_->drawTextCentered(
            L"+", workspaceAddRect_.x, workspaceAddRect_.y,
            workspaceAddRect_.w, workspaceAddRect_.h,
            hot(workspaceAddRect_) ? uiTheme_.text : uiTheme_.accent, true);
    }
    y += rowH + 4.0f;

    auto& repos = workspace_.repos();
    if (repos.empty()) {
        const Rect addRow{r.x, y, r.w, rowH};
        rowBackground(addRow);
        renderer_->drawIcon(IconKind::Folder, r.x + pad, y + (rowH - th * 0.78f) * 0.5f,
                            th * 0.78f, uiTheme_.accent);
        renderer_->drawText(L"Add project folder", r.x + pad + th + 7.0f,
                            y + tDY, r.w - pad * 2.0f - th, th,
                            uiTheme_.text, false);
        y += rowH;
        if (!recentProjects_.empty()) {
            y += metrics_.sectionGap();
            header(L"RECENT", r.right() - pad);
            y += rowH + 4.0f;
            for (size_t i = 0; i < recentProjects_.size() && i < 5; ++i) {
                if (y > r.bottom()) break;
                const Rect row{r.x, y, r.w, rowH};
                rowBackground(row);
                const std::wstring& path = recentProjects_[i];
                const size_t slash = path.find_last_of(L"\\/");
                const std::wstring name =
                    slash == std::wstring::npos ? path
                                                : path.substr(slash + 1);
                iconRow(IconKind::Folder, r.x + pad, y, name,
                        uiTheme_.text, uiTheme_.dim);
                sidebarRows_.push_back(
                    {row, RowKind::RecentProject, -1, -1, path});
                y += rowH;
            }
        }
    }
    const float iconSz = th;  // square project icon
    for (int i = 0; i < static_cast<int>(repos.size()); ++i) {
        if (y > r.bottom()) break;
        Repo& repo = repos[i];
        const Rect repoRow{ r.x, y, r.w, rowH };
        const bool projectSelected =
            !repo.isGit() && activeSession() &&
            workspacePathsEqual(activeSession()->cwd(), repo.path);
        rowBackground(repoRow, projectSelected);
        // Git repositories disclose worktrees; ordinary folders open directly.
        if (repo.isGit())
            renderer_->drawText(repo.expanded ? L"v" : L">", r.x + pad,
                                y + tDY, metrics_.cellW * 1.5f, th,
                                uiTheme_.dim, true);
        const float iconX = r.x + pad + metrics_.cellW * 1.5f;
        const float iconY = y + (rowH - iconSz) * 0.5f;
        std::wstring iconPath = resolveRepoIcon(repo);
        const BuiltinIcon* builtin = findBuiltinIcon(iconPath);
        if (builtin) {
            renderer_->drawText(builtin->glyph, iconX, iconY, iconSz + 4.0f,
                                iconSz + 4.0f, uiTheme_.accent, true);
        } else if (iconPath.empty() ||
                   !renderer_->drawImage(iconPath, iconX, iconY, iconSz, iconSz)) {
            // Default: a folder glyph (tinted by repo) instead of a blank box.
            static const Color kRepoTints[] = {
                { 120, 200, 160 }, { 130, 170, 230 }, { 220, 170, 110 },
                { 200, 140, 200 }, { 210, 130, 130 }, { 150, 190, 120 } };
            renderer_->drawIcon(repo.isGit() ? IconKind::Branch
                                             : IconKind::Folder,
                                iconX, iconY, iconSz,
                                kRepoTints[i % 6]);
        }
        const float nameX = iconX + iconSz + 8.0f;
        const bool favorite = std::any_of(
            favoriteProjects_.begin(), favoriteProjects_.end(),
            [&](const std::wstring& path) {
                return workspacePathsEqual(path, repo.path);
            });
        const std::wstring repoLabel =
            favorite ? L"★  " + repo.name : repo.name;
        renderer_->drawText(repoLabel, nameX, y + tDY,
                            r.x + r.w - nameX - pad,
                            th, uiTheme_.text, true);
        sidebarRows_.push_back({ repoRow, RowKind::RepoHeader, i, -1, L"" });
        y += rowH;

        if (repo.expanded) {
            for (int w = 0; w < static_cast<int>(repo.worktrees.size()); ++w) {
                if (y > r.bottom()) break;  // don't emit rows past the panel
                const Worktree& wt = repo.worktrees[w];
                const Rect worktreeRow{ r.x, y, r.w, rowH };
                const bool selected = activeSession() &&
                    _wcsicmp(activeSession()->cwd().c_str(), wt.path.c_str()) == 0;
                rowBackground(worktreeRow, selected);
                std::wstring statusLabel = wt.label;
                if (wt.status.changed > 0)
                    statusLabel += L"  \u25cf " + std::to_wstring(wt.status.changed);
                if (wt.status.ahead > 0)
                    statusLabel += L"  \u2191" + std::to_wstring(wt.status.ahead);
                if (wt.status.behind > 0)
                    statusLabel += L"  \u2193" + std::to_wstring(wt.status.behind);
                iconRow(IconKind::Branch, r.x + pad + metrics_.cellW * 2.0f, y,
                        statusLabel, wt.status.changed > 0 ? uiTheme_.text : uiTheme_.dim,
                        wt.status.changed > 0 ? Color{220, 170, 110} : uiTheme_.accent);
                sidebarRows_.push_back({ worktreeRow, RowKind::Worktree, i, w, L"" });
                y += rowH;
            }
        }
    }

    // ---- SSH: configured hosts; click to open `ssh <host>` in a new tab -----
    if (!sshHosts_.empty()) {
        y += metrics_.sectionGap();
        header(L"SSH", r.right() - pad);
        y += rowH + 4.0f;
        for (int i = 0; i < static_cast<int>(sshHosts_.size()); ++i) {
            if (y > r.bottom()) break;
            const Rect row{ r.x, y, r.w, rowH };
            rowBackground(row);
            iconRow(IconKind::Globe, r.x + pad, y, sshHosts_[i].name,
                    uiTheme_.text, uiTheme_.accent);
            sidebarRows_.push_back({ row, RowKind::SshHost, i, -1, L"" });
            y += rowH;
        }
    }

    // ---- AGENTS: configured agent sessions; click to open in a new tab ------
    if (!agents_.empty()) {
        y += metrics_.sectionGap();
        header(L"AGENTS", r.right() - pad);
        y += rowH + 4.0f;
        for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
            if (y > r.bottom()) break;
            const Rect row{ r.x, y, r.w, rowH };
            rowBackground(row);
            std::wstring label = agents_[i].name;
            Color stateColor = uiTheme_.dim;
            const wchar_t* state = L"idle";
            for (const auto& tab : tabs_) {
                for (Pane* leaf : tab->leaves()) {
                    if (!leaf->session ||
                        leaf->session->context().agentName != agents_[i].name)
                        continue;
                    switch (leaf->session->agentActivity()) {
                    case AgentActivity::Running:
                        state = L"running"; stateColor = uiTheme_.accent; break;
                    case AgentActivity::Waiting:
                        state = L"waiting"; stateColor = Color{230, 185, 90}; break;
                    case AgentActivity::NeedsInput:
                        state = L"needs input"; stateColor = Color{230, 185, 90}; break;
                    case AgentActivity::Done:
                        state = L"done"; stateColor = Color{120, 200, 160}; break;
                    case AgentActivity::Failed:
                        state = L"failed"; stateColor = Color{220, 120, 120}; break;
                    case AgentActivity::Idle: break;
                    }
                }
            }
            label += L"  ";
            label += state;
            iconRow(IconKind::Spark, r.x + pad, y, label, uiTheme_.text,
                    stateColor);
            sidebarRows_.push_back({ row, RowKind::Agent, i, -1, L"" });
            y += rowH;
        }
    }
}

void Window::drawFilesPanel(const Rect& r) {
    fileBreadcrumbs_.clear();
    renderer_->fillRect(r.x, r.y, r.w, r.h, uiTheme_.sidebarBg);
    renderer_->fillRect(r.x, r.y, 1.0f, r.h, uiTheme_.border);
    const float pad = metrics_.sidebarPad();
    const float rowH = metrics_.rowH();
    const float th = metrics_.cellH;
    const float tDY = (rowH - th) * 0.5f;
    float y = r.y + 10.0f;
    auto rowBackground = [&](const Rect& row) {
        if (row.contains(static_cast<float>(lastMouseX_),
                         static_cast<float>(lastMouseY_)))
            renderer_->fillRoundedRect(
                row.x + 4.0f, row.y + 1.0f, row.w - 8.0f, row.h - 2.0f,
                std::min(6.0f * dpiScale_, row.h * 0.24f),
                uiTheme_.tabActiveBg);
    };

    refreshFileList();
    renderer_->drawText(L"FILES", r.x + pad, y + tDY,
                        metrics_.cellW * 6.0f, th,
                        uiTheme_.sidebarHdr, true);
    if (!browsePath_.empty()) {
        std::vector<std::pair<std::wstring, std::wstring>> crumbs;
        std::wstring cumulative;
        size_t start = 0;
        while (start < browsePath_.size()) {
            size_t slash = browsePath_.find_first_of(L"\\/", start);
            const size_t end =
                slash == std::wstring::npos ? browsePath_.size() : slash;
            std::wstring part = browsePath_.substr(start, end - start);
            if (!part.empty()) {
                if (!cumulative.empty() && cumulative.back() != L'\\')
                    cumulative += L'\\';
                cumulative += part;
                crumbs.push_back({part, cumulative});
            }
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
        const size_t first = crumbs.size() > 2 ? crumbs.size() - 2 : 0;
        float bx = r.x + pad + metrics_.cellW * 6.0f;
        for (size_t i = first; i < crumbs.size(); ++i) {
            if (i > first) {
                renderer_->drawText(L"›", bx, y + tDY,
                                    metrics_.cellW * 2.0f, th,
                                    uiTheme_.dim, false);
                bx += metrics_.cellW * 1.6f;
            }
            const float width = std::min(
                metrics_.cellW *
                    (static_cast<float>(crumbs[i].first.size()) + 1.0f),
                std::max(1.0f, r.right() - pad - bx));
            Rect hit{bx, y, width, rowH};
            renderer_->drawText(crumbs[i].first, bx, y + tDY, width, th,
                                i + 1 == crumbs.size() ? uiTheme_.text
                                                      : uiTheme_.dim,
                                i + 1 == crumbs.size());
            fileBreadcrumbs_.push_back({hit, crumbs[i].second});
            bx += width;
            if (bx >= r.right() - pad) break;
        }
    }
    y += rowH + 4.0f;

    const float isz = th * 0.78f;
    auto iconRow = [&](IconKind k, const std::wstring& txt, const Color& tc,
                       const Color& ic) {
        renderer_->drawIcon(k, r.x + pad, y + (rowH - isz) * 0.5f, isz, ic);
        const float tx = r.x + pad + isz + 7.0f;
        renderer_->drawText(txt, tx, y + tDY, r.x + r.w - tx - pad, th, tc,
                            false);
    };

    if (!browsePath_.empty()) {
        const Rect row{ r.x, y, r.w, rowH };
        rowBackground(row);
        iconRow(IconKind::Up, L"..", uiTheme_.dim, uiTheme_.dim);
        sidebarRows_.push_back({ row, RowKind::FileUp, -1, -1, L"" });
        y += rowH;
    }
    for (const FileEntry& e : fileEntries_) {
        if (y > r.bottom()) break;
        const Rect row{ r.x, y, r.w, rowH };
        rowBackground(row);
        iconRow(e.isDir ? IconKind::Folder : IconKind::File, e.name,
                e.isDir ? uiTheme_.text : uiTheme_.dim, e.isDir ? Color{ 220, 190, 110 } : uiTheme_.dim);
        sidebarRows_.push_back(
            { row,
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
    tabRects_.assign(tabs_.size(), Rect{});
    tabCloseRects_.assign(tabs_.size(), Rect{});
    tabOverflowRect_ = {};
    renderer_->fillRect(r.x, r.y, r.w, r.h, uiTheme_.tabBg);

    const bool light = isLightColor(uiTheme_.tabBg);
    const Color hoverBg =
        blendColor(uiTheme_.tabBg, uiTheme_.text, light ? 0.08f : 0.11f);
    const Color shadow =
        blendColor(uiTheme_.tabBg, Color{0, 0, 0}, light ? 0.16f : 0.48f);
    const Color activeBorder =
        blendColor(uiTheme_.tabActiveBg, uiTheme_.accent, light ? 0.22f : 0.30f);
    const float radius = std::clamp(r.h * 0.20f, 4.0f, 8.0f);
    const float controlInset = std::max(4.0f, r.h * 0.13f);
    const float textY = r.y + (r.h - metrics_.cellH) * 0.5f + 1.0f;
    const float closeW = std::max(metrics_.cellH, 24.0f * dpiScale_);
    const float bw = r.h;
    const float actionsLeft = r.right() - bw * 3.0f;

    // A persistent disclosure button makes the whole sidebar directly
    // collapsible and, importantly, remains available to restore it.
    const float toggleW = r.h;
    sidebarToggleRect_ = { r.x, r.y, toggleW, r.h };
    const bool toggleHot = sidebarToggleRect_.contains(
        static_cast<float>(lastMouseX_), static_cast<float>(lastMouseY_));
    if (toggleHot)
        renderer_->fillRoundedRect(
            r.x + controlInset, r.y + controlInset, toggleW - 2 * controlInset,
            r.h - 2 * controlInset, radius * 0.75f, hoverBg);
    renderer_->drawTextCentered(
        sidebarEffectiveVisible_ ? L"‹" : L"›", sidebarToggleRect_.x,
        sidebarToggleRect_.y, sidebarToggleRect_.w, sidebarToggleRect_.h,
        toggleHot ? uiTheme_.text : uiTheme_.dim, true);

    const float tabsStart = r.x + toggleW;
    const float plusW = std::max(metrics_.cellW * 3.0f, 34.0f * dpiScale_);
    const float overflowW = plusW;
    const float available =
        std::max(0.0f, actionsLeft - tabsStart - plusW - 2.0f);
    // Overflow before labels collapse into indistinguishable "prof..." chips.
    // Reserve roughly twelve readable glyphs in addition to close/padding;
    // the active-centered overflow window then keeps fewer, useful neighbors.
    const float minTabW =
        std::max(metrics_.cellW * 12.0f + closeW + 10.0f * dpiScale_,
                 116.0f * dpiScale_);
    const float maxTabW =
        std::max(metrics_.cellW * 18.0f, 190.0f * dpiScale_);

    std::vector<std::wstring> titles;
    std::vector<float> preferredWidths;
    titles.reserve(tabs_.size());
    preferredWidths.reserve(tabs_.size());
    for (const auto& tab : tabs_) {
        std::wstring title = tab->title();
        if (tab->pinned()) title = L"●  " + title;
        titles.push_back(std::move(title));
        const float natural =
            (static_cast<float>(titles.back().size()) + 2.5f) * metrics_.cellW +
            closeW;
        preferredWidths.push_back(std::clamp(natural, minTabW, maxTabW));
    }

    const auto layout =
        layoutTabStrip(preferredWidths, activeTab_, available, minTabW,
                       overflowW);
    float visibleEnd = 0.0f;
    for (const TabStripItem& item : layout.items) {
        const size_t i = item.index;
        const float x = tabsStart + item.x;
        const float tw = item.width;
        const bool active = (i == activeTab_);
        const bool hot = static_cast<int>(i) == hoverTab_;
        const float cardX = x + 1.0f;
        const float cardW = std::max(1.0f, tw - 2.0f);
        if (active) {
            // Extend below the clipped strip so only the top corners show,
            // matching native TabView/Windows Terminal's attached-card shape.
            renderer_->fillRoundedRect(cardX, r.y + 5.0f, cardW,
                                       r.h + radius, radius, shadow);
            renderer_->fillRoundedRect(cardX, r.y + 3.0f, cardW,
                                       r.h + radius, radius,
                                       uiTheme_.tabActiveBg);
            renderer_->strokeRoundedRect(cardX, r.y + 3.0f, cardW,
                                         r.h + radius, radius, activeBorder,
                                         1.0f);
        } else if (hot) {
            renderer_->fillRoundedRect(cardX, r.y + 4.0f, cardW,
                                       r.h - 7.0f, radius, hoverBg);
        }

        const float textX = x + std::max(metrics_.cellW, 10.0f * dpiScale_);
        const float textW = std::max(1.0f, tw - (textX - x) - closeW);
        const size_t maxChars = std::max<size_t>(
            1, static_cast<size_t>(std::floor(textW / metrics_.cellW)));
        renderer_->drawText(elideTabTitle(titles[i], maxChars), textX, textY,
                            textW, metrics_.cellH,
                            active ? uiTheme_.text : uiTheme_.dim, active);
        if (active) {
            const float indicatorW =
                std::clamp(tw * 0.28f, 24.0f * dpiScale_, 54.0f * dpiScale_);
            renderer_->fillRoundedRect(
                x + (tw - indicatorW) * 0.5f, r.bottom() - 3.0f, indicatorW,
                3.0f, 1.5f, uiTheme_.accent);
        }

        // × close button — shown on the active or hovered tab (the whole area
        // stays clickable regardless, matching browser/VS Code behavior).
        const Rect closeRect{ x + tw - closeW, r.y, closeW, r.h };
        tabCloseRects_[i] = closeRect;
        if (active || hot) {
            const bool hotClose = closeRect.contains(
                static_cast<float>(lastMouseX_), static_cast<float>(lastMouseY_));
            if (hotClose)
                renderer_->fillRoundedRect(
                    closeRect.x + controlInset * 0.65f,
                    closeRect.y + controlInset * 0.75f,
                    closeRect.w - controlInset * 1.3f,
                    closeRect.h - controlInset * 1.5f, radius * 0.65f,
                    blendColor(hoverBg, uiTheme_.text,
                               light ? 0.05f : 0.08f));
            renderer_->drawTextCentered(
                L"×", closeRect.x, closeRect.y, closeRect.w, closeRect.h,
                hotClose ? uiTheme_.text : uiTheme_.dim, true);
        }
        tabRects_[i] = {x, r.y, tw, r.h};
        visibleEnd = item.x + item.width;
    }

    float controlsX = tabsStart + visibleEnd;
    if (layout.overflow) {
        tabOverflowRect_ = {controlsX, r.y, overflowW, r.h};
        const bool overflowHot = tabOverflowRect_.contains(
            static_cast<float>(lastMouseX_), static_cast<float>(lastMouseY_));
        if (overflowHot)
            renderer_->fillRoundedRect(
                controlsX + controlInset, r.y + controlInset,
                overflowW - 2 * controlInset, r.h - 2 * controlInset,
                radius * 0.75f, hoverBg);
        renderer_->drawTextCentered(
            L"⋯", tabOverflowRect_.x, tabOverflowRect_.y, tabOverflowRect_.w,
            tabOverflowRect_.h,
            overflowHot ? uiTheme_.text : uiTheme_.dim, true);
        controlsX += overflowW;
    }

    // "+" new-tab button, always kept before the fixed top-right toolbar.
    const float plusLimit = std::max(tabsStart, actionsLeft - plusW);
    const float plusX = std::min(controlsX, plusLimit);
    plusRect_ = { plusX, r.y, plusW, r.h };
    const bool plusHot = plusRect_.contains(
        static_cast<float>(lastMouseX_), static_cast<float>(lastMouseY_));
    if (plusHot)
        renderer_->fillRoundedRect(
            plusX + controlInset, r.y + controlInset,
            plusW - 2 * controlInset, r.h - 2 * controlInset,
            radius * 0.75f, hoverBg);
    renderer_->drawTextCentered(L"+", plusRect_.x, plusRect_.y, plusRect_.w,
                                plusRect_.h,
                                plusHot ? uiTheme_.text : uiTheme_.dim, true);

    // ---- top-right contextual menus: folder, coffee, more -----------------
    // Mask any overflowing tab title before painting the fixed toolbar.
    renderer_->fillRect(actionsLeft, r.y, bw * 3.0f, r.h, uiTheme_.tabBg);
    renderer_->fillRect(actionsLeft, r.y + 6.0f, 1.0f, r.h - 12.0f,
                        uiTheme_.border);
    const float isz = bw * 0.46f;
    float bx = r.x + r.w - bw;
    menuButtonRect_ = { bx, r.y, bw, r.h };
    if (menuButtonRect_.contains(static_cast<float>(lastMouseX_),
                                 static_cast<float>(lastMouseY_)))
        renderer_->fillRoundedRect(
            bx + controlInset, r.y + controlInset, bw - 2 * controlInset,
            r.h - 2 * controlInset, radius * 0.75f, hoverBg);
    renderer_->drawIcon(IconKind::Menu, bx + (bw - isz) * 0.5f,
                        r.y + (r.h - isz) * 0.5f, isz,
                        uiTheme_.text);

    bx -= bw;
    awakeButtonRect_ = { bx, r.y, bw, r.h };
    if (keepAwake_ || awakeButtonRect_.contains(
                          static_cast<float>(lastMouseX_),
                          static_cast<float>(lastMouseY_)))
        renderer_->fillRoundedRect(
            bx + controlInset, r.y + controlInset, bw - 2 * controlInset,
            r.h - 2 * controlInset, radius * 0.75f, hoverBg);
    renderer_->drawIcon(IconKind::Coffee, bx + (bw - isz) * 0.5f,
                        r.y + (r.h - isz) * 0.5f, isz,
                        keepAwake_ ? uiTheme_.accent : uiTheme_.text);
    if (keepAwake_)
        renderer_->fillRoundedRect(
            bx + bw * 0.32f, r.bottom() - 4.0f, bw * 0.36f, 3.0f,
            1.5f, uiTheme_.accent);
    bx -= bw;
    openButtonRect_ = { bx, r.y, bw, r.h };
    if (openButtonRect_.contains(static_cast<float>(lastMouseX_),
                                 static_cast<float>(lastMouseY_)))
        renderer_->fillRoundedRect(
            bx + controlInset, r.y + controlInset, bw - 2 * controlInset,
            r.h - 2 * controlInset, radius * 0.75f, hoverBg);
    renderer_->drawIcon(IconKind::Folder, bx + (bw - isz) * 0.5f,
                        r.y + (r.h - isz) * 0.5f, isz, uiTheme_.text);
    renderer_->fillRect(r.x, r.bottom() - 1.0f, r.w, 1.0f, uiTheme_.border);
}

void Window::drawPanes(const Rect& r) {
    paneCloseRect_ = {};
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
        if (pr.w <= 0.0f || pr.h <= 0.0f) continue;
        // Fill the pane background, then clip the grid to the pane so a grid that
        // is momentarily wider than the pane (resize lag / wide output) can never
        // bleed into the sidebar or the right files panel.
        renderer_->fillRect(pr.x, pr.y, pr.w, pr.h, theme_.background);
        renderer_->pushClip(pr.x, pr.y, pr.w, pr.h);
        const float pad = metrics_.panePad();
        renderer_->drawGrid(leaf->session->grid(), pr.x + pad, pr.y + pad);
        const uint64_t viewportTop = leaf->session->viewportRow();
        for (const InlineImage& image : leaf->session->inlineImages()) {
            if (image.row < viewportTop) continue;
            const uint64_t relativeRow = image.row - viewportTop;
            if (relativeRow >= static_cast<uint64_t>(
                                   leaf->session->grid().rows))
                continue;
            const float ix =
                pr.x + pad + static_cast<float>(image.column) * metrics_.cellW;
            const float iy =
                pr.y + pad + static_cast<float>(relativeRow) * metrics_.cellH;
            const float iw =
                static_cast<float>(image.widthCells) * metrics_.cellW;
            const float ih =
                static_cast<float>(image.heightCells) * metrics_.cellH;
            if (renderer_->drawImage(image.path, ix, iy, iw, ih))
                SetPropW(hwnd_, L"Liney.InlineImageActive",
                         reinterpret_cast<HANDLE>(1));
        }
        renderer_->popClip();
        if (leaf->session->exited()) {
            const float bh = metrics_.cellH + 10.0f;
            const float bw = std::min(pr.w - 16.0f, metrics_.cellW * 38.0f);
            const float bx = pr.x + 8.0f;
            const float by = pr.y + pr.h - bh - 8.0f;
            renderer_->fillRoundedRect(bx + 2.0f, by + 3.0f, bw, bh,
                                       6.0f * dpiScale_,
                                       uiTheme_.workspaceBg);
            renderer_->fillRoundedRect(bx, by, bw, bh, 6.0f * dpiScale_,
                                       uiTheme_.tabActiveBg);
            renderer_->strokeRoundedRect(bx, by, bw, bh,
                                         6.0f * dpiScale_, uiTheme_.accent,
                                         1.0f);
            renderer_->drawText(L"Shell exited - right-click to restart",
                                bx + 8.0f, by + 4.0f, bw - 16.0f,
                                metrics_.cellH, uiTheme_.text, true);
        }
        const bool focused = (leaf == t->active());
        renderer_->strokeRect(pr.x, pr.y, pr.w, pr.h, uiTheme_.border, 1.0f);
        // In split layouts, a short edge marker identifies keyboard focus
        // without surrounding the entire terminal in a high-salience neon
        // rectangle. A single pane needs no extra focus decoration.
        if (focused && t->isSplit()) {
            const float marker = std::min(pr.w, 42.0f * dpiScale_);
            renderer_->fillRoundedRect(
                pr.x + (pr.w - marker) * 0.5f, pr.y,
                marker, std::max(2.0f, 2.0f * dpiScale_),
                1.0f * dpiScale_, uiTheme_.accent);
        }
    }

    // A compact active-pane control keeps closing discoverable even in a deep
    // split tree. The count clarifies scope; Ctrl+Shift+W and this × close one
    // pane, while the tab × closes the whole tab.
    if (t->isSplit() && !findActive_ && t->active()) {
        const Rect& pr = t->active()->rect;
        const size_t count = t->leaves().size();
        if (t->compactLayout() && !compactLayoutNotified_) {
            showToast(L"Some panes are hidden");
            compactLayoutNotified_ = true;
        } else if (!t->compactLayout()) {
            compactLayoutNotified_ = false;
        }
        const float h = std::max(metrics_.cellH + 6.0f,
                                 26.0f * dpiScale_);
        const bool showCount = !t->zoom() && !t->compactLayout() &&
                               pr.w >= 150.0f * dpiScale_;
        const std::wstring countLabel =
            (t->compactLayout() ? L"Compact  ·  " : L"") +
            std::to_wstring(count) + (count == 1 ? L" pane" : L" panes");
        const float labelW =
            showCount ? std::max(58.0f * dpiScale_,
                                 metrics_.cellW *
                                     (static_cast<float>(countLabel.size()) +
                                      1.5f))
                      : 0.0f;
        const float closeW = h;
        const float totalW = labelW + closeW;
        const float x = std::max(pr.x + 6.0f,
                                 pr.right() - totalW - 8.0f);
        const float y = pr.y + 8.0f;
        const float radius = h * 0.28f;
        renderer_->fillRoundedRect(x + 2.0f, y + 3.0f, totalW, h, radius,
                                   uiTheme_.workspaceBg);
        renderer_->fillRoundedRect(x, y, totalW, h, radius,
                                   uiTheme_.tabActiveBg);
        renderer_->strokeRoundedRect(x, y, totalW, h, radius,
                                     uiTheme_.border, 1.0f);
        if (showCount)
            renderer_->drawText(countLabel, x + 9.0f, y + 3.0f,
                                labelW - 9.0f, metrics_.cellH,
                                uiTheme_.dim, false);
        paneCloseRect_ = {x + labelW, y, closeW, h};
        const bool closeHot = paneCloseRect_.contains(
            static_cast<float>(lastMouseX_),
            static_cast<float>(lastMouseY_));
        if (showCount)
            renderer_->fillRect(paneCloseRect_.x, y + 5.0f, 1.0f,
                                h - 10.0f, uiTheme_.border);
        if (closeHot)
            renderer_->fillRoundedRect(
                paneCloseRect_.x + 3.0f, y + 3.0f, closeW - 6.0f, h - 6.0f,
                radius * 0.75f,
                blendColor(uiTheme_.tabActiveBg, uiTheme_.text, 0.12f));
        renderer_->drawTextCentered(
            L"×", paneCloseRect_.x, paneCloseRect_.y, paneCloseRect_.w,
            paneCloseRect_.h, closeHot ? uiTheme_.text : uiTheme_.dim, true);
    }

    // A solid accent "ZOOM" pill in the zoomed pane's top-right corner so it's
    // clear the other panes are hidden, not gone.
    if (Pane* z = t->zoom()) {
        const Rect& pr = z->rect;
        const float bw = metrics_.cellW * 6.5f, bh = metrics_.cellH + 6.0f;
        // Top-right of the pane, clamped so it stays on-screen at any width.
        const float rightInset =
            paneCloseRect_.w > 0.0f ? paneCloseRect_.w + 20.0f : 14.0f;
        float bx = pr.x + pr.w - bw - rightInset;
        if (bx < pr.x + 8.0f) bx = pr.x + 8.0f;
        const float by = pr.y + 12.0f;
        renderer_->fillRoundedRect(bx, by, bw, bh, bh * 0.5f,
                                   uiTheme_.accent);
        renderer_->drawText(L"ZOOM", bx + metrics_.cellW, by + 3.0f, bw,
                            metrics_.cellH, uiTheme_.workspaceBg, true);
    }

    // The find bar floats over the focused pane's top-right corner.
    if (findActive_ && t->active()) drawFindBar(t->active()->rect);
}

void Window::drawToast() {
    if (toastMessage_.empty()) return;
    if (GetTickCount64() >= toastUntil_) {
        toastMessage_.clear();
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float margin = 18.0f * dpiScale_;
    const float height = metrics_.cellH + 18.0f * dpiScale_;
    const float width = std::min(
        static_cast<float>(client.right) - margin * 2.0f,
        std::max(220.0f * dpiScale_,
                 metrics_.cellW * (static_cast<float>(toastMessage_.size()) + 4.0f)));
    const float x = (static_cast<float>(client.right) - width) * 0.5f;
    const ULONGLONG now = GetTickCount64();
    BOOL animationsEnabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                          &animationsEnabled, 0);
    const float enter = animationsEnabled
                            ? std::clamp(
                                  static_cast<float>(now - toastStarted_) /
                                      180.0f,
                                  0.0f, 1.0f)
                            : 1.0f;
    // A short ease-out lift gives transient feedback without animating the
    // terminal or looping. The idle render cadence naturally stops afterward.
    const float eased = 1.0f - (1.0f - enter) * (1.0f - enter);
    const float y = static_cast<float>(client.bottom) - height - margin +
                    (1.0f - eased) * 10.0f * dpiScale_;
    const Color border = toastError_ ? Color{220, 90, 90} : uiTheme_.border;
    renderer_->fillRoundedRect(x + 2.0f, y + 3.0f, width, height,
                               7.0f * dpiScale_, uiTheme_.workspaceBg);
    renderer_->fillRoundedRect(x, y, width, height, 7.0f * dpiScale_,
                               uiTheme_.tabActiveBg);
    renderer_->strokeRoundedRect(x, y, width, height, 7.0f * dpiScale_,
                                 border, 1.0f);
    renderer_->fillRoundedRect(x + 5.0f * dpiScale_,
                               y + 8.0f * dpiScale_, 3.0f * dpiScale_,
                               height - 16.0f * dpiScale_,
                               1.5f * dpiScale_,
                               toastError_ ? border : uiTheme_.accent);
    renderer_->drawText(toastMessage_, x + 12.0f * dpiScale_,
                        y + 8.0f * dpiScale_, width - 24.0f * dpiScale_,
                        metrics_.cellH, uiTheme_.text, false);
}

void Window::drawWelcome(const Rect& r) {
    if (tabs_.size() != 1 || !activeTab() ||
        activeTab()->leaves().size() != 1)
        return;
    if (r.w < 360.0f * dpiScale_ || r.h < 260.0f * dpiScale_) return;
    const float width = std::min(r.w - 48.0f * dpiScale_, 520.0f * dpiScale_);
    const float height = 220.0f * dpiScale_;
    const float x = r.x + (r.w - width) * 0.5f;
    const float y = r.y + std::max(28.0f * dpiScale_, (r.h - height) * 0.32f);
    renderer_->fillRoundedRect(x + 3.0f, y + 4.0f, width, height,
                               10.0f * dpiScale_, uiTheme_.workspaceBg);
    renderer_->fillRoundedRect(x, y, width, height, 10.0f * dpiScale_,
                               uiTheme_.tabActiveBg);
    renderer_->strokeRoundedRect(x, y, width, height, 10.0f * dpiScale_,
                                 uiTheme_.border, 1.0f);
    renderer_->drawText(L"Welcome to Liney", x + 24.0f * dpiScale_,
                        y + 22.0f * dpiScale_, width - 48.0f * dpiScale_,
                        metrics_.cellH, uiTheme_.text, true);
    renderer_->drawText(
        L"Add a folder to connect terminals, worktrees and agent tasks.",
        x + 24.0f * dpiScale_, y + 58.0f * dpiScale_,
        width - 48.0f * dpiScale_, metrics_.cellH, uiTheme_.dim, false);
    const float buttonY = y + 108.0f * dpiScale_;
    const float buttonH = 38.0f * dpiScale_;
    const float gap = 12.0f * dpiScale_;
    const float buttonW = (width - 48.0f * dpiScale_ - gap) * 0.5f;
    welcomeOpenRect_ = {x + 24.0f * dpiScale_, buttonY, buttonW, buttonH};
    welcomePaletteRect_ = {welcomeOpenRect_.right() + gap, buttonY,
                           buttonW, buttonH};
    renderer_->fillRoundedRect(welcomeOpenRect_.x, welcomeOpenRect_.y,
                               welcomeOpenRect_.w, welcomeOpenRect_.h,
                               6.0f * dpiScale_, uiTheme_.accent);
    renderer_->drawText(L"Add project folder", welcomeOpenRect_.x + 14.0f * dpiScale_,
                        welcomeOpenRect_.y + 8.0f * dpiScale_,
                        welcomeOpenRect_.w - 28.0f * dpiScale_, metrics_.cellH,
                        uiTheme_.workspaceBg, true);
    renderer_->strokeRoundedRect(welcomePaletteRect_.x, welcomePaletteRect_.y,
                                 welcomePaletteRect_.w, welcomePaletteRect_.h,
                                 6.0f * dpiScale_, uiTheme_.border, 1.0f);
    renderer_->drawText(L"Browse commands", welcomePaletteRect_.x + 14.0f * dpiScale_,
                        welcomePaletteRect_.y + 8.0f * dpiScale_,
                        welcomePaletteRect_.w - 28.0f * dpiScale_, metrics_.cellH,
                        uiTheme_.text, true);
    renderer_->drawText(L"Ctrl + Shift + P   Command palette",
                        x + 24.0f * dpiScale_, y + 174.0f * dpiScale_,
                        width - 48.0f * dpiScale_, metrics_.cellH,
                        uiTheme_.dim, false);
}


} // namespace liney
