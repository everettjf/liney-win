#include "render/D2DRenderer.h"

#include <cmath>
#include <utility>

namespace liney {

static D2D1_COLOR_F toColorF(const Color& c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}

bool D2DRenderer::initialize(void* hwnd) {
    hwnd_ = static_cast<HWND>(hwnd);
    return createDeviceResources();
}

bool D2DRenderer::createDeviceResources() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        d3dDevice_.GetAddressOf(), &featureLevel, d3dContext_.GetAddressOf());
    if (FAILED(hr)) {
        // No usable GPU (VM, RDP session, broken driver): fall back to the
        // WARP software rasterizer instead of failing to start at all.
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            d3dDevice_.ReleaseAndGetAddressOf(), &featureLevel,
            d3dContext_.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice))) return false;

    D2D1_FACTORY_OPTIONS opts{};
    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
        reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = d2dDevice_->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    // WIC factory for loading image files (best-effort; drawImage no-ops if null).
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(wicFactory_.GetAddressOf()));

    return buildTextFormats();
}

bool D2DRenderer::buildTextFormats() {
    if (!dwriteFactory_) return false;
    textFormat_.Reset();
    textFormatBold_.Reset();
    textFormatItalic_.Reset();
    textFormatBoldItalic_.Reset();

    // Font (or size) changed: every cached glyph is stale. The atlas is
    // recreated lazily at the new cell size.
    glyphCache_.clear();
    atlasBrush_.Reset();
    atlasBitmap_.Reset();
    atlasRT_.Reset();
    atlasX_ = atlasY_ = 0.0f;
    atlasBroken_ = false;

    const wchar_t* family = fontFamily_.c_str();
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, fontSize_, L"en-us",
        textFormat_.GetAddressOf());
    if (FAILED(hr)) {
        // Requested family may be absent; fall back to a guaranteed monospace.
        family = L"Consolas";
        hr = dwriteFactory_->CreateTextFormat(
            family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize_, L"en-us",
            textFormat_.GetAddressOf());
        if (FAILED(hr)) return false;
    }
    textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Bold / italic variants for SGR-styled cells (best-effort; cellFormat
    // falls back to the plain format for any that failed).
    auto makeVariant = [&](DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                           ComPtr<IDWriteTextFormat>& out) {
        if (SUCCEEDED(dwriteFactory_->CreateTextFormat(
                family, nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                fontSize_, L"en-us", out.GetAddressOf()))) {
            out->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
    };
    makeVariant(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, textFormatBold_);
    makeVariant(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
                textFormatItalic_);
    makeVariant(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_ITALIC,
                textFormatBoldItalic_);

    // Derive the monospace cell size from a representative glyph.
    ComPtr<IDWriteTextLayout> layout;
    hr = dwriteFactory_->CreateTextLayout(L"M", 1, textFormat_.Get(), 1000.0f,
                                          1000.0f, layout.GetAddressOf());
    if (SUCCEEDED(hr)) {
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        cellW_ = tm.width > 0.0f ? tm.width : fontSize_ * 0.6f;
        cellH_ = tm.height > 0.0f ? tm.height : fontSize_ * 1.2f;
    } else {
        cellW_ = fontSize_ * 0.6f;
        cellH_ = fontSize_ * 1.2f;
    }
    // Snap the cell to whole pixels. Drawing at a fractional pitch makes
    // column N sit at a different subpixel phase than the atlas rasterized
    // (blurry glyphs) and — worse — diverges from the rounded size handed to
    // cellSize(), which the app uses for mouse hit-testing: at column 100 the
    // drift is several columns. One integral pitch keeps draw + hit-test
    // + atlas in exact agreement.
    cellW_ = std::ceil(cellW_);
    cellH_ = std::ceil(cellH_);
    return true;
}

void D2DRenderer::setFont(const std::wstring& family, float sizePx) {
    fontFamily_ = family.empty() ? L"Cascadia Mono" : family;
    fontSize_ = (sizePx < 6.0f) ? 6.0f : (sizePx > 96.0f ? 96.0f : sizePx);
    buildTextFormats();
}

bool D2DRenderer::createSwapChainResources() {
    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice))) return false;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) return false;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(
            __uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = widthPx_;
    scd.Height = heightPx_;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    HRESULT hr = factory->CreateSwapChainForHwnd(
        d3dDevice_.Get(), hwnd_, &scd, nullptr, nullptr,
        swapChain_.GetAddressOf());
    if (FAILED(hr)) return false;

    return bindTarget();
}

