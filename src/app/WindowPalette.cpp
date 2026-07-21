#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <cwctype>

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
};

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
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
    std::vector<int> result;
    const std::wstring query = lower(paletteQuery_);
    for (const PaletteAction& action : kActions) {
        if (query.empty() || lower(action.name).find(query) != std::wstring::npos)
            result.push_back(action.id);
    }
    return result;
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
                 action == L"newAdminWindow" ? 15 : 0;
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
    const float height = row * static_cast<float>(count + 1) + 12.0f;
    renderer_->fillRect(x, y, width, height, uiTheme_.sidebarBg);
    renderer_->strokeRect(x, y, width, height, uiTheme_.accent, 1.5f);
    const std::wstring prompt = L"> " + paletteQuery_;
    renderer_->drawText(prompt, x + 12.0f, y + 6.0f, width - 24.0f,
                        metrics_.cellH, uiTheme_.text, true);
    for (size_t i = 0; i < count; ++i) {
        const PaletteAction* action = nullptr;
        for (const auto& candidate : kActions)
            if (candidate.id == visible[i]) { action = &candidate; break; }
        if (!action) continue;
        const float ry = y + row * static_cast<float>(i + 1) + 6.0f;
        if (i == paletteSelected_)
            renderer_->fillRect(x + 4.0f, ry, width - 8.0f, row,
                                uiTheme_.tabActiveBg);
        renderer_->drawText(action->name, x + 14.0f, ry + 4.0f,
                            width * 0.68f, metrics_.cellH,
                            i == paletteSelected_ ? uiTheme_.accent : uiTheme_.text,
                            i == paletteSelected_);
        renderer_->drawText(action->shortcut, x + width * 0.70f, ry + 4.0f,
                            width * 0.27f, metrics_.cellH, uiTheme_.dim, false);
    }
}

} // namespace liney
