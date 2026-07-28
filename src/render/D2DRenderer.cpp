#include "render/D2DRenderer.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <fstream>
#include <utility>
#include <d3dcompiler.h>

namespace liney {

D2DRenderer::~D2DRenderer() {
    wchar_t metricsPath[32768]{};
    const DWORD n = GetEnvironmentVariableW(
        L"LINEY_FRAME_METRICS", metricsPath,
        static_cast<DWORD>(_countof(metricsPath)));
    if (!n || n >= _countof(metricsPath) || frameTimesMs_.empty()) return;
    std::sort(frameTimesMs_.begin(), frameTimesMs_.end());
    const auto percentile = [&](double p) {
        return frameTimesMs_[std::min(
            frameTimesMs_.size() - 1,
            static_cast<size_t>(std::ceil(frameTimesMs_.size() * p) - 1))];
    };
    std::ofstream out(metricsPath, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "{\"frames\":" << frameTimesMs_.size()
        << ",\"p50Ms\":" << percentile(0.50)
        << ",\"p95Ms\":" << percentile(0.95)
        << ",\"p99Ms\":" << percentile(0.99)
        << ",\"maxMs\":" << frameTimesMs_.back() << "}";
}

static D2D1_COLOR_F toColorF(const Color& c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}

bool D2DRenderer::initialize(void* hwnd) {
    hwnd_ = static_cast<HWND>(hwnd);
    wchar_t delay[16]{};
    const DWORD count = GetEnvironmentVariableW(
        L"LINEY_CAPTURE_DELAY_MS", delay,
        static_cast<DWORD>(_countof(delay)));
    if (count > 0 && count < _countof(delay))
        captureReadyAt_ = GetTickCount64() + wcstoul(delay, nullptr, 10);
    return createDeviceResources();
}

bool D2DRenderer::createDeviceResources() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel{};
    wchar_t forceWarp[8]{};
    const bool warp = GetEnvironmentVariableW(
        L"LINEY_FORCE_WARP", forceWarp,
        static_cast<DWORD>(_countof(forceWarp))) > 0;
    HRESULT hr = D3D11CreateDevice(
        nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags,
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
    uiTextFormat_.Reset();
    uiTextFormatBold_.Reset();
    ligatureTypography_.Reset();

    // Font (or size) changed: every cached glyph is stale. The atlas is
    // recreated lazily at the new cell size.
    glyphCache_.clear();
    atlasBrush_.Reset();
    atlasTarget_.Reset();
    atlasContext_.Reset();
    atlasTarget_.Reset();
    atlasTexture_.Reset();
    atlasSrv_.Reset();
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

    // DirectWrite typography is applied only to explicitly enabled
    // programming-operator runs. The ordinary atlas path remains one glyph
    // per cell, which is the safest default for TUIs and cursor positioning.
    if (SUCCEEDED(dwriteFactory_->CreateTypography(
            ligatureTypography_.GetAddressOf()))) {
        DWRITE_FONT_FEATURE feature{
            DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES, 1
        };
        ligatureTypography_->AddFontFeature(feature);
        feature.nameTag = DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES;
        ligatureTypography_->AddFontFeature(feature);
    }

    // Keep application chrome visually native even when the terminal uses a
    // distinctive programming font. Segoe UI Variable ships on Windows 11;
    // classic Segoe UI is the Windows 10 fallback.
    const float uiFontSize = 13.0f * uiScale_;
    const wchar_t* uiFamily = L"Segoe UI Variable Text";
    hr = dwriteFactory_->CreateTextFormat(
        uiFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, uiFontSize, L"en-us",
        uiTextFormat_.GetAddressOf());
    if (FAILED(hr)) {
        uiFamily = L"Segoe UI";
        hr = dwriteFactory_->CreateTextFormat(
            uiFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, uiFontSize,
            L"en-us", uiTextFormat_.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        uiTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        uiTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        uiTextFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (SUCCEEDED(dwriteFactory_->CreateTextFormat(
                uiFamily, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, uiFontSize,
                L"en-us", uiTextFormatBold_.GetAddressOf()))) {
            uiTextFormatBold_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            uiTextFormatBold_->SetParagraphAlignment(
                DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            uiTextFormatBold_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
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

void D2DRenderer::setUiScale(float scale) {
    const float next = std::clamp(scale, 0.75f, 4.0f);
    if (std::fabs(next - uiScale_) < 0.001f) return;
    uiScale_ = next;
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

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(
        0, __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    if (FAILED(hr) ||
        FAILED(d3dDevice_->CreateRenderTargetView(
            backBuffer.Get(), nullptr, d3dTargetView_.ReleaseAndGetAddressOf())))
        return false;

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
    d3dTargetView_.Reset();
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
    QueryPerformanceCounter(&frameStarted_);
    if (deviceLost_ && !recreateDevice()) return;
    if (atlasNeedsReset_ && atlasContext_) {
        // Deferred from atlasSlot: safe to wipe between frames.
        glyphCache_.clear();
        atlasX_ = atlasY_ = 0.0f;
        atlasContext_->BeginDraw();
        atlasContext_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));
        if (FAILED(atlasContext_->EndDraw())) atlasBroken_ = true;
        atlasNeedsReset_ = false;
    }
    if (!d2dContext_ || !targetBitmap_ || !brush_) return;
    pendingGlyphVertices_.clear();
    d2dContext_->BeginDraw();
    frameOpen_ = true;
    d2dContext_->Clear(toColorF(workspaceBg_));  // workspace bg (gutters/margins)
}

void D2DRenderer::endFrame() {
    if (!d2dContext_ || !swapChain_ || !frameOpen_) return;
    frameOpen_ = false;
    const HRESULT hrDraw = d2dContext_->EndDraw();
    if (SUCCEEDED(hrDraw) && !pendingGlyphVertices_.empty() &&
        !drawGlyphBatch(pendingGlyphVertices_))
        atlasBroken_ = true;
    wchar_t capturePath[32768]{};
    wchar_t headless[8]{};
    const bool isHeadless =
        GetEnvironmentVariableW(L"LINEY_HEADLESS", headless,
                                static_cast<DWORD>(_countof(headless))) > 0;
    if (!capturedFrame_ && isHeadless && SUCCEEDED(hrDraw) &&
        GetTickCount64() >= captureReadyAt_) {
        const DWORD captureLength = GetEnvironmentVariableW(
            L"LINEY_CAPTURE_PNG", capturePath,
            static_cast<DWORD>(_countof(capturePath)));
        if (captureLength > 0 && captureLength < _countof(capturePath))
            capturedFrame_ = captureBackBufferPng(capturePath);
    }
    const HRESULT hrPresent = swapChain_->Present(1, 0);
    LARGE_INTEGER ended{}, frequency{};
    QueryPerformanceCounter(&ended);
    QueryPerformanceFrequency(&frequency);
    if (frameStarted_.QuadPart && frequency.QuadPart) {
        const double ms =
            (ended.QuadPart - frameStarted_.QuadPart) * 1000.0 /
            frequency.QuadPart;
        if (frameTimesMs_.size() >= 4096)
            frameTimesMs_.erase(frameTimesMs_.begin());
        frameTimesMs_.push_back(ms);
    }
    wchar_t simulate[8]{};
    if (!simulatedDeviceLoss_ && GetEnvironmentVariableW(
            L"LINEY_SIMULATE_DEVICE_LOSS", simulate,
            static_cast<DWORD>(_countof(simulate))) > 0) {
        simulatedDeviceLoss_ = true;
        deviceLost_ = true;
    }
    // A GPU driver update / TDR reset / RDP GPU switch removes the device;
    // without recovery every later Present fails silently and the window
    // freezes forever. Flag it and rebuild everything next frame.
    if (hrDraw == D2DERR_RECREATE_TARGET ||
        hrPresent == DXGI_ERROR_DEVICE_REMOVED ||
        hrPresent == DXGI_ERROR_DEVICE_RESET) {
        deviceLost_ = true;
    }
}

bool D2DRenderer::captureBackBufferPng(const std::wstring& path) {
    if (path.empty() || !swapChain_ || !d3dDevice_ || !d3dContext_ ||
        !wicFactory_)
        return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(
            0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(backBuffer.GetAddressOf()))))
        return false;

    D3D11_TEXTURE2D_DESC desc{};
    backBuffer->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0 ||
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(d3dDevice_->CreateTexture2D(&desc, nullptr,
                                           staging.GetAddressOf())))
        return false;
    d3dContext_->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d3dContext_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return false;

    bool ok = false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    IPropertyBag2* optionsRaw = nullptr;
    DeleteFileW(path.c_str());
    if (SUCCEEDED(wicFactory_->CreateStream(stream.GetAddressOf())) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wicFactory_->CreateEncoder(
            GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), &optionsRaw))) {
        ComPtr<IPropertyBag2> options;
        options.Attach(optionsRaw);
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->Initialize(options.Get())) &&
            SUCCEEDED(frame->SetSize(desc.Width, desc.Height)) &&
            SUCCEEDED(frame->SetPixelFormat(&format)) &&
            IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
            mapped.RowPitch <= UINT_MAX / desc.Height &&
            SUCCEEDED(frame->WritePixels(
                desc.Height, mapped.RowPitch, mapped.RowPitch * desc.Height,
                static_cast<BYTE*>(mapped.pData))) &&
            SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
            ok = true;
        }
    }
    d3dContext_->Unmap(staging.Get(), 0);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
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
    atlasTarget_.Reset();
    atlasContext_.Reset();
    atlasSrv_.Reset();
    atlasTexture_.Reset();
    glyphVertexShader_.Reset();
    glyphPixelShader_.Reset();
    glyphInputLayout_.Reset();
    glyphVertexBuffer_.Reset();
    glyphConstants_.Reset();
    glyphSampler_.Reset();
    glyphBlend_.Reset();
    glyphVertexCapacity_ = 0;
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

