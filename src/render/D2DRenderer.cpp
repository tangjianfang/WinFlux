#include <flux/D2DRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace flux {

namespace {

D2D1_COLOR_F to_d2d(Color c) noexcept {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

D2D1_RECT_F to_rect(Rect r) noexcept {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}

unsigned color_key(Color c) noexcept {
    return (static_cast<unsigned>(c.r) << 24u) | (static_cast<unsigned>(c.g) << 16u) |
           (static_cast<unsigned>(c.b) << 8u) | c.a;
}

unsigned format_key(float size_pt, bool bold) noexcept {
    return (static_cast<unsigned>(std::lround(size_pt * 10.0f)) << 1u) | (bold ? 1u : 0u);
}

float pt_to_dip(float pt) noexcept { return pt * 96.0f / 72.0f; }

// ---- Icon geometry helpers (24x24 stroke grid) -------------------------
// Polyline / circle figure builders shared by every icon kind. `filled`
// figures are accumulated into the icon's fill geometry; hollow outlines
// into the stroke geometry.
void fig_poly(ID2D1GeometrySink* s, const D2D1_POINT_2F* pts, UINT n,
              D2D1_FIGURE_END end) noexcept {
    s->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    if (n > 1) s->AddLines(pts + 1, n - 1);
    s->EndFigure(end);
}

void fig_poly_filled(ID2D1GeometrySink* s, const D2D1_POINT_2F* pts, UINT n) noexcept {
    s->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    if (n > 1) s->AddLines(pts + 1, n - 1);
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
}

void fig_circle(ID2D1GeometrySink* s, float cx, float cy, float r, bool filled) noexcept {
    s->BeginFigure(D2D1::Point2F(cx + r, cy),
                   filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point = D2D1::Point2F(cx - r, cy);
    arc.size = D2D1::SizeF(r, r);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = D2D1_ARC_SIZE_SMALL;
    s->AddArc(arc);
    arc.point = D2D1::Point2F(cx + r, cy);
    s->AddArc(arc);
    s->EndFigure(filled ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
}

} // namespace

D2DRenderer::~D2DRenderer() {
    brushes_.clear();
    formats_.clear();
    text_layouts_.clear();
    icon_geos_.clear();
    icon_fill_geos_.clear();
    gradients_.clear();
    icon_stroke_.Reset();
    dwrite_.Reset();
    last_rt_.Reset();
}

void D2DRenderer::attach(IPresenter& presenter) noexcept {
    presenter_ = &presenter;
}

bool D2DRenderer::valid() const noexcept {
    return presenter_ != nullptr && presenter_->valid();
}

bool D2DRenderer::ensure_dwrite() noexcept {
    if (dwrite_ != nullptr) return true;
    return SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())));
}

void D2DRenderer::resize(unsigned width_px, unsigned height_px) noexcept {
    if (presenter_ != nullptr) presenter_->resize(width_px, height_px);
}

void D2DRenderer::set_dpi(float dpi) noexcept {
    dpi_ = dpi > 0.0f ? dpi : 96.0f;
    // New DPI -> the cached world transform is stale; force next begin() to
    // reapply SetTransform even if every other input (offset, physLeft/Top)
    // happens to coincide.
    transform_dirty_ = true;
    if (presenter_ != nullptr) presenter_->set_dpi(dpi_);
}

