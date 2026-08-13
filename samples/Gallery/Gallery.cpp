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
    // Top-level gallery navigation. The window can't fit every section
    // (twelve cards at full padding overflow the work area on most
    // monitors), so the body switches between four category tabs. Each
    // tab holds a small enough set to fit a 1000-px-tall window.
    //   0 = Form controls, 1 = Pickers, 2 = Patterns, 3 = Mini-apps
    State<int> gallery_tab;
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
            row->children.push_back(label(cell_p.text, std::move(cell_p)));
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
        header->children.push_back(label(chev_p.text, std::move(chev_p)));

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
        outer->children.push_back(label(pg_p.text, std::move(pg_p)));
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
    row->children.push_back(label(x_p.text, std::move(x_p)));

    return row;
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

// ---- Root ------------------------------------------------------------------

// Top-level category tab bar. A thin wrapper over segmented_control so the
// four tab labels read as the "section navigation" for the body below.
ElementPtr tab_bar(App& app) {
    const Palette& pal = app.host.palette();
    auto bar = segmented_control(pal,
        {L"Form controls", L"Pickers", L"Patterns", L"Mini-apps"},
        app.gallery_tab.get(),
        [&app](int i) { app.gallery_tab.set(i); });
    // The segmented_control is intrinsic-width by default; without this,
    // the four segments sit squished in the upper-left of the root column
    // instead of spanning the full window width.
    bar->props.flex_grow = 1.0f;
    return bar;
}

// Build the body for the currently-active gallery tab. Each tab is its own
// 2-column grid; the section set was chosen so every card fits inside a
// 1000-px window without scrolling.
ElementPtr build_body(App& app) {
    Props body_p;
    body_p.direction = Direction::row;
    body_p.align = Align::start;
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
    case 3:   // Mini-apps — Settings, Inbox, Media Player
    default:
        left_col = mk_col({
            settings_section(app),
            inbox_section(app),
        });
        right_col = mk_col({
            media_player_section(app),
        });
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
    return parent;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enable_per_monitor_dpi_v2();

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
    app.gallery_tab        = app.host.make_state<int>(0);
    app.host.set_root_builder([&app] { return build(app); });

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