// flux::ui data viz extras — bar chart, avatar group, breadcrumbs, and a
// parameterized month calendar, promoted from the Gallery sample.

#include <flux/Components.hpp>

#include <algorithm>

namespace flux::ui {

ElementPtr bar_chart(const Palette& pal, const std::vector<std::wstring>& labels,
                     const std::vector<float>& values, float max_value, Props p) {
    constexpr float kBarWidth = 24.0f;
    constexpr float kBarGap = 8.0f;
    constexpr float kMaxBarHeight = 140.0f;

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

        Props val_p;
        val_p.text = std::to_wstring(static_cast<int>(v));
        val_p.font_size_pt = tokens::font_caption;
        val_p.text_color = pal.text_secondary;
        val_p.text_align = Align::center;
        col->children.push_back(label(val_p.text, val_p));

        Props spacer_p;
        spacer_p.height = kMaxBarHeight - h;
        col->children.push_back(view(std::move(spacer_p)));

        Props bar_p;
        bar_p.background = pal.accent;
        bar_p.width = kBarWidth;
        bar_p.height = h;
        bar_p.radius = static_cast<float>(tokens::radius_sm);
        col->children.push_back(view(std::move(bar_p)));

        Props lab_p;
        lab_p.text = labels[i];
        lab_p.font_size_pt = tokens::font_caption;
        lab_p.text_color = pal.text;
        lab_p.text_align = Align::center;
        col->children.push_back(label(lab_p.text, lab_p));

        row->children.push_back(std::move(col));
    }

    Props chart_p = std::move(p);
    chart_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md)};
    chart_p.align = Align::stretch;
    auto chart = view(std::move(chart_p));
    chart->children.push_back(std::move(row));
    return chart;
}

ElementPtr avatar_group(const Palette& pal, const std::vector<Color>& backgrounds,
                        const std::vector<std::wstring>& initials, int max_visible,
                        Props) {
    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    auto row = view(std::move(row_p));

    const auto avatar = [&](const std::wstring& initial, Color bg,
                            const std::wstring& text, Color text_color, Color bkg) {
        Props outer_p;
        outer_p.background = bg;
        outer_p.width = 32.0f;
        outer_p.height = 32.0f;
        outer_p.radius = 16.0f;
        outer_p.border_color = pal.surface;
        outer_p.border_width = 2.0f;
        auto outer = view(std::move(outer_p));
        Props txt_p;
        txt_p.text = text.empty() ? initial : text;
        txt_p.text_color = text_color;
        txt_p.font_size_pt = tokens::font_caption;
        txt_p.text_align = Align::center;
        outer->children.push_back(label(txt_p.text, txt_p));
        (void)bkg;
        return outer;
    };

    const int visible = std::min(max_visible, static_cast<int>(initials.size()));
    const int overflow = static_cast<int>(initials.size()) - visible;
    for (int i = 0; i < visible; ++i) {
        auto av = avatar(initials[static_cast<size_t>(i)],
                         backgrounds[static_cast<size_t>(i)], L"", pal.on_accent, {});
        if (i > 0) av->props.margin.left = -12.0f;   // overlap
        row->children.push_back(std::move(av));
    }
    if (overflow > 0) {
        Props more_p;
        more_p.background = pal.surface_variant;
        const auto more_txt = L"+" + std::to_wstring(overflow);
        auto more = avatar(L"", {}, more_txt, pal.text_secondary, {});
        more->props.margin.left = -12.0f;
        row->children.push_back(std::move(more));
        (void)more_p;
    }
    return row;
}

ElementPtr breadcrumbs(const Palette& pal, const std::vector<std::wstring>& items,
                       std::function<void(int)> on_select, Props) {
    Props row_p;
    row_p.direction = Direction::row;
    row_p.align = Align::center;
    row_p.gap = static_cast<float>(tokens::spacing_xs);
    auto row = view(std::move(row_p));

    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            row->children.push_back(flux::icon(IconKind::chevron_right, [&] {
                Props sep_p;
                sep_p.width = 14.0f;
                sep_p.height = 14.0f;
                sep_p.text_color = pal.text_secondary;
                return sep_p;
            }()));
        }
        const bool is_last = i + 1 == items.size();
        Props ip;
        ip.text = items[i];
        ip.text_color = is_last ? pal.text : pal.accent;
        ip.font_size_pt = tokens::font_body;
        ip.text_align = Align::center;
        ip.padding = EdgeInsets{static_cast<float>(tokens::spacing_xs),
                                static_cast<float>(tokens::spacing_xs),
                                static_cast<float>(tokens::spacing_xs),
                                static_cast<float>(tokens::spacing_xs)};
        if (!is_last) {
            ip.hover_background = pal.surface_variant;
            ip.radius = static_cast<float>(tokens::radius_sm);
            const int idx = static_cast<int>(i);
            ip.on_click = [on_select, idx] { if (on_select) on_select(idx); };
        }
        row->children.push_back(label(ip.text, ip));
    }
    return row;
}

ElementPtr calendar(const Palette& pal, int year, int month, int selected_day,
                    std::function<void(int)> on_month_delta,
                    std::function<void(int)> on_select_day, Props) {
    Props outer_p;
    outer_p.direction = Direction::column;
    outer_p.gap = static_cast<float>(tokens::spacing_sm);
    auto outer = view(std::move(outer_p));

    // Header: [prev] Month Year [next]
    Props header_p;
    header_p.direction = Direction::row;
    header_p.align = Align::center;
    header_p.gap = static_cast<float>(tokens::spacing_md);
    header_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                  static_cast<float>(tokens::spacing_sm),
                                  static_cast<float>(tokens::spacing_sm),
                                  static_cast<float>(tokens::spacing_sm)};
    auto header = view(std::move(header_p));

    auto nav_btn = [&](IconKind kind, int delta, const wchar_t* tip) {
        Props p;
        p.icon = kind;
        p.width = 28.0f;
        p.height = 28.0f;
        p.text_color = pal.text_secondary;
        p.tooltip_text = tip;
        p.on_click = [on_month_delta, delta] {
            if (on_month_delta) on_month_delta(delta);
        };
        return flux::button(L"", p.on_click, std::move(p));
    };
    header->children.push_back(nav_btn(IconKind::chevron_left, -1, L"Previous month"));
    header->children.push_back(nav_btn(IconKind::chevron_right, 1, L"Next month"));

    // The month body reuses the date picker grid (its header/weekday row
    // included); selection + month navigation wire through the callbacks.
    Props body_p;
    outer->children.push_back(header);
    outer->children.push_back(date_picker(pal, year, month, selected_day,
                                          [on_select_day](int, int, int d) {
                                              if (on_select_day) on_select_day(d);
                                          }));
    (void)body_p;
    return outer;
}

} // namespace flux::ui