void D2DRenderer::begin(Color clear, const Rect* dirty) noexcept {
    frame_rt_ = nullptr;
    if (presenter_ == nullptr) return;

    SurfaceFrame frame = presenter_->begin_frame(dirty);
    frame_rt_ = frame.rt;
    if (frame_rt_ == nullptr) return;

    // Device-lost recovery: if the target identity changed since the last
    // frame, brushes created from the dead target are invalid — drop them.
    if (last_rt_.Get() != frame_rt_) {
        brushes_.clear();
        text_layouts_.clear();   // IDWriteTextLayout holds the dwrite factory; safe to keep, but rebind to new format pointer if we ever cache that.
        icon_geos_.clear();
        icon_fill_geos_.clear();
        gradients_.clear();
        bitmaps_.clear();   // WIC-decoded bitmaps are bound to the dead target
        icon_stroke_.Reset();
        last_rt_ = frame_rt_;   // AddRefs; releases the previous (possibly dead) RT
        // The new RT starts with the identity transform; any cached transform
        // state from the old (now-dead) RT doesn't carry over.
        transform_dirty_ = true;
    }

    frame_offset_x_ = frame.offsetX;
    frame_offset_y_ = frame.offsetY;

    frame_rt_->BeginDraw();
    // World transform: logical (96 DPI) -> device px, then into the returned
    // DXGI surface coordinate space. The returned surface's top-left maps to
    // composition coord (physLeft - offsetX, physTop - offsetY), so we add
    // (offsetX - physLeft) to translate logical→DXGI. For a full repaint
    // offsetX == physLeft == 0 and this reduces to plain Scale(dpi/96).
    //
    // The transform is only re-applied when one of its inputs actually
    // changed. For HwndPresenter (offset == 0, single-surface RT) the
    // transform is constant across frames after the first DPI init; the
    // DCompPresenter path still re-applies every frame because the offset
    // varies per dirty-rect sub-allocation.
    const float tx = static_cast<float>(frame.offsetX) - static_cast<float>(frame.physLeft);
    const float ty = static_cast<float>(frame.offsetY) - static_cast<float>(frame.physTop);
    if (transform_dirty_ ||
        dpi_ != last_transform_dpi_ ||
        frame.offsetX != last_transform_offset_x_ ||
        frame.offsetY != last_transform_offset_y_ ||
        frame.physLeft != last_transform_phys_left_ ||
        frame.physTop != last_transform_phys_top_) {
        frame_rt_->SetTransform(
            D2D1::Matrix3x2F::Scale(dpi_ / 96.0f, dpi_ / 96.0f) *
            D2D1::Matrix3x2F::Translation(tx, ty));
        last_transform_dpi_ = dpi_;
        last_transform_offset_x_ = frame.offsetX;
        last_transform_offset_y_ = frame.offsetY;
        last_transform_phys_left_ = frame.physLeft;
        last_transform_phys_top_ = frame.physTop;
        transform_dirty_ = false;
    }

    // Constrain drawing to the dirty region so a sub-allocated composition
    // surface is never written outside the requested update rect. PushAxis-
    // AlignedClip is in the render target's local space (DXGI pixels, NOT
    // affected by the world transform), so we must pass physical pixels here
    // — frame.dirty is in logical units and would be off by `scale` plus an
    // origin shift.
    D2D1_RECT_F clip{};
    if (frame.physWidth > 0 && frame.physHeight > 0) {
        clip = D2D1::RectF(static_cast<float>(frame.physLeft),
                           static_cast<float>(frame.physTop),
                           static_cast<float>(frame.physLeft + frame.physWidth),
                           static_cast<float>(frame.physTop + frame.physHeight));
    } else {
        clip = D2D1::RectF(0.0f, 0.0f, 1e6f, 1e6f);
    }
    frame_rt_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clip_depth_;

    // Clear the dirty region to the background colour. For full repaint this
    // is equivalent to Clear() over the whole surface; for a partial repaint
    // it fills only the dirty rect (already clipped), preserving neighbours.
    // We pass logical units — SetTransform above will scale them into DXGI.
    const Rect clip_logical = (frame.dirty.w > 0.0f && frame.dirty.h > 0.0f)
                                  ? frame.dirty
                                  : Rect{0.0f, 0.0f, 1e6f, 1e6f};
    fill_rect(clip_logical, clear);
}

void D2DRenderer::end() noexcept {
    if (frame_rt_ == nullptr) return;
    ID2D1RenderTarget* rt = frame_rt_;
    // Pop every outstanding clip (dirty clip + any left by render_element) so
    // EndDraw sees a balanced clip stack.
    while (clip_depth_ > 0) {
        rt->PopAxisAlignedClip();
        --clip_depth_;
    }
    frame_rt_ = nullptr;
    // EndDraw + commit happen inside the presenter (it owns the composition).
    if (presenter_ != nullptr) presenter_->end_frame();
}

ID2D1SolidColorBrush* D2DRenderer::brush(Color c) noexcept {
    if (frame_rt_ == nullptr) return nullptr;
    const unsigned key = color_key(c);
    auto it = brushes_.find(key);
    if (it != brushes_.end()) return it->second.Get();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(frame_rt_->CreateSolidColorBrush(to_d2d(c), b.GetAddressOf()))) {
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
    if (!ensure_dwrite()) return nullptr;
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
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        // Brushes are cached per colour and every other draw mutates their
        // opacity — reset from the colour's own alpha so the per-frame
        // background clear never inherits a stale partial alpha.
        b->SetOpacity(c.a / 255.0f);
        frame_rt_->FillRectangle(to_rect(r), b);
    }
}

