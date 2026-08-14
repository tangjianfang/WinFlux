# Round 1 — 视觉地基 (Visual Foundation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 动画系统（时钟驱动的声明式补间 + 渲染期 hover/press 插值）、渐变/阴影/矢量图标/per-element 光标，Gallery 新增 Motion & Effects 演示页。

**Architecture:** 动画状态不进元素树：`Animated<T>`（app 侧）供 builder 每帧读取并经 `Host::animate_for` 挂住 `WM_TIMER` 时钟；时钟运行期间视同全脏（`animating()`）。渲染期 hover/press 高亮由 Host 记录的「变更前路径 + 变更时刻」插值。新增画笔字段（opacity / gradient / shadow / icon）全部进 `DamageTracker::paint_props_equal`。

**Tech Stack:** C++20 / Win32 / D2D（`SetOpacity`、`ID2D1LinearGradientBrush`、`ID2D1PathGeometry`）/ DWrite。无新第三方依赖。

## Global Constraints

- 零警告：`/W4 /permissive-`，双 preset（x64-debug / x64-release）构建零警告。
- 测试：`ctest --preset x64-debug` 与 `x64-release` 全绿（`WinFluxSmokeTest` 必须全过）。
- Token 三处同步：`include/flux/Theme.hpp` + `DESIGN.md` + `tests/smoke.cpp` 断言。
- 新画笔 Props 字段必须加入 `DamageTracker::paint_props_equal`（`src/render/Damage.cpp`）。
- 坐标全部逻辑单位（96 DPI 基线）；只有 presenter / `to_logical_px` 碰物理像素。
- 每任务一提交，沿用现有提交风格（`Round 1: ...` / `feat: ...`）。
- 设计偏差记录：渐变 v1 只做水平 L→R（`Gradient{from,to}` 无 angle 字段）——尚无消费者需要斜向，YAGNI。

---

### Task 1: Motion + shadow tokens（三处同步）

**Files:**
- Modify: `include/flux/Theme.hpp` (tokens namespace, `theme_fade_ms` 之后)
- Modify: `DESIGN.md` (Motion 表 + Elevation 小节)
- Test: `tests/smoke.cpp` (`test_theme` 尾部)

**Interfaces:**
- Produces: `tokens::motion_fast_ms(120)`, `tokens::motion_medium_ms(200)`, `tokens::motion_slow_ms(280)`, `tokens::shadow_blur_sm(4)`, `tokens::shadow_blur_md(8)`, `tokens::shadow_blur_lg(14)`, `tokens::shadow_alpha(0.10f)`, `tokens::shadow_offset_y(2.0f)`

- [x] **Step 1: 写失败断言** — `tests/smoke.cpp` `test_theme()` 的 `progress_height` 断言后追加：

```cpp
    // Motion tokens (Round 1 visual foundation).
    expect(tokens::motion_fast_ms == 120, "motion_fast_ms == 120");
    expect(tokens::motion_medium_ms == 200, "motion_medium_ms == 200");
    expect(tokens::motion_slow_ms == 280, "motion_slow_ms == 280");
    expect(tokens::shadow_blur_sm == 4, "shadow_blur_sm == 4");
    expect(tokens::shadow_blur_md == 8, "shadow_blur_md == 8");
    expect(tokens::shadow_blur_lg == 14, "shadow_blur_lg == 14");
    expect(near_eq(tokens::shadow_alpha, 0.10f), "shadow_alpha == 0.10");
    expect(near_eq(tokens::shadow_offset_y, 2.0f), "shadow_offset_y == 2.0");
```

- [x] **Step 2: 跑测试确认失败**

Run: `cmake --build --preset x64-debug --target WinFluxSmokeTest && out/build/x64-debug/Debug/WinFluxSmokeTest.exe`
Expected: 8 条 `[FAIL]`（token 未定义则编译失败——两种都算“失败确认”）。

- [x] **Step 3: Theme.hpp 加 token** — `theme_fade_ms` 行后：

```cpp
inline constexpr int motion_fast_ms = 120;    // hover / press micro-transitions
inline constexpr int motion_medium_ms = 200;  // expand / collapse, slides
inline constexpr int motion_slow_ms = 280;    // overlay enter / exit

// Hand-drawn shadow falloff (elevation 1..3 -> sm/md/lg blur). No D2D
// effects dependency: the renderer stacks concentric rounded rects.
inline constexpr int shadow_blur_sm = 4;
inline constexpr int shadow_blur_md = 8;
inline constexpr int shadow_blur_lg = 14;
inline constexpr float shadow_alpha = 0.10f;    // per-layer, elevation 1
inline constexpr float shadow_offset_y = 2.0f;  // downward shift, elevation 1
```

- [x] **Step 4: DESIGN.md 同步** — `theme_fade_ms` 行后加三行 motion token；表后新增：

```markdown
### Elevation (hand-drawn shadow falloff)

| Token | Value | Usage |
|---|---|---|
| `shadow_blur_sm` | `4` | Cards elevation 1 |
| `shadow_blur_md` | `8` | Popovers elevation 2 |
| `shadow_blur_lg` | `14` | Modals elevation 3 |
| `shadow_alpha` | `0.10` | Per-layer alpha at elevation 1 |
| `shadow_offset_y` | `2.0` | Downward offset at elevation 1 |
```

- [x] **Step 5: 构建双 preset + 测试通过**

Run: `cmake --build --preset x64-debug && cmake --build --preset x64-release && ctest --preset x64-debug && ctest --preset x64-release`
Expected: 零警告，全 PASS。

- [x] **Step 6: Commit**

```bash
git add include/flux/Theme.hpp DESIGN.md tests/smoke.cpp
git commit -m "Round 1: motion + shadow tokens (theme/DESIGN/smoke in sync)"
```

---

### Task 2: `Animation.hpp` — Curve + `Animated<T>`

