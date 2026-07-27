#include "core/CommandPalette.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <utility>

namespace liney {
namespace {

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}

std::wstring trim(std::wstring value) {
    const auto isSpace = [](wchar_t ch) { return iswspace(ch) != 0; };
    while (!value.empty() && isSpace(value.front())) value.erase(value.begin());
    while (!value.empty() && isSpace(value.back())) value.pop_back();
    return value;
}

bool containsWord(const std::wstring& words, const std::wstring& wanted) {
    size_t start = 0;
    while (start < words.size()) {
        while (start < words.size() && iswspace(words[start])) ++start;
        size_t end = start;
        while (end < words.size() && !iswspace(words[end])) ++end;
        if (words.substr(start, end - start) == wanted) return true;
        start = end;
    }
    return false;
}

std::wstring canonicalFilter(std::wstring filter) {
    filter = lower(trim(std::move(filter)));
    if (filter == L"action" || filter == L"actions" || filter == L"command" ||
        filter == L"commands")
        return L"actions";
    if (filter == L"tab" || filter == L"tabs") return L"tabs";
    if (filter == L"pane" || filter == L"panes" || filter == L"layout")
        return L"pane";
    if (filter == L"workspace" || filter == L"workspaces" ||
        filter == L"project" || filter == L"projects" || filter == L"folder" ||
        filter == L"folders")
        return L"workspace";
    if (filter == L"repo" || filter == L"repos" || filter == L"git" ||
        filter == L"worktree" || filter == L"worktrees")
        return L"git";
    if (filter == L"profile" || filter == L"profiles" || filter == L"shell" ||
        filter == L"shells")
        return L"profile";
    if (filter == L"ssh" || filter == L"host" || filter == L"hosts")
        return L"ssh";
    if (filter == L"agent" || filter == L"agents") return L"agent";
    if (filter == L"tool" || filter == L"tools" || filter == L"system")
        return L"tools";
    return {};
}

} // namespace

PaletteSearchQuery parsePaletteSearchQuery(const std::wstring& query) {
    PaletteSearchQuery parsed;
    const std::wstring cleaned = trim(query);
    const size_t colon = cleaned.find(L':');
    if (colon != std::wstring::npos) {
        const std::wstring candidate = canonicalFilter(cleaned.substr(0, colon));
        if (!candidate.empty()) {
            parsed.filter = candidate;
            parsed.text = trim(cleaned.substr(colon + 1));
            return parsed;
        }
    }
    parsed.text = cleaned;
    return parsed;
}

int paletteFuzzyScore(const std::wstring& text, const std::wstring& query) {
    if (query.empty()) return 0;
    const std::wstring haystack = lower(text);
    const std::wstring needle = lower(query);
    size_t at = 0;
    int score = 0;
    int previous = -2;
    for (wchar_t wanted : needle) {
        const size_t found = haystack.find(wanted, at);
        if (found == std::wstring::npos) return std::numeric_limits<int>::max();
        score += static_cast<int>(found - at);
        if (static_cast<int>(found) == previous + 1) score -= 2;
        if (found == 0 || haystack[found - 1] == L' ' ||
            haystack[found - 1] == L'/' || haystack[found - 1] == L'\\' ||
            haystack[found - 1] == L':' || haystack[found - 1] == L'-')
            score -= 3;
        previous = static_cast<int>(found);
        at = found + 1;
    }
    return score + static_cast<int>(haystack.size() - needle.size()) / 4;
}

std::vector<int> rankPaletteItems(const std::vector<PaletteSearchItem>& items,
                                  const std::wstring& query) {
    const PaletteSearchQuery parsed = parsePaletteSearchQuery(query);
    struct Ranked {
        int score = 0;
        int recent = -1;
        int order = 0;
        size_t source = 0;
        int id = 0;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        const PaletteSearchItem& item = items[i];
        if (!parsed.filter.empty() &&
            !containsWord(lower(item.filters), parsed.filter))
            continue;
        const std::wstring haystack =
            item.label + L" " + item.category + L" " + item.keywords;
        const int score = paletteFuzzyScore(haystack, parsed.text);
        if (score == std::numeric_limits<int>::max()) continue;
        ranked.push_back({score, item.recentRank, item.defaultOrder, i, item.id});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [&](const Ranked& a, const Ranked& b) {
        if (parsed.text.empty()) {
            const bool ar = a.recent >= 0;
            const bool br = b.recent >= 0;
            if (ar != br) return ar;
            if (ar && a.recent != b.recent) return a.recent < b.recent;
        }
        if (a.score != b.score) return a.score < b.score;
        if (a.order != b.order) return a.order < b.order;
        return a.source < b.source;
    });
    std::vector<int> result;
    result.reserve(ranked.size());
    for (const Ranked& item : ranked) result.push_back(item.id);
    return result;
}

} // namespace liney
