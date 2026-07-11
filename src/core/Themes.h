#pragma once

#include <string>
#include <vector>

#include "render/Cell.h"

namespace liney {

// Chrome (application-UI) colors: the sidebar, tab strip, pane borders and the
// accent used for the active-pane divider / active tab / icons. Distinct from
// Theme, which colors the terminal grid itself. Defaults are the built-in
// "Emerald Night" look.
struct UiTheme {
    Color sidebarBg{ 24, 24, 28 };
    Color sidebarHdr{ 130, 130, 140 };
    Color text{ 205, 205, 210 };
    Color dim{ 140, 140, 150 };
    Color tabBg{ 18, 18, 22 };
    Color tabActiveBg{ 40, 42, 52 };
    Color accent{ 120, 200, 160 };   // active-pane border, active tab, "+"
    Color border{ 55, 55, 66 };      // inactive pane border
    Color workspaceBg{ 13, 13, 15 }; // gutters/margins behind the panes
};

// A named look pairing a terminal palette (Theme) with chrome colors (UiTheme).
// The Settings dialog offers these by name; config.json stores the name plus
// any explicit overrides (accentColor / theme object).
struct ThemePreset {
    std::wstring name;
    Theme terminal;
    UiTheme ui;
};

// The built-in presets, in menu order. The first entry is the default look.
inline std::vector<ThemePreset> builtinThemePresets() {
    std::vector<ThemePreset> v;

    auto ansiDark = [] {
        Theme t;  // Cell.h default xterm-ish palette on black
        return t;
    };

    // 1) Emerald Night — the original green-accented dark look.
    {
        ThemePreset p;
        p.name = L"Emerald Night";
        p.terminal = ansiDark();
        p.ui = UiTheme{};  // defaults
        v.push_back(std::move(p));
    }
    // 2) Azure Night — cool blue accent.
    {
        ThemePreset p;
        p.name = L"Azure Night";
        p.terminal = ansiDark();
        p.terminal.background = { 16, 18, 24 };
        p.ui.sidebarBg = { 20, 23, 30 };
        p.ui.tabBg = { 15, 17, 23 };
        p.ui.tabActiveBg = { 34, 40, 55 };
        p.ui.accent = { 90, 170, 245 };
        p.ui.border = { 48, 54, 70 };
        p.ui.workspaceBg = { 11, 13, 18 };
        v.push_back(std::move(p));
    }
    // 3) Violet Night — purple accent.
    {
        ThemePreset p;
        p.name = L"Violet Night";
        p.terminal = ansiDark();
        p.terminal.background = { 20, 17, 26 };
        p.ui.sidebarBg = { 26, 22, 32 };
        p.ui.tabBg = { 20, 17, 25 };
        p.ui.tabActiveBg = { 46, 40, 58 };
        p.ui.accent = { 185, 150, 240 };
        p.ui.border = { 60, 52, 72 };
        p.ui.workspaceBg = { 15, 12, 19 };
        v.push_back(std::move(p));
    }
    // 4) Amber Dark — warm amber accent.
    {
        ThemePreset p;
        p.name = L"Amber Dark";
        p.terminal = ansiDark();
        p.terminal.background = { 22, 19, 15 };
        p.ui.sidebarBg = { 28, 24, 19 };
        p.ui.tabBg = { 22, 19, 14 };
        p.ui.tabActiveBg = { 52, 44, 30 };
        p.ui.accent = { 240, 185, 90 };
        p.ui.border = { 68, 58, 40 };
        p.ui.workspaceBg = { 16, 14, 10 };
        v.push_back(std::move(p));
    }
    // 5) Rose Dark — pink/red accent.
    {
        ThemePreset p;
        p.name = L"Rose Dark";
        p.terminal = ansiDark();
        p.terminal.background = { 24, 17, 19 };
        p.ui.sidebarBg = { 30, 22, 24 };
        p.ui.tabBg = { 23, 16, 18 };
        p.ui.tabActiveBg = { 54, 38, 42 };
        p.ui.accent = { 240, 130, 150 };
        p.ui.border = { 70, 50, 54 };
        p.ui.workspaceBg = { 17, 12, 13 };
        v.push_back(std::move(p));
    }
    // 6) Nord-ish — muted slate with a frost accent.
    {
        ThemePreset p;
        p.name = L"Slate Frost";
        p.terminal = ansiDark();
        p.terminal.background = { 34, 40, 49 };      // #2E3440-ish
        p.terminal.foreground = { 216, 222, 233 };
        p.ui.sidebarBg = { 41, 48, 58 };
        p.ui.sidebarHdr = { 150, 160, 175 };
        p.ui.text = { 216, 222, 233 };
        p.ui.dim = { 140, 150, 165 };
        p.ui.tabBg = { 34, 40, 49 };
        p.ui.tabActiveBg = { 59, 66, 82 };
        p.ui.accent = { 136, 192, 208 };
        p.ui.border = { 67, 76, 94 };
        p.ui.workspaceBg = { 29, 34, 42 };
        v.push_back(std::move(p));
    }
    // 7) Paper Light — a light theme.
    {
        ThemePreset p;
        p.name = L"Paper Light";
        p.terminal.background = { 250, 250, 248 };
        p.terminal.foreground = { 40, 42, 46 };
        p.terminal.ansi[0] = { 60, 60, 60 };
        p.terminal.ansi[7] = { 90, 90, 90 };
        p.terminal.ansi[15] = { 20, 20, 20 };
        p.ui.sidebarBg = { 238, 238, 235 };
        p.ui.sidebarHdr = { 110, 110, 118 };
        p.ui.text = { 45, 47, 52 };
        p.ui.dim = { 120, 122, 128 };
        p.ui.tabBg = { 230, 230, 227 };
        p.ui.tabActiveBg = { 255, 255, 255 };
        p.ui.accent = { 30, 130, 90 };
        p.ui.border = { 200, 200, 196 };
        p.ui.workspaceBg = { 220, 220, 216 };
        v.push_back(std::move(p));
    }
    return v;
}

// Find a preset by name (case-sensitive). Returns nullptr when absent.
inline const ThemePreset* findThemePreset(
    const std::vector<ThemePreset>& presets, const std::wstring& name) {
    for (const ThemePreset& p : presets)
        if (p.name == name) return &p;
    return nullptr;
}

} // namespace liney
