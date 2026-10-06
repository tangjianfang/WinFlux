// flux::ui pickers — radio group, colour swatch grid, and date picker,
// promoted from the Gallery sample (parameterized; month math exported for
// tests).

#include <flux/Components.hpp>

#include <algorithm>

namespace flux::ui {

int days_in_month(int year, int month) noexcept {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 1) {   // February
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month];
}

int first_weekday(int year, int month) noexcept {
    // Zeller's congruence — returns 0=Sunday..6=Saturday.
    int m = month + 1;
    int y = year;
    if (m <= 2) { m += 12; y -= 1; }
    const int k = y % 100;
    const int j = y / 100;
    const int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    return (h + 6) % 7;
}

ElementPtr radio_group(const Palette& pal, std::vector<std::wstring> options,
                       int selected, std::function<void(int)> on_change, Props p) {
    Props col_p = std::move(p);
    col_p.gap = static_cast<float>(tokens::spacing_xs);
    col_p.direction = Direction::column;
    auto col = view(std::move(col_p));
    for (size_t i = 0; i < options.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_sel = idx == selected;

        Props row_p;
        row_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_xs),
                                   static_cast<float>(tokens::spacing_xs),
                                   static_cast<float>(tokens::spacing_xs),
                                   static_cast<float>(tokens::spacing_xs)};
        row_p.gap = static_cast<float>(tokens::spacing_sm);
        row_p.direction = Direction::row;
        row_p.align = Align::center;
        row_p.radius = static_cast<float>(tokens::radius_sm);
        row_p.focusable = true;   // keyboard-reachable options (review 2026-10-06 X-2)
        row_p.hover_background = pal.surface_hover;
        row_p.cursor = Cursor::hand;
        auto handler = [on_change, idx] { if (on_change) on_change(idx); };
        row_p.on_click = handler;
        auto row = view(std::move(row_p));

        Props circle_p;
        circle_p.width = 16.0f;
        circle_p.height = 16.0f;
        circle_p.radius = 8.0f;
        circle_p.border_color = is_sel ? pal.accent : pal.border;
        circle_p.border_width = 1.5f;
        if (is_sel) circle_p.background = pal.accent;
        row->children.push_back(view(std::move(circle_p)));

        Props lbl_p;
        lbl_p.text_color = pal.text;
        lbl_p.font_size_pt = tokens::font_body;
        lbl_p.text_align = Align::start;
        row->children.push_back(label(options[i], std::move(lbl_p)));
        col->children.push_back(std::move(row));
    }
    return col;
}

ElementPtr color_picker(const Palette& pal, std::vector<Color> swatches, int selected,
                        std::function<void(int)> on_change, int per_row, Props p) {
    Props outer_p = std::move(p);
    outer_p.gap = static_cast<float>(tokens::spacing_sm);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));
    for (size_t i = 0; i < swatches.size(); i += static_cast<size_t>(per_row)) {
        Props row_p;
        row_p.gap = static_cast<float>(tokens::spacing_sm);
        row_p.direction = Direction::row;
        auto row = view(std::move(row_p));
        for (size_t j = i;
             j < std::min(i + static_cast<size_t>(per_row), swatches.size()); ++j) {
            const int idx = static_cast<int>(j);
            const bool is_sel = idx == selected;
            Props sw_p;
            sw_p.width = 36.0f;
            sw_p.height = 36.0f;
            sw_p.radius = static_cast<float>(tokens::radius_md);
            sw_p.background = swatches[j];
            sw_p.border_color = is_sel ? pal.accent : pal.border;
            sw_p.border_width = is_sel ? 2.5f : 1.0f;
            sw_p.tooltip_text = L"Swatch " + std::to_wstring(idx + 1);
            sw_p.focusable = true;   // keyboard-reachable swatches (review 2026-10-06 X-2)
            sw_p.cursor = Cursor::hand;
            sw_p.on_click = [on_change, idx] { if (on_change) on_change(idx); };
            row->children.push_back(view(std::move(sw_p)));
        }
        outer->children.push_back(std::move(row));
    }
    return outer;
}

ElementPtr date_picker(const Palette& pal, int year, int month, int day,
                       std::function<void(int, int, int)> on_change, Props p) {
    static const wchar_t* month_names[] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"};

    Props outer_p = std::move(p);
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    Props header_p;
    header_p.text_color = pal.text;
    header_p.font_size_pt = tokens::font_subtitle;
    header_p.bold = true;
    header_p.text_align = Align::start;
    const auto header_text =
        std::wstring(month_names[std::clamp(month, 0, 11)]) + L" " + std::to_wstring(year);
    outer->children.push_back(label(header_text, std::move(header_p)));

    static const wchar_t* dow[] = {L"S", L"M", L"T", L"W", L"T", L"F", L"S"};
    Props dow_row_p;
    dow_row_p.gap = static_cast<float>(tokens::spacing_xs);
    dow_row_p.direction = Direction::row;
    auto dow_row = view(std::move(dow_row_p));
    for (const wchar_t* d : dow) {
        Props dp;
        dp.width = 28.0f;
        dp.height = 22.0f;
        dp.text_color = pal.text_secondary;
        dp.font_size_pt = tokens::font_caption;
        dp.bold = true;
        dp.text_align = Align::center;
        dow_row->children.push_back(label(d, std::move(dp)));
    }
    outer->children.push_back(std::move(dow_row));

    const int first = first_weekday(year, month);
    const int total = days_in_month(year, month);
    int cell_day = 1 - first;
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
                cell_p.enabled = false;
            } else {
                const bool is_sel = cell_day == day;
                cell_p.text_color = is_sel ? pal.on_accent : pal.text;
                cell_p.background = is_sel ? std::optional<Color>{pal.accent} : std::nullopt;
                const int captured = cell_day;
                cell_p.focusable = true;   // keyboard-reachable days (review 2026-10-06 X-2)
                cell_p.cursor = Cursor::hand;
                cell_p.on_click = [year, month, captured, on_change] {
                    if (on_change) on_change(year, month, captured);
                };
            }
            const auto txt = in_month ? std::to_wstring(cell_day) : std::wstring();
            row->children.push_back(label(txt, std::move(cell_p)));
        }
        outer->children.push_back(std::move(row));
    }
    return outer;
}

} // namespace flux::ui
