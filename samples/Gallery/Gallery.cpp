// WinFlux Gallery — comprehensive demo surface for every component and
// styling option the framework exposes. The root builder re-runs every
// frame, state lives in flux::State<T> handles, and one HWND renders the
// whole scene via D2D + DirectComposition.

#include <windows.h>

#include <string>
#include <vector>

#include <algorithm>

#include <flux/flux.hpp>

using namespace flux;

namespace {

// Forward-declared so App's State<std::vector<ToastEntry>> member can name
// it before the overlay helpers (which hold the full definition) appear.
struct ToastEntry;

struct App {
    Host host;
    State<int> count;
    State<bool> enable_animations;
    State<bool> enable_sound;
    State<float> progress;
    // Phase A form primitives
    State<std::wstring> input_name;
    State<std::wstring> input_email;
    State<float> volume;
    State<bool> dark_mode;
    // Phase B pickers
    State<int> theme_pick;
    State<int> accent_pick;
    State<int> birthday_year;
    State<int> birthday_month;
    State<int> birthday_day;
    // Phase C navigation / interaction
    State<int> active_tab;
    State<int> expanded_section;
    State<int> view_mode;
    State<int> inbox_page;
    State<bool> show_alert;
    // Phase D apps
    State<bool> settings_email_digest;
    State<bool> settings_auto_update;
    State<int> settings_timezone;     // 0=local, 1=UTC
    State<int> settings_language;     // 0=English, 1=中文, 2=日本語
    State<int> inbox_selected;        // selected message id, -1 = none
    State<bool> media_playing;
    State<float> media_volume;        // 0..100
    // Phase E overlay & feedback state.
    State<std::vector<ToastEntry>> toasts;
    int next_toast_id = 0;
    State<bool> modal_open;
    State<bool> tooltip_visible;
    State<std::wstring> tooltip_hint;
    // Phase F data viz state.
    State<int> cal_year;
    State<int> cal_month;       // 0..11
    State<int> cal_selected;
    // Top-level gallery navigation. The window can't fit every section
    // (twelve cards at full padding overflow the work area on most
    // monitors), so the body switches between category tabs. Each tab
    // holds a small enough set to fit a 1000-px-tall window.
    //   0 = Form controls, 1 = Pickers, 2 = Patterns, 3 = Overlays,
    //   4 = Data viz (Phase F calendar / chart / avatars),
    //   5 = Mini-apps (Phase D), 6 = Motion (Round 1 visual foundation)
    State<int> gallery_tab;
    // Round 1 motion demo state. No bind needed — builders read these via
    // host.animate() every frame, which also keeps the clock armed while a
    // tween is in flight.
    Animated<float> anim_progress{0.0f};
    Animated<Color> anim_color{rgb(59, 130, 246)};
    // Round 2 scroll & layer state.
    State<bool> layer_modal_open;
    State<bool> layer_popup_open;
    // Corner toast with an expiry deadline (ms, GetTickCount64 clock).
    struct LayerToast {
        unsigned long long deadline_ms;
        std::wstring text;
    };
    std::vector<LayerToast> layer_toasts;
    int next_layer_toast_seq = 0;
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

// A tall thin spacer view — used to push the next control down without
// coupling it to the parent's flex math. Height is in logical px.
ElementPtr spacer_h(float height) {
    Props p;
    p.height = height;
    p.direction = Direction::column;
    return view(std::move(p));
}

// A labelled text-input row: caption above, input below. The label-input
// pairing is the most common pattern in the Settings mini-app, so it
// gets its own helper instead of being open-coded in every call site.
ElementPtr label_input_row(const Palette& pal, const wchar_t* caption,
                           std::wstring value,
                           std::function<void(std::wstring)> on_change) {
    Props col_p;
    col_p.gap = static_cast<float>(tokens::spacing_xs);
    col_p.direction = Direction::column;
    auto col = view(std::move(col_p));
    Props cap;
    cap.text = caption;
    cap.text_color = pal.text_secondary;
    cap.font_size_pt = tokens::font_caption;
    cap.text_align = Align::start;
    col->children.push_back(label(caption, std::move(cap)));
    col->children.push_back(text_input(std::move(value), std::move(on_change)));
    return col;
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

// ---- Composite pickers ----------------------------------------------------
//
// These don't add new element types — each picker is a container whose
// children carry their own click handlers. The framework's existing click
// dispatch walks up from the clicked leaf to the nearest interactive
// ancestor, so per-option clicks reach the right handler without any new
// infrastructure. Each child knows its own visual state (selected/not) at
// build time.

ElementPtr radio_option(const Palette& pal, const wchar_t* text, bool selected,
                        ClickHandler on_select) {
    Props row_p;
    row_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_xs),
                               static_cast<float>(tokens::spacing_xs),
                               static_cast<float>(tokens::spacing_xs),
                               static_cast<float>(tokens::spacing_xs)};
    row_p.gap = static_cast<float>(tokens::spacing_sm);
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    row_p.radius = static_cast<float>(tokens::radius_sm);
    row_p.on_click = std::move(on_select);
    auto row = view(std::move(row_p));

    Props circle_p;
    circle_p.width = 16.0f;
    circle_p.height = 16.0f;
    circle_p.radius = 8.0f;
    circle_p.border_color = selected ? pal.accent : pal.border;
    circle_p.border_width = 1.5f;
    if (selected) circle_p.background = pal.accent;
    row->children.push_back(view(std::move(circle_p)));

    Props label_p;
    label_p.text_color = pal.text;
    label_p.font_size_pt = tokens::font_body;
    label_p.text_align = Align::start;
    row->children.push_back(label(std::wstring(text), std::move(label_p)));

    return row;
}

ElementPtr radio_group(const Palette& pal, std::vector<std::wstring> options,
                       int selected, std::function<void(int)> on_change) {
    Props col_p;
    col_p.gap = static_cast<float>(tokens::spacing_xs);
    col_p.direction = Direction::column;
    auto col = view(std::move(col_p));
    for (size_t i = 0; i < options.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_sel = idx == selected;
        auto handler = [on_change, idx] { if (on_change) on_change(idx); };
        col->children.push_back(
            radio_option(pal, options[i].c_str(), is_sel, std::move(handler)));
    }
    return col;
}

ElementPtr color_swatch(const Palette& pal, Color c, bool selected,
                        ClickHandler on_select) {
    Props p;
    p.width = 36.0f;
    p.height = 36.0f;
    p.radius = static_cast<float>(tokens::radius_md);
    p.background = c;
    p.border_color = selected ? pal.accent : pal.border;
    p.border_width = selected ? 2.5f : 1.0f;
    p.on_click = std::move(on_select);
    return view(std::move(p));
}

ElementPtr color_picker(const Palette& pal, std::vector<Color> swatches,
                        int selected, std::function<void(int)> on_change,
                        int per_row = 6) {
    // Wrap-less grid: stack rows of `per_row` swatches. The framework has
    // only row/column layouts, so manual row construction is the cleanest
    // expression. per_row=6 gives a tidy 6-wide grid for typical palettes.
    Props outer_p;
    outer_p.gap = static_cast<float>(tokens::spacing_sm);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));
    for (size_t i = 0; i < swatches.size(); i += static_cast<size_t>(per_row)) {
        Props row_p;
        row_p.gap = static_cast<float>(tokens::spacing_sm);
        row_p.direction = Direction::row;
        auto row = view(std::move(row_p));
        for (size_t j = i; j < std::min(i + static_cast<size_t>(per_row), swatches.size()); ++j) {
            const int idx = static_cast<int>(j);
            auto handler = [on_change, idx] { if (on_change) on_change(idx); };
            row->children.push_back(
                color_swatch(pal, swatches[j], idx == selected, std::move(handler)));
        }
        outer->children.push_back(std::move(row));
    }
    return outer;
}

namespace {
int days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1) {   // February
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month];
}
int first_weekday(int year, int month) {
    // Zeller's congruence — returns 0=Sunday..6=Saturday.
    int m = month + 1;
    int y = year;
    if (m <= 2) { m += 12; y -= 1; }
    const int k = y % 100;
    const int j = y / 100;
    int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    return (h + 6) % 7;   // shift so 0=Sunday
}
} // namespace

