#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "core/CommandPalette.h"
#include "core/RenderSignal.h"
#include "core/SshProfiles.h"
#include "util/InputBox.h"

namespace liney {
namespace {

constexpr int kOpenTabActionBase = 30000;

struct PaletteAction {
    int id;
    const wchar_t* name;
    const wchar_t* shortcut;
    const wchar_t* category;
    const wchar_t* filters;
    const wchar_t* keywords;
};

constexpr PaletteAction kActions[] = {
    {1, L"New tab", L"Ctrl+Shift+T", L"Session", L"actions tabs profile",
        L"create shell terminal"},
    {14, L"New window", L"", L"Session", L"actions",
        L"create terminal window"},
    {15, L"New administrator window", L"", L"Session", L"actions tools",
        L"admin elevated run as administrator"},
    {2, L"Split right", L"Alt+D", L"Pane", L"actions pane",
        L"layout columns side by side"},
    {3, L"Split down", L"Shift+Alt+D", L"Pane", L"actions pane",
        L"layout rows stacked"},
    {6, L"Zoom or restore pane", L"Ctrl+Shift+Z", L"Pane", L"actions pane",
        L"maximize focus layout"},
    {7, L"Equalize panes", L"Ctrl+Shift+E", L"Pane", L"actions pane",
        L"balance layout sizes"},
    {21, L"Swap active pane with next pane", L"", L"Pane", L"actions pane",
        L"layout reorder"},
    {22, L"Move active pane to new tab", L"", L"Pane", L"actions pane tabs",
        L"detach layout"},
    {23, L"Move active pane forward", L"", L"Pane", L"actions pane",
        L"layout reorder"},
    {24, L"Close active pane or tab", L"Ctrl+Shift+W", L"Pane",
        L"actions pane tabs", L"remove dismiss"},
    {26, L"Close other panes", L"", L"Pane", L"actions pane",
        L"remove dismiss layout"},
    {18, L"Rename active tab", L"", L"Tabs", L"actions tabs",
        L"title label"},
    {19, L"Pin or unpin active tab", L"", L"Tabs", L"actions tabs",
        L"keep fixed"},
    {20, L"Duplicate active tab", L"", L"Tabs", L"actions tabs",
        L"clone copy"},
    {25, L"Close active tab and all panes", L"", L"Tabs", L"actions tabs",
        L"remove dismiss"},
    {27, L"Close other tabs", L"", L"Tabs", L"actions tabs",
        L"remove dismiss"},
    {4, L"Toggle sidebar", L"Ctrl+Shift+B", L"View", L"actions",
        L"workspace panel show hide"},
    {5, L"Toggle files panel", L"Ctrl+Shift+F", L"View", L"actions",
        L"folder tree show hide"},
    {8, L"Find in terminal", L"Ctrl+F", L"Search", L"actions tools",
        L"text screen output"},
    {16, L"Search command history", L"", L"Search", L"actions tools",
        L"previous shell input"},
    {10, L"Workspace snapshots", L"", L"Workspace",
        L"actions workspace", L"save restore layout sessions"},
    {9, L"Settings", L"Ctrl+,", L"Tools", L"actions tools",
        L"preferences configuration"},
    {11, L"Check for updates", L"Ctrl+Shift+U", L"Tools", L"actions tools",
        L"upgrade release version"},
    {12, L"Toggle keep awake", L"Ctrl+Shift+K", L"Tools", L"actions tools",
        L"sleep power coffee"},
    {17, L"Export diagnostic bundle", L"", L"Tools", L"actions tools",
        L"support logs troubleshoot zip"},
};

} // namespace

void Window::openCommandPalette() {
    paletteActive_ = true;
    paletteQuery_.clear();
    paletteSelected_ = 0;
}

void Window::closeCommandPalette() {
    paletteActive_ = false;
    paletteQuery_.clear();
    paletteSelected_ = 0;
}

std::vector<int> Window::filteredPaletteActions() const {
    std::vector<PaletteSearchItem> candidates;
    int order = 0;
    auto recentRank = [&](int id) {
        const auto found =
            std::find(paletteRecentActions_.begin(), paletteRecentActions_.end(), id);
        return found == paletteRecentActions_.end()
            ? -1
            : static_cast<int>(found - paletteRecentActions_.begin());
    };
    for (const PaletteAction& action : kActions)
        candidates.push_back({action.id, action.name, action.category,
                              action.filters, action.keywords, order++,
                              recentRank(action.id)});
    for (size_t i = 0; i < shellProfiles_.size() && i < 100; ++i)
        candidates.push_back(
            {3000 + static_cast<int>(i), shellProfiles_[i].name, L"Profile",
             L"profile actions", L"new tab shell session", 1000 + order++, -1});
    for (size_t i = 0; i < sshHosts_.size() && i < 100; ++i)
        candidates.push_back(
            {4000 + static_cast<int>(i), sshHosts_[i].name, L"SSH", L"ssh",
             L"remote host connect session", 2000 + order++, -1});
    for (size_t i = 0; i < agents_.size() && i < 100; ++i)
        candidates.push_back(
            {5000 + static_cast<int>(i), agents_[i].name, L"Agent", L"agent",
             L"task automation session", 3000 + order++, -1});
    for (size_t i = 0; i < recentProjects_.size() && i < 100; ++i)
        candidates.push_back(
            {6000 + static_cast<int>(i), recentProjects_[i], L"Recent project",
             L"workspace", L"folder repository open", 4000 + order++, -1});
    for (size_t i = 0;
         i < tabs_.size() &&
         i < static_cast<size_t>(
                 std::numeric_limits<int>::max() - kOpenTabActionBase);
         ++i) {
        const int id = kOpenTabActionBase + static_cast<int>(i);
        candidates.push_back(
            {id, paletteActionLabel(id), L"Tab", L"tabs",
             L"open switch session pane", 5000 + order++, -1});
    }
    const auto& repos = workspace_.repos();
    for (size_t i = 0; i < repos.size() && i < 100; ++i) {
        const int id = 10000 + static_cast<int>(i);
        candidates.push_back(
            {id, repos[i].name, repos[i].isGit() ? L"Git repository" : L"Folder",
             repos[i].isGit() ? L"workspace git" : L"workspace",
             repos[i].path + L" project open", 6000 + order++, -1});
        for (size_t j = 0; j < repos[i].worktrees.size() && j < 100; ++j)
            candidates.push_back(
                {11000 + static_cast<int>(i * 100 + j),
                 repos[i].name + L" / " + repos[i].worktrees[j].label,
                 L"Worktree", L"workspace git",
                 repos[i].worktrees[j].path + L" branch open",
                 7000 + order++, -1});
    }
    return rankPaletteItems(candidates, paletteQuery_);
}

std::wstring Window::paletteActionLabel(int id) const {
    for (const auto& action : kActions)
        if (action.id == id) return action.name;
    if (id >= 3000 && id < 3100) {
        const size_t i = static_cast<size_t>(id - 3000);
        if (i < shellProfiles_.size()) return L"Profile: " + shellProfiles_[i].name;
    }
    if (id >= 4000 && id < 4100) {
        const size_t i = static_cast<size_t>(id - 4000);
        if (i < sshHosts_.size()) return L"SSH: " + sshHosts_[i].name;
    }
    if (id >= 5000 && id < 5100) {
        const size_t i = static_cast<size_t>(id - 5000);
        if (i < agents_.size()) return L"Agent: " + agents_[i].name;
    }
    if (id >= 6000 && id < 6100) {
        const size_t i = static_cast<size_t>(id - 6000);
        if (i < recentProjects_.size()) return L"Recent project: " + recentProjects_[i];
    }
    if (id >= kOpenTabActionBase) {
        const size_t i =
            static_cast<size_t>(id - kOpenTabActionBase);
        if (i < tabs_.size()) {
            std::wstring label =
                L"Tab " + std::to_wstring(i + 1) + L": " +
                tabs_[i]->title();
            const size_t panes = tabs_[i]->leaves().size();
            if (panes > 1)
                label += L" (" + std::to_wstring(panes) + L" panes)";
            return label;
        }
    }
    const auto& repos = workspace_.repos();
    if (id >= 10000 && id < 10100) {
        const size_t i = static_cast<size_t>(id - 10000);
        if (i < repos.size())
            return (repos[i].isGit() ? L"Git repository: " : L"Folder: ") +
                   repos[i].name;
    }
    if (id >= 11000 && id < 21000) {
        const int packed = id - 11000;
        const size_t i = static_cast<size_t>(packed / 100);
        const size_t j = static_cast<size_t>(packed % 100);
        if (i < repos.size() && j < repos[i].worktrees.size())
            return L"Worktree: " + repos[i].name + L" / " + repos[i].worktrees[j].label;
    }
    return L"Action";
}

std::wstring Window::paletteActionCategory(int id) const {
    if (paletteQuery_.empty() &&
        std::find(paletteRecentActions_.begin(), paletteRecentActions_.end(), id) !=
            paletteRecentActions_.end())
        return L"Recent";
    for (const auto& action : kActions)
        if (action.id == id) return action.category;
    if (id >= 3000 && id < 3100) return L"Profile";
    if (id >= 4000 && id < 4100) return L"SSH";
    if (id >= 5000 && id < 5100) return L"Agent";
    if (id >= 6000 && id < 6100) return L"Recent project";
    if (id >= kOpenTabActionBase) return L"Tab";
    if (id >= 11000 && id < 21000) return L"Worktree";
    if (id >= 10000 && id < 10100) {
        const size_t i = static_cast<size_t>(id - 10000);
        if (i < workspace_.repos().size() && workspace_.repos()[i].isGit())
            return L"Git repository";
        return L"Folder";
    }
    return L"Action";
}

std::wstring Window::paletteActionShortcut(int id) const {
    for (const auto& action : kActions)
        if (action.id == id) return action.shortcut;
    return {};
}

std::wstring Window::paletteActionDisabledReason(int id) const {
    Tab* tab = activeTab();
    const bool split = tab && tab->isSplit();
    switch (id) {
    case 6:
    case 7:
    case 21:
    case 22:
    case 23:
    case 26:
        if (!split) return L"Needs split panes";
        break;
    case 18:
    case 19:
    case 20:
    case 24:
    case 25:
        if (!tab) return L"No active tab";
        break;
    case 27:
        if (tabs_.size() <= 1) return L"No other tabs";
        break;
    default:
        break;
    }
    return {};
}

void Window::rememberPaletteAction(int id) {
    // Dynamic collection IDs can be reassigned when tabs/worktrees change, so
    // only stable built-in actions participate in the recent-command list.
    const bool stable = std::any_of(
        std::begin(kActions), std::end(kActions),
        [=](const PaletteAction& action) { return action.id == id; });
    if (!stable) return;
    paletteRecentActions_.erase(
        std::remove(paletteRecentActions_.begin(), paletteRecentActions_.end(), id),
        paletteRecentActions_.end());
    paletteRecentActions_.insert(paletteRecentActions_.begin(), id);
    if (paletteRecentActions_.size() > 5) paletteRecentActions_.resize(5);
}

void Window::onPaletteChar(wchar_t ch) {
    if (!paletteActive_) return;
    if (ch >= 0x20 && ch != 0x7f && paletteQuery_.size() < 128) {
        paletteQuery_.push_back(ch);
        paletteSelected_ = 0;
    }
}

void Window::executePaletteAction(int id) {
    if (!paletteActionDisabledReason(id).empty()) {
        MessageBeep(MB_ICONINFORMATION);
        return;
    }
    rememberPaletteAction(id);
    closeCommandPalette();
    if (id >= 3000 && id < 3100) {
        const size_t i = static_cast<size_t>(id - 3000);
        if (i < shellProfiles_.size())
            newTabShell(shellProfiles_[i].command,
                        activeSession() ? activeSession()->cwd() : homeDir());
        return;
    }
    if (id >= 4000 && id < 4100) {
        const size_t i = static_cast<size_t>(id - 4000);
        if (i < sshHosts_.size()) {
            if (auto* session = newTabShell(buildSshCommand(sshHosts_[i]), L"")) {
                SessionContext context;
                context.role = SessionRole::Ssh;
                context.taskName = sshHosts_[i].name;
                session->setContext(std::move(context));
            }
        }
        return;
    }
    if (id >= 5000 && id < 5100) {
        const size_t i = static_cast<size_t>(id - 5000);
        if (i < agents_.size()) {
            const std::wstring cwd = agents_[i].cwd.empty()
                ? (activeSession() ? activeSession()->cwd() : homeDir()) : agents_[i].cwd;
            if (auto* session = newTabShell(agents_[i].command, cwd)) {
                SessionContext context;
                context.role = SessionRole::Agent;
                context.agentName = agents_[i].name;
                context.testCommand = agents_[i].testCommand;
                session->setContext(std::move(context));
            }
        }
        return;
    }
    if (id >= 6000 && id < 6100) {
        const size_t i = static_cast<size_t>(id - 6000);
        if (i < recentProjects_.size()) {
            const std::wstring path = recentProjects_[i];
            rememberRecentProject(path);
            newTab(path);
            if (TerminalSession* session = activeSession()) {
                bool matched = false;
                for (const Repo& project : workspace_.repos()) {
                    if (workspacePathsEqual(project.path, path)) {
                        SessionContext context;
                        context.projectPath = project.path;
                        if (project.isGit()) context.worktreePath = path;
                        session->setContext(std::move(context));
                        matched = true;
                        break;
                    }
                    for (const Worktree& worktree : project.worktrees) {
                        if (!workspacePathsEqual(worktree.path, path)) continue;
                        SessionContext context;
                        context.projectPath = project.path;
                        context.worktreePath = worktree.path;
                        session->setContext(std::move(context));
                        matched = true;
                        break;
                    }
                    if (matched) break;
                }
            }
        }
        return;
    }
    if (id >= kOpenTabActionBase) {
        const size_t i =
            static_cast<size_t>(id - kOpenTabActionBase);
        if (i < tabs_.size()) {
            clearSelection();
            activeTab_ = i;
            hoverTab_ = -1;
            updateTitle();
            markRenderDirty();
        }
        return;
    }
    const auto& repos = workspace_.repos();
    if (id >= 10000 && id < 10100) {
        const size_t i = static_cast<size_t>(id - 10000);
        if (i < repos.size()) {
            rememberRecentProject(repos[i].path);
            newTab(repos[i].path);
            if (TerminalSession* session = activeSession()) {
                SessionContext context;
                context.projectPath = repos[i].path;
                if (repos[i].isGit())
                    context.worktreePath = repos[i].path;
                session->setContext(std::move(context));
            }
        }
        return;
    }
    if (id >= 11000 && id < 21000) {
        const int packed = id - 11000;
        const size_t i = static_cast<size_t>(packed / 100);
        const size_t j = static_cast<size_t>(packed % 100);
        if (i < repos.size() && j < repos[i].worktrees.size()) {
            rememberRecentProject(repos[i].worktrees[j].path);
            newTab(repos[i].worktrees[j].path);
            if (TerminalSession* session = activeSession()) {
                SessionContext context;
                context.projectPath = repos[i].path;
                context.worktreePath = repos[i].worktrees[j].path;
                session->setContext(std::move(context));
            }
        }
        return;
    }
    switch (id) {
    case 1: newTab(activeSession() ? activeSession()->cwd() : homeDir()); break;
    case 2: splitActive(SplitDir::Cols); break;
    case 3: splitActive(SplitDir::Rows); break;
    case 4: sidebarVisible_ = !sidebarVisible_; break;
    case 5: filesPanelVisible_ = !filesPanelVisible_; break;
    case 6: toggleZoom(); break;
    case 7: equalizePanes(); break;
    case 8: openFind(); break;
    case 9: openSettingsDialog(); break;
    case 10: openWorkspaceSnapshotMenu(); break;
    case 11: checkForUpdates(); break;
    case 12: toggleKeepAwake(); break;
    case 14: openNewWindow(false); break;
    case 15: openNewWindow(true); break;
    case 16: searchHistory(); break;
    case 17: exportDiagnostics(); break;
    case 18: {
        if (Tab* tab = activeTab()) {
            const std::wstring title = inputBox(hwnd_, L"Rename tab", L"Tab title:",
                                                tab->customTitle());
            tab->setCustomTitle(title);
            updateTitle();
        }
        break;
    }
    case 19:
        togglePinActiveTab();
        break;
    case 20:
        if (auto* session = activeSession())
            newTabShell(session->shellCommand(), session->cwd());
        break;
    case 21:
        if (Tab* tab = activeTab()) tab->swapActiveWithNext();
        break;
    case 22:
        if (Tab* tab = activeTab()) {
            if (auto session = tab->detachActive()) {
                tabs_.push_back(std::make_unique<Tab>(std::move(session)));
                activeTab_ = tabs_.size() - 1;
                updateTitle();
            }
        }
        break;
    case 23:
        if (Tab* tab = activeTab()) tab->moveActiveForward();
        break;
    case 24:
        closeActivePaneConfirming();
        break;
    case 25:
        if (!tabs_.empty()) closeTabConfirming(activeTab_);
        break;
    case 26:
        closeOtherPanes();
        break;
    case 27:
        if (!tabs_.empty()) {
            Tab* keep = tabs_[activeTab_].get();
            std::vector<size_t> victims;
            for (size_t i = 0; i < tabs_.size(); ++i)
                if (i != activeTab_) victims.push_back(i);
            closeTabSet(victims, keep);
        }
        break;
    default: break;
    }
}

bool Window::executeConfiguredBinding(int virtualKey, bool ctrl, bool shift,
                                      bool alt) {
    for (const KeyBinding& binding : keybindings_) {
        if (!binding.chord.matches(virtualKey, ctrl, shift, alt)) continue;
        const std::wstring& action = binding.action;
        int id = action == L"newTab" ? 1 :
                 action == L"splitRight" ? 2 :
                 action == L"splitDown" ? 3 :
                 action == L"toggleSidebar" ? 4 :
                 action == L"toggleFiles" ? 5 :
                 action == L"zoomPane" ? 6 :
                 action == L"equalize" ? 7 :
                 action == L"find" ? 8 :
                 action == L"settings" ? 9 :
                 action == L"workspaceSnapshots" ? 10 :
                 action == L"checkUpdates" ? 11 :
                 action == L"keepAwake" ? 12 :
                 action == L"commandPalette" ? 13 :
                 action == L"newWindow" ? 14 :
                 action == L"newAdminWindow" ? 15 :
                 action == L"searchHistory" ? 16 :
                 action == L"exportDiagnostics" ? 17 :
                 action == L"renameTab" ? 18 :
                 action == L"pinTab" ? 19 :
                 action == L"duplicateTab" ? 20 :
                 action == L"swapPane" ? 21 :
                 action == L"detachPane" ? 22 :
                 action == L"closePane" ? 24 :
                 action == L"closeTab" ? 25 :
                 action == L"closeOtherPanes" ? 26 :
                 action == L"closeOtherTabs" ? 27 : 0;
        if (action == L"movePane") id = 23;
        if (id == 13) openCommandPalette();
        else if (id != 0) executePaletteAction(id);
        return id != 0;
    }
    return false;
}

bool Window::onPaletteKey(WPARAM key) {
    if (!paletteActive_) return false;
    const std::vector<int> actions = filteredPaletteActions();
    switch (key) {
    case VK_ESCAPE: closeCommandPalette(); return true;
    case VK_BACK:
        if (!paletteQuery_.empty()) paletteQuery_.pop_back();
        paletteSelected_ = 0;
        return true;
    case VK_UP:
        if (!actions.empty())
            paletteSelected_ = (paletteSelected_ + actions.size() - 1) % actions.size();
        return true;
    case VK_DOWN:
        if (!actions.empty()) paletteSelected_ = (paletteSelected_ + 1) % actions.size();
        return true;
    case VK_PRIOR:
        if (!actions.empty())
            paletteSelected_ = paletteSelected_ > 8 ? paletteSelected_ - 8 : 0;
        return true;
    case VK_NEXT:
        if (!actions.empty())
            paletteSelected_ =
                std::min(paletteSelected_ + 8, actions.size() - 1);
        return true;
    case VK_RETURN:
        if (!actions.empty()) {
            if (paletteSelected_ >= actions.size()) paletteSelected_ = 0;
            executePaletteAction(actions[paletteSelected_]);
        }
        return true;
    default: return false;
    }
}

void Window::drawCommandPalette() {
    if (!paletteActive_) return;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float width = std::min(620.0f * dpiScale_,
                                 static_cast<float>(client.right) - 40.0f);
    const float row = metrics_.rowH();
    const float x = (static_cast<float>(client.right) - width) * 0.5f;
    const float y = metrics_.tabBarH() + 24.0f;
    const std::vector<int> visible = filteredPaletteActions();
    const size_t count = std::min<size_t>(visible.size(), 8);
    const size_t displayRows = std::max<size_t>(count, 1);
    const size_t start = paletteSelected_ >= count && count > 0
        ? paletteSelected_ - count + 1 : 0;
    const float height = row * static_cast<float>(displayRows + 2) + 12.0f;
    const float radius = 9.0f * dpiScale_;
    renderer_->fillRoundedRect(x + 5.0f, y + 7.0f, width, height, radius,
                               uiTheme_.workspaceBg);
    renderer_->fillRoundedRect(x, y, width, height, radius,
                               uiTheme_.sidebarBg);
    renderer_->strokeRoundedRect(x, y, width, height, radius,
                                 uiTheme_.border, 1.0f);
    const std::wstring prompt = paletteQuery_.empty()
        ? L"Search everything  ·  Try tabs:, pane:, git:, ssh: or agent:"
        : L"> " + paletteQuery_;
    renderer_->drawText(prompt, x + 12.0f, y + 6.0f, width - 24.0f,
                        metrics_.cellH,
                        paletteQuery_.empty() ? uiTheme_.dim : uiTheme_.text,
                        !paletteQuery_.empty());
    for (size_t i = 0; i < count; ++i) {
        const size_t visibleIndex = start + i;
        const int id = visible[visibleIndex];
        const std::wstring label = paletteActionLabel(id);
        const std::wstring category = paletteActionCategory(id);
        const std::wstring disabled = paletteActionDisabledReason(id);
        const bool enabled = disabled.empty();
        const float ry = y + row * static_cast<float>(i + 1) + 6.0f;
        if (visibleIndex == paletteSelected_)
            renderer_->fillRoundedRect(x + 6.0f, ry + 1.0f, width - 12.0f,
                                       row - 2.0f, 6.0f * dpiScale_,
                                       uiTheme_.tabActiveBg);
        renderer_->drawText(category, x + 14.0f, ry + 4.0f,
                            width * 0.18f, metrics_.cellH,
                            visibleIndex == paletteSelected_ && enabled
                                ? uiTheme_.accent
                                : uiTheme_.sidebarHdr,
                            false);
        renderer_->drawText(label, x + width * 0.20f, ry + 4.0f,
                            width * 0.50f, metrics_.cellH,
                            !enabled ? uiTheme_.dim
                                     : visibleIndex == paletteSelected_
                                         ? uiTheme_.accent
                                         : uiTheme_.text,
                            visibleIndex == paletteSelected_ && enabled);
        const std::wstring detail =
            enabled ? paletteActionShortcut(id) : disabled;
        renderer_->drawText(detail, x + width * 0.71f, ry + 4.0f,
                            width * 0.26f, metrics_.cellH, uiTheme_.dim, false);
    }
    if (visible.empty()) {
        renderer_->drawText(L"No matching commands", x + 14.0f, y + row + 10.0f,
                            width - 28.0f, metrics_.cellH, uiTheme_.dim, false);
    }
    const float footerY = y + height - row;
    renderer_->fillRect(x + 8.0f, footerY - 1.0f, width - 16.0f, 1.0f,
                        uiTheme_.border);
    const std::wstring footer = std::to_wstring(visible.size()) +
        (visible.size() == 1 ? L" result" : L" results") +
        L"    ↑↓ navigate   PgUp/PgDn jump   Enter open   Esc close";
    renderer_->drawText(footer, x + 14.0f, footerY + 4.0f, width - 28.0f,
                        metrics_.cellH, uiTheme_.sidebarHdr, false);
}

} // namespace liney