bool D2DRenderer::bindTarget() {
    ComPtr<IDXGISurface> surface;
    HRESULT hr = swapChain_->GetBuffer(
        0, __uuidof(IDXGISurface),
        reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    hr = d2dContext_->CreateBitmapFromDxgiSurface(
        surface.Get(), &props, targetBitmap_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) return false;

    d2dContext_->SetTarget(targetBitmap_.Get());

    if (!brush_) {
        d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), brush_.GetAddressOf());
    }
    return true;
}

void D2DRenderer::releaseSwapChainResources() {
    if (d2dContext_) d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
}

void D2DRenderer::resize(unsigned widthPx, unsigned heightPx) {
    widthPx_ = widthPx;
    heightPx_ = heightPx;
    if (!d3dDevice_ || widthPx == 0 || heightPx == 0) return;

    if (!swapChain_) {
        createSwapChainResources();
        return;
    }
    releaseSwapChainResources();
    HRESULT hr = swapChain_->ResizeBuffers(0, widthPx, heightPx,
                                           DXGI_FORMAT_UNKNOWN, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        deviceLost_ = true;
        return;
    }
    if (FAILED(hr)) return;  // leave target unbound; beginFrame skips the frame
    bindTarget();
}

void D2DRenderer::cellSize(unsigned& wPx, unsigned& hPx) const {
    wPx = static_cast<unsigned>(cellW_ + 0.5f);
    hPx = static_cast<unsigned>(cellH_ + 0.5f);
}

void D2DRenderer::setColors(const Color& workspaceBg, const Color& termBg) {
    workspaceBg_ = workspaceBg;
    termBg_ = termBg;
}

void D2DRenderer::beginFrame() {
    if (deviceLost_ && !recreateDevice()) return;
    if (atlasNeedsReset_ && atlasRT_) {
        // Deferred from atlasSlot: safe to wipe between frames.
        glyphCache_.clear();
        atlasX_ = atlasY_ = 0.0f;
        atlasRT_->BeginDraw();
        atlasRT_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));
        if (FAILED(atlasRT_->EndDraw())) atlasBroken_ = true;
        atlasNeedsReset_ = false;
    }
    if (!d2dContext_ || !targetBitmap_ || !brush_) return;
    d2dContext_->BeginDraw();
    frameOpen_ = true;
    d2dContext_->Clear(toColorF(workspaceBg_));  // workspace bg (gutters/margins)
}

void D2DRenderer::endFrame() {
    if (!d2dContext_ || !swapChain_ || !frameOpen_) return;
    frameOpen_ = false;
    const HRESULT hrDraw = d2dContext_->EndDraw();
    const HRESULT hrPresent = swapChain_->Present(1, 0);
    // A GPU driver update / TDR reset / RDP GPU switch removes the device;
    // without recovery every later Present fails silently and the window
    // freezes forever. Flag it and rebuild everything next frame.
    if (hrDraw == D2DERR_RECREATE_TARGET ||
        hrPresent == DXGI_ERROR_DEVICE_REMOVED ||
        hrPresent == DXGI_ERROR_DEVICE_RESET) {
        deviceLost_ = true;
    }
}

bool D2DRenderer::recreateDevice() {
    // Drop every device-bound object, then rebuild the device, swap chain and
    // (via buildTextFormats inside createDeviceResources) the glyph atlas.
    releaseSwapChainResources();
    swapChain_.Reset();
    brush_.Reset();
    imageCache_.clear();
    glyphCache_.clear();
    atlasBrush_.Reset();
    atlasBitmap_.Reset();
    atlasRT_.Reset();
    atlasX_ = atlasY_ = 0.0f;
    atlasBroken_ = false;
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();

    if (!createDeviceResources()) return false;
    deviceLost_ = false;
    if (widthPx_ && heightPx_) createSwapChainResources();
    return d2dContext_ && targetBitmap_ && brush_;
}