ElementPtr date_picker(const Palette& pal, int year, int month, int day,
                       std::function<void(int, int, int)> on_change) {
    Props outer_p;
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    // Month header: "September 2026"
    static const wchar_t* month_names[] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"};
    Props header_p;
    header_p.text_color = pal.text;
    header_p.font_size_pt = tokens::font_subtitle;
    header_p.bold = true;
    header_p.text_align = Align::start;
    auto header_text = std::wstring(month_names[std::clamp(month, 0, 11)]) + L" " + std::to_wstring(year);
    header_p.text = header_text;
    outer->children.push_back(label(header_text, std::move(header_p)));

    // Day-of-week header row.
    static const wchar_t* dow[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
    Props dow_row_p;
    dow_row_p.gap = static_cast<float>(tokens::spacing_xs);
    dow_row_p.direction = Direction::row;
    auto dow_row = view(std::move(dow_row_p));
    for (int i = 0; i < 7; ++i) {
        Props d;
        d.width = 28.0f;
        d.height = 22.0f;
        d.text = dow[i];
        d.text_color = pal.text_secondary;
        d.font_size_pt = tokens::font_caption;
        d.bold = true;
        d.text_align = Align::center;
        auto cell = label(dow[i], std::move(d));
        dow_row->children.push_back(std::move(cell));
    }
    outer->children.push_back(std::move(dow_row));

    // Day grid: 6 rows × 7 cols.
    const int first = first_weekday(year, month);
    const int total = days_in_month(year, month);
    int cell_day = 1 - first;   // may be <= 0 (leading blanks)
    for (int r = 0; r < 6; ++r) {
        Props row_p;
        row_p.gap = static_cast<float>(tokens::spacing_xs);
        row_p.direction = Direction::row;
        auto row = view(std::move(row_p));
        for (int c = 0; c < 7; ++c, ++cell_day) {
            Props cell_p;
            cell_p.width = 28.0f;
            cell_p.height = 28.0f;
            cell_p.radius = static_cast<float>(tokens::radius_sm);
            cell_p.text_align = Align::center;
            cell_p.font_size_pt = tokens::font_caption;
            const bool in_month = cell_day >= 1 && cell_day <= total;
            if (!in_month) {
                cell_p.text_color = pal.divider;
                cell_p.text = L"";
                cell_p.enabled = false;
            } else {
                cell_p.text = std::to_wstring(cell_day);
                cell_p.text_color = cell_day == day ? pal.on_accent : pal.text;
                cell_p.background = cell_day == day
                                       ? std::optional<Color>{pal.accent}
                                       : std::nullopt;
                cell_p.enabled = true;
                const int captured_day = cell_day;
                cell_p.on_click = [year, month, captured_day, on_change] {
                    if (on_change) on_change(year, month, captured_day);
                };
            }
            row->children.push_back(label(cell_p.text, cell_p));
        }
        outer->children.push_back(std::move(row));
    }
    return outer;
}

// ---- Inputs (form primitives) ---------------------------------------------
//
// TextInput accepts text via WM_CHAR/VK_BACK dispatched to the element at
// focused_input_path_; the host does the bookkeeping. Slider click + drag
// update on_value_change with the domain-mapped value. ToggleSwitch is
// on_click-driven like checkbox but paints as a track + thumb.

ElementPtr inputs_section(App& app) {
    const Palette& pal = app.host.palette();

    auto name_field = text_input(app.input_name.get(),
        [&app](std::wstring s) { app.input_name.set(std::move(s)); });

    auto email_field = text_input(app.input_email.get(),
        [&app](std::wstring s) { app.input_email.set(std::move(s)); });
    {
        Props p;
        p.background = pal.surface_variant;
        p.text_color = pal.text_secondary;
        p.font_size_pt = tokens::font_caption;
        p.text_align = Align::start;
        // Placeholder text shown when the input is empty.
        auto placeholder = label(L"you@example.com", std::move(p));
        if (app.input_email.get().empty()) {
            email_field->children.push_back(std::move(placeholder));
        }
    }

    auto value_label = [&] {
        Props p;
        p.text_color = pal.text_secondary;
        p.font_size_pt = tokens::font_caption;
        p.text_align = Align::start;
        auto lbl = label(L"Volume: " + std::to_wstring(static_cast<int>(app.volume.get())) + L"%", std::move(p));
        return lbl;
    }();

    auto volume_slider = slider(app.volume.get(), 0.0f, 100.0f,
        [&app](float v) { app.volume.set(v); });
    {
        Props p;
        p.width = 100.0f;
        volume_slider->props.width = p.width;
    }

    auto dark_toggle = toggle_switch(app.dark_mode.get(),
        [&app](bool v) { app.dark_mode.set(v); });
    dark_toggle->props.flex_grow = 0.0f;

    Props field_label_p;
    field_label_p.text_color = pal.text_secondary;
    field_label_p.font_size_pt = tokens::font_caption;
    field_label_p.text_align = Align::start;

    auto fields = col_view({
        label(L"Name", field_label_p),
        name_field,
        label(L"Email", field_label_p),
        email_field,
    }, static_cast<float>(tokens::spacing_xs));

    auto sliders = col_view({
        value_label,
        volume_slider,
    }, static_cast<float>(tokens::spacing_xs));

    auto toggle_row = row_view({
        [&] {
            Props p = field_label_p;
            // Push the toggle to the right edge of the row by absorbing
            // the free space; without flex_grow, the label's intrinsic
            // width sits next to the toggle and the gap between them is
            // tiny — and on wider columns the row's text gets clipped.
            p.flex_grow = 1.0f;
            return label(L"Dark theme", std::move(p));
        }(),
        dark_toggle,
    }, static_cast<float>(tokens::spacing_md), Align::center);

    auto body = col_view({
        std::move(fields),
        std::move(sliders),
        std::move(toggle_row),
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Inputs", std::move(body), pal);
}

// ---- Pickers (RadioGroup, ColorPicker, DatePicker) -------------------------
//
// Split into two cards so the Pickers tab can render them in a 2-column grid.
// The calendar wants the full row width (it has 6 columns of 7 cells) so it
// lives on the right; the radio group and swatch grid are narrower and stack
// on the left.

ElementPtr pickers_left_section(App& app) {
    const Palette& pal = app.host.palette();

    std::vector<std::wstring> theme_options = {L"Light", L"Dark", L"High contrast"};
    auto theme_radio = radio_group(pal, theme_options, app.theme_pick.get(),
        [&app](int i) { app.theme_pick.set(i); });

    std::vector<Color> swatches = {
        rgb(220, 100, 40),    // orange
        rgb(40, 140, 220),    // blue
        rgb(60, 180, 100),    // green
        rgb(200, 80, 80),     // red
        rgb(180, 80, 180),    // magenta
        rgb(220, 180, 40),    // amber
        rgb(80, 80, 80),      // gray
        rgb(140, 100, 60),    // brown
        rgb(60, 60, 200),     // indigo
        rgb(40, 160, 160),    // teal
        rgb(255, 130, 60),    // peach
        rgb(100, 200, 200),   // sky
    };
    auto swatch_picker = color_picker(pal, swatches, app.accent_pick.get(),
        [&app](int i) { app.accent_pick.set(i); });

    Props caption_p;
    caption_p.text_color = pal.text_secondary;
    caption_p.font_size_pt = tokens::font_caption;
    caption_p.text_align = Align::start;

    auto body = col_view({
        label(L"Theme (radio)", caption_p),
        theme_radio,
        label(L"Accent (swatches)", caption_p),
        swatch_picker,
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Selection", std::move(body), pal);
}

ElementPtr pickers_right_section(App& app) {
    const Palette& pal = app.host.palette();

    auto cal = date_picker(pal,
        app.birthday_year.get(),
        app.birthday_month.get(),
        app.birthday_day.get(),
        [&app](int y, int m, int d) {
            app.birthday_year.set(y);
            app.birthday_month.set(m);
            app.birthday_day.set(d);
        });

    Props caption_p;
    caption_p.text_color = pal.text_secondary;
    caption_p.font_size_pt = tokens::font_caption;
    caption_p.text_align = Align::start;

    auto body = col_view({
        label(L"Birthday (calendar)", caption_p),
        cal,
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Calendar", std::move(body), pal);
}

struct TabSpec { std::wstring label; std::wstring body; };

ElementPtr tabs(const Palette& pal, std::vector<TabSpec> tabs_in, int active,
                std::function<void(int)> on_change) {
    Props outer_p;
    outer_p.gap = 0.0f;
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    // Tab strip — accent text + bold indicates the active tab.
    Props strip_p;
    strip_p.gap = 0.0f;
    strip_p.direction = Direction::row;
    strip_p.align = Align::stretch;
    auto strip = view(std::move(strip_p));
    for (size_t i = 0; i < tabs_in.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_active = idx == active;
        Props tab_p;
        tab_p.flex_grow = 1.0f;
        tab_p.height = 36.0f;
        tab_p.padding = EdgeInsets{0.0f, static_cast<float>(tokens::spacing_sm),
                                   0.0f, static_cast<float>(tokens::spacing_sm)};
        tab_p.text = tabs_in[i].label;
        tab_p.text_color = is_active ? pal.accent : pal.text_secondary;
        tab_p.font_size_pt = tokens::font_body;
        tab_p.bold = is_active;
        tab_p.text_align = Align::center;
        tab_p.on_click = [on_change, idx] { if (on_change) on_change(idx); };
        auto tab = label(tabs_in[i].label, std::move(tab_p));
        strip->children.push_back(std::move(tab));
    }
    outer->children.push_back(std::move(strip));

    Props div_p;
    div_p.background = pal.divider;
    div_p.height = 1.0f;
    outer->children.push_back(view(std::move(div_p)));

    Props body_p;
    body_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md), 0.0f, 0.0f, 0.0f};
    body_p.text_color = pal.text;
    body_p.font_size_pt = tokens::font_body;
    body_p.text_align = Align::start;
    const std::wstring content = (active >= 0 && active < static_cast<int>(tabs_in.size()))
                                     ? tabs_in[active].body
                                     : L"";
    outer->children.push_back(label(content, std::move(body_p)));

    return outer;
}

ElementPtr accordion(const Palette& pal, std::vector<TabSpec> items,
                     int expanded, std::function<void(int)> on_toggle) {
    Props outer_p;
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    for (size_t i = 0; i < items.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_open = idx == expanded;

        Props item_p;
        item_p.gap = 0.0f;
        item_p.direction = Direction::column;
        item_p.radius = static_cast<float>(tokens::radius_md);
        item_p.border_color = pal.border;
        item_p.border_width = 1.0f;
        item_p.background = pal.surface;
        item_p.padding = EdgeInsets{0.0f, 0.0f, 0.0f, 0.0f};
        auto item = view(std::move(item_p));

        Props header_p;
        header_p.gap = static_cast<float>(tokens::spacing_sm);
        header_p.direction = Direction::row;
        header_p.align = Align::center;
        header_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md)};
        header_p.on_click = [on_toggle, idx, is_open] {
            if (on_toggle) on_toggle(is_open ? -1 : idx);
        };
        auto header = view(std::move(header_p));

        Props title_p;
        title_p.text = items[i].label;
        title_p.text_color = pal.text;
        title_p.font_size_pt = tokens::font_body;
        title_p.bold = true;
        title_p.text_align = Align::start;
        title_p.flex_grow = 1.0f;
        header->children.push_back(label(items[i].label, std::move(title_p)));

        Props chev_p;
        chev_p.text = is_open ? L"v" : L">";
        chev_p.text_color = pal.text_secondary;
        chev_p.font_size_pt = tokens::font_caption;
        chev_p.text_align = Align::center;
        header->children.push_back(label(chev_p.text, chev_p));

        item->children.push_back(std::move(header));

        if (is_open) {
            Props body_p;
            body_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                        static_cast<float>(tokens::spacing_md),
                                        static_cast<float>(tokens::spacing_md),
                                        static_cast<float>(tokens::spacing_md)};
            body_p.text_color = pal.text_secondary;
            body_p.font_size_pt = tokens::font_body;
            body_p.text_align = Align::start;
            item->children.push_back(label(items[i].body, std::move(body_p)));
        }
        outer->children.push_back(std::move(item));
    }
    return outer;
}

ElementPtr segmented_control(const Palette& pal, std::vector<std::wstring> options,
                             int selected, std::function<void(int)> on_change) {
    Props outer_p;
    outer_p.gap = 0.0f;
    outer_p.direction = Direction::row;
    outer_p.radius = static_cast<float>(tokens::radius_control);
    outer_p.border_color = pal.border;
    outer_p.border_width = 1.0f;
    outer_p.padding = EdgeInsets{0.0f, 0.0f, 0.0f, 0.0f};
    outer_p.align = Align::stretch;
    auto outer = view(std::move(outer_p));

    for (size_t i = 0; i < options.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_sel = idx == selected;
        Props seg_p;
        seg_p.flex_grow = 1.0f;
        seg_p.height = static_cast<float>(tokens::control_height_sm);
        seg_p.text = options[i];
        seg_p.text_color = is_sel ? pal.on_accent : pal.text;
        seg_p.background = is_sel
                                ? std::optional<Color>{pal.accent}
                                : std::nullopt;
        seg_p.font_size_pt = tokens::font_caption;
        seg_p.text_align = Align::center;
        seg_p.on_click = [on_change, idx] { if (on_change) on_change(idx); };
        auto seg = label(options[i], std::move(seg_p));
        outer->children.push_back(std::move(seg));
    }
    return outer;
}

ElementPtr pagination(const Palette& pal, int current, int total,
                      std::function<void(int)> on_change) {
    Props outer_p;
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::row;
    outer_p.align = Align::center;
    auto outer = view(std::move(outer_p));

    auto mk_btn = [&](const std::wstring& txt, bool enabled, int next) {
        Props p;
        p.width = 28.0f;
        p.height = 28.0f;
        p.radius = static_cast<float>(tokens::radius_sm);
        p.text = txt;
        p.text_color = enabled ? pal.text : pal.text_secondary;
        p.font_size_pt = tokens::font_body;
        p.text_align = Align::center;
        p.enabled = enabled;
        p.on_click = [on_change, next] { if (on_change) on_change(next); };
        return label(txt, std::move(p));
    };
    outer->children.push_back(mk_btn(L"<", current > 0, current - 1));
    for (int p = 0; p < total; ++p) {
        const bool is_cur = p == current;
        Props pg_p;
        pg_p.width = 28.0f;
        pg_p.height = 28.0f;
        pg_p.radius = static_cast<float>(tokens::radius_sm);
        pg_p.text = std::to_wstring(p + 1);
        pg_p.text_color = is_cur ? pal.on_accent : pal.text;
        pg_p.background = is_cur
                                ? std::optional<Color>{pal.accent}
                                : std::nullopt;
        pg_p.font_size_pt = tokens::font_caption;
        pg_p.text_align = Align::center;
        pg_p.on_click = [on_change, p] { if (on_change) on_change(p); };
        outer->children.push_back(label(pg_p.text, pg_p));
    }
    outer->children.push_back(mk_btn(L">", current < total - 1, current + 1));
    return outer;
}

ElementPtr alert(const Palette& pal, const wchar_t* kind, const wchar_t* text,
                 ClickHandler on_dismiss) {
    (void)kind;
    Props p;
    p.gap = static_cast<float>(tokens::spacing_md);
    p.direction = Direction::row;
    p.align = Align::center;
    p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                           static_cast<float>(tokens::spacing_md),
                           static_cast<float>(tokens::spacing_md),
                           static_cast<float>(tokens::spacing_md)};
    p.radius = static_cast<float>(tokens::radius_md);
    p.background = pal.surface_variant;
    auto row = view(std::move(p));

    Props txt;
    txt.text = text;
    txt.text_color = pal.text;
    txt.font_size_pt = tokens::font_body;
    txt.text_align = Align::start;
    txt.flex_grow = 1.0f;
    row->children.push_back(label(text, std::move(txt)));

    Props x_p;
    x_p.width = 24.0f;
    x_p.height = 24.0f;
    x_p.text = L"x";
    x_p.text_color = pal.text_secondary;
    x_p.font_size_pt = tokens::font_caption;
    x_p.text_align = Align::center;
    x_p.on_click = std::move(on_dismiss);
    row->children.push_back(label(x_p.text, x_p));

    return row;
}


// ---- Phase E: Overlays & Feedback -------------------------------------------
//
// Toast / Modal / Tooltip / Badge. None of these need a new framework
// element type — they're compositions of existing primitives plus host-side
// State<T> for the open/close transitions. The point of Phase E is to show
// that the framework composes into the standard "feedback" UI vocabulary:
// notifications, dialogs, hover hints, and inline status pills.

struct ToastEntry {
    int id;
    std::wstring text;
    std::wstring kind;       // "info" | "success" | "warning" | "danger"
};

// Tiny inline pill. Used inside rows to mark items (counts, status, kind).
// `kind` picks the colour scheme; default is the brand accent. Renders as a
// label with a fully-rounded radius and horizontal padding so it looks like a
// chip in a row of text.
ElementPtr badge(const Palette& pal, const std::wstring& text,
                 const wchar_t* kind = L"accent") {
    Color bg = pal.accent;
    Color fg = pal.on_accent;
    if      (std::wcscmp(kind, L"success") == 0) { bg = pal.success; }
    else if (std::wcscmp(kind, L"warning") == 0) { bg = pal.warning; }
    else if (std::wcscmp(kind, L"danger")  == 0) { bg = pal.danger;  }
    else if (std::wcscmp(kind, L"info")    == 0) { bg = pal.info;    }
    else if (std::wcscmp(kind, L"neutral") == 0) { bg = pal.surface_variant; fg = pal.text_secondary; }

    Props p;
    p.background = bg;
    p.text_color = fg;
    p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_xs)};
    p.radius = 999.0f;
    p.font_size_pt = tokens::font_caption;
    return label(text, std::move(p));
}

