// WinFlux Gallery — the M0 demo surface. Everything below is built
// declaratively: the root builder re-runs every frame, state lives in
// flux::State<T> handles, and one HWND renders the whole scene via D2D.

#include <windows.h>

#include <string>

#include <flux/flux.hpp>

using namespace flux;

namespace {

struct App {
    Host host;
    State<int> count;
    State<bool> animations;
    State<bool> bold_text;
    State<float> progress;
};

EdgeInsets pad_all(float v) noexcept { return EdgeInsets{v, v, v, v}; }

// Applies the standard button chrome from the current palette (the React
// analogue of a shared component style).
Props button_chrome(const Palette& pal) {
    Props p;
    p.background = pal.accent;
    p.hover_background = pal.accent_hover;
    p.pressed_background = lerp(pal.accent, rgb(0, 0, 0), 0.15f);
    p.disabled_background = pal.surface_variant;
    p.disabled_text_color = pal.text_secondary;
    p.text_color = pal.on_accent;
    return p;
}

ElementPtr header(App& app) {
    const Palette& pal = app.host.palette();
    const ThemeMode mode = app.host.theme_mode();
    const wchar_t* next = mode == ThemeMode::light   ? L"Dark mode"
                          : mode == ThemeMode::dark  ? L"High contrast"
                                                     : L"Light mode";

    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    row_p.gap = static_cast<float>(tokens::spacing_sm);
    return with_children(view(std::move(row_p)), {
        [&] {
            Props p;
            p.font_size_pt = tokens::font_title;
            p.bold = true;
            p.text_color = pal.text;
            p.flex_grow = 1.0f;
            p.text_align = Align::start;
            return label(L"WinFlux Gallery", std::move(p));
        }(),
        button(next,
               [&app, mode] {
                   app.host.set_theme_mode(mode == ThemeMode::light   ? ThemeMode::dark
                                          : mode == ThemeMode::dark  ? ThemeMode::high_contrast
                                                                     : ThemeMode::light);
               },
               button_chrome(pal)),
    });
}

ElementPtr counter_row(App& app) {
    const Palette& pal = app.host.palette();
    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    row_p.gap = static_cast<float>(tokens::spacing_sm);
    return with_children(view(std::move(row_p)), {
        button(L"Increment",
               [&app] { app.count.set(app.count.get() + 1); },
               button_chrome(pal)),
        [&] {
            Props p = button_chrome(pal);
            p.enabled = false;
            return button(L"Disabled", nullptr, std::move(p));
        }(),
        [&] {
            Props p;
            p.text_color = pal.text;
            p.text_align = Align::start;
            return label(L"Count: " + std::to_wstring(app.count.get()), std::move(p));
        }(),
    });
}

ElementPtr options_card(App& app) {
    const Palette& pal = app.host.palette();

    Props card_p;
    card_p.background = pal.surface;
    card_p.radius = static_cast<float>(tokens::radius_card);
    card_p.padding = pad_all(static_cast<float>(tokens::spacing_md));
    card_p.gap = static_cast<float>(tokens::spacing_sm + tokens::spacing_xs);

    Props progress_row;
    progress_row.direction = Direction::row;
    progress_row.align = Align::center;
    progress_row.gap = static_cast<float>(tokens::spacing_sm);

    return with_children(view(std::move(card_p)), {
        [&] {
            Props p;
            p.font_size_pt = tokens::font_subtitle;
            p.bold = true;
            p.text_color = pal.text;
            p.text_align = Align::start;
            return label(L"Options", std::move(p));
        }(),
        checkbox(L"Enable animations", app.animations.get(),
                 [&app](bool v) { app.animations.set(v); }),
        checkbox(L"Bold labels", app.bold_text.get(),
                 [&app](bool v) { app.bold_text.set(v); }),
        with_children(view(std::move(progress_row)), {
            [&] {
                Props p;
                p.text_color = pal.text_secondary;
                p.text_align = Align::start;
                return label(L"Progress", std::move(p));
            }(),
            [&] {
                Props p;
                p.flex_grow = 1.0f;
                return progress(app.progress.get(), std::move(p));
            }(),
        }),
        button(L"Advance progress",
               [&app] {
                   float v = app.progress.get() + 0.1f;
                   if (v > 1.0f) v = 0.0f;
                   app.progress.set(v);
               },
               button_chrome(pal)),
    });
}

ElementPtr footer(App& app) {
    const Palette& pal = app.host.palette();
    Props p;
    p.font_size_pt = tokens::font_caption;
    p.text_color = pal.text_secondary;
    p.text_align = Align::start;
    return label(L"Direct2D scene tree \u00B7 one HWND \u00B7 flex layout \u00B7 Tab cycles focus",
                 std::move(p));
}

ElementPtr build(App& app) {
    const Palette& pal = app.host.palette();

    Props root_p;
    root_p.direction = Direction::column;
    root_p.padding = pad_all(static_cast<float>(tokens::spacing_lg));
    root_p.gap = static_cast<float>(tokens::spacing_md);
    root_p.background = pal.background;
    return with_children(view(std::move(root_p)), {
        header(app),
        counter_row(app),
        options_card(app),
        footer(app),
    });
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enable_per_monitor_dpi_v2();

    App app;
    app.count = app.host.make_state<int>(0);
    app.animations = app.host.make_state<bool>(true);
    app.bold_text = app.host.make_state<bool>(false);
    app.progress = app.host.make_state<float>(0.35f);
    app.host.set_root_builder([&app] { return build(app); });

    Host::Config cfg;
    cfg.title = L"WinFlux Gallery";
    cfg.width = 980;
    cfg.height = 640;
    if (!app.host.create(cfg, instance)) {
        MessageBoxW(nullptr, L"WinFlux host creation failed (Direct2D unavailable?).",
                    L"WinFlux", MB_ICONERROR);
        return 1;
    }
    return app.host.run();
}
