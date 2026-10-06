#pragma once

// WinFlux Host — the single-HWND application shell. Owns the window,
// the Direct2D renderer, the frame pipeline (rebuild -> layout -> paint),
// interaction state (hover/pressed/focus paths), and the theme cross-fade.
// Object address is stable until WM_NCDESTROY finishes (same invariant as
// nfui); the WndProc is noexcept and never lets exceptions escape.

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "D2DRenderer.hpp"
#include "DCompPresenter.hpp"
#include "Damage.hpp"
#include "HwndPresenter.hpp"
#include "Animation.hpp"
#include "Element.hpp"
#include "FlexLayout.hpp"
#include "Presenter.hpp"
#include "Theme.hpp"

namespace flux {

// React useState analogue. Outlives the per-frame element tree; set() writes
// the value and marks the Host dirty so the next frame rebuilds the tree.
template <class T>
class State {
public:
    State() : impl_(std::make_shared<Impl>()) {}
    explicit State(T init) : impl_(std::make_shared<Impl>(std::move(init))) {}

    [[nodiscard]] const T& get() const noexcept { return impl_->value; }
    void set(T value) {
        impl_->value = std::move(value);
        if (impl_->notify) impl_->notify();
    }
    void bind(std::function<void()> notify) { impl_->notify = std::move(notify); }

private:
    struct Impl {
        T value{};
        std::function<void()> notify;
        Impl() = default;
        explicit Impl(T v) : value(std::move(v)) {}
    };
    std::shared_ptr<Impl> impl_;
};

class Host {
public:
    struct Config {
        std::wstring title = L"WinFlux";
        int width = 960;
        int height = 640;
        int x = -1;   // -1/-1 => CW_USEDEFAULT (or a restored placement)
        int y = -1;
    };

    Host() noexcept;
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    [[nodiscard]] bool create(const Config& config, HINSTANCE instance) noexcept;
    int run() noexcept;   // message loop; returns WM_QUIT exit code

    void set_root_builder(std::function<ElementPtr()> builder);
    void request_render();

    // Declarative overlay channel. Call from the root builder for each
    // overlay visible THIS frame (the set resets on every paint_frame).
    // Overlay roots are laid out against the full canvas, drawn on top of
    // the main tree, and hit-tested first (later entries on top). A root
    // with a background colour acts as a scrim: it blocks hits to layers
    // below and its on_click is the Esc/close semantics. Roots without a
    // background are click-through where no child is hit.
    void overlay(ElementPtr root) noexcept;

    // Keep the animation clock ticking for `ms` more milliseconds. Safe to
    // call from builders (runs inside paint_frame): it only arms a timer;
    // the ticks call request_render, which re-enters cleanly.
    void animate_for(unsigned ms) noexcept;

    // Read an Animated<T> for this frame's build and extend the animation
    // clock until the tween settles. Builders call this instead of
    // current() so frames keep arriving while the tween is in flight.
    template <class T>
    [[nodiscard]] T animate(const Animated<T>& a) noexcept {
        const unsigned long long now = Animated<T>::now();
        if (a.active_at(now)) animate_for(a.remaining_at(now) + 1);
        return a.current_at(now);
    }

    // App-level key hook, consulted first on every WM_KEYDOWN (e.g. the
    // command palette's Ctrl+K). Return true to consume the key.
    void set_key_hook(std::function<bool(unsigned vk)> hook) noexcept {
        key_hook_ = std::move(hook);
    }
    // Right-click notification (WM_CONTEXTMENU) in logical client units.
    void set_context_menu_handler(std::function<void(float lx, float ly)> h) noexcept {
        context_menu_handler_ = std::move(h);
    }
    // WM_CLOSE veto hook (review 2026-10-06 F5): runs while the window is
    // still alive — the place to persist placement/settings. Return false
    // to keep the window open (dirty-form confirm); true (or no handler)
    // proceeds with destruction.
    void set_close_handler(std::function<bool()> handler) noexcept {
        close_handler_ = std::move(handler);
    }

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    // Logical-unit position of the most recent completed click (used by
    // anchored popups that don't know their trigger's laid-out rect).
    [[nodiscard]] std::pair<float, float> last_click_pos() const noexcept {
        return {last_click_lx_, last_click_ly_};
    }
    [[nodiscard]] ThemeMode theme_mode() const noexcept { return mode_; }
    void set_theme_mode(ThemeMode mode);   // starts 200 ms cross-fade
    [[nodiscard]] const Palette& palette() const noexcept { return shown_; }

    // useState factory — binds the state to this Host's render invalidation.
    template <class T>
    [[nodiscard]] State<T> make_state(T init = T{}) {
        State<T> s(std::move(init));
        s.bind([this] { request_render(); });
        return s;
    }

private:
    using Path = std::vector<int>;

    // Scroll offsets and measured extents, keyed by the scroll_view's
    // positional path (same identity model as hover/pressed paths: stable
    // while the tree shape is stable). Rebuilt every frame by
    // layout_scroll_views; offsets carry over frame to frame by path match.
    struct ScrollState {
        float offset_y = 0.0f;
        float content_h = 0.0f;
        float viewport_h = 0.0f;
        unsigned long long last_scroll_ms = 0;
    };
    std::map<Path, ScrollState> scrolls_;

