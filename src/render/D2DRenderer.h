#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "render/IRenderer.h"

namespace liney {

// Direct2D/DirectWrite renderer presented through a DXGI / D3D11 swap chain
// (WARP fallback when no hardware device is available; device-lost recovery
// rebuilds everything). Monochrome glyphs are rasterized once into a glyph
// atlas and drawn via FillOpacityMask tinted with the cell fg; color glyphs
// (emoji) and atlas misses fall back to per-cell DrawText with color-font
// support. See RENDERING.md for the design background.
class D2DRenderer final : public IRenderer {
public:
    bool initialize(void* hwnd) override;
    void resize(unsigned widthPx, unsigned heightPx) override;
    void cellSize(unsigned& wPx, unsigned& hPx) const override;
    void setFont(const std::wstring& family, float sizePx) override;
    void setColors(const Color& workspaceBg, const Color& termBg) override;

    void beginFrame() override;
    void endFrame() override;
    void pushClip(float x, float y, float w, float h) override;
    void popClip() override;
    void fillRect(float x, float y, float w, float h, const Color& c) override;
    void strokeRect(float x, float y, float w, float h, const Color& c,
                    float thickness) override;
    void drawText(const std::wstring& text, float x, float y, float maxW,
                  float rowH, const Color& c, bool bold) override;
    bool drawImage(const std::wstring& path, float x, float y, float w,
                   float h) override;
    void drawIcon(IconKind kind, float x, float y, float size,
                  const Color& c) override;
    void drawGrid(const Grid& grid, float originX, float originY) override;

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool createDeviceResources();
    bool buildTextFormats();
    bool createSwapChainResources();
    bool bindTarget();
    void releaseSwapChainResources();
    // Tear down and rebuild all device-bound state after device loss (driver
    // update, TDR, RDP GPU switch). Returns true when rendering can resume.
    bool recreateDevice();

    HWND hwnd_ = nullptr;
    unsigned widthPx_ = 0, heightPx_ = 0;
    float cellW_ = 0.0f, cellH_ = 0.0f;
    std::wstring fontFamily_ = L"Cascadia Mono";
    float fontSize_ = 16.0f;
    Color workspaceBg_{ 13, 13, 15 };
    Color termBg_{ 0, 0, 0 };

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    // Pick the text format matching a cell's bold/italic flags (never null).
    IDWriteTextFormat* cellFormat(uint32_t flags) const;

    // Draw the cursor for `grid` (shape, blink phase, focus state).
    void drawCursor(const Grid& grid, float originX, float originY);

    // Blink half-period; the classic Windows caret cadence.
    static constexpr unsigned long long kCursorBlinkMs = 530;

    // Glyph atlas: each unique (grapheme, bold/italic, wide) is rasterized
    // once into an offscreen bitmap; cells then draw as FillOpacityMask with
    // the cell's fg brush instead of re-shaping text every frame. Falls back
    // to per-cell DrawText when the atlas can't be created.
    bool ensureAtlas();
    bool atlasSlot(const std::wstring& ch, uint32_t flags, D2D1_RECT_F& src);
    static constexpr float kAtlasSize = 2048.0f;

    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<IDWriteTextFormat> textFormatBold_;
    ComPtr<IDWriteTextFormat> textFormatItalic_;
    ComPtr<IDWriteTextFormat> textFormatBoldItalic_;
    ComPtr<IWICImagingFactory> wicFactory_;
    // Cache of loaded images by path (null entry = failed-to-load, don't retry).
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> imageCache_;

    // Glyph atlas state (see ensureAtlas/atlasSlot). Keys are the grapheme's
    // UTF-16 units plus one trailing style unit.
    ComPtr<ID2D1BitmapRenderTarget> atlasRT_;
    ComPtr<ID2D1Bitmap> atlasBitmap_;
    ComPtr<ID2D1SolidColorBrush> atlasBrush_;  // white; owned by atlasRT_
    std::unordered_map<std::wstring, D2D1_RECT_F> glyphCache_;
    float atlasX_ = 0.0f, atlasY_ = 0.0f;      // next free slot position
    bool atlasBroken_ = false;                 // creation failed: use DrawText
    bool deviceLost_ = false;   // EndDraw/Present reported device removal
    bool frameOpen_ = false;    // BeginDraw issued; endFrame may EndDraw
    bool atlasNeedsReset_ = false;  // atlas overflowed mid-frame; wipe between frames
    bool simulatedDeviceLoss_ = false; // one-shot headless recovery test hook
    // Per-cell find-highlight overlay, rebuilt at the top of drawGrid
    // (0 none, 1 match, 2 active match). Member to avoid per-frame realloc.
    std::vector<uint8_t> findOverlay_;
};

} // namespace liney
