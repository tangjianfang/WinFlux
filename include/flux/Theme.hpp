#pragma once

// WinFlux theme layer. Token values are identical to the nfui DESIGN.md
// contract (light/dark/HC palettes, 8px spacing, control/card radii) so both
// libraries share one visual language. All sizes are logical units at a 96 DPI
// baseline; the render layer scales them per monitor DPI.

namespace flux {

enum class ThemeMode { light, dark, high_contrast };

struct Color {
    unsigned char r{0};
    unsigned char g{0};
    unsigned char b{0};

    [[nodiscard]] constexpr bool operator==(const Color&) const noexcept = default;
};

[[nodiscard]] constexpr Color rgb(unsigned char r, unsigned char g, unsigned char b) noexcept {
    return Color{r, g, b};
}

struct Palette {
    Color background;       // window chrome
    Color surface;          // cards / panels
    Color surface_hover;
    Color surface_variant;  // recessed nested surface (alt rows, tracks)
    Color border;
    Color divider;
    Color text;
    Color text_secondary;
    Color accent;           // brand action colour
    Color accent_hover;
    Color on_accent;        // text drawn on accent
    Color selection;
    Color danger;
    Color success;
    Color warning;
    Color info;
};

[[nodiscard]] Palette palette(ThemeMode mode) noexcept;

// Per-channel linear interpolation (a at t=0, b at t=1; t clamped).
[[nodiscard]] Color lerp(Color a, Color b, float t) noexcept;
[[nodiscard]] Palette lerp_palette(const Palette& a, const Palette& b, float t) noexcept;

// Design tokens — single source of truth for spacing / sizing / radii / type.
namespace tokens {
inline constexpr int spacing_xs = 4;
inline constexpr int spacing_sm = 8;
inline constexpr int spacing_md = 16;
inline constexpr int spacing_lg = 24;
inline constexpr int spacing_xl = 32;

inline constexpr int control_height_sm = 24;
inline constexpr int control_height_md = 32;
inline constexpr int control_height_lg = 40;

inline constexpr int radius_sm = 4;
inline constexpr int radius_control = 6;
inline constexpr int radius_md = 8;
inline constexpr int radius_card = 10;
inline constexpr int radius_lg = 12;

inline constexpr float font_caption = 12.0f;
inline constexpr float font_body = 14.0f;
inline constexpr float font_subtitle = 16.0f;
inline constexpr float font_title = 20.0f;
inline constexpr float font_hero = 28.0f;

inline constexpr int theme_fade_ms = 200;
inline constexpr int checkbox_box = 18;
inline constexpr int progress_height = 10;
} // namespace tokens

} // namespace flux
