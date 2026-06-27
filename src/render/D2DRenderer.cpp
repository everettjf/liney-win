#include "render/D2DRenderer.h"

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

    // Bold variant for SGR-bold cells (best-effort; ignore failure).
    if (SUCCEEDED(dwriteFactory_->CreateTextFormat(
            family, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, fontSize_, L"en-us",
            textFormatBold_.GetAddressOf()))) {
        textFormatBold_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        textFormatBold_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

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
    swapChain_->ResizeBuffers(0, widthPx, heightPx, DXGI_FORMAT_UNKNOWN, 0);
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
    if (!d2dContext_ || !targetBitmap_ || !brush_) return;
    d2dContext_->BeginDraw();
    d2dContext_->Clear(toColorF(workspaceBg_));  // workspace bg (gutters/margins)
}

void D2DRenderer::endFrame() {
    if (!d2dContext_ || !swapChain_) return;
    d2dContext_->EndDraw();
    swapChain_->Present(1, 0);
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
        line(bx + s * 0.22f, by + s * 0.22f, bx + s * 0.78f, by + s * 0.78f);  // \
        line(bx + s * 0.78f, by + s * 0.22f, bx + s * 0.22f, by + s * 0.78f);  // /
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
    case IconKind::Menu:  // hamburger: 3 evenly-spaced horizontal bars
        for (int i = 0; i < 3; ++i) {
            float ly = by + s * (0.22f + 0.28f * i);
            fillR(bx, ly - t * 0.5f, bx + s, ly + t * 0.5f);
        }
        break;
    }
}

void D2DRenderer::drawGrid(const Grid& grid, float originX, float originY) {
    if (!d2dContext_ || !brush_) return;

    const D2D1_RECT_F clip = D2D1::RectF(
        originX, originY, originX + grid.cols * cellW_,
        originY + grid.rows * cellH_);
    d2dContext_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    // Terminal background (cells matching it are skipped below).
    brush_->SetColor(toColorF(termBg_));
    d2dContext_->FillRectangle(clip, brush_.Get());

    for (int y = 0; y < grid.rows; ++y) {
        for (int x = 0; x < grid.cols; ++x) {
            const Cell& cell = grid.at(x, y);
            const float px = originX + x * cellW_;
            const float py = originY + y * cellH_;
            const D2D1_RECT_F rect = D2D1::RectF(px, py, px + cellW_, py + cellH_);

            // Inverse video swaps foreground and background.
            Color fg = cell.fg, bg = cell.bg;
            if (cell.flags & kFlagInverse) std::swap(fg, bg);

            // Selection highlight (row-major inclusive range).
            bool selected = false;
            if (grid.hasSelection) {
                const bool afterStart =
                    y > grid.selStartY ||
                    (y == grid.selStartY && x >= grid.selStartX);
                const bool beforeEnd =
                    y < grid.selEndY || (y == grid.selEndY && x <= grid.selEndX);
                selected = afterStart && beforeEnd;
            }
            // Find-on-screen highlight: 0 = none, 1 = match, 2 = active match.
            int findHit = 0;
            if (!selected && !grid.findMatches.empty()) {
                for (size_t i = 0; i < grid.findMatches.size(); ++i) {
                    const Grid::FindSpan& m = grid.findMatches[i];
                    if (y == m.y && x >= m.x && x < m.x + m.len) {
                        findHit = (static_cast<int>(i) == grid.findCurrent) ? 2 : 1;
                        break;
                    }
                }
            }
            if (selected) bg = Color{ 50, 78, 124 };
            else if (findHit == 2) bg = Color{ 190, 145, 40 };   // active match
            else if (findHit == 1) bg = Color{ 95, 80, 30 };     // other matches

            if (bg.r || bg.g || bg.b) {
                brush_->SetColor(toColorF(bg));
                d2dContext_->FillRectangle(rect, brush_.Get());
            }
            if (!cell.ch.empty() && cell.ch != L" ") {
                IDWriteTextFormat* fmt =
                    (cell.flags & kFlagBold) && textFormatBold_
                        ? textFormatBold_.Get()
                        : textFormat_.Get();
                brush_->SetColor(toColorF(fg));
                d2dContext_->DrawText(
                    cell.ch.c_str(), static_cast<UINT32>(cell.ch.size()), fmt,
                    rect, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (cell.flags & kFlagUnderline) {
                brush_->SetColor(toColorF(fg));
                const float uy = py + cellH_ - 1.0f;
                d2dContext_->DrawLine(D2D1::Point2F(px, uy),
                                      D2D1::Point2F(px + cellW_, uy),
                                      brush_.Get(), 1.0f);
            }
        }
    }

    if (grid.cursorVisible && grid.cursorX < grid.cols &&
        grid.cursorY < grid.rows) {
        const float px = originX + grid.cursorX * cellW_;
        const float py = originY + grid.cursorY * cellH_;
        brush_->SetColor(D2D1::ColorF(0.80f, 0.80f, 0.80f, 0.55f));
        d2dContext_->FillRectangle(
            D2D1::RectF(px, py, px + cellW_, py + cellH_), brush_.Get());
    }

    d2dContext_->PopAxisAlignedClip();
}

} // namespace liney