void D2DRenderer::fillRoundedRect(float x, float y, float w, float h,
                                  float radius, const Color& c) {
    if (!d2dContext_ || !brush_ || w <= 0.0f || h <= 0.0f) return;
    brush_->SetColor(toColorF(c));
    radius = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    d2dContext_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius),
        brush_.Get());
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

void D2DRenderer::strokeRoundedRect(float x, float y, float w, float h,
                                    float radius, const Color& c,
                                    float thickness) {
    if (!d2dContext_ || !brush_ || w <= 0.0f || h <= 0.0f) return;
    brush_->SetColor(toColorF(c));
    const float inset = thickness * 0.5f;
    radius = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    d2dContext_->DrawRoundedRectangle(
        D2D1::RoundedRect(
            D2D1::RectF(x + inset, y + inset, x + w - inset, y + h - inset),
            radius, radius),
        brush_.Get(), thickness);
}

void D2DRenderer::drawText(const std::wstring& text, float x, float y,
                           float maxW, float rowH, const Color& c, bool bold) {
    if (!d2dContext_ || !brush_ || text.empty()) return;
    IDWriteTextFormat* fmt =
        bold && uiTextFormatBold_ ? uiTextFormatBold_.Get()
                                 : (uiTextFormat_ ? uiTextFormat_.Get()
                                                  : textFormat_.Get());
    brush_->SetColor(toColorF(c));
    d2dContext_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt,
                          D2D1::RectF(x, y, x + maxW, y + rowH), brush_.Get(),
                           D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void D2DRenderer::drawTextCentered(const std::wstring& text, float x, float y,
                                   float w, float h, const Color& c, bool bold) {
    if (!d2dContext_ || !brush_ || text.empty() || w <= 0.0f || h <= 0.0f)
        return;
    IDWriteTextFormat* fmt =
        bold && uiTextFormatBold_ ? uiTextFormatBold_.Get()
                                 : (uiTextFormat_ ? uiTextFormat_.Get()
                                                  : textFormat_.Get());
    if (!fmt) return;

    const DWRITE_TEXT_ALIGNMENT oldTextAlignment = fmt->GetTextAlignment();
    const DWRITE_PARAGRAPH_ALIGNMENT oldParagraphAlignment =
        fmt->GetParagraphAlignment();
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    brush_->SetColor(toColorF(c));
    d2dContext_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), fmt,
                          D2D1::RectF(x, y, x + w, y + h), brush_.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);

    fmt->SetTextAlignment(oldTextAlignment);
    fmt->SetParagraphAlignment(oldParagraphAlignment);
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
    if (atlasContext_ && atlasSrv_) return true;
    if (!d2dDevice_ || !d3dDevice_) return false;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(kAtlasSize);
    desc.Height = static_cast<UINT>(kAtlasSize);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d3dDevice_->CreateTexture2D(
            &desc, nullptr, atlasTexture_.ReleaseAndGetAddressOf())) ||
        FAILED(d3dDevice_->CreateShaderResourceView(
            atlasTexture_.Get(), nullptr, atlasSrv_.ReleaseAndGetAddressOf())) ||
        FAILED(d2dDevice_->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
            atlasContext_.ReleaseAndGetAddressOf()))) {
        atlasTexture_.Reset();
        atlasSrv_.Reset();
        atlasContext_.Reset();
        atlasBroken_ = true;
        return false;
    }
    ComPtr<IDXGISurface> surface;
    if (FAILED(atlasTexture_.As(&surface))) {
        atlasBroken_ = true;
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(atlasContext_->CreateBitmapFromDxgiSurface(
            surface.Get(), &props, atlasTarget_.ReleaseAndGetAddressOf())) ||
        FAILED(atlasContext_->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White),
            atlasBrush_.ReleaseAndGetAddressOf()))) {
        atlasBrush_.Reset();
        atlasTarget_.Reset();
        atlasContext_.Reset();
        atlasSrv_.Reset();
        atlasTexture_.Reset();
        atlasBroken_ = true;
        return false;
    }
    atlasContext_->SetTarget(atlasTarget_.Get());
    atlasContext_->BeginDraw();
    atlasContext_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));
    if (FAILED(atlasContext_->EndDraw())) {
        atlasBroken_ = true;
        return false;
    }
    atlasX_ = atlasY_ = 0.0f;
    glyphCache_.clear();
    SetPropW(hwnd_, L"Liney.D3DGlyphAtlasReady",
             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    return ensureGlyphShader();
}