**Files:**
- Create: `include/flux/Animation.hpp`
- Modify: `include/flux/flux.hpp`（umbrella include，追加 `#include <flux/Animation.hpp>`；实现时先读该文件确认排列）
- Test: `tests/smoke.cpp`（新 `test_animation()`，在 `main()` 挂钩）

**Interfaces:**
- Produces:
  - `enum class Curve { linear, ease_in_out, ease_out }`
  - `float apply_curve(float t, Curve) noexcept`（constexpr）
  - `float approach_blend(bool on_now, bool on_before, unsigned long long now_ms, unsigned long long changed_at_ms, unsigned duration_ms) noexcept`
  - `template <class T> class Animated<T>`：`set/set_at(T, unsigned ms, Curve, [now])`、`current()/current_at(now)`、`active_at(now)`、`remaining_at(now)`、`target()`；支持 `T ∈ {float,int,Color}`（`lerp_value` 重载）
  - `Animated<T>::now()` = `GetTickCount64()`

- [x] **Step 1: 写失败测试** — `tests/smoke.cpp` 新增（`test_theme` 之后）：

```cpp
void test_animation() {
    // Curves: endpoints exact, monotone, distinct.
    expect(near_eq(apply_curve(0.0f, Curve::linear), 0.0f) &&
           near_eq(apply_curve(1.0f, Curve::linear), 1.0f), "curve linear endpoints");
    expect(near_eq(apply_curve(0.0f, Curve::ease_in_out), 0.0f) &&
           near_eq(apply_curve(1.0f, Curve::ease_in_out), 1.0f), "curve ease_in_out endpoints");
    expect(near_eq(apply_curve(0.5f, Curve::ease_in_out), 0.5f), "ease_in_out symmetric midpoint");
    expect(apply_curve(0.25f, Curve::ease_out) > 0.25f, "ease_out front-loads");
    expect(apply_curve(2.0f, Curve::ease_out) == 1.0f, "curve clamps t > 1");

    // approach_blend: snap cases + ramp direction.
    expect(near_eq(approach_blend(true, true, 1000, 0, 100), 1.0f), "blend steady-on");
    expect(near_eq(approach_blend(false, false, 1000, 0, 100), 0.0f), "blend steady-off");
    expect(near_eq(approach_blend(true, false, 50, 0, 100), 0.5f), "blend ramping in");
    expect(near_eq(approach_blend(false, true, 50, 0, 100), 0.5f), "blend ramping out");
    expect(near_eq(approach_blend(true, false, 500, 0, 100), 1.0f), "blend clamped past end");

    // Animated<float> with injected clock.
    Animated<float> a{0.0f};
    a.set_at(1.0f, 100, Curve::linear, 0);
    expect(a.active_at(0) && a.active_at(99) && !a.active_at(100), "active window half-open");
    expect(near_eq(a.current_at(0), 0.0f), "tween starts at from");
    expect(near_eq(a.current_at(50), 0.5f), "linear midpoint");
    expect(near_eq(a.current_at(100), 1.0f) && near_eq(a.current_at(500), 1.0f), "tween settles at to");
    expect(a.remaining_at(25) == 75 && a.remaining_at(500) == 0, "remaining math");

    // Retarget mid-flight continues from the current visual value.
    a.set_at(0.5f, 100, Curve::linear, 50);   // was 0.5 at t=50
    expect(near_eq(a.current_at(50), 0.5f), "retarget starts from visual value");

    // duration 0 or same-value set snaps.
    Animated<float> b{2.0f};
    b.set_at(9.0f, 0, Curve::ease_in_out, 0);
    expect(!b.active_at(0) && near_eq(b.current_at(0), 9.0f), "zero-duration snaps");
    b.set_at(9.0f, 500, Curve::linear, 0);
    expect(!b.active_at(0), "same-value set is a no-op tween");

    // Animated<Color> interpolates per channel.
    Animated<Color> c{rgb(0, 0, 0)};
    c.set_at(rgb(200, 100, 50), 100, Curve::linear, 0);
    expect(c.current_at(50) == rgb(100, 50, 25), "Animated<Color> channel lerp");
}
```

并在 `main()` 里 `test_animation();`（跟随现有调用列表）。文件顶部 `#include <flux/Animation.hpp>`（经 flux.hpp 或直接）。

- [x] **Step 2: 确认失败**（编译错误 = 未定义即失败）

Run: `cmake --build --preset x64-debug --target WinFluxSmokeTest`
Expected: 编译失败，`Animated` / `apply_curve` 未定义。

- [x] **Step 3: 实现 `include/flux/Animation.hpp`**

