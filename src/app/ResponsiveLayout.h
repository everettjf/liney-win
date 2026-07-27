#pragma once

namespace liney {

struct ResponsivePanelLayout {
    float leftWidth = 0.0f;
    float centerWidth = 0.0f;
    float rightWidth = 0.0f;
    bool leftCompact = false;
    bool rightCompact = false;
};

// Preserve terminal working space before side-panel width. The workspace panel
// has priority over the optional file navigator at very narrow widths.
ResponsivePanelLayout layoutResponsivePanels(
    float totalWidth, bool showLeft, bool showRight, float desiredPanelWidth,
    float compactPanelWidth, float minimumTerminalWidth);

} // namespace liney
