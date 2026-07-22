#include "WindowGeometry.h"

namespace liney {

WindowRect clampWindowToWorkArea(WindowRect window, WindowRect workArea) {
    const int right = workArea.x + workArea.width;
    const int bottom = workArea.y + workArea.height;
    if (window.x + window.width > right) window.x = right - window.width;
    if (window.y + window.height > bottom) window.y = bottom - window.height;
    if (window.x < workArea.x) window.x = workArea.x;
    if (window.y < workArea.y) window.y = workArea.y;
    return window;
}

} // namespace liney