```cpp
#pragma once

// WinFlux animation primitives. The scene tree is rebuilt from scratch
// every frame, so animation state must NOT live in Element/Props —
// Animated<T> is app-side state (next to State<T>) that builders read
// once per frame; the Host keeps a timer alive while any tween is
// unsettled (Host::animate_for) so the rebuilt frames keep arriving.

#include <windows.h>

#include <algorithm>
#include <cstdint>

#include "Theme.hpp"

namespace flux {

enum class Curve { linear, ease_in_out, ease_out };

// Normalized timing curve; t is clamped to [0, 1].
[[nodiscard]] constexpr float apply_curve(float t, Curve c) noexcept {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    switch (c) {
    case Curve::linear:      return t;
    case Curve::ease_out:    return 1.0f - (1.0f - t) * (1.0f - t);
    case Curve::ease_in_out: return t * t * (3.0f - 2.0f * t);
    }
    return t;
}

// Per-type lerp for the value types Animated supports.
[[nodiscard]] constexpr float lerp_value(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}
[[nodiscard]] constexpr int lerp_value(int a, int b, float t) noexcept {
    return static_cast<int>(static_cast<float>(a) + static_cast<float>(b - a) * t + 0.5f);
}
[[nodiscard]] inline Color lerp_value(Color a, Color b, float t) noexcept {
    return lerp(a, b, t);
}

// Blend factor (0..1) for a render-time interaction highlight (hover /
// press). `on_now`: element is in the current set; `on_before`: it was in
// the set when the set last changed. Ramp-in on enter, ramp-out on leave.
[[nodiscard]] inline float approach_blend(bool on_now, bool on_before,
                                          unsigned long long now_ms,
                                          unsigned long long changed_at_ms,
                                          unsigned duration_ms) noexcept {
    if (on_now == on_before) return on_now ? 1.0f : 0.0f;
    const float t = duration_ms == 0
        ? 1.0f
        : static_cast<float>(now_ms - changed_at_ms) / static_cast<float>(duration_ms);
    const float c = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return on_now ? c : 1.0f - c;
}

template <class T>
class Animated {
public:
    Animated() = default;
    explicit Animated(T init) noexcept : from_(init), to_(init) {}

    // Tween to `value` over `duration_ms` starting now (snaps when 0).
    void set(T value, unsigned duration_ms, Curve c = Curve::ease_in_out) noexcept {
        set_at(std::move(value), duration_ms, c, now());
    }
    // Clock-injected variant for tests and synchronized batches.
    void set_at(T value, unsigned duration_ms, Curve c,
                unsigned long long now_ms) noexcept {
        curve_ = c;
        if (duration_ms == 0 || value == to_) {
            from_ = to_ = std::move(value);
            duration_ms_ = 0;
            start_ms_ = now_ms;
            return;
        }
        from_ = current_at(now_ms);   // retarget from the current visual value
        to_ = std::move(value);
        start_ms_ = now_ms;
        duration_ms_ = duration_ms;
    }

    [[nodiscard]] const T& target() const noexcept { return to_; }
    [[nodiscard]] bool active_at(unsigned long long now_ms) const noexcept {
        return duration_ms_ != 0 && now_ms < start_ms_ + duration_ms_;
    }
    [[nodiscard]] unsigned remaining_at(unsigned long long now_ms) const noexcept {
        if (!active_at(now_ms)) return 0;
        return static_cast<unsigned>(start_ms_ + duration_ms_ - now_ms);
    }
    [[nodiscard]] T current() const noexcept { return current_at(now()); }
    [[nodiscard]] T current_at(unsigned long long now_ms) const noexcept {
        if (duration_ms_ == 0) return to_;
        const float t = apply_curve(
            static_cast<float>(now_ms - start_ms_) / static_cast<float>(duration_ms_),
            curve_);
        return lerp_value(from_, to_, t);
    }

    static unsigned long long now() noexcept { return GetTickCount64(); }

private:
    T from_{};
    T to_{};
    unsigned long long start_ms_ = 0;
    unsigned duration_ms_ = 0;
    Curve curve_ = Curve::ease_in_out;
};

} // namespace flux
```

`include/flux/flux.hpp` 追加 `#include <flux/Animation.hpp>`（与现有 include 并列）。

- [x] **Step 4: 测试通过**

Run: `cmake --build --preset x64-debug --target WinFluxSmokeTest && out/build/x64-debug/Debug/WinFluxSmokeTest.exe`
Expected: 全 PASS（含新 ~17 条）。

- [x] **Step 5: 双 preset 全量验证 + Commit**

```bash
cmake --build --preset x64-debug && cmake --build --preset x64-release
ctest --preset x64-debug && ctest --preset x64-release
git add include/flux/Animation.hpp include/flux/flux.hpp tests/smoke.cpp
git commit -m "Round 1: Animated<T> + curves + approach_blend (Animation.hpp)"
```

---

### Task 3: Host 动画时钟（`animate_for` / `animating()`）

**Files:**
- Modify: `include/flux/Host.hpp`
- Modify: `src/core/Host.cpp`

**Interfaces:**
- Consumes: Task 1 `tokens::motion_*`。
- Produces: `void Host::animate_for(unsigned ms) noexcept`（public）；`bool Host::animating() const noexcept`（private，derived）；`template <class T> T Host::animate(const Animated<T>&) noexcept`（public，builder 侧读取 tween 并续时钟）；`fade_timer_id_` → `anim_timer_id_`。

- [x] **Step 1: Host.hpp 声明** — public 区（`request_render` 附近）：

```cpp
    // Keep the animation clock ticking for `ms` more milliseconds. Safe to
    // call from builders (runs inside paint_frame): it only arms a timer;
    // the ticks call request_render, which re-enters cleanly.
    void animate_for(unsigned ms) noexcept;

    // Read an Animated<T> for this frame's build and extend the animation
    // clock until the tween settles. Builders call this instead of
    // current() so partial-repaint stays off while anything animates.
    template <class T>
    [[nodiscard]] T animate(const Animated<T>& a) noexcept {
        const unsigned long long now = Animated<T>::now();
        if (a.active_at(now)) animate_for(a.remaining_at(now) + 1);
        return a.current_at(now);
    }
```

`#include <flux/Animation.hpp>`（或相对 `"Animation.hpp"`，与该头现有风格一致）。private 区：

```cpp
    void update_cursor() noexcept;   // (Task 8 占位声明，本任务一并加入)
    [[nodiscard]] bool animating() const noexcept {
        return fading_ || GetTickCount64() < anim_deadline_ms_;
    }
```

成员与常量更名/新增：

```cpp
    unsigned long long anim_deadline_ms_ = 0;   // animate_for 的统一截止时刻
    static constexpr UINT_PTR anim_timer_id_ = 1;      // was fade_timer_id_
    static constexpr unsigned anim_tick_ms_ = 16;      // was fade_tick_ms_
```

- [x] **Step 2: Host.cpp 实现** — `request_render` 后新增：

```cpp
void Host::animate_for(unsigned ms) noexcept {
    if (ms == 0) return;
    const unsigned long long deadline = GetTickCount64() + ms + 1;
    if (deadline > anim_deadline_ms_) anim_deadline_ms_ = deadline;
    if (hwnd_ != nullptr) SetTimer(hwnd_, anim_timer_id_, anim_tick_ms_, nullptr);
}
```

`handle_timer` 重写（fade 逻辑并入统一时钟）：