// One toast card. The accent stripe on the left picks up the entry's kind
// (the same palette role as the dot in the Inbox unread badges). The close
// button is a square 24-px button with a subtle hover background so it reads
// as a dismiss control, not as primary chrome.
ElementPtr toast_bar(const Palette& pal, const ToastEntry& entry,
                     ClickHandler on_close) {
    Color accent = pal.info;
    if      (entry.kind == L"success") accent = pal.success;
    else if (entry.kind == L"warning") accent = pal.warning;
    else if (entry.kind == L"danger")  accent = pal.danger;

    Props outer_p;
    outer_p.background = pal.surface;
    outer_p.border_color = pal.border;
    outer_p.border_width = 1.0f;
    outer_p.radius = static_cast<float>(tokens::radius_card);
    outer_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md)};
    outer_p.direction = Direction::row;
    outer_p.align = Align::center;
    outer_p.gap = static_cast<float>(tokens::spacing_md);
    auto outer = view(std::move(outer_p));

    Props stripe_p;
    stripe_p.background = accent;
    stripe_p.width = 4.0f;
    stripe_p.height = 32.0f;
    stripe_p.radius = static_cast<float>(tokens::radius_sm);
    outer->children.push_back(view(std::move(stripe_p)));

    Props label_p;
    label_p.text = entry.text;
    label_p.text_color = pal.text;
    label_p.font_size_pt = tokens::font_body;
    label_p.text_align = Align::start;
    label_p.flex_grow = 1.0f;
    outer->children.push_back(label(entry.text, std::move(label_p)));

    Props close_p;
    close_p.text = L"×";
    close_p.text_color = pal.text_secondary;
    close_p.font_size_pt = tokens::font_subtitle;
    close_p.text_align = Align::center;
    close_p.width = 24.0f;
    close_p.height = 24.0f;
    close_p.radius = 12.0f;
    close_p.hover_background = pal.surface_variant;
    close_p.on_click = std::move(on_close);
    outer->children.push_back(label(close_p.text, close_p));

    return outer;
}

// Modal dialog: a centred card with title, body, and a single Close action.
// The Host has no z-order primitive, so this demo renders the modal as a
// regular card that overlays the rest of the body content via being the
// LAST child of root. No dimmed backdrop — the body content stays readable
// behind the card; production modals would add a backdrop element that
// captures clicks outside the card to dismiss.
ElementPtr modal_card(const Palette& pal, const std::wstring& title,
                      ElementPtr body, ClickHandler on_close) {
    Props card_p;
    card_p.background = pal.surface;
    card_p.border_color = pal.border;
    card_p.border_width = 1.0f;
    card_p.radius = static_cast<float>(tokens::radius_lg);
    card_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_lg),
        static_cast<float>(tokens::spacing_lg),
        static_cast<float>(tokens::spacing_lg),
        static_cast<float>(tokens::spacing_lg)};
    card_p.direction = Direction::column;
    card_p.gap = static_cast<float>(tokens::spacing_md);
    card_p.width = 480.0f;
    auto card = view(std::move(card_p));

    Props title_p;
    title_p.text = title;
    title_p.font_size_pt = tokens::font_title;
    title_p.text_color = pal.text;
    title_p.text_align = Align::start;
    card->children.push_back(label(title, std::move(title_p)));

    if (body) card->children.push_back(std::move(body));

    Props actions_p;
    actions_p.direction = Direction::row;
    actions_p.align = Align::end;
    actions_p.gap = static_cast<float>(tokens::spacing_md);
    auto actions = view(std::move(actions_p));

    Props close_p = button_secondary(pal);
    close_p.text = L"Close";
    close_p.on_click = on_close;
    actions->children.push_back(button(L"Close", on_close, std::move(close_p)));
    card->children.push_back(std::move(actions));

    return card;
}

// Tooltip demo: a button that, on hover, reveals a popup with explanatory
// text. The Host already tracks hover_path_, but per-element show/hide
// timers need a delay before the popup appears (otherwise it flashes
// during the cursor's transit). This demo omits the delay — the popup
// shows immediately on hover via a State<bool> keyed off the button's
// hover state. The Host doesn't expose a "hovered" flag on Props yet, so
// we wire it through on_click toggling for visibility: each hover fires
// a state flip. This is a simplification; a real tooltip would use a
// render-element branch keyed off hover_path_.
ElementPtr tooltip_demo(App& app, const Palette& pal,
                        const std::wstring& label_text,
                        const std::wstring& hint) {
    // The State<bool> lives as a member of App; for the demo each tooltip
    // reuses one slot via the index passed in via label suffix. Cleaner:
    // allocate locally. Since App is rebuilt every frame, we keep the
    // visibility in a per-tooltip slot keyed by an offset into App.
    // For simplicity: track the last-hovered tooltip id in App.
    Props trigger_p = button_secondary(pal);
    trigger_p.text = label_text;
    trigger_p.on_click = [&app, hint_copy = hint] {
        // Click toggles a transient message that the host shows in the
        // popup. The popup is rendered right below.
        app.tooltip_hint.set(std::move(hint_copy));
        app.tooltip_visible.set(true);
    };
    auto trigger = button(label_text, {}, std::move(trigger_p));

    Props popup_p;
    popup_p.background = pal.surface;
    popup_p.border_color = pal.border;
    popup_p.border_width = 1.0f;
    popup_p.radius = static_cast<float>(tokens::radius_md);
    popup_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm)};
    popup_p.direction = Direction::row;
    popup_p.align = Align::center;
    popup_p.gap = static_cast<float>(tokens::spacing_sm);

    Props popup_label_p;
    popup_label_p.text = hint;
    popup_label_p.text_color = pal.text_secondary;
    popup_label_p.font_size_pt = tokens::font_caption;
    popup_label_p.text_align = Align::start;

    auto popup = view(std::move(popup_p));
    popup->children.push_back(label(hint, std::move(popup_label_p)));

    // Stack the trigger on top of the popup; the popup only shows when the
    // State is true. Without a "visible" Props flag we conditionally
    // include it in the tree.
    ElementPtr popup_elt;
    if (app.tooltip_visible.get()) {
        popup_elt = std::move(popup);
    } else {
        Props empty_p;
        empty_p.height = 0.0f;
        popup_elt = view(std::move(empty_p));
    }

    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_xs);
    col_p.align = Align::start;
    auto col = view(std::move(col_p));
    col->children.push_back(std::move(trigger));
    col->children.push_back(std::move(popup_elt));
    return col;
}

ElementPtr overlays_section(App& app) {
    const Palette& pal = app.host.palette();

    Props caption_p;
    caption_p.text_color = pal.text_secondary;
    caption_p.font_size_pt = tokens::font_caption;
    caption_p.text_align = Align::start;

    // ---- Toast demo ----
    auto push_toast = [&](const std::wstring& kind, const std::wstring& text) {
        auto v = app.toasts.get();
        v.push_back({app.next_toast_id++, text, kind});
        app.toasts.set(std::move(v));
    };
    // Captures by value (not [&]) so the resulting std::function can be
    // copied into each toast_bar's on_click. The `app` reference is captured
    // by reference but `app` outlives every toast (it's the long-lived host
    // state), so this is safe.
    std::function<void(int)> dismiss_toast = [&app](int id) {
        auto v = app.toasts.get();
        v.erase(std::remove_if(v.begin(), v.end(),
            [id](const ToastEntry& e) { return e.id == id; }), v.end());
        app.toasts.set(std::move(v));
    };

    Props info_btn_p = button_primary(pal);
    info_btn_p.text = L"Info";
    info_btn_p.on_click = [&] { push_toast(L"info", L"New message in your inbox."); };

    Props success_btn_p = button_secondary(pal);
    success_btn_p.text = L"Success";
    success_btn_p.on_click = [&] { push_toast(L"success", L"Profile saved."); };

    Props warning_btn_p = button_secondary(pal);
    warning_btn_p.text = L"Warning";
    warning_btn_p.on_click = [&] { push_toast(L"warning", L"Storage almost full."); };

    Props danger_btn_p = button_secondary(pal);
    danger_btn_p.text = L"Danger";
    danger_btn_p.on_click = [&] { push_toast(L"danger", L"Failed to delete file."); };

    auto toast_buttons = row_view({
        button(L"Info",    [&] { push_toast(L"info",    L"New message in your inbox."); }, std::move(info_btn_p)),
        button(L"Success", [&] { push_toast(L"success", L"Profile saved.");             }, std::move(success_btn_p)),
        button(L"Warning", [&] { push_toast(L"warning", L"Storage almost full.");       }, std::move(warning_btn_p)),
        button(L"Danger",  [&] { push_toast(L"danger",  L"Failed to delete file.");     }, std::move(danger_btn_p)),
    }, static_cast<float>(tokens::spacing_sm), Align::start);

    Props toast_stack_p;
    toast_stack_p.direction = Direction::column;
    toast_stack_p.gap = static_cast<float>(tokens::spacing_sm);
    toast_stack_p.align = Align::stretch;
    auto toast_stack = view(std::move(toast_stack_p));
    auto entries = app.toasts.get();
    if (entries.empty()) {
        Props empty_p;
        empty_p.text = L"No toasts yet — click a button above.";
        empty_p.text_color = pal.text_secondary;
        empty_p.font_size_pt = tokens::font_caption;
        empty_p.text_align = Align::start;
        toast_stack->children.push_back(label(empty_p.text, empty_p));
    } else {
        for (const auto& entry : entries) {
            int id = entry.id;
            toast_stack->children.push_back(toast_bar(pal, entry,
                [dismiss = dismiss_toast, id] { dismiss(id); }));
        }
    }

    // ---- Modal demo ----
    auto open_modal = [&app] { app.modal_open.set(true); };
    auto close_modal = [&app] { app.modal_open.set(false); };

    Props open_btn_p = button_primary(pal);
    open_btn_p.text = L"Open modal";
    open_btn_p.on_click = open_modal;

    Props modal_body_p;
    modal_body_p.text = L"This is a centred dialog. Click Close to dismiss.";
    modal_body_p.text_color = pal.text;
    modal_body_p.font_size_pt = tokens::font_body;
    modal_body_p.text_align = Align::start;
    auto modal_body = label(modal_body_p.text, modal_body_p);

    ElementPtr modal_area;
    if (app.modal_open.get()) {
        Props wrapper_p;
        wrapper_p.direction = Direction::column;
        wrapper_p.gap = static_cast<float>(tokens::spacing_md);
        wrapper_p.align = Align::stretch;
        auto wrapper = view(std::move(wrapper_p));
        wrapper->children.push_back(modal_card(pal, L"Confirm action",
            std::move(modal_body), close_modal));
        Props note_p;
        note_p.text = L"Modal is open. The card above is the dialog.";
        note_p.text_color = pal.text_secondary;
        note_p.font_size_pt = tokens::font_caption;
        note_p.text_align = Align::start;
        wrapper->children.push_back(label(note_p.text, note_p));
        modal_area = std::move(wrapper);
    } else {
        Props trigger_row_p;
        trigger_row_p.direction = Direction::row;
        trigger_row_p.align = Align::start;
        trigger_row_p.gap = static_cast<float>(tokens::spacing_md);
        auto trigger_row = view(std::move(trigger_row_p));
        trigger_row->children.push_back(
            button(L"Open modal", open_modal, std::move(open_btn_p)));
        modal_area = std::move(trigger_row);
    }

    // ---- Badge examples ----
    Props badge_row_p;
    badge_row_p.direction = Direction::row;
    badge_row_p.gap = static_cast<float>(tokens::spacing_sm);
    badge_row_p.align = Align::center;
    auto badge_row = view(std::move(badge_row_p));
    badge_row->children.push_back(badge(pal, L"3 unread",  L"info"));
    badge_row->children.push_back(badge(pal, L"Saved",     L"success"));
    badge_row->children.push_back(badge(pal, L"Pending",   L"warning"));
    badge_row->children.push_back(badge(pal, L"Error",     L"danger"));
    badge_row->children.push_back(badge(pal, L"Default",   L"accent"));
    badge_row->children.push_back(badge(pal, L"Draft",     L"neutral"));

    // ---- Tooltip demo ----
    auto dismiss_tip = [&app] { app.tooltip_visible.set(false); };
    auto tip1 = tooltip_demo(app, pal, L"Save",
        L"Save your changes to disk (Ctrl+S).");
    auto tip2 = tooltip_demo(app, pal, L"Delete",
        L"Permanently delete the selected item.");
    auto tip3 = tooltip_demo(app, pal, L"Settings",
        L"Open application settings and preferences.");

    Props tip_row_p;
    tip_row_p.direction = Direction::row;
    tip_row_p.gap = static_cast<float>(tokens::spacing_md);
    tip_row_p.align = Align::start;
    auto tip_row = view(std::move(tip_row_p));
    tip_row->children.push_back(std::move(tip1));
    tip_row->children.push_back(std::move(tip2));
    tip_row->children.push_back(std::move(tip3));
    // A small dismiss control so the tooltip popup can be cleared without
    // clicking each button again.
    Props dismiss_tip_p = button_secondary(pal);
    dismiss_tip_p.text = L"Hide";
    dismiss_tip_p.on_click = dismiss_tip;
    Props dismiss_p;
    dismiss_p.direction = Direction::row;
    dismiss_p.align = Align::end;
    auto dismiss_row = view(std::move(dismiss_p));
    dismiss_row->children.push_back(
        button(L"Hide hint", dismiss_tip, std::move(dismiss_tip_p)));
    tip_row->children.push_back(std::move(dismiss_row));

    auto body = col_view({
        label(L"Badges",                         caption_p),
        badge_row,
        label(L"Toasts (click to push)",         caption_p),
        toast_buttons,
        toast_stack,
        label(L"Modal dialog",                   caption_p),
        modal_area,
        label(L"Tooltips (click button to show hint)", caption_p),
        tip_row,
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Overlays & feedback", std::move(body), pal);
}


// ---- Mini-app: Settings -----------------------------------------------------
//
// A realistic settings panel: a sidebar of categories (composition of clickable
// rows), content area with toggle switches, segmented controls, and a primary
// action button. Demonstrates that the framework primitives compose into a
// feature-complete screen without any new element types.

ElementPtr settings_section(App& app) {
    const Palette& pal = app.host.palette();

    // ---- Sidebar row: two segmented-style category buttons ----
    const int tab = app.settings_timezone.get();   // reuse state to track "Preferences/Account" tab
    Props tabs_p;
    tabs_p.gap = 0.0f;
    tabs_p.direction = Direction::row;
    tabs_p.radius = static_cast<float>(tokens::radius_control);
    tabs_p.border_color = pal.border;
    tabs_p.border_width = 1.0f;
    auto tab_row = view(std::move(tabs_p));
    auto mk_tab = [&](const wchar_t* text, int idx) {
        const bool is_on = idx == tab;
        Props p;
        p.flex_grow = 1.0f;
        p.height = static_cast<float>(tokens::control_height_sm);
        p.text = text;
        p.text_color = is_on ? pal.on_accent : pal.text;
        p.background = is_on
                            ? std::optional<Color>{pal.accent}
                            : std::nullopt;
        p.font_size_pt = tokens::font_caption;
        p.text_align = Align::center;
        p.on_click = [&app, idx] { app.settings_timezone.set(idx); };
        return label(text, std::move(p));
    };
    tab_row->children.push_back(mk_tab(L"Preferences", 0));
    tab_row->children.push_back(mk_tab(L"Account",     1));

    // ---- Preferences panel (tab == 0) ----
    auto pref_panel = [&]() {
        Props panel_p;
        panel_p.gap = static_cast<float>(tokens::spacing_md);
        panel_p.direction = Direction::column;
        auto panel = view(std::move(panel_p));

        // Email digest toggle row.
        Props r1_p;
        r1_p.gap = static_cast<float>(tokens::spacing_md);
        r1_p.direction = Direction::row;
        r1_p.align = Align::center;
        r1_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm), 0.0f,
                                   static_cast<float>(tokens::spacing_sm), 0.0f};
        auto r1 = view(std::move(r1_p));
        Props r1t;
        r1t.text = L"Email digest";
        r1t.text_color = pal.text;
        r1t.font_size_pt = tokens::font_body;
        r1t.text_align = Align::start;
        r1t.flex_grow = 1.0f;
        r1->children.push_back(label(L"Email digest", std::move(r1t)));
        r1->children.push_back(toggle_switch(app.settings_email_digest.get(),
            [&app](bool v) { app.settings_email_digest.set(v); }));
        panel->children.push_back(std::move(r1));

        // Auto-update toggle row.
        Props r2_p;
        r2_p.gap = static_cast<float>(tokens::spacing_md);
        r2_p.direction = Direction::row;
        r2_p.align = Align::center;
        r2_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm), 0.0f,
                                   static_cast<float>(tokens::spacing_sm), 0.0f};
        auto r2 = view(std::move(r2_p));
        Props r2t;
        r2t.text = L"Auto-update";
        r2t.text_color = pal.text;
        r2t.font_size_pt = tokens::font_body;
        r2t.text_align = Align::start;
        r2t.flex_grow = 1.0f;
        r2->children.push_back(label(L"Auto-update", std::move(r2t)));
        r2->children.push_back(toggle_switch(app.settings_auto_update.get(),
            [&app](bool v) { app.settings_auto_update.set(v); }));
        panel->children.push_back(std::move(r2));

        // Language label + segmented control.
        Props lang_lbl;
        lang_lbl.text = L"Language";
        lang_lbl.text_color = pal.text_secondary;
        lang_lbl.font_size_pt = tokens::font_caption;
        lang_lbl.text_align = Align::start;
        panel->children.push_back(label(L"Language", std::move(lang_lbl)));
        panel->children.push_back(segmented_control(pal,
            {L"English", L"中文", L"日本語"},
            app.settings_language.get(),
            [&app](int v) { app.settings_language.set(v); }));

        panel->children.push_back(spacer_h(8.0f));

        // Save button.
        Props save_p = button_primary(pal);
        save_p.text = L"Save changes";
        save_p.height = static_cast<float>(tokens::control_height_md);
        save_p.on_click = [] { /* persist */ };
        panel->children.push_back(button(L"Save changes", nullptr, std::move(save_p)));
        return panel;
    };

    // ---- Account panel (tab == 1) ----
    auto account_panel = [&]() {
        Props panel_p;
        panel_p.gap = static_cast<float>(tokens::spacing_md);
        panel_p.direction = Direction::column;
        auto panel = view(std::move(panel_p));

        // Display name input.
        panel->children.push_back(label_input_row(pal, L"Display name",
            app.input_name.get(),
            [&app](std::wstring v) { app.input_name.set(std::move(v)); }));

        // Email input.
        panel->children.push_back(label_input_row(pal, L"Email",
            app.input_email.get(),
            [&app](std::wstring v) { app.input_email.set(std::move(v)); }));

        panel->children.push_back(spacer_h(8.0f));

        Props signout_p;
        signout_p.background = pal.surface;
        signout_p.hover_background = pal.surface_hover;
        signout_p.pressed_background = pal.surface_variant;
        signout_p.border_color = pal.danger;
        signout_p.border_width = 1.0f;
        signout_p.text_color = pal.danger;
        signout_p.height = static_cast<float>(tokens::control_height_md);
        signout_p.text = L"Sign out";
        signout_p.radius = static_cast<float>(tokens::radius_control);
        signout_p.on_click = [] { /* sign out */ };
        panel->children.push_back(button(L"Sign out", nullptr, std::move(signout_p)));
        return panel;
    };

    Props body_p;
    body_p.gap = static_cast<float>(tokens::spacing_md);
    body_p.direction = Direction::column;
    auto body = view(std::move(body_p));
    body->children.push_back(std::move(tab_row));
    body->children.push_back(tab == 0 ? pref_panel() : account_panel());

    return section_card(L"Settings", std::move(body), pal);
}

