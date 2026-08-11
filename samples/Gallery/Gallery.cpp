// WinFlux Gallery — comprehensive demo surface for every component and
// styling option the framework exposes. The root builder re-runs every
// frame, state lives in flux::State<T> handles, and one HWND renders the
// whole scene via D2D + DirectComposition.

#include <windows.h>

#include <string>
#include <vector>

#include <flux/flux.hpp>

using namespace flux;

namespace {

struct App {
    Host host;
    State<int> count;
    State<bool> enable_animations;
    State<bool> enable_sound;
    State<float> progress;
};

EdgeInsets pad_all(float v) noexcept { return EdgeInsets{v, v, v, v}; }

// ---- Shared chrome helpers --------------------------------------------------
//
// Four button variants (primary / secondary / danger / ghost) plus the shared
// "section card" wrapper used to group each demo. Mirrors what an app would
// hoist into its own design-system module.

Props button_primary(const Palette& pal) {
    Props p;
    p.background = pal.accent;
    p.hover_background = pal.accent_hover;
    p.pressed_background = lerp(pal.accent, rgb(0, 0, 0), 0.15f);
    p.disabled_background = pal.surface_variant;
    p.disabled_text_color = pal.text_secondary;
    p.text_color = pal.on_accent;
    return p;
}

Props button_secondary(const Palette& pal) {
    Props p;
    p.background = pal.surface;
    p.hover_background = pal.surface_hover;
    p.pressed_background = pal.surface_variant;
    p.border_color = pal.border;
    p.border_width = 1.0f;
    p.radius = static_cast<float>(tokens::radius_control);
    p.disabled_background = pal.surface_variant;
    p.disabled_text_color = pal.text_secondary;
    p.text_color = pal.text;
    return p;
}

Props button_danger(const Palette& pal) {
    Props p;
    p.background = pal.danger;
    p.hover_background = lerp(pal.danger, rgb(255, 255, 255), 0.10f);
    p.pressed_background = lerp(pal.danger, rgb(0, 0, 0), 0.20f);
    p.disabled_background = pal.surface_variant;
    p.disabled_text_color = pal.text_secondary;
    p.text_color = rgb(255, 255, 255);
    return p;
}

Props button_ghost(const Palette& pal) {
    Props p;
    p.hover_background = pal.surface_hover;
    p.pressed_background = pal.surface_variant;
    p.disabled_text_color = pal.text_secondary;
    p.text_color = pal.accent;
    return p;
}

ElementPtr section_card(const wchar_t* title, ElementPtr body, const Palette& pal) {
    Props card_p;
    card_p.background = pal.surface;
    card_p.radius = static_cast<float>(tokens::radius_card);
    card_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg)};
    card_p.gap = static_cast<float>(tokens::spacing_md);

    Props title_p;
    title_p.font_size_pt = tokens::font_subtitle;
    title_p.bold = true;
    title_p.text_color = pal.text;
    title_p.text_align = Align::start;

    auto parent = view(std::move(card_p));
    parent->children.push_back(label(title, std::move(title_p)));
    parent->children.push_back(std::move(body));
    return parent;
}

ElementPtr row_view(std::vector<ElementPtr> children,
                    float gap = static_cast<float>(tokens::spacing_sm),
                    Align align = Align::center) {
    Props p;
    p.direction = Direction::row;
    p.gap = gap;
    p.align = align;
    auto parent = view(std::move(p));
    parent->children.reserve(children.size());
    for (auto& c : children) parent->children.push_back(std::move(c));
    return parent;
}

ElementPtr col_view(std::vector<ElementPtr> children,
                    float gap = static_cast<float>(tokens::spacing_sm),
                    Align align = Align::start) {
    Props p;
    p.direction = Direction::column;
    p.gap = gap;
    p.align = align;
    auto parent = view(std::move(p));
    parent->children.reserve(children.size());
    for (auto& c : children) parent->children.push_back(std::move(c));
    return parent;
}

// ---- Header ----------------------------------------------------------------