```cpp
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
    const bool more = fading_ || now < anim_deadline_ms_;
    if (!more) {
        anim_deadline_ms_ = 0;
        if (hwnd_ != nullptr) KillTimer(hwnd_, anim_timer_id_);
    }
    request_render();   // settle frame or next tween step
}
```

`set_theme_mode` 的 `SetTimer(...)` 行替换为 `animate_for(static_cast<unsigned>(tokens::theme_fade_ms));`。wndproc `WM_TIMER` 分支 `Host::fade_timer_id_` → `Host::anim_timer_id_`。`paint_frame` 两处 `fading_` 换成 `animating()`（短路行与 force-full 行），并保持注释同步。

- [x] **Step 3: 构建 + 双 preset 测试**

Run: `cmake --build --preset x64-debug && cmake --build --preset x64-release && ctest --preset x64-debug && ctest --preset x64-release`
Expected: 零警告全绿（fade 行为由 host_lifecycle 覆盖，不变语义）。

- [x] **Step 4: Commit**

```bash
git add include/flux/Host.hpp src/core/Host.cpp
git commit -m "Round 1: unified animation clock (animate_for / animating)"
```

---

### Task 4: 渲染期 hover/press 插值

**Files:**
- Modify: `include/flux/Host.hpp`（成员）
- Modify: `src/core/Host.cpp`（mouse_move / lbutton_down / render_element）
- Test: `tests/smoke.cpp`（`approach_blend` 已在 Task 2 覆盖；本任务无新增纯函数）

**Interfaces:**
- Consumes: Task 2 `approach_blend`；Task 1 `motion_fast_ms`；Task 3 `animate_for`。
- Produces: `hover_from_path_` / `press_from_path_` / `hover_change_ms_` / `press_change_ms_`（Host private 成员）；render_element 内 `hover_t` / `press_t` 0..1 混合因子，背景色 `lerp(base, hover_bg, hover_t)` 叠 `lerp(—, pressed_bg, press_t)`。

- [x] **Step 1: Host.hpp 成员**（`prev_focus_path_` 之后）：

```cpp
    // Hover/press micro-transition state: the set as it was at the moment
    // it last changed plus the change timestamp. render_element blends the
    // highlight over motion_fast_ms; the animation clock keeps frames
    // coming while the blend is in flight (animating() => full dirty).
    Path hover_from_path_;
    Path press_from_path_;
    unsigned long long hover_change_ms_ = 0;
    unsigned long long press_change_ms_ = 0;
```

- [x] **Step 2: 记录变更** — `handle_mouse_move` 的 hover 变更分支改为：

```cpp
    if (path != hover_path_) {
        hover_from_path_ = std::move(hover_path_);
        hover_path_ = std::move(path);
        hover_change_ms_ = GetTickCount64();
        animate_for(static_cast<unsigned>(tokens::motion_fast_ms));
        // Hover highlight is applied at render time from hover_path_ and is
        // not encoded in the element tree; the damage diff is fed the OLD
        // and NEW paths in paint_frame so this becomes a partial repaint.
        request_render();
    }
```

`handle_lbutton_down` 末尾状态提交块改为：

```cpp
    if (new_focus != focus_path_ || new_pressed != pressed_path_) {
        press_from_path_ = std::move(pressed_path_);
        focus_path_ = std::move(new_focus);
        pressed_path_ = std::move(new_pressed);
        press_change_ms_ = GetTickCount64();
        animate_for(static_cast<unsigned>(tokens::motion_fast_ms));
        if (hwnd_ != nullptr && !pressed_path_.empty()) SetCapture(hwnd_);
        request_render();
    }
```

（`handle_lbutton_up` 的 `pressed_path_.clear()` 前补 `press_from_path_ = pressed_path_; press_change_ms_ = GetTickCount64(); animate_for(motion_fast);`——按下高亮同样淡出。）

- [x] **Step 3: render_element 混合** — 替换 hovered/pressed 计算与背景分支：

```cpp
    const unsigned long long now_ms = GetTickCount64();
    const bool hover_now  = is_prefix(path, hover_path_) && !path.empty();
    const bool hover_prev = is_prefix(path, hover_from_path_) && !path.empty();
    const float hover_t = p.enabled
        ? approach_blend(hover_now, hover_prev, now_ms, hover_change_ms_,
                         static_cast<unsigned>(tokens::motion_fast_ms))
        : 0.0f;
    const bool pressed_now  = path == pressed_path_;
    const bool pressed_prev = path == press_from_path_;
    const float press_t = p.enabled
        ? approach_blend(pressed_now, pressed_prev, now_ms, press_change_ms_,
                         static_cast<unsigned>(tokens::motion_fast_ms))
        : 0.0f;

    if (p.background.has_value()) {
        Color bg = *p.background;
        if (!p.enabled && p.disabled_background.has_value()) {
            bg = *p.disabled_background;
        } else {
            if (hover_t > 0.0f && p.hover_background.has_value())
                bg = lerp(bg, *p.hover_background, hover_t);
            if (press_t > 0.0f && p.pressed_background.has_value())
                bg = lerp(bg, *p.pressed_background, press_t);
        }
        renderer_.fill_round_rect(r, p.radius, bg);
    }
```

（旧 `hovered/pressed` 布尔其余使用处——checkbox 边框、slider——保留布尔 `hover_now/pressed_now` 即可，直接改名引用。）

- [x] **Step 4: 构建 + 测试 + 视觉验证**

Run: build 双 preset + ctest 双 preset → 启动 `out/build/x64-debug/Debug/WinFluxGallery.exe`，鼠标扫过按钮排，观察高亮 120ms 渐入渐出（截图对比前后帧可选）。

- [x] **Step 5: Commit**

```bash
git add include/flux/Host.hpp src/core/Host.cpp
git commit -m "Round 1: hover/press micro-transitions (render-time blend)"
```

---

### Task 5: Props 扩展（opacity / gradient / shadow / icon / cursor）+ paint_props_equal