bool D2DRenderer::ensureGlyphShader() {
    if (glyphVertexShader_ && glyphPixelShader_ && glyphInputLayout_ &&
        glyphConstants_ && glyphSampler_ && glyphBlend_)
        return true;
    if (!d3dDevice_) return false;

    static const char shader[] = R"HLSL(
cbuffer Viewport : register(b0) { float2 viewport; float2 unused; };
struct VSIn {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct VSOut {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
VSOut vsMain(VSIn input) {
    VSOut output;
    output.position = float4(
        input.position.x * 2.0 / viewport.x - 1.0,
        1.0 - input.position.y * 2.0 / viewport.y, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
Texture2D atlasTexture : register(t0);
SamplerState atlasSampler : register(s0);
float4 psMain(VSOut input) : SV_TARGET {
    float coverage = atlasTexture.Sample(atlasSampler, input.uv).a;
    return float4(input.color.rgb * coverage, input.color.a * coverage);
}
)HLSL";
    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif
    if (FAILED(D3DCompile(shader, sizeof(shader) - 1, "LineyGlyphShader",
                          nullptr, nullptr, "vsMain", "vs_4_0", flags, 0,
                          vsBlob.GetAddressOf(), errors.GetAddressOf())) ||
        FAILED(D3DCompile(shader, sizeof(shader) - 1, "LineyGlyphShader",
                          nullptr, nullptr, "psMain", "ps_4_0", flags, 0,
                          psBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf())))
        return false;
    if (FAILED(d3dDevice_->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
            glyphVertexShader_.ReleaseAndGetAddressOf())) ||
        FAILED(d3dDevice_->CreatePixelShader(
            psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
            glyphPixelShader_.ReleaseAndGetAddressOf())))
        return false;
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(GlyphVertex, x)),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         static_cast<UINT>(offsetof(GlyphVertex, u)),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         static_cast<UINT>(offsetof(GlyphVertex, r)),
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(d3dDevice_->CreateInputLayout(
            layout, static_cast<UINT>(_countof(layout)),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            glyphInputLayout_.ReleaseAndGetAddressOf())))
        return false;
    D3D11_BUFFER_DESC constants{};
    constants.ByteWidth = 16;
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(d3dDevice_->CreateBuffer(
            &constants, nullptr, glyphConstants_.ReleaseAndGetAddressOf())))
        return false;
    D3D11_SAMPLER_DESC sampler{};
    // Atlas slots and destination cells are pixel-aligned. Point sampling
    // preserves ClearType-like edge contrast and prevents adjacent slots from
    // bleeding into one another during dense output.
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(d3dDevice_->CreateSamplerState(
            &sampler, glyphSampler_.ReleaseAndGetAddressOf())))
        return false;
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    const bool ready = SUCCEEDED(d3dDevice_->CreateBlendState(
        &blend, glyphBlend_.ReleaseAndGetAddressOf()));
    if (ready)
        SetPropW(hwnd_, L"Liney.D3DGlyphShaderReady",
                 reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    return ready;
}

