#include <flux/Host.hpp>

#include <windowsx.h>
#include <imm.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <flux/Accessibility.hpp>
#include <flux/Clipboard.hpp>

#include <flux/FlexLayout.hpp>
#include <flux/flux.hpp>

namespace flux {

// =====================================================================
// Component builders
// =====================================================================

namespace {
ElementPtr make(std::wstring type, Props p) {
    auto e = std::make_shared<Element>();
    e->type = std::move(type);
    e->props = std::move(p);
    return e;
}
} // namespace

ElementPtr view(Props p) { return make(L"view", std::move(p)); }

ElementPtr label(std::wstring text, Props p) {
    p.text = std::move(text);
    p.text_align = Align::start;
    return make(L"label", std::move(p));
}

ElementPtr button(std::wstring text, ClickHandler on_click, Props p) {
    p.text = std::move(text);
    p.on_click = std::move(on_click);
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.padding.left == 0.0f && p.padding.right == 0.0f) {
        p.padding.left = static_cast<float>(tokens::spacing_md);
        p.padding.right = static_cast<float>(tokens::spacing_md);
    }
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    if (!p.text_color.has_value()) p.text_color = Color{255, 255, 255};
    p.cursor = Cursor::hand;
    return make(L"button", std::move(p));
}

ElementPtr checkbox(std::wstring text, bool checked, std::function<void(bool)> on_change, Props p) {
    p.text = std::move(text);
    p.checked = checked;
    p.focusable = true;
    auto handler = std::move(on_change);
    p.on_click = [handler, checked] {
        if (handler) handler(!checked);
    };
    return make(L"checkbox", std::move(p));
}

ElementPtr progress(float value, Props p) {
    p.progress_value = std::clamp(value, 0.0f, 1.0f);
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::progress_height);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_sm);
    return make(L"progress", std::move(p));
}

ElementPtr image(std::wstring path, Props p) {
    // F10: WIC-decoded file, drawn aspect-fit. Intrinsic size comes from the
    // decoded dimensions (see measure_leaf); an undecodable path measures
    // 64x64 and draws nothing.
    p.image_path = std::move(path);
    return make(L"image", std::move(p));
}

ElementPtr icon(IconKind kind, Props p) {
    p.icon = kind;
    if (!p.width.has_value()) p.width = 24.0f;
    if (!p.height.has_value()) p.height = 24.0f;
    return make(L"icon", std::move(p));
}

ElementPtr scroll_view(ElementPtr content, Props p) {
    p.focusable = true;
    auto e = make(L"scroll_view", std::move(p));
    if (content != nullptr) e->children.push_back(std::move(content));
    return e;
}

ElementPtr text_input(std::wstring value, std::function<void(std::wstring)> on_change, Props p) {
    // The text input owns its visual chrome. The change handler is stored in
    // on_text_change; the host's wndproc dispatches WM_CHAR/VK_BACK to the
    // element at focused_input_path_ by calling that handler with the new
    // text. A click focuses the input — see handle_lbutton_down.
    p.text = std::move(value);
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.padding.left == 0.0f) p.padding.left = static_cast<float>(tokens::spacing_md);
    if (p.padding.right == 0.0f) p.padding.right = static_cast<float>(tokens::spacing_sm);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    if (!p.background.has_value()) p.background = std::optional<Color>{};
    if (!p.border_color.has_value()) p.border_color = std::optional<Color>{};
    if (p.border_width == 0.0f) p.border_width = 1.0f;
    if (!p.text_color.has_value()) p.text_color = std::optional<Color>{};
    if (p.text_align == Align::center) p.text_align = Align::start;
    p.cursor = Cursor::ibeam;
    p.on_text_change = std::move(on_change);
    return make(L"text_input", std::move(p));
}

ElementPtr slider(float value, float vmin, float vmax,
                  std::function<void(float)> on_change, Props p) {
    p.slider_min = vmin;
    p.slider_max = vmax;
    if (vmax > vmin) {
        p.slider_value = (std::clamp(value, vmin, vmax) - vmin) / (vmax - vmin);
    } else {
        p.slider_value = 0.0f;
    }
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    // Click + drag both route through on_value_change: handle_lbutton_down
    // and the mouse_move-while-pressed branch both compute the normalised x
    // and invoke this handler with the domain value.
    p.cursor = Cursor::size_we;
    p.on_value_change = std::move(on_change);
    return make(L"slider", std::move(p));
}

ElementPtr toggle_switch(bool on, std::function<void(bool)> on_change, Props p) {
    p.checked = on;
    p.focusable = true;
    if (!p.height.has_value()) p.height = 24.0f;
    if (!p.width.has_value()) p.width = 44.0f;
    p.on_click = [handler = std::move(on_change), on] {
        if (handler) handler(!on);
    };
    return make(L"toggle", std::move(p));
}

ElementPtr with_children(ElementPtr parent, std::initializer_list<ElementPtr> children) {
    if (parent != nullptr) {
        parent->children.reserve(parent->children.size() + children.size());
        for (const auto& c : children) parent->children.push_back(c);
    }
    return parent;
}

// =====================================================================
// Host
// =====================================================================

namespace {
constexpr wchar_t kClassName[] = L"WinFluxHost";

float ease_in_out(float t) noexcept { return t * t * (3.0f - 2.0f * t); }

// RAII guard for the painting_ re-entrancy flag. paint_frame may throw (a
// user builder or paint callback escaping); wndproc catches at the boundary.
// This guarantees the flag resets on every exit path (gen-check early-return,
// dirty_rects.empty() return, normal completion, or a throw) without
// scattering `painting_ = false` at each return site.
struct PaintScope {
    bool& flag;
    explicit PaintScope(bool& f) noexcept : flag(f) { flag = true; }
    ~PaintScope() noexcept { flag = false; }
    PaintScope(const PaintScope&) = delete;
    PaintScope& operator=(const PaintScope&) = delete;
};

// Inline QueryPerformanceCounter helpers; one source for the cast so the
// rest of the code reads as `qpc()` -> long long. Compiled in ALL configs:
// measurement is a handful of QPC calls, and WINFLUX_PERF_LOG needs it in
// release builds (where the numbers are honest).
// Debug HUD switch: opt-in via WINFLUX_DEBUG_HUD=1 (any build, zero cost
// when unset — one getenv at first paint).
[[nodiscard]] inline bool debug_hud_enabled() noexcept {
    static const bool on = [] {
        const char* e = std::getenv("WINFLUX_DEBUG_HUD");
        return e != nullptr && e[0] == '1';
    }();
    return on;
}

inline long long qpc() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline double qpc_to_ms(long long freq, long long delta) noexcept {
    return freq > 0 ? (static_cast<double>(delta) * 1000.0 / static_cast<double>(freq))
                    : 0.0;
}
} // namespace

LRESULT CALLBACK host_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    Host* host = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        host = cs != nullptr ? static_cast<Host*>(cs->lpCreateParams) : nullptr;
        if (host != nullptr) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
            host->hwnd_ = hwnd;   // bind before any further messages arrive
        }
    } else {
        host = reinterpret_cast<Host*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (host != nullptr) {
        try {
            switch (msg) {
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd, &ps);
                host->paint_frame();
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;   // D2D clears the whole surface each frame
            case WM_SIZE:
                host->handle_size(static_cast<unsigned>(LOWORD(lparam)),
                                  static_cast<unsigned>(HIWORD(lparam)));
                return 0;
            case WM_DPICHANGED: {
                const UINT dpi = HIWORD(wparam);
                const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
                if (suggested != nullptr) host->handle_dpi_changed(dpi, *suggested);
                return 0;
            }
            case WM_MOUSEMOVE:
                host->handle_mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_MOUSELEAVE:
                host->handle_mouse_leave();
                return 0;
            case WM_LBUTTONDOWN:
                host->handle_lbutton_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_LBUTTONUP:
                host->handle_lbutton_up(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_KEYDOWN:
                host->handle_key_down(wparam);
                return 0;
            case WM_CHAR:
                host->handle_char(wparam);
                return 0;
            case WM_IME_SETCONTEXT:
                // Let the IME run, but suppress the default composition
                // window — the composition text renders inline in the
                // focused text_input (F6).
                return DefWindowProcW(hwnd, msg, wparam & ~ISC_SHOWUICOMPOSITIONWINDOW,
                                      lparam);
            case WM_IME_STARTCOMPOSITION:
                return 0;   // no default composition window
            case WM_IME_COMPOSITION:
                host->handle_ime_composition(lparam);
                return 0;
            case WM_IME_ENDCOMPOSITION:
                host->handle_ime_end_composition();
                return 0;
            case WM_IME_NOTIFY:
                if (wparam == IMN_SETCOMPOSITIONWINDOW) {
                    host->handle_ime_position();
                }
                return 0;
            case WM_MOUSEWHEEL:
                host->handle_mouse_wheel(static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)),
                                         GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_CAPTURECHANGED:
                host->drag_scroll_.clear();
                return 0;
            case WM_CONTEXTMENU: {
                const int sx = GET_X_LPARAM(lparam);
                const int sy = GET_Y_LPARAM(lparam);
                host->handle_contextmenu(sx, sy);
                return 0;
            }
            case WM_TIMER:
                if (wparam == Host::anim_timer_id_) host->handle_timer();
                return 0;
            case WM_SETCURSOR:
                // Per-element cursor while over the client area; everything
                // else (borders, title bar) keeps the default handling.
                if (LOWORD(lparam) == HTCLIENT) {
                    host->update_cursor();
                    return TRUE;
                }
                break;
            case WM_CLOSE:
                // Veto hook runs while the window is still alive — the
                // save-on-exit spot (F5). false keeps the window open.
                if (host->close_handler_) {
                    const auto handler = host->close_handler_;   // copy: the handler may rebuild state
                    if (!handler()) return 0;
                }
                break;
            case WM_GETOBJECT:
                // UIA L0 (X-12): hand screen readers a window-level provider
                // so they read the window name instead of nothing.
                if (lparam == static_cast<LPARAM>(UiaRootObjectId)) {
                    if (auto* provider = a11y::window_provider(hwnd); provider != nullptr) {
                        return UiaReturnRawElementProvider(hwnd, wparam, lparam, provider);
                    }
                }
                break;
            case WM_DESTROY:
                a11y::shutdown_for(hwnd);
                // The window is gone: end run_message_loop's GetMessageW wait.
                // Without this every WinFlux process lingered as a zombie
                // after the user closed the window (review 2026-10-06 R-3).
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY:
                host->hwnd_ = nullptr;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            default:
                break;
            }
        } catch (...) {
            // A user callback (builder / on_click / props.paint) threw. The
            // scene may be half-updated; invalidate so the next paint
            // rebuilds from current state instead of trusting the last
            // baseline (R-6 — this catch used to be unreachable: every
            // callee was noexcept, so any throw terminated the process).
            (void)host->bump_generation();
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

Host::Host() noexcept { shown_ = flux::palette(mode_); }

Host::~Host() { destroy(); }

void Host::destroy() noexcept {
    flush_perf_log();   // emit any partial-window measurements (U-4)
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

bool Host::create(const Config& config, HINSTANCE instance,
                  IPresenter* external_presenter) noexcept {
    instance_ = instance;
    ui_thread_id_ = GetCurrentThreadId();   // single-thread invariant (R6)
    // WIC image decoding (F10) needs COM on the UI thread; D2D/DWrite/DComp
    // themselves do not. Balanced in destroy().
    com_initialized_ = SUCCEEDED(CoInitializeEx(
        nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &host_wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    if (RegisterClassExW(&wc) == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) return false;
    }

    // Logical-to-physical at creation: config sizes are logical (96 DPI).
    RECT rc{0, 0, config.width, config.height};
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    HWND hwnd = CreateWindowExW(0, kClassName, config.title.c_str(),
                                WS_OVERLAPPEDWINDOW,
                                config.x < 0 ? CW_USEDEFAULT : config.x,
                                config.y < 0 ? CW_USEDEFAULT : config.y,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, instance, this);
    if (hwnd == nullptr) return false;

    if (external_presenter != nullptr) {
        // Test seam (review 2026-10-06 06-R4): an externally-owned presenter
        // replaces the DComp/Hwnd pair. It must outlive the Host. A presenter
        // returning a null-rt SurfaceFrame still exercises the full
        // build/layout/diff pipeline — only the draw is skipped.
        presenter_ = external_presenter;
    } else if (dcomp_presenter_.create(hwnd)) {
        presenter_ = &dcomp_presenter_;
    } else if (hwnd_presenter_.create(hwnd)) {
        presenter_ = &hwnd_presenter_;
    } else {
        DestroyWindow(hwnd);
        hwnd_ = nullptr;
        return false;
    }
    renderer_.attach(*presenter_);
    // Initialize DPI at the create-time monitor DPI. Without this the renderer
    // paints at 96 DPI until the first WM_DPICHANGED arrives — which on most
    // machines is never (DPI only changes when the source monitor changes).
    renderer_.set_dpi(static_cast<float>(GetDpiForWindow(hwnd)));
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}

int Host::run() noexcept { return run_message_loop(); }

void Host::set_root_builder(std::function<ElementPtr()> builder) {
    root_builder_ = std::move(builder);
    request_render();
}

void Host::overlay(ElementPtr root) noexcept {
    // Only meaningful while the builder runs (paint_frame clears the set
    // before invoking it); a stray call outside a frame is a programming
    // error — release builds drop it, debug builds say so (R6).
    assert((painting_ || root == nullptr) &&
           "Host::overlay() must be called from inside the root builder");
    if (painting_ && root != nullptr) frame_overlays_.push_back(std::move(root));
}

void Host::request_render() {
    // Single-thread invariant: the scene tree, COM devices, and input paths
    // are UI-thread-only. A cross-thread set()/request_render is a latent
    // race — catch it in debug builds instead of wondering about the
    // intermittent corruption later (review 2026-10-06, R6).
    assert(ui_thread_id_ == 0 || GetCurrentThreadId() == ui_thread_id_);
    // Bump BEFORE the painting_ guard so a state mutation fired from inside
    // a click handler (which runs with painting_ == true and would otherwise
    // early-return here) still invalidates the next paint_frame's baseline.
    (void)bump_generation();
    if (hwnd_ == nullptr || painting_) return;
    paint_frame();   // drive the presenter directly (no InvalidateRect round-trip)
    if (hwnd_ != nullptr) ValidateRect(hwnd_, nullptr);   // clear dirty; avoid a double WM_PAINT
}

void Host::ensure_repaint_after_handler(Generation before) {
    // R-5: handlers that only mutate plain app structs (overlay hosts, for
    // instance) schedule no paint of their own — without this, an overlay
    // closed via Esc lingers as stale pixels until unrelated input. If the
    // handler already went through State::set/request_render, the generation
    // moved and this is a free no-op.
    if (dirty_generation_ == before) request_render();
}

void Host::set_theme_mode(ThemeMode mode) {
    if (mode == mode_ && !fading_) return;
    mode_ = mode;
    fade_from_ = shown_;
    fade_to_ = flux::palette(mode);
    fade_start_ms_ = GetTickCount64();
    fading_ = true;
    animate_for(static_cast<unsigned>(tokens::theme_fade_ms));
    request_render();
}

void Host::animate_for(unsigned ms) noexcept {
    if (ms == 0) return;
    const unsigned long long deadline = GetTickCount64() + ms + 1;
    if (deadline > anim_deadline_ms_) anim_deadline_ms_ = deadline;
    if (hwnd_ != nullptr) SetTimer(hwnd_, anim_timer_id_, anim_tick_ms_, nullptr);
}

void Host::layout_scroll_views(Element& e, const Path& prefix, const LayoutContext& ctx,
                               std::map<Path, ScrollState>& next) noexcept {
    if (e.type == L"scroll_view") {
        ScrollState st;
        if (const auto it = scrolls_.find(prefix); it != scrolls_.end()) st = it->second;
        st.content_h = layout_scroll_view(e, st.offset_y, ctx);
        st.viewport_h = std::max(0.0f, e.h - e.props.padding.top - e.props.padding.bottom);
        next.emplace(prefix, st);
    }
    for (size_t i = 0; i < e.children.size(); ++i) {
        Path child = prefix;
        child.push_back(static_cast<int>(i));
        layout_scroll_views(*e.children[i], child, ctx, next);
    }
}

void Host::scroll_by(const Path& sv_path, float delta) {
    auto it = scrolls_.find(sv_path);
    if (it == scrolls_.end()) return;
    const float max_off = std::max(0.0f, it->second.content_h - it->second.viewport_h);
    scroll_to(sv_path, std::clamp(it->second.offset_y + delta, 0.0f, max_off));
}

void Host::scroll_to(const Path& sv_path, float offset) {
    auto it = scrolls_.find(sv_path);
    if (it == scrolls_.end()) return;
    const float max_off = std::max(0.0f, it->second.content_h - it->second.viewport_h);
    const float next = std::clamp(offset, 0.0f, max_off);
    if (next == it->second.offset_y) return;
    it->second.offset_y = next;
    const unsigned long long now = GetTickCount64();
    it->second.last_scroll_ms = now;
    // Auto-hide: the bar is static (alpha 1) for a 600 ms hold, then fades
    // over motion_medium_ms. Only the fade needs frames — keep the timer
    // alive through the hold with WAKE-only ticks (no animating() => no
    // fullscreen dirty), then promote to real frames when the fade starts.
    scrollbar_fade_at_ms_ = now + 600u;
    scrollbar_fade_path_ = sv_path;   // damage pass dirties the bar rect while it fades
    if (hwnd_ != nullptr) SetTimer(hwnd_, anim_timer_id_, anim_tick_ms_, nullptr);
    // Virtualization hook: the scroll_view element (still this frame's
    // tree) observes the offset and can rebuild just the visible window.
    // Stack-copy first (R-2): the callback may rebuild the tree.
    if (const Element* sv = resolve(sv_path);
        sv != nullptr && sv->props.on_scroll) {
        const auto on_scroll = sv->props.on_scroll;
        on_scroll(next);
    }
    request_render();
}

void Host::handle_mouse_wheel(int delta, int px, int py) {
    // WM_MOUSEWHEEL delivers SCREEN coordinates; everything downstream is
    // client-logical.
    POINT pt{px, py};
    if (hwnd_ != nullptr) ScreenToClient(hwnd_, &pt);
    const float lx = to_logical_px(pt.x);
    const float ly = to_logical_px(pt.y);
    Path path;
    (void)hit_test_all(lx, ly, path);
    // Nearest scrollable ancestor of the point wins.
    for (Path walk = path; !walk.empty(); walk.pop_back()) {
        const auto it = scrolls_.find(walk);
        if (it == scrolls_.end()) continue;
        if (it->second.content_h > it->second.viewport_h) {
            // One notch (±120) scrolls three control rows.
            const float step = static_cast<float>(delta) / 120.0f *
                               3.0f * static_cast<float>(tokens::control_height_md);
            scroll_by(walk, -step);
            return;
        }
    }
}

void Host::update_tooltip() noexcept {
    // Tooltip is armed when the hovered element (or its nearest annotated
    // ancestor) has a tooltip_text and the pointer has dwelled since the
    // last hover change for motion_slow_ms.
    std::wstring text;
    for (Path walk = hover_path_; !walk.empty(); walk.pop_back()) {
        if (const Element* e = resolve(walk);
            e != nullptr && !e->props.tooltip_text.empty()) {
            text = e->props.tooltip_text;
            break;
        }
    }
    if (text.empty()) return;

    const unsigned long long now = GetTickCount64();
    const unsigned long long dwell =
        static_cast<unsigned long long>(tokens::motion_slow_ms);
    const unsigned long long elapsed = now - hover_change_ms_;
    if (elapsed < dwell) {
        // Not armed yet: schedule the re-check for the moment the dwell
        // completes (otherwise no later paint would ever show the tip).
        animate_for(static_cast<unsigned>(dwell - elapsed) + 2u);
        return;
    }
    // Armed: the card follows the cursor, so keep frames coming ONLY while
    // the pointer actually moves inside the same hover target — a parked
    // cursor must not self-perpetuate a 60fps fullscreen repaint loop.
    if (last_mouse_lx_ != tooltip_follow_lx_ || last_mouse_ly_ != tooltip_follow_ly_) {
        tooltip_follow_lx_ = last_mouse_lx_;
        tooltip_follow_ly_ = last_mouse_ly_;
        animate_for(64);
    }
    frame_overlays_.push_back(build_tooltip(text));
}

ElementPtr Host::build_tooltip(const std::wstring& text) const noexcept {
    // Transparent root (no background => click-through) anchored at the
    // cursor: spacer(h = y + 16) + row{spacer(w = x + 12), card}.
    const float anchor_y = last_mouse_ly_ + 16.0f;
    const float anchor_x = last_mouse_lx_ + 12.0f;

    Props root_p;
    root_p.direction = Direction::column;
    auto root = view(std::move(root_p));

    Props gap_p;
    gap_p.height = anchor_y;
    root->children.push_back(view(std::move(gap_p)));

    Props row_p;
    row_p.direction = Direction::row;
    auto row = view(std::move(row_p));
    Props indent_p;
    indent_p.width = anchor_x;
    row->children.push_back(view(std::move(indent_p)));

    Props card_p;
    card_p.background = shown_.surface;
    card_p.border_color = shown_.border;
    card_p.border_width = 1.0f;
    card_p.radius = static_cast<float>(tokens::radius_md);
    card_p.padding = EdgeInsets{6.0f, 8.0f, 6.0f, 8.0f};
    card_p.shadow_elevation = 2;
    card_p.text_color = shown_.text_secondary;
    card_p.font_size_pt = tokens::font_caption;
    card_p.text_align = Align::start;
    row->children.push_back(label(text, std::move(card_p)));

    root->children.push_back(std::move(row));
    return root;
}

void Host::handle_timer() {
    const unsigned long long now = GetTickCount64();
    if (fading_) {
        const float t = static_cast<float>(now - fade_start_ms_) /
                        static_cast<float>(tokens::theme_fade_ms);
        if (t >= 1.0f) {
            shown_ = fade_to_;
            fading_ = false;
        } else {
            shown_ = lerp_palette(fade_from_, fade_to_, ease_in_out(t));
        }
    }
    // Scrollbar auto-hide hold elapsed: start the fade frames.
    if (scrollbar_fade_at_ms_ != 0 && now >= scrollbar_fade_at_ms_) {
        scrollbar_fade_at_ms_ = 0;
        animate_for(static_cast<unsigned>(tokens::motion_medium_ms) + 32u);
    }
    const bool frame_needed = fading_ || now < anim_deadline_ms_;
    const bool more = frame_needed || scrollbar_fade_at_ms_ != 0;
    if (!more && hwnd_ != nullptr) {
        KillTimer(hwnd_, anim_timer_id_);
    }
    // Wake-only ticks (scrollbar hold) deliberately render nothing.
    if (frame_needed) request_render();   // next tween step, or the settle frame
}

float Host::to_logical_px(int px) const noexcept {
    const float dpi = renderer_.dpi();
    return dpi > 0.0f ? static_cast<float>(px) * 96.0f / dpi : static_cast<float>(px);
}

void Host::handle_size(unsigned width_px, unsigned height_px) {
    renderer_.resize(width_px, height_px);
    force_full_dirty_ = true;   // surface geometry changed — diff is meaningless
    request_render();
}

void Host::handle_dpi_changed(unsigned dpi, const RECT& suggested) {
    renderer_.set_dpi(static_cast<float>(dpi));
    if (hwnd_ != nullptr) {
        SetWindowPos(hwnd_, nullptr, suggested.left, suggested.top,
                     suggested.right - suggested.left, suggested.bottom - suggested.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    force_full_dirty_ = true;   // DPI change invalidates every primitive's size
    request_render();
}

// =====================================================================
// Hit testing + interaction paths
// =====================================================================

bool Host::contains(const Element& e, float lx, float ly) noexcept {
    return lx >= e.x && ly >= e.y && lx <= e.x + e.w && ly <= e.y + e.h;
}

bool Host::is_prefix(const Path& prefix, const Path& full) noexcept {
    if (prefix.size() > full.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] != full[i]) return false;
    }
    return true;
}

bool Host::rects_intersect(const Rect& a, const Rect& b) noexcept {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

void Host::update_cursor() noexcept {
    if (tree_ == nullptr) return;
    Path path;
    (void)hit_test_all(last_mouse_lx_, last_mouse_ly_, path);
    Cursor want = Cursor::arrow;
    // Nearest annotated ancestor wins (a label inside a button shows the
    // button's cursor). Disabled elements show the arrow — a hand cursor
    // over a dead control promises interaction it can't deliver (X-4).
    for (Path walk = path; !walk.empty(); walk.pop_back()) {
        if (const Element* e = resolve(walk);
            e != nullptr && e->props.enabled && e->props.cursor != Cursor::arrow) {
            want = e->props.cursor;
            break;
        }
    }
    // While dragging a slider the horizontal resize cursor overrides.
    if (!pressed_path_.empty()) {
        if (const Element* e = resolve(pressed_path_);
            e != nullptr && e->type == L"slider") {
            want = Cursor::size_we;
        }
    }
    const PCWSTR id = want == Cursor::hand     ? IDC_HAND
                    : want == Cursor::ibeam    ? IDC_IBEAM
                    : want == Cursor::size_ns  ? IDC_SIZENS
                    : want == Cursor::size_we  ? IDC_SIZEWE
                    : want == Cursor::cross    ? IDC_CROSS
                                               : IDC_ARROW;
    SetCursor(LoadCursorW(nullptr, id));
}

const Element* Host::hit_test(float lx, float ly, Path& path) const noexcept {
    if (tree_ == nullptr || !contains(*tree_, lx, ly)) return nullptr;

    const Element* node = tree_.get();
    while (true) {
        const Element* next = nullptr;
        for (int i = static_cast<int>(node->children.size()) - 1; i >= 0; --i) {
            const Element& c = *node->children[static_cast<size_t>(i)];
            if (contains(c, lx, ly)) {
                next = &c;
                path.push_back(i);
                break;
            }
        }
        if (next == nullptr) return node;
        node = next;
    }
}

const Element* Host::resolve(const Path& path) const noexcept {
    // {-1, overlay_index, ...} addresses an element inside the i-th overlay.
    if (!path.empty() && path[0] == -1) {
        if (path.size() < 2) return nullptr;
        const size_t idx = static_cast<size_t>(path[1]);
        if (idx >= frame_overlays_.size()) return nullptr;
        const Element* node = frame_overlays_[idx].get();
        for (size_t i = 2; i < path.size(); ++i) {
            const int index = path[i];
            if (index < 0 || static_cast<size_t>(index) >= node->children.size()) return nullptr;
            node = node->children[static_cast<size_t>(index)].get();
        }
        return node;
    }
    if (tree_ == nullptr) return nullptr;
    const Element* node = tree_.get();
    for (const int index : path) {
        if (index < 0 || static_cast<size_t>(index) >= node->children.size()) return nullptr;
        node = node->children[static_cast<size_t>(index)].get();
    }
    return node;
}

const Element* Host::hit_test_all(float lx, float ly, Path& path) const noexcept {
    // Overlays first, topmost (last shown) first. A root with a background
    // is a scrim: it swallows hits that miss every child; a transparent
    // root is click-through outside its children.
    for (size_t oi = frame_overlays_.size(); oi-- > 0;) {
        const Element& root = *frame_overlays_[oi];
        if (!contains(root, lx, ly)) continue;
        Path inner;
        const Element* node = &root;
        bool descended = false;
        while (true) {
            const Element* next = nullptr;
            for (int i = static_cast<int>(node->children.size()) - 1; i >= 0; --i) {
                const Element& c = *node->children[static_cast<size_t>(i)];
                if (contains(c, lx, ly)) {
                    next = &c;
                    inner.push_back(i);
                    break;
                }
            }
            if (next == nullptr) break;
            node = next;
            descended = true;
        }
        if (descended) {
            path.clear();
            path.push_back(-1);
            path.push_back(static_cast<int>(oi));
            path.insert(path.end(), inner.begin(), inner.end());
            return node;
        }
        // A root with a background is a scrim; a transparent root WITH an
        // on_click is an invisible scrim (the click-outside-to-close idiom
        // used by dropdowns/menus/palettes). Both swallow hits that miss
        // every child; only a handler-less transparent root passes through.
        if (root.props.background.has_value() || root.props.on_click) {
            path = Path{-1, static_cast<int>(oi)};
            return &root;
        }
    }
    return hit_test(lx, ly, path);
}

void Host::collect_focusable(const Element& e, const Path& prefix, std::vector<Path>& out) const noexcept {
    if (e.props.focusable && e.props.enabled) out.push_back(prefix);
    for (size_t i = 0; i < e.children.size(); ++i) {
        Path child_path = prefix;
        child_path.push_back(static_cast<int>(i));
        collect_focusable(*e.children[i], child_path, out);
    }
}

void Host::handle_mouse_move(int px, int py) {
    if (!mouse_tracking_) {
        mouse_tracking_ = true;
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        TrackMouseEvent(&tme);
    }
    const float lx = to_logical_px(px);
    const float ly = to_logical_px(py);
    last_mouse_lx_ = lx;
    last_mouse_ly_ = ly;

    // Scrollbar thumb drag: map the pointer to the scroll offset directly.
    if (!drag_scroll_.empty()) {
        const Element* sv = resolve(drag_scroll_);
        const auto it = scrolls_.find(drag_scroll_);
        if (sv != nullptr && it != scrolls_.end()) {
            const ScrollState& st = it->second;
            const float max_off = std::max(0.0f, st.content_h - st.viewport_h);
            const float thumb_h = std::max(24.0f, st.viewport_h * (st.viewport_h / st.content_h));
            const float track_y = sv->y + sv->props.padding.top;
            const float span = st.viewport_h - thumb_h;
            if (span > 0.0f && max_off > 0.0f) {
                const float ratio = (ly - drag_grab_dy_ - track_y) / span;
                scroll_to(drag_scroll_, std::clamp(ratio, 0.0f, 1.0f) * max_off);
            }
        }
        return;
    }

    Path path;
    (void)hit_test_all(lx, ly, path);

    // Drag-while-pressed: if the user pressed a slider and is still holding
    // the button, mouse_move translates the thumb. pressed_path_ resolves to
    // the slider; we compute the new value and call on_value_change.
    if (!pressed_path_.empty()) {
        const Generation drag_gen = dirty_generation_;
        if (const Element* e = resolve(pressed_path_);
            e != nullptr && e->type == L"slider" && e->w > 0.0f
            && e->props.on_value_change) {
            const float t = std::clamp((lx - e->x) / e->w, 0.0f, 1.0f);
            const float v = e->props.slider_min + t * (e->props.slider_max - e->props.slider_min);
            const auto on_change = e->props.on_value_change;   // R-2: survives a nested rebuild
            on_change(v);
        } else if (const Element* dh = resolve(pressed_path_);
                   dh != nullptr && dh->type == L"drag_handle" &&
                   dh->props.on_value_change && pressed_path_.size() >= 2) {
            // Generic drag handle (splitter): normalized position inside the
            // PARENT container along the handle's axis maps to 0..1.
            Path parent_path = pressed_path_;
            parent_path.pop_back();
            if (const Element* parent = resolve(parent_path); parent != nullptr) {
                const bool vert = dh->props.drag_vertical;
                const float span = vert ? parent->h : parent->w;
                if (span > 0.0f) {
                    const float pos = vert ? (ly - parent->y) : (lx - parent->x);
                    const auto on_change = dh->props.on_value_change;   // R-2
                    on_change(std::clamp(pos / span, 0.0f, 1.0f));
                }
            }
        }
        ensure_repaint_after_handler(drag_gen);
    }

    if (path != hover_path_) {
        hover_from_path_ = std::move(hover_path_);
        hover_path_ = std::move(path);
        hover_change_ms_ = GetTickCount64();
        animate_for(static_cast<unsigned>(tokens::motion_fast_ms));
        // Hover highlight is applied at render time from hover_path_ and is not
        // encoded in the element tree; the damage diff is fed the OLD and NEW
        // paths in paint_frame so this becomes a partial repaint.
        request_render();
        return;
    }

    // Unchanged hover target, but a paint-bearing element under the cursor
    // renders pointer-dependent pixels (chart crosshairs): repaint while
    // the pointer keeps moving. Parked cursors cost nothing.
    for (Path walk = hover_path_; !walk.empty(); walk.pop_back()) {
        if (const Element* e = resolve(walk);
            e != nullptr && e->props.paint) {
            request_render();
            break;
        }
    }
}

void Host::handle_mouse_leave() {
    mouse_tracking_ = false;
    if (!hover_path_.empty()) {
        hover_path_.clear();
        request_render();   // drop the stale hover highlight
    }
}

void Host::handle_lbutton_down(int px, int py) {
    const float lx = to_logical_px(px);
    const float ly = to_logical_px(py);
    Path path;
    const Element* target = hit_test_all(lx, ly, path);

    // Scrollbar thumb/track interaction takes precedence over content hits.
    if (target != nullptr) {
        for (Path walk = path; !walk.empty(); walk.pop_back()) {
            const Element* sv = resolve(walk);
            if (sv == nullptr || sv->type != L"scroll_view") continue;
            const auto it = scrolls_.find(walk);
            if (it == scrolls_.end() || it->second.content_h <= it->second.viewport_h) break;
            const ScrollState& st = it->second;
            const Props& pp = sv->props;
            const float vw = sv->w - pp.padding.left - pp.padding.right;
            const float track_x = sv->x + pp.padding.left + vw -
                                  static_cast<float>(tokens::scrollbar_width) - 2.0f;
            const float track_y = sv->y + pp.padding.top;
            if (lx >= track_x && ly >= track_y && ly <= track_y + st.viewport_h) {
                const float max_off = st.content_h - st.viewport_h;
                const float thumb_h = std::max(24.0f, st.viewport_h * (st.viewport_h / st.content_h));
                const float thumb_y = track_y + (st.viewport_h - thumb_h) * (st.offset_y / max_off);
                if (ly >= thumb_y && ly <= thumb_y + thumb_h) {
                    drag_scroll_ = walk;
                    drag_grab_dy_ = ly - thumb_y;
                    if (hwnd_ != nullptr) SetCapture(hwnd_);
                } else {
                    // Track click: page toward the click position.
                    scroll_by(walk, ly < thumb_y ? -st.viewport_h * 0.9f : st.viewport_h * 0.9f);
                }
                return;
            }
            break;   // outermost scroll_view owns the bar
        }
    }

    Path new_focus;
    Path new_pressed;
    if (target != nullptr) {
        // Walk up: an interactive ancestor owns the press (label inside
        // button). Drag handles (on_value_change, no on_click) are
        // pressable too — the slider/drag_handle branches consume it.
        Path walk = path;
        while (!walk.empty()) {
            // X-1: an overlay root's on_click is "click OUTSIDE to close".
            // A hit that started deeper inside the overlay (the card body,
            // a hint row, drawer text) must not bubble up to it — swallow
            // the click instead of closing. The root itself (scrim /
            // transparent margin) is still pressed when directly hit.
            if (walk.size() == 2 && walk[0] == -1 && walk != path) break;
            if (const Element* e = resolve(walk);
                e != nullptr && e->props.enabled &&
                (e->props.on_click || e->props.on_value_change)) {
                new_pressed = walk;
                new_focus = walk;
                break;
            }
            walk.pop_back();
        }
        // Text inputs own their own edit-mode focus independent of the
        // keyboard focus ring — WM_CHAR dispatches here, not to focus_path_.
        if (target->type == L"text_input") {
            focused_input_path_ = path;
            input_caret_ = input_anchor_ = caret_from_x(*target, lx);   // F1
        } else if (!focused_input_path_.empty()) {
            focused_input_path_.clear();
        }
    } else {
        // Background click drops text-input focus.
        if (!focused_input_path_.empty()) focused_input_path_.clear();
    }
    // Sliders fire their on_value_change on mouse_down so a single click
    // jumps the thumb to that x without requiring drag. Slider drag is
    // handled in handle_mouse_move (pressed_path_ anchors the target).
    if (!new_pressed.empty()) {
        const Generation press_gen = dirty_generation_;
        if (const Element* e = resolve(new_pressed);
            e != nullptr && e->type == L"slider" && e->w > 0.0f
            && e->props.on_value_change) {
            const float t = std::clamp((lx - e->x) / e->w, 0.0f, 1.0f);
            const float v = e->props.slider_min + t * (e->props.slider_max - e->props.slider_min);
            const auto on_change = e->props.on_value_change;   // R-2: survives a nested rebuild
            on_change(v);
        } else if (const Element* dh = resolve(new_pressed);
                   dh != nullptr && dh->type == L"drag_handle" &&
                   dh->props.on_value_change && new_pressed.size() >= 2) {
            // A drag_handle press immediately reports the normalized
            // position inside its parent (splitter jump-to).
            Path parent_path = new_pressed;
            parent_path.pop_back();
            if (const Element* parent = resolve(parent_path); parent != nullptr) {
                const bool vert = dh->props.drag_vertical;
                const float span = vert ? parent->h : parent->w;
                if (span > 0.0f) {
                    const float pos = vert ? (ly - parent->y) : (lx - parent->x);
                    const auto on_change = dh->props.on_value_change;   // R-2
                    on_change(std::clamp(pos / span, 0.0f, 1.0f));
                }
            }
        }
        ensure_repaint_after_handler(press_gen);
    }
    // Background click clears focus (target == nullptr OR no interactive
    // ancestor); keyboard activation on a now-blank focus path doesn't work
    // but matches Win32 tabbing behaviour: clicking outside any focusable
    // element drops focus.
    if (new_focus != focus_path_ || new_pressed != pressed_path_) {
        press_from_path_ = std::move(pressed_path_);
        focus_path_ = std::move(new_focus);
        pressed_path_ = std::move(new_pressed);
        press_change_ms_ = GetTickCount64();
        animate_for(static_cast<unsigned>(tokens::motion_fast_ms));
        if (hwnd_ != nullptr && !pressed_path_.empty()) SetCapture(hwnd_);
        a11y::notify_focus(hwnd_);   // click focus is announced too (X-12)
        request_render();
    }
}

void Host::handle_lbutton_up(int px, int py) {
    if (hwnd_ != nullptr) ReleaseCapture();
    if (!drag_scroll_.empty()) {
        drag_scroll_.clear();
        return;
    }
    if (pressed_path_.empty()) return;

    Path under;
    (void)hit_test_all(to_logical_px(px), to_logical_px(py), under);
    // Walk up from the release point and check whether the pressed element is
    // still an ancestor — that's the Win32 click semantics.
    Path walk = under;
    bool inside = false;
    while (!walk.empty()) {
        if (walk == pressed_path_) {
            inside = true;
            break;
        }
        walk.pop_back();
    }

    // Capture path BEFORE clearing — resolve() walks the live tree and the
    // handler below may trigger a rebuild that invalidates any previously
    // cached pointer. Holding the path keeps the click target stable.
    const Path fired = pressed_path_;
    press_from_path_ = pressed_path_;
    press_change_ms_ = GetTickCount64();
    animate_for(static_cast<unsigned>(tokens::motion_fast_ms));
    pressed_path_.clear();
    request_render();   // drop the pressed highlight via partial diff

    if (inside) {
        if (const Element* e = resolve(fired);
            e != nullptr && e->props.on_click && e->props.enabled) {
            last_click_lx_ = to_logical_px(px);
            last_click_ly_ = to_logical_px(py);
            // R-2: stack-copy before invoking — a handler that mutates
            // State<T> re-enters paint_frame, which replaces the scene tree
            // and would otherwise destroy the closure mid-execution. R-5:
            // if it changed nothing repaint-visible, paint anyway.
            const auto on_click = e->props.on_click;
            const Generation gen = dirty_generation_;
            on_click();
            ensure_repaint_after_handler(gen);
        }
    }
}

// Points the text-input focus (WM_CHAR caret) at the first element laid
// out this frame with Props.autofocus (main tree first, then overlays).
// Used by the command palette so typing works right after Ctrl+K.
void Host::apply_autofocus() noexcept {
    autofocus_path_.clear();
    std::function<void(const Element&, const Path&)> walk =
        [&](const Element& e, const Path& prefix) {
            if (!autofocus_path_.empty()) return;
            if (e.props.autofocus && e.type == L"text_input") {
                autofocus_path_ = prefix;
                return;
            }
            for (size_t i = 0; i < e.children.size() && autofocus_path_.empty(); ++i) {
                Path child = prefix;
                child.push_back(static_cast<int>(i));
                walk(*e.children[i], child);
            }
        };
    if (tree_ != nullptr) walk(*tree_, Path{});
    for (size_t oi = 0; oi < frame_overlays_.size() && autofocus_path_.empty(); ++oi) {
        if (frame_overlays_[oi] != nullptr) {
            walk(*frame_overlays_[oi], Path{-1, static_cast<int>(oi)});
        }
    }
    if (!autofocus_path_.empty() && focused_input_path_ != autofocus_path_) {
        focused_input_path_ = autofocus_path_;
        if (const Element* fe = resolve(autofocus_path_); fe != nullptr) {
            input_caret_ = input_anchor_ = fe->props.text.size();   // F1
        }
    }
}

void Host::handle_contextmenu(int screen_x, int screen_y) {    if (hwnd_ == nullptr || !context_menu_handler_) return;
    POINT pt{screen_x, screen_y};
    ScreenToClient(hwnd_, &pt);
    context_menu_handler_(to_logical_px(pt.x), to_logical_px(pt.y));
}

void Host::handle_key_down(WPARAM vk) {
    if (tree_ == nullptr) return;

    // App key hook first (command palette Ctrl+K and friends).
    if (key_hook_ && key_hook_(static_cast<unsigned>(vk))) return;

    // Hotkey registry (F7) — exact modifier match, first registration wins.
    {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        for (const HotkeyEntry& entry : hotkeys_) {
            if (entry.key.vk == vk && entry.key.ctrl == ctrl
                && entry.key.shift == shift && entry.key.alt == alt) {
                auto action = entry.action;   // copy: the action may rebuild state (R-2 pattern)
                const Generation gen = dirty_generation_;
                action();
                ensure_repaint_after_handler(gen);
                return;
            }
        }
    }

    // Esc closes the topmost overlay that HAS a dismiss handler — the
    // host-native tooltip (appended last, no on_click) must not swallow it.
    if (vk == VK_ESCAPE) {
        for (size_t oi = frame_overlays_.size(); oi-- > 0;) {
            const Element* top = frame_overlays_[oi].get();
            if (top != nullptr && top->props.on_click && top->props.enabled) {
                // R-2 copy + R-5 repaint: closing an overlay must be visible
                // immediately, even when the close handler only flips a plain
                // app-side struct.
                const auto on_click = top->props.on_click;
                const Generation gen = dirty_generation_;
                on_click();
                ensure_repaint_after_handler(gen);
                return;
            }
        }
    }

    // VK_BACK on a focused text_input deletes the selection, or the single
    // character before the caret. We do this in WM_KEYDOWN because VK_BACK
    // does not generate a WM_CHAR message.
    if (vk == VK_BACK && !focused_input_path_.empty()) {
        if (const Element* e = resolve(focused_input_path_);
            e != nullptr && e->type == L"text_input" && e->props.on_text_change) {
            std::wstring text = e->props.text;
            const auto [lo, hi] = input_sel_range();
            const size_t clo = std::min(lo, text.size());
            const size_t chi = std::min(hi, text.size());
            if (clo != chi) {
                text.erase(clo, chi - clo);
                input_caret_ = input_anchor_ = clo;
            } else if (input_caret_ > 0 && !text.empty()) {
                text.erase(input_caret_ - 1, 1);
                input_caret_ = input_anchor_ = input_caret_ - 1;
            }
            const auto on_change = e->props.on_text_change;   // R-2
            const Generation gen = dirty_generation_;
            on_change(std::move(text));
            ensure_repaint_after_handler(gen);
        }
        return;
    }

    // Text-editing keys for the focused input (F1): caret movement, selection
    // (Shift+arrows, Ctrl+A), Delete, and clipboard via Ctrl+C/X/V. Arrows /
    // Home / End are claimed only when the input has no on_key of its own —
    // the command palette's input navigates its list with them instead.
    if (!focused_input_path_.empty()) {
        if (const Element* fe = resolve(focused_input_path_);
            fe != nullptr && fe->type == L"text_input" && fe->props.enabled) {
            const bool nav_claimed = fe->props.on_key != nullptr;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            const std::wstring cur = fe->props.text;
            const size_t len = cur.size();
            // Clamp against the current text: handlers/rebuilds may have
            // shortened it since the selection was made (see handle_char).
            const auto raw_sel = input_sel_range();
            const std::pair<size_t, size_t> sel{std::min(raw_sel.first, len),
                                                std::min(raw_sel.second, len)};
            bool handled = false;
            bool text_changed = false;
            std::wstring next = cur;
            const auto move_caret = [&](size_t pos) {
                input_caret_ = pos;
                if (!shift) input_anchor_ = pos;
            };
            switch (vk) {
            case VK_LEFT:
                if (!nav_claimed) {
                    handled = true;
                    if (sel.first != sel.second && !shift) {
                        input_caret_ = input_anchor_ = sel.first;
                    } else {
                        move_caret(input_caret_ > 0 ? input_caret_ - 1 : 0);
                    }
                }
                break;
            case VK_RIGHT:
                if (!nav_claimed) {
                    handled = true;
                    if (sel.first != sel.second && !shift) {
                        input_caret_ = input_anchor_ = sel.second;
                    } else {
                        move_caret(input_caret_ < len ? input_caret_ + 1 : len);
                    }
                }
                break;
            case VK_HOME:
                if (!nav_claimed) { handled = true; move_caret(0); }
                break;
            case VK_END:
                if (!nav_claimed) { handled = true; move_caret(len); }
                break;
            case VK_DELETE:
                handled = true;
                if (sel.first != sel.second) {
                    next.erase(sel.first, sel.second - sel.first);
                    input_caret_ = input_anchor_ = sel.first;
                    text_changed = true;
                } else if (input_caret_ < len) {
                    next.erase(input_caret_, 1);
                    input_anchor_ = input_caret_;
                    text_changed = true;
                }
                break;
            case 'A': case 'C': case 'X': case 'V':
                if (ctrl) {   // plain letters arrive as WM_CHAR instead
                    handled = true;
                    if (vk == 'A') {
                        input_anchor_ = 0;
                        input_caret_ = len;
                    } else if (vk == 'C' || vk == 'X') {
                        // Windows convention: with no selection, copy/cut
                        // the whole single-line field.
                        const std::wstring sel_text =
                            sel.first != sel.second ? cur.substr(sel.first, sel.second - sel.first)
                                                    : cur;
                        (void)flux::clipboard_set_text(sel_text);
                        if (vk == 'X') {
                            if (sel.first != sel.second) {
                                next = cur.substr(0, sel.first) + cur.substr(sel.second);
                                input_caret_ = input_anchor_ = sel.first;
                            } else {
                                next.clear();
                                input_caret_ = input_anchor_ = 0;
                            }
                            text_changed = true;
                        }
                    } else {   // V — newlines don't belong in a single-line input
                        std::wstring paste;
                        for (const wchar_t c : flux::clipboard_get_text()) {
                            if (c != L'\r' && c != L'\n') paste.push_back(c);
                        }
                        if (!paste.empty()) {
                            next = cur.substr(0, sel.first) + paste + cur.substr(sel.second);
                            input_caret_ = input_anchor_ = sel.first + paste.size();
                            text_changed = true;
                        }
                    }
                }
                break;
            default:
                break;
            }
            if (handled) {
                if (text_changed) {
                    if (fe->props.on_text_change) {
                        const auto on_change = fe->props.on_text_change;   // R-2
                        const Generation gen = dirty_generation_;
                        on_change(std::move(next));
                        ensure_repaint_after_handler(gen);
                    }
                } else {
                    // Caret/selection-only move: invisible to the tree diff
                    // (Host-side state), so force one full dirty frame.
                    force_full_dirty_ = true;
                    request_render();
                }
                return;
            }
        }
    }

    // Focused scroll_view: arrow / page / home / end scrolling.
    if (!focus_path_.empty()) {
        if (const Element* fe = resolve(focus_path_);
            fe != nullptr && fe->type == L"scroll_view") {
            const auto it = scrolls_.find(focus_path_);
            if (it != scrolls_.end() && it->second.content_h > it->second.viewport_h) {
                const float row = static_cast<float>(tokens::control_height_md);
                const float page = it->second.viewport_h * 0.9f;
                switch (vk) {
                case VK_UP:    scroll_by(focus_path_, -row);  return;
                case VK_DOWN:  scroll_by(focus_path_, row);   return;
                case VK_PRIOR: scroll_by(focus_path_, -page); return;
                case VK_NEXT:  scroll_by(focus_path_, page);  return;
                case VK_HOME:  scroll_to(focus_path_, 0.0f);  return;
                case VK_END:   scroll_to(focus_path_, 1.0e9f); return;
                default: break;
                }
            }
        }
    }

    // Focused slider: arrows step by 1/10 of the range, Home/End clamp —
    // the keyboard equivalent of dragging the thumb (review 2026-10-06 X-2).
    if (!focus_path_.empty()) {
        if (const Element* fe = resolve(focus_path_);
            fe != nullptr && fe->type == L"slider" && fe->props.enabled
            && fe->props.on_value_change) {
            const float lo = fe->props.slider_min;
            const float hi = fe->props.slider_max;
            const float cur = lo + std::clamp(fe->props.slider_value, 0.0f, 1.0f) * (hi - lo);
            float next = cur;
            switch (vk) {
            case VK_LEFT:
            case VK_DOWN:  next = cur - (hi - lo) / 10.0f; break;
            case VK_RIGHT:
            case VK_UP:    next = cur + (hi - lo) / 10.0f; break;
            case VK_HOME:  next = lo; break;
            case VK_END:   next = hi; break;
            default: break;
            }
            if (next != cur) {
                const auto on_change = fe->props.on_value_change;   // R-2
                const Generation gen = dirty_generation_;
                on_change(std::clamp(next, lo, hi));
                ensure_repaint_after_handler(gen);
                return;
            }
        }
    }

    // Focused element's own key handler (arrows/Enter/Esc for lists and
    // the command palette) — before the generic Tab/Space machinery.
    if (!focus_path_.empty()) {
        if (const Element* fe = resolve(focus_path_);
            fe != nullptr && fe->props.on_key && fe->props.enabled) {
            const auto on_key = fe->props.on_key;   // R-2: survives a nested rebuild
            const Generation gen = dirty_generation_;
            on_key(static_cast<unsigned>(vk));
            ensure_repaint_after_handler(gen);
            return;
        }
    }

    // Inputs usually reach the user via focused_input_path_ (mouse click,
    // or Tab now that focus moves steer it). When an input holds only the
    // edit focus — clicked, not tabbed to — navigation keys for handlers
    // living on it (the command palette) dispatch here. The allowlist keeps
    // text-editing keys (backspace via VK_BACK, chars via WM_CHAR) flowing
    // to the built-in input handling.
    if (!focused_input_path_.empty()) {
        switch (vk) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_RETURN: case VK_PRIOR: case VK_NEXT: {
            if (const Element* fe = resolve(focused_input_path_);
                fe != nullptr && fe->props.on_key && fe->props.enabled) {
                const auto on_key = fe->props.on_key;   // R-2
                const Generation gen = dirty_generation_;
                on_key(static_cast<unsigned>(vk));
                ensure_repaint_after_handler(gen);
                return;
            }
            break;
        }
        default:
            break;
        }
    }

    std::vector<Path> order;
    collect_focusable(*tree_, Path{}, order);
    for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
        collect_focusable(*frame_overlays_[oi],
                          Path{-1, static_cast<int>(oi)}, order);
    }
    if (order.empty()) return;

    if (vk == VK_TAB) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        // X-3: "not focused yet" must not read as "at order[0]" — forward
        // Tab starts ON the first element (the old +1-from-nothing skipped
        // it) and Shift+Tab wraps to the last one.
        size_t index = order.size();
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == focus_path_) {
                index = i;
                break;
            }
        }
        const size_t next = index == order.size()
            ? (shift ? order.size() - 1 : 0)
            : (shift ? (index + order.size() - 1) % order.size()
                     : (index + 1) % order.size());
        if (focus_path_ != order[next]) {
            focus_path_ = order[next];
            // Keep the edit focus aligned with the keyboard focus: tabbing
            // ONTO a text_input routes WM_CHAR/VK_BACK to it (previously
            // only a mouse click did, so Tab-focused inputs silently ate
            // keystrokes); tabbing away releases the caret.
            if (const Element* fe = resolve(focus_path_);
                fe != nullptr && fe->type == L"text_input") {
                focused_input_path_ = focus_path_;
                input_caret_ = input_anchor_ = fe->props.text.size();   // F1
            } else if (!focused_input_path_.empty()) {
                focused_input_path_.clear();
            }
            a11y::notify_focus(hwnd_);   // screen readers follow the move (X-12)
            request_render();   // focus ring moved — partial diff covers it
        }
        return;
    }

    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (const Element* e = resolve(focus_path_);
            e != nullptr && e->props.on_click && e->props.enabled) {
            const auto on_click = e->props.on_click;   // R-2: survives a nested rebuild
            const Generation gen = dirty_generation_;
            on_click();
            ensure_repaint_after_handler(gen);
        }
    }
}

std::pair<size_t, size_t> Host::input_sel_range() const noexcept {
    return input_anchor_ < input_caret_ ? std::pair<size_t, size_t>{input_anchor_, input_caret_}
                                        : std::pair<size_t, size_t>{input_caret_, input_anchor_};
}

size_t Host::caret_from_x(const Element& e, float x) noexcept {
    // Nearest character boundary to the click: prefix widths come from the
    // (cached) text measurements, so this is a handful of cache hits for
    // typical input lengths.
    const std::wstring& text = e.props.text;
    const float rel = x - (e.x + e.props.padding.left);
    if (rel <= 0.0f) return 0;
    size_t best = text.size();
    float best_dist = rel;
    for (size_t i = 0; i <= text.size(); ++i) {
        const auto [w, h] = renderer_.measure_text(text.substr(0, i),
                                                   e.props.font_size_pt, false);
        const float d = std::fabs(w - rel);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

void Host::handle_ime_composition(LPARAM lparam) {
    // Inline composition (F6): the in-progress string renders as underlined
    // ghost text in the focused input; the committed result appends
    // through the normal on_text_change path. Composition state is
    // Host-side (like the caret) — invisible to the tree diff, so the
    // repaint is forced.
    if (hwnd_ == nullptr) return;
    if (focused_input_path_.empty()) return;
    HIMC imc = ImmGetContext(hwnd_);
    if (imc == nullptr) return;

    bool changed = false;
    if ((lparam & GCS_RESULTSTR) != 0) {
        const int bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
        if (bytes > 0) {
            std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
            ImmGetCompositionStringW(imc, GCS_RESULTSTR, result.data(), bytes);
            if (const Element* e = resolve(focused_input_path_);
                e != nullptr && e->type == L"text_input" && e->props.on_text_change) {
                const auto [lo, hi] = input_sel_range();
                const size_t clo = std::min(lo, e->props.text.size());
                const size_t chi = std::min(hi, e->props.text.size());
                std::wstring text = e->props.text.substr(0, clo) + result
                                  + e->props.text.substr(chi);
                input_caret_ = input_anchor_ = clo + result.size();
                const auto on_change = e->props.on_text_change;   // R-2
                const Generation gen = dirty_generation_;
                ime_composition_.clear();
                on_change(std::move(text));
                ensure_repaint_after_handler(gen);
                changed = true;
            }
        } else {
            ime_composition_.clear();
            changed = true;
        }
    }
    if ((lparam & GCS_COMPSTR) != 0) {
        const int bytes = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
        ime_composition_.assign(static_cast<size_t>(std::max(0, bytes)) / sizeof(wchar_t),
                                L'\0');
        if (bytes > 0) {
            ImmGetCompositionStringW(imc, GCS_COMPSTR, ime_composition_.data(), bytes);
        }
        changed = true;
    }
    ImmReleaseContext(hwnd_, imc);
    if (changed) {
        force_full_dirty_ = true;   // composition is render-time state
        request_render();
    }
}

void Host::handle_ime_end_composition() {
    if (!ime_composition_.empty()) {
        ime_composition_.clear();
        force_full_dirty_ = true;
        request_render();
    }
}

void Host::handle_ime_position() {
    // Pin the IME's own windows (candidate list, remaining composition UI)
    // to the caret of the focused input, in screen coordinates.
    if (hwnd_ == nullptr || focused_input_path_.empty()) return;
    const Element* e = resolve(focused_input_path_);
    if (e == nullptr || e->type != L"text_input") return;
    HIMC imc = ImmGetContext(hwnd_);
    if (imc == nullptr) return;
    const std::wstring shown = e->props.masked
        ? std::wstring(e->props.text.size(), L'●') + ime_composition_
        : e->props.text + ime_composition_;
    const size_t caret = std::min(input_caret_ + ime_composition_.size(), shown.size());
    const auto [tw, th] = renderer_.measure_text(shown.substr(0, caret),
                                                 e->props.font_size_pt, false);
    POINT pt{static_cast<LONG>(e->x + e->props.padding.left + tw),
             static_cast<LONG>(e->y + (e->h - th) * 0.5f)};
    ClientToScreen(hwnd_, &pt);
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = pt;
    ImmSetCompositionWindow(imc, &cf);
    ImmReleaseContext(hwnd_, imc);
}

void Host::handle_char(WPARAM ch) {
    // WM_CHAR delivers UTF-16 code units (wchar_t on Windows). VK_BACK has
    // already been handled in WM_KEYDOWN, and we want printable characters
    // only — reject control codes so a stray WM_CHAR from the keyboard
    // autorepeat doesn't pollute the input.
    if (ch < 0x20) return;
    if (focused_input_path_.empty()) return;
    if (const Element* e = resolve(focused_input_path_);
        e != nullptr && e->type == L"text_input" && e->props.on_text_change) {
        std::wstring text = e->props.text;
        // Typing replaces the selection, then inserts at its start (F1).
        // Indices clamp against the CURRENT text: an app handler (or a
        // rebuilt tree) may have shortened it since the caret last moved —
        // erase/substr past the end throw.
        const auto [lo, hi] = input_sel_range();
        const size_t clo = std::min(lo, text.size());
        const size_t chi = std::min(hi, text.size());
        text.erase(clo, chi - clo);
        text.insert(clo, 1, static_cast<wchar_t>(ch));
        input_caret_ = input_anchor_ = clo + 1;
        const auto on_change = e->props.on_text_change;   // R-2: survives a nested rebuild
        on_change(std::move(text));
        // Handlers that mutate plain app structs (not State<T>) don't
        // invalidate anything themselves — the palette query would stay
        // stale until an unrelated repaint.
        request_render();
    }
}

// =====================================================================
// Frame pipeline: rebuild -> layout -> paint
// =====================================================================

void Host::paint_frame() {
    // Re-entry / validity guard: a WM_PAINT (or a builder callback that
    // synchronously pumps) must not wipe frame_overlays_ mid-draw, and a
    // frame drawn with an invalid presenter must not advance the paint
    // baseline (the recovered frame would never repaint).
    if (painting_ || !renderer_.valid()) return;
    // Short-circuit: if nothing has invalidated the scene since the last paint
    // (and we don't owe a forced full repaint or an in-flight animation /
    // fade frame), the DComp surface is still correct. Skipping avoids
    // running the builder, flex_layout, and DamageTracker on stray WM_PAINTs
    // that arrive between state mutations (window uncover, drag, etc.).
    if (dirty_generation_ == last_painted_generation_ && !force_full_dirty_ && !animating()) {
        return;
    }
    PaintScope scope(painting_);

    // Reset the overlay set before the build: the root builder re-declares
    // every visible overlay each frame (declarative lifetime).
    frame_overlays_.clear();

    if (qpc_freq_ == 0) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        qpc_freq_ = f.QuadPart;
    }
    const long long t_frame_start = qpc();

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float logical_w = to_logical_px(rc.right - rc.left);
    const float logical_h = to_logical_px(rc.bottom - rc.top);

    const long long t_after_rect = qpc();

    // Rebuild the scene tree (React re-render).
    tree_ = root_builder_ ? root_builder_() : nullptr;
    if (tree_ != nullptr) {
        LayoutContext ctx;
        ctx.measure_leaf = [this](const Element& e) -> std::pair<float, float> {
            const float box = static_cast<float>(tokens::checkbox_box);
            const float wrap_w = (e.props.text_wrap && e.props.width.has_value())
                ? *e.props.width : 0.0f;
            const auto [tw, th] = renderer_.measure_text(e.props.text, e.props.font_size_pt,
                                                         e.props.bold, wrap_w);
            if (e.type == L"checkbox") {
                // Whole-row hit target of at least 24 logical px (WCAG
                // 2.5.8; the box itself is 18 — review 2026-10-06 X-7).
                return {box + tokens::spacing_sm + tw, std::max(24.0f, std::max(box, th))};
            }
            if (e.type == L"image" && e.props.image_path.has_value()) {
                const auto dims = renderer_.image_size(*e.props.image_path);
                const float iw = dims.has_value() ? dims->first : 64.0f;
                const float ih = dims.has_value() ? dims->second : 64.0f;
                return {e.props.width.value_or(iw), e.props.height.value_or(ih)};
            }
            // Leaf views with no text (bars, spacers, avatars, decorative
            // rectangles) still have an intrinsic size when width/height is
            // explicit — without falling back to those, parents walking the
            // tree during measure_impl see a 0×0 leaf and collapse the
            // subtree to intrinsic 0. The bar chart's bars and spacers and
            // the avatar group's 32-px circles all rely on this.
            const float extra = e.props.icon.has_value()
                ? 20.0f + static_cast<float>(tokens::spacing_sm)
                : 0.0f;
            const float w = e.props.width.value_or(tw) + extra;
            const float h = e.props.height.value_or(th);
            return {w, h};
        };
        flex_layout(*tree_, logical_w, logical_h, ctx);
        // Lay out scroll_view contents (unbounded height + offset baking)
        // and carry scroll offsets over by path identity.
        std::map<Path, ScrollState> next_scrolls;
        layout_scroll_views(*tree_, Path{}, ctx, next_scrolls);
        // Host-native tooltip may append one more pass-through overlay
        // (armed by dwelling hover); it lays out with the rest below.
        update_tooltip();
        // Overlays lay out against the full canvas (anchoring is pure
        // flex — see test_overlay_anchoring). They are collected inside
        // root_builder_(), which ran above. Overlay scroll_views lay out
        // with the sentinel-prefixed paths, so the wheel/keyboard/drag
        // chain (which already resolves {-1, idx} paths) finds them in
        // the same scrolls_ registry as main-tree ones.
        for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
            if (frame_overlays_[oi] == nullptr) continue;
            flex_layout(*frame_overlays_[oi], logical_w, logical_h, ctx);
            layout_scroll_views(*frame_overlays_[oi], Path{-1, static_cast<int>(oi)},
                                ctx, next_scrolls);
        }
        // Commit AFTER the overlay pass: moving earlier would drop every
        // sentinel-keyed entry into a moved-from map.
        scrolls_ = std::move(next_scrolls);
        apply_autofocus();
    }

    const long long t_after_layout = qpc();

    // Compute damage. resize / DPI change / any in-flight animation (theme
    // fade or an animate_for tween) force a fullscreen update; otherwise
    // diff prev_tree_ against the new tree and also fold in interaction-state
    // changes (hover/press/focus) that the structural diff can't see.
    DamageTracker::InteractionState prev_interaction{
        prev_hover_path_, prev_pressed_path_, prev_focus_path_};
    DamageTracker::InteractionState next_interaction{
        hover_path_, pressed_path_, focus_path_};

    // Interaction state that points into an overlay can't be resolved by
    // the main-tree diff — resolve those paths against the (already laid
    // out) overlay trees and dirty just the old/new element rects. An
    // unresolvable path (tree shape changed) still falls back to full.
    const auto touches_overlay = [](const Path& p) noexcept {
        return !p.empty() && p[0] == -1;
    };
    std::vector<Rect> overlay_interaction_rects;
    bool overlay_interaction_unresolved = false;
    {
        const Path* const interaction_paths[] = {
            &hover_path_, &pressed_path_, &focus_path_,
            &prev_hover_path_, &prev_pressed_path_, &prev_focus_path_};
        for (const Path* p : interaction_paths) {
            if (!touches_overlay(*p)) continue;
            if (const Element* e = resolve(*p)) {
                overlay_interaction_rects.push_back(Rect{e->x, e->y, e->w, e->h});
            } else {
                overlay_interaction_unresolved = true;
            }
        }
    }
    const bool overlay_set_changed = frame_overlays_.size() != prev_overlays_.size();

    std::vector<Rect> dirty_rects;
    if (force_full_dirty_ || fading_ || overlay_set_changed || overlay_interaction_unresolved) {
    }
    // Fullscreen only for what genuinely spans the window: an explicit
    // force (resize/DPI), the theme cross-fade (every element's colors
    // interpolate), or an overlay appearing/disappearing. Plain tweens
    // (hover/press micro-transitions, drawer slides, drag feedback) run
    // through the diff — the InteractionState pass covers the hovered/
    // pressed elements and per-frame prop changes surface structurally.
    // Review 2026-10-06: `animating()` used to force fullscreen here, which
    // disabled partial repaint for the entire 120 ms of every hover change.
    if (force_full_dirty_ || fading_ || overlay_set_changed ||
        overlay_interaction_unresolved) {
        if (logical_w > 0.0f && logical_h > 0.0f) {
            dirty_rects.push_back(Rect{0.0f, 0.0f, logical_w, logical_h});
        }
    } else {
        dirty_rects = damage_tracker_.compute(prev_tree_.get(), tree_.get(),
                                              logical_w, logical_h,
                                              &prev_interaction,
                                              &next_interaction);
        for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
            auto rects = damage_tracker_.compute(prev_overlays_[oi].get(),
                                                 frame_overlays_[oi].get(),
                                                 logical_w, logical_h);
            dirty_rects.insert(dirty_rects.end(), rects.begin(), rects.end());
        }
        dirty_rects.insert(dirty_rects.end(),
                           overlay_interaction_rects.begin(),
                           overlay_interaction_rects.end());
        // Scrollbar auto-hide fade: the bar's alpha is render-time state the
        // tree diff can't see — dirty its rect explicitly while it holds or
        // fades (the old blanket-animation fullscreen used to cover this).
        if (GetTickCount64() <
            scrollbar_fade_at_ms_ + static_cast<unsigned long long>(tokens::motion_medium_ms)) {
            if (const Element* sv = resolve(scrollbar_fade_path_);
                sv != nullptr && sv->type == L"scroll_view") {
                const auto it = scrolls_.find(scrollbar_fade_path_);
                if (it != scrolls_.end() && it->second.content_h > it->second.viewport_h) {
                    const float vw = sv->w - sv->props.padding.left - sv->props.padding.right;
                    const float bx = sv->x + sv->props.padding.left + vw
                                   - static_cast<float>(tokens::scrollbar_width) - 2.0f;
                    dirty_rects.push_back(Rect{bx, sv->y + sv->props.padding.top,
                                               static_cast<float>(tokens::scrollbar_width) + 4.0f,
                                               it->second.viewport_h});
                }
            }
        }
        if (dirty_rects.size() > damage_tracker_.max_rects * 2) {
            dirty_rects.assign(1, Rect{0.0f, 0.0f, logical_w, logical_h});
        }
    }
    force_full_dirty_ = false;

    const long long t_after_damage = qpc();
    if (debug_hud_enabled()) {
        last_build_ms_  = qpc_to_ms(qpc_freq_, t_after_rect - t_frame_start);
        last_layout_ms_ = qpc_to_ms(qpc_freq_, t_after_layout - t_after_rect);
        last_damage_ms_ = qpc_to_ms(qpc_freq_, t_after_damage - t_after_layout);
    }

    if (dirty_rects.empty()) {
        // Nothing changed; the composition visual is already correct. Do NOT
        // advance prev_tree_ / prev_*_path_ here — the next frame's diff
        // baseline must remain the last frame that actually drew. But DO
        // advance last_painted_generation_ so the gen short-circuit above
        // catches subsequent stray WM_PAINTs without re-running the diff.
        last_painted_generation_ = dirty_generation_;
        // Counted as a SKIP: the pipeline ran but nothing drew. Averaged
        // separately from drawn frames so partial-repaint stats stay honest.
        ++skip_counter_;
        t_total_ += qpc() - t_frame_start;
        log_frame_summary(GetTickCount64());
        return;
    }

    // Iterate each dirty rect separately so the Presenter's sub-allocated
    // BeginDraw can target the actual changed region. render_element prunes
    // subtrees whose bounds don't intersect the dirty rect, so a partial
    // repaint only visits the elements that actually need new pixels.
    const long long t_before_draw = qpc();
    bool drew_any = false;
    for (const Rect& dr : dirty_rects) {
        renderer_.begin(shown_.background, &dr);
        if (renderer_.frame_active()) {
            drew_any = true;
            if (tree_ != nullptr) render_element(*tree_, Path{}, dr);
            for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
                if (frame_overlays_[oi] != nullptr) {
                    render_element(*frame_overlays_[oi],
                                   Path{-1, static_cast<int>(oi)}, dr);
                }
            }
            // Debug HUD (F4): outline exactly what this repaint touched +
            // a per-frame timing badge. Enabled only via WINFLUX_DEBUG_HUD=1
            // so release builds pay nothing.
            if (debug_hud_enabled()) draw_debug_hud(dr, dirty_rects.size());
        }
        renderer_.end();
    }
    if (!drew_any) {
        // R-7: every BeginDraw failed (device lost, surface exhausted).
        // Advancing the diff baseline or the generation here would mark the
        // content as painted when it wasn't — after recovery the diff would
        // come up empty and the window would show stale pixels until
        // unrelated input. Leave both untouched; the next paint retries
        // against the same baseline.
        ++skip_counter_;
        t_total_ += qpc() - t_frame_start;
        log_frame_summary(GetTickCount64());
        return;
    }

    // Snapshots advance AFTER a real draw, so a frame that produced no dirty
    // rects preserves the true previous baseline for the next diff.
    prev_tree_ = tree_;
    prev_overlays_ = frame_overlays_;
    prev_hover_path_   = hover_path_;
    prev_pressed_path_ = pressed_path_;
    prev_focus_path_   = focus_path_;
    last_painted_generation_ = dirty_generation_;

    const long long t_end = qpc();
    if (debug_hud_enabled()) {
        last_draw_ms_ = qpc_to_ms(qpc_freq_, t_end - t_before_draw);
    }
    t_total_   += t_end - t_frame_start;                   // GetClientRect + tree build
    t_builder_ += t_after_rect - t_frame_start;
    t_layout_  += t_after_layout - t_after_rect;
    t_damage_  += t_after_damage - t_after_layout;
    t_draw_    += t_end - t_before_draw;
    ++frame_counter_;
    log_frame_summary(GetTickCount64());
}

void Host::draw_debug_hud(const Rect& dr, size_t rect_count) noexcept {
    // Outline the damage rect: exactly what this repaint touched.
    renderer_.draw_round_rect(Rect{dr.x + 0.5f, dr.y + 0.5f, dr.w - 1.0f, dr.h - 1.0f},
                              2.0f, shown_.danger, 1.5f, 0.8f);
    // Per-frame timing badge, drawn when this rect covers the top-left.
    const Rect badge{8.0f, 8.0f, 620.0f, 24.0f};
    if (!rects_intersect(dr, badge)) return;
    wchar_t buf[192];
    std::swprintf(buf, std::size(buf),
                  L"HUD  build %.2f  layout %.2f  damage %.2f  draw %.2f ms   rects %zu   gen %llu",
                  last_build_ms_, last_layout_ms_, last_damage_ms_, last_draw_ms_,
                  rect_count, static_cast<unsigned long long>(dirty_generation_));
    renderer_.fill_round_rect(badge, 4.0f, rgba(0, 0, 0, 176), 1.0f);
    renderer_.draw_text(badge, buf, tokens::font_caption, rgb(255, 255, 255),
                        false, Align::start, 1.0f);
}

void Host::flush_perf_log() noexcept {
    // Teardown: a scenario shorter than one interval (or one wall-clock
    // window) would otherwise leave nothing in the log at all (U-4).
    if (frame_counter_ + skip_counter_ > 0) log_frame_summary(GetTickCount64());
}

void Host::log_frame_summary(unsigned long long now_ms) noexcept {
    // Emit by frame count OR wall time, whichever comes first: count-only
    // sampling made sub-120-frame scenarios invisible (review 2026-10-06
    // U-4 — a 35-frame idle probe logged zero lines).
    if (frame_counter_ + skip_counter_ == 0) return;
    if (perf_emit_ms_ == 0) {
        perf_emit_ms_ = now_ms;   // arm the wall-clock window on the first frame
        return;
    }
    const bool by_count = frame_counter_ + skip_counter_ >= kFrameTimingInterval;
    const bool by_time = now_ms - perf_emit_ms_ >= kPerfEmitMs;
    if (!by_count && !by_time) return;

    // Per-frame averages in milliseconds. builder includes GetClientRect since
    // it's a cheap pre-pass and not separately useful.
    const double total_ms  = qpc_to_ms(qpc_freq_, t_total_);
    const double build_ms  = qpc_to_ms(qpc_freq_, t_builder_);
    const double layout_ms = qpc_to_ms(qpc_freq_, t_layout_);
    const double damage_ms = qpc_to_ms(qpc_freq_, t_damage_);
    const double draw_ms   = qpc_to_ms(qpc_freq_, t_draw_);
    const double avg_total = frame_counter_ > 0 ? total_ms / frame_counter_ : 0.0;
    const double avg_build = frame_counter_ > 0 ? build_ms / frame_counter_ : 0.0;
    const double avg_layout= frame_counter_ > 0 ? layout_ms/ frame_counter_ : 0.0;
    const double avg_damage= frame_counter_ > 0 ? damage_ms/ frame_counter_ : 0.0;
    const double avg_draw  = frame_counter_ > 0 ? draw_ms  / frame_counter_ : 0.0;

    wchar_t buf[256];
    std::swprintf(buf, std::size(buf),
                  L"[WinFlux] %u frames: total=%.3fms build=%.3f layout=%.3f damage=%.3f draw=%.3f",
                  frame_counter_, avg_total, avg_build, avg_layout, avg_damage, avg_draw);
    wchar_t tail[64];
    std::swprintf(tail, std::size(tail), L" (%u skipped)",
                  static_cast<unsigned>(skip_counter_));
    wcsncat_s(buf, tail, _TRUNCATE);
#ifdef _DEBUG
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
#endif
    // WINFLUX_PERF_LOG=<path>: append the same summary for headless perf
    // measurement (release builds included).
    if (const char* path = perf_log_path()) {
        if (FILE* f = std::fopen(path, "a")) {
            std::fprintf(f, "%ls\n", buf);
            std::fclose(f);
        }
    }

    t_total_ = t_builder_ = t_layout_ = t_damage_ = t_draw_ = 0;
    frame_counter_ = 0;
    skip_counter_ = 0;
    perf_emit_ms_ = now_ms;
}

const char* Host::perf_log_path() noexcept {
    static const char* path = std::getenv("WINFLUX_PERF_LOG");
    return (path != nullptr && path[0] != '\0') ? path : nullptr;
}

void Host::render_element(const Element& e, const Path& path, const Rect& clip) {
    const Props& p = e.props;
    const Rect r{e.x, e.y, e.w, e.h};

    // Cull subtrees outside the dirty rect. For a small partial repaint this
    // avoids walking the entire scene tree; for a fullscreen dirty rect
    // (clip covers everything) the test always passes. An element whose
    // bounds don't intersect the clip can still affect rendering if its
    // children do — but children are clipped to the parent container in the
    // DOM-equivalent sense, so we only descend when the parent intersects.
    if (!rects_intersect(r, clip)) return;

    const bool hovered = is_prefix(path, hover_path_) && !path.empty();
    const bool hover_prev = is_prefix(path, hover_from_path_) && !path.empty();
    const unsigned long long now_ms = GetTickCount64();
    const float hover_t = p.enabled
        ? approach_blend(hovered, hover_prev, now_ms, hover_change_ms_,
                         static_cast<unsigned>(tokens::motion_fast_ms))
        : 0.0f;
    const bool pressed = path == pressed_path_;
    const bool press_prev = path == press_from_path_;
    const float press_t = p.enabled
        ? approach_blend(pressed, press_prev, now_ms, press_change_ms_,
                         static_cast<unsigned>(tokens::motion_fast_ms))
        : 0.0f;
    const bool focused = path == focus_path_;
    const Palette& pal = shown_;
    const float op = std::clamp(p.opacity, 0.0f, 1.0f);

    // Background stack: shadow below, gradient (or solid) fill, border on
    // top. An opacity of 0 hides the element's own paint but not its
    // children (they carry their own opacity).
    if (op > 0.001f) {
        if (p.shadow_elevation > 0) {
            renderer_.draw_shadow(r, p.radius, p.shadow_elevation);
        }
        if (p.background_gradient.has_value()) {
            renderer_.fill_round_rect_gradient(r, p.radius, *p.background_gradient, op);
        } else if (p.background.has_value()) {
            // Disabled snaps; hover/press highlight ramps over
            // motion_fast_ms (blend of the base with each state colour).
            Color bg = *p.background;
            if (!p.enabled && p.disabled_background.has_value()) {
                bg = *p.disabled_background;
            } else {
                // X-4: hover/press states DEFAULT for interactive elements
                // (review 2026-10-06 — the library injects the affordance
                // instead of every caller hand-passing Props). Explicit
                // Props win; non-interactive elements with an explicit
                // hover_background still animate it.
                const bool interactive = p.on_click || p.on_value_change;
                const std::optional<Color> hover_c =
                    p.hover_background.has_value() ? p.hover_background
                    : interactive ? std::optional<Color>{pal.surface_hover} : std::nullopt;
                const std::optional<Color> press_c =
                    p.pressed_background.has_value() ? p.pressed_background
                    : interactive ? std::optional<Color>{pal.surface_variant} : std::nullopt;
                if (hover_t > 0.0f && hover_c.has_value()) bg = lerp(bg, *hover_c, hover_t);
                if (press_t > 0.0f && press_c.has_value()) bg = lerp(bg, *press_c, press_t);
            }
            renderer_.fill_round_rect(r, p.radius, bg, op);
        } else if (p.enabled && (hover_t > 0.0f || press_t > 0.0f)
                   && (p.on_click || p.on_value_change)) {
            // X-4: interactive carriers without their own background (tabs,
            // table rows on plain stripes, breadcrumb links) get a default
            // hover/press wash, faded in through the state blend factor.
            const Color hover_c = p.hover_background.value_or(pal.surface_hover);
            const Color press_c = p.pressed_background.value_or(pal.surface_variant);
            if (press_t > 0.0f) {
                renderer_.fill_round_rect(r, p.radius, press_c, op * press_t * 0.9f);
            } else if (hover_t > 0.0f) {
                renderer_.fill_round_rect(r, p.radius, hover_c, op * hover_t * 0.9f);
            }
        }
        if (p.border_color.has_value() && p.border_width > 0.0f) {
            const float half = p.border_width * 0.5f;
            renderer_.draw_round_rect(Rect{r.x + half, r.y + half, r.w - p.border_width, r.h - p.border_width},
                                      p.radius, *p.border_color, p.border_width, op);
        }
        // Custom painter (charts, gauges): after the chrome, before
        // children. The pointer position is element-relative so painters
        // can render crosshairs/value bubbles without knowing layout.
        if (p.paint) {
            p.paint(renderer_, r.x, r.y, r.w, r.h, hovered,
                    hovered ? last_mouse_lx_ - r.x : -1.0f,
                    hovered ? last_mouse_ly_ - r.y : -1.0f);
        }
    }

    // Typed-element glyphs.
    if (e.type == L"button" || e.type == L"label") {
        const Color tc = (!p.enabled && p.disabled_text_color.has_value())
                             ? *p.disabled_text_color
                             : p.text_color.value_or(pal.text);
        // Optional inline icon (20 px box) shifts the text rect right.
        Rect text_r = r;
        if (p.icon.has_value() && op > 0.001f) {
            constexpr float kInline = 20.0f;
            const float iy = r.y + (r.h - kInline) * 0.5f;
            renderer_.draw_icon(Rect{r.x + p.padding.left, iy, kInline, kInline},
                                *p.icon, tc, 1.8f, op);
            text_r = Rect{r.x + p.padding.left + kInline + static_cast<float>(tokens::spacing_sm),
                          r.y,
                          r.w - p.padding.left - kInline - static_cast<float>(tokens::spacing_sm) - p.padding.right,
                          r.h};
        }
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, p.bold, p.text_align,
                            p.text_wrap, p.text_ellipsis, op);
    } else if (e.type == L"icon") {
        const float side = std::min(r.w, r.h);
        if (side > 0.0f && op > 0.001f) {
            const float ix = r.x + (r.w - side) * 0.5f;
            const float iy = r.y + (r.h - side) * 0.5f;
            renderer_.draw_icon(Rect{ix, iy, side, side}, p.icon.value_or(IconKind::close),
                                p.text_color.value_or(pal.text), side / 12.0f, op);
        }
    } else if (e.type == L"checkbox") {
        const float box = static_cast<float>(tokens::checkbox_box);
        const float box_y = e.y + (e.h - box) * 0.5f;
        const Rect box_r{e.x, box_y, box, box};
        const bool is_checked = p.checked.value_or(false);
        const Color border_c = !p.enabled ? pal.divider
                              : is_checked || hovered || focused ? pal.accent : pal.border;
        renderer_.fill_round_rect(box_r, static_cast<float>(tokens::radius_sm),
                                  is_checked && p.enabled ? pal.accent : pal.surface, op);
        renderer_.draw_round_rect(box_r, static_cast<float>(tokens::radius_sm), border_c, 1.5f, op);
        if (is_checked) {
            const Color tick = p.enabled ? pal.on_accent : pal.divider;
            renderer_.draw_line(e.x + box * 0.22f, box_y + box * 0.52f,
                                e.x + box * 0.42f, box_y + box * 0.72f, tick, 2.0f, op);
            renderer_.draw_line(e.x + box * 0.42f, box_y + box * 0.72f,
                                e.x + box * 0.78f, box_y + box * 0.30f, tick, 2.0f, op);
        }
        const Color tc = p.enabled ? pal.text : pal.text_secondary;
        const Rect text_r{e.x + box + tokens::spacing_sm, e.y,
                          e.w - box - tokens::spacing_sm, e.h};
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, false, Align::start, op);
    } else if (e.type == L"progress") {
        renderer_.fill_round_rect(r, p.radius, pal.surface_variant, op);
        const float fill_w = e.w * p.progress_value;
        if (fill_w > 0.5f) {
            renderer_.push_clip(r);
            renderer_.fill_round_rect(Rect{e.x, e.y, fill_w, e.h}, p.radius, pal.accent, op);
            renderer_.pop_clip();
        }
    } else if (e.type == L"image" && p.image_path.has_value()) {
        renderer_.draw_image(*p.image_path,
                             Rect{r.x + p.padding.left, r.y + p.padding.top,
                                  r.w - p.padding.left - p.padding.right,
                                  r.h - p.padding.top - p.padding.bottom},
                             op);
    } else if (e.type == L"text_input") {
        const bool is_focused = path == focused_input_path_;
        const Color bg = is_focused && p.enabled ? pal.surface
                      : !p.enabled ? pal.surface_variant
                      : (p.background.value_or(pal.surface));
        const Color border_c = p.input_error && p.enabled ? pal.danger
                            : is_focused && p.enabled ? pal.accent
                            : !p.enabled ? pal.divider
                            : (p.border_color.value_or(pal.border));
        renderer_.fill_round_rect(r, p.radius, bg, op);
        renderer_.draw_round_rect(Rect{r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f},
                                  p.radius, border_c, p.border_width, op);
        const Color tc = p.enabled ? pal.text : pal.text_secondary;
        const Rect text_r{r.x + p.padding.left, r.y,
                          r.w - p.padding.left - p.padding.right, r.h};
        // F1: masked inputs render bullets; the caret/selection indices still
        // address the real text (same length, so mapping is 1:1).
        const std::wstring shown = p.masked ? std::wstring(p.text.size(), L'●') : p.text;
        const size_t caret = std::min(input_caret_, shown.size());
        const size_t anchor = std::min(input_anchor_, shown.size());
        if (is_focused && p.enabled && caret != anchor) {
            const auto [w1, h1] = renderer_.measure_text(
                shown.substr(0, std::min(caret, anchor)), p.font_size_pt, false);
            const auto [w2, h2] = renderer_.measure_text(
                shown.substr(0, std::max(caret, anchor)), p.font_size_pt, false);
            renderer_.fill_rect(Rect{text_r.x + w1, r.y + 2.0f, w2 - w1, r.h - 4.0f},
                                pal.selection);
        }
        if (!shown.empty()) {
            renderer_.draw_text(text_r, shown, p.font_size_pt, tc, false, Align::start, op);
        } else if (!p.placeholder.empty()) {
            renderer_.draw_text(text_r, p.placeholder, p.font_size_pt, pal.text_secondary,
                                false, Align::start, op);
        }
        // IME inline composition (F6): underlined ghost text after the real
        // content; the caret follows it while composing.
        if (is_focused && p.enabled && !ime_composition_.empty()) {
            const auto [tw, th] = renderer_.measure_text(shown, p.font_size_pt, false);
            const Rect comp_r{text_r.x + tw, r.y,
                              text_r.w - tw, r.h};
            renderer_.draw_text(comp_r, ime_composition_, p.font_size_pt, tc, false,
                                Align::start, op);
            const auto [cw, ch2] = renderer_.measure_text(ime_composition_,
                                                          p.font_size_pt, false);
            renderer_.fill_rect(Rect{comp_r.x, comp_r.y + r.h * 0.5f + th * 0.5f - 2.0f,
                                     cw, 1.5f}, pal.accent);
        }
        // Caret: thin accent line at the caret index. No blink animation in
        // this pass — a blink must dirty only the input's rect (partial
        // animation), which the framework doesn't have yet; a steady caret
        // is enough to communicate focus without a 60fps fullscreen cost.
        if (is_focused && p.enabled) {
            const auto [tw, th] = renderer_.measure_text(shown.substr(0, caret),
                                                         p.font_size_pt, false);
            const float caret_x = text_r.x + tw;
            const float caret_h = th;
            const float caret_y = r.y + (r.h - caret_h) * 0.5f;
            renderer_.fill_round_rect(Rect{caret_x, caret_y, 1.5f, caret_h},
                                      0.5f, pal.accent, op);
        }
    } else if (e.type == L"slider") {
        const float track_h = 4.0f;
        const float track_y = r.y + (r.h - track_h) * 0.5f;
        const Color track_c = !p.enabled ? pal.divider
                          : (path == pressed_path_) ? pal.accent
                          : pal.surface_variant;
        renderer_.fill_round_rect(Rect{r.x, track_y, r.w, track_h},
                                  track_h * 0.5f, track_c, op);
        const float fill_w = std::clamp(r.w * std::clamp(p.slider_value, 0.0f, 1.0f),
                                        0.0f, r.w);
        if (fill_w > 0.5f) {
            renderer_.fill_round_rect(Rect{r.x, track_y, fill_w, track_h},
                                      track_h * 0.5f,
                                      p.enabled ? pal.accent : pal.divider, op);
        }
        const float thumb_r = (r.h - track_h) * 0.5f + 2.0f;
        const float thumb_x = r.x + fill_w;
        const float thumb_y = r.y + r.h * 0.5f;
        const Color thumb_c = !p.enabled ? pal.text_secondary : pal.accent;
        renderer_.fill_round_rect(
            Rect{thumb_x - thumb_r, thumb_y - thumb_r, thumb_r * 2.0f, thumb_r * 2.0f},
            thumb_r, thumb_c, op);
    } else if (e.type == L"toggle") {
        const bool is_on = p.checked.value_or(false);
        const Color track_c = !p.enabled ? pal.divider
                          : is_on ? pal.accent
                          : (path == pressed_path_) ? pal.text_secondary
                          : pal.surface_variant;
        renderer_.fill_round_rect(r, r.h * 0.5f, track_c, op);
        const float knob_r = (r.h - 4.0f) * 0.5f;
        const float knob_x = is_on ? (r.x + r.w - r.h * 0.5f)
                                   : (r.x + r.h * 0.5f);
        const float knob_y = r.y + r.h * 0.5f;
        renderer_.fill_round_rect(
            Rect{knob_x - knob_r, knob_y - knob_r, knob_r * 2.0f, knob_r * 2.0f},
            knob_r, p.enabled ? pal.on_accent : pal.text_secondary, op);
    }

    // Focus ring — 2px accent, outside-in on the control bounds.
    if (focused && p.focusable && p.enabled) {
        renderer_.draw_round_rect(Rect{r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f},
                                  p.radius, pal.accent, 2.0f, op);
    }

    // Children paint over the container background (painter's algorithm).
    // scroll_view content is additionally clipped to the viewport and the
    // floating scrollbar is drawn on top afterwards.
    const bool is_scroll = e.type == L"scroll_view";
    if (is_scroll) {
        renderer_.push_clip(Rect{r.x + p.padding.left, r.y + p.padding.top,
                                 r.w - p.padding.left - p.padding.right,
                                 r.h - p.padding.top - p.padding.bottom});
    }
    for (size_t i = 0; i < e.children.size(); ++i) {
        Path child_path = path;
        child_path.push_back(static_cast<int>(i));
        render_element(*e.children[i], child_path, clip);
    }
    if (is_scroll) {
        renderer_.pop_clip();
        draw_scrollbar(e, path, now_ms);
    }
}

void Host::draw_scrollbar(const Element& e, const Path& path, unsigned long long now_ms) noexcept {
    const auto it = scrolls_.find(path);
    if (it == scrolls_.end()) return;
    const ScrollState& st = it->second;
    if (st.content_h <= st.viewport_h || st.viewport_h <= 0.0f) return;

    // Auto-hide: solid for a 600 ms hold after the last scroll, then fade
    // over motion_medium_ms (the clock armed at scroll time keeps frames
    // coming through the fade).
    constexpr unsigned long long kHoldMs = 600;
    const float fade_t = static_cast<float>(now_ms - st.last_scroll_ms - kHoldMs) /
                         static_cast<float>(tokens::motion_medium_ms);
    const float bar_alpha = fade_t >= 1.0f ? 0.0f : (fade_t <= 0.0f ? 1.0f : 1.0f - fade_t);
    if (bar_alpha <= 0.0f) return;

    const Props& p = e.props;
    const float vw = e.w - p.padding.left - p.padding.right;
    const float track_x = e.x + p.padding.left + vw - static_cast<float>(tokens::scrollbar_width);
    const float track_y = e.y + p.padding.top;
    const float vh = st.viewport_h;
    const float max_off = st.content_h - vh;
    const float thumb_h = std::max(24.0f, vh * (vh / st.content_h));
    const float thumb_y = track_y + (vh - thumb_h) * (max_off > 0.0f ? st.offset_y / max_off : 0.0f);

    const Palette& pal = shown_;
    const float track_w = 3.0f;
    renderer_.fill_round_rect(Rect{track_x + static_cast<float>(tokens::scrollbar_width) - track_w,
                                   track_y, track_w, vh},
                              track_w * 0.5f, pal.divider, 0.35f * bar_alpha);
    renderer_.fill_round_rect(Rect{track_x, thumb_y,
                                   static_cast<float>(tokens::scrollbar_width), thumb_h},
                              static_cast<float>(tokens::scrollbar_width) * 0.5f,
                              pal.text_secondary, 0.55f * bar_alpha);
}

} // namespace flux