**Files:**
- Modify: `include/flux/Element.hpp`
- Modify: `src/core/Host.cpp`（`icon()` builder 定义）
- Modify: `src/render/Damage.cpp`（`paint_props_equal`）
- Test: `tests/smoke.cpp`（`test_damage` 内追加）

**Interfaces:**
- Produces:
  - `enum class Cursor { arrow, hand, ibeam, size_ns, size_we, cross }`
  - `enum class IconKind { chevron_down, chevron_up, chevron_left, chevron_right, close, check, search, plus, minus, hamburger, user, trash, edit, arrow_right, calendar, inbox, play, pause, skip_prev, skip_next, info, success, warning, danger }`
  - `struct Gradient { Color from{}; Color to{}; constexpr operator== default; }`
  - `Props.opacity`（float, 1.0）、`Props.background_gradient`（optional<Gradient>）、`Props.shadow_elevation`（int, 0）、`Props.icon`（optional<IconKind>）、`Props.cursor`（Cursor, arrow）
  - `ElementPtr icon(IconKind, Props = {})`

- [x] **Step 1: 写失败测试** — smoke `test_damage`（实现时读该函数尾部）追加：

```cpp
    // New paint props (Round 1) must participate in the paint diff.
    {
        Element a = view(); a.x = 0; a.y = 0; a.w = 10; a.h = 10;
        Element b = a;
        b.props.opacity = 0.5f;
        expect(!DamageTracker::paint_props_equal(a.props, b.props), "opacity change is paint-dirty");
        b = a; b.props.shadow_elevation = 2;
        expect(!DamageTracker::paint_props_equal(a.props, b.props), "shadow change is paint-dirty");
        b = a; b.props.background_gradient = Gradient{rgb(0, 0, 0), rgb(255, 255, 255)};
        expect(!DamageTracker::paint_props_equal(a.props, b.props), "gradient change is paint-dirty");
        b = a; b.props.icon = IconKind::close;
        expect(!DamageTracker::paint_props_equal(a.props, b.props), "icon change is paint-dirty");
        b = a; b.props.cursor = Cursor::hand;
        expect(DamageTracker::paint_props_equal(a.props, b.props), "cursor is NOT paint-dirty");
    }
```

- [x] **Step 2: 确认失败**（编译错：`Gradient`/`IconKind`/`Cursor` 未定义）

- [x] **Step 3: Element.hpp** — `Direction/Align` 枚举后加：

```cpp
enum class Cursor { arrow, hand, ibeam, size_ns, size_we, cross };

// 24x24 stroke-grid icon ids. Drawn as D2D path geometry (no bitmap/WIC).
enum class IconKind {
    chevron_down, chevron_up, chevron_left, chevron_right,
    close, check, search, plus, minus, hamburger,
    user, trash, edit, arrow_right, calendar, inbox,
    play, pause, skip_prev, skip_next,
    info, success, warning, danger,
};

struct Gradient {
    Color from{};
    Color to{};
    [[nodiscard]] constexpr bool operator==(const Gradient&) const noexcept = default;
};
```

`Props` Paint 块（`radius` 后）：

```cpp
    float opacity = 1.0f;                          // self-draw alpha, 0..1
    std::optional<Gradient> background_gradient;   // wins over background
    int shadow_elevation = 0;                      // 0..3 hand-drawn falloff
    std::optional<IconKind> icon;                  // glyph: icon element payload or inline button/label adornment
```

Interaction 块（`enabled` 后）：`Cursor cursor = Cursor::arrow;`

builder 声明（`progress` 后）：`[[nodiscard]] ElementPtr icon(IconKind kind, Props p = {});`

- [x] **Step 4: Host.cpp builder 定义**（`progress` 后）：

```cpp
ElementPtr icon(IconKind kind, Props p) {
    p.icon = kind;
    if (!p.width.has_value()) p.width = 24.0f;
    if (!p.height.has_value()) p.height = 24.0f;
    return make(L"icon", std::move(p));
}
```

- [x] **Step 5: Damage.cpp** — `paint_props_equal` 字段列表加（实现时读现状，风格一致）：

```cpp
    a.opacity == b.opacity &&
    a.background_gradient == b.background_gradient &&
    a.shadow_elevation == b.shadow_elevation &&
    a.icon == b.icon &&
```

（`cursor` 不参与——交互期属性，不影响像素。）

- [x] **Step 6: 测试通过 + 双 preset + Commit**

```bash
cmake --build --preset x64-debug && cmake --build --preset x64-release
ctest --preset x64-debug && ctest --preset x64-release
git add include/flux/Element.hpp src/core/Host.cpp src/render/Damage.cpp tests/smoke.cpp
git commit -m "Round 1: opacity/gradient/shadow/icon/cursor props + diff coverage"
```

---

### Task 6: 渲染器 — alpha、渐变、阴影、图标几何

**Files:**
- Modify: `include/flux/D2DRenderer.hpp`
- Modify: `src/render/D2DRenderer.cpp`

**Interfaces:**
- Consumes: Task 5 `IconKind`/`Gradient`、Task 1 shadow tokens。
- Produces（全部 `noexcept`，逻辑单位）：
  - 既有 draw 调用加尾参 `float alpha = 1.0f`：`fill_round_rect` / `draw_round_rect` / `draw_line` / `draw_text`
  - `void fill_round_rect_gradient(Rect r, float radius, const Gradient& g, float alpha = 1.0f)`
  - `void draw_shadow(Rect r, float radius, int elevation)`
  - `void draw_icon(Rect box, IconKind kind, Color c, float stroke_w, float alpha = 1.0f)`
  - 设备重建时清空新缓存（`text_layouts_.clear()` 同位置）。

- [x] **Step 1: 头文件声明**（public draw 区）+ private 成员：

