#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace liney {

// 24-bit RGB color.
struct Color {
    uint8_t r = 0, g = 0, b = 0;
};

// Terminal color theme: default fg/bg plus the 16-color ANSI palette. The
// defaults match the built-in xterm-ish palette, so an unset theme is a no-op.
struct Theme {
    Color background{ 0, 0, 0 };
    Color foreground{ 204, 204, 204 };
    Color ansi[16] = {
        { 0, 0, 0 },       { 205, 0, 0 },   { 0, 205, 0 },   { 205, 205, 0 },
        { 0, 0, 238 },     { 205, 0, 205 }, { 0, 205, 205 }, { 229, 229, 229 },
        { 127, 127, 127 }, { 255, 0, 0 },   { 0, 255, 0 },   { 255, 255, 0 },
        { 92, 92, 255 },   { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 },
    };
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
// it is produced from a terminal screen snapshot (built-in emulator or
// libghostty-vt).
struct Grid {
    int cols = 0;
    int rows = 0;
    std::vector<Cell> cells;

    // Cursor position (cell coordinates) and whether it should be drawn.
    int cursorX = 0;
    int cursorY = 0;
    bool cursorVisible = false;

    // Selection highlight, in cell coordinates, normalized and inclusive
    // (row-major from start to end). Set by the UI on the focused pane's grid.
    bool hasSelection = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;

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
