#pragma once

#include <string>
#include <vector>

namespace liney {

// Platform-independent search metadata used by the command palette. Keeping the
// parser/ranker outside Window makes filter aliases and recent-item ordering
// deterministic and unit-testable without Win32.
struct PaletteSearchItem {
    int id = 0;
    std::wstring label;
    std::wstring category;
    std::wstring filters;
    std::wstring keywords;
    int defaultOrder = 0;
    int recentRank = -1;  // 0 is the most recently used; -1 means not recent.
};

struct PaletteSearchQuery {
    std::wstring filter;
    std::wstring text;
};

PaletteSearchQuery parsePaletteSearchQuery(const std::wstring& query);
int paletteFuzzyScore(const std::wstring& text, const std::wstring& query);
std::vector<int> rankPaletteItems(const std::vector<PaletteSearchItem>& items,
                                  const std::wstring& query);

} // namespace liney