```cpp
    // Icon geometry + gradient brush + round-cap stroke style caches. All
    // keyed by value; cleared alongside brushes_ on device-lost.
    std::unordered_map<int, Microsoft::WRL::ComPtr<ID2D1PathGeometry>> icon_geos_;
    std::unordered_map<unsigned long long, Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>> gradients_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> icon_stroke_;
    [[nodiscard]] ID2D1PathGeometry* icon_geometry(IconKind kind) noexcept;
    [[nodiscard]] ID2D1LinearGradientBrush* gradient_brush(const Gradient& g) noexcept;
    [[nodiscard]] ID2D1StrokeStyle* icon_stroke() noexcept;
```

- [x] **Step 2: alpha 实现** — `brush(Color)` 不变；每个使用点在使用前 `b->SetOpacity(alpha)`（缓存画刷逐次设置，绝不依赖上次状态）。`draw_text` 的画刷同法。

- [x] **Step 3: 渐变** — `gradient_brush(g)`：key=`from.r<<16|from.g<<8|from.b` 与 to 组合的 64 位 key；未命中时 `frame_rt_->CreateGradientStopCollection`（2 stops，GAMMA_2_2 / EXTEND_CLAMP）+ `CreateLinearGradientBrush({0,0},{1,0})`；命中后每帧 `SetStartPoint({r.x, r.y+0.5f*r.h})`、`SetEndPoint({r.x+r.w, ...})` 再 `FillRoundedRectangle`。

- [x] **Step 4: 阴影** —

```cpp
void D2DRenderer::draw_shadow(Rect r, float radius, int elevation) noexcept {
    if (elevation <= 0 || frame_rt_ == nullptr) return;
    const int blurs[3] = {tokens::shadow_blur_sm, tokens::shadow_blur_md,
                          tokens::shadow_blur_lg};
    const float blur = static_cast<float>(blurs[std::clamp(elevation, 1, 3) - 1]);
    constexpr int kLayers = 3;
    const float dy = tokens::shadow_offset_y * static_cast<float>(elevation);
    for (int i = 1; i <= kLayers; ++i) {
        const float grow = blur * static_cast<float>(i) / kLayers;
        const float a = tokens::shadow_alpha * static_cast<float>(elevation) *
                        (1.0f - static_cast<float>(i - 1) / kLayers);
        fill_round_rect(Rect{r.x - grow, r.y - grow + dy, r.w + 2.0f * grow,
                             r.h + 2.0f * grow}, radius + grow, Color{0, 0, 0}, a);
    }
}
```

- [x] **Step 5: 图标几何** — 24×24 网格折线；`icon_geometry(kind)` 惰性建（`frame_rt_->GetFactory()`），`icon_stroke()` 圆头圆接 2px 基准。坐标表（M=move, L=line, 闭合=fill）：

```
chevron_down  M15 6 L9 12 L15 18        chevron_up   M9 18 L15 12 L9 6
chevron_left  M15 6 L9 12 L15 18→M14 5 L7 12 L14 19   chevron_right M10 5 L17 12 L10 19
close         M6 6 L18 18  M18 6 L6 18  check        M5 13 L10 18 L19 7
search        圆(11,11,r7) + L16.5 16.5 L21 21       plus   M12 5 V19  M5 12 H19
minus         M5 12 H19                       hamburger    M4 7 H20  M4 12 H20  M4 17 H20
user          圆(12,8,r4) + M5 20 C5 15 19 15 19 20
trash         M5 7 H19  M9 7 V5 H15 V7  M7 7 L8 20 H16 L17 7
edit          M4 20 L5 16 L16 5 L19 8 L8 19 Z(fill)
arrow_right   M4 12 H20  M13 5 L20 12 L13 19
calendar      R(4,6,16,14) + M4 10 H20 + M8 4 V8 M16 4 V8
inbox         M4 13 L8 6 H16 L20 13 V18 H4 Z + M4 13 H10 L12 16 L14 13 H20
play          三角 fill M8 5 L19 12 L8 19 Z    pause  R(7,5,3.5,14)fill R(13.5,5,3.5,14)fill
skip_prev     M6 5 V19 + 三角 fill M19 5 L9 12 L19 19 Z
skip_next     M18 5 V19 + 三角 fill M5 5 L15 12 L5 19 Z
info          圆(12,12,r9) + M12 11 V16 + 点(12,8,r1)fill
success       圆(12,12,r9) + M8 12.5 L11 15.5 L16.5 9.5
warning       M12 4 L21 19 H3 Z + M12 10 V14 + 点(12,17,r0.8)fill
danger        圆(12,12,r9) + M9 9 L15 15  M15 9 L9 15
```

sink 写法：折线 `BeginFigure(p0)` + `AddLines(pts)`；圆/点 `AddArc`/小矩形 fill；`draw_icon` 里按 `box` 缩放（保存 `begin()` 时的 world 矩阵到成员 `world_`，`SetTransform(world_ * Scale(s) * Translation(x,y))` 绘制后还原）。

- [x] **Step 6: 设备丢失清理** — `text_layouts_.clear()` 处同步 `icon_geos_.clear(); gradients_.clear(); icon_stroke_.Reset();`

- [x] **Step 7: 构建（零警告）+ ctest + Commit**（渲染路径 Task 7 才接线，本任务编译验证为主）

```bash
cmake --build --preset x64-debug && cmake --build --preset x64-release && ctest --preset x64-debug && ctest --preset x64-release
git add include/flux/D2DRenderer.hpp src/render/D2DRenderer.cpp
git commit -m "Round 1: renderer alpha/gradient/shadow/icon-geometry"
```

---

### Task 7: render_element 接线（shadow → gradient → bg → icon → opacity）

**Files:**
- Modify: `src/core/Host.cpp`（`render_element`、`measure_leaf`）

**Interfaces:**
- Consumes: Task 5 props、Task 6 renderer API。

- [x] **Step 1: 绘制序** — render_element 背景/边框段改为（裁剪检查后）：

