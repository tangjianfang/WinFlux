#include <flux/Theme.hpp>

namespace flux {

namespace {
constexpr unsigned char uc(int v) noexcept { return static_cast<unsigned char>(v); }

int clamp_byte(int v) noexcept { return v < 0 ? 0 : (v > 255 ? 255 : v); }
} // namespace

Palette palette(ThemeMode mode) noexcept {
    switch (mode) {
    case ThemeMode::dark:
        // Same values as nfui theme_palette(dark) — one visual language.
        return Palette{
            rgb(36, 36, 38), rgb(46, 46, 48), rgb(56, 56, 60), rgb(40, 40, 42),
            rgb(64, 64, 68), rgb(58, 58, 62), rgb(238, 238, 240), rgb(170, 170, 176),
            rgb(232, 142, 110), rgb(244, 165, 130), rgb(255, 255, 255), rgb(58, 46, 38),
            rgb(232, 122, 108), rgb(120, 184, 140), rgb(232, 176, 70), rgb(96, 168, 216),
        };
    case ThemeMode::high_contrast:
        // WCAG AAA profile; text pairs clear 7:1, UI components 3:1.
        return Palette{
            rgb(0, 0, 0), rgb(31, 31, 31), rgb(96, 96, 96), rgb(20, 20, 20),
            rgb(255, 255, 255), rgb(96, 96, 96), rgb(255, 255, 255), rgb(220, 220, 220),
            rgb(255, 235, 59), rgb(0, 255, 255), rgb(0, 0, 0), rgb(255, 255, 255),
            rgb(255, 132, 132), rgb(80, 255, 132), rgb(255, 176, 0), rgb(0, 160, 255),
        };
    case ThemeMode::light:
    default:
        return Palette{
            rgb(250, 249, 245), rgb(240, 238, 230), rgb(232, 229, 219), rgb(228, 225, 215),
            rgb(219, 215, 204), rgb(207, 203, 191), rgb(31, 30, 29), rgb(107, 104, 98),
            rgb(217, 119, 87), rgb(193, 95, 63), rgb(255, 255, 255), rgb(242, 214, 200),
            rgb(199, 84, 80), rgb(77, 124, 95), rgb(184, 130, 31), rgb(76, 137, 194),
        };
    }
}

Color lerp(Color a, Color b, float t) noexcept {
    if (!(t > 0.0f)) return a;
    if (t >= 1.0f) return b;
    auto mix = [t](unsigned char va, unsigned char vb) noexcept {
        return uc(clamp_byte(static_cast<int>(
            static_cast<float>(va) + (static_cast<float>(vb) - static_cast<float>(va)) * t + 0.5f)));
    };
    return Color{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}

Palette lerp_palette(const Palette& a, const Palette& b, float t) noexcept {
    Palette out;
    out.background     = lerp(a.background, b.background, t);
    out.surface        = lerp(a.surface, b.surface, t);
    out.surface_hover  = lerp(a.surface_hover, b.surface_hover, t);
    out.surface_variant = lerp(a.surface_variant, b.surface_variant, t);
    out.border         = lerp(a.border, b.border, t);
    out.divider        = lerp(a.divider, b.divider, t);
    out.text           = lerp(a.text, b.text, t);
    out.text_secondary = lerp(a.text_secondary, b.text_secondary, t);
    out.accent         = lerp(a.accent, b.accent, t);
    out.accent_hover   = lerp(a.accent_hover, b.accent_hover, t);
    out.on_accent      = lerp(a.on_accent, b.on_accent, t);
    out.selection      = lerp(a.selection, b.selection, t);
    out.danger         = lerp(a.danger, b.danger, t);
    out.success        = lerp(a.success, b.success, t);
    out.warning        = lerp(a.warning, b.warning, t);
    out.info           = lerp(a.info, b.info, t);
    return out;
}

} // namespace flux