void D2DRenderer::fill_round_rect(Rect r, float radius, Color c, float alpha) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        b->SetOpacity(alpha);
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        frame_rt_->FillRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b);
    }
}

void D2DRenderer::draw_round_rect(Rect r, float radius, Color c, float stroke_width,
                                  float alpha) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        b->SetOpacity(alpha);
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        frame_rt_->DrawRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b, stroke_width);
    }
}

void D2DRenderer::draw_line(float x1, float y1, float x2, float y2, Color c,
                            float stroke_width, float alpha) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        b->SetOpacity(alpha);
        frame_rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, stroke_width);
    }
}

void D2DRenderer::draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                            bool bold, Align halign, float alpha) noexcept {
    draw_text(r, text, size_pt, c, bold, halign, /*wrap=*/false, /*ellipsis=*/false,
              alpha);
}

void D2DRenderer::draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                            bool bold, Align halign, bool wrap, bool ellipsis,
                            float alpha) noexcept {
    if (frame_rt_ == nullptr || text.empty()) return;
    ID2D1SolidColorBrush* b = brush(c);
    IDWriteTextFormat* fmt = format(size_pt, bold);
    if (b == nullptr || fmt == nullptr) return;

    b->SetOpacity(alpha);
    fmt->SetTextAlignment(halign == Align::center ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : halign == Align::end    ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                  : DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Word wrap + trimming are per-call format state (same pattern as the
    // alignment above: always set, never rely on the previous draw).
    fmt->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                              : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (ellipsis) {
        IDWriteInlineObject* sign = nullptr;
        const unsigned fk = format_key(size_pt, bold);
        auto it = ellipsis_signs_.find(fk);
        if (it != ellipsis_signs_.end()) {
            sign = it->second.Get();
        } else if (ensure_dwrite()) {
            Microsoft::WRL::ComPtr<IDWriteInlineObject> made;
            if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(fmt,
                                                              made.GetAddressOf()))) {
                sign = made.Get();
                ellipsis_signs_.emplace(fk, std::move(made));
            }
        }
        DWRITE_TRIMMING trimming{};
        trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
        trimming.delimiter = 0;
        trimming.delimiterCount = 0;
        fmt->SetTrimming(&trimming, sign);
    } else {
        DWRITE_TRIMMING no_trim{};
        no_trim.granularity = DWRITE_TRIMMING_GRANULARITY_NONE;
        fmt->SetTrimming(&no_trim, nullptr);
    }
    frame_rt_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, to_rect(r), b,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

ID2D1LinearGradientBrush* D2DRenderer::gradient_brush(const Gradient& g) noexcept {
    if (frame_rt_ == nullptr) return nullptr;
    const unsigned long long key =
        (static_cast<unsigned long long>(color_key(g.from)) << 32u) | color_key(g.to);
    auto it = gradients_.find(key);
    if (it != gradients_.end()) return it->second.Get();

    const D2D1_GRADIENT_STOP stops[2] = {
        {0.0f, to_d2d(g.from)},
        {1.0f, to_d2d(g.to)},
    };
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> coll;
    if (FAILED(frame_rt_->CreateGradientStopCollection(
            stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, coll.GetAddressOf()))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> b;
    // Start/end points are in the RT's current (world-transformed) space;
    // they are re-pointed per fill so the cached brush serves every rect.
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = D2D1::Point2F(0.0f, 0.0f);
    props.endPoint = D2D1::Point2F(1.0f, 0.0f);
    if (FAILED(frame_rt_->CreateLinearGradientBrush(props, coll.Get(), b.GetAddressOf()))) {
        return nullptr;
    }
    auto* raw = b.Get();
    gradients_.emplace(key, std::move(b));
    return raw;
}

void D2DRenderer::fill_round_rect_gradient(Rect r, float radius, const Gradient& g,
                                           float alpha) noexcept {
    if (frame_rt_ == nullptr) return;
    ID2D1LinearGradientBrush* b = gradient_brush(g);
    if (b == nullptr) return;
    b->SetOpacity(alpha);
    b->SetStartPoint(D2D1::Point2F(r.x, r.y + r.h * 0.5f));
    b->SetEndPoint(D2D1::Point2F(r.x + r.w, r.y + r.h * 0.5f));
    const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
    frame_rt_->FillRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b);
}

// ---- WIC image support (F10) ----------------------------------------------

namespace {
// Decode dimensions only — no render target needed, so layout-time
// intrinsic-size queries work before the first begin().
[[nodiscard]] bool wic_frame_dims(IWICImagingFactory* factory, const std::wstring& path,
                                  unsigned& w, unsigned& h) noexcept {
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            decoder.GetAddressOf()))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return false;
    return SUCCEEDED(frame->GetSize(&w, &h));
}
constexpr unsigned kMaxImageSide = 4096;   // decode guard (F10 perf rail)
} // namespace

