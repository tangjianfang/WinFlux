#include <flux/DCompPresenter.hpp>

#include <algorithm>

namespace flux {

namespace {

RECT to_physical_rect(const Rect& r, float scale, unsigned surf_w, unsigned surf_h) noexcept {
    RECT out{};
    out.left = static_cast<LONG>(r.x * scale);
    out.top = static_cast<LONG>(r.y * scale);
    out.right = static_cast<LONG>((r.x + r.w) * scale);
    out.bottom = static_cast<LONG>((r.y + r.h) * scale);
    out.left = std::max<LONG>(0, std::min<LONG>(out.left, static_cast<LONG>(surf_w)));
    out.top = std::max<LONG>(0, std::min<LONG>(out.top, static_cast<LONG>(surf_h)));
    out.right = std::max<LONG>(0, std::min<LONG>(out.right, static_cast<LONG>(surf_w)));
    out.bottom = std::max<LONG>(0, std::min<LONG>(out.bottom, static_cast<LONG>(surf_h)));
    return out;
}

} // namespace

DCompPresenter::~DCompPresenter() { teardown(); }

void DCompPresenter::teardown() noexcept {
    frame_bitmap_.Reset();
    release_surface();
    root_visual_.Reset();
    dcomp_target_.Reset();
    dcomp_device_.Reset();
    release_d2d();
    dxgi_device_.Reset();
    d3d_device_.Reset();
    hwnd_ = nullptr;
}

void DCompPresenter::release_surface() noexcept {
    if (surface_ != nullptr) {
        // Best-effort: discard any in-flight draw before releasing.
        surface_.Reset();
    }
}

void DCompPresenter::release_d2d() noexcept {
    if (context_ != nullptr) context_->SetTarget(nullptr);
    context_.Reset();
    d2d_device_.Reset();
}

bool DCompPresenter::create_device_resources() noexcept {
    // D3D11 device on the default adapter; we only need it as the rendering
    // backing for D2D, so the BGRA + render-target bind flags match the
    // documented D2D-on-DComp requirement.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1,
    };
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dCtx;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    flags, levels, static_cast<UINT>(std::size(levels)),
                                    D3D11_SDK_VERSION,
                                    d3d_device_.GetAddressOf(),
                                    nullptr,
                                    d3dCtx.GetAddressOf());
    if (FAILED(hr)) {
        // WARP fallback for headless / no-GPU environments.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                               levels, static_cast<UINT>(std::size(levels)),
                               D3D11_SDK_VERSION,
                               d3d_device_.GetAddressOf(),
                               nullptr,
                               d3dCtx.GetAddressOf());
        if (FAILED(hr)) return false;
    }
    d3dCtx.Reset();   // not needed; the D2D device owns the rendering context

    hr = d3d_device_.As(&dxgi_device_);
    if (FAILED(hr)) return false;

    // D2D1CreateDevice creates the device + a paired ID2D1Factory internally;
    // we only need the device + its context for drawing.
    hr = D2D1CreateDevice(dxgi_device_.Get(), nullptr, d2d_device_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                          context_.GetAddressOf());
    if (FAILED(hr)) return false;

    // DIP == surface pixel: keep the context at 96 DPI so the renderer's
    // Scale(dpi/96) world transform maps logical units to physical pixels
    // exactly as the legacy HwndRenderTarget path did.
    context_->SetDpi(96.0f, 96.0f);
    return true;
}

bool DCompPresenter::create_surface(unsigned w, unsigned h) noexcept {
    if (w == 0 || h == 0) return false;
    HRESULT hr = dcomp_device_->CreateSurface(w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                                              DXGI_ALPHA_MODE_PREMULTIPLIED,
                                              surface_.GetAddressOf());
    if (FAILED(hr)) return false;
    if (root_visual_ != nullptr) root_visual_->SetContent(surface_.Get());
    return true;
}

