#pragma once

#include <string>
#include <vector>

#include "workspace/GitStatusParser.h"

namespace liney {

// One checkout of a repo (the main worktree plus any `git worktree add`ed ones).
struct Worktree {
    std::wstring path;
    std::wstring label;  // branch name or short path, for the sidebar
    GitWorktreeStatus status;
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

    // Add an explicit project folder (need not be under the root). Deduped by
    // path; keeps the list sorted by name.
    void addProject(const std::wstring& path);

    // Remove the repo with this path. Returns true if one was removed.
    bool removeRepoByPath(const std::wstring& path);

    const std::wstring& root() const { return root_; }
    std::vector<Repo>& repos() { return repos_; }

    // Populate repo.worktrees via `git worktree list` (no-op if already loaded).
    void loadWorktrees(Repo& repo);
    void refreshStatus(Worktree& worktree);

    // Create a worktree + branch `name` for `repo` as a sibling directory.
    // Returns the new path on success (empty on failure); refreshes the list.
    // On failure `err` (when non-null) receives git's message.
    std::wstring addWorktree(Repo& repo, const std::wstring& name,
                             std::wstring* err = nullptr);

    // Remove the worktree at `path` (git refuses the main one). Refreshes list.
    // On failure `err` (when non-null) receives git's message.
    bool removeWorktree(Repo& repo, const std::wstring& path,
                        std::wstring* err = nullptr);

private:
    std::wstring root_;
    std::vector<Repo> repos_;
};

} // namespace liney