std::optional<std::pair<float, float>> D2DRenderer::image_size(
    const std::wstring& path) noexcept {
    if (const auto it = image_sizes_.find(path); it != image_sizes_.end()) {
        return it->second;
    }
    if (wic_ == nullptr) {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    __uuidof(IWICImagingFactory),
                                    reinterpret_cast<void**>(wic_.GetAddressOf())))) {
            return std::nullopt;   // COM not initialized / WIC unavailable
        }
    }
    unsigned w = 0, h = 0;
    if (!wic_frame_dims(wic_.Get(), path, w, h) || w == 0 || h == 0
        || w > kMaxImageSide || h > kMaxImageSide) {
        return std::nullopt;
    }
    image_sizes_.emplace(path, std::pair{static_cast<float>(w), static_cast<float>(h)});
    return image_sizes_.at(path);
}

void D2DRenderer::draw_image(const std::wstring& path, const Rect& r,
                             float alpha) noexcept {
    if (frame_rt_ == nullptr || r.w <= 0.0f || r.h <= 0.0f) return;

    ID2D1Bitmap* bmp = nullptr;
    if (const auto it = bitmaps_.find(path); it != bitmaps_.end()) {
        bmp = it->second.Get();
    } else {
        const auto dims = image_size(path);
        if (!dims.has_value()) return;   // undecodable or over the size guard
        if (wic_ == nullptr) return;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic_->CreateDecoderFromFilename(
                path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf()))) {
            return;
        }
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return;
        // Convert explicitly to premultiplied BGRA: handing the raw frame
        // to CreateBitmapFromWicBitmap fails with UNSUPPORTEDPIXELFORMAT for
        // anything D2D can't consume directly.
        Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
        if (FAILED(wic_->CreateFormatConverter(conv.GetAddressOf()))) return;
        WICPixelFormatGUID dst = GUID_WICPixelFormat32bppPBGRA;
        if (FAILED(conv->Initialize(frame.Get(), dst, WICBitmapDitherTypeNone,
                                    nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
            return;
        }
        Microsoft::WRL::ComPtr<ID2D1Bitmap> made;
        const HRESULT hrb = frame_rt_->CreateBitmapFromWicBitmap(conv.Get(), nullptr,
                                                                 made.GetAddressOf());
        if (FAILED(hrb)) return;
        bmp = made.Get();
        bitmaps_.emplace(path, std::move(made));
    }

    // Aspect-fit, centred in the target rect.
    const auto [bw, bh] = bmp->GetSize();
    if (bw <= 0.0f || bh <= 0.0f) return;
    const float scale = std::min(r.w / bw, r.h / bh);
    const float dw = bw * scale;
    const float dh = bh * scale;
    frame_rt_->DrawBitmap(bmp,
                          D2D1::RectF(r.x + (r.w - dw) * 0.5f, r.y + (r.h - dh) * 0.5f,
                                      r.x + (r.w + dw) * 0.5f, r.y + (r.h + dh) * 0.5f),
                          std::clamp(alpha, 0.0f, 1.0f));
}