```cpp
    const float op = std::clamp(p.opacity, 0.0f, 1.0f);
    if (op > 0.001f) {
        if (p.shadow_elevation > 0)
            renderer_.draw_shadow(r, p.radius, p.shadow_elevation);
        if (p.background_gradient.has_value())
            renderer_.fill_round_rect_gradient(r, p.radius, *p.background_gradient, op);
        else if (p.background.has_value())
            renderer_.fill_round_rect(r, p.radius, bg /* Task 4 混合色 */, op);
        if (p.border_color.has_value() && p.border_width > 0.0f) {
            const float half = p.border_width * 0.5f;
            renderer_.draw_round_rect(Rect{r.x + half, r.y + half,
                                           r.w - p.border_width, r.h - p.border_width},
                                      p.radius, *p.border_color, p.border_width, op);
        }
    }
```

其余自绘（glyph 文字、focus ring）全传 `op`；`op <= 0.001f` 时跳过自绘但**仍递归子节点**（子有自己的 opacity）。

- [x] **Step 2: icon 元素分支 + 行内图标** — glyph 区加：

```cpp
    } else if (e.type == L"icon") {
        const float side = std::min(r.w, r.h);
        const float ix = r.x + (r.w - side) * 0.5f, iy = r.y + (r.h - side) * 0.5f;
        renderer_.draw_icon(Rect{ix, iy, side, side}, p.icon.value_or(IconKind::close),
                            p.text_color.value_or(pal.text), side / 12.0f, op);
    }
```

`button`/`label` 分支：`p.icon` 存在时先画 20px 盒内图标，文字矩形左移 `20 + spacing_sm`：

```cpp
        Rect text_r = r;
        if (p.icon.has_value()) {
            constexpr float kInline = 20.0f;
            const float iy = r.y + (r.h - kInline) * 0.5f;
            renderer_.draw_icon(Rect{r.x + p.padding.left, iy, kInline, kInline},
                                *p.icon, tc, 1.8f, op);
            text_r = Rect{r.x + p.padding.left + kInline + tokens::spacing_sm,
                          r.y, r.w - p.padding.left - kInline - tokens::spacing_sm - p.padding.right, r.h};
        }
        renderer_.draw_text(text_r, p.text, p.font_size_pt, tc, p.bold, p.text_align, op);
```

（`text_align` 居中的按钮视觉不变；start 对齐时图标占位才影响。）

- [x] **Step 3: measure_leaf** — 文本叶子的返回值扩为：

```cpp
            float extra = 0.0f;
            if (e.props.icon.has_value()) extra = 20.0f + static_cast<float>(tokens::spacing_sm);
            const float w = e.props.width.value_or(tw) + extra;
            ... return {w, h};
```

- [x] **Step 4: 构建 + 测试 + 视觉验证**（Gallery 现有页不回归——无新 props 消费者，默认值零变化）

- [x] **Step 5: Commit**

```bash
git add src/core/Host.cpp
git commit -m "Round 1: render_element paints shadow/gradient/icon/opacity"
```

---

### Task 8: per-element 光标（`WM_SETCURSOR`）

**Files:**
- Modify: `include/flux/Host.hpp`（声明已在 Task 3 加入；补成员 `last_mouse_lx_/ly_`）
- Modify: `src/core/Host.cpp`（wndproc、`update_cursor`、`handle_mouse_move` 记录、builder 默认值）
- Modify: `include/flux/Element.hpp`（无——Cursor 已在 Task 5）

**Interfaces:**
- Consumes: Task 5 `Props.cursor`。
- Produces: builder 默认 `button→hand`、`text_input→ibeam`、`slider→size_we`；拖动 slider 期间覆盖为 `size_we`。

- [x] **Step 1: wndproc 分支**（`WM_TIMER` 后）：

```cpp
            case WM_SETCURSOR:
                if (LOWORD(lparam) == HTCLIENT) {
                    host->update_cursor();
                    return TRUE;
                }
                break;
```

- [x] **Step 2: update_cursor 实现**（Host.cpp，hit_test 区）：

```cpp
void Host::update_cursor() noexcept {
    if (tree_ == nullptr) return;
    Path path;
    (void)hit_test(last_mouse_lx_, last_mouse_ly_, path);
    Cursor want = Cursor::arrow;
    for (Path walk = path; !walk.empty(); walk.pop_back()) {
        if (const Element* e = resolve(walk);
            e != nullptr && e->props.cursor != Cursor::arrow) {
            want = e->props.cursor;
            break;
        }
    }
    if (!pressed_path_.empty()) {
        if (const Element* e = resolve(pressed_path_);
            e != nullptr && e->type == L"slider")
            want = Cursor::size_we;
    }
    static constexpr struct { Cursor c; PCWSTR id; } kMap[] = {
        {Cursor::arrow, IDC_ARROW},   {Cursor::hand, IDC_HAND},
        {Cursor::ibeam, IDC_IBEAM},   {Cursor::size_ns, IDC_SIZENS},
        {Cursor::size_we, IDC_SIZEWE},{Cursor::cross, IDC_CROSS},
    };
    for (const auto& m : kMap)
        if (m.c == want) { SetCursor(LoadCursorW(nullptr, m.id)); return; }
}
```

`handle_mouse_move` 开头记录 `last_mouse_lx_ = lx; last_mouse_ly_ = ly;`。

- [x] **Step 3: builder 默认**（Host.cpp，遵循现有「默认即覆写」惯例）：
  - `button`：`p.cursor = Cursor::hand;`（padding 默认块附近）
  - `text_input`：`p.cursor = Cursor::ibeam;`
  - `slider`：`p.cursor = Cursor::size_we;`

- [x] **Step 4: 构建 + ctest + 视觉验证**（Gallery：按钮手型、输入框 I-beam、slider 双向箭头）

- [x] **Step 5: Commit**

```bash
git add include/flux/Host.hpp src/core/Host.cpp
git commit -m "Round 1: per-element cursors via WM_SETCURSOR"
```

---

### Task 9: Gallery「Motion & Effects」演示页

