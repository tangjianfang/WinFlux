// flux::ui navigation components — tabs / segmented / pagination /
// accordion, promoted from the Gallery sample and parameterized (no App&
// dependency; state flows in via arguments, changes flow out via callbacks).

#include <flux/Components.hpp>

#include <algorithm>

namespace flux::ui {

ElementPtr tabs(const Palette& pal, std::vector<std::wstring> labels, int active,
                std::function<void(int)> on_change, Props p) {
    Props outer_p = std::move(p);
    outer_p.gap = 0.0f;
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    Props strip_p;
    strip_p.gap = 0.0f;
    strip_p.direction = Direction::row;
    strip_p.align = Align::stretch;
    auto strip = view(std::move(strip_p));
    for (size_t i = 0; i < labels.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_active = idx == active;
        Props tab_p;
        tab_p.flex_grow = 1.0f;
        tab_p.height = 36.0f;
        tab_p.text_color = is_active ? pal.accent : pal.text_secondary;
        tab_p.font_size_pt = tokens::font_body;
        tab_p.bold = is_active;
        tab_p.text_align = Align::center;
        tab_p.focusable = true;   // keyboard-reachable (review 2026-10-06 X-2)
        tab_p.cursor = Cursor::hand;
        tab_p.hover_background = pal.surface_hover;
        tab_p.on_click = [on_change, idx] { if (on_change) on_change(idx); };
        strip->children.push_back(label(labels[i], std::move(tab_p)));
    }
    outer->children.push_back(std::move(strip));

    // Active-tab underline + hairline divider.
    Props div_p;
    div_p.height = 2.0f;
    div_p.direction = Direction::row;
    auto div = view(std::move(div_p));
    for (size_t i = 0; i < labels.size(); ++i) {
        Props seg_p;
        seg_p.flex_grow = 1.0f;
        seg_p.height = 2.0f;
        seg_p.background = static_cast<int>(i) == active ? pal.accent : pal.divider;
        div->children.push_back(view(std::move(seg_p)));
    }
    outer->children.push_back(std::move(div));
    return outer;
}

ElementPtr segmented(const Palette& pal, std::vector<std::wstring> options,
                     int active, std::function<void(int)> on_change, Props p) {
    Props outer_p = std::move(p);
    outer_p.gap = 0.0f;
    outer_p.direction = Direction::row;
    outer_p.radius = static_cast<float>(tokens::radius_control);
    outer_p.border_color = pal.border;
    outer_p.border_width = 1.0f;
    outer_p.align = Align::stretch;
    auto outer = view(std::move(outer_p));

    for (size_t i = 0; i < options.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_sel = idx == active;
        Props seg_p;
        seg_p.flex_grow = 1.0f;
        seg_p.height = static_cast<float>(tokens::control_height_sm);
        seg_p.text_color = is_sel ? pal.on_accent : pal.text;
        seg_p.background = is_sel ? std::optional<Color>{pal.accent} : std::nullopt;
        seg_p.font_size_pt = tokens::font_caption;
        seg_p.text_align = Align::center;
        seg_p.focusable = true;   // keyboard-reachable (review 2026-10-06 X-2)
        seg_p.cursor = Cursor::hand;
        seg_p.on_click = [on_change, idx] { if (on_change) on_change(idx); };
        outer->children.push_back(label(options[i], std::move(seg_p)));
    }
    return outer;
}

ElementPtr pagination(const Palette& pal, int current, int total_pages,
                      std::function<void(int)> on_change, Props p) {
    if (total_pages < 1) total_pages = 1;
    current = std::clamp(current, 0, total_pages - 1);

    Props outer_p = std::move(p);
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::row;
    outer_p.align = Align::center;
    auto outer = view(std::move(outer_p));

    auto mk_btn = [&](bool enabled, int next, IconKind icon_kind) {
        Props bp;
        bp.width = 28.0f;
        bp.height = 28.0f;
        bp.radius = static_cast<float>(tokens::radius_sm);
        bp.icon = icon_kind;
        bp.text_color = enabled ? pal.text : pal.text_secondary;
        bp.enabled = enabled;
        bp.focusable = enabled;   // keyboard-reachable (review 2026-10-06 X-2)
        bp.cursor = Cursor::hand;
        bp.hover_background = pal.surface_hover;
        bp.on_click = [on_change, next] { if (on_change) on_change(next); };
        return flux::icon(icon_kind, std::move(bp));
    };
    outer->children.push_back(mk_btn(current > 0, current - 1, IconKind::chevron_left));
    for (int pg = 0; pg < total_pages; ++pg) {
        const bool is_cur = pg == current;
        Props pg_p;
        pg_p.width = 28.0f;
        pg_p.height = 28.0f;
        pg_p.radius = static_cast<float>(tokens::radius_sm);
        pg_p.text_color = is_cur ? pal.on_accent : pal.text;
        pg_p.background = is_cur ? std::optional<Color>{pal.accent} : std::nullopt;
        pg_p.font_size_pt = tokens::font_caption;
        pg_p.text_align = Align::center;
        pg_p.focusable = true;   // keyboard-reachable (review 2026-10-06 X-2)
        pg_p.cursor = Cursor::hand;
        pg_p.on_click = [on_change, pg] { if (on_change) on_change(pg); };
        outer->children.push_back(label(std::to_wstring(pg + 1), std::move(pg_p)));
    }
    outer->children.push_back(mk_btn(current < total_pages - 1, current + 1,
                                     IconKind::chevron_right));
    return outer;
}

ElementPtr accordion(const Palette& pal, std::vector<AccordionSection> sections,
                     int open_index, std::function<void(int)> on_toggle, Props p) {
    Props outer_p = std::move(p);
    outer_p.gap = static_cast<float>(tokens::spacing_xs);
    outer_p.direction = Direction::column;
    auto outer = view(std::move(outer_p));

    for (size_t i = 0; i < sections.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_open = idx == open_index;

        Props item_p;
        item_p.direction = Direction::column;
        item_p.radius = static_cast<float>(tokens::radius_md);
        item_p.border_color = pal.border;
        item_p.border_width = 1.0f;
        item_p.background = pal.surface;
        auto item = view(std::move(item_p));

        Props header_p;
        header_p.gap = static_cast<float>(tokens::spacing_sm);
        header_p.direction = Direction::row;
        header_p.align = Align::center;
        header_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md),
                                      static_cast<float>(tokens::spacing_md)};
        header_p.cursor = Cursor::hand;
        header_p.focusable = true;   // keyboard-reachable (review 2026-10-06 X-2)
        header_p.hover_background = pal.surface_hover;
        header_p.on_click = [on_toggle, idx, is_open] {
            if (on_toggle) on_toggle(is_open ? -1 : idx);
        };
        auto header = view(std::move(header_p));