void D2DRenderer::pushClip(float x, float y, float w, float h) {
    if (!d2dContext_) return;
    d2dContext_->PushAxisAlignedClip(D2D1::RectF(x, y, x + w, y + h),
                                     D2D1_ANTIALIAS_MODE_ALIASED);
}

void D2DRenderer::popClip() {
    if (d2dContext_) d2dContext_->PopAxisAlignedClip();
}

void D2DRenderer::fillRect(float x, float y, float w, float h, const Color& c) {
    if (!d2dContext_ || !brush_) return;
    brush_->SetColor(toColorF(c));
    d2dContext_->FillRectangle(D2D1::RectF(x, y, x + w, y + h), brush_.Get());
}

void D2DRenderer::strokeRect(float x, float y, float w, float h, const Color& c,
                             float thickness) {
    if (!d2dContext_ || !brush_) return;
    brush_->SetColor(toColorF(c));
    // Inset by half the stroke so the border stays inside the rect.
    const float i = thickness * 0.5f;
    d2dContext_->DrawRectangle(D2D1::RectF(x + i, y + i, x + w - i, y + h - i),
                               brush_.Get(), thickness);
}

void D2DRenderer::drawText(const std::wstring& text, float x, float y,
                           float maxW, float rowH, const Color& c, bool bold) {
    if (!d2dContext_ || !brush_ || text.empty()) return;
    IDWriteTextFormat* fmt =
        bold && textFormatBold_ ? textFormatBold_.Get() : textFormat_.Get();
    brush_->SetColor(toColorF(c));
    d2dContext_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt,
                          D2D1::RectF(x, y, x + maxW, y + rowH), brush_.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

bool D2DRenderer::drawImage(const std::wstring& path, float x, float y, float w,
                            float h) {
    if (!d2dContext_) return false;

    auto it = imageCache_.find(path);
    if (it == imageCache_.end()) {
        // Load via WIC, convert to a Direct2D bitmap, and cache (null on fail).
        ComPtr<ID2D1Bitmap> bmp;
        if (wicFactory_) {
            ComPtr<IWICBitmapDecoder> decoder;
            ComPtr<IWICBitmapFrameDecode> frame;
            ComPtr<IWICFormatConverter> conv;
            if (SUCCEEDED(wicFactory_->CreateDecoderFromFilename(
                    path.c_str(), nullptr, GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf())) &&
                SUCCEEDED(decoder->GetFrame(0, frame.GetAddressOf())) &&
                SUCCEEDED(wicFactory_->CreateFormatConverter(conv.GetAddressOf())) &&
                SUCCEEDED(conv->Initialize(
                    frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                    WICBitmapDitherTypeNone, nullptr, 0.0,
                    WICBitmapPaletteTypeMedianCut))) {
                d2dContext_->CreateBitmapFromWicBitmap(conv.Get(), nullptr,
                                                       bmp.GetAddressOf());
            }
        }
        it = imageCache_.emplace(path, bmp).first;
    }
    if (!it->second) return false;
    d2dContext_->DrawBitmap(it->second.Get(), D2D1::RectF(x, y, x + w, y + h),
                            1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}

void D2DRenderer::drawIcon(IconKind kind, float x, float y, float size,
                           const Color& c) {
    if (!d2dContext_ || !brush_) return;
    brush_->SetColor(toColorF(c));
    ID2D1DeviceContext* dc = d2dContext_.Get();
    ID2D1SolidColorBrush* br = brush_.Get();
    const float p = size * 0.16f;             // padding
    const float bx = x + p, by = y + p, s = size - 2 * p;
    const float cx = x + size * 0.5f, cy = y + size * 0.5f;
    const float t = size * 0.085f < 1.2f ? 1.2f : size * 0.085f;
    auto line = [&](float x1, float y1, float x2, float y2) {
        dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), br, t);
    };
    auto fillR = [&](float x1, float y1, float x2, float y2) {
        dc->FillRectangle(D2D1::RectF(x1, y1, x2, y2), br);
    };
    auto ring = [&](float ex, float ey, float rx, float ry) {
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ex, ey), rx, ry), br, t);
    };
    auto dot = [&](float ex, float ey, float r) {
        dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ex, ey), r, r), br);
    };
    switch (kind) {
    case IconKind::Folder:
        fillR(bx, by + s * 0.16f, bx + s * 0.42f, by + s * 0.34f);     // tab
        fillR(bx, by + s * 0.30f, bx + s, by + s * 0.84f);             // body
        break;
    case IconKind::File: {
        const float lx = bx + s * 0.20f, rx = bx + s * 0.80f;
        dc->DrawRectangle(D2D1::RectF(lx, by, rx, by + s), br, t);
        line(bx + s * 0.34f, by + s * 0.40f, bx + s * 0.66f, by + s * 0.40f);
        line(bx + s * 0.34f, by + s * 0.58f, bx + s * 0.66f, by + s * 0.58f);
        break;
    }
    case IconKind::Branch: {
        const float r = s * 0.13f;
        const float tx = bx + s * 0.28f, ty = by + s * 0.22f;
        const float btmY = by + s * 0.80f, brx = bx + s * 0.74f, bry = by + s * 0.50f;
        line(tx, ty, tx, btmY);            // trunk
        line(tx, bry, brx, bry);           // branch
        dot(tx, ty, r); dot(tx, btmY, r); dot(brx, bry, r);
        break;
    }
    case IconKind::Globe:
        ring(cx, cy, s * 0.46f, s * 0.46f);
        ring(cx, cy, s * 0.18f, s * 0.46f);            // meridian
        line(cx - s * 0.46f, cy, cx + s * 0.46f, cy);  // equator
        break;
    case IconKind::Spark:
        line(cx, by, cx, by + s);                              // |
        line(bx + s * 0.18f, cy, bx + s * 0.82f, cy);          // -
        line(bx + s * 0.22f, by + s * 0.22f, bx + s * 0.78f, by + s * 0.78f);  // diagonal
        line(bx + s * 0.78f, by + s * 0.22f, bx + s * 0.22f, by + s * 0.78f);  // diagonal
        break;
    case IconKind::Power:
        ring(cx, cy + s * 0.06f, s * 0.40f, s * 0.40f);
        fillR(cx - t * 0.5f, by, cx + t * 0.5f, cy);   // top stem (overdraws ring gap)
        break;
    case IconKind::Settings:
        for (int i = 0; i < 3; ++i) {
            float ly = by + s * (0.22f + 0.28f * i);
            line(bx, ly, bx + s, ly);
            float kx = bx + s * (i == 0 ? 0.66f : i == 1 ? 0.30f : 0.54f);
            fillR(kx - s * 0.07f, ly - s * 0.09f, kx + s * 0.07f, ly + s * 0.09f);
        }
        break;
    case IconKind::Download:
        line(cx, by + s * 0.10f, cx, by + s * 0.60f);              // shaft
        line(cx - s * 0.20f, by + s * 0.40f, cx, by + s * 0.62f);  // chevron
        line(cx + s * 0.20f, by + s * 0.40f, cx, by + s * 0.62f);
        line(bx + s * 0.18f, by + s * 0.86f, bx + s * 0.82f, by + s * 0.86f);  // base
        break;
    case IconKind::Up:
        line(cx, by + s * 0.85f, cx, by + s * 0.20f);             // shaft
        line(cx - s * 0.22f, by + s * 0.42f, cx, by + s * 0.18f); // chevron up
        line(cx + s * 0.22f, by + s * 0.42f, cx, by + s * 0.18f);
        break;
    case IconKind::Coffee:
        dc->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(bx, by + s * 0.30f,
                                          bx + s * 0.72f, by + s * 0.82f),
                               s * 0.08f, s * 0.08f), br, t);
        ring(bx + s * 0.78f, by + s * 0.56f, s * 0.18f, s * 0.18f);
        line(bx + s * 0.20f, by + s * 0.18f, bx + s * 0.20f, by);
        line(bx + s * 0.48f, by + s * 0.18f, bx + s * 0.48f, by);
        break;
    case IconKind::Menu:  // overflow / more: three horizontal dots
        for (int i = 0; i < 3; ++i)
            dot(bx + s * (0.20f + 0.30f * i), cy, s * 0.09f);
        break;
    }
}