bool DCompPresenter::create_composition() noexcept {
    HRESULT hr = DCompositionCreateDevice(dxgi_device_.Get(), __uuidof(IDCompositionDevice),
                                           reinterpret_cast<void**>(dcomp_device_.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = dcomp_device_->CreateTargetForHwnd(hwnd_, /*topmost*/ FALSE,
                                            dcomp_target_.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = dcomp_device_->CreateVisual(root_visual_.GetAddressOf());
    if (FAILED(hr)) return false;

    if (!create_surface(width_px_, height_px_)) return false;

    hr = dcomp_target_->SetRoot(root_visual_.Get());
    if (FAILED(hr)) return false;

    hr = dcomp_device_->Commit();
    return SUCCEEDED(hr);
}

bool DCompPresenter::create(HWND hwnd) noexcept {
    hwnd_ = hwnd;
    if (hwnd_ == nullptr) return false;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    width_px_ = static_cast<unsigned>(rc.right - rc.left);
    height_px_ = static_cast<unsigned>(rc.bottom - rc.top);
    if (width_px_ == 0) width_px_ = 1;
    if (height_px_ == 0) height_px_ = 1;

    const UINT dpi_x = GetDpiForWindow(hwnd_);
    dpi_ = dpi_x != 0 ? static_cast<float>(dpi_x) : 96.0f;

    if (!create_device_resources()) return false;
    if (!create_composition()) return false;
    return true;
}

void DCompPresenter::resize(unsigned width_px, unsigned height_px) noexcept {
    width_px_ = width_px == 0 ? 1 : width_px;
    height_px_ = height_px == 0 ? 1 : height_px;
    if (dcomp_device_ == nullptr) return;
    release_surface();
    if (!create_surface(width_px_, height_px_)) return;
    dcomp_device_->Commit();
}

void DCompPresenter::set_dpi(float dpi) noexcept {
    dpi_ = dpi > 0.0f ? dpi : 96.0f;
}

bool DCompPresenter::rebuild_d2d() noexcept {
    // Device-lost recovery: recreate the D2D device/context and surface but
    // keep the DComp target/visual (they are independent of the D2D device).
    release_d2d();
    release_surface();
    if (!create_device_resources()) return false;
    if (!create_surface(width_px_, height_px_)) return false;
    if (root_visual_ != nullptr) root_visual_->SetContent(surface_.Get());
    if (dcomp_device_ != nullptr) dcomp_device_->Commit();
    return true;
}

SurfaceFrame DCompPresenter::begin_frame(const Rect* dirty) noexcept {
    SurfaceFrame frame;
    if (!valid() || hwnd_ == nullptr) return frame;

    const float scale = dpi_ / 96.0f;
    Rect dirty_logical = (dirty != nullptr) ? *dirty : Rect{};
    if (dirty_logical.w <= 0.0f || dirty_logical.h <= 0.0f) {
        dirty_logical = Rect{0.0f, 0.0f, static_cast<float>(width_px_),
                             static_cast<float>(height_px_)};
    }

    const RECT phys = to_physical_rect(dirty_logical, scale, width_px_, height_px_);
    if (phys.right <= phys.left || phys.bottom <= phys.top) return frame;

    Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
    POINT updateOffset{};
    HRESULT hr = surface_->BeginDraw(&phys, IID_PPV_ARGS(&dxgiSurface), &updateOffset);
    if (FAILED(hr)) {
        if (!rebuild_d2d() || surface_ == nullptr) return frame;
        hr = surface_->BeginDraw(&phys, IID_PPV_ARGS(&dxgiSurface), &updateOffset);
        if (FAILED(hr)) return frame;
    }

    frame_bitmap_.Reset();
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = context_->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bp,
                                               frame_bitmap_.GetAddressOf());
    if (FAILED(hr)) {
        surface_->EndDraw();
        return frame;
    }
    context_->SetTarget(frame_bitmap_.Get());

    drew_this_frame_ = true;
    frame.rt = context_.Get();   // ID2D1DeviceContext is-a ID2D1RenderTarget
    frame.dirty = dirty_logical;
    frame.offsetX = updateOffset.x;
    frame.offsetY = updateOffset.y;
    return frame;
}

void DCompPresenter::end_frame() noexcept {
    if (!drew_this_frame_) return;
    drew_this_frame_ = false;

    const HRESULT hr = context_->EndDraw();
    context_->SetTarget(nullptr);
    frame_bitmap_.Reset();
    surface_->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET) {
        rebuild_d2d();
        // Rebuild already commits; fall through to a final commit regardless.
    }
    if (dcomp_device_ != nullptr) dcomp_device_->Commit();
}

bool DCompPresenter::valid() const noexcept {
    return dcomp_device_ != nullptr && surface_ != nullptr && context_ != nullptr;
}

} // namespace flux