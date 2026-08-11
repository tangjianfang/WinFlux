#pragma once

// WinFlux render layer — Direct2D + DirectWrite draw commands. The renderer
// no longer owns a render target; it is attached to an IPresenter (DComp by
// default, Hwnd fallback) which supplies a per-frame drawing surface. All
// draw calls take LOGICAL units (96 DPI baseline); begin() loads a uniform
// scale transform of dpi/96 (plus the presenter's update-offset translation)
// so callers never touch device pixels. The brush/format caches are cleared
// when the underlying render target is recreated (device-lost recovery).

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <utility>

#include "Element.hpp"
#include "Presenter.hpp"   // Rect + IPresenter + SurfaceFrame
#include "Theme.hpp"

namespace flux {

class D2DRenderer {
public:
    D2DRenderer() noexcept = default;
    ~D2DRenderer();
    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    // Binds the renderer to a presenter that owns the device + surface. The
    // renderer never outlives the presenter; Host owns both and tears the
    // renderer down first.
    void attach(IPresenter& presenter) noexcept;
    void resize(unsigned width_px, unsigned height_px) noexcept;
    void set_dpi(float dpi) noexcept;
    [[nodiscard]] float dpi() const noexcept { return dpi_; }
    [[nodiscard]] bool valid() const noexcept;

    // dirty == nullptr => update the whole client area (full repaint).
    void begin(Color clear, const Rect* dirty) noexcept;
    void end() noexcept;

    void fill_rect(Rect r, Color c) noexcept;
    void fill_round_rect(Rect r, float radius, Color c) noexcept;
    void draw_round_rect(Rect r, float radius, Color c, float stroke_width) noexcept;
    void draw_line(float x1, float y1, float x2, float y2, Color c, float stroke_width) noexcept;
    void draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                   bool bold, Align halign) noexcept;
    [[nodiscard]] std::pair<float, float> measure_text(const std::wstring& text,
                                                       float size_pt, bool bold) noexcept;
    void push_clip(Rect r) noexcept;
    void pop_clip() noexcept;

private:
    [[nodiscard]] ID2D1SolidColorBrush* brush(Color c) noexcept;
    [[nodiscard]] IDWriteTextFormat* format(float size_pt, bool bold) noexcept;
    bool ensure_dwrite() noexcept;

    IPresenter* presenter_ = nullptr;
    ID2D1RenderTarget* frame_rt_ = nullptr;   // transient, valid only during begin/end
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> last_rt_;  // device-lost cache invalidation
    int frame_offset_x_ = 0;
    int frame_offset_y_ = 0;
    // Cached values for the world transform last applied via SetTransform.
    // The transform is `Scale(dpi/96) * Translation(offsetX - physLeft, ...)`;
    // for HwndPresenter the offset is always zero so the transform only
    // changes on DPI change, and we can skip the per-frame SetTransform call.
    // For DCompPresenter the offset varies per dirty rect, so this just
    // degenerates into the original "always set" behaviour.
    float last_transform_dpi_ = 0.0f;
    int last_transform_offset_x_ = 0;
    int last_transform_offset_y_ = 0;
    int last_transform_phys_left_ = 0;
    int last_transform_phys_top_ = 0;
    bool transform_dirty_ = true;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    std::unordered_map<unsigned int, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> brushes_;
    std::unordered_map<unsigned int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    // Per-(text,format) IDWriteTextLayout cache, keyed by a 64-bit hash that
    // mixes the format key with the text pointer/size. measure_text reuses
    // the same layout across frames so long as the text and format haven't
    // changed — every leaf measurement would otherwise allocate a fresh
    // layout, which dominates the cost of partial-repaint frames with many
    // labels. Bounded by the number of distinct (text, format) pairs the
    // app actually uses; for a typical UI this is O(few dozen).
    std::unordered_map<unsigned long long, Microsoft::WRL::ComPtr<IDWriteTextLayout>> text_layouts_;
    float dpi_ = 96.0f;
    int clip_depth_ = 0;
};

} // namespace flux