**Files:**
- Modify: `samples/Gallery/Gallery.cpp`（App 成员、`motion_section`、tab_bar、build_body——实现时先读 `tab_bar`/`build_body`/App struct 现状再挂钩）

**Interfaces:**
- Consumes: 全部 Round 1 API。
- Produces: 新 tab「Motion」：①Animated 进度条+计数（点击触发 tween）②渐变卡片 ③elevation 1/2/4 阴影卡 ④图标栅格（全部 24 枚 IconKind）⑤光标体验行。

- [x] **Step 1: App 成员**（App struct 内，实现时对齐现有命名）：

```cpp
    flux::Animated<float> anim_progress{0.0f};
    flux::Animated<Color> anim_color{flux::rgb(59, 130, 246)};
```

- [x] **Step 2: motion_section**（新函数，返回 section_card 包裹的 column）：

```cpp
ElementPtr motion_section(App& app) {
    const Palette& pal = app.host.palette();
    auto col = col_view({
        // 1) tween demo: 按钮 -> 0/1 目标，进度条 + 读数跟随
        row_view({
            button(L"Fill", [&app] { app.anim_progress.set(1.0f,
                static_cast<unsigned>(tokens::motion_slow_ms) * 3, flux::Curve::ease_in_out); },
                button_primary(pal)),
            button(L"Drain", [&app] { app.anim_progress.set(0.0f,
                static_cast<unsigned>(tokens::motion_slow_ms) * 3, flux::Curve::ease_in_out); },
                button_secondary(pal)),
        }, static_cast<float>(tokens::spacing_sm), Align::start),
        [&, v = app.host.animate(app.anim_progress)] {
            Props p; p.height = static_cast<float>(tokens::progress_height);
            return flux::progress(v, p);
        }(),
        [&, v = app.host.animate(app.anim_progress)] {
            Props p; p.text = std::to_wstring(static_cast<int>(v * 100)) + L"%";
            p.text_color = pal.text_secondary; p.text_align = Align::start;
            p.font_size_pt = tokens::font_caption;
            return flux::label(p.text, p);
        }(),
        // 2) gradient card
        [&] {
            Props p; p.background_gradient = Gradient{pal.accent, pal.info};
            p.radius = static_cast<float>(tokens::radius_card);
            p.height = 64.0f; p.shadow_elevation = 2;
            p.text = L"Gradient + elevation 2"; p.text_color = pal.on_accent;
            return flux::label(p.text, p);
        }(),
        // 3) elevation row
        row_view({
            [&] { Props p; p.background = pal.surface; p.radius = static_cast<float>(tokens::radius_card);
                  p.shadow_elevation = 1; p.width = 120; p.height = 56;
                  p.text = L"elev 1"; p.text_color = pal.text; return flux::label(p.text, p); }(),
            [&] { Props p; p.background = pal.surface; p.radius = static_cast<float>(tokens::radius_card);
                  p.shadow_elevation = 2; p.width = 120; p.height = 56;
                  p.text = L"elev 2"; p.text_color = pal.text; return flux::label(p.text, p); }(),
            [&] { Props p; p.background = pal.surface; p.radius = static_cast<float>(tokens::radius_card);
                  p.shadow_elevation = 3; p.width = 120; p.height = 56;
                  p.text = L"elev 3"; p.text_color = pal.text; return flux::label(p.text, p); }(),
        }, static_cast<float>(tokens::spacing_md), Align::start),
        // 4) icon grid (全部 IconKind)
        [&] {
            constexpr IconKind kAll[] = { IconKind::chevron_down, /* …实现时列全 24 项… */ };
            auto grid = view();
            for (IconKind k : kAll)
                grid->children.push_back(flux::icon(k, [&]{ Props p;
                    p.text_color = pal.text_secondary; return p; }()));
            return grid;
        }(),
        // 5) cursor row
        row_view({
            [&] { Props p = button_secondary(pal); p.cursor = Cursor::cross; p.text = L"cross";
                  return flux::button(L"cross", [] {}, p); }(),
            [&] { Props p = button_secondary(pal); p.cursor = Cursor::size_ns; p.text = L"resize";
                  return flux::button(L"resize", [] {}, p); }(),
        }, static_cast<float>(tokens::spacing_sm), Align::start),
    }, static_cast<float>(tokens::spacing_md));
    return section_card(L"Motion & Effects", std::move(col), pal);
}
```

（lambda-in-initializer 处的写法按 Gallery 现有风格展开为具名局部变量；`col_view/row_view/section_card/button_primary` 为 Gallery 既有助手。图标栅格 `kAll` 必须列全 24 个枚举项。）

- [x] **Step 3: 挂钩** — `tab_bar` 的 tab 列表与 `build_body` 的 switch 各加一档「Motion」（实现时对齐现有 TabSpec/active 常量）。

- [x] **Step 4: 构建 + 启动验证**：`WinFluxGallery.exe` → Motion 页：点 Fill/Drain 观察 840ms 补间、渐变卡、三级阴影、24 图标齐全、光标切换。截图留档（可选）。

- [x] **Step 5: 双 preset + Commit（Round 1 收口提交）**

```bash
cmake --build --preset x64-debug && cmake --build --preset x64-release
ctest --preset x64-debug && ctest --preset x64-release
git add samples/Gallery/Gallery.cpp
git commit -m "Round 1: Gallery Motion & Effects demo (tween/gradient/shadow/icons/cursors)"
```

---

## 验收清单（Round 1 完成定义）

- [x] 双 preset 零警告构建、ctest 全绿
- [x] hover/press 120ms 渐入渐出（Gallery 按钮排肉眼验证）
- [x] Animated tween 在 Gallery Motion 页可视
- [x] 渐变卡 / elevation 1-3 阴影 / 24 图标 / 光标全部呈现
- [x] 现有 Gallery 页面无视觉回归（逐 tab 过一遍）
- [x] 9 个提交，全部独立可构建

## 后续轮次

Round 2（scroll_view + overlay）计划在本轮验收后编写——其任务细节依赖 Round 1 落地后的真实 API。