// ---- Mini-app: Inbox --------------------------------------------------------
//
// A list+messages layout composed of clickable rows + pagination. The list
// rows toggle a "selected" state via State<int> (message id). Pagination
// controls the page index; the selected message renders a preview pane.

namespace {
struct InboxMessage {
    int id;
    std::wstring from;
    std::wstring subject;
    std::wstring preview;
    std::wstring time;
};

const std::vector<InboxMessage>& inbox_messages() {
    static const std::vector<InboxMessage> kMessages = {
        {1, L"Ada Lovelace",     L"Notes on the Analytical Engine",
            L"I have been re-reading your work on the difference engine and wanted to share some thoughts on loops…",
            L"9:42 AM"},
        {2, L"Grace Hopper",     L"Compiler follow-up",
            L"The FLOW-MATIC draft is in good shape. One question about the macro expansion pass…",
            L"Yesterday"},
        {3, L"Alan Turing",      L"Halting problem redux",
            L"If you have a moment, I would like to revisit the proof from our last conversation. I think the diagonal can be sharpened…",
            L"Mon"},
        {4, L"Edsger Dijkstra",  L"On goto considered harmful",
            L"I have attached a draft for your review. The key argument is that the unconstrained jump obscures the program's invariants…",
            L"Sun"},
        {5, L"Margaret Hamilton",L"Apollo guidance updates",
            L"The priority displays look great. A couple of edge cases in the ring buffer I would like to walk through…",
            L"Aug 10"},
        {6, L"Donald Knuth",     L"Volume 4B notes",
            L"I made progress on the combinatorial algorithms chapter. The dancing links section needs another pass…",
            L"Aug 9"},
        {7, L"Linus Torvalds",   L"Re: kernel scheduler",
            L"Your CFS patch looks reasonable. I have one concern about the wakeup latency in the idle path…",
            L"Aug 8"},
        {8, L"Barbara Liskov",   L"Substitutability examples",
            L"I put together a small set of LSP violations worth discussing in the next reading group…",
            L"Aug 7"},
    };
    return kMessages;
}
} // namespace

ElementPtr inbox_section(App& app) {
    const Palette& pal = app.host.palette();
    const auto& all = inbox_messages();
    constexpr int kPerPage = 4;
    const int total_pages = (static_cast<int>(all.size()) + kPerPage - 1) / kPerPage;
    const int page = std::clamp(app.inbox_page.get(), 0, total_pages - 1);
    const int start = page * kPerPage;
    const int end = std::min(start + kPerPage, static_cast<int>(all.size()));

    // ---- List column ----
    Props list_p;
    list_p.gap = static_cast<float>(tokens::spacing_xs);
    list_p.direction = Direction::column;
    auto list = view(std::move(list_p));
    for (int i = start; i < end; ++i) {
        const auto& m = all[i];
        const bool is_sel = app.inbox_selected.get() == m.id;
        Props row_p;
        row_p.gap = static_cast<float>(tokens::spacing_xs);
        row_p.direction = Direction::column;
        row_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                   static_cast<float>(tokens::spacing_md),
                                   static_cast<float>(tokens::spacing_sm),
                                   static_cast<float>(tokens::spacing_md)};
        row_p.radius = static_cast<float>(tokens::radius_md);
        row_p.background = is_sel ? pal.surface_variant : pal.surface;
        row_p.border_color = is_sel ? pal.accent : pal.border;
        row_p.border_width = 1.0f;
        row_p.on_click = [&app, id = m.id] { app.inbox_selected.set(id); };
        auto row = view(std::move(row_p));

        Props top;
        top.gap = static_cast<float>(tokens::spacing_sm);
        top.direction = Direction::row;
        top.align = Align::center;
        auto top_row = view(std::move(top));
        Props from;
        from.text = m.from;
        from.text_color = pal.text;
        from.font_size_pt = tokens::font_body;
        from.bold = true;
        from.text_align = Align::start;
        from.flex_grow = 1.0f;
        top_row->children.push_back(label(m.from, std::move(from)));
        Props time;
        time.text = m.time;
        time.text_color = pal.text_secondary;
        time.font_size_pt = tokens::font_caption;
        time.text_align = Align::end;
        top_row->children.push_back(label(m.time, std::move(time)));
        row->children.push_back(std::move(top_row));

        Props subj;
        subj.text = m.subject;
        subj.text_color = pal.text;
        subj.font_size_pt = tokens::font_caption;
        subj.text_align = Align::start;
        row->children.push_back(label(m.subject, std::move(subj)));

        list->children.push_back(std::move(row));
    }

    // ---- Pagination strip ----
    auto page_strip = pagination(pal, page, total_pages,
        [&app](int p) { app.inbox_page.set(p); });

    Props body_p;
    body_p.gap = static_cast<float>(tokens::spacing_md);
    body_p.direction = Direction::column;
    auto body = view(std::move(body_p));
    body->children.push_back(std::move(list));
    body->children.push_back(std::move(page_strip));

    // ---- Preview column (selected message) ----
    Props preview_p;
    preview_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                   static_cast<float>(tokens::spacing_md),
                                   static_cast<float>(tokens::spacing_md),
                                   static_cast<float>(tokens::spacing_md)};
    preview_p.radius = static_cast<float>(tokens::radius_md);
    preview_p.background = pal.surface;
    preview_p.border_color = pal.border;
    preview_p.border_width = 1.0f;
    preview_p.gap = static_cast<float>(tokens::spacing_sm);
    preview_p.direction = Direction::column;
    auto preview = view(std::move(preview_p));
    int sel = app.inbox_selected.get();
    const InboxMessage* selected = nullptr;
    for (const auto& m : all) if (m.id == sel) { selected = &m; break; }
    if (selected != nullptr) {
        Props f;
        f.text = selected->from;
        f.text_color = pal.text;
        f.font_size_pt = tokens::font_subtitle;
        f.bold = true;
        f.text_align = Align::start;
        preview->children.push_back(label(selected->from, std::move(f)));
        Props s;
        s.text = selected->subject;
        s.text_color = pal.text_secondary;
        s.font_size_pt = tokens::font_body;
        s.text_align = Align::start;
        preview->children.push_back(label(selected->subject, std::move(s)));
        Props b;
        b.text = selected->preview;
        b.text_color = pal.text;
        b.font_size_pt = tokens::font_body;
        b.text_align = Align::start;
        preview->children.push_back(label(selected->preview, std::move(b)));
    } else {
        Props empty;
        empty.text = L"Select a message";
        empty.text_color = pal.text_secondary;
        empty.font_size_pt = tokens::font_body;
        empty.text_align = Align::center;
        preview->children.push_back(label(L"Select a message", std::move(empty)));
    }

    // Row: list | preview.
    Props row_p;
    row_p.gap = static_cast<float>(tokens::spacing_md);
    row_p.direction = Direction::row;
    row_p.align = Align::start;
    auto row = view(std::move(row_p));

    // List column gets flex_grow so it fills.
    Props listcol_p;
    listcol_p.gap = static_cast<float>(tokens::spacing_md);
    listcol_p.direction = Direction::column;
    listcol_p.flex_grow = 1.0f;
    auto list_col = view(std::move(listcol_p));
    list_col->children.push_back(std::move(body));
    row->children.push_back(std::move(list_col));

    Props previewcol_p;
    previewcol_p.gap = static_cast<float>(tokens::spacing_md);
    previewcol_p.direction = Direction::column;
    previewcol_p.width = 220.0f;
    auto preview_col = view(std::move(previewcol_p));
    preview_col->children.push_back(std::move(preview));
    row->children.push_back(std::move(preview_col));

    return section_card(L"Inbox", std::move(row), pal);
}

