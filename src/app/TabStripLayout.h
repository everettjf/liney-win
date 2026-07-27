#pragma once

#include <cstddef>
#include <vector>

namespace liney {

// One visible item in the tab strip. `index` always refers to the original
// tab vector, even when older tabs are hidden behind the overflow menu.
struct TabStripItem {
    size_t index = 0;
    float x = 0.0f;
    float width = 0.0f;
};

struct TabStripLayout {
    std::vector<TabStripItem> items;
    bool overflow = false;
};

// Fit tabs into `availableWidth`, preserving their preferred widths when
// possible. Tabs shrink down to `minimumWidth`; when even that cannot fit, a
// contiguous window containing `activeIndex` is returned and room is reserved
// for a single overflow button.
TabStripLayout layoutTabStrip(const std::vector<float>& preferredWidths,
                              size_t activeIndex, float availableWidth,
                              float minimumWidth, float overflowButtonWidth);

// Preserve the identity of the active tab when a different tab is erased.
// Closing the active tab selects the tab that moved into its slot, or the
// previous final tab when the erased tab was last.
size_t activeTabAfterClose(size_t tabCountBefore, size_t activeIndex,
                           size_t erasedIndex);

} // namespace liney
