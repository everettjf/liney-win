#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace liney {

// 24-bit RGB color.
struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

// Per-cell attribute bits.
enum CellFlags : uint32_t {
    kFlagNone      = 0,
    kFlagBold      = 1u << 0,
    kFlagItalic    = 1u << 1,
    kFlagUnderline = 1u << 2,
    kFlagInverse   = 1u << 3,
};

// A single terminal cell. `ch` holds one grapheme (UTF-16; may span >1 code
// unit for surrogate pairs / combining marks). Empty or L" " == blank.
struct Cell {
    std::wstring ch;
    Color fg{ 220, 220, 220 };
    Color bg{ 0, 0, 0 };
    uint32_t flags = kFlagNone;
};

// A fixed-size, row-major grid of cells. This is what the renderer consumes;
// it will eventually be produced from a libghostty-vt screen snapshot.
struct Grid {
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells;

    void resize(int c, int r) {
        cols = c < 0 ? 0 : c;
        rows = r < 0 ? 0 : r;
        cells.assign(static_cast<size_t>(cols) * rows, Cell{});
    }

    Cell& at(int x, int y) { return cells[static_cast<size_t>(y) * cols + x]; }
    const Cell& at(int x, int y) const {
        return cells[static_cast<size_t>(y) * cols + x];
    }
};

} // namespace liney