IDWriteTextFormat* D2DRenderer::cellFormat(uint32_t flags) const {
    const bool bold = (flags & kFlagBold) != 0;
    const bool italic = (flags & kFlagItalic) != 0;
    if (bold && italic && textFormatBoldItalic_) return textFormatBoldItalic_.Get();
    if (italic && textFormatItalic_) return textFormatItalic_.Get();
    if (bold && textFormatBold_) return textFormatBold_.Get();
    return textFormat_.Get();
}

bool D2DRenderer::ensureAtlas() {
    if (atlasBroken_) return false;
    if (atlasRT_) return true;
    if (!d2dContext_) return false;
    if (FAILED(d2dContext_->CreateCompatibleRenderTarget(
            D2D1::SizeF(kAtlasSize, kAtlasSize), atlasRT_.GetAddressOf())) ||
        FAILED(atlasRT_->GetBitmap(atlasBitmap_.ReleaseAndGetAddressOf())) ||
        FAILED(atlasRT_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White),
            atlasBrush_.ReleaseAndGetAddressOf()))) {
        atlasBrush_.Reset();
        atlasBitmap_.Reset();
        atlasRT_.Reset();
        atlasBroken_ = true;
        return false;
    }
    atlasX_ = atlasY_ = 0.0f;
    glyphCache_.clear();
    return true;
}

