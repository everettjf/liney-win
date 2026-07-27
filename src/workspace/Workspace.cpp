#include "workspace/Workspace.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>

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

bool projectLess(const Repo& a, const Repo& b) {
    const int byName = _wcsicmp(a.name.c_str(), b.name.c_str());
    if (byName != 0) return byName < 0;
    return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
}

} // namespace

std::wstring normalizeWorkspacePath(const std::wstring& input) {
    if (input.empty()) return L"";
    std::wstring path = input;
    std::replace(path.begin(), path.end(), L'/', L'\\');

    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed > 0 && needed < 32768) {
        std::wstring full(static_cast<size_t>(needed), L'\0');
        const DWORD written = GetFullPathNameW(
            path.c_str(), needed, full.data(), nullptr);
        if (written > 0 && written < needed) {
            full.resize(written);
            path = std::move(full);
        }
    }

    while (path.size() > 1 &&
           (path.back() == L'\\' || path.back() == L'/')) {
        if (path.size() == 3 && path[1] == L':') break;
        path.pop_back();
    }
    return path;
}

bool workspacePathsEqual(const std::wstring& a, const std::wstring& b) {
    const std::wstring left = normalizeWorkspacePath(a);
    const std::wstring right = normalizeWorkspacePath(b);
    return !left.empty() && !right.empty() &&
           _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool isGitRepositoryPath(const std::wstring& input) {
    const std::wstring dir = normalizeWorkspacePath(input);
    if (dir.empty()) return false;
    const DWORD attr = GetFileAttributesW((dir + L"\\.git").c_str());
    return attr != INVALID_FILE_ATTRIBUTES;  // .git may be a dir or a file
}

void Workspace::scan(const std::wstring& root) {
    root_ = normalizeWorkspacePath(root);
    repos_.clear();
    if (root_.empty()) return;  // empty means explicit projects only

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((root_ + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = root_ + L"\\" + name;
        if (isGitRepositoryPath(path))
            repos_.push_back(
                Repo{name, path, ProjectKind::GitRepository, {}, false, false});
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(repos_.begin(), repos_.end(), projectLess);
}

void Workspace::addProject(const std::wstring& input) {
    const std::wstring path = normalizeWorkspacePath(input);
    if (path.empty()) return;
    for (const Repo& r : repos_)
        if (workspacePathsEqual(r.path, path)) return;  // already present
    const ProjectKind kind = isGitRepositoryPath(path)
                                 ? ProjectKind::GitRepository
                                 : ProjectKind::Folder;
    repos_.push_back(Repo{basename(path), path, kind, {}, false, false});
    std::sort(repos_.begin(), repos_.end(), projectLess);
}

bool Workspace::removeRepoByPath(const std::wstring& path) {
    for (auto it = repos_.begin(); it != repos_.end(); ++it) {
        if (workspacePathsEqual(it->path, path)) {
            repos_.erase(it);
            return true;
        }
    }
    return false;
}

void Workspace::loadWorktrees(Repo& repo) {
    if (repo.loaded) return;
    repo.loaded = true;
    repo.worktrees.clear();
    if (!repo.isGit()) return;

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
        Worktree worktree{curPath, label, {}, true};
        refreshStatus(worktree);
        repo.worktrees.push_back(std::move(worktree));
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
        repo.worktrees.push_back(
            Worktree{repo.path, basename(repo.path), {}, true});
}

void Workspace::refreshStatus(Worktree& worktree) {
    if (!worktree.git) return;
    bool ok = false;
    const std::wstring output = runCapture(
        L"git status --porcelain=v2 --branch --untracked-files=normal",
        worktree.path, &ok);
    if (ok) {
        worktree.status = parseGitStatusPorcelainV2(output);
        if (!worktree.status.branch.empty() && !worktree.status.detached)
            worktree.label = worktree.status.branch;
    }
}

std::wstring Workspace::addWorktree(Repo& repo, const std::wstring& name,
                                    std::wstring* err) {
    if (!repo.isGit()) {
        if (err) *err = L"Git worktrees require a Git repository.";
        return L"";
    }
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
    bool validBranch = false;
    const std::wstring validation =
        runCapture(L"git check-ref-format --branch \"" + name + L"\"",
                   repo.path, &validBranch);
    if (!validBranch) {
        if (err) *err = validation.empty() ? L"Git rejected the branch name."
                                           : validation;
        return L"";
    }
    std::wstring pathName = name;
    std::replace(pathName.begin(), pathName.end(), L'/', L'-');
    const std::wstring path = parentDir(repo.path) + L"\\" +
                              basename(repo.path) + L"-" + pathName;
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (err) *err = L"The worktree directory already exists: " + path;
        return L"";
    }

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
    if (!repo.isGit()) {
        if (err) *err = L"Git worktrees require a Git repository.";
        return false;
    }
    bool ok = false;
    const std::wstring out =
        runCapture(L"git worktree remove \"" + path + L"\"", repo.path, &ok);
    if (!ok && err) *err = out;  // e.g. "contains modified files, use --force"
    repo.loaded = false;
    loadWorktrees(repo);
    return ok;
}

} // namespace liney