bool D2DRenderer::drawGlyphBatch(const std::vector<GlyphVertex>& vertices) {
    if (vertices.empty()) return true;
    if (!d3dContext_ || !d3dTargetView_ || !atlasSrv_ ||
        !ensureGlyphShader())
        return false;
    SetPropW(hwnd_, L"Liney.D3DGlyphD2DCommitted",
             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));

    const size_t needed = vertices.size() * sizeof(GlyphVertex);
    if (!glyphVertexBuffer_ || glyphVertexCapacity_ < needed) {
        D3D11_BUFFER_DESC desc{};
        glyphVertexCapacity_ = 4096;
        while (glyphVertexCapacity_ < needed) glyphVertexCapacity_ *= 2;
        desc.ByteWidth = static_cast<UINT>(glyphVertexCapacity_);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(d3dDevice_->CreateBuffer(
                &desc, nullptr, glyphVertexBuffer_.ReleaseAndGetAddressOf())))
            return false;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d3dContext_->Map(glyphVertexBuffer_.Get(), 0,
                                D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    std::memcpy(mapped.pData, vertices.data(), needed);
    d3dContext_->Unmap(glyphVertexBuffer_.Get(), 0);
    SetPropW(hwnd_, L"Liney.D3DGlyphVertexUploaded",
             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    if (FAILED(d3dContext_->Map(glyphConstants_.Get(), 0,
                                D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return false;
    float* viewport = static_cast<float*>(mapped.pData);
    viewport[0] = static_cast<float>(widthPx_);
    viewport[1] = static_cast<float>(heightPx_);
    viewport[2] = viewport[3] = 0.0f;
    d3dContext_->Unmap(glyphConstants_.Get(), 0);

    const UINT stride = sizeof(GlyphVertex);
    const UINT offset = 0;
    ID3D11Buffer* vb = glyphVertexBuffer_.Get();
    ID3D11Buffer* cb = glyphConstants_.Get();
    ID3D11ShaderResourceView* srv = atlasSrv_.Get();
    ID3D11SamplerState* sampler = glyphSampler_.Get();
    d3dContext_->OMSetRenderTargets(1, d3dTargetView_.GetAddressOf(), nullptr);
    const float blendFactor[4] = {};
    d3dContext_->OMSetBlendState(glyphBlend_.Get(), blendFactor, 0xffffffff);
    D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(widthPx_),
                      static_cast<float>(heightPx_), 0.0f, 1.0f};
    d3dContext_->RSSetViewports(1, &vp);
    d3dContext_->IASetInputLayout(glyphInputLayout_.Get());
    d3dContext_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    d3dContext_->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3dContext_->VSSetShader(glyphVertexShader_.Get(), nullptr, 0);
    d3dContext_->VSSetConstantBuffers(0, 1, &cb);
    d3dContext_->PSSetShader(glyphPixelShader_.Get(), nullptr, 0);
    d3dContext_->PSSetShaderResources(0, 1, &srv);
    d3dContext_->PSSetSamplers(0, 1, &sampler);
    d3dContext_->Draw(static_cast<UINT>(vertices.size()), 0);
    SetPropW(hwnd_, L"Liney.D3DGlyphShaderActive",
             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    ID3D11ShaderResourceView* nullSrv = nullptr;
    d3dContext_->PSSetShaderResources(0, 1, &nullSrv);

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
    atlasContext_->BeginDraw();
    atlasContext_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
    atlasContext_->Clear(D2D1::ColorF(0, 0, 0, 0.0f));
    atlasContext_->DrawText(ch.c_str(), static_cast<UINT32>(ch.size()),
                            cellFormat(flags), rect, atlasBrush_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    atlasContext_->PopAxisAlignedClip();
    if (FAILED(atlasContext_->EndDraw())) {
        atlasBroken_ = true;
        return false;
    }
    atlasContext_->Flush();
    SetPropW(hwnd_, L"Liney.D3DGlyphSlotReady",
             reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
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
    // once, then sampled and tinted by one D3D11 shader batch); DrawText is the
    // fallback for color fonts and when the shader/atlas is unavailable.
    std::vector<GlyphVertex> glyphVertices;
    glyphVertices.reserve(static_cast<size_t>(grid.cols) * grid.rows * 3);
    std::vector<uint8_t> shaped(static_cast<size_t>(grid.cols) * grid.rows, 0);
    const D2D1_ANTIALIAS_MODE prevAA = d2dContext_->GetAntialiasMode();
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    // Opt-in shaping is deliberately narrow: only ASCII operator sequences
    // known to be used by programming fonts are shaped as a run. CJK, emoji,
    // combining graphemes, spaces and ordinary shell text stay on the exact
    // per-cell path, preserving terminal column geometry and font fallback.
    if (ligatures_ && ligatureTypography_) {
        const auto hasOperatorLigature = [](const std::wstring& s) {
            static const wchar_t* candidates[] = {
                L"->", L"=>", L"!=", L"==", L"<=", L">=", L"::",
                L"&&", L"||", L"/*", L"*/", L"..", L"++", L"--"
            };
            for (const wchar_t* candidate : candidates)
                if (s.find(candidate) != std::wstring::npos) return true;
            return false;
        };
        constexpr uint32_t styleMask =
            kFlagBold | kFlagItalic | kFlagFaint | kFlagInverse |
            kFlagInvisible;
        for (int y = 0; y < grid.rows; ++y) {
            int x = 0;
            while (x < grid.cols) {
                const Cell& first = grid.at(x, y);
                if (first.ch.empty() || first.ch == L" " ||
                    first.ch.size() != 1 || first.ch[0] > 0x7f ||
                    (first.flags & (kFlagWide | kFlagWideTail |
                                    kFlagInvisible))) {
                    ++x;
                    continue;
                }
                Color runFg, runBg;
                cellColors(grid, x, y, findHitAt(x, y), runFg, runBg);
                if (first.flags & kFlagFaint)
                    runFg = Color{
                        static_cast<uint8_t>((runFg.r + runBg.r) / 2),
                        static_cast<uint8_t>((runFg.g + runBg.g) / 2),
                        static_cast<uint8_t>((runFg.b + runBg.b) / 2)
                    };
                const uint32_t runStyle = first.flags & styleMask;
                std::wstring text;
                int end = x;
                while (end < grid.cols) {
                    const Cell& c = grid.at(end, y);
                    if (c.ch.empty() || c.ch == L" " || c.ch.size() != 1 ||
                        c.ch[0] > 0x7f ||
                        (c.flags & (kFlagWide | kFlagWideTail |
                                    kFlagInvisible)) ||
                        (c.flags & styleMask) != runStyle)
                        break;
                    Color fg, bg;
                    cellColors(grid, end, y, findHitAt(end, y), fg, bg);
                    if (c.flags & kFlagFaint)
                        fg = Color{ static_cast<uint8_t>((fg.r + bg.r) / 2),
                                    static_cast<uint8_t>((fg.g + bg.g) / 2),
                                    static_cast<uint8_t>((fg.b + bg.b) / 2) };
                    if (fg.r != runFg.r || fg.g != runFg.g || fg.b != runFg.b)
                        break;
                    text += c.ch;
                    ++end;
                }
                if (end - x >= 2 && hasOperatorLigature(text)) {
                    const float px = originX + x * cellW_;
                    const float py = originY + y * cellH_;
                    const float width = (end - x) * cellW_;
                    ComPtr<IDWriteTextLayout> layout;
                    if (SUCCEEDED(dwriteFactory_->CreateTextLayout(
                            text.c_str(), static_cast<UINT32>(text.size()),
                            cellFormat(first.flags), width, cellH_,
                            layout.GetAddressOf()))) {
                        const DWRITE_TEXT_RANGE range{
                            0, static_cast<UINT32>(text.size())
                        };
                        layout->SetTypography(ligatureTypography_.Get(), range);
                        brush_->SetColor(toColorF(runFg));
                        d2dContext_->DrawTextLayout(
                            D2D1::Point2F(px, py), layout.Get(), brush_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        SetPropW(hwnd_, L"Liney.LigatureRunActive",
                                 reinterpret_cast<HANDLE>(1));
                        for (int sx = x; sx < end; ++sx)
                            shaped[static_cast<size_t>(y) * grid.cols + sx] = 1;
                    }
                }
                x = std::max(end, x + 1);
            }
        }
    }

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

            if (hasGlyph && !(cell.flags & kFlagInvisible) &&
                !shaped[static_cast<size_t>(y) * grid.cols + x]) {
                brush_->SetColor(toColorF(fg));
                const D2D1_RECT_F dst = D2D1::RectF(px, py, px + w, py + cellH_);
                D2D1_RECT_F srcRect{};
                // Color glyphs (emoji) can't go through the atlas: the mask
                // path tints a monochrome silhouette with the fg brush. Draw
                // them directly with color-font support instead.
                if (!isColorGlyph(cell.ch) &&
                    atlasSlot(cell.ch, cell.flags, srcRect)) {
                    const float u0 = srcRect.left / kAtlasSize;
                    const float v0 = srcRect.top / kAtlasSize;
                    const float u1 = srcRect.right / kAtlasSize;
                    const float v1 = srcRect.bottom / kAtlasSize;
                    const float r = fg.r / 255.0f, g = fg.g / 255.0f;
                    const float b = fg.b / 255.0f;
                    const GlyphVertex tl{dst.left, dst.top, u0, v0,
                                         r, g, b, 1.0f};
                    const GlyphVertex tr{dst.right, dst.top, u1, v0,
                                         r, g, b, 1.0f};
                    const GlyphVertex bl{dst.left, dst.bottom, u0, v1,
                                         r, g, b, 1.0f};
                    const GlyphVertex br{dst.right, dst.bottom, u1, v1,
                                         r, g, b, 1.0f};
                    glyphVertices.insert(glyphVertices.end(),
                                         {tl, tr, bl, bl, tr, br});
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
    pendingGlyphVertices_.insert(pendingGlyphVertices_.end(),
                                 glyphVertices.begin(), glyphVertices.end());
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
