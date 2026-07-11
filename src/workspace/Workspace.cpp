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

std::wstring parentDir(const std::wstring& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    size_t slash = path.find_last_of(L"\\/", end ? end - 1 : 0);
    return (slash == std::wstring::npos) ? path : path.substr(0, slash);
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

void Workspace::addProject(const std::wstring& path) {
    if (path.empty()) return;
    for (const Repo& r : repos_)
        if (r.path == path) return;  // already present
    repos_.push_back(Repo{ basename(path), path, {}, false, false });
    std::sort(repos_.begin(), repos_.end(),
              [](const Repo& a, const Repo& b) { return a.name < b.name; });
}

bool Workspace::removeRepoByPath(const std::wstring& path) {
    for (auto it = repos_.begin(); it != repos_.end(); ++it) {
        if (it->path == path) { repos_.erase(it); return true; }
    }
    return false;
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

std::wstring Workspace::addWorktree(Repo& repo, const std::wstring& name,
                                    std::wstring* err) {
    if (name.empty()) return L"";
    // The name lands inside a quoted command line and becomes a branch name +
    // path component — restrict it to characters that are safe as both. This
    // blocks quote-escape injection (`"` or a trailing `\`) and the
    // characters git refuses in branch names anyway.
    for (wchar_t c : name) {
        const bool okChar = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_' ||
                            c == L'.' || c == L'/';
        if (!okChar) {
            if (err)
                *err = L"Invalid worktree name (use letters, digits, - _ . /): " +
                       name;
            return L"";
        }
    }
    const std::wstring path =
        parentDir(repo.path) + L"\\" + basename(repo.path) + L"-" + name;

    bool ok = false;
    std::wstring out =
        runCapture(L"git worktree add \"" + path + L"\" -b \"" + name + L"\"",
                   repo.path, &ok);
    if (!ok) {
        // Branch may already exist; check it out into the worktree instead.
        out = runCapture(L"git worktree add \"" + path + L"\" \"" + name + L"\"",
                         repo.path, &ok);
    }
    if (!ok && err) *err = out;
    repo.loaded = false;
    loadWorktrees(repo);
    repo.expanded = true;
    return ok ? path : L"";
}

bool Workspace::removeWorktree(Repo& repo, const std::wstring& path,
                               std::wstring* err) {
    bool ok = false;
    const std::wstring out =
        runCapture(L"git worktree remove \"" + path + L"\"", repo.path, &ok);
    if (!ok && err) *err = out;  // e.g. "contains modified files, use --force"
    repo.loaded = false;
    loadWorktrees(repo);
    return ok;
}

} // namespace liney