ElementPtr header(App& app) {
    const Palette& pal = app.host.palette();
    const ThemeMode mode = app.host.theme_mode();
    const wchar_t* next = mode == ThemeMode::light   ? L"Dark mode"
                          : mode == ThemeMode::dark  ? L"High contrast"
                                                     : L"Light mode";

    Props title_p;
    title_p.font_size_pt = tokens::font_title;
    title_p.bold = true;
    title_p.text_color = pal.text;
    title_p.flex_grow = 1.0f;
    title_p.text_align = Align::start;

    Props subtitle_p;
    subtitle_p.font_size_pt = tokens::font_body;
    subtitle_p.text_color = pal.text_secondary;
    subtitle_p.flex_grow = 1.0f;
    subtitle_p.text_align = Align::start;

    Props header_p;
    header_p.direction = Direction::row;
    header_p.align = Align::center;
    header_p.gap = static_cast<float>(tokens::spacing_md);

    auto title_col = col_view({
        label(L"WinFlux Gallery", std::move(title_p)),
        label(L"Component showcase", std::move(subtitle_p)),
    }, 0.0f, Align::start);

    auto parent = view(std::move(header_p));
    parent->children.push_back(std::move(title_col));
    parent->children.push_back(button(next,
        [&app, mode] {
            app.host.set_theme_mode(mode == ThemeMode::light   ? ThemeMode::dark
                                   : mode == ThemeMode::dark  ? ThemeMode::high_contrast
                                                              : ThemeMode::light);
        },
        button_secondary(pal)));
    return parent;
}

// ---- Buttons ---------------------------------------------------------------

