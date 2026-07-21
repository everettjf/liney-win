#pragma once

#include <string>
#include <vector>

namespace liney {

struct ShellProfile {
    std::wstring id;
    std::wstring name;
    std::wstring command;
};

// Discover supported local shells without spawning them. Results are stable,
// deduplicated, and always include cmd.exe as the final fallback.
std::vector<ShellProfile> discoverShellProfiles();

// Add Liney's OSC 7/133 bootstrap to PowerShell commands. Other shells are
// returned unchanged. The integration script is installed atomically in the
// user config directory.
std::wstring prepareShellCommand(const std::wstring& command);

} // namespace liney