void D2DRenderer::draw_shadow(Rect r, float radius, int elevation) noexcept {
    if (elevation <= 0 || frame_rt_ == nullptr) return;
    const int blurs[3] = {tokens::shadow_blur_sm, tokens::shadow_blur_md,
                          tokens::shadow_blur_lg};
    const float blur = static_cast<float>(blurs[std::clamp(elevation, 1, 3) - 1]);
    constexpr int kLayers = 3;
    const float dy = tokens::shadow_offset_y * static_cast<float>(elevation);
    for (int i = 1; i <= kLayers; ++i) {
        const float grow = blur * static_cast<float>(i) / static_cast<float>(kLayers);
        const float a = tokens::shadow_alpha * static_cast<float>(elevation) *
                        (1.0f - static_cast<float>(i - 1) / static_cast<float>(kLayers));
        fill_round_rect(Rect{r.x - grow, r.y - grow + dy, r.w + 2.0f * grow,
                             r.h + 2.0f * grow},
                        radius + grow, Color{0, 0, 0}, a);
    }
}

std::pair<float, float> D2DRenderer::measure_text(const std::wstring& text,
                                                  float size_pt, bool bold,
                                                  float wrap_width) noexcept {
    if (text.empty() || !ensure_dwrite()) return {0.0f, 0.0f};
    IDWriteTextFormat* fmt = format(size_pt, bold);
    if (fmt == nullptr) return {0.0f, 0.0f};

    // Key = (format bits) ^ (FNV-1a hash of text bytes). format_key already
    // embeds size+bold; mixing in the text bytes gives a unique key per
    // (text, size, bold) tuple. Hash quality is irrelevant — just needs to
    // spread distinct strings across buckets.
    const unsigned fk = format_key(size_pt, bold);
    unsigned long long hash = 1469598103934665603ULL;   // FNV offset basis
    for (wchar_t c : text) {
        hash ^= static_cast<unsigned long long>(c);
        hash *= 1099511628211ULL;   // FNV prime
    }
    const unsigned long long key = (static_cast<unsigned long long>(fk) << 32) ^ hash;

    // Wrap width joins the cache key: a wrapped layout is a different
    // shape than the natural one. 0 = unbounded (natural single/multi-line
    // size; explicit \n still breaks lines).
    const unsigned wrap_key = wrap_width > 0.0f
        ? static_cast<unsigned>(std::lround(wrap_width)) : 0u;
    const unsigned long long full_key =
        key ^ (static_cast<unsigned long long>(wrap_key) << 40u);

    Microsoft::WRL::ComPtr<IDWriteTextLayout>& slot = text_layouts_[full_key];
    if (slot == nullptr) {
        // Bound the cache: data-driven text (timestamps, per-row values)
        // would otherwise grow the map without limit. A coarse clear-when-
        // huge policy is fine — the common strings repopulate within a
        // frame or two.
        constexpr size_t kMaxCachedLayouts = 4096;
        if (text_layouts_.size() >= kMaxCachedLayouts) text_layouts_.clear();
        // Unbounded width layout: the measured extent is the natural text
        // size. With a wrap width, the layout wraps there and the height
        // reflects the resulting line count.
        const float lw = wrap_key != 0u ? static_cast<float>(wrap_key) : 100000.0f;
        const HRESULT hr = dwrite_->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), fmt,
            lw, 100000.0f, slot.GetAddressOf());
        if (FAILED(hr)) {
            text_layouts_.erase(full_key);
            return {0.0f, 0.0f};
        }
    }
    DWRITE_TEXT_METRICS m{};
    slot->GetMetrics(&m);
    return {std::ceil(m.width), std::ceil(m.height)};
}

ID2D1StrokeStyle* D2DRenderer::icon_stroke() noexcept {
    if (icon_stroke_ != nullptr) return icon_stroke_.Get();
    if (frame_rt_ == nullptr) return nullptr;
    // Round caps/joins keep 45-degree chevrons and checks from sprouting
    // spikes at 1.5-2 px stroke widths.
    D2D1_STROKE_STYLE_PROPERTIES props{};
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 2.0f;
    ID2D1Factory* fac = nullptr;
    frame_rt_->GetFactory(&fac);
    if (fac == nullptr) return nullptr;
    if (FAILED(fac->CreateStrokeStyle(props, nullptr, 0, icon_stroke_.GetAddressOf()))) {
        return nullptr;
    }
    return icon_stroke_.Get();
}