        Props title_p;
        title_p.text_color = pal.text;
        title_p.font_size_pt = tokens::font_body;
        title_p.bold = true;
        title_p.text_align = Align::start;
        title_p.flex_grow = 1.0f;
        header->children.push_back(label(sections[i].title, std::move(title_p)));

        Props chev_p;
        chev_p.width = 18.0f;
        chev_p.height = 18.0f;
        chev_p.icon = is_open ? IconKind::chevron_up : IconKind::chevron_down;
        chev_p.text_color = pal.text_secondary;
        header->children.push_back(flux::icon(*chev_p.icon, std::move(chev_p)));

        item->children.push_back(std::move(header));

        if (is_open && sections[i].body != nullptr) {
            Props body_p;
            body_p.padding = EdgeInsets{0.0f, static_cast<float>(tokens::spacing_md),
                                        static_cast<float>(tokens::spacing_md),
                                        static_cast<float>(tokens::spacing_md)};
            body_p.text_color = pal.text_secondary;
            body_p.font_size_pt = tokens::font_body;
            body_p.text_align = Align::start;
            sections[i].body->props.padding = body_p.padding;
            item->children.push_back(std::move(sections[i].body));
        }
        outer->children.push_back(std::move(item));
    }
    return outer;
}

} // namespace flux::ui
