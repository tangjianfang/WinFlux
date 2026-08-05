#pragma once

// DirectComposition presenter. Owns the device-level resources that survive
// a resize: a D3D11 device, its DXGI device, a D2D1 device + device context,
// and the composition device/target/visual/surface. On WM_SIZE the backing
// surface is rebuilt and re-committed; the D2D/D3D device context persists.
//
// begin_frame() calls IDCompositionSurface::BeginDraw with the dirty rect
// (translated to physical pixels), wraps the returned DXGI surface as a D2D
// bitmap, sets it as the device-context target, and returns the update offset
// so the renderer can position its world transform. end_frame() calls
// EndDraw on both the D2D context and the composition surface, then commits.
// Device-lost (D2DERR_RECREATE_TARGET) triggers a full D2D+surface rebuild.

#include "Presenter.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <wrl/client.h>

namespace flux {

class DCompPresenter : public IPresenter {
public:
    DCompPresenter() noexcept = default;
    ~DCompPresenter() override;
    DCompPresenter(const DCompPresenter&) = delete;
    DCompPresenter& operator=(const DCompPresenter&) = delete;

    bool create(HWND hwnd) noexcept override;
    void resize(unsigned width_px, unsigned height_px) noexcept override;
    void set_dpi(float dpi) noexcept override;
    SurfaceFrame begin_frame(const Rect* dirty) noexcept override;
    void end_frame() noexcept override;
    [[nodiscard]] bool valid() const noexcept override;

private:
    bool create_device_resources() noexcept;   // D3D11 + DXGI + D2D device/context
    bool create_composition() noexcept;        // DComp device/target/visual + surface
    bool create_surface(unsigned w, unsigned h) noexcept;
    void release_surface() noexcept;
    void release_d2d() noexcept;                // D2D device/context + surface
    bool rebuild_d2d() noexcept;                // device-lost recovery
    void teardown() noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_device_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_device_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcomp_target_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_visual_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> frame_bitmap_;

    HWND hwnd_ = nullptr;
    unsigned width_px_ = 0;
    unsigned height_px_ = 0;
    float dpi_ = 96.0f;
    bool drew_this_frame_ = false;
};

} // namespace flux