// ---- Mini-app: Media Player -------------------------------------------------
//
// Now-playing card with cover-art placeholder, title/artist, transport row
// (back / play-pause / forward), a progress bar driven by a slider, and a
// volume control. Uses toggle_switch for play state, slider for scrub+volume.

ElementPtr media_player_section(App& app) {
    const Palette& pal = app.host.palette();

    // ---- Cover art: a coloured square with a glyph ----
    Props art_p;
    art_p.width = 96.0f;
    art_p.height = 96.0f;
    art_p.radius = static_cast<float>(tokens::radius_lg);
    art_p.background = pal.accent;
    art_p.text = L"♫";
    art_p.text_color = pal.on_accent;
    art_p.font_size_pt = 32.0f;
    art_p.text_align = Align::center;
    art_p.enabled = false;
    auto art = label(L"♫", std::move(art_p));

    // ---- Title + artist (column) ----
    Props meta_p;
    meta_p.gap = static_cast<float>(tokens::spacing_xs);
    meta_p.direction = Direction::column;
    meta_p.flex_grow = 1.0f;
    auto meta = view(std::move(meta_p));
    Props title;
    title.text = L"Rhapsody in Blue";
    title.text_color = pal.text;
    title.font_size_pt = tokens::font_subtitle;
    title.bold = true;
    title.text_align = Align::start;
    meta->children.push_back(label(L"Rhapsody in Blue", std::move(title)));
    Props artist;
    artist.text = L"George Gershwin";
    artist.text_color = pal.text_secondary;
    artist.font_size_pt = tokens::font_caption;
    artist.text_align = Align::start;
    meta->children.push_back(label(L"George Gershwin", std::move(artist)));

    Props top_p;
    top_p.gap = static_cast<float>(tokens::spacing_md);
    top_p.direction = Direction::row;
    top_p.align = Align::center;
    auto top_row = view(std::move(top_p));
    top_row->children.push_back(std::move(art));
    top_row->children.push_back(std::move(meta));

    // ---- Transport row: back / play-pause / forward ----
    auto mk_transport = [&](const wchar_t* glyph, ClickHandler on_click) {
        Props p;
        p.width = 40.0f;
        p.height = 40.0f;
        p.radius = 20.0f;
        p.background = pal.surface;
        p.hover_background = pal.surface_hover;
        p.pressed_background = pal.surface_variant;
        p.border_color = pal.border;
        p.border_width = 1.0f;
        p.text = glyph;
        p.text_color = pal.text;
        p.font_size_pt = tokens::font_subtitle;
        p.text_align = Align::center;
        p.on_click = std::move(on_click);
        return button(glyph, nullptr, std::move(p));
    };
    Props transport_p;
    transport_p.gap = static_cast<float>(tokens::spacing_sm);
    transport_p.direction = Direction::row;
    transport_p.align = Align::center;
    auto transport = view(std::move(transport_p));
    transport->children.push_back(mk_transport(L"⏮", [] { /* prev */ }));
    const bool playing = app.media_playing.get();
    transport->children.push_back(mk_transport(playing ? L"⏸" : L"▶",
        [&app] { app.media_playing.set(!app.media_playing.get()); }));
    transport->children.push_back(mk_transport(L"⏭", [] { /* next */ }));

    // ---- Scrub slider (mock: bound to progress state) ----
    auto scrub = slider(app.progress.get(), 0.0f, 1.0f,
        [&app](float v) { app.progress.set(v); });

    // ---- Volume slider ----
    auto volume = slider(app.media_volume.get(), 0.0f, 100.0f,
        [&app](float v) { app.media_volume.set(v); });

    Props volume_p;
    volume_p.gap = static_cast<float>(tokens::spacing_sm);
    volume_p.direction = Direction::row;
    volume_p.align = Align::center;
    auto volume_row = view(std::move(volume_p));
    Props vol_lbl;
    vol_lbl.text = L"♫";
    vol_lbl.text_color = pal.text_secondary;
    vol_lbl.font_size_pt = tokens::font_body;
    vol_lbl.text_align = Align::center;
    vol_lbl.width = 24.0f;
    volume_row->children.push_back(label(L"♫", std::move(vol_lbl)));
    volume_row->children.push_back(std::move(volume));

    Props body_p;
    body_p.gap = static_cast<float>(tokens::spacing_md);
    body_p.direction = Direction::column;
    auto body = view(std::move(body_p));
    body->children.push_back(std::move(top_row));
    body->children.push_back(std::move(scrub));
    body->children.push_back(spacer_h(4.0f));
    // Center the transport row.
    Props center_p;
    center_p.direction = Direction::row;
    center_p.align = Align::center;
    auto center = view(std::move(center_p));
    center->children.push_back(std::move(transport));
    body->children.push_back(std::move(center));
    body->children.push_back(std::move(volume_row));

    return section_card(L"Media Player", std::move(body), pal);
}

// ---- Patterns (Tabs, Accordion, SegmentedControl, Pagination, Alert) -----
//
// Split into two cards so the Patterns tab can render them in a 2-column
// grid. Left card holds the navigators (tabs, accordion, segmented); right
// card holds the data-flavoured patterns (pagination, alert).

