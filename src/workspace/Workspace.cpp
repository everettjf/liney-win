#include "workspace/Workspace.h"

#include <windows.h>

#include <algorithm>

#include "util/Process.h"

namespace liney {

namespace {

std::wstring basename(const std::wstring& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    size_t start = path.find_last_of(L"\\/", end ? end - 1 : 0);
    start = (start == std::wstring::npos) ? 0 : start + 1;
    return path.substr(start, end - start);
}

bool isGitRepo(const std::wstring& dir) {
    DWORD attr = GetFileAttributesW((dir + L"\\.git").c_str());
    return attr != INVALID_FILE_ATTRIBUTES;  // .git may be a dir or a file
}

} // namespace

void Workspace::scan(const std::wstring& root) {
    root_ = root;
    repos_.clear();

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = root + L"\\" + name;
        if (isGitRepo(path)) repos_.push_back(Repo{ name, path, {}, false, false });
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(repos_.begin(), repos_.end(),
              [](const Repo& a, const Repo& b) { return a.name < b.name; });
}

void Workspace::loadWorktrees(Repo& repo) {
    if (repo.loaded) return;
    repo.loaded = true;
    repo.worktrees.clear();

    bool ok = false;
    std::wstring out =
        runCapture(L"git worktree list --porcelain", repo.path, &ok);

    // Parse porcelain: blocks separated by blank lines, "worktree <path>" and
    // optional "branch refs/heads/<name>".
    std::wstring curPath, curBranch;
    auto flush = [&]() {
        if (curPath.empty()) return;
        std::wstring label =
            !curBranch.empty() ? curBranch : basename(curPath);
        repo.worktrees.push_back(Worktree{ curPath, label });
        curPath.clear();
        curBranch.clear();
    };
    size_t i = 0;
    while (i <= out.size()) {
        size_t nl = out.find(L'\n', i);
        if (nl == std::wstring::npos) nl = out.size();
        std::wstring line = out.substr(i, nl - i);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        i = nl + 1;

        if (line.empty()) {
            flush();
        } else if (line.rfind(L"worktree ", 0) == 0) {
            flush();
            curPath = line.substr(9);
        } else if (line.rfind(L"branch ", 0) == 0) {
            std::wstring ref = line.substr(7);
            const std::wstring prefix = L"refs/heads/";
            curBranch = ref.rfind(prefix, 0) == 0 ? ref.substr(prefix.size())
                                                  : ref;
        }
        if (nl == out.size()) break;
    }
    flush();

    // Fallback: if git wasn't available, at least show the repo root itself.
    if (repo.worktrees.empty())
        repo.worktrees.push_back(Worktree{ repo.path, basename(repo.path) });
}

} // namespace liney
