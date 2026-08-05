#pragma once

// Legacy Direct2D 1.0 presenter — the fallback path used when
// DirectComposition creation fails (old OS, driver without DComp). It owns a
// single ID2D1HwndRenderTarget per window; begin_frame returns it as a
// full-client-area surface (offset 0,0), end_frame ends the draw and rebuilds
// the target on D2DERR_RECREATE_TARGET. Dirty-rect sub-regions are honoured by
// the renderer's clip; the HWND RT itself repaints the whole dirty region.

#include "Presenter.hpp"

#include <d2d1.h>
#include <wrl/client.h>

namespace flux {

class HwndPresenter : public IPresenter {
public:
    HwndPresenter() noexcept = default;
    ~HwndPresenter() override;
    HwndPresenter(const HwndPresenter&) = delete;
    HwndPresenter& operator=(const HwndPresenter&) = delete;

    bool create(HWND hwnd) noexcept override;
    void resize(unsigned width_px, unsigned height_px) noexcept override;
    void set_dpi(float dpi) noexcept override { dpi_ = dpi > 0.0f ? dpi : 96.0f; }
    SurfaceFrame begin_frame(const Rect* dirty) noexcept override;
    void end_frame() noexcept override;
    [[nodiscard]] bool valid() const noexcept override { return rt_ != nullptr; }

private:
    void recreate_target() noexcept;

    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    HWND hwnd_ = nullptr;
    unsigned width_px_ = 0;
    unsigned height_px_ = 0;
    float dpi_ = 96.0f;
    bool drew_this_frame_ = false;
};

} // namespace flux