ElementPtr patterns_left_section(App& app) {
    const Palette& pal = app.host.palette();

    std::vector<TabSpec> tab_specs = {
        {L"Overview", L"A snapshot of recent activity — keep an eye on what matters."},
        {L"Activity", L"Your last 10 actions show up here, newest at the top."},
        {L"Settings", L"Tweak the things only you can see — your account, your data."},
    };
    auto tab_block = tabs(pal, tab_specs, app.active_tab.get(),
        [&app](int i) { app.active_tab.set(i); });

    std::vector<TabSpec> acc_specs = {
        {L"What is WinFlux?",      L"A single-HWND C++20 UI framework with a declarative scene tree."},
        {L"How does damage work?", L"The diff is structural; hover/press/focus feed the partial repaint."},
        {L"License?",              L"This sample is MIT — adopt the pattern, not the project."},
    };
    auto acc_block = accordion(pal, acc_specs, app.expanded_section.get(),
        [&app](int i) { app.expanded_section.set(i); });

    auto seg = segmented_control(pal,
        std::vector<std::wstring>{L"List", L"Grid", L"Compact"},
        app.view_mode.get(),
        [&app](int i) { app.view_mode.set(i); });

    Props caption_p;
    caption_p.text_color = pal.text_secondary;
    caption_p.font_size_pt = tokens::font_caption;
    caption_p.text_align = Align::start;

    auto body = col_view({
        label(L"Tabs", caption_p),
        tab_block,
        label(L"Accordion", caption_p),
        acc_block,
        label(L"Segmented control", caption_p),
        seg,
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Navigation", std::move(body), pal);
}

ElementPtr patterns_right_section(App& app) {
    const Palette& pal = app.host.palette();

    auto pag = pagination(pal, app.inbox_page.get(), 6,
        [&app](int i) { app.inbox_page.set(i); });

    auto dismiss_alert = [&app] { app.show_alert.set(false); };
    ElementPtr al;
    if (app.show_alert.get()) {
        al = alert(pal, L"info", L"This alert dismisses with the x button.", dismiss_alert);
    } else {
        Props empty_p;
        empty_p.height = 1.0f;
        al = view(std::move(empty_p));
    }

    Props caption_p;
    caption_p.text_color = pal.text_secondary;
    caption_p.font_size_pt = tokens::font_caption;
    caption_p.text_align = Align::start;

    auto body = col_view({
        label(L"Pagination", caption_p),
        pag,
        label(L"Alert (dismissible)", caption_p),
        al,
    }, static_cast<float>(tokens::spacing_md));

    return section_card(L"Status", std::move(body), pal);
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

// ---- Round 1: Motion & Effects --------------------------------------------
//
// Exercises everything the visual-foundation round added: Animated<T>
// tweens (progress + colour), gradients, elevation 1-3 shadows, the full
// 24-icon set, and the per-element cursors.

ElementPtr motion_section(App& app) {
    const Palette& pal = app.host.palette();

    // 1) Tween demo — one Animated<float> drives the progress bar, the %
    // readout, and (through a second Animated<Color>) a colour swatch.
    constexpr unsigned kTweenMs =
        static_cast<unsigned>(tokens::motion_slow_ms) * 3;   // 840 ms
    const float v = app.host.animate(app.anim_progress);
    const Color swatch = app.host.animate(app.anim_color);

    auto tween_col = col_view({
        row_view({
            button(L"Fill", [&app] {
                const Palette& p = app.host.palette();
                app.anim_progress.set(1.0f, kTweenMs, Curve::ease_in_out);
                app.anim_color.set(p.success, kTweenMs, Curve::ease_in_out);
            }, button_primary(pal)),
            button(L"Drain", [&app] {
                const Palette& p = app.host.palette();
                app.anim_progress.set(0.0f, kTweenMs, Curve::ease_in_out);
                app.anim_color.set(p.accent, kTweenMs, Curve::ease_in_out);
            }, button_secondary(pal)),
        }, static_cast<float>(tokens::spacing_sm), Align::start),
        [&] {
            Props p;
            p.height = static_cast<float>(tokens::progress_height);
            p.width = 320.0f;
            return progress(v, std::move(p));
        }(),
        [&] {
            Props p;
            p.text = std::to_wstring(static_cast<int>(v * 100.0f)) + L"% · " +
                     L"ease_in_out 840 ms";
            p.text_color = pal.text_secondary;
            p.font_size_pt = tokens::font_caption;
            p.text_align = Align::start;
            return label(p.text, p);
        }(),
        [&] {
            // Colour tween target card.
            Props p;
            p.background = swatch;
            p.radius = static_cast<float>(tokens::radius_md);
            p.width = 320.0f;
            p.height = 24.0f;
            return view(std::move(p));
        }(),
    }, static_cast<float>(tokens::spacing_sm));

    // 2) Gradient + elevation cards.
    auto elevation_card = [&](const wchar_t* caption, int elevation,
                              std::optional<Gradient> gradient) {
        Props p;
        p.radius = static_cast<float>(tokens::radius_card);
        p.width = 150.0f;
        p.height = 56.0f;
        p.shadow_elevation = elevation;
        p.text = caption;
        p.text_color = pal.text;
        p.font_size_pt = tokens::font_caption;
        if (gradient.has_value()) {
            p.background_gradient = gradient;
            p.text_color = pal.on_accent;
        } else {
            p.background = pal.surface;
        }
        return label(p.text, p);
    };
    auto elev_row = row_view({
        elevation_card(L"elevation 1", 1, std::nullopt),
        elevation_card(L"elevation 2", 2, std::nullopt),
        elevation_card(L"elevation 3", 3, std::nullopt),
        elevation_card(L"gradient e2", 2, Gradient{pal.accent, pal.info}),
    }, static_cast<float>(tokens::spacing_md), Align::start);

    // 3) Icon grid — all 24 IconKind values, 6 per row.
    constexpr IconKind kAll[] = {
        IconKind::chevron_down, IconKind::chevron_up, IconKind::chevron_left,
        IconKind::chevron_right, IconKind::close, IconKind::check,
        IconKind::search, IconKind::plus, IconKind::minus, IconKind::hamburger,
        IconKind::user, IconKind::trash, IconKind::edit, IconKind::arrow_right,
        IconKind::calendar, IconKind::inbox, IconKind::play, IconKind::pause,
        IconKind::skip_prev, IconKind::skip_next, IconKind::info,
        IconKind::success, IconKind::warning, IconKind::danger,
    };
    constexpr int kPerRow = 6;
    constexpr int kCount = static_cast<int>(std::size(kAll));
    auto icon_grid = view();
    for (int r = 0; r < (kCount + kPerRow - 1) / kPerRow; ++r) {
        auto row = view();
        for (int c = 0; c < kPerRow; ++c) {
            const int idx = r * kPerRow + c;
            if (idx >= kCount) break;
            Props ip;
            ip.text_color = pal.text_secondary;
            ip.hover_background = pal.surface_hover;
            ip.radius = static_cast<float>(tokens::radius_md);
            row->children.push_back(icon(kAll[idx], std::move(ip)));
        }
        row->props.direction = Direction::row;
        row->props.gap = static_cast<float>(tokens::spacing_md);
        icon_grid->children.push_back(std::move(row));
    }
    icon_grid->props.direction = Direction::column;
    icon_grid->props.gap = static_cast<float>(tokens::spacing_sm);

    // 4) Cursor sampler — buttons already show the hand; these opt into
    // the rarer shapes.
    auto cursor_row = row_view({
        [&] {
            Props p = button_secondary(pal);
            p.cursor = Cursor::cross;
            return button(L"cross", [] {}, std::move(p));
        }(),
        [&] {
            Props p = button_secondary(pal);
            p.cursor = Cursor::size_ns;
            return button(L"resize ns", [] {}, std::move(p));
        }(),
        [&] {
            Props p = button_secondary(pal);
            p.cursor = Cursor::ibeam;
            return button(L"ibeam", [] {}, std::move(p));
        }(),
    }, static_cast<float>(tokens::spacing_sm), Align::start);

    auto body = col_view({
        std::move(tween_col),
        std::move(elev_row),
        std::move(icon_grid),
        std::move(cursor_row),
    }, static_cast<float>(tokens::spacing_md));
    return section_card(L"Motion & Effects", std::move(body), pal);
}

// ---- Round 2: Scroll & Layer ------------------------------------------------
//
// Exercises the scroll_view element (30 rows, wheel/bar/keys) and the
// declarative overlay channel: a real scrim+centred modal (Esc / click-
// outside close), a corner toast stack with auto-expiry, and a popup
// anchored below the last click point.

ElementPtr scroll_layer_section(App& app) {
    const Palette& pal = app.host.palette();

    // Long list inside a fixed 360-px viewport.
    auto rows = view();
    for (int i = 1; i <= 30; ++i) {
        Props rp;
        rp.direction = Direction::row;
        rp.align = Align::center;
        rp.gap = static_cast<float>(tokens::spacing_sm);
        rp.padding = EdgeInsets{6.0f, 6.0f, 6.0f, 6.0f};
        rp.background = (i % 2 == 0) ? pal.surface_variant : pal.surface;
        rp.radius = static_cast<float>(tokens::radius_sm);
        auto row = view(std::move(rp));
        Props np;
        np.text = L"Row " + std::to_wstring(i) + L" of 30 — scroll me";
        np.text_color = pal.text;
        np.font_size_pt = tokens::font_body;
        np.text_align = Align::start;
        row->children.push_back(label(np.text, np));
        rows->children.push_back(std::move(row));
    }
    Props rows_p;
    rows_p.direction = Direction::column;
    rows_p.gap = static_cast<float>(tokens::spacing_xs);
    rows = with_children(view(std::move(rows_p)), {std::move(rows)});
    Props sv_p;
    sv_p.height = 360.0f;
    sv_p.border_color = pal.border;
    sv_p.border_width = 1.0f;
    sv_p.radius = static_cast<float>(tokens::radius_md);
    sv_p.padding = EdgeInsets{6.0f, 6.0f, 6.0f, 6.0f};
    sv_p.flex_grow = 1.0f;
    auto sv = scroll_view(std::move(rows), std::move(sv_p));

    // Overlay triggers.
    auto triggers = row_view({
        button(L"Open modal", [&app] { app.layer_modal_open.set(true); },
              button_primary(pal)),
        button(L"Popup at click", [&app] { app.layer_popup_open.set(true); },
              button_secondary(pal)),
        button(L"Corner toast", [&app] {
            const int seq = app.next_layer_toast_seq++;
            app.layer_toasts.push_back({GetTickCount64() + 2200,
                                        L"Toast " + std::to_wstring(seq) +
                                            L" — auto-expires"});
            app.host.request_render();
        }, button_secondary(pal)),
    }, static_cast<float>(tokens::spacing_sm), Align::start);

    auto body = col_view({
        std::move(triggers),
        std::move(sv),
    }, static_cast<float>(tokens::spacing_md));
    body->props.align = Align::stretch;
    return section_card(L"Scroll & Layer", std::move(body), pal);
}

// Overlay roots declared from build() every frame (declarative lifetime).
void declare_layer_overlays(App& app) {
    Host& host = app.host;
    const Palette& pal = host.palette();

    // 1) Modal: full-canvas scrim + centred card. Scrim click / Esc run the
    //    root's on_click (close).
    if (app.layer_modal_open.get()) {
        Props scrim;
        scrim.background = rgba(0, 0, 0, 96);
        scrim.direction = Direction::column;
        scrim.justify = Justify::center;
        scrim.align = Align::center;
        scrim.on_click = [&app] { app.layer_modal_open.set(false); };
        auto root = view(std::move(scrim));

        Props card_p;
        card_p.background = pal.surface;
        card_p.radius = static_cast<float>(tokens::radius_lg);
        card_p.width = 360.0f;
        card_p.direction = Direction::column;
        card_p.gap = static_cast<float>(tokens::spacing_md);
        card_p.padding = pad_all(static_cast<float>(tokens::spacing_lg));
        card_p.shadow_elevation = 3;
        auto card = view(std::move(card_p));

        Props title_p;
        title_p.text = L"A real modal";
        title_p.text_color = pal.text;
        title_p.font_size_pt = tokens::font_title;
        title_p.text_align = Align::start;
        card->children.push_back(label(title_p.text, title_p));

        Props body_p;
        body_p.text = L"Scrim blocks the page, Esc / clicking outside closes.";
        body_p.text_color = pal.text_secondary;
        body_p.font_size_pt = tokens::font_body;
        body_p.text_align = Align::start;
        card->children.push_back(label(body_p.text, body_p));

        card->children.push_back(button(L"Close",
            [&app] { app.layer_modal_open.set(false); }, button_primary(pal)));

        root->children.push_back(std::move(card));
        host.overlay(std::move(root));
    }

    // 2) Anchored popup below the last click point (transparent root =
    //    click-through outside the menu; click closes via root on_click).
    if (app.layer_popup_open.get()) {
        const auto [cx, cy] = host.last_click_pos();
        Props root_p;
        root_p.direction = Direction::column;
        root_p.on_click = [&app] { app.layer_popup_open.set(false); };
        auto root = view(std::move(root_p));

        Props gap_p;
        gap_p.height = cy + 4.0f;
        root->children.push_back(view(std::move(gap_p)));

        Props row_p;
        row_p.direction = Direction::row;
        auto row = view(std::move(row_p));
        Props indent_p;
        indent_p.width = cx;
        row->children.push_back(view(std::move(indent_p)));

        Props menu_p;
        menu_p.background = pal.surface;
        menu_p.border_color = pal.border;
        menu_p.border_width = 1.0f;
        menu_p.radius = static_cast<float>(tokens::radius_md);
        menu_p.direction = Direction::column;
        menu_p.padding = pad_all(static_cast<float>(tokens::spacing_sm));
        menu_p.shadow_elevation = 2;
        auto menu = view(std::move(menu_p));
        const wchar_t* items[] = {L"Cut", L"Copy", L"Paste", L"— divider —"};
        for (const wchar_t* it : items) {
            Props ip;
            ip.text = it;
            ip.text_color = pal.text;
            ip.font_size_pt = tokens::font_body;
            ip.text_align = Align::start;
            ip.height = 26.0f;
            menu->children.push_back(label(ip.text, ip));
        }
        row->children.push_back(std::move(menu));
        root->children.push_back(std::move(row));
        host.overlay(std::move(root));
    }

    // 3) Corner toasts: drop expired entries, keep the clock armed until
    //    the next deadline, show the rest bottom-right.
    const unsigned long long now = GetTickCount64();
    unsigned long long next_deadline = 0;
    size_t w = 0;
    for (size_t i = 0; i < app.layer_toasts.size(); ++i) {
        if (app.layer_toasts[i].deadline_ms > now) {
            app.layer_toasts[w++] = app.layer_toasts[i];
            if (next_deadline == 0 || app.layer_toasts[i].deadline_ms < next_deadline)
                next_deadline = app.layer_toasts[i].deadline_ms;
        }
    }
    if (w < app.layer_toasts.size()) {
        app.layer_toasts.resize(w);
        host.request_render();
    }
    if (next_deadline != 0) {
        host.animate_for(static_cast<unsigned>(next_deadline - now) + 2u);
    }
    if (!app.layer_toasts.empty()) {
        Props root_p;
        root_p.direction = Direction::column;
        root_p.justify = Justify::end;
        root_p.align = Align::end;
        root_p.gap = static_cast<float>(tokens::spacing_sm);
        root_p.padding = pad_all(static_cast<float>(tokens::spacing_lg));
        auto root = view(std::move(root_p));
        for (const auto& t : app.layer_toasts) {
            Props tp;
            tp.background = pal.surface;
            tp.border_color = pal.border;
            tp.border_width = 1.0f;
            tp.radius = static_cast<float>(tokens::radius_md);
            tp.padding = EdgeInsets{10.0f, 10.0f, 10.0f, 10.0f};
            tp.text = t.text;
            tp.text_color = pal.text;
            tp.font_size_pt = tokens::font_caption;
            tp.text_align = Align::start;
            tp.shadow_elevation = 2;
            root->children.push_back(label(tp.text, tp));
        }
        host.overlay(std::move(root));
    }
}

// ---- Root ------------------------------------------------------------------

// Top-level category tab bar. A thin wrapper over segmented_control so the
// four tab labels read as the "section navigation" for the body below.
ElementPtr tab_bar(App& app) {
    const Palette& pal = app.host.palette();
    auto bar = segmented_control(pal,
        {L"Form controls", L"Pickers", L"Patterns", L"Overlays",
         L"Data viz", L"Mini-apps", L"Motion", L"Scroll"},
        app.gallery_tab.get(),
        [&app](int i) { app.gallery_tab.set(i); });
    return bar;
}

// ---- Phase F: Data viz (Calendar / Bar chart / Avatar group / Breadcrumb) --
//
// Four mainstream "data display" widgets composed entirely out of the
// existing primitives — no new element types, just views, labels, and the
// state+on_click machinery.

namespace {

// Shared month name array (also used by date_picker).
inline constexpr const wchar_t* kMonthNames[] = {
    L"January", L"February", L"March", L"April", L"May", L"June",
    L"July", L"August", L"September", L"October", L"November", L"December"};

} // namespace

// ---- Calendar ---------------------------------------------------------------
//
// A self-contained month grid: header (prev / month-name / next), weekday
// labels, then 6 weeks of 7 day cells. Cells in adjacent months render in
// text_secondary; the selected cell highlights with pal.accent; clicking
// any cell updates app.cal_selected and (for prev/next month days) switches
// the active month.

ElementPtr calendar(App& app, const Palette& pal) {
    const int year  = app.cal_year.get();
    const int month = app.cal_month.get();
    const int sel   = app.cal_selected.get();

    const auto move_month = [&app](int delta) {
        int m = app.cal_month.get() + delta;
        int y = app.cal_year.get();
        while (m < 0)  { m += 12; y -= 1; }
        while (m > 11) { m -= 12; y += 1; }
        app.cal_month.set(m);
        app.cal_year.set(y);
    };

    // Header row: [◀]  [Month Year (flex_grow=1)]  [▶]
    Props header_p;
    header_p.direction = Direction::row;
    header_p.align = Align::center;
    header_p.gap = static_cast<float>(tokens::spacing_md);
    header_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm)};
    auto header = view(std::move(header_p));

    Props prev_p = button_secondary(pal);
    prev_p.text = L"◀";
    prev_p.width = 28.0f;
    prev_p.height = 28.0f;
    prev_p.padding = EdgeInsets{0, 0, 0, 0};
    {
        auto mm = move_month;
        prev_p.on_click = [mm] { mm(-1); };
    }
    header->children.push_back(button(L"◀", prev_p.on_click, std::move(prev_p)));

    Props title_p;
    title_p.text  = std::wstring(kMonthNames[month]) + L" " + std::to_wstring(year);
    title_p.font_size_pt = tokens::font_subtitle;
    title_p.text_color = pal.text;
    title_p.text_align = Align::center;
    title_p.flex_grow = 1.0f;
    auto title_text = title_p.text;
    header->children.push_back(label(title_text, std::move(title_p)));

    Props next_p = button_secondary(pal);
    next_p.text = L"▶";
    next_p.width = 28.0f;
    next_p.height = 28.0f;
    next_p.padding = EdgeInsets{0, 0, 0, 0};
    {
        auto mm = move_month;
        next_p.on_click = [mm] { mm(+1); };
    }
    header->children.push_back(button(L"▶", next_p.on_click, std::move(next_p)));

    // Weekday labels row.
    Props dow_p;
    dow_p.direction = Direction::row;
    dow_p.gap = static_cast<float>(tokens::spacing_xs);
    dow_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs)};
    auto dow_row = view(std::move(dow_p));
    static const wchar_t* kDow[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
    for (int i = 0; i < 7; ++i) {
        Props d;
        d.text  = kDow[i];
        d.text_color = pal.text_secondary;
        d.font_size_pt = tokens::font_caption;
        d.text_align = Align::center;
        d.width = 32.0f;
        d.height = 24.0f;
        auto d_text = d.text;
        dow_row->children.push_back(label(d_text, std::move(d)));
    }

    // Day grid: 6 weeks × 7 days. The first week may start with days from
    // the previous month; the last week may end with days from the next.
    Props grid_p;
    grid_p.direction = Direction::column;
    grid_p.gap = static_cast<float>(tokens::spacing_xs);
    grid_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs),
        static_cast<float>(tokens::spacing_xs)};
    auto grid = view(std::move(grid_p));

    const int first_dow = first_weekday(year, month);
    const int dim = days_in_month(year, month);
    const int prev_dim = days_in_month(month == 0 ? year - 1 : year,
                                      month == 0 ? 11 : month - 1);

    int day = 1 - first_dow;     // signed day-of-month counter
    for (int week = 0; week < 6; ++week) {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.gap = static_cast<float>(tokens::spacing_xs);
        auto row = view(std::move(row_p));
        for (int col = 0; col < 7; ++col, ++day) {
            const bool is_prev = (day < 1);
            const bool is_next = (day > dim);
            int display = day;
            int midx = month;
            if (is_prev) { display = prev_dim + day; midx = month == 0 ? 11 : month - 1; }
            if (is_next) { display = day - dim;      midx = month == 11 ? 0 : month + 1; }

            const bool is_sel = (midx == month && display == sel);
            const bool is_today = (year == 2026 && month == 7 && display == 14);

            Props cell_p;
            cell_p.width  = 32.0f;
            cell_p.height = 32.0f;
            cell_p.radius = 16.0f;
            cell_p.text = std::to_wstring(display);
            cell_p.font_size_pt = tokens::font_caption;
            cell_p.text_align = Align::center;
            cell_p.text_color = is_prev || is_next ? pal.text_secondary : pal.text;
            if (is_sel) {
                cell_p.background = pal.accent;
                cell_p.text_color = pal.on_accent;
            } else if (is_today) {
                cell_p.border_color = pal.accent;
                cell_p.border_width = 1.0f;
            }
            cell_p.hover_background = is_sel ? std::optional<Color>{pal.accent_hover}
                                             : std::optional<Color>{pal.surface_hover};
            const int captured_day = display;
            const int captured_midx = midx;
            cell_p.on_click = [&app, captured_day, captured_midx] {
                if (captured_midx >= 0 && captured_midx <= 11) {
                    app.cal_month.set(captured_midx);
                    app.cal_selected.set(captured_day);
                }
            };
            // Extract text into a local first so it isn't read from a
            // moved-from Props during the argument evaluation of label().
            auto cell_text = cell_p.text;
            row->children.push_back(label(cell_text, std::move(cell_p)));
        }
        grid->children.push_back(std::move(row));
    }

    Props cal_p;
    cal_p.direction = Direction::column;
    cal_p.gap = static_cast<float>(tokens::spacing_xs);
    cal_p.background = pal.surface;
    cal_p.border_color = pal.border;
    cal_p.border_width = 1.0f;
    cal_p.radius = static_cast<float>(tokens::radius_card);
    cal_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm),
        static_cast<float>(tokens::spacing_sm)};
    auto cal = view(std::move(cal_p));
    cal->children.push_back(std::move(header));
    cal->children.push_back(std::move(dow_row));
    cal->children.push_back(std::move(grid));
    return cal;
}

