#include <flux/D2DRenderer.hpp>

#include <cmath>
#include <cstring>

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
    brushes_.clear();
    formats_.clear();
    text_layouts_.clear();
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
        frame_rt_->FillRectangle(to_rect(r), b);
    }
}

void D2DRenderer::fill_round_rect(Rect r, float radius, Color c) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        frame_rt_->FillRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b);
    }
}

void D2DRenderer::draw_round_rect(Rect r, float radius, Color c, float stroke_width) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        const float clamped = std::min(radius, std::min(r.w, r.h) * 0.5f);
        frame_rt_->DrawRoundedRectangle(D2D1::RoundedRect(to_rect(r), clamped, clamped), b, stroke_width);
    }
}

void D2DRenderer::draw_line(float x1, float y1, float x2, float y2, Color c, float stroke_width) noexcept {
    if (frame_rt_ == nullptr) return;
    if (ID2D1SolidColorBrush* b = brush(c)) {
        frame_rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, stroke_width);
    }
}

void D2DRenderer::draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                            bool bold, Align halign) noexcept {
    if (frame_rt_ == nullptr || text.empty()) return;
    ID2D1SolidColorBrush* b = brush(c);
    IDWriteTextFormat* fmt = format(size_pt, bold);
    if (b == nullptr || fmt == nullptr) return;

    fmt->SetTextAlignment(halign == Align::center ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : halign == Align::end    ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                  : DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    frame_rt_->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt, to_rect(r), b,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

std::pair<float, float> D2DRenderer::measure_text(const std::wstring& text,
                                                  float size_pt, bool bold) noexcept {
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

    Microsoft::WRL::ComPtr<IDWriteTextLayout>& slot = text_layouts_[key];
    if (slot == nullptr) {
        // Unbounded width layout: the measured extent is the natural text size.
        const HRESULT hr = dwrite_->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), fmt,
            100000.0f, 100000.0f, slot.GetAddressOf());
        if (FAILED(hr)) {
            text_layouts_.erase(key);
            return {0.0f, 0.0f};
        }
    }
    DWRITE_TEXT_METRICS m{};
    slot->GetMetrics(&m);
    return {std::ceil(m.width), std::ceil(m.height)};
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