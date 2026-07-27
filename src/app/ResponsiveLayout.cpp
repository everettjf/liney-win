#include "app/ResponsiveLayout.h"

#include <algorithm>

namespace liney {

ResponsivePanelLayout layoutResponsivePanels(
    float totalWidth, bool showLeft, bool showRight, float desiredPanelWidth,
    float compactPanelWidth, float minimumTerminalWidth) {
    ResponsivePanelLayout out;
    totalWidth = std::max(0.0f, totalWidth);
    desiredPanelWidth = std::max(0.0f, desiredPanelWidth);
    compactPanelWidth =
        std::clamp(compactPanelWidth, 0.0f, desiredPanelWidth);
    minimumTerminalWidth =
        std::clamp(minimumTerminalWidth, 0.0f, totalWidth);

    float budget = std::max(0.0f, totalWidth - minimumTerminalWidth);
    if (showLeft && showRight) {
        if (budget >= desiredPanelWidth * 2.0f) {
            out.leftWidth = out.rightWidth = desiredPanelWidth;
        } else if (budget >= compactPanelWidth * 2.0f) {
            out.leftWidth = out.rightWidth = budget * 0.5f;
            out.leftCompact = out.rightCompact = true;
        } else if (budget >= compactPanelWidth) {
            // The workspace carries repository/task context; retain it and
            // collapse the optional file navigator first.
            out.leftWidth = std::min(desiredPanelWidth, budget);
            out.leftCompact = out.leftWidth < desiredPanelWidth;
            out.rightCompact = true;
        } else {
            // A sliver of panel is worse than no panel: its labels are clipped
            // and it steals the last usable terminal columns. Collapse both
            // panels and leave their persistent toolbar toggles available.
            out.leftCompact = out.rightCompact = true;
        }
    } else if (showLeft || showRight) {
        // Only expose a panel when it can reach its compact readable width.
        const float width = budget >= compactPanelWidth
                                ? std::min(desiredPanelWidth, budget)
                                : 0.0f;
        if (showLeft) {
            out.leftWidth = width;
            out.leftCompact = width < desiredPanelWidth;
        } else {
            out.rightWidth = width;
            out.rightCompact = width < desiredPanelWidth;
        }
    }
    out.centerWidth =
        std::max(0.0f, totalWidth - out.leftWidth - out.rightWidth);
    return out;
}

} // namespace liney
