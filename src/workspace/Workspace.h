#pragma once

#include <string>
#include <vector>

namespace liney {

// One checkout of a repo (the main worktree plus any `git worktree add`ed ones).
struct Worktree {
    std::wstring path;
    std::wstring label;  // branch name or short path, for the sidebar
};

// A git repository discovered in the workspace root.
struct Repo {
    std::wstring name;
    std::wstring path;
    std::vector<Worktree> worktrees;
    bool expanded = false;  // sidebar disclosure state
    bool loaded = false;    // worktrees fetched yet?
};

// The sidebar's data model: git repositories found one level under a root
// directory, each expandable to its worktrees. Worktrees are loaded lazily
// (a `git worktree list` per repo) the first time a repo is expanded.
class Workspace {
public:
    // Discover repos directly under `root` (dirs containing a .git entry).
    void scan(const std::wstring& root);

    const std::wstring& root() const { return root_; }
    std::vector<Repo>& repos() { return repos_; }

    // Populate repo.worktrees via `git worktree list` (no-op if already loaded).
    void loadWorktrees(Repo& repo);

private:
    std::wstring root_;
    std::vector<Repo> repos_;
};

} // namespace liney