ElementPtr buttons_section(App& app) {
    const Palette& pal = app.host.palette();

    Props count_chip_p;
    count_chip_p.background = pal.surface_variant;
    count_chip_p.radius = static_cast<float>(tokens::radius_sm);
    count_chip_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                      static_cast<float>(tokens::spacing_xs),
                                      static_cast<float>(tokens::spacing_sm),
                                      static_cast<float>(tokens::spacing_xs)};
    count_chip_p.text_color = pal.text;
    count_chip_p.font_size_pt = tokens::font_body;
    count_chip_p.bold = true;

    Props primary_lg = button_primary(pal);
    primary_lg.height = static_cast<float>(tokens::control_height_lg);
    primary_lg.font_size_pt = tokens::font_subtitle;

    Props secondary_sm = button_secondary(pal);
    secondary_sm.height = static_cast<float>(tokens::control_height_sm);
    secondary_sm.font_size_pt = tokens::font_caption;

    auto count_text = L"Clicked: " + std::to_wstring(app.count.get());
    auto inc = [&app] { app.count.set(app.count.get() + 1); };

    auto variants = row_view({
        button(L"Primary",   inc, button_primary(pal)),
        button(L"Secondary", inc, button_secondary(pal)),
        button(L"Danger",    inc, button_danger(pal)),
        button(L"Ghost",     inc, button_ghost(pal)),
    });

    auto sizes = row_view({
        [&] { Props p = secondary_sm; return button(L"Small",  inc, std::move(p)); }(),
        [&] { Props p = button_primary(pal); return button(L"Medium", inc, std::move(p)); }(),
        [&] { Props p = primary_lg; return button(L"Large",  inc, std::move(p)); }(),
    });

    auto disabled = row_view({
        [&] { Props p = button_primary(pal);   p.enabled = false;
              return button(L"Disabled", nullptr, std::move(p)); }(),
        [&] { Props p = button_secondary(pal); p.enabled = false;
              return button(L"Disabled", nullptr, std::move(p)); }(),
        [&] { Props p = button_danger(pal);    p.enabled = false;
              return button(L"Disabled", nullptr, std::move(p)); }(),
    });

    auto counter_row = row_view({
        label(count_text, std::move(count_chip_p)),
    }, static_cast<float>(tokens::spacing_md));

    auto body = col_view({
        std::move(variants),
        std::move(sizes),
        std::move(disabled),
        std::move(counter_row),
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Buttons", std::move(body), pal);
}

// ---- Checkboxes ------------------------------------------------------------

ElementPtr checkboxes_section(App& app) {
    const Palette& pal = app.host.palette();

    auto interactive = row_view({
        checkbox(L"Enable animations", app.enable_animations.get(),
                 [&app](bool v) { app.enable_animations.set(v); }),
        checkbox(L"Enable sound", app.enable_sound.get(),
                 [&app](bool v) { app.enable_sound.set(v); }),
    });

    auto disabled = row_view({
        [&] { Props p; p.enabled = false;
              return checkbox(L"Disabled, unchecked", false, nullptr, std::move(p)); }(),
        [&] { Props p; p.enabled = false;
              return checkbox(L"Disabled, checked", true, nullptr, std::move(p)); }(),
    });

    auto body = col_view({
        std::move(interactive),
        std::move(disabled),
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Checkboxes", std::move(body), pal);
}

// ---- Progress --------------------------------------------------------------

ElementPtr progress_section(App& app) {
    const Palette& pal = app.host.palette();

    Props label_strong_p;
    label_strong_p.text_color = pal.text;
    label_strong_p.font_size_pt = tokens::font_caption;
    label_strong_p.bold = true;
    label_strong_p.text_align = Align::start;

    Props bar_p;
    bar_p.flex_grow = 1.0f;

    auto labelled = [&](const wchar_t* caption, float v) {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.align = Align::center;
        row_p.gap = static_cast<float>(tokens::spacing_sm);

        auto parent = view(std::move(row_p));
        parent->children.push_back(label(caption, label_strong_p));
        parent->children.push_back(progress(v, bar_p));
        return parent;
    };

    auto body = col_view({
        labelled(L"0%",   0.00f),
        labelled(L"25%",  0.25f),
        labelled(L"50%",  0.50f),
        labelled(L"75%",  0.75f),
        labelled(L"100%", 1.00f),
        labelled(L"Live", app.progress.get()),
        button(L"Advance progress (+10%)",
               [&app] {
                   float v = app.progress.get() + 0.1f;
                   if (v > 1.0f) v = 0.0f;
                   app.progress.set(v);
               },
               button_primary(pal)),
    }, static_cast<float>(tokens::spacing_sm));

    return section_card(L"Progress", std::move(body), pal);
}

// ---- Typography ------------------------------------------------------------

ElementPtr typography_section(App& app) {
    const Palette& pal = app.host.palette();

    auto sized = [&](float pt, bool bold, const wchar_t* text) {
        Props p;
        p.font_size_pt = pt;
        p.bold = bold;
        p.text_color = pal.text;
        p.text_align = Align::start;
        return label(text, std::move(p));
    };

    auto coloured = [&](Color c, const wchar_t* text) {
        Props p;
        p.font_size_pt = tokens::font_body;
        p.text_color = c;
        p.text_align = Align::start;
        return label(text, std::move(p));
    };

    auto scale = col_view({
        sized(tokens::font_caption,  false, L"Caption (12pt regular)"),
        sized(tokens::font_body,     false, L"Body (14pt regular)"),
        sized(tokens::font_body,     true,  L"Body (14pt bold)"),
        sized(tokens::font_subtitle, false, L"Subtitle (16pt regular)"),
        sized(tokens::font_subtitle, true,  L"Subtitle (16pt bold)"),
        sized(tokens::font_title,    true,  L"Title (20pt bold)"),
        sized(tokens::font_hero,     true,  L"Hero (28pt bold)"),
    }, static_cast<float>(tokens::spacing_xs));

    auto primary_colours = row_view({
        coloured(pal.text,           L"text"),
        coloured(pal.text_secondary, L"text_secondary"),
        coloured(pal.accent,         L"accent"),
    }, static_cast<float>(tokens::spacing_lg));

    auto status_colours = row_view({
        coloured(pal.danger,  L"danger"),
        coloured(pal.success, L"success"),
        coloured(pal.warning, L"warning"),
        coloured(pal.info,    L"info"),
    }, static_cast<float>(tokens::spacing_lg));

    auto body = col_view({
        std::move(scale),
        std::move(primary_colours),
        std::move(status_colours),
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Typography", std::move(body), pal);
}

// ---- Containers (view styling) ---------------------------------------------

ElementPtr containers_section(App& app) {
    const Palette& pal = app.host.palette();

    auto swatch = [&](Color fill, const wchar_t* caption, float radius) {
        Props card_p;
        card_p.background = fill;
        card_p.radius = radius;
        card_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md)};
        card_p.border_color = pal.border;
        card_p.border_width = 1.0f;
        card_p.text_color = pal.text;
        card_p.text_align = Align::center;
        card_p.width = 140.0f;
        card_p.height = 80.0f;
        return label(caption, std::move(card_p));
    };

    auto bordered = [&] {
        Props card_p;
        card_p.background = pal.surface;
        card_p.border_color = pal.border;
        card_p.border_width = 1.0f;
        card_p.radius = static_cast<float>(tokens::radius_card);
        card_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md),
                                    static_cast<float>(tokens::spacing_md)};
        card_p.gap = static_cast<float>(tokens::spacing_xs);

        Props t;
        t.text_color = pal.text;
        t.font_size_pt = tokens::font_body;
        t.bold = true;
        t.text_align = Align::start;

        Props s;
        s.text_color = pal.text_secondary;
        s.font_size_pt = tokens::font_caption;
        s.text_align = Align::start;

        auto parent = view(std::move(card_p));
        parent->children.push_back(label(L"Bordered card", std::move(t)));
        parent->children.push_back(label(L"border 1px, radius_card", std::move(s)));
        return parent;
    };

    auto nested = [&] {
        Props outer_p;
        outer_p.background = pal.surface;
        outer_p.radius = static_cast<float>(tokens::radius_card);
        outer_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                     static_cast<float>(tokens::spacing_md),
                                     static_cast<float>(tokens::spacing_md),
                                     static_cast<float>(tokens::spacing_md)};
        outer_p.gap = static_cast<float>(tokens::spacing_sm);

        Props inner_p;
        inner_p.background = pal.surface_variant;
        inner_p.radius = static_cast<float>(tokens::radius_sm);
        inner_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                     static_cast<float>(tokens::spacing_sm),
                                     static_cast<float>(tokens::spacing_sm),
                                     static_cast<float>(tokens::spacing_sm)};

        Props t;
        t.text_color = pal.text;
        t.font_size_pt = tokens::font_caption;
        t.text_align = Align::start;

        auto inner = view(std::move(inner_p));
        inner->children.push_back(label(L"Nested surface_variant (alt rows / tracks)", std::move(t)));

        auto parent = view(std::move(outer_p));
        parent->children.push_back(std::move(inner));
        return parent;
    };

    auto surface_row = row_view({
        swatch(pal.surface, L"surface", static_cast<float>(tokens::radius_sm)),
        swatch(pal.surface, L"surface", static_cast<float>(tokens::radius_md)),
        swatch(pal.surface, L"surface", static_cast<float>(tokens::radius_lg)),
    }, static_cast<float>(tokens::spacing_md));

    auto variant_row = row_view({
        swatch(pal.surface_variant, L"surface_variant", static_cast<float>(tokens::radius_sm)),
        swatch(pal.surface_variant, L"surface_variant", static_cast<float>(tokens::radius_md)),
        swatch(pal.surface_variant, L"surface_variant", static_cast<float>(tokens::radius_lg)),
    }, static_cast<float>(tokens::spacing_md));

    auto mixed_row = row_view({
        bordered(),
        nested(),
    }, static_cast<float>(tokens::spacing_md));

    auto body = col_view({
        std::move(surface_row),
        std::move(variant_row),
        std::move(mixed_row),
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Containers", std::move(body), pal);
}

// ---- Layout ----------------------------------------------------------------

ElementPtr layout_section(App& app) {
    const Palette& pal = app.host.palette();

    Props pill_p;
    pill_p.text_color = pal.text;
    pill_p.font_size_pt = tokens::font_caption;
    pill_p.text_align = Align::center;
    pill_p.background = pal.surface_variant;
    pill_p.radius = static_cast<float>(tokens::radius_sm);
    pill_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                 static_cast<float>(tokens::spacing_xs),
                                 static_cast<float>(tokens::spacing_sm),
                                 static_cast<float>(tokens::spacing_xs)};

    auto pill = [&](const wchar_t* text) {
        Props p = pill_p;
        return label(text, std::move(p));
    };

    auto row_demo = row_view({
        pill(L"row"),
        pill(L"direction=row"),
        pill(L"gap=8"),
    }, static_cast<float>(tokens::spacing_sm));

    auto column_demo = [&] {
        Props p;
        p.direction = Direction::column;
        p.gap = static_cast<float>(tokens::spacing_sm);
        auto parent = view(std::move(p));
        parent->children.push_back(pill(L"column"));
        parent->children.push_back(pill(L"direction=col"));
        parent->children.push_back(pill(L"gap=8"));
        return parent;
    }();

    auto grow_demo = [&] {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.align = Align::stretch;
        row_p.gap = static_cast<float>(tokens::spacing_sm);

        Props flex_left = pill_p;
        Props flex_mid = pill_p;
        flex_mid.flex_grow = 1.0f;
        flex_mid.text_color = pal.on_accent;
        flex_mid.background = pal.accent;
        Props flex_right = pill_p;

        auto parent = view(std::move(row_p));
        parent->children.push_back(label(L"start", std::move(flex_left)));
        parent->children.push_back(label(L"flex_grow=1 (fills remaining)", std::move(flex_mid)));
        parent->children.push_back(label(L"end", std::move(flex_right)));
        return parent;
    }();

    auto align_demo = [&] {
        Props p;
        p.direction = Direction::row;
        p.align = Align::center;
        p.gap = static_cast<float>(tokens::spacing_sm);
        p.height = 48.0f;
        p.background = pal.surface_variant;
        p.radius = static_cast<float>(tokens::radius_sm);

        Props left_p = pill_p;
        Props mid_p = pill_p;
        mid_p.height = 32.0f;
        Props right_p = pill_p;
        right_p.height = 24.0f;

        auto parent = view(std::move(p));
        parent->children.push_back(label(L"tall",   std::move(left_p)));
        parent->children.push_back(label(L"medium", std::move(mid_p)));
        parent->children.push_back(label(L"short",  std::move(right_p)));
        return parent;
    }();

    Props body_p;
    body_p.direction = Direction::column;
    body_p.gap = static_cast<float>(tokens::spacing_md);

    auto row_demo_copy = std::make_shared<Element>();
    row_demo_copy->type = row_demo->type;
    row_demo_copy->props = row_demo->props;
    row_demo_copy->children.reserve(row_demo->children.size());
    for (const auto& c : row_demo->children) {
        auto child = std::make_shared<Element>();
        child->type = c->type;
        child->props = c->props;
        row_demo_copy->children.push_back(std::move(child));
    }

    auto body = view(std::move(body_p));
    body->children.push_back(std::move(row_demo));
    body->children.push_back(row_view({ std::move(row_demo_copy), std::move(column_demo) },
                                       static_cast<float>(tokens::spacing_lg)));
    body->children.push_back(std::move(grow_demo));
    body->children.push_back(std::move(align_demo));

    return section_card(L"Layout", std::move(body), pal);
}