// ---- Bar chart --------------------------------------------------------------
//
// One bar column per (label, value) pair. The bar height is proportional to
// value / max_value; columns align at the bottom (the baseline) so the chart
// "reads" naturally regardless of which bar is tallest.

ElementPtr bar_chart(const Palette& pal,
                     const std::vector<std::wstring>& labels,
                     const std::vector<float>& values,
                     float max_value) {
    constexpr float kBarWidth      = 24.0f;
    constexpr float kBarGap        = 8.0f;
    constexpr float kMaxBarHeight  = 140.0f;

    Props row_p;
    row_p.direction = Direction::row;
    row_p.gap = kBarGap;
    row_p.align = Align::end;
    row_p.flex_grow = 1.0f;
    auto row = view(std::move(row_p));

    for (size_t i = 0; i < labels.size(); ++i) {
        const float v = values[i];
        const float h = std::max(2.0f, (v / max_value) * kMaxBarHeight);

        Props col_p;
        col_p.direction = Direction::column;
        col_p.align = Align::center;
        col_p.gap = 4.0f;
        col_p.flex_grow = 1.0f;
        auto col = view(std::move(col_p));

        // Value above the bar.
        Props val_p;
        val_p.text  = std::to_wstring(static_cast<int>(v));
        val_p.font_size_pt = tokens::font_caption;
        val_p.text_color = pal.text_secondary;
        val_p.text_align = Align::center;
        auto val_text = val_p.text;
        col->children.push_back(label(val_text, std::move(val_p)));

        // Spacer pushes the bar to the bottom of the column.
        Props spacer_p;
        spacer_p.height = kMaxBarHeight - h;
        col->children.push_back(view(std::move(spacer_p)));

        // Bar itself.
        Props bar_p;
        bar_p.background = pal.accent;
        bar_p.width  = kBarWidth;
        bar_p.height = h;
        bar_p.radius = static_cast<float>(tokens::radius_sm);
        col->children.push_back(view(std::move(bar_p)));

        // Label below.
        Props lab_p;
        lab_p.text  = labels[i];
        lab_p.font_size_pt = tokens::font_caption;
        lab_p.text_color = pal.text;
        lab_p.text_align = Align::center;
        auto lab_text = lab_p.text;
        col->children.push_back(label(lab_text, std::move(lab_p)));

        row->children.push_back(std::move(col));
    }

    Props chart_p;
    chart_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md)};
    chart_p.align = Align::stretch;
    auto chart = view(std::move(chart_p));
    chart->children.push_back(std::move(row));
    return chart;
}

// One avatar circle: a 32×32 view with the initial letter centered.
ElementPtr avatar_circle(const Palette& pal, const std::wstring& initial,
                         Color bg, bool bordered = true) {
    Props outer_p;
    outer_p.background = bg;
    outer_p.width  = 32.0f;
    outer_p.height = 32.0f;
    outer_p.radius = 16.0f;
    outer_p.border_color = pal.surface;
    outer_p.border_width = bordered ? 2.0f : 0.0f;
    outer_p.align = Align::center;
    auto outer = view(std::move(outer_p));

    Props txt_p;
    txt_p.text  = initial;
    txt_p.text_color = pal.on_accent;
    txt_p.font_size_pt = tokens::font_caption;
    txt_p.text_align = Align::center;
    auto txt_text = txt_p.text;
    outer->children.push_back(label(txt_text, std::move(txt_p)));
    return outer;
}

// A row of avatar circles with negative margin for overlap. If there are
// more than max_visible initials, the last visible slot is replaced by a
// "+N" indicator.
ElementPtr avatar_group(const Palette& pal,
                        const std::vector<Color>& backgrounds,
                        const std::vector<std::wstring>& initials,
                        int max_visible = 4) {
    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    auto row = view(std::move(row_p));

    const int visible = std::min(max_visible, static_cast<int>(initials.size()));
    const int overflow = static_cast<int>(initials.size()) - visible;

    for (int i = 0; i < visible; ++i) {
        auto av = avatar_circle(pal, initials[i], backgrounds[i], true);
        if (i > 0) {
            // Negative margin pulls each subsequent avatar leftward so the
            // circles overlap. Layout reserves margin_main space but the
            // size is clamped to kMinSize, so the row ends up the width of
            // one avatar plus (n-1)*overlap.
            av->props.margin.left = -12.0f;
        }
        row->children.push_back(std::move(av));
    }
    if (overflow > 0) {
        Props more_p;
        more_p.background = pal.surface_variant;
        more_p.text_color = pal.text_secondary;
        more_p.text = L"+" + std::to_wstring(overflow);
        more_p.width  = 32.0f;
        more_p.height = 32.0f;
        more_p.radius = 16.0f;
        more_p.border_color = pal.surface;
        more_p.border_width = 2.0f;
        more_p.font_size_pt = tokens::font_caption;
        more_p.text_align = Align::center;
        more_p.align = Align::center;
        auto more = view(std::move(more_p));
        auto more_text = more_p.text;
        more->children.push_back(label(more_text, std::move(more_p)));
        more->props.margin.left = -12.0f;
        row->children.push_back(std::move(more));
    }
    return row;
}

// Breadcrumb: items separated by › chevrons. All but the last are
// clickable (calls on_select with the index).
ElementPtr breadcrumbs(const Palette& pal,
                       const std::vector<std::wstring>& items,
                       std::function<void(int)> on_select) {
    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    row_p.gap = static_cast<float>(tokens::spacing_xs);
    auto row = view(std::move(row_p));

    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            Props sep_p;
            sep_p.text  = L"›";
            sep_p.text_color = pal.text_secondary;
            sep_p.font_size_pt = tokens::font_body;
            sep_p.text_align = Align::center;
            auto sep_text = sep_p.text;
            row->children.push_back(label(sep_text, std::move(sep_p)));
        }
        const bool is_last = (i + 1 == items.size());
        Props p;
        p.text  = items[i];
        p.text_color = is_last ? pal.text : pal.accent;
        p.font_size_pt = tokens::font_body;
        p.text_align = Align::center;
        p.padding = EdgeInsets{
            static_cast<float>(tokens::spacing_xs),
            static_cast<float>(tokens::spacing_xs),
            static_cast<float>(tokens::spacing_xs),
            static_cast<float>(tokens::spacing_xs)};
        if (!is_last) {
            p.hover_background = pal.surface_variant;
            p.radius = static_cast<float>(tokens::radius_sm);
            const int idx = static_cast<int>(i);
            p.on_click = [on_select, idx] { on_select(idx); };
        }
        auto item_text = p.text;
        row->children.push_back(label(item_text, std::move(p)));
    }
    return row;
}

