#include "app/TabStripLayout.h"

#include <algorithm>
#include <cmath>

namespace liney {
namespace {

std::vector<float> shrinkToFit(std::vector<float> widths, float available,
                               float minimum) {
    if (widths.empty()) return widths;
    minimum = std::max(1.0f, minimum);
    for (float& width : widths) width = std::max(minimum, width);

    for (;;) {
        float total = 0.0f;
        size_t flexible = 0;
        for (float width : widths) {
            total += width;
            if (width > minimum + 0.01f) ++flexible;
        }
        const float excess = total - available;
        if (excess <= 0.01f || flexible == 0) break;

        const float reduction = excess / static_cast<float>(flexible);
        bool changed = false;
        for (float& width : widths) {
            if (width <= minimum + 0.01f) continue;
            const float next = std::max(minimum, width - reduction);
            changed = changed || next < width;
            width = next;
        }
        if (!changed) break;
    }
    return widths;
}

} // namespace

TabStripLayout layoutTabStrip(const std::vector<float>& preferredWidths,
                              size_t activeIndex, float availableWidth,
                              float minimumWidth, float overflowButtonWidth) {
    TabStripLayout result;
    if (preferredWidths.empty() || availableWidth <= 0.0f) return result;

    activeIndex = std::min(activeIndex, preferredWidths.size() - 1);
    minimumWidth = std::max(1.0f, minimumWidth);
    overflowButtonWidth = std::max(0.0f, overflowButtonWidth);

    float minimumTotal =
        minimumWidth * static_cast<float>(preferredWidths.size());
    size_t first = 0;
    size_t count = preferredWidths.size();
    float tabsWidth = availableWidth;
    float effectiveMinimum = minimumWidth;

    if (minimumTotal > availableWidth) {
        result.overflow = preferredWidths.size() > 1;
        const float reserved =
            result.overflow
                ? std::min(overflowButtonWidth,
                           std::max(0.0f, availableWidth - 1.0f))
                : 0.0f;
        tabsWidth = std::max(1.0f, availableWidth - reserved);
        effectiveMinimum = std::min(minimumWidth, tabsWidth);
        count = std::max<size_t>(
            1, std::min(preferredWidths.size(),
                        static_cast<size_t>(
                            std::floor(tabsWidth / effectiveMinimum))));
        first = activeIndex > count / 2 ? activeIndex - count / 2 : 0;
        if (first + count > preferredWidths.size())
            first = preferredWidths.size() - count;
    }

    std::vector<float> visible;
    visible.reserve(count);
    for (size_t i = 0; i < count; ++i)
        visible.push_back(preferredWidths[first + i]);
    visible = shrinkToFit(std::move(visible), tabsWidth, effectiveMinimum);

    float x = 0.0f;
    for (size_t i = 0; i < visible.size(); ++i) {
        result.items.push_back({first + i, x, visible[i]});
        x += visible[i];
    }
    return result;
}

size_t activeTabAfterClose(size_t tabCountBefore, size_t activeIndex,
                           size_t erasedIndex) {
    if (tabCountBefore <= 1) return 0;
    activeIndex = std::min(activeIndex, tabCountBefore - 1);
    erasedIndex = std::min(erasedIndex, tabCountBefore - 1);
    if (erasedIndex < activeIndex) return activeIndex - 1;
    if (erasedIndex == activeIndex && activeIndex == tabCountBefore - 1)
        return activeIndex - 1;
    return activeIndex;
}

} // namespace liney
