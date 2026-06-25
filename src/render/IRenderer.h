#pragma once

#include <string>

#include "render/Cell.h"

namespace liney {

// Renderer abstraction.
//
// The MVP ships a Direct2D/DirectWrite implementation (D2DRenderer). A future
// glyph-atlas + Direct3D 11 implementation can be swapped in behind this same
// interface. The renderer composites a frame from primitives: chrome (sidebar,
// tab strip, pane borders) via fillRect/strokeRect/drawText, and terminal panes
// via drawGrid at a pixel origin. See RENDERING.md for the two-stage plan.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Bind to a native window (HWND) and create GPU resources.
    virtual bool initialize(void* hwnd) = 0;

    // React to a client-area resize, in pixels.
    virtual void resize(unsigned widthPx, unsigned heightPx) = 0;

    // Current monospace cell size, in pixels.
    virtual void cellSize(unsigned& wPx, unsigned& hPx) const = 0;

    // Frame lifecycle: clear to the background, draw, then present.
    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;

    // Chrome primitives (pixel coordinates).
    virtual void fillRect(float x, float y, float w, float h, const Color& c) = 0;
    virtual void strokeRect(float x, float y, float w, float h, const Color& c,
                            float thickness = 1.0f) = 0;
    // Single line of UI text, clipped to [x, x+maxW] x [y, y+rowH].
    virtual void drawText(const std::wstring& text, float x, float y, float maxW,
                          float rowH, const Color& c, bool bold = false) = 0;

    // Draw a terminal grid (cells + cursor) with its top-left at (originX,
    // originY), clipped to the grid's pixel extent.
    virtual void drawGrid(const Grid& grid, float originX, float originY) = 0;
};

} // namespace liney