bool D2DRenderer::atlasSlot(const std::wstring& ch, uint32_t flags,
                            D2D1_RECT_F& src) {
    if (!ensureAtlas()) return false;

    wchar_t styleKey = 1;  // never 0 so the key unit can't be a terminator
    if (flags & kFlagBold) styleKey |= 2;
    if (flags & kFlagItalic) styleKey |= 4;
    if (flags & kFlagWide) styleKey |= 8;
    std::wstring key = ch;
    key.push_back(styleKey);

    auto it = glyphCache_.find(key);
    if (it != glyphCache_.end()) {
        src = it->second;
        return true;
    }

    const float w = (flags & kFlagWide) ? cellW_ * 2.0f : cellW_;
    const float h = cellH_;
    if (atlasX_ + w > kAtlasSize) {
        atlasX_ = 0.0f;
        atlasY_ += h;
    }
    if (atlasY_ + h > kAtlasSize) {
        // Atlas full (enormous glyph variety). Don't wipe mid-frame: the
        // FillOpacityMask commands already batched on d2dContext_ this frame
        // still sample the existing slots, so clearing now would corrupt
        // cells drawn earlier in the pass. Fall back to DrawText for this
        // glyph and rebuild the atlas before the next frame begins.
        atlasNeedsReset_ = true;
        return false;
    }

    const D2D1_RECT_F rect =
        D2D1::RectF(atlasX_, atlasY_, atlasX_ + w, atlasY_ + h);
    atlasRT_->BeginDraw();
    atlasRT_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
    atlasRT_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));
    atlasRT_->DrawText(ch.c_str(), static_cast<UINT32>(ch.size()),
                       cellFormat(flags), rect, atlasBrush_.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
    atlasRT_->PopAxisAlignedClip();
    if (FAILED(atlasRT_->EndDraw())) {
        atlasBroken_ = true;
        return false;
    }
    atlasX_ += w;
    glyphCache_.emplace(std::move(key), rect);
    src = rect;
    return true;
}

// True for grapheme clusters that may carry a color (emoji) glyph: anything
// non-BMP (surrogate pairs — most emoji), a VS16 emoji-presentation selector,
// or the BMP symbol/dingbat blocks. These bypass the atlas (which tints a
// monochrome mask) and render via DrawText with color-font support. Non-emoji
// non-BMP text (e.g. CJK extension B) also lands here — it just takes the
// slower per-cell path, which is rare enough not to matter.
static bool isColorGlyph(const std::wstring& ch) {
    for (wchar_t u : ch) {
        if (u >= 0xD800 && u <= 0xDFFF) return true;  // non-BMP
        if (u == 0xFE0F) return true;                 // VS16: emoji presentation
        if (u >= 0x2600 && u <= 0x27BF) return true;  // misc symbols / dingbats
        if (u == 0x2B50 || u == 0x2B55) return true;  // ⭐ ⭕
    }
    return false;
}

