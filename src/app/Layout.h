#pragma once

namespace liney {

// Axis-aligned rectangle in pixels. Used for all chrome/pane layout.
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// UI metrics, derived from the monospace cell size so chrome scales with font.
struct Metrics {
    float cellW = 8.0f;
    float cellH = 16.0f;

    float sidebarW() const { return cellW * 28.0f; }   // ~28 chars wide
    float filesPanelW() const { return cellW * 28.0f; } // right folder-tree panel
    float tabBarH() const { return cellH + 12.0f; }
    // Sidebar row height — roomier than the grid line height so entries
    // breathe (was cellH + 4, which read as cramped).
    float rowH() const { return cellH + 10.0f; }
    float sidebarPad() const { return 12.0f; }          // sidebar left/right inset
    float sectionGap() const { return cellH * 0.9f; }   // gap above a section header
    float gutter() const { return 1.0f; }               // gap between split panes
    // Inner padding between a pane's border and its terminal grid, so text
    // doesn't press against the frame (the norm in Windows Terminal/Ghostty).
    // Derived from the cell so it scales with font size and DPI.
    float panePad() const { return cellH * 0.35f; }
};

} // namespace liney