    void layout_scroll_views(Element& e, const Path& prefix, const LayoutContext& ctx,
                             std::map<Path, ScrollState>& next) noexcept;
    void draw_scrollbar(const Element& e, const Path& path, unsigned long long now_ms) noexcept;

    // Overlays collected by overlay() during this frame's build plus the
    // previous frame's laid-out snapshot for the damage diff. Overlay
    // element paths carry a {-1, index} sentinel prefix so they never
    // collide with main-tree paths.
    std::vector<ElementPtr> frame_overlays_;
    std::vector<ElementPtr> prev_overlays_;
    // Overlay-aware hit test: overlays topmost-first, then the main tree.
    [[nodiscard]] const Element* hit_test_all(float lx, float ly, Path& path) const noexcept;

    friend LRESULT CALLBACK host_wndproc(HWND, UINT, WPARAM, LPARAM) noexcept;

    void paint_frame();
    // Monotonic invalidation counter — see the dirty_generation_ block below.
    using Generation = unsigned long long;
    // Handler-dispatch bookkeeping (review 2026-10-06 R-2/R-5): callbacks are
    // copied to the stack before invocation — a handler that mutates State<T>
    // re-enters paint_frame, which replaces the scene tree and would destroy
    // the std::function still executing. Afterwards, if the handler scheduled
    // no repaint itself (plain app-struct mutation, no State::set), schedule
    // one so e.g. an overlay closed via Esc doesn't linger as stale pixels.
    // The generation snapshot tells the two cases apart for free.
    void ensure_repaint_after_handler(Generation before);
    void handle_mouse_move(int px, int py);
    void handle_mouse_leave();
    void handle_lbutton_down(int px, int py);
    void handle_lbutton_up(int px, int py);
    void handle_key_down(WPARAM vk);
    void handle_char(WPARAM ch);
    void handle_mouse_wheel(int delta, int px, int py);
    void handle_contextmenu(int screen_x, int screen_y);
    // Applies a scroll delta (or absolute target) to the scroll_view at
    // `sv_path`, clamping to range, arming the auto-hide clock, repainting.
    void scroll_by(const Path& sv_path, float delta);
    void scroll_to(const Path& sv_path, float offset);
    void handle_size(unsigned width_px, unsigned height_px);
    void handle_dpi_changed(unsigned dpi, const RECT& suggested);
    void handle_timer();
    void update_cursor() noexcept;
    void destroy() noexcept;

    // True while the theme fade or any animate_for() tween is in flight.
    // Frames in this window are treated as fullscreen-dirty: partial-dirty
    // only pays off when the scene is at rest.
    [[nodiscard]] bool animating() const noexcept {
        return fading_ || GetTickCount64() < anim_deadline_ms_;
    }

    [[nodiscard]] float to_logical_px(int px) const noexcept;
    [[nodiscard]] const Element* hit_test(float lx, float ly, Path& path) const noexcept;
    [[nodiscard]] const Element* resolve(const Path& path) const noexcept;
    void collect_focusable(const Element& e, const Path& prefix, std::vector<Path>& out) const noexcept;
    void render_element(const Element& e, const Path& path, const Rect& clip);
    [[nodiscard]] static bool rects_intersect(const Rect& a, const Rect& b) noexcept;

    static bool contains(const Element& e, float lx, float ly) noexcept;
    static bool is_prefix(const Path& prefix, const Path& full) noexcept;

    // Order matters: C++ destroys members in reverse declaration order, so the
    // presenters must outlive the renderer. D2DRenderer's destructor releases
    // the render target whose device is owned by whichever presenter is live;
    // violating that ordering during teardown is undefined behaviour.
    DCompPresenter dcomp_presenter_;   // default path
    HwndPresenter hwnd_presenter_;      // fallback when DComp creation fails
    IPresenter* presenter_ = nullptr;   // points at whichever presenter is live
    DamageTracker damage_tracker_;      // Phase 2 diff for partial repaint
    bool painting_ = false;             // re-entrancy guard for direct-draw
    bool force_full_dirty_ = false;     // resize/DPI/theme-fade => fullscreen dirty
    ElementPtr prev_tree_;              // previous frame's laid-out scene tree
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DWORD ui_thread_id_ = 0;   // single-thread invariant; asserted in debug builds
    std::function<ElementPtr()> root_builder_;
    ElementPtr tree_;

    // Bumped whenever something might have invalidated the rendered scene.
    // paint_frame compares this against last_painted_generation_; if they
    // match, the frame is a true no-op (no builder, no layout, no diff, no
    // draw) and we can skip everything. State::set, request_render, resize,
    // DPI change, theme fade, and the interaction handlers all bump it.
    // Sentinel ensures the first paint_frame() never matches (last != gen)
    // and so always runs the full pipeline.
    Generation dirty_generation_ = 0;
    Generation last_painted_generation_ = ~Generation{0};
    [[nodiscard]] Generation bump_generation() noexcept { return ++dirty_generation_; }