// Builds the stroke outline + fill accents for one icon on the 24x24 grid.
// Coordinates follow the Feather/24-grid convention (2 px baseline stroke).
bool D2DRenderer::build_icon(IconKind kind) noexcept {
    if (frame_rt_ == nullptr) return false;
    ID2D1Factory* fac = nullptr;
    frame_rt_->GetFactory(&fac);
    if (fac == nullptr) return false;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> stroke;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> fill;
    if (FAILED(fac->CreatePathGeometry(stroke.GetAddressOf()))) return false;
    const bool has_fill = kind == IconKind::play || kind == IconKind::pause ||
                          kind == IconKind::skip_prev || kind == IconKind::skip_next ||
                          kind == IconKind::edit || kind == IconKind::info ||
                          kind == IconKind::warning;
    if (has_fill && FAILED(fac->CreatePathGeometry(fill.GetAddressOf()))) return false;

    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(stroke->Open(sink.GetAddressOf()))) return false;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> fsink;
    if (has_fill && FAILED(fill->Open(fsink.GetAddressOf()))) return false;

    using P = D2D1_POINT_2F;
    auto pts = [](std::initializer_list<D2D1_POINT_2F> l) {
        return std::vector<D2D1_POINT_2F>(l);
    };
    const auto line = [&](std::initializer_list<D2D1_POINT_2F> l) {
        const std::vector<P> v = pts(l);
        fig_poly(sink.Get(), v.data(), static_cast<UINT>(v.size()), D2D1_FIGURE_END_OPEN);
    };
    const auto closed = [&](std::initializer_list<D2D1_POINT_2F> l) {
        const std::vector<P> v = pts(l);
        fig_poly(sink.Get(), v.data(), static_cast<UINT>(v.size()), D2D1_FIGURE_END_CLOSED);
    };
    const auto circle = [&](float cx, float cy, float r) {
        fig_circle(sink.Get(), cx, cy, r, false);
    };
    const auto dot = [&](float cx, float cy, float r) {
        fig_circle(fsink.Get(), cx, cy, r, true);
    };
    const auto solid = [&](std::initializer_list<D2D1_POINT_2F> l) {
        const std::vector<P> v = pts(l);
        fig_poly_filled(fsink.Get(), v.data(), static_cast<UINT>(v.size()));
    };

    switch (kind) {
    case IconKind::chevron_down:  line({P{15, 6},  P{9, 12},  P{15, 18}}); break;
    case IconKind::chevron_up:    line({P{9, 18},  P{15, 12}, P{9, 6}});   break;
    case IconKind::chevron_left:  line({P{14, 5},  P{7, 12},  P{14, 19}}); break;
    case IconKind::chevron_right: line({P{10, 5},  P{17, 12}, P{10, 19}}); break;
    case IconKind::close:
        line({P{6, 6},  P{18, 18}});
        line({P{18, 6}, P{6, 18}});
        break;
    case IconKind::check: line({P{5, 13}, P{10, 18}, P{19, 7}}); break;
    case IconKind::search:
        circle(11, 11, 7);
        line({P{16.5f, 16.5f}, P{21, 21}});
        break;
    case IconKind::plus:
        line({P{12, 5}, P{12, 19}});
        line({P{5, 12}, P{19, 12}});
        break;
    case IconKind::minus: line({P{5, 12}, P{19, 12}}); break;
    case IconKind::hamburger:
        line({P{4, 7},  P{20, 7}});
        line({P{4, 12}, P{20, 12}});
        line({P{4, 17}, P{20, 17}});
        break;
    case IconKind::user:
        circle(12, 8, 4);
        line({P{5, 20}, P{5, 18}, P{7, 15}, P{12, 14}, P{17, 15}, P{19, 18}, P{19, 20}});
        break;
    case IconKind::trash:
        line({P{5, 7}, P{19, 7}});
        line({P{9, 7}, P{9, 5}, P{15, 5}, P{15, 7}});
        line({P{7, 7}, P{8, 20}, P{16, 20}, P{17, 7}});
        break;
    case IconKind::edit:
        closed({P{4, 20}, P{5, 16}, P{16, 5}, P{19, 8}, P{8, 19}});
        solid({P{4.6f, 19}, P{5.4f, 16.4f}, P{16, 5.8f}, P{18.2f, 8}, P{7.6f, 18.6f}});
        break;
    case IconKind::arrow_right:
        line({P{4, 12}, P{20, 12}});
        line({P{13, 5}, P{20, 12}, P{13, 19}});
        break;
    case IconKind::calendar:
        closed({P{4, 6}, P{20, 6}, P{20, 20}, P{4, 20}});
        line({P{4, 10}, P{20, 10}});
        line({P{8, 4}, P{8, 8}});
        line({P{16, 4}, P{16, 8}});
        break;
    case IconKind::inbox:
        closed({P{4, 13}, P{8, 6}, P{16, 6}, P{20, 13}, P{20, 18}, P{4, 18}});
        line({P{4, 13}, P{10, 13}, P{12, 16}, P{14, 13}, P{20, 13}});
        break;
    case IconKind::play:
        solid({P{8, 5}, P{19, 12}, P{8, 19}});
        break;
    case IconKind::pause:
        solid({P{7, 5}, P{10.5f, 5}, P{10.5f, 19}, P{7, 19}});
        solid({P{13.5f, 5}, P{17, 5}, P{17, 19}, P{13.5f, 19}});
        break;
    case IconKind::skip_prev:
        solid({P{19, 5}, P{9, 12}, P{19, 19}});
        line({P{6, 5}, P{6, 19}});
        break;
    case IconKind::skip_next:
        solid({P{5, 5}, P{15, 12}, P{5, 19}});
        line({P{18, 5}, P{18, 19}});
        break;
    case IconKind::info:
        circle(12, 12, 9);
        line({P{12, 11}, P{12, 16}});
        dot(12, 8.2f, 1.1f);
        break;
    case IconKind::success:
        circle(12, 12, 9);
        line({P{8, 12.5f}, P{11, 15.5f}, P{16.5f, 9.5f}});
        break;
    case IconKind::warning:
        closed({P{12, 4}, P{21, 19}, P{3, 19}});
        line({P{12, 10}, P{12, 14}});
        dot(12, 17, 0.9f);
        break;
    case IconKind::danger:
        circle(12, 12, 9);
        line({P{9, 9}, P{15, 15}});
        line({P{15, 9}, P{9, 15}});
        break;
    }

    if (FAILED(sink->Close())) return false;
    if (has_fill && FAILED(fsink->Close())) return false;
    icon_geos_.emplace(static_cast<int>(kind), std::move(stroke));
    if (has_fill) icon_fill_geos_.emplace(static_cast<int>(kind), std::move(fill));
    return true;
}

