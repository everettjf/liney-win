#pragma once

#include <string>

namespace liney {

struct GitWorktreeStatus {
    std::wstring branch;
    int ahead = 0;
    int behind = 0;
    int changed = 0;
    bool detached = false;
};

GitWorktreeStatus parseGitStatusPorcelainV2(const std::wstring& text);

} // namespace liney
