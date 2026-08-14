#pragma once

// WinFlux animation primitives. The scene tree is rebuilt from scratch
// every frame, so animation state must NOT live in Element/Props —
// Animated<T> is app-side state (kept next to State<T>) that builders read
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
// press). `on_now`: the element is in the current set; `on_before`: it was
// in the set when the set last changed. Ramps in on enter, out on leave,
// snaps when the element's membership is unchanged.
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
