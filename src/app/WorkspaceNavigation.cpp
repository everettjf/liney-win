#include "app/WorkspaceNavigation.h"

namespace liney {

bool workspaceSessionIsReusable(bool requestHasWorktree,
                                bool candidateHasWorktree,
                                bool worktreeMatches,
                                bool projectMatches,
                                bool cwdMatches) {
    if (requestHasWorktree) return worktreeMatches || cwdMatches;
    return (projectMatches && !candidateHasWorktree) || cwdMatches;
}

}  // namespace liney
