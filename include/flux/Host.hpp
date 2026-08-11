#pragma once

// WinFlux Host — the single-HWND application shell. Owns the window,
// the Direct2D renderer, the frame pipeline (rebuild -> layout -> paint),
// interaction state (hover/pressed/focus paths), and the theme cross-fade.
// Object address is stable until WM_NCDESTROY finishes (same invariant as
// nfui); the WndProc is noexcept and never lets exceptions escape.

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "D2DRenderer.hpp"
#include "DCompPresenter.hpp"
#include "Damage.hpp"
#include "HwndPresenter.hpp"
#include "Element.hpp"
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
    };

    Host() noexcept;
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    [[nodiscard]] bool create(const Config& config, HINSTANCE instance) noexcept;
    int run() noexcept;   // message loop; returns WM_QUIT exit code

    void set_root_builder(std::function<ElementPtr()> builder) noexcept;
    void request_render() noexcept;

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }
    [[nodiscard]] ThemeMode theme_mode() const noexcept { return mode_; }
    void set_theme_mode(ThemeMode mode) noexcept;   // starts 200 ms cross-fade
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

    friend LRESULT CALLBACK host_wndproc(HWND, UINT, WPARAM, LPARAM) noexcept;

    void paint_frame() noexcept;
    void handle_mouse_move(int px, int py) noexcept;
    void handle_mouse_leave() noexcept;
    void handle_lbutton_down(int px, int py) noexcept;
    void handle_lbutton_up(int px, int py) noexcept;
    void handle_key_down(WPARAM vk) noexcept;
    void handle_size(unsigned width_px, unsigned height_px) noexcept;
    void handle_dpi_changed(unsigned dpi, const RECT& suggested) noexcept;
    void handle_timer() noexcept;
    void destroy() noexcept;

    [[nodiscard]] float to_logical_px(int px) const noexcept;
    [[nodiscard]] const Element* hit_test(float lx, float ly, Path& path) const noexcept;
    [[nodiscard]] const Element* resolve(const Path& path) const noexcept;
    void collect_focusable(const Element& e, const Path& prefix, std::vector<Path>& out) const noexcept;
    void render_element(const Element& e, const Path& path, const Rect& clip) noexcept;
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
D2DRenderer renderer_;
    DamageTracker damage_tracker_;      // Phase 2 diff for partial repaint
    bool painting_ = false;             // re-entrancy guard for direct-draw
    bool force_full_dirty_ = false;     // resize/DPI/theme-fade => fullscreen dirty
    ElementPtr prev_tree_;              // previous frame's laid-out scene tree
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    std::function<ElementPtr()> root_builder_;
    ElementPtr tree_;

    ThemeMode mode_ = ThemeMode::light;
    Palette shown_{};         // palette currently displayed (fade-interpolated)
    Palette fade_from_{};
    Palette fade_to_{};
    unsigned long long fade_start_ms_ = 0;
    bool fading_ = false;

    Path hover_path_;
    Path pressed_path_;
    Path focus_path_;
    // Snapshots of the above from the previous paint_frame, used to feed the
    // damage diff so hover/press/focus changes produce partial repaints
    // instead of being forced into a fullscreen update.
    Path prev_hover_path_;
    Path prev_pressed_path_;
    Path prev_focus_path_;
    bool mouse_tracking_ = false;

    static constexpr UINT_PTR fade_timer_id_ = 1;
    static constexpr unsigned int fade_tick_ms_ = 16;
};

} // namespace flux
