// WinFlux host lifecycle test — the core-path mapped check for harness
// finding #2. Exercises the part of the Host surface that is not headless:
// window class registration, HWND creation, presenter selection (DComp
// with HwndRenderTarget fallback), root-builder wiring, a render round-trip,
// and clean teardown. A second CTest target keeps any GUI-session dependency
// isolated from the always-fast smoke test.

#include <cstdio>
#include <cstdlib>
#include <thread>

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
