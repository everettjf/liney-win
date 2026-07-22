#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/Layout.h"
#include "app/Pane.h"
#include "core/TerminalSession.h"

namespace liney {

// A tab: a binary split tree of panes with one active (focused) leaf.
class Tab {
public:
    explicit Tab(std::unique_ptr<TerminalSession> first);
    // Build from a pre-assembled pane tree (used to restore a saved layout).
    // Fixes up parent pointers and focuses the first leaf.
    explicit Tab(std::unique_ptr<Pane> root);

    Pane* root() const { return root_.get(); }
    Pane* active() const { return active_; }
    void setActive(Pane* leaf);

    // Split the active leaf along `dir`, moving the existing session into one
    // child and `incoming` into the other (which becomes active).
    void splitActive(SplitDir dir, std::unique_ptr<TerminalSession> incoming);

    // Close the active leaf and collapse its parent split. Returns false when
    // the tab has no panes left (caller should drop the tab).
    bool closeActive();

    // Move the active session out of this split, collapsing its former parent.
    // Returns null for a single-pane tab (which callers can duplicate instead).
    std::unique_ptr<TerminalSession> detachActive();
    void swapActiveWithNext();
    void moveActiveForward();

    // Close every pane except the active one, collapsing the tab back to a
    // single pane. No-op when already a single pane.
    void closeOthers();
    bool isSplit() const { return root_ && root_->isSplit; }

    // Assign pixel rects to every pane within `area` and resize sessions to fit.
    void layout(const Rect& area, const Metrics& m);

    // Reset every split ratio to 0.5 so panes are evenly distributed.
    void equalize();

    // Pane zoom: when a leaf is zoomed it temporarily fills the whole tab
    // (industry-standard for working in a deeply-split layout — tmux/iTerm2).
    // setZoom(nullptr) or setting a non-leaf clears it.
    void setZoom(Pane* p);
    Pane* zoom() const { return zoom_; }

    Pane* hitTest(float x, float y) const;          // leaf at point, or nullptr
    // Split node whose divider is within `tol` px of the point, or nullptr.
    Pane* splitDividerAt(float x, float y, float tol) const;
    void focusDir(SplitDir axis, bool positive);     // move focus geometrically
    std::vector<Pane*> leaves() const;
    std::wstring title() const;
    void setCustomTitle(std::wstring value) { customTitle_ = std::move(value); }
    const std::wstring& customTitle() const { return customTitle_; }
    void setPinned(bool value) { pinned_ = value; }
    bool pinned() const { return pinned_; }

private:
    std::unique_ptr<Pane> root_;
    Pane* active_ = nullptr;
    Pane* zoom_ = nullptr;  // zoomed leaf (fills the tab), or nullptr
    std::wstring customTitle_;
    bool pinned_ = false;
};

} // namespace liney