ID2D1PathGeometry* D2DRenderer::icon_geometry(IconKind kind) noexcept {
    auto it = icon_geos_.find(static_cast<int>(kind));
    if (it != icon_geos_.end()) return it->second.Get();
    if (!build_icon(kind)) return nullptr;
    it = icon_geos_.find(static_cast<int>(kind));
    return it != icon_geos_.end() ? it->second.Get() : nullptr;
}

ID2D1PathGeometry* D2DRenderer::icon_fill_geometry(IconKind kind) noexcept {
    auto it = icon_fill_geos_.find(static_cast<int>(kind));
    if (it != icon_fill_geos_.end()) return it->second.Get();
    // Building the stroke geometry also builds the fill accents.
    if (icon_geometry(kind) == nullptr) return nullptr;
    it = icon_fill_geos_.find(static_cast<int>(kind));
    return it != icon_fill_geos_.end() ? it->second.Get() : nullptr;
}

void D2DRenderer::draw_icon(Rect box, IconKind kind, Color c, float stroke_w,
                            float alpha) noexcept {
    if (frame_rt_ == nullptr) return;
    ID2D1PathGeometry* geo = icon_geometry(kind);
    if (geo == nullptr) return;
    ID2D1SolidColorBrush* b = brush(c);
    ID2D1StrokeStyle* style = icon_stroke();
    if (b == nullptr || style == nullptr) return;

    const float side = std::min(box.w, box.h);
    if (side <= 0.0f) return;
    const float s = side / 24.0f;
    b->SetOpacity(alpha);

    // The geometry lives on the 24x24 grid; scale+translate it into `box`
    // via a temporary transform. Stroke width is given in final logical
    // units, so divide by the scale to keep it constant on screen.
    D2D1_MATRIX_3X2_F world{};
    frame_rt_->GetTransform(&world);
    frame_rt_->SetTransform(world * D2D1::Matrix3x2F::Scale(s, s) *
                                      D2D1::Matrix3x2F::Translation(box.x, box.y));
    frame_rt_->DrawGeometry(geo, b, std::max(0.25f, stroke_w / s), style);
    if (ID2D1PathGeometry* fg = icon_fill_geometry(kind); fg != nullptr) {
        frame_rt_->FillGeometry(fg, b);
    }
    frame_rt_->SetTransform(world);
}

