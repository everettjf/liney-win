#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "render/IRenderer.h"

namespace liney {

// Stage-1 renderer: per-cell direct draw using Direct2D for background fills
// and DirectWrite for glyphs, presented through a DXGI / Direct3D 11 swap
// chain. Correct and GPU-backed, but re-rasterizes glyphs every frame. The
// glyph-atlas upgrade (Stage 2, see RENDERING.md) will replace the body of
// render() while reusing this class's device / swap-chain plumbing.
class D2DRenderer final : public IRenderer {
public:
    bool initialize(void* hwnd) override;
    void resize(unsigned widthPx, unsigned heightPx) override;
    void cellSize(unsigned& wPx, unsigned& hPx) const override;
    void setFont(const std::wstring& family, float sizePx) override;

    void beginFrame() override;
    void endFrame() override;
    void fillRect(float x, float y, float w, float h, const Color& c) override;
    void strokeRect(float x, float y, float w, float h, const Color& c,
                    float thickness) override;
    void drawText(const std::wstring& text, float x, float y, float maxW,
                  float rowH, const Color& c, bool bold) override;
    void drawGrid(const Grid& grid, float originX, float originY) override;

private:
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool createDeviceResources();
    bool buildTextFormats();
    bool createSwapChainResources();
    bool bindTarget();
    void releaseSwapChainResources();

    HWND hwnd_ = nullptr;
    unsigned widthPx_ = 0, heightPx_ = 0;
    float cellW_ = 0.0f, cellH_ = 0.0f;
    std::wstring fontFamily_ = L"Cascadia Mono";
    float fontSize_ = 16.0f;

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<IDWriteTextFormat> textFormatBold_;
};

} // namespace liney
