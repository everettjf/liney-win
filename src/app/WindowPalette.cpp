#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <cwctype>
#include <limits>

#include "core/SshProfiles.h"
#include "util/InputBox.h"

namespace liney {
namespace {

struct PaletteAction {
    int id;
    const wchar_t* name;
    const wchar_t* shortcut;
};

constexpr PaletteAction kActions[] = {
    {1, L"New tab", L"Ctrl+Shift+T"},
    {2, L"Split side by side", L"Alt+D"},
    {3, L"Split stacked", L"Shift+Alt+D"},
    {4, L"Toggle sidebar", L"Ctrl+Shift+B"},
    {5, L"Toggle files panel", L"Ctrl+Shift+F"},
    {6, L"Zoom or restore pane", L"Ctrl+Shift+Z"},
    {7, L"Equalize panes", L"Ctrl+Shift+E"},
    {8, L"Find in terminal", L"Ctrl+F"},
    {9, L"Settings", L"Ctrl+,"},
    {10, L"Workspace snapshots", L""},
    {11, L"Check for updates", L"Ctrl+Shift+U"},
    {12, L"Toggle keep awake", L"Ctrl+Shift+K"},
    {14, L"New window", L""},
    {15, L"New administrator window", L""},
    {16, L"Search command history", L""},
    {17, L"Export diagnostic bundle", L""},
    {18, L"Rename active tab", L""},
    {19, L"Pin or unpin active tab", L""},
    {20, L"Duplicate active tab", L""},
    {21, L"Swap active pane with next pane", L""},
    {22, L"Move active pane to new tab", L""},
    {23, L"Move active pane forward", L""},
};

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}

int fuzzyScore(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) return 0;
    const std::wstring haystack = lower(text);
    const std::wstring needle = lower(query);
    size_t at = 0;
    int score = 0;
    int previous = -2;
    for (wchar_t wanted : needle) {
        const size_t found = haystack.find(wanted, at);
        if (found == std::wstring::npos) return std::numeric_limits<int>::max();
        score += static_cast<int>(found - at);
        if (static_cast<int>(found) == previous + 1) score -= 2;
        if (found == 0 || haystack[found - 1] == L' ' ||
            haystack[found - 1] == L'/' || haystack[found - 1] == L'\\') score -= 3;
        previous = static_cast<int>(found);
        at = found + 1;
    }
    return score + static_cast<int>(haystack.size() - needle.size()) / 4;
}

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
    std::vector<int> candidates;
    for (const PaletteAction& action : kActions) {
        candidates.push_back(action.id);
    }
    for (size_t i = 0; i < shellProfiles_.size() && i < 100; ++i)
        candidates.push_back(3000 + static_cast<int>(i));
    for (size_t i = 0; i < sshHosts_.size() && i < 100; ++i)
        candidates.push_back(4000 + static_cast<int>(i));
    for (size_t i = 0; i < agents_.size() && i < 100; ++i)
        candidates.push_back(5000 + static_cast<int>(i));
    for (size_t i = 0; i < recentProjects_.size() && i < 100; ++i)
        candidates.push_back(6000 + static_cast<int>(i));
    const auto& repos = workspace_.repos();
    for (size_t i = 0; i < repos.size() && i < 100; ++i) {
        candidates.push_back(10000 + static_cast<int>(i));
        for (size_t j = 0; j < repos[i].worktrees.size() && j < 100; ++j)
            candidates.push_back(11000 + static_cast<int>(i * 100 + j));
    }
    std::vector<std::pair<int, int>> ranked;
    for (int id : candidates) {
        const int score = fuzzyScore(paletteActionLabel(id), paletteQuery_);
        if (score != std::numeric_limits<int>::max()) ranked.push_back({score, id});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    std::vector<int> result;
    result.reserve(ranked.size());
    for (const auto& item : ranked) result.push_back(item.second);
    return result;
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
    const auto& repos = workspace_.repos();
    if (id >= 10000 && id < 10100) {
        const size_t i = static_cast<size_t>(id - 10000);
        if (i < repos.size()) return L"Workspace: " + repos[i].name;
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

void Window::onPaletteChar(wchar_t ch) {
    if (!paletteActive_) return;
    if (ch >= 0x20 && ch != 0x7f && paletteQuery_.size() < 128) {
        paletteQuery_.push_back(ch);
        paletteSelected_ = 0;
    }
}

void Window::executePaletteAction(int id) {
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
        }
        return;
    }
    const auto& repos = workspace_.repos();
    if (id >= 10000 && id < 10100) {
        const size_t i = static_cast<size_t>(id - 10000);
        if (i < repos.size()) {
            rememberRecentProject(repos[i].path);
            newTab(repos[i].path);
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
                 action == L"detachPane" ? 22 : 0;
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
    const size_t start = paletteSelected_ >= count && count > 0
        ? paletteSelected_ - count + 1 : 0;
    const float height = row * static_cast<float>(count + 1) + 12.0f;
    renderer_->fillRect(x, y, width, height, uiTheme_.sidebarBg);
    renderer_->strokeRect(x, y, width, height, uiTheme_.accent, 1.5f);
    const std::wstring prompt = L"> " + paletteQuery_;
    renderer_->drawText(prompt, x + 12.0f, y + 6.0f, width - 24.0f,
                        metrics_.cellH, uiTheme_.text, true);
    for (size_t i = 0; i < count; ++i) {
        const size_t visibleIndex = start + i;
        const PaletteAction* action = nullptr;
        for (const auto& candidate : kActions)
            if (candidate.id == visible[visibleIndex]) { action = &candidate; break; }
        const std::wstring dynamicLabel = paletteActionLabel(visible[visibleIndex]);
        const float ry = y + row * static_cast<float>(i + 1) + 6.0f;
        if (visibleIndex == paletteSelected_)
            renderer_->fillRect(x + 4.0f, ry, width - 8.0f, row,
                                uiTheme_.tabActiveBg);
        renderer_->drawText(action ? action->name : dynamicLabel, x + 14.0f, ry + 4.0f,
                            width * 0.68f, metrics_.cellH,
                            visibleIndex == paletteSelected_ ? uiTheme_.accent : uiTheme_.text,
                            visibleIndex == paletteSelected_);
        renderer_->drawText(action ? action->shortcut : L"", x + width * 0.70f, ry + 4.0f,
                            width * 0.27f, metrics_.cellH, uiTheme_.dim, false);
    }
}

} // namespace liney
