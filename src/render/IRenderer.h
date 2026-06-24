#pragma once

#include "render/Cell.h"

namespace liney {

// Renderer abstraction.
//
// The MVP ships a Direct2D/DirectWrite implementation (D2DRenderer). A future
// glyph-atlas + Direct3D 11 implementation can be swapped in behind this same
// interface without touching call sites. See RENDERING.md for the two-stage
// plan. The grid/dirty-set/cursor/selection parameters will grow as features
// land; for the scaffold, render() takes the full grid.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // Bind to a native window (HWND) and create GPU resources.
    virtual bool initialize(void* hwnd) = 0;

    // React to a client-area resize, in pixels.
    virtual void resize(unsigned widthPx, unsigned heightPx) = 0;

    // Current monospace cell size, in pixels.
    virtual void cellSize(unsigned& wPx, unsigned& hPx) const = 0;

    // Draw one frame from `grid` and present it.
    virtual void render(const Grid& grid) = 0;
};

} // namespace liney
