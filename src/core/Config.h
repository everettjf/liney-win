#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/Themes.h"
#include "render/Cell.h"
#include "util/Json.h"

namespace liney {

// An agent-backed session: a named entry that opens a tab running `command`
// (e.g. an AI agent CLI) in `cwd`. Mirrors macOS liney's agent sessions.
struct AgentDef {
    std::wstring name;
    std::wstring command;
    std::wstring cwd;  // empty => inherit
};

// User settings, loaded from %USERPROFILE%\.liney\config.json. A default file is
// written on first run. Mirrors macOS liney's ~/.liney/ persistence directory.
struct Config {
    std::wstring shell = L"cmd.exe";        // default shell for new tabs
    std::wstring fontFamily = L"Cascadia Mono";
    float fontSize = 16.0f;
    int scrollback = 10000;                 // history lines retained per session
    std::wstring workspaceRoot;             // empty => parent of launch directory
    std::wstring sessionStartHook;          // command run in each new shell
    std::wstring sessionExitHook;           // command run when a pane closes
    std::wstring appExitHook;               // command run on app quit
    std::vector<std::wstring> sshHosts;     // e.g. "user@host"; sidebar SSH list
    std::vector<AgentDef> agents;           // sidebar AGENTS list
    std::wstring themeName;                  // active preset name (see Themes.h)
    Theme theme;                            // terminal palette (preset + overrides)
    UiTheme uiTheme;                        // chrome palette (preset + accent override)
    bool unixTools = true;                  // append Git's usr/bin to PATH (ls/cat/…)
    bool copyOnSelect = false;              // copy to clipboard as soon as a drag ends
    bool multiLinePasteWarning = true;      // confirm before pasting multiple lines
    // Per-project sidebar icons: repo name -> icon file path (png/ico).
    std::vector<std::pair<std::wstring, std::wstring>> projectIcons;
    // Explicit project folders added to the sidebar (besides scanned ones).
    std::vector<std::wstring> projects;
};

// %USERPROFILE%\.liney (created if missing). Empty if USERPROFILE is unset.
std::wstring configDir();

// Load config (creating the directory + a default config.json if absent).
Config loadConfig();

// Persist just the fontSize back to config.json, preserving every other key
// (parse → set → dump). Best-effort: silently no-ops if the file can't be
// read/written. Used to remember the zoom level across launches.
void saveFontSize(float size);

// Persist just the fontFamily, same parse → set → dump approach as
// saveFontSize. Used by the in-app font picker.
void saveFontFamily(const std::wstring& family);

// Write `content` via temp file + atomic rename, so a crash mid-write can't
// leave a truncated file. Returns false on failure (target left untouched).
bool writeFileAtomic(const std::wstring& path, const std::string& content);

// Re-parse config.json, apply `mutate`, write it back atomically. Preserves
// every other key; refuses to overwrite a config.json that no longer parses
// (so a hand-edit typo can't cost the user their whole file).
void updateConfigJson(const std::function<void(Json&)>& mutate);

} // namespace liney