void D2DRenderer::draw_polyline(const std::vector<std::pair<float, float>>& pts,
                                Color c, float stroke_w, float alpha) noexcept {
    if (frame_rt_ == nullptr || pts.size() < 2) return;
    ID2D1Factory* fac = nullptr;
    frame_rt_->GetFactory(&fac);
    if (fac == nullptr) return;
    ID2D1SolidColorBrush* b = brush(c);
    if (b == nullptr) return;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(fac->CreatePathGeometry(geo.GetAddressOf()))) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].first, pts[0].second),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    std::vector<D2D1_POINT_2F> rest;
    rest.reserve(pts.size() - 1);
    for (size_t i = 1; i < pts.size(); ++i)
        rest.push_back(D2D1::Point2F(pts[i].first, pts[i].second));
    sink->AddLines(rest.data(), static_cast<UINT>(rest.size()));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    b->SetOpacity(alpha);
    frame_rt_->DrawGeometry(geo.Get(), b, stroke_w);
}

void D2DRenderer::draw_polygon(const std::vector<std::pair<float, float>>& pts,
                               Color c, float alpha) noexcept {
    if (frame_rt_ == nullptr || pts.size() < 3) return;
    ID2D1Factory* fac = nullptr;
    frame_rt_->GetFactory(&fac);
    if (fac == nullptr) return;
    ID2D1SolidColorBrush* b = brush(c);
    if (b == nullptr) return;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(fac->CreatePathGeometry(geo.GetAddressOf()))) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf()))) return;
    sink->BeginFigure(D2D1::Point2F(pts[0].first, pts[0].second),
                      D2D1_FIGURE_BEGIN_FILLED);
    std::vector<D2D1_POINT_2F> rest;
    rest.reserve(pts.size() - 1);
    for (size_t i = 1; i < pts.size(); ++i)
        rest.push_back(D2D1::Point2F(pts[i].first, pts[i].second));
    sink->AddLines(rest.data(), static_cast<UINT>(rest.size()));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    b->SetOpacity(alpha);
    frame_rt_->FillGeometry(geo.Get(), b);
}

void D2DRenderer::draw_pie_slice(float cx, float cy, float r, float start_deg,
                                 float sweep_deg, Color c, float alpha) noexcept {
    if (frame_rt_ == nullptr || r <= 0.0f || sweep_deg == 0.0f) return;
    ID2D1Factory* fac = nullptr;
    frame_rt_->GetFactory(&fac);
    if (fac == nullptr) return;
    ID2D1SolidColorBrush* b = brush(c);
    if (b == nullptr) return;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geo;
    if (FAILED(fac->CreatePathGeometry(geo.GetAddressOf()))) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geo->Open(sink.GetAddressOf()))) return;

    constexpr float kPi = 3.14159265358979f;
    const auto pt_on = [&](float deg) {
        const float rad = (deg - 90.0f) * kPi / 180.0f;   // 0deg = 12 o'clock
        return D2D1::Point2F(cx + r * std::sin(rad), cy - r * std::cos(rad));
    };
    sink->BeginFigure(D2D1::Point2F(cx, cy), D2D1_FIGURE_BEGIN_FILLED);
    D2D1_ARC_SEGMENT arc{};
    arc.point = pt_on(start_deg + sweep_deg);
    arc.size = D2D1::SizeF(r, r);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize = sweep_deg > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
    sink->AddLine(pt_on(start_deg));
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    b->SetOpacity(alpha);
    frame_rt_->FillGeometry(geo.Get(), b);
}

void D2DRenderer::push_clip(Rect r) noexcept {
    if (frame_rt_ == nullptr) return;
    frame_rt_->PushAxisAlignedClip(to_rect(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ++clip_depth_;
}

void D2DRenderer::pop_clip() noexcept {
    if (frame_rt_ == nullptr || clip_depth_ <= 0) return;
    frame_rt_->PopAxisAlignedClip();
    --clip_depth_;
}

} // namespace flux