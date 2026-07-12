#pragma once

#include <windows.h>

#include <string>

#include "render/Cell.h"

namespace liney {

// The settings the dialog edits. In/out: pass current values, read them back
// when showSettingsDialog returns true (OK).
struct SettingsValues {
    std::wstring shell;              // default shell command for new tabs
    std::wstring fontFamily;         // monospace font family
    float fontSize = 16.0f;          // logical point size
    std::wstring themeName;          // active theme preset (see Themes.h)
    Color accent{ 120, 200, 160 };   // chrome accent (divider / active tab)
    bool accentExplicit = false;     // OUT: user picked a custom accent color
    bool themePicked = false;        // OUT: user actively chose a preset
    int scrollback = 10000;          // history lines per session
    bool copyOnSelect = false;       // copy as soon as a selection ends
    bool multiLinePasteWarning = true;  // confirm multi-line pastes
    bool unixTools = true;           // add Git's usr/bin to PATH for shells
    bool rememberLayout = false;     // restore tabs/panes on launch
    std::wstring workspaceRoot;      // sidebar scan root (empty = launch parent)
};

// Modal, click-to-configure settings window (the GUI counterpart of
// config.json). Returns true when the user pressed OK; `v` then holds the
// edited values. The caller applies + persists them.
bool showSettingsDialog(HWND owner, SettingsValues& v);

} // namespace liney
