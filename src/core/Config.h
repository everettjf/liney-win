#pragma once

#include <string>
#include <vector>

#include "render/Cell.h"

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
    std::wstring workspaceRoot;             // empty => parent of launch directory
    std::wstring sessionStartHook;          // command run in each new shell
    std::vector<std::wstring> sshHosts;     // e.g. "user@host"; sidebar SSH list
    std::vector<AgentDef> agents;           // sidebar AGENTS list
    Theme theme;                            // colors (defaults = built-in palette)
};

// %USERPROFILE%\.liney (created if missing). Empty if USERPROFILE is unset.
std::wstring configDir();

// Load config (creating the directory + a default config.json if absent).
Config loadConfig();

} // namespace liney