// Effective fg/bg of one cell after inverse video and the selection / find
// highlights. Shared by both drawGrid passes so backgrounds and glyphs agree.
// `findHit` comes from the per-frame overlay (0 none, 1 match, 2 active) —
// pre-stamped once per drawGrid so this isn't O(matches) per cell.
static void cellColors(const Grid& grid, int x, int y, uint8_t findHit,
                       Color& fg, Color& bg) {
    const Cell& cell = grid.at(x, y);
    fg = cell.fg;
    bg = cell.bg;
    if (cell.flags & kFlagInverse) std::swap(fg, bg);

    // Selection highlight (stamped per cell from the terminal's selection).
    if (cell.flags & kFlagSelected) bg = Color{ 50, 78, 124 };
    else if (findHit == 2) bg = Color{ 190, 145, 40 };   // active match
    else if (findHit == 1) bg = Color{ 95, 80, 30 };     // other matches
}

void D2DRenderer::drawGrid(const Grid& grid, float originX, float originY) {
    if (!d2dContext_ || !brush_) return;

    // Stamp find matches into a per-cell overlay once (searching a common
    // character in a maximized window used to rescan the whole match list for
    // every cell, twice per frame).
    findOverlay_.assign(static_cast<size_t>(grid.cols) * grid.rows, 0);
    for (size_t i = 0; i < grid.findMatches.size(); ++i) {
        const Grid::FindSpan& m = grid.findMatches[i];
        if (m.y < 0 || m.y >= grid.rows) continue;
        const uint8_t v = (static_cast<int>(i) == grid.findCurrent) ? 2 : 1;
        for (int x = m.x; x < m.x + m.len && x < grid.cols; ++x)
            if (x >= 0) findOverlay_[static_cast<size_t>(m.y) * grid.cols + x] = v;
    }
    const auto findHitAt = [&](int x, int y) -> uint8_t {
        return findOverlay_[static_cast<size_t>(y) * grid.cols + x];
    };

    const D2D1_RECT_F clip = D2D1::RectF(
        originX, originY, originX + grid.cols * cellW_,
        originY + grid.rows * cellH_);
    d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    // Terminal background (cells matching it are skipped below).
    brush_->SetColor(toColorF(termBg_));
    d2dContext_->FillRectangle(clip, brush_.Get());

    // Pass 1: cell backgrounds. Separate from the glyph pass so a wide (CJK)
    // glyph spilling into its spacer-tail cell is never overpainted by that
    // tail's background fill.
    for (int y = 0; y < grid.rows; ++y) {
        for (int x = 0; x < grid.cols; ++x) {
            Color fg, bg;
            cellColors(grid, x, y, findHitAt(x, y), fg, bg);
            if (bg.r == termBg_.r && bg.g == termBg_.g && bg.b == termBg_.b)
                continue;  // already painted by the clear above
            const float px = originX + x * cellW_;
            const float py = originY + y * cellH_;
            brush_->SetColor(toColorF(bg));
            d2dContext_->FillRectangle(
                D2D1::RectF(px, py, px + cellW_, py + cellH_), brush_.Get());
        }
    }

    // Pass 2: glyphs + decorations. Glyphs come from the atlas (rasterized
    // once, tinted per cell via FillOpacityMask — which requires the aliased
    // antialias mode); DrawText is the fallback when the atlas is unavailable.
    const D2D1_ANTIALIAS_MODE prevAA = d2dContext_->GetAntialiasMode();
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int y = 0; y < grid.rows; ++y) {
        for (int x = 0; x < grid.cols; ++x) {
            const Cell& cell = grid.at(x, y);
            if (cell.flags & kFlagWideTail) continue;  // drawn by its head cell
            const bool hasGlyph = !cell.ch.empty() && cell.ch != L" ";
            const uint32_t deco =
                cell.flags & (kFlagUnderline | kFlagStrikethrough);
            if (!hasGlyph && !deco) continue;

            Color fg, bg;
            cellColors(grid, x, y, findHitAt(x, y), fg, bg);
            // Faint (SGR 2): draw at half intensity toward the background.
            if (cell.flags & kFlagFaint)
                fg = Color{ static_cast<uint8_t>((fg.r + bg.r) / 2),
                            static_cast<uint8_t>((fg.g + bg.g) / 2),
                            static_cast<uint8_t>((fg.b + bg.b) / 2) };

            const float px = originX + x * cellW_;
            const float py = originY + y * cellH_;
            const float w = (cell.flags & kFlagWide) ? cellW_ * 2.0f : cellW_;

            if (hasGlyph && !(cell.flags & kFlagInvisible)) {
                brush_->SetColor(toColorF(fg));
                const D2D1_RECT_F dst = D2D1::RectF(px, py, px + w, py + cellH_);
                D2D1_RECT_F srcRect{};
                // Color glyphs (emoji) can't go through the atlas: the mask
                // path tints a monochrome silhouette with the fg brush. Draw
                // them directly with color-font support instead.
                if (!isColorGlyph(cell.ch) &&
                    atlasSlot(cell.ch, cell.flags, srcRect)) {
                    d2dContext_->FillOpacityMask(
                        atlasBitmap_.Get(), brush_.Get(),
                        D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL, &dst, &srcRect);
                } else {
                    d2dContext_->DrawText(
                        cell.ch.c_str(), static_cast<UINT32>(cell.ch.size()),
                        cellFormat(cell.flags), dst, brush_.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP |
                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
                }
            }
            if (deco) {
                brush_->SetColor(toColorF(fg));
                if (cell.flags & kFlagUnderline) {
                    const float uy = py + cellH_ - 1.0f;
                    d2dContext_->DrawLine(D2D1::Point2F(px, uy),
                                          D2D1::Point2F(px + w, uy),
                                          brush_.Get(), 1.0f);
                }
                if (cell.flags & kFlagStrikethrough) {
                    const float sy = py + cellH_ * 0.5f;
                    d2dContext_->DrawLine(D2D1::Point2F(px, sy),
                                          D2D1::Point2F(px + w, sy),
                                          brush_.Get(), 1.0f);
                }
            }
        }
    }
    d2dContext_->SetAntialiasMode(prevAA);

    drawCursor(grid, originX, originY);

    d2dContext_->PopAxisAlignedClip();
}