// ---- Data viz section -------------------------------------------------------
//
// Two side-by-side cards, each holding a different mix of the widgets above.
// Left card:  Calendar + Bar chart (date-picker + activity over time).
// Right card: Avatar group + Breadcrumb (identity + path navigation).
//
// Cards split 50/50 via flex_grow=1; each card's inner row aligns the
// intrinsically-narrow widget to the left and lets the wider one
// (chart, breadcrumb) flex-grow to fill the remaining width.
ElementPtr data_viz_section(App& app) {
    const Palette& pal = app.host.palette();

    // ---- Calendar ----
    auto cal_block = calendar(app, pal);

    // ---- Bar chart ----
    std::vector<std::wstring> week_labels = {
        L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"};
    std::vector<float> week_values = {30.0f, 65.0f, 45.0f, 80.0f, 55.0f, 90.0f, 50.0f};
    auto chart_block = bar_chart(pal, week_labels, week_values, 100.0f);

    Props chart_wrapper_p;
    chart_wrapper_p.background = pal.surface;
    chart_wrapper_p.border_color = pal.border;
    chart_wrapper_p.border_width = 1.0f;
    chart_wrapper_p.radius = static_cast<float>(tokens::radius_card);
    chart_wrapper_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md)};
    // Stretch horizontally so the chart block fills the wrapper.
    chart_wrapper_p.align = Align::stretch;
    auto chart_wrapper = view(std::move(chart_wrapper_p));
    {
        Props chart_title_p;
        chart_title_p.text = L"Daily activity (last 7 days)";
        chart_title_p.text_color = pal.text;
        chart_title_p.font_size_pt = tokens::font_subtitle;
        chart_title_p.text_align = Align::start;
        chart_title_p.padding = EdgeInsets{
            0, 0, static_cast<float>(tokens::spacing_md), 0};
        auto chart_title_text = chart_title_p.text;
        chart_wrapper->children.push_back(label(chart_title_text, std::move(chart_title_p)));
        chart_wrapper->children.push_back(std::move(chart_block));
    }

    // ---- Avatar group ----
    std::vector<Color> team_colors = {pal.accent, pal.success, pal.warning,
                                      pal.info, pal.danger};
    std::vector<std::wstring> team_initials = {L"AM", L"BJ", L"CK", L"DL", L"EM"};
    auto avatars_block = avatar_group(pal, team_colors, team_initials, 4);

    Props avatar_wrapper_p;
    avatar_wrapper_p.background = pal.surface;
    avatar_wrapper_p.border_color = pal.border;
    avatar_wrapper_p.border_width = 1.0f;
    avatar_wrapper_p.radius = static_cast<float>(tokens::radius_card);
    avatar_wrapper_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md)};
    avatar_wrapper_p.direction = Direction::column;
    avatar_wrapper_p.gap = static_cast<float>(tokens::spacing_sm);
    avatar_wrapper_p.align = Align::stretch;
    auto avatar_wrapper = view(std::move(avatar_wrapper_p));
    {
        Props av_title_p;
        av_title_p.text = L"Team members (5 total, 4 visible)";
        av_title_p.text_color = pal.text;
        av_title_p.font_size_pt = tokens::font_subtitle;
        av_title_p.text_align = Align::start;
        auto av_title_text = av_title_p.text;
        avatar_wrapper->children.push_back(label(av_title_text, std::move(av_title_p)));
        avatar_wrapper->children.push_back(std::move(avatars_block));
    }

    // ---- Breadcrumb ----
    auto breadcrumb_block = breadcrumbs(pal,
        {L"Home", L"Projects", L"WinFlux", L"Gallery"},
        [](int) {});

    Props crumb_wrapper_p;
    crumb_wrapper_p.background = pal.surface;
    crumb_wrapper_p.border_color = pal.border;
    crumb_wrapper_p.border_width = 1.0f;
    crumb_wrapper_p.radius = static_cast<float>(tokens::radius_card);
    crumb_wrapper_p.padding = EdgeInsets{
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md),
        static_cast<float>(tokens::spacing_md)};
    crumb_wrapper_p.align = Align::stretch;
    auto crumb_wrapper = view(std::move(crumb_wrapper_p));
    {
        Props crumb_title_p;
        crumb_title_p.text = L"Breadcrumb (path navigation)";
        crumb_title_p.text_color = pal.text;
        crumb_title_p.font_size_pt = tokens::font_subtitle;
        crumb_title_p.text_align = Align::start;
        crumb_title_p.padding = EdgeInsets{
            0, 0, static_cast<float>(tokens::spacing_md), 0};
        auto crumb_title_text = crumb_title_p.text;
        crumb_wrapper->children.push_back(label(crumb_title_text, std::move(crumb_title_p)));
        crumb_wrapper->children.push_back(std::move(breadcrumb_block));
    }

    // Two-column layout inside each card: narrow intrinsic widget left,
    // flex-grow widget right. Earlier the columns had direction:column
    // with align:start, which left the intrinsically-narrow widgets flush-
    // left and the rest of the 900px-wide card empty.
    Props left_col_p;
    left_col_p.direction = Direction::row;
    left_col_p.gap = static_cast<float>(tokens::spacing_lg);
    left_col_p.align = Align::start;
    auto left_col = view(std::move(left_col_p));
    left_col->children.push_back(std::move(cal_block));
    {
        // Chart wrapper sits on the right and grows to fill the remaining
        // width. Inside it, the bars (each flex_grow=1) share that width
        // evenly.
        auto cw = std::move(chart_wrapper);
        cw->props.flex_grow = 1.0f;
        left_col->children.push_back(std::move(cw));
    }

    Props right_col_p;
    right_col_p.direction = Direction::row;
    right_col_p.gap = static_cast<float>(tokens::spacing_lg);
    right_col_p.align = Align::start;
    auto right_col = view(std::move(right_col_p));
    {
        // Avatar wrapper is intrinsically narrow (4×32 px with negative
        // margin overlap ≈ 76 px). Keep it on the left.
        auto aw = std::move(avatar_wrapper);
        right_col->children.push_back(std::move(aw));
    }
    {
        // Breadcrumb wrapper fills the remaining width so the path
        // navigation stretches to the card edge.
        auto bw = std::move(crumb_wrapper);
        bw->props.flex_grow = 1.0f;
        right_col->children.push_back(std::move(bw));
    }

    Props body_p;
    body_p.direction = Direction::row;
    body_p.gap = static_cast<float>(tokens::spacing_lg);
    // align:stretch so wrap_left / wrap_right fill the card row's cross
    // axis (vertical) instead of collapsing to intrinsic height.
    body_p.align = Align::stretch;
    auto body = view(std::move(body_p));
    auto wrap_left  = section_card(L"Date picker",         std::move(left_col),  pal);
    auto wrap_right = section_card(L"Identity & navigation", std::move(right_col), pal);
    // Stretch both cards to a 50/50 split across the body width.
    wrap_left->props.flex_grow  = 1.0f;
    wrap_right->props.flex_grow = 1.0f;
    // section_card creates a column with align:start; override to stretch
    // so the row's flex_grow children (chart, breadcrumb) actually fill
    // the card width.
    wrap_left->props.align  = Align::stretch;
    wrap_right->props.align = Align::stretch;
    body->children.push_back(std::move(wrap_left));
    body->children.push_back(std::move(wrap_right));
    return body;
}

// Build the body for the currently-active gallery tab. Each tab is its own
// 2-column grid; the section set was chosen so every card fits inside a
// 1000-px window without scrolling.
ElementPtr build_body(App& app) {
    Props body_p;
    body_p.direction = Direction::row;
    // align:stretch so the row's children fill the body's cross axis
    // (vertical). Without this, dv in the Data viz tab (case 4) collapses
    // to its intrinsic height (~194 px) and leaves a wide empty band below.
    body_p.align = Align::stretch;
    body_p.gap = static_cast<float>(tokens::spacing_lg);
    auto body = view(std::move(body_p));
    // Without flex_grow, flex_layout would size the body to the sum of its
    // two columns' intrinsic widths — which leaves a wide empty band on the
    // right once the window is wider than the content. Stretch the body
    // across the root, then split it 50/50 between the two columns below.
    body->props.flex_grow = 1.0f;

    auto mk_col = [&](std::vector<ElementPtr> children) {
        auto c = col_view(std::move(children),
                          static_cast<float>(tokens::spacing_lg), Align::stretch);
        c->props.flex_grow = 1.0f;
        return c;
    };

    ElementPtr left_col;
    ElementPtr right_col;
    switch (app.gallery_tab.get()) {
    case 0:   // Form controls — primitive element types
        left_col = mk_col({
            buttons_section(app),
            checkboxes_section(app),
            progress_section(app),
        });
        right_col = mk_col({
            inputs_section(app),
            containers_section(app),
        });
        break;
    case 1:   // Pickers — composite controls (radio / colour / date)
        left_col = mk_col({
            pickers_left_section(app),
        });
        right_col = mk_col({
            pickers_right_section(app),
        });
        break;
    case 2:   // Patterns — navigation + interaction patterns
        left_col = mk_col({
            patterns_left_section(app),
        });
        right_col = mk_col({
            patterns_right_section(app),
        });
        break;
    case 3:   // Overlays — Toast / Modal / Tooltip / Badge (Phase E)
        left_col = mk_col({
            overlays_section(app),
        });
        right_col = mk_col({});
        break;
    case 4:   // Data viz — Calendar / Bar chart / Avatar group / Breadcrumb
        left_col = mk_col({});
        right_col = mk_col({});
        // Single full-width body, not 2-column. Replace with the unified
        // data_viz_section output and bypass the column layout entirely.
        body->children.clear();
        {
            auto dv = data_viz_section(app);
            // Without flex_grow=1, the data_viz_section collapses to its
            // intrinsic width (the two cards' intrinsic widths side-by-side),
            // leaving a wide empty band on the right. Setting flex_grow=1
            // makes it fill the body, and the two cards inside (each
            // flex_grow=1) split the resulting width 50/50.
            dv->props.flex_grow = 1.0f;
            body->children.push_back(std::move(dv));
        }
        return body;
    case 5:   // Mini-apps — Settings, Inbox, Media Player
        left_col = mk_col({
            settings_section(app),
            inbox_section(app),
        });
        right_col = mk_col({
            media_player_section(app),
        });
        break;
    case 6:   // Motion — tweens / gradients / shadows / icons / cursors
        left_col = mk_col({});
        right_col = mk_col({});
        body->children.clear();
        {
            auto mv = motion_section(app);
            mv->props.flex_grow = 1.0f;
            body->children.push_back(std::move(mv));
        }
        return body;
    case 7:   // Scroll & Layer — scroll_view + overlay channel
        left_col = mk_col({});
        right_col = mk_col({});
        body->children.clear();
        {
            auto sv = scroll_layer_section(app);
            sv->props.flex_grow = 1.0f;
            body->children.push_back(std::move(sv));
        }
        return body;
    default:
        break;
    }
    body->children.push_back(std::move(left_col));
    body->children.push_back(std::move(right_col));
    return body;
}

ElementPtr build(App& app) {
    const Palette& pal = app.host.palette();

    Props root_p;
    root_p.direction = Direction::column;
    root_p.padding = pad_all(static_cast<float>(tokens::spacing_lg));
    root_p.gap = static_cast<float>(tokens::spacing_lg);
    root_p.background = pal.background;
    // Cross-axis stretch so the tab bar and the 2-column body both span
    // the full window width. Without this each child sizes to its own
    // intrinsic width and the tab segments end up squished in the
    // upper-left of the page.
    root_p.align = Align::stretch;

    auto parent = view(std::move(root_p));
    parent->children.push_back(header(app));
    parent->children.push_back(tab_bar(app));
    parent->children.push_back(build_body(app));
    parent->children.push_back(footer(app));
    // Overlay roots are re-declared every frame while their state is on.
    declare_layer_overlays(app);
    return parent;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int) {
    enable_per_monitor_dpi_v2();

    // Optional initial gallery tab from the command line (e.g.
    // `WinFluxGallery.exe 6` opens on Motion). Falls back to Form controls
    // for any non-numeric / out-of-range argument.
    int initial_tab = 0;
    if (cmd_line != nullptr && *cmd_line != L'\0') {
        try {
            const int parsed = std::wcstol(cmd_line, nullptr, 10);
            if (parsed >= 0 && parsed <= 7) initial_tab = parsed;
        } catch (...) {
            initial_tab = 0;
        }
    }

    // Query the work area (excludes the taskbar + docked app bars). The
    // Gallery originally hard-coded 1200x1100, which overflowed the screen
    // on smaller monitors and covered the taskbar. We shrink that to
    // work-area minus a 24-px margin on every side, with a 1000-px height
    // cap so a single 1080p-tall monitor still leaves headroom. The cap
    // matters: a 4K display has a work area taller than the content needs,
    // and an oversized window just means the cards float in the middle of
    // a sea of background.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int phys_w = work.right - work.left;
    const int phys_h = work.bottom - work.top;
    constexpr int kMargin = 24;
    constexpr int kMaxHeight = 1000;
    constexpr int kMinWidth  = 900;
    constexpr int kMinHeight = 600;

    App app;
    app.count             = app.host.make_state<int>(0);
    app.enable_animations = app.host.make_state<bool>(true);
    app.enable_sound      = app.host.make_state<bool>(false);
    app.progress          = app.host.make_state<float>(0.35f);
    app.input_name = app.host.make_state<std::wstring>(L"");
    app.input_email = app.host.make_state<std::wstring>(L"");
    app.volume = app.host.make_state<float>(40.0f);
    app.dark_mode = app.host.make_state<bool>(false);
    app.theme_pick = app.host.make_state<int>(1);
    app.accent_pick = app.host.make_state<int>(2);
    app.birthday_year  = app.host.make_state<int>(2026);
    app.birthday_month = app.host.make_state<int>(8);   // September (0-indexed)
    app.birthday_day   = app.host.make_state<int>(14);
    app.active_tab       = app.host.make_state<int>(0);
    app.expanded_section = app.host.make_state<int>(0);
    app.view_mode        = app.host.make_state<int>(0);
    app.inbox_page       = app.host.make_state<int>(0);
    app.show_alert       = app.host.make_state<bool>(true);
    app.settings_email_digest = app.host.make_state<bool>(true);
    app.settings_auto_update  = app.host.make_state<bool>(false);
    app.settings_timezone  = app.host.make_state<int>(0);
    app.settings_language  = app.host.make_state<int>(0);
    app.inbox_selected     = app.host.make_state<int>(1);
    app.media_playing      = app.host.make_state<bool>(false);
    app.media_volume       = app.host.make_state<float>(60.0f);
    app.toasts             = app.host.make_state<std::vector<ToastEntry>>({
        {0, L"New message in your inbox.",    L"info"},
        {1, L"Profile saved.",                 L"success"},
        {2, L"Storage almost full.",           L"warning"},
        {3, L"Failed to delete file.",         L"danger"},
    });
    app.next_toast_id      = 4;
    app.modal_open         = app.host.make_state<bool>(false);
    app.tooltip_visible    = app.host.make_state<bool>(false);
    app.tooltip_hint       = app.host.make_state<std::wstring>(L"");
    app.cal_year     = app.host.make_state<int>(2026);
    app.cal_month    = app.host.make_state<int>(7);     // August (0-indexed)
    app.cal_selected = app.host.make_state<int>(14);
    app.gallery_tab  = app.host.make_state<int>(initial_tab);
    app.layer_modal_open = app.host.make_state<bool>(false);
    app.layer_popup_open = app.host.make_state<bool>(false);
    app.host.set_root_builder([&app] { return build(app); });

    // Screenshot-driven verification seeds (Round 2, temporary): a trailing
    // "seed" argument pre-opens the layer demos so no synthetic input is
    // needed to capture them.
    if (cmd_line != nullptr && wcsstr(cmd_line, L"seed") != nullptr) {
        app.layer_toasts.push_back({GetTickCount64() + 60000,
                                    L"Startup toast — seeded"});
        app.layer_modal_open.set(true);
    }

    Host::Config cfg;
    cfg.title = L"WinFlux Gallery";
    cfg.width  = std::clamp(phys_w - 2 * kMargin, kMinWidth,  4096);
    cfg.height = std::clamp(std::min(phys_h - 2 * kMargin, kMaxHeight),
                            kMinHeight, 4096);
    if (!app.host.create(cfg, instance)) {
        MessageBoxW(nullptr, L"WinFlux host creation failed (Direct2D unavailable?).",
                    L"WinFlux", MB_ICONERROR);
        return 1;
    }
    return app.host.run();
}