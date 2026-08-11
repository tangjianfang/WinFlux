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
            case WM_TIMER:
                if (wparam == Host::fade_timer_id_) host->handle_timer();
                return 0;
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

void Host::request_render() noexcept {
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
    if (hwnd_ != nullptr) SetTimer(hwnd_, fade_timer_id_, fade_tick_ms_, nullptr);
    request_render();
}

void Host::handle_timer() noexcept {
    if (!fading_) {
        if (hwnd_ != nullptr) KillTimer(hwnd_, fade_timer_id_);
        return;
    }
    const unsigned long long elapsed = GetTickCount64() - fade_start_ms_;
    const float t = static_cast<float>(elapsed) / static_cast<float>(tokens::theme_fade_ms);
    if (t >= 1.0f) {
        shown_ = fade_to_;
        fading_ = false;
        if (hwnd_ != nullptr) KillTimer(hwnd_, fade_timer_id_);
    } else {
        shown_ = lerp_palette(fade_from_, fade_to_, ease_in_out(t));
    }
    request_render();
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
    if (tree_ == nullptr) return nullptr;
    const Element* node = tree_.get();
    for (const int index : path) {
        if (index < 0 || static_cast<size_t>(index) >= node->children.size()) return nullptr;
        node = node->children[static_cast<size_t>(index)].get();
    }
    return node;
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
    Path path;
    (void)hit_test(to_logical_px(px), to_logical_px(py), path);
    if (path != hover_path_) {
        hover_path_ = std::move(path);
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
    Path path;
    const Element* target = hit_test(to_logical_px(px), to_logical_px(py), path);
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
    }
    // Background click clears focus (target == nullptr OR no interactive
    // ancestor); keyboard activation on a now-blank focus path doesn't work
    // but matches Win32 tabbing behaviour: clicking outside any focusable
    // element drops focus.
    if (new_focus != focus_path_ || new_pressed != pressed_path_) {
        focus_path_ = std::move(new_focus);
        pressed_path_ = std::move(new_pressed);
        if (hwnd_ != nullptr && !pressed_path_.empty()) SetCapture(hwnd_);
        request_render();
    }
}

void Host::handle_lbutton_up(int px, int py) noexcept {
    if (hwnd_ != nullptr) ReleaseCapture();
    if (pressed_path_.empty()) return;

    Path under;
    (void)hit_test(to_logical_px(px), to_logical_px(py), under);
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
    pressed_path_.clear();
    request_render();   // drop the pressed highlight via partial diff

    if (inside) {
        if (const Element* e = resolve(fired);
            e != nullptr && e->props.on_click && e->props.enabled) {
            // The handler may mutate state -> rebuild happens next frame.
            e->props.on_click();
        }
    }
}

void Host::handle_key_down(WPARAM vk) noexcept {
    if (tree_ == nullptr) return;
    std::vector<Path> order;
    collect_focusable(*tree_, Path{}, order);
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

// =====================================================================
// Frame pipeline: rebuild -> layout -> paint
// =====================================================================

void Host::paint_frame() noexcept {
    if (painting_ || !renderer_.valid()) return;
    painting_ = true;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float logical_w = to_logical_px(rc.right - rc.left);
    const float logical_h = to_logical_px(rc.bottom - rc.top);

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
            return {tw, th};
        };
        flex_layout(*tree_, logical_w, logical_h, ctx);
    }

    // Compute damage. resize / DPI change / theme fade in progress force a
    // fullscreen update; otherwise diff prev_tree_ against the new tree and
    // also fold in interaction-state changes (hover/press/focus) that the
    // structural diff can't see.
    DamageTracker::InteractionState prev_interaction{
        prev_hover_path_, prev_pressed_path_, prev_focus_path_};
    DamageTracker::InteractionState next_interaction{
        hover_path_, pressed_path_, focus_path_};

    std::vector<Rect> dirty_rects;
    if (force_full_dirty_ || fading_) {
        if (logical_w > 0.0f && logical_h > 0.0f) {
            dirty_rects.push_back(Rect{0.0f, 0.0f, logical_w, logical_h});
        }
    } else {
        dirty_rects = damage_tracker_.compute(prev_tree_.get(), tree_.get(),
                                              logical_w, logical_h,
                                              &prev_interaction,
                                              &next_interaction);
    }
    force_full_dirty_ = false;

    if (dirty_rects.empty()) {
        // Nothing changed; the composition visual is already correct. Do NOT
        // advance prev_tree_ / prev_*_path_ here — the next frame's diff
        // baseline must remain the last frame that actually drew.
        painting_ = false;
        return;
    }

    // Iterate each dirty rect separately so the Presenter's sub-allocated
    // BeginDraw can target the actual changed region. render_element prunes
    // subtrees whose bounds don't intersect the dirty rect, so a partial
    // repaint only visits the elements that actually need new pixels.
    for (const Rect& dr : dirty_rects) {
        renderer_.begin(shown_.background, &dr);
        if (tree_ != nullptr) render_element(*tree_, Path{}, dr);
        renderer_.end();
    }

    // Snapshots advance AFTER a real draw, so a frame that produced no dirty
    // rects preserves the true previous baseline for the next diff.
    prev_tree_ = tree_;
    prev_hover_path_   = hover_path_;
    prev_pressed_path_ = pressed_path_;
    prev_focus_path_   = focus_path_;

    painting_ = false;
}

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
    const bool pressed = path == pressed_path_;
    const bool focused = path == focus_path_;
    const Palette& pal = shown_;

    // Background with state precedence: disabled > pressed > hover > rest.
    if (p.background.has_value()) {
        Color bg = *p.background;
        if (!p.enabled && p.disabled_background.has_value()) bg = *p.disabled_background;
        else if (pressed && p.pressed_background.has_value()) bg = *p.pressed_background;
        else if (hovered && p.enabled && p.hover_background.has_value()) bg = *p.hover_background;
        renderer_.fill_round_rect(r, p.radius, bg);
    }
    if (p.border_color.has_value() && p.border_width > 0.0f) {
        const float half = p.border_width * 0.5f;
        renderer_.draw_round_rect(Rect{r.x + half, r.y + half, r.w - p.border_width, r.h - p.border_width},
                                  p.radius, *p.border_color, p.border_width);
    }

    // Typed-element glyphs.
    if (e.type == L"button" || e.type == L"label") {
        const Color tc = (!p.enabled && p.disabled_text_color.has_value())
                             ? *p.disabled_text_color
                             : p.text_color.value_or(pal.text);
        renderer_.draw_text(r, p.text, p.font_size_pt, tc, p.bold, p.text_align);
    } else if (e.type == L"checkbox") {
        const float box = static_cast<float>(tokens::checkbox_box);
        const float box_y = e.y + (e.h - box) * 0.5f;
        const Rect box_r{e.x, box_y, box, box};
        const bool is_checked = p.checked.value_or(false);
        const Color border_c = !p.enabled ? pal.divider
                              : is_checked || hovered || focused ? pal.accent : pal.border;
        renderer_.fill_round_rect(box_r, static_cast<float>(tokens::radius_sm),
                                  is_checked && p.enabled ? pal.accent : pal.surface);
        renderer_.draw_round_rect(box_r, static_cast<float>(tokens::radius_sm), border_c, 1.5f);
        if (is_checked) {
            const Color tick = p.enabled ? pal.on_accent : pal.divider;
            renderer_.draw_line(e.x + box * 0.22f, box_y + box * 0.52f,
                                e.x + box * 0.42f, box_y + box * 0.72f, tick, 2.0f);
            renderer_.draw_line(e.x + box * 0.42f, box_y + box * 0.72f,
                                e.x + box * 0.78f, box_y + box * 0.30f, tick, 2.0f);
        }
        const Color tc = p.enabled ? pal.text : pal.text_secondary;
        const Rect text_r{e.x + box + tokens::spacing_sm, e.y,
                          e.w - box - tokens::spacing_sm, e.h};
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, false, Align::start);
    } else if (e.type == L"progress") {
        renderer_.fill_round_rect(r, p.radius, pal.surface_variant);
        const float fill_w = e.w * p.progress_value;
        if (fill_w > 0.5f) {
            renderer_.push_clip(r);
            renderer_.fill_round_rect(Rect{e.x, e.y, fill_w, e.h}, p.radius, pal.accent);
            renderer_.pop_clip();
        }
    }

    // Focus ring — 2px accent, outside-in on the control bounds.
    if (focused && p.focusable && p.enabled) {
        renderer_.draw_round_rect(Rect{r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f},
                                  p.radius, pal.accent, 2.0f);
    }

    // Children paint over the container background (painter's algorithm).
    for (size_t i = 0; i < e.children.size(); ++i) {
        Path child_path = path;
        child_path.push_back(static_cast<int>(i));
        render_element(*e.children[i], child_path, clip);
    }
}

} // namespace flux
