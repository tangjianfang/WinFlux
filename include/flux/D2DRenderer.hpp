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
#include <wincodec.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
    // True between a successful begin() and its end() — a failed BeginDraw
    // (device lost, surface exhausted) leaves it false so the caller can
    // skip advancing its diff baseline (review 2026-10-06 R-7).
    [[nodiscard]] bool frame_active() const noexcept { return frame_rt_ != nullptr; }

    void fill_rect(Rect r, Color c) noexcept;
    void fill_round_rect(Rect r, float radius, Color c, float alpha = 1.0f) noexcept;
    // Decodes `path` via WIC on first use and caches per device (device loss
    // clears the cache). Draws aspect-fit, centred in `r`. Requires COM on
    // the thread — Host::create initializes it. Images larger than 4096 px
    // on a side are refused (draws nothing) to keep decode off the hot path.
    void draw_image(const std::wstring& path, const Rect& r, float alpha = 1.0f) noexcept;
    // Intrinsic pixel size of `path` (WIC decode, cached, render-target
    // independent — usable during layout). nullopt when undecodable.
    [[nodiscard]] std::optional<std::pair<float, float>> image_size(
        const std::wstring& path) noexcept;
    void draw_round_rect(Rect r, float radius, Color c, float stroke_width,
                         float alpha = 1.0f) noexcept;
    void draw_line(float x1, float y1, float x2, float y2, Color c,
                   float stroke_width, float alpha = 1.0f) noexcept;
    void draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                   bool bold, Align halign, float alpha = 1.0f) noexcept;
    // Multi-line text: explicit \n always breaks; text_wrap wraps at the
    // rect width; text_ellipsis trims a single overflowing line with "…".
    void draw_text(Rect r, const std::wstring& text, float size_pt, Color c,
                   bool bold, Align halign, bool wrap, bool ellipsis,
                   float alpha = 1.0f) noexcept;
    // Horizontal L->R gradient fill; SetStartPoint/SetEndPoint are updated
    // per call so one cached brush serves every rect.
    void fill_round_rect_gradient(Rect r, float radius, const Gradient& g,
                                  float alpha = 1.0f) noexcept;
    // Elevation 1..3 hand-drawn shadow: concentric low-alpha rounded rects
    // below/around the base rect (no D2D effects dependency).
    void draw_shadow(Rect r, float radius, int elevation) noexcept;
    // 24x24 stroke-grid icon scaled into `box` (square side = min(w,h)).
    // stroke_w is in final logical units; fill accents drawn when the icon
    // has any (play/pause triangles, notification dots, ...).
    void draw_icon(Rect box, IconKind kind, Color c, float stroke_w,
                   float alpha = 1.0f) noexcept;
    // Chart primitives (logical units). Polyline strokes connect the points
    // in order; polygon fills the closed shape; pie slices are filled
    // wedge figures (angles in degrees, 0 = +x axis, clockwise).
    void draw_polyline(const std::vector<std::pair<float, float>>& pts, Color c,
                       float stroke_w, float alpha = 1.0f) noexcept;
    void draw_polygon(const std::vector<std::pair<float, float>>& pts, Color c,
                      float alpha = 1.0f) noexcept;
    void draw_pie_slice(float cx, float cy, float r, float start_deg,
                        float sweep_deg, Color c, float alpha = 1.0f) noexcept;
    [[nodiscard]] std::pair<float, float> measure_text(const std::wstring& text,
                                                       float size_pt, bool bold,
                                                       float wrap_width = 0.0f) noexcept;
    void push_clip(Rect r) noexcept;
    void pop_clip() noexcept;

private:
    [[nodiscard]] ID2D1SolidColorBrush* brush(Color c) noexcept;
    [[nodiscard]] IDWriteTextFormat* format(float size_pt, bool bold) noexcept;
    bool ensure_dwrite() noexcept;
    // Icon geometry (built lazily via the frame RT's factory, keyed by
    // IconKind). `icon_geometry` returns the stroke outline; `icon_fill_`
    // the filled accents (nullptr for icons without any).
    [[nodiscard]] ID2D1PathGeometry* icon_geometry(IconKind kind) noexcept;
    [[nodiscard]] ID2D1PathGeometry* icon_fill_geometry(IconKind kind) noexcept;
    bool build_icon(IconKind kind) noexcept;
    [[nodiscard]] ID2D1LinearGradientBrush* gradient_brush(const Gradient& g) noexcept;
    [[nodiscard]] ID2D1StrokeStyle* icon_stroke() noexcept;

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
    // Round cap/join stroke style shared by every icon draw.
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> icon_stroke_;
    // Icon geometries keyed by IconKind (stroke outline + fill accents).
    std::unordered_map<int, Microsoft::WRL::ComPtr<ID2D1PathGeometry>> icon_geos_;
    std::unordered_map<int, Microsoft::WRL::ComPtr<ID2D1PathGeometry>> icon_fill_geos_;
    // WIC image support (F10): lazily-created factory, per-device bitmaps
    // (cleared when the render target identity changes), and target-
    // independent decoded sizes for layout-time queries.
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;
    std::unordered_map<std::wstring, std::pair<float, float>> image_sizes_;
    // Gradient brushes keyed by (from,to) colour pair.
    std::unordered_map<unsigned long long,
                       Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>> gradients_;
    // Ellipsis trimming signs keyed by format_key (created from the dwrite
    // factory + that format).
    std::unordered_map<unsigned int,
                       Microsoft::WRL::ComPtr<IDWriteInlineObject>> ellipsis_signs_;
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