#include "workspace/GitStatusParser.h"

#include <cwchar>

namespace liney {

GitWorktreeStatus parseGitStatusPorcelainV2(const std::wstring& text) {
    GitWorktreeStatus result;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.rfind(L"# branch.head ", 0) == 0) {
            result.branch = line.substr(14);
            result.detached = result.branch == L"(detached)";
        } else if (line.rfind(L"# branch.ab ", 0) == 0) {
            int ahead = 0, behind = 0;
            if (swscanf(line.c_str() + 12, L"+%d -%d", &ahead, &behind) == 2) {
                result.ahead = ahead;
                result.behind = behind;
            }
        } else if (!line.empty() &&
                   (line[0] == L'1' || line[0] == L'2' || line[0] == L'u' ||
                    line[0] == L'?' || line[0] == L'!')) {
            // Ignored files are deliberately not dirty state.
            if (line[0] != L'!') ++result.changed;
        }
        if (end == text.size()) break;
        pos = end + 1;
    }
    return result;
}

} // namespace liney