void D2DRenderer::drawCursor(const Grid& grid, float originX, float originY) {
    if (!grid.cursorVisible || grid.cursorX >= grid.cols ||
        grid.cursorY >= grid.rows)
        return;

    // Unfocused panes always show a hollow block (the universal terminal cue).
    CursorShape shape = grid.focused ? grid.cursorShape : CursorShape::HollowBlock;

    // Blink only while focused; the idle render tick (~100ms) keeps the phase
    // fresh. Hollow cursors don't blink.
    if (grid.focused && grid.cursorBlink && shape != CursorShape::HollowBlock &&
        (GetTickCount64() / kCursorBlinkMs) % 2 != 0)
        return;

    const float px = originX + grid.cursorX * cellW_;
    const float py = originY + grid.cursorY * cellH_;
    const bool wide =
        (grid.at(grid.cursorX, grid.cursorY).flags & kFlagWide) != 0;
    const float w = wide ? cellW_ * 2.0f : cellW_;
    const Color c = grid.cursorColorSet ? grid.cursorColor : Color{ 204, 204, 204 };

    switch (shape) {
    case CursorShape::Block:
        // Translucent so the glyph underneath stays legible without a redraw.
        brush_->SetColor(D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f,
                                      0.55f));
        d2dContext_->FillRectangle(D2D1::RectF(px, py, px + w, py + cellH_),
                                   brush_.Get());
        break;
    case CursorShape::Bar: {
        brush_->SetColor(toColorF(c));
        const float bw = cellW_ * 0.14f < 1.5f ? 1.5f : cellW_ * 0.14f;
        d2dContext_->FillRectangle(D2D1::RectF(px, py, px + bw, py + cellH_),
                                   brush_.Get());
        break;
    }
    case CursorShape::Underline: {
        brush_->SetColor(toColorF(c));
        const float uh = cellH_ * 0.12f < 2.0f ? 2.0f : cellH_ * 0.12f;
        d2dContext_->FillRectangle(
            D2D1::RectF(px, py + cellH_ - uh, px + w, py + cellH_), brush_.Get());
        break;
    }
    case CursorShape::HollowBlock:
        brush_->SetColor(toColorF(c));
        d2dContext_->DrawRectangle(
            D2D1::RectF(px + 0.5f, py + 0.5f, px + w - 0.5f, py + cellH_ - 0.5f),
            brush_.Get(), 1.0f);
        break;
    }
}

} // namespace liney
