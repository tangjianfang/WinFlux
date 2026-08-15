// flux::ui layout containers — splitter (draggable two-pane), drawer
// (right-edge slide-in overlay), sidenav (collapsible icon rail).

#include <flux/Components.hpp>

#include <algorithm>

namespace flux::ui {

namespace {

ElementPtr make_drag_handle(const Palette& pal, bool vertical,
                            std::function<void(float)> on_value) {
    Props p;
    // Main-axis thickness only — the cross axis stretches with the parent.
    if (vertical) p.height = 6.0f;
    else p.width = 6.0f;
    p.flex_grow = 0.0f;
    p.background = std::nullopt;
    p.hover_background = pal.surface_hover;
    p.cursor = vertical ? Cursor::size_ns : Cursor::size_we;
    p.tooltip_text = L"Drag to resize";
    auto handle = std::make_shared<Element>();
    handle->type = L"drag_handle";
    handle->props = std::move(p);
    handle->props.on_value_change = std::move(on_value);
    return handle;
}

} // namespace

ElementPtr splitter(const Palette& pal, ElementPtr a, ElementPtr b, float ratio,
                    std::function<void(float)> on_ratio, bool vertical) {
    ratio = std::clamp(ratio, 0.05f, 0.95f);

    Props root_p;
    root_p.direction = vertical ? Direction::column : Direction::row;
    root_p.align = Align::stretch;
    auto root = view(std::move(root_p));

    if (a != nullptr) {
        a->props.flex_grow = ratio;
        root->children.push_back(std::move(a));
    }
    root->children.push_back(make_drag_handle(pal, vertical, on_ratio));
    if (b != nullptr) {
        b->props.flex_grow = 1.0f - ratio;
        root->children.push_back(std::move(b));
    }
    return root;
}

// ---- Drawer -----------------------------------------------------------------

void drawer_open(DrawerHost& dh) noexcept {
    dh.open = true;
    dh.slide.set(1.0f, static_cast<unsigned>(tokens::motion_slow_ms), Curve::ease_out);
}

void drawer_close(DrawerHost& dh) noexcept {
    dh.open = false;
    dh.slide.set(0.0f, static_cast<unsigned>(tokens::motion_medium_ms), Curve::ease_in_out);
}

void overlay_drawer(Host& host, DrawerHost& dh, const Palette& pal,
                    ElementPtr content) noexcept {
    // The slide tween keeps running while closing so the overlay stays
    // declared until the panel has fully left the canvas.
    const float t = host.animate(dh.slide);
    if (!dh.open && !dh.slide.active_at(Animated<float>::now())) return;

    Props root_p;
    root_p.direction = Direction::row;
    root_p.justify = Justify::end;
    root_p.align = Align::stretch;
    root_p.on_click = [&dh] { drawer_close(dh); };   // click-outside closes
    auto root = view(std::move(root_p));

    Props panel_p;
    panel_p.width = dh.width;
    panel_p.background = pal.surface;
    panel_p.border_color = pal.border;
    panel_p.border_width = 1.0f;
    panel_p.direction = Direction::column;
    panel_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_md)};
    panel_p.gap = static_cast<float>(tokens::spacing_md);
    panel_p.shadow_elevation = 3;
    // Slide-in: right margin shrinks from full width to 0.
    panel_p.margin = EdgeInsets{0.0f, 0.0f, dh.width * (1.0f - t), 0.0f};
    auto panel = view(std::move(panel_p));
    if (content != nullptr) panel->children.push_back(std::move(content));

    // Header row with a close chevron.
    Props close_p;
    close_p.icon = IconKind::close;
    close_p.width = 28.0f;
    close_p.height = 28.0f;
    close_p.text_color = pal.text_secondary;
    close_p.tooltip_text = L"Close (Esc)";
    panel->children.insert(panel->children.begin(),
                           flux::icon(IconKind::close, std::move(close_p)));

    root->children.push_back(std::move(panel));
    host.overlay(std::move(root));
}

// ---- SideNav ------------------------------------------------------------------

ElementPtr sidenav(const Palette& pal, std::vector<NavEntry> entries, int active,
                   std::function<void(int)> on_change, bool collapsed,
                   std::function<void()> on_toggle) {
    Props root_p;
    root_p.direction = Direction::column;
    root_p.width = collapsed ? 56.0f : 200.0f;
    root_p.gap = 2.0f;
    root_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm), 0.0f, 0.0f, 0.0f};
    root_p.background = pal.surface;
    root_p.border_color = pal.border;
    root_p.border_width = 1.0f;
    root_p.align = Align::start;
    auto root = view(std::move(root_p));

    // Collapse toggle.
    Props tog_p;
    tog_p.icon = collapsed ? IconKind::hamburger : IconKind::chevron_left;
    tog_p.width = 40.0f;
    tog_p.height = 40.0f;
    tog_p.text_color = pal.text_secondary;
    tog_p.tooltip_text = collapsed ? L"Expand navigation" : L"Collapse navigation";
    root->children.push_back(flux::button(L"", on_toggle, std::move(tog_p)));

    for (size_t i = 0; i < entries.size(); ++i) {
        const int idx = static_cast<int>(i);
        const bool is_active = idx == active;
        Props ip;
        ip.icon = entries[i].icon;
        ip.text = collapsed ? L"" : entries[i].label;
        ip.text_color = is_active ? pal.on_accent : pal.text;
        ip.background = is_active ? std::optional<Color>{pal.accent} : std::nullopt;
        ip.hover_background = is_active ? std::nullopt : std::optional<Color>{pal.surface_hover};
        ip.radius = static_cast<float>(tokens::radius_control);
        ip.height = 40.0f;
        ip.width = collapsed ? 40.0f : 188.0f;
        ip.font_size_pt = tokens::font_body;
        ip.text_align = Align::start;
        ip.tooltip_text = entries[i].label;
        ip.on_click = [on_change, idx] { if (on_change) on_change(idx); };
        root->children.push_back(
            flux::button(collapsed ? L"" : entries[i].label, ip.on_click, ip));
    }
    return root;
}

} // namespace flux::ui
