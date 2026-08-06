#pragma once

namespace liney {

// Decide whether a workspace sidebar target belongs to an existing terminal.
// Path comparison stays with the caller so it can use Windows' canonical,
// case-insensitive workspace identity rules.
bool workspaceSessionIsReusable(bool requestHasWorktree,
                                bool candidateHasWorktree,
                                bool worktreeMatches,
                                bool projectMatches,
                                bool cwdMatches);

}  // namespace liney
