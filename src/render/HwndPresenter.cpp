#include <flux/HwndPresenter.hpp>

namespace flux {

HwndPresenter::~HwndPresenter() {
    rt_.Reset();
    factory_.Reset();
}

bool HwndPresenter::create(HWND hwnd) noexcept {
    hwnd_ = hwnd;
    if (hwnd_ == nullptr) return false;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) return false;
    recreate_target();
    return rt_ != nullptr;
}

void HwndPresenter::recreate_target() noexcept {
    rt_.Reset();
    if (factory_ == nullptr || hwnd_ == nullptr) return;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_px_ = static_cast<unsigned>(rc.right - rc.left);
    height_px_ = static_cast<unsigned>(rc.bottom - rc.top);
    if (width_px_ == 0) width_px_ = 1;
    if (height_px_ == 0) height_px_ = 1;

    const UINT dpi_x = GetDpiForWindow(hwnd_);
    dpi_ = dpi_x != 0 ? static_cast<float>(dpi_x) : 96.0f;

    const D2D1_SIZE_U size = D2D1::SizeU(width_px_, height_px_);
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    rtp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    rtp.dpiX = 96.0f;
    rtp.dpiY = 96.0f;   // DIP == device px, matching the DComp context path
    D2D1_HWND_RENDER_TARGET_PROPERTIES htp = D2D1::HwndRenderTargetProperties(hwnd_, size);
    // Surface silent failure: a persistent transient error here would leave
    // valid() returning true (factory exists) but rt_ == nullptr, so the
    // renderer would BeginDraw on a null target. Log once and bail.
    const HRESULT hr = factory_->CreateHwndRenderTarget(rtp, htp, rt_.GetAddressOf());
    if (FAILED(hr)) {
        OutputDebugStringW(L"WinFlux: HwndPresenter::recreate_target failed\n");
    }
}

void HwndPresenter::resize(unsigned width_px, unsigned height_px) noexcept {
    // ID2D1HwndRenderTarget::Resize rejects 0x0 (E_INVALIDARG). WM_SIZE
    // SIZE_MINIMIZED delivers lParam=0; clamping to 1x1 lets us keep a live
    // RT through the minimized state and restore on the next WM_SIZE.
    width_px_ = width_px == 0 ? 1 : width_px;
    height_px_ = height_px == 0 ? 1 : height_px;
    if (rt_ == nullptr) return;
    const HRESULT hr = rt_->Resize(D2D1::SizeU(width_px_, height_px_));
    if (FAILED(hr)) recreate_target();
}

SurfaceFrame HwndPresenter::begin_frame(const Rect* dirty) noexcept {
    SurfaceFrame frame;
    if (rt_ == nullptr) recreate_target();
    if (rt_ == nullptr) return frame;
    // The HWND render target owns the whole client surface; the renderer
    // clips to the dirty rect itself. Offset is always (0,0).
    drew_this_frame_ = true;
    frame.rt = rt_.Get();
    frame.dirty = (dirty != nullptr) ? *dirty : Rect{};
    frame.offsetX = 0;
    frame.offsetY = 0;
    return frame;
}

void HwndPresenter::end_frame() noexcept {
    if (!drew_this_frame_) return;
    drew_this_frame_ = false;
    if (rt_ == nullptr) return;
    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) recreate_target();
}

} // namespace flux