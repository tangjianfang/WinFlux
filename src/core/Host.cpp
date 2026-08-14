#include <flux/Host.hpp>

#include <windowsx.h>

#include <algorithm>

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

// RAII guard for the painting_ re-entrancy flag. paint_frame is noexcept but
// wndproc catches any exception at the boundary; this guarantees the flag
// resets on every exit path (gen-check early-return, dirty_rects.empty()
// return, normal completion, or a throw) without scattering `painting_ = false`
// at each return site.
struct PaintScope {
    bool& flag;
    explicit PaintScope(bool& f) noexcept : flag(f) { flag = true; }
    ~PaintScope() noexcept { flag = false; }
    PaintScope(const PaintScope&) = delete;
    PaintScope& operator=(const PaintScope&) = delete;
};

#ifdef _DEBUG
// Inline QueryPerformanceCounter helper; one source for the cast so the
// rest of the code reads as `qpc()` -> long long.
inline long long qpc() noexcept {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
inline double qpc_to_ms(long long freq, long long delta) noexcept {
    return freq > 0 ? (static_cast<double>(delta) * 1000.0 / static_cast<double>(freq))
                    : 0.0;
}
#endif
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
            case WM_MOUSEWHEEL:
                host->handle_mouse_wheel(static_cast<int>(GET_WHEEL_DELTA_WPARAM(wparam)),
                                         GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
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
            case WM_NCDESTROY:
                host->hwnd_ = nullptr;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                break;
            default:
                break;
            }
        } catch (...) {
            // Exceptions must never cross the WndProc boundary.
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

Host::Host() noexcept { shown_ = flux::palette(mode_); }

Host::~Host() { destroy(); }

void Host::destroy() noexcept {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Host::create(const Config& config, HINSTANCE instance) noexcept {
    instance_ = instance;

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
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, instance, this);
    if (hwnd == nullptr) return false;

    if (dcomp_presenter_.create(hwnd)) {
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

void Host::set_root_builder(std::function<ElementPtr()> builder) noexcept {
    root_builder_ = std::move(builder);
    request_render();
}

void Host::overlay(ElementPtr root) noexcept {
    // Only meaningful while the builder runs (paint_frame clears the set
    // before invoking it); a stray call outside a frame is dropped.
    if (painting_ && root != nullptr) frame_overlays_.push_back(std::move(root));
}

void Host::request_render() noexcept {
    // Bump BEFORE the painting_ guard so a state mutation fired from inside
    // a click handler (which runs with painting_ == true and would otherwise
    // early-return here) still invalidates the next paint_frame's baseline.
    (void)bump_generation();
    if (hwnd_ == nullptr || painting_) return;
    paint_frame();   // drive the presenter directly (no InvalidateRect round-trip)
    if (hwnd_ != nullptr) ValidateRect(hwnd_, nullptr);   // clear dirty; avoid a double WM_PAINT
}

void Host::set_theme_mode(ThemeMode mode) noexcept {
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

void Host::scroll_by(const Path& sv_path, float delta) noexcept {
    auto it = scrolls_.find(sv_path);
    if (it == scrolls_.end()) return;
    const float max_off = std::max(0.0f, it->second.content_h - it->second.viewport_h);
    scroll_to(sv_path, std::clamp(it->second.offset_y + delta, 0.0f, max_off));
}

void Host::scroll_to(const Path& sv_path, float offset) noexcept {
    auto it = scrolls_.find(sv_path);
    if (it == scrolls_.end()) return;
    const float max_off = std::max(0.0f, it->second.content_h - it->second.viewport_h);
    const float next = std::clamp(offset, 0.0f, max_off);
    if (next == it->second.offset_y) return;
    it->second.offset_y = next;
    it->second.last_scroll_ms = GetTickCount64();
    // Hold 600 ms + fade over motion_medium: arm the clock past both so the
    // auto-hide actually completes (see draw_scrollbar).
    animate_for(600u + static_cast<unsigned>(tokens::motion_medium_ms) + 32u);
    request_render();
}

void Host::handle_mouse_wheel(int delta, int px, int py) noexcept {
    const float lx = to_logical_px(px);
    const float ly = to_logical_px(py);
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

void Host::handle_timer() noexcept {
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
    if (!fading_ && now >= anim_deadline_ms_) {
        anim_deadline_ms_ = 0;
        if (hwnd_ != nullptr) KillTimer(hwnd_, anim_timer_id_);
    }
    request_render();   // next tween step, or the settle frame
}

float Host::to_logical_px(int px) const noexcept {
    const float dpi = renderer_.dpi();
    return dpi > 0.0f ? static_cast<float>(px) * 96.0f / dpi : static_cast<float>(px);
}

void Host::handle_size(unsigned width_px, unsigned height_px) noexcept {
    renderer_.resize(width_px, height_px);
    force_full_dirty_ = true;   // surface geometry changed — diff is meaningless
    request_render();
}

void Host::handle_dpi_changed(unsigned dpi, const RECT& suggested) noexcept {
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
    // button's cursor).
    for (Path walk = path; !walk.empty(); walk.pop_back()) {
        if (const Element* e = resolve(walk);
            e != nullptr && e->props.cursor != Cursor::arrow) {
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
        if (root.props.background.has_value()) {
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

void Host::handle_mouse_move(int px, int py) noexcept {
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
        if (const Element* e = resolve(pressed_path_);
            e != nullptr && e->type == L"slider" && e->w > 0.0f
            && e->props.on_value_change) {
            const float t = std::clamp((lx - e->x) / e->w, 0.0f, 1.0f);
            const float v = e->props.slider_min + t * (e->props.slider_max - e->props.slider_min);
            e->props.on_value_change(v);
        }
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
    }
}

void Host::handle_mouse_leave() noexcept {
    mouse_tracking_ = false;
    if (!hover_path_.empty()) {
        hover_path_.clear();
        request_render();   // drop the stale hover highlight
    }
}

void Host::handle_lbutton_down(int px, int py) noexcept {
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
        // Walk up: an interactive ancestor owns the press (label inside button).
        Path walk = path;
        while (!walk.empty()) {
            if (const Element* e = resolve(walk); e != nullptr && e->props.on_click && e->props.enabled) {
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
        if (const Element* e = resolve(new_pressed);
            e != nullptr && e->type == L"slider" && e->w > 0.0f
            && e->props.on_value_change) {
            const float t = std::clamp((lx - e->x) / e->w, 0.0f, 1.0f);
            const float v = e->props.slider_min + t * (e->props.slider_max - e->props.slider_min);
            e->props.on_value_change(v);
        }
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
        request_render();
    }
}

void Host::handle_lbutton_up(int px, int py) noexcept {
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
            // The handler may mutate state -> rebuild happens next frame.
            last_click_lx_ = to_logical_px(px);
            last_click_ly_ = to_logical_px(py);
            e->props.on_click();
        }
    }
}

void Host::handle_key_down(WPARAM vk) noexcept {
    if (tree_ == nullptr) return;

    // Esc closes the topmost overlay: its root's on_click is the dismiss
    // semantics (scrim click and Esc share one handler).
    if (vk == VK_ESCAPE && !frame_overlays_.empty()) {
        const Element* top = frame_overlays_.back().get();
        if (top != nullptr && top->props.on_click && top->props.enabled) {
            top->props.on_click();
        }
        return;
    }

    // VK_BACK on a focused text_input deletes the trailing character. We do
    // this in WM_KEYDOWN because VK_BACK does not generate a WM_CHAR message.
    if (vk == VK_BACK && !focused_input_path_.empty()) {
        if (const Element* e = resolve(focused_input_path_);
            e != nullptr && e->type == L"text_input" && e->props.on_text_change) {
            std::wstring text = e->props.text;
            if (!text.empty()) text.pop_back();
            e->props.on_text_change(std::move(text));
        }
        return;
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

    std::vector<Path> order;
    collect_focusable(*tree_, Path{}, order);
    for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
        collect_focusable(*frame_overlays_[oi],
                          Path{-1, static_cast<int>(oi)}, order);
    }
    if (order.empty()) return;

    if (vk == VK_TAB) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        size_t index = 0;
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] == focus_path_) {
                index = i;
                break;
            }
        }
        index = shift ? (index + order.size() - 1) % order.size()
                      : (index + 1) % order.size();
        if (focus_path_ != order[index]) {
            focus_path_ = order[index];
            request_render();   // focus ring moved — partial diff covers it
        }
        return;
    }

    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (const Element* e = resolve(focus_path_);
            e != nullptr && e->props.on_click && e->props.enabled) {
            e->props.on_click();   // handler-driven render; no force_full_dirty
        }
    }
}

void Host::handle_char(WPARAM ch) noexcept {
    // WM_CHAR delivers UTF-16 code units (wchar_t on Windows). VK_BACK has
    // already been handled in WM_KEYDOWN, and we want printable characters
    // only — reject control codes so a stray WM_CHAR from the keyboard
    // autorepeat doesn't pollute the input.
    if (ch < 0x20) return;
    if (focused_input_path_.empty()) return;
    if (const Element* e = resolve(focused_input_path_);
        e != nullptr && e->type == L"text_input" && e->props.on_text_change) {
        std::wstring text = e->props.text;
        text.push_back(static_cast<wchar_t>(ch));
        e->props.on_text_change(std::move(text));
    }
}

// =====================================================================
// Frame pipeline: rebuild -> layout -> paint
// =====================================================================

void Host::paint_frame() noexcept {
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

#ifdef _DEBUG
    if (qpc_freq_ == 0) {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        qpc_freq_ = f.QuadPart;
    }
    const long long t_frame_start = qpc();
#endif

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float logical_w = to_logical_px(rc.right - rc.left);
    const float logical_h = to_logical_px(rc.bottom - rc.top);

#ifdef _DEBUG
    const long long t_after_rect = qpc();
#endif

    // Rebuild the scene tree (React re-render).
    tree_ = root_builder_ ? root_builder_() : nullptr;
    if (tree_ != nullptr) {
        LayoutContext ctx;
        ctx.measure_leaf = [this](const Element& e) -> std::pair<float, float> {
            const float box = static_cast<float>(tokens::checkbox_box);
            const auto [tw, th] = renderer_.measure_text(e.props.text, e.props.font_size_pt, e.props.bold);
            if (e.type == L"checkbox") {
                return {box + tokens::spacing_sm + tw, std::max(box, th)};
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
        scrolls_ = std::move(next_scrolls);
        // Overlays lay out against the full canvas (anchoring is pure
        // flex — see test_overlay_anchoring). They are collected inside
        // root_builder_(), which ran above.
        for (auto& ov : frame_overlays_) {
            if (ov != nullptr) flex_layout(*ov, logical_w, logical_h, ctx);
        }
    }

#ifdef _DEBUG
    const long long t_after_layout = qpc();
#endif

    // Compute damage. resize / DPI change / any in-flight animation (theme
    // fade or an animate_for tween) force a fullscreen update; otherwise
    // diff prev_tree_ against the new tree and also fold in interaction-state
    // changes (hover/press/focus) that the structural diff can't see.
    DamageTracker::InteractionState prev_interaction{
        prev_hover_path_, prev_pressed_path_, prev_focus_path_};
    DamageTracker::InteractionState next_interaction{
        hover_path_, pressed_path_, focus_path_};

    // Interaction state that points into an overlay can't be resolved
    // against the main tree by the damage diff — force full for that frame.
    // (Overlay set changes likewise: scrim appears/disappears everywhere.)
    const auto touches_overlay = [](const Path& p) noexcept {
        return !p.empty() && p[0] == -1;
    };
    const bool overlay_interaction = touches_overlay(hover_path_) ||
        touches_overlay(pressed_path_) || touches_overlay(focus_path_) ||
        touches_overlay(prev_hover_path_) || touches_overlay(prev_pressed_path_) ||
        touches_overlay(prev_focus_path_);
    const bool overlay_set_changed = frame_overlays_.size() != prev_overlays_.size();

    std::vector<Rect> dirty_rects;
    if (force_full_dirty_ || animating() || overlay_set_changed || overlay_interaction) {
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
        if (dirty_rects.size() > damage_tracker_.max_rects * 2) {
            dirty_rects.assign(1, Rect{0.0f, 0.0f, logical_w, logical_h});
        }
    }
    force_full_dirty_ = false;

#ifdef _DEBUG
    const long long t_after_damage = qpc();
#endif

    if (dirty_rects.empty()) {
        // Nothing changed; the composition visual is already correct. Do NOT
        // advance prev_tree_ / prev_*_path_ here — the next frame's diff
        // baseline must remain the last frame that actually drew. But DO
        // advance last_painted_generation_ so the gen short-circuit above
        // catches subsequent stray WM_PAINTs without re-running the diff.
        last_painted_generation_ = dirty_generation_;
#ifdef _DEBUG
        t_total_ += qpc() - t_frame_start;
        if (++frame_counter_ >= kFrameTimingInterval) { log_frame_summary(); }
#endif
        return;
    }

    // Iterate each dirty rect separately so the Presenter's sub-allocated
    // BeginDraw can target the actual changed region. render_element prunes
    // subtrees whose bounds don't intersect the dirty rect, so a partial
    // repaint only visits the elements that actually need new pixels.
#ifdef _DEBUG
    const long long t_before_draw = qpc();
#endif
    for (const Rect& dr : dirty_rects) {
        renderer_.begin(shown_.background, &dr);
        if (tree_ != nullptr) render_element(*tree_, Path{}, dr);
        for (size_t oi = 0; oi < frame_overlays_.size(); ++oi) {
            if (frame_overlays_[oi] != nullptr) {
                render_element(*frame_overlays_[oi],
                               Path{-1, static_cast<int>(oi)}, dr);
            }
        }
        renderer_.end();
    }

    // Snapshots advance AFTER a real draw, so a frame that produced no dirty
    // rects preserves the true previous baseline for the next diff.
    prev_tree_ = tree_;
    prev_overlays_ = frame_overlays_;
    prev_hover_path_   = hover_path_;
    prev_pressed_path_ = pressed_path_;
    prev_focus_path_   = focus_path_;
    last_painted_generation_ = dirty_generation_;

#ifdef _DEBUG
    const long long t_end = qpc();
    t_total_   += t_end - t_frame_start;
    t_builder_ += t_after_rect - t_frame_start;          // GetClientRect + tree build
    t_layout_  += t_after_layout - t_after_rect;
    t_damage_  += t_after_damage - t_after_layout;
    t_draw_    += t_end - t_before_draw;
    if (++frame_counter_ >= kFrameTimingInterval) { log_frame_summary(); }
#endif
}

#ifdef _DEBUG
void Host::log_frame_summary() noexcept {
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
                  L"[WinFlux] %u frames: total=%.3fms build=%.3f layout=%.3f damage=%.3f draw=%.3f\n",
                  frame_counter_, avg_total, avg_build, avg_layout, avg_damage, avg_draw);
    OutputDebugStringW(buf);

    t_total_ = t_builder_ = t_layout_ = t_damage_ = t_draw_ = 0;
    frame_counter_ = 0;
}
#endif

void Host::render_element(const Element& e, const Path& path, const Rect& clip) noexcept {
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
                if (hover_t > 0.0f && p.hover_background.has_value())
                    bg = lerp(bg, *p.hover_background, hover_t);
                if (press_t > 0.0f && p.pressed_background.has_value())
                    bg = lerp(bg, *p.pressed_background, press_t);
            }
            renderer_.fill_round_rect(r, p.radius, bg, op);
        }
        if (p.border_color.has_value() && p.border_width > 0.0f) {
            const float half = p.border_width * 0.5f;
            renderer_.draw_round_rect(Rect{r.x + half, r.y + half, r.w - p.border_width, r.h - p.border_width},
                                      p.radius, *p.border_color, p.border_width, op);
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
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, p.bold, p.text_align, op);
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
    } else if (e.type == L"text_input") {
        const bool is_focused = path == focused_input_path_;
        const Color bg = is_focused && p.enabled ? pal.surface
                      : !p.enabled ? pal.surface_variant
                      : (p.background.value_or(pal.surface));
        const Color border_c = is_focused && p.enabled ? pal.accent
                            : !p.enabled ? pal.divider
                            : (p.border_color.value_or(pal.border));
        renderer_.fill_round_rect(r, p.radius, bg, op);
        renderer_.draw_round_rect(Rect{r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f},
                                  p.radius, border_c, p.border_width, op);
        const Color tc = p.enabled ? pal.text : pal.text_secondary;
        const Rect text_r{r.x + p.padding.left, r.y,
                          r.w - p.padding.left - p.padding.right, r.h};
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, false, Align::start, op);
        // Caret: thin 1px accent line at the trailing edge of the text. No
        // blink animation in this pass; a blink would need a timer + State<T>
        // round-trip and the existing framework doesn't have a cheap hook
        // for it. Caret visibility IS enough to communicate focus.
        if (is_focused && p.enabled) {
            const auto [tw, th] = renderer_.measure_text(p.text, p.font_size_pt, false);
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
                          : (path == pressed_path_) ? pal.surface_variant
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
                          : (path == pressed_path_) ? pal.surface_variant
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
