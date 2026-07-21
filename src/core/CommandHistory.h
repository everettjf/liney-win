#pragma once

#include <string>
#include <vector>

namespace liney {

struct HistoryEntry {
    std::wstring command;
    std::wstring cwd;
    int exitCode = 0;
    unsigned long long timestamp = 0;
};

// Local-only history used by Liney's command search. The file is bounded and
// never included in diagnostics or AI context unless the user selects a single
// command through the existing explicit AI action.
void appendCommandHistory(const HistoryEntry& entry);
std::vector<HistoryEntry> searchCommandHistory(const std::wstring& query,
                                                size_t limit = 30);

} // namespace liney
