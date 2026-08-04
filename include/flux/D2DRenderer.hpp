#pragma once

// WinFlux render layer — Direct2D 1.1 + DirectWrite. One HWND render
// target per Host window; the whole scene tree is composited into it each
// frame. All draw calls take LOGICAL units (96 DPI baseline); begin() loads
// a uniform scale transform of dpi/96 so callers never touch device pixels.

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <unordered_map>
#include <utility>

#include "Element.hpp"
#include "Theme.hpp"

namespace flux {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

class D2DRenderer {
public:
    D2DRenderer() noexcept = default;
    ~D2DRenderer();
    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    [[nodiscard]] bool create(HWND hwnd) noexcept;
    void resize(unsigned width_px, unsigned height_px) noexcept;
    void set_dpi(float dpi) noexcept;
    [[nodiscard]] float dpi() const noexcept { return dpi_; }
    [[nodiscard]] bool valid() const noexcept { return rt_ != nullptr; }

    void begin(Color clear) noexcept;
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
    void recreate_target() noexcept;

    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    std::unordered_map<unsigned int, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> brushes_;
    std::unordered_map<unsigned int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    HWND hwnd_ = nullptr;
    unsigned width_px_ = 0;
    unsigned height_px_ = 0;
    float dpi_ = 96.0f;
    int clip_depth_ = 0;
};

} // namespace flux
