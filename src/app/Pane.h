#pragma once

#include <memory>

#include "app/Layout.h"
#include "core/TerminalSession.h"

namespace liney {

// Split orientation. Cols = children side by side (vertical divider); Rows =
// children stacked top/bottom (horizontal divider).
enum class SplitDir { Cols, Rows };

// A node in a tab's binary split tree. A leaf hosts one TerminalSession; a split
// node has two children laid out per `dir` and `ratio` (fraction for child a).
struct Pane {
    // Leaf payload (non-null iff !isSplit).
    std::unique_ptr<TerminalSession> session;

    // Split payload (valid iff isSplit).
    bool isSplit = false;
    SplitDir dir = SplitDir::Cols;
    float ratio = 0.5f;
    std::unique_ptr<Pane> a, b;

    Pane* parent = nullptr;
    Rect rect;  // computed each layout pass

    bool leaf() const { return !isSplit; }
};

} // namespace liney