    ThemeMode mode_ = ThemeMode::light;
    Palette shown_{};         // palette currently displayed (fade-interpolated)
    Palette fade_from_{};
    Palette fade_to_{};
    unsigned long long fade_start_ms_ = 0;
    bool fading_ = false;

    Path hover_path_;
    Path pressed_path_;
    Path focus_path_;
    // Scrollbar thumb drag: the scroll_view being dragged plus the grab
    // offset inside the thumb (logical px).
    Path drag_scroll_;
    float drag_grab_dy_ = 0.0f;
    // Hover/press micro-transition state: the set as it was when it last
    // changed, plus the change timestamp. render_element blends the
    // highlight over motion_fast_ms while the animation clock keeps
    // frames coming (animating() => fullscreen dirty).
    Path hover_from_path_;
    Path press_from_path_;
    unsigned long long hover_change_ms_ = 0;
    unsigned long long press_change_ms_ = 0;
    // Last mouse position in logical units (WM_SETCURSOR hit-testing).
    float last_mouse_lx_ = 0.0f;
    float last_mouse_ly_ = 0.0f;
    // Logical position of the last completed click (anchored popups).
    float last_click_lx_ = 0.0f;
    float last_click_ly_ = 0.0f;
    // Host-native tooltip: armed when the hovered element carries a
    // tooltip_text and the pointer has dwelled for motion_slow_ms. Built
    // as an extra pass-through overlay after the builder's own overlays.
    void update_tooltip() noexcept;
    [[nodiscard]] ElementPtr build_tooltip(const std::wstring& text) const noexcept;
    void apply_autofocus() noexcept;
    // Cursor position the tooltip last rendered at — armed frames are only
    // requested while the pointer keeps moving (no idle repaint loop).
    float tooltip_follow_lx_ = -1.0f;
    float tooltip_follow_ly_ = -1.0f;
    // Unified animation-clock deadline (animate_for). The WM_TIMER stays
    // armed while now < deadline or a theme fade is running.
    unsigned long long anim_deadline_ms_ = 0;
    // Scrollbar auto-hide: hold-end timestamp (WAKE-only timer ticks —
    // frames are only requested once the fade window opens).
    unsigned long long scrollbar_fade_at_ms_ = 0;
    // App-level hooks (set_key_hook / set_context_menu_handler / set_close_handler).
    std::function<bool(unsigned vk)> key_hook_;
    std::function<bool()> close_handler_;
    std::function<void(float, float)> context_menu_handler_;
    // Tracks whether any element had Props.autofocus this frame.
    Path autofocus_path_;
    // The text input currently receiving WM_CHAR. A click on a text_input
    // updates this path; WM_CHAR appends (and VK_BACK deletes) one char and
    // invokes the input's on_click handler with the new text. Empty path
    // means no input is in edit mode.
    Path focused_input_path_;
    // Snapshots of the above from the previous paint_frame, used to feed the
    // damage diff so hover/press/focus changes produce partial repaints
    // instead of being forced into a fullscreen update.
    Path prev_hover_path_;
    Path prev_pressed_path_;
    Path prev_focus_path_;
    bool mouse_tracking_ = false;

    // Declared last → destroyed first → releases its render target before the
    // presenter's device is torn down (renderer_ relies on presenter_'s
    // device for ID2D1RenderTarget creation).
    D2DRenderer renderer_;

    static constexpr UINT_PTR anim_timer_id_ = 1;   // was fade_timer_id_
    static constexpr unsigned int anim_tick_ms_ = 16;   // ~60 fps tween steps

    // ---- Frame-timing instrumentation ------------------------------------
    // Per-phase QueryPerformanceCounter deltas + rolling frame counter. In
    // debug builds a one-line summary goes to the debugger every
    // kFrameTimingInterval frames OR every kPerfEmitMs of wall time —
    // whichever comes first — plus a final partial summary at teardown; in
    // ANY build, setting WINFLUX_PERF_LOG to a file path appends the same
    // summary there (perf measurement without attaching a profiler). Zero
    // cost when the env var is unset. (Time-based emission added in review
    // 2026-10-06 U-4: sub-120-frame scenarios used to log nothing at all.)
    static constexpr unsigned kFrameTimingInterval = 120;   // ~2s @60fps
    static constexpr unsigned long long kPerfEmitMs = 2000;
    void log_frame_summary(unsigned long long now_ms) noexcept;
    void flush_perf_log() noexcept;
    [[nodiscard]] static const char* perf_log_path() noexcept;
    long long qpc_freq_ = 0;
    unsigned frame_counter_ = 0;   // frames that reached the draw
    unsigned skip_counter_ = 0;    // pipeline ran, damage empty, nothing drew
    unsigned long long perf_emit_ms_ = 0;   // wall clock of the last summary
    long long t_total_ = 0;
    long long t_builder_ = 0;
    long long t_layout_ = 0;
    long long t_damage_ = 0;
    long long t_draw_ = 0;
};

} // namespace flux
