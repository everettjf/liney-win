#pragma once

#include <windows.h>

#include <string>

namespace liney {

// The settings the dialog edits. In/out: pass current values, read them back
// when showSettingsDialog returns true (OK). Font/theme stay in their own
// pickers (Font… menu, config.json).
struct SettingsValues {
    std::wstring shell;              // default shell command for new tabs
    int scrollback = 10000;          // history lines per session
    bool copyOnSelect = false;       // copy as soon as a selection ends
    bool multiLinePasteWarning = true;  // confirm multi-line pastes
    bool unixTools = true;           // add Git's usr/bin to PATH for shells
    std::wstring workspaceRoot;      // sidebar scan root (empty = launch parent)
};

// Modal, click-to-configure settings window (the GUI counterpart of
// config.json). Returns true when the user pressed OK; `v` then holds the
// edited values. The caller applies + persists them.
bool showSettingsDialog(HWND owner, SettingsValues& v);

} // namespace liney
