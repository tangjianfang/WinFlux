#include <flux/D2DRenderer.hpp>

#include <cmath>

namespace flux {

namespace {

D2D1_COLOR_F to_d2d(Color c) noexcept {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 1.0f);
}

D2D1_RECT_F to_rect(Rect r) noexcept {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}

unsigned color_key(Color c) noexcept {
    return (static_cast<unsigned>(c.r) << 16u) | (static_cast<unsigned>(c.g) << 8u) | c.b;
}

unsigned format_key(float size_pt, bool bold) noexcept {
    return (static_cast<unsigned>(std::lround(size_pt * 10.0f)) << 1u) | (bold ? 1u : 0u);
}

float pt_to_dip(float pt) noexcept { return pt * 96.0f / 72.0f; }

} // namespace

D2DRenderer::~D2DRenderer() {
    // ComPtr members release in declaration order; explicit ordering keeps
    // the render target destroyed before its factory.
    rt_.Reset();
    brushes_.clear();
    formats_.clear();
    dwrite_.Reset();
    factory_.Reset();
}

bool D2DRenderer::create(HWND hwnd) noexcept {
    hwnd_ = hwnd;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) return false;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(hr)) return false;
    recreate_target();
    return rt_ != nullptr;
}

void D2DRenderer::recreate_target() noexcept {
    brushes_.clear();
    rt_.Reset();
    if (factory_ == nullptr || hwnd_ == nullptr) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_px_ = static_cast<unsigned>(rc.right - rc.left);
    height_px_ = static_cast<unsigned>(rc.bottom - rc.top);

    const UINT dpi_x = GetDpiForWindow(hwnd_);
    dpi_ = dpi_x != 0 ? static_cast<float>(dpi_x) : 96.0f;

    const D2D1_SIZE_U size = D2D1::SizeU(width_px_, height_px_);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES htp = D2D1::HwndRenderTargetProperties(hwnd_, size);
    factory_->CreateHwndRenderTarget(rtp, htp, rt_.GetAddressOf());
}

void D2DRenderer::resize(unsigned width_px, unsigned height_px) noexcept {
    if (rt_ == nullptr) return;
    width_px_ = width_px;
    height_px_ = height_px;
    const HRESULT hr = rt_->Resize(D2D1::SizeU(width_px, height_px));
    if (FAILED(hr)) {
        // Device-lost path: rebuild the target and all device resources.
        recreate_target();
    }
}

void D2DRenderer::set_dpi(float dpi) noexcept {
    dpi_ = dpi > 0.0f ? dpi : 96.0f;
}

void D2DRenderer::begin(Color clear) noexcept {
    if (rt_ == nullptr) return;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(dpi_ / 96.0f, dpi_ / 96.0f));
    rt_->Clear(to_d2d(clear));
    clip_depth_ = 0;
}

void D2DRenderer::end() noexcept {
    if (rt_ == nullptr) return;
    while (clip_depth_ > 0) {
        rt_->PopAxisAlignedClip();
        --clip_depth_;
    }
    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        recreate_target();
    }
}

ID2D1SolidColorBrush* D2DRenderer::brush(Color c) noexcept {
    const unsigned key = color_key(c);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second.Get();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    if (rt_ == nullptr || FAILED(rt_->CreateSolidColorBrush(to_d2d(c), b.GetAddressOf()))) {
        return nullptr;
    }
    auto* raw = b.Get();
    brushes_.emplace(key, std::move(b));
    return raw;
}

IDWriteTextFormat* D2DRenderer::format(float size_pt, bool bold) noexcept {
    const unsigned key = format_key(size_pt, bold);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    if (dwrite_ == nullptr) return nullptr;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    const HRESULT hr = dwrite_->CreateTextFormat(
        L"Segoe UI", nullptr,
        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        pt_to_dip(size_pt), L"en-us", fmt.GetAddressOf());
    if (FAILED(hr)) return nullptr;
    auto* raw = fmt.Get();
    formats_.emplace(key, std::move(fmt));
    return raw;
}

void D2DRenderer::fill_rect(Rect r, Color c) noexcept {
    if (rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        rt_->FillRectangle(to_rect(r), b);
    }
}

void D2DRenderer::fill_round_rect(Rect r, float radius, Color c) noexcept {
    if (rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b);
    }
}

void D2DRenderer::draw_round_rect(Rect r, float radius, Color c, float stroke_width) noexcept {
    if (rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        rt_->DrawRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b, stroke_width);
    }
}

void D2DRenderer::draw_line(float x1, float y1, float x2, float y2, Color c, float stroke_width) noexcept {
    if (rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, stroke_width);
    }
}

void D2DRenderer::draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                            bool bold, Align halign) noexcept {
    if (rt_ == nullptr || text.empty()) return;
    ID2D1SolidColorBrush* b = brush(c);
    IDWriteTextFormat* fmt = format(size_pt, bold);
    if (b == nullptr || fmt == nullptr) return;

    fmt->SetTextAlignment(halign == Align::center ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : halign == Align::end    ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                  : DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, to_rect(r), b,
                   D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

std::pair<float, float> D2DRenderer::measure_text(const std::wstring& text,
                                                  float size_pt, bool bold) noexcept {
    if (text.empty() || dwrite_ == nullptr) return {0.0f, 0.0f};
    IDWriteTextFormat* fmt = format(size_pt, bold);
    if (fmt == nullptr) return {0.0f, 0.0f};

    // Unbounded width layout: the measured extent is the natural text size.
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    const HRESULT hr = dwrite_->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), fmt,
        100000.0f, 100000.0f, layout.GetAddressOf());
    if (FAILED(hr)) return {0.0f, 0.0f};
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return {std::ceil(m.width), std::ceil(m.height)};
}

void D2DRenderer::push_clip(Rect r) noexcept {
    if (rt_ == nullptr) return;
    rt_->PushAxisAlignedClip(to_rect(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clip_depth_;
}

void D2DRenderer::pop_clip() noexcept {
    if (rt_ == nullptr || clip_depth_ <= 0) return;
    rt_->PopAxisAlignedClip();
    --clip_depth_;
}

} // namespace flux
