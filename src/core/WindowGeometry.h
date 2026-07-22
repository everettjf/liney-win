#pragma once

namespace liney {

struct WindowRect {
    int x;
    int y;
    int width;
    int height;
};

WindowRect clampWindowToWorkArea(WindowRect window, WindowRect workArea);

} // namespace liney
