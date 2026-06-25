#pragma once

#include <string>

namespace liney {

// User settings, loaded from %USERPROFILE%\.liney\config.json. A default file is
// written on first run. Mirrors macOS liney's ~/.liney/ persistence directory.
struct Config {
    std::wstring shell = L"cmd.exe";        // default shell for new tabs
    std::wstring fontFamily = L"Cascadia Mono";
    float fontSize = 16.0f;
    std::wstring workspaceRoot;             // empty => parent of launch directory
};

// %USERPROFILE%\.liney (created if missing). Empty if USERPROFILE is unset.
std::wstring configDir();

// Load config (creating the directory + a default config.json if absent).
Config loadConfig();

} // namespace liney
