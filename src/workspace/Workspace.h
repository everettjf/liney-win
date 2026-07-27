#pragma once

#include <string>
#include <vector>

#include "workspace/GitStatusParser.h"

namespace liney {

enum class ProjectKind {
    Folder,
    GitRepository,
};

// One checkout of a repo (the main worktree plus any `git worktree add`ed ones).
struct Worktree {
    std::wstring path;
    std::wstring label;  // branch name or short path, for the sidebar
    GitWorktreeStatus status;
    bool git = false;
};

// A Workspace project. Explicit projects may be ordinary folders; projects
// discovered below workspaceRoot are Git repositories.
struct Repo {
    std::wstring name;
    std::wstring path;
    ProjectKind kind = ProjectKind::Folder;
    std::vector<Worktree> worktrees;
    bool expanded = false;  // sidebar disclosure state
    bool loaded = false;    // worktrees fetched yet?

    bool isGit() const { return kind == ProjectKind::GitRepository; }
};

// Normalize paths for stable, case-insensitive Workspace identity. This keeps
// the same folder from appearing twice due to slash, case, or trailing-slash
// differences in hand-edited configuration.
std::wstring normalizeWorkspacePath(const std::wstring& path);
bool workspacePathsEqual(const std::wstring& a, const std::wstring& b);
bool isGitRepositoryPath(const std::wstring& path);

// The sidebar's data model: Git repositories found one level under a root plus
// explicitly added Git repositories or ordinary folders. Git worktrees are
// loaded lazily the first time their repository is expanded.
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
    const std::vector<Repo>& repos() const { return repos_; }

    // Populate repo.worktrees via `git worktree list`. Ordinary folders have
    // no worktree children and never invoke Git.
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