// ---- Footer ----------------------------------------------------------------

ElementPtr footer(App& app) {
    const Palette& pal = app.host.palette();
    Props p;
    p.font_size_pt = tokens::font_caption;
    p.text_color = pal.text_secondary;
    p.text_align = Align::start;
    return label(L"DirectComposition scene tree · dirty-region repaint · flex layout · Tab cycles focus",
                 std::move(p));
}

// ---- Root ------------------------------------------------------------------

ElementPtr build(App& app) {
    const Palette& pal = app.host.palette();

    Props root_p;
    root_p.direction = Direction::column;
    root_p.padding = pad_all(static_cast<float>(tokens::spacing_lg));
    root_p.gap = static_cast<float>(tokens::spacing_lg);
    root_p.background = pal.background;

    // Two-column body. Without flex-wrap the only way to get a multi-column
    // page is an explicit row of two columns; each column then stacks its
    // own sections vertically. Header + footer span full width above and
    // below.
    Props body_p;
    body_p.direction = Direction::row;
    body_p.align = Align::start;
    body_p.gap = static_cast<float>(tokens::spacing_lg);

    auto left_col = col_view({
        buttons_section(app),
        checkboxes_section(app),
        progress_section(app),
    }, static_cast<float>(tokens::spacing_lg), Align::start);

    auto right_col = col_view({
        typography_section(app),
        containers_section(app),
        layout_section(app),
    }, static_cast<float>(tokens::spacing_lg), Align::start);

    auto body = view(std::move(body_p));
    body->children.push_back(std::move(left_col));
    body->children.push_back(std::move(right_col));

    auto parent = view(std::move(root_p));
    parent->children.push_back(header(app));
    parent->children.push_back(std::move(body));
    parent->children.push_back(footer(app));
    return parent;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enable_per_monitor_dpi_v2();

    App app;
    app.count = app.host.make_state<int>(0);
    app.enable_animations = app.host.make_state<bool>(true);
    app.enable_sound = app.host.make_state<bool>(false);
    app.progress = app.host.make_state<float>(0.35f);
    app.host.set_root_builder([&app] { return build(app); });

    Host::Config cfg;
    cfg.title = L"WinFlux Gallery";
    cfg.width = 1200;
    cfg.height = 1100;
    if (!app.host.create(cfg, instance)) {
        MessageBoxW(nullptr, L"WinFlux host creation failed (Direct2D unavailable?).",
                    L"WinFlux", MB_ICONERROR);
        return 1;
    }
    return app.host.run();
}