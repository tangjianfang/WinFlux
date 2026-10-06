// WinFlux host lifecycle test — the core-path mapped check for harness
// finding #2. Exercises the part of the Host surface that is not headless:
// window class registration, HWND creation, presenter selection (DComp
// with HwndRenderTarget fallback), root-builder wiring, a render round-trip,
// and clean teardown. A second CTest target keeps any GUI-session dependency
// isolated from the always-fast smoke test.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include <d3d11.h>
#include <d2d1_1.h>
#include <wrl/client.h>

#include <flux/flux.hpp>

using namespace flux;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) {
        std::printf("[PASS] %s\n", what);
    } else {
        std::printf("[FAIL] %s\n", what);
        ++failures;
    }
}

    class RecordingPresenter final : public IPresenter {
    public:
        bool create(HWND) noexcept override {
            D3D_FEATURE_LEVEL fl{};
            if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                         D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                         nullptr, 0, D3D11_SDK_VERSION,
                                         &d3d_, &fl, nullptr))) {
                return false;
            }
            if (FAILED(d3d_.As(&dxgi_))) return false;
            // D2D1CreateDevice builds the device + factory pair in one call.
            if (FAILED(D2D1CreateDevice(dxgi_.Get(), nullptr,
                                        d2d_device_.GetAddressOf()))) {
                return false;
            }
            if (FAILED(d2d_device_->CreateDeviceContext(
                           D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) {
                return false;
            }
            const D2D1_BITMAP_PROPERTIES1 target_props =
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                          D2D1_ALPHA_MODE_PREMULTIPLIED),
                                        96.0f, 96.0f);
            if (FAILED(dc_->CreateBitmap(D2D1::SizeU(320, 240), nullptr, 0, &target_props,
                                         target_.GetAddressOf()))) {
                return false;
            }
            const D2D1_BITMAP_PROPERTIES1 read_props =
                D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                      D2D1_ALPHA_MODE_PREMULTIPLIED),
                    96.0f, 96.0f);
            return SUCCEEDED(dc_->CreateBitmap(D2D1::SizeU(320, 240), nullptr, 0,
                                               &read_props, staging_.GetAddressOf()));
        }
        SurfaceFrame begin_frame(const Rect* dirty) noexcept override {
            ++frames;
            if (dirty != nullptr) rects.push_back(*dirty);
            // The renderer calls BeginDraw on the returned rt itself — a
            // presenter only prepares the target. The phys* fields feed the
            // renderer's axis-aligned clip (DXGI pixels); at this target's
            // fixed 96 DPI they equal the logical dirty rect.
            dc_->SetTarget(target_.Get());
            SurfaceFrame f;
            f.rt = dc_.Get();
            f.dirty = dirty != nullptr ? *dirty : Rect{};
            if (dirty != nullptr) {
                f.physLeft = static_cast<int>(dirty->x);
                f.physTop = static_cast<int>(dirty->y);
                f.physWidth = static_cast<unsigned>(dirty->w);
                f.physHeight = static_cast<unsigned>(dirty->h);
            } else {
                f.physWidth = 320;
                f.physHeight = 240;
            }
            return f;
        }
        void end_frame() noexcept override {
            // The renderer already called EndDraw on the context. Mirror the
            // target into the CPU-readable staging bitmap for assertions.
            const D2D1_POINT_2U dst{0, 0};
            const D2D1_RECT_U src{0, 0, 320, 240};
            (void)staging_->CopyFromRenderTarget(&dst, dc_.Get(), &src);
            dc_->SetTarget(nullptr);
        }
        void resize(unsigned, unsigned) noexcept override {}
        void set_dpi(float) noexcept override {}
        [[nodiscard]] bool valid() const noexcept override { return dc_ != nullptr; }
        // For pixel-level assertions (F10 round-trip): map AFTER end_frame.
        [[nodiscard]] ID2D1Bitmap1* staging() noexcept { return staging_.Get(); }

        unsigned frames = 0;
        std::vector<Rect> rects;

    private:
        Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_;
        Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> staging_;
    };

} // namespace

int main() {
    enable_per_monitor_dpi_v2();

    // CI runners can't guarantee an interactive window station (D2D device
    // creation fails in a service session); set WINFLUX_SKIP_GUI_TESTS=1 to
    // opt this target out there. The headless smoke target always runs.
    if (std::getenv("WINFLUX_SKIP_GUI_TESTS") != nullptr) {
        std::printf("WinFluxHostLifecycleTest: skipped (WINFLUX_SKIP_GUI_TESTS set)\n");
        return 0;
    }

    Host host;
    Host::Config cfg;
    cfg.title = L"WinFlux host lifecycle test";
    cfg.width = 320;
    cfg.height = 240;

    const bool created = host.create(cfg, GetModuleHandleW(nullptr));
    check(created, "Host::create returns true (presenter selected, HWND live)");
    check(host.hwnd() != nullptr, "Host::hwnd() is non-null after create");

    // A trivial root builder so request_render has something to draw.
    host.set_root_builder([] {
        Props p;
        p.background = rgb(245, 245, 240);
        p.padding = {16, 16, 16, 16};
        return with_children(view(std::move(p)), {label(L"lifecycle OK")});
    });

    host.request_render();   // drives a presenter frame through Host::paint_frame
    check(host.hwnd() != nullptr, "Host survives a paint round-trip");

    // Round 2 coverage: a scroll_view (20 rows) plus a declarative overlay
    // (scrim + centred card) declared from the root builder every frame.
    host.set_root_builder([&host] {
        auto content = view();
        for (int i = 0; i < 20; ++i) {
            content->children.push_back(label(L"row " + std::to_wstring(i)));
        }
        Props sp;
        sp.width = 200.0f;
        sp.height = 150.0f;
        sp.background = rgb(255, 255, 255);
        auto sv = scroll_view(std::move(content), std::move(sp));

        Props rp;
        rp.padding = {16, 16, 16, 16};
        auto root = with_children(view(std::move(rp)), {std::move(sv)});

        Props scrim;
        scrim.background = rgba(0, 0, 0, 96);   // scrim: blocks + click-through-children
        scrim.direction = Direction::column;
        scrim.justify = Justify::center;
        scrim.align = Align::center;
        Props card_p;
        card_p.background = rgb(255, 255, 255);
        card_p.width = 120.0f;
        card_p.height = 48.0f;
        card_p.radius = 8.0f;
        auto card = label(L"overlay", std::move(card_p));
        host.overlay(with_children(view(std::move(scrim)), {std::move(card)}));
        return root;
    });
    host.request_render();   // overlay set changes 0 -> 1: full dirty path
    host.request_render();   // same shape: overlay diff path
    check(host.hwnd() != nullptr, "Host survives scroll_view + overlay round-trips");

    // 06-R4 headless interaction net: the injected WARP recording presenter
    // (RecordingPresenter above) records every dirty rect while the full
    // build/layout/diff pipeline runs against a real software render target —
    // the recorded rects ARE the damage assertions, driven through real
    // messages (hover partiality, click dispatch, Tab focus).
    {

        Host test_host;
        Host::Config test_cfg;
        test_cfg.title = L"recording";
        test_cfg.width = 320;
        test_cfg.height = 240;
        RecordingPresenter rec;
        check(rec.create(nullptr),
              "WARP recording presenter initializes (test-side create)");
        bool clicked = false;
        test_host.set_root_builder([&test_host, &clicked] {
            Props root_p;
            root_p.direction = Direction::column;
            root_p.gap = 8.0f;
            root_p.padding = {16, 16, 16, 16};
            root_p.align = Align::start;   // natural child sizes (no stretch)
            auto root = view(std::move(root_p));
            Props one_p;
            one_p.width = 140.0f;
            one_p.height = 32.0f;
            root->children.push_back(button(L"Click me one",
                                            [&clicked] { clicked = true; },
                                            std::move(one_p)));
            Props two_p;
            two_p.width = 140.0f;
            two_p.height = 32.0f;
            root->children.push_back(button(L"Click me two", [] {}, std::move(two_p)));
            return root;
        });
        check(test_host.create(test_cfg, GetModuleHandleW(nullptr), &rec),
              "create accepts an external presenter (06-R4 seam)");
        rec.rects.clear();   // drop the baseline (fullscreen) paint

        // Hover over the first button: damage must be PARTIAL (the button's
        // rect), never the whole client area.
        SendMessageW(test_host.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(60, 30));
        check(!rec.rects.empty(), "hover produces a damage rect");
        {
            bool all_partial = true;
            for (const Rect& r : rec.rects) {
                if (r.w * r.h > 320.0f * 240.0f * 0.9f) all_partial = false;
            }
            check(all_partial, "hover damage is partial, not fullscreen");
        }

        // Real click through WM_LBUTTONDOWN/UP dispatches on_click.
        SendMessageW(test_host.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(60, 30));
        SendMessageW(test_host.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(60, 30));
        check(clicked, "button on_click fires through the message path");

        // Tab moves focus to the second button: partial damage again.
        rec.rects.clear();
        SendMessageW(test_host.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        check(!rec.rects.empty(), "Tab repaints the focus ring change");
        {
            bool all_partial = true;
            for (const Rect& r : rec.rects) {
                if (r.w * r.h > 320.0f * 240.0f * 0.9f) all_partial = false;
            }
            check(all_partial, "focus change damage is partial");
        }
    }

    // F10 image round-trip at the PIXEL level: encode an 8x8 red PNG with
    // WIC, render it through an image element into the WARP target, and
    // read the target back — the decoded image must actually paint.
    {
        const std::wstring png = (std::filesystem::temp_directory_path()
                                  / L"winflux_f10_red.png").wstring();
        bool wrote = false;
        {
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           __uuidof(IWICImagingFactory),
                                           reinterpret_cast<void**>(factory.GetAddressOf())))) {
                Microsoft::WRL::ComPtr<IWICStream> stream;
                Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
                Microsoft::WRL::ComPtr<IWICBitmap> bmp;
                HRESULT ha = factory->CreateStream(stream.GetAddressOf());
                HRESULT hb = stream->InitializeFromFilename(png.c_str(), GENERIC_WRITE);
                HRESULT hc = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                                    encoder.GetAddressOf());
                HRESULT hd = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
                HRESULT he = factory->CreateBitmap(8, 8, GUID_WICPixelFormat32bppBGRA,
                                                   WICBitmapCacheOnLoad, bmp.GetAddressOf());
                if (SUCCEEDED(ha) && SUCCEEDED(hb) && SUCCEEDED(hc) && SUCCEEDED(hd)
                    && SUCCEEDED(he)) {
                    Microsoft::WRL::ComPtr<IWICBitmapLock> lock;
                    const WICRect all{0, 0, 8, 8};
                    const HRESULT hl = bmp->Lock(&all, WICBitmapLockWrite,
                                                 lock.GetAddressOf());
                    if (SUCCEEDED(hl)) {
                        UINT cb = 0, stride = 0;
                        BYTE* px = nullptr;
                        lock->GetStride(&stride);
                        lock->GetDataPointer(&cb, &px);
                        std::vector<BYTE> rows(static_cast<size_t>(stride) * 8, 0);
                        for (int y = 0; y < 8; ++y) {
                            for (int x = 0; x < 8; ++x) {
                                BYTE* p = px + y * stride + x * 4;
                                p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255;   // opaque red
                                (void)p;
                            }
                            std::memcpy(rows.data() + static_cast<size_t>(y) * stride,
                                        px + y * stride, 8 * 4);
                        }
                        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
                        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
                        wrote = SUCCEEDED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))
                             && SUCCEEDED(frame->Initialize(nullptr))
                             && SUCCEEDED(frame->SetSize(8, 8))
                             && SUCCEEDED(frame->SetPixelFormat(&pf))
                             && SUCCEEDED(frame->WritePixels(8, stride,
                                                            static_cast<UINT>(rows.size()),
                                                            rows.data()))
                             && SUCCEEDED(frame->Commit())
                             && SUCCEEDED(encoder->Commit());
                        (void)bmp;   // pixels travel through WritePixels
                    }
                }
            }
        }
        if (wrote) {
            Host img_host;
            Host::Config img_cfg;
            img_cfg.title = L"image";
            img_cfg.width = 200;
            img_cfg.height = 150;
            RecordingPresenter rec2;
            check(rec2.create(nullptr), "second WARP presenter initializes");
            img_host.set_root_builder([&png] {
                Props rp;
                rp.background = rgb(255, 255, 255);
                rp.padding = {20, 20, 20, 20};
                Props ip;
                ip.width = 100.0f;
                ip.height = 100.0f;
                return with_children(view(std::move(rp)),
                                     {image(png, std::move(ip))});
            });
            check(img_host.create(img_cfg, GetModuleHandleW(nullptr), &rec2),
                  "image host creates");
            img_host.request_render();
            D2D1_MAPPED_RECT mapped{};
            if (SUCCEEDED(rec2.staging()->Map(D2D1_MAP_OPTIONS_READ, &mapped))) {
                int red = 0;
                for (UINT y = 0; y < 150; ++y) {
                    for (UINT x = 0; x < 200; ++x) {
                        const BYTE* p = mapped.bits + y * mapped.pitch + x * 4;
                        if (p[2] > 200 && p[1] < 100 && p[0] < 100) ++red;
                    }
                }
                rec2.staging()->Unmap();
                check(red > 50, "image element paints decoded pixels (F10)");
            } else {
                check(false, "staging bitmap readable for the pixel check (F10)");
            }
        } else {
            check(false, "WIC PNG encode for the F10 round-trip");
        }
        std::error_code ec;
        std::filesystem::remove(png, ec);
    }

    // F6: IME messages with no real IME / no focused input must be inert
    // (synthetic composition carries no ImmGetContext data — the handler
    // degrades to a no-op instead of touching state).
    {
        SendMessageW(host.hwnd(), WM_IME_SETCONTEXT, TRUE, ISC_SHOWUIALL);
        SendMessageW(host.hwnd(), WM_IME_STARTCOMPOSITION, 0, 0);
        SendMessageW(host.hwnd(), WM_IME_COMPOSITION, 0, GCS_COMPSTR);
        SendMessageW(host.hwnd(), WM_IME_ENDCOMPOSITION, 0, 0);
        SendMessageW(host.hwnd(), WM_IME_NOTIFY, IMN_SETCOMPOSITIONWINDOW, 0);
        check(host.hwnd() != nullptr, "IME message paths are inert without an IME");
    }

    // F1 text-input round-trip through the REAL message path: autofocus
    // steers focused_input_path_ during paint_frame, then WM_CHAR /
    // VK_BACK dispatch synchronously (SendMessage) into handle_char /
    // handle_key_down and must surface in the app-side string.
    {
        std::wstring typed;
        host.set_root_builder([&host, &typed] {
            Props ip;
            ip.autofocus = true;
            ip.placeholder = L"type here";
            // Like a real app, the value round-trips through app state —
            // the rebuilt tree re-renders the current text.
            auto input = text_input(typed,
                                    [&typed](std::wstring v) { typed = std::move(v); },
                                    std::move(ip));
            Props rp;
            rp.padding = {16, 16, 16, 16};
            return with_children(view(std::move(rp)), {std::move(input)});
        });
        host.request_render();   // builder + autofocus apply here
        SendMessageW(host.hwnd(), WM_CHAR, L'A', 0);
        SendMessageW(host.hwnd(), WM_CHAR, L'B', 0);
        SendMessageW(host.hwnd(), WM_CHAR, L'C', 0);
        check(typed == L"ABC", "WM_CHAR appends through on_text_change");
        SendMessageW(host.hwnd(), WM_KEYDOWN, VK_BACK, 0);
        check(typed == L"AB", "VK_BACK deletes the trailing character");
        SendMessageW(host.hwnd(), WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(host.hwnd(), WM_KEYDOWN, VK_DELETE, 0);
        check(typed == L"B", "Home + Delete removes the leading character");
    }

    // Exit path probe (review 2026-10-06 R-3): WM_CLOSE must destroy the
    // window AND end the message loop (WM_DESTROY -> PostQuitMessage). A
    // watchdog thread posts a fallback WM_QUIT after 5 s so a regression
    // shows up as a failed check, not a hung test.
    {
        const DWORD main_tid = GetCurrentThreadId();
        std::thread watchdog([main_tid] {
            Sleep(5000);
            PostThreadMessageW(main_tid, WM_QUIT, 0, 0);
        });
        watchdog.detach();
        const ULONGLONG t0 = GetTickCount64();
        PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
        const int loop_result = host.run();
        const ULONGLONG elapsed = GetTickCount64() - t0;
        check(loop_result == 0, "run() returns 0 after WM_CLOSE");
        check(elapsed < 4500, "message loop exits promptly (WM_DESTROY posts WM_QUIT)");
    }

    // Destructor destroys the HWND and tears down the presenter chain.
    if (failures == 0) {
        std::printf("WinFlux host lifecycle: all checks passed.\n");
        return 0;
    }
    std::printf("WinFlux host lifecycle: %d check(s) failed.\n", failures);
    return 1;
}