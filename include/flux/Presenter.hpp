#pragma once

// WinFlux presentation layer abstraction. A Host does not own a specific
// Direct2D render target; it owns an IPresenter, which owns the DXGI/D2D
// device resources and hands out a per-frame drawing surface. The default
// implementation is DirectComposition (DCompPresenter); HwndPresenter is the
// legacy Direct2D 1.0 fallback used when DComp creation fails.
//
// All coordinates passed across this interface are LOGICAL units at the 96 DPI
// baseline, identical to the D2DRenderer draw API. The presenter translates
// the requested dirty rect into the surface's physical-pixel coordinate
// space internally and returns the BeginDraw update offset so the renderer
// can compose the correct world transform.

#include <d2d1.h>

namespace flux {

// Axis-aligned rectangle in logical units (96 DPI baseline). Lives here so
// both the Presenter interface and the renderer can use it without a cycle.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// One frame of drawable surface handed to the renderer by begin_frame().
//   rt        — the D2D render target to issue draw calls on this frame
//   dirty     — the dirty rect actually being updated, in logical units
//   offsetX/Y — the IDCompositionSurface::BeginDraw update offset, in
//                physical surface pixels; the renderer subtracts it from the
//                world transform so logical coordinates land correctly inside
//                the (possibly sub-allocated) returned DXGI surface.
struct SurfaceFrame {
    ID2D1RenderTarget* rt = nullptr;
    Rect dirty{};
    int offsetX = 0;
    int offsetY = 0;
};

class IPresenter {
public:
    virtual ~IPresenter() = default;

    virtual bool create(HWND hwnd) noexcept = 0;
    virtual void resize(unsigned width_px, unsigned height_px) noexcept = 0;
    virtual void set_dpi(float dpi) noexcept = 0;
    // dirty == nullptr or empty dirty => update the whole client area.
    virtual SurfaceFrame begin_frame(const Rect* dirty) noexcept = 0;
    virtual void end_frame() noexcept = 0;
    [[nodiscard]] virtual bool valid() const noexcept = 0;
};

} // namespace flux