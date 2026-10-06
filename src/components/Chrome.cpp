// App-chrome builders (review 2026-10-06 F23): the four shapes every tool
// app assembles by hand — a bottom status bar, an icon toolbar, loading
// skeletons, and empty states. Pure builders; nothing here needs a host.

#include <flux/Components.hpp>

namespace flux::ui {

ElementPtr status_bar(const Palette& pal, std::vector<std::wstring> sections, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::row;
    root_p.align = Align::center;
    root_p.gap = static_cast<float>(tokens::spacing_lg);
    root_p.height = static_cast<float>(tokens::control_height_sm);
    root_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md), 0.0f, 0.0f,
                                static_cast<float>(tokens::spacing_md)};
    root_p.background = pal.surface_variant;
    auto root = view(std::move(root_p));
    for (size_t i = 0; i < sections.size(); ++i) {
        Props sp;
        sp.text = std::move(sections[i]);
        sp.text_color = pal.text_secondary;
        sp.font_size_pt = tokens::font_caption;
        sp.text_align = Align::start;
        if (i == 0) sp.flex_grow = 1.0f;   // first section stretches, rest right-pack
        root->children.push_back(label(sp.text, std::move(sp)));
    }
    return root;
}

ElementPtr toolbar(const Palette& pal, std::vector<ToolbarAction> actions, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::row;
    root_p.align = Align::center;
    root_p.gap = static_cast<float>(tokens::spacing_xs);
    auto root = view(std::move(root_p));
    for (auto& a : actions) {
        // Move the handler out before moving the action (R-9 pattern).
        auto on_click = std::move(a.on_click);
        root->children.push_back(
            icon_button(pal, a.icon, a.tooltip, std::move(on_click)));
    }
    return root;
}

ElementPtr skeleton(const Palette& pal, int lines, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::column;
    root_p.gap = static_cast<float>(tokens::spacing_sm);
    auto root = view(std::move(root_p));
    for (int i = 0; i < lines; ++i) {
        Props lp;
        lp.height = 12.0f;
        lp.radius = static_cast<float>(tokens::radius_sm);
        lp.background = pal.surface_variant;
        // Staircase widths read as "text lines", not bars.
        lp.flex_grow = 1.0f;
        lp.width = (i == lines - 1) ? 180.0f : std::optional<float>{};
        root->children.push_back(view(std::move(lp)));
    }
    return root;
}

ElementPtr empty_state(const Palette& pal, IconKind kind, std::wstring title,
                       std::wstring hint, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::column;
    root_p.align = Align::center;
    root_p.justify = Justify::center;
    root_p.gap = static_cast<float>(tokens::spacing_sm);
    root_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_xl),
                                static_cast<float>(tokens::spacing_xl),
                                static_cast<float>(tokens::spacing_xl),
                                static_cast<float>(tokens::spacing_xl)};
    auto root = view(std::move(root_p));

    Props ip;
    ip.width = 40.0f;
    ip.height = 40.0f;
    ip.icon = kind;
    ip.text_color = pal.text_secondary;
    root->children.push_back(flux::icon(kind, std::move(ip)));

    Props tp;
    tp.text = std::move(title);
    tp.text_color = pal.text;
    tp.font_size_pt = tokens::font_subtitle;
    tp.bold = true;
    root->children.push_back(label(tp.text, std::move(tp)));

    if (!hint.empty()) {
        Props hp;
        hp.text = std::move(hint);
        hp.text_color = pal.text_secondary;
        hp.font_size_pt = tokens::font_body;
        hp.text_align = Align::center;
        root->children.push_back(label(hp.text, std::move(hp)));
    }
    return root;
}

} // namespace flux::ui
