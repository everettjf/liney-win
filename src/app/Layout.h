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
    // Windows chrome follows monitor DPI, independently of terminal zoom.
    // Ctrl +/- is a terminal-content preference and must not resize the whole
    // application shell.
    float uiScale = 1.0f;

    float sidebarW() const { return 224.0f * uiScale; }
    float filesPanelW() const { return 224.0f * uiScale; }
    float compactSidebarW() const { return 152.0f * uiScale; }
    float minimumTerminalW() const { return 360.0f * uiScale; }
    float tabBarH() const { return 40.0f * uiScale; }
    // Sidebar row height — roomier than the grid line height so entries
    // breathe (was cellH + 4, which read as cramped).
    float rowH() const { return 34.0f * uiScale; }
    float sidebarPad() const { return 12.0f * uiScale; }
    float sectionGap() const { return 14.0f * uiScale; }
    float gutter() const { return 1.0f * uiScale; }
    // Inner padding between a pane's border and its terminal grid, so text
    // doesn't press against the frame (the norm in Windows Terminal/Ghostty).
    // Derived from the cell so it scales with font size and DPI.
    float panePad() const { return 6.0f * uiScale; }
};

} // namespace liney
