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

    float sidebarW() const { return cellW * 26.0f; }   // ~26 chars wide
    float tabBarH() const { return cellH + 10.0f; }
    float rowH() const { return cellH + 4.0f; }         // sidebar row height
    float gutter() const { return 1.0f; }               // gap between split panes
};

} // namespace